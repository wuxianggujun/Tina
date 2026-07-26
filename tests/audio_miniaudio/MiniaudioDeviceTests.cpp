#include <tina/audio/miniaudio/MiniaudioDevice.hpp>

#include <tina/audio/AudioErrors.hpp>
#include <tina/core/memory/CountingMemoryResource.hpp>
#include <tina/core/memory/MemoryTracker.hpp>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <thread>

namespace Tina::Audio {
namespace {

template <typename Result>
void expectFailureCode(const Result& result, Core::ErrorCode expectedCode)
{
    EXPECT_FALSE(result);
    if (!result)
    {
        EXPECT_EQ(result.error().code, expectedCode);
    }
}

TEST(MiniaudioDeviceTest, NullBackendStartsStopsAndInvokesCallback)
{
    auto device = MiniaudioDevice::Create(MiniaudioDeviceConfig{
        .useNullBackend = true,
        .sampleRate = 48000,
        .channels = 2,
        .periodFrames = 256,
    });
    ASSERT_TRUE(device.has_value()) << (device ? "" : device.error().message);
    EXPECT_TRUE(device->isNullBackend());
    EXPECT_FALSE(device->isRunning());
    EXPECT_EQ(device->sampleRate(), 48000U);
    EXPECT_EQ(device->channels(), 2U);

    ASSERT_TRUE(device->start().has_value()) << "start failed";
    EXPECT_TRUE(device->isRunning());

    // Null backend runs on a worker thread; wait briefly for callbacks.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
    while (device->callbackInvocations() == 0 && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    EXPECT_GT(device->callbackInvocations(), 0U);

    device->stop();
    EXPECT_FALSE(device->isRunning());
    device->shutdown();
    EXPECT_FALSE(device->isRunning());
}

TEST(MiniaudioDeviceTest, RejectsInvalidConfig)
{
    expectFailureCode(MiniaudioDevice::Create(MiniaudioDeviceConfig{.sampleRate = 0}),
                      AudioErrorCode::InvalidConfiguration);
    expectFailureCode(MiniaudioDevice::Create(MiniaudioDeviceConfig{.channels = 0}),
                      AudioErrorCode::InvalidConfiguration);
}

TEST(MiniaudioDeviceTest, BundleCreatesEngineAndNullDevice)
{
    Core::MemoryTracker tracker;
    Core::CountingMemoryResource resource(tracker, Core::MemoryTag::Audio, *std::pmr::new_delete_resource());
    {
        auto bundle = createMiniaudioAudioBundle(
            AudioEngineConfig{.voiceCapacity = 4, .commandCapacity = 8, .completionCapacity = 8},
            MiniaudioDeviceConfig{.useNullBackend = true}, resource);
        ASSERT_TRUE(bundle.has_value()) << (bundle ? "" : bundle.error().message);
        EXPECT_EQ(bundle->engine.state(), AudioEngineState::Disabled);
        EXPECT_TRUE(bundle->device.isNullBackend());

        auto voice = bundle->engine.createVoice();
        ASSERT_TRUE(voice.has_value());
        const float pcm[4] = {0.0F, 0.5F, 0.0F, -0.5F};
        ASSERT_TRUE(bundle->engine
                        .bindVoiceClip(*voice, AudioPcmClipView{.frames = pcm, .frameCount = 4, .channels = 1,
                                                                .sampleRate = 8000})
                        .has_value());
        ASSERT_TRUE(bundle->engine.enqueuePlay(*voice).has_value());
        ASSERT_TRUE(bundle->device.start().has_value());

        AudioCompletionEvent events[2]{};
        auto pumped = bundle->engine.pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
        ASSERT_TRUE(pumped.has_value());
        EXPECT_EQ(*pumped, 1U);
        EXPECT_EQ(events[0].kind, AudioCompletionKind::Started);

        bundle->device.stop();
        bundle->engine.shutdown();
        bundle->device.shutdown();
    }
    EXPECT_EQ(tracker.snapshot(Core::MemoryTag::Audio).currentBytes, 0U);
}

TEST(MiniaudioDeviceTest, WrongThreadStartIsRejected)
{
    auto device = MiniaudioDevice::Create(MiniaudioDeviceConfig{.useNullBackend = true});
    ASSERT_TRUE(device.has_value()) << (device ? "" : device.error().message);

    Core::ErrorCode code = Core::CoreErrorCode::Internal;
    std::thread worker([&] {
        auto status = device->start();
        if (!status)
        {
            code = status.error().code;
        }
    });
    worker.join();
    EXPECT_EQ(code, AudioErrorCode::WrongOwnerThread);
}

TEST(MiniaudioDeviceTest, NullBackendAppliesVoiceControlsFadeAndRetiresOneShot)
{
    Core::MemoryTracker tracker;
    Core::CountingMemoryResource resource(tracker, Core::MemoryTag::Audio, *std::pmr::new_delete_resource());
    {
        auto bundle = createMiniaudioAudioBundle(
            AudioEngineConfig{.voiceCapacity = 1, .commandCapacity = 8, .completionCapacity = 8},
            MiniaudioDeviceConfig{
                .useNullBackend = true,
                .sampleRate = 48000,
                .channels = 2,
                .periodFrames = 128,
            },
            resource);
        ASSERT_TRUE(bundle.has_value()) << (bundle ? "" : bundle.error().message);

        std::array<float, 4800> pcm{};
        pcm.fill(0.25F);
        auto voice = bundle->engine.playOneShotPcm(AudioPcmClipView{
            .frames = pcm.data(),
            .frameCount = pcm.size(),
            .channels = 1,
            .sampleRate = 48000,
        });
        ASSERT_TRUE(voice.has_value()) << (voice ? "" : voice.error().message);
        ASSERT_TRUE(bundle->engine.setVoiceGain(*voice, 0.8F).has_value());
        ASSERT_TRUE(bundle->engine.setVoicePitch(*voice, 0.5F).has_value());
        ASSERT_TRUE(bundle->engine.setVoicePan(*voice, 0.5F).has_value());

        AudioCompletionEvent events[4]{};
        auto started = bundle->engine.pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
        ASSERT_TRUE(started.has_value());
        ASSERT_EQ(*started, 1U);
        EXPECT_EQ(events[0].kind, AudioCompletionKind::Started);
        ASSERT_TRUE(bundle->engine
                        .startVoiceFade(*voice,
                                        AudioVoiceFadeDesc{
                                            .targetGain = 0.0F,
                                            .duration = Core::Duration{0.01},
                                            .endAction = AudioFadeEndAction::StopVoice,
                                        })
                        .has_value());
        ASSERT_TRUE(bundle->device.start().has_value());

        bool sawStopped = false;
        bool retired = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
        while (std::chrono::steady_clock::now() < deadline)
        {
            auto pumped = bundle->engine.pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
            ASSERT_TRUE(pumped.has_value());
            for (Core::u32 index = 0; index < *pumped; ++index)
            {
                sawStopped = sawStopped || events[index].kind == AudioCompletionKind::Stopped;
            }
            auto live = bundle->engine.isVoiceLive(*voice);
            ASSERT_TRUE(live.has_value());
            if (!*live)
            {
                retired = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }

        EXPECT_GT(bundle->device.callbackInvocations(), 0U);
        EXPECT_TRUE(sawStopped);
        EXPECT_TRUE(retired);
        auto stats = bundle->engine.stats();
        ASSERT_TRUE(stats.has_value());
        EXPECT_EQ(stats->liveVoices, 0U);
        EXPECT_EQ(stats->boundClipVoices, 0U);
        EXPECT_GT(stats->mixFramesRendered, 0U);

        bundle->device.stop();
        bundle->engine.shutdown();
        bundle->device.shutdown();
    }
    EXPECT_EQ(tracker.snapshot(Core::MemoryTag::Audio).currentBytes, 0U);
}

TEST(MiniaudioDeviceTest, NullBackendConsumesBoundedStreamEofAndCancel)
{
    Core::MemoryTracker tracker;
    Core::CountingMemoryResource resource(tracker, Core::MemoryTag::Audio, *std::pmr::new_delete_resource());
    {
        auto bundle = createMiniaudioAudioBundle(
            AudioEngineConfig{
                .voiceCapacity = 1,
                .commandCapacity = 8,
                .completionCapacity = 8,
                .streamBufferFrameCapacity = 1024,
            },
            MiniaudioDeviceConfig{
                .useNullBackend = true,
                .sampleRate = 48000,
                .channels = 2,
                .periodFrames = 64,
            },
            resource);
        ASSERT_TRUE(bundle.has_value()) << (bundle ? "" : bundle.error().message);

        std::array<float, 1024> pcm{};
        pcm.fill(0.25F);
        auto eofVoice = bundle->engine.playPcmStream(AudioPcmStreamDesc{
            .channels = 1,
            .sampleRate = 48000,
            .bufferCapacityFrames = pcm.size(),
        });
        ASSERT_TRUE(eofVoice.has_value());
        ASSERT_TRUE(bundle->engine
                        .submitPcmStreamFrames(
                            *eofVoice,
                            AudioPcmStreamChunkView{.frames = pcm.data(), .frameCount = pcm.size()})
                        .has_value());
        ASSERT_TRUE(bundle->engine.signalPcmStreamEof(*eofVoice).has_value());
        ASSERT_TRUE(bundle->engine.pumpCompletions(4).has_value());
        ASSERT_TRUE(bundle->device.start().has_value());

        AudioCompletionEvent events[4]{};
        bool sawStopped = false;
        const auto eofDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
        while (std::chrono::steady_clock::now() < eofDeadline)
        {
            auto pumped = bundle->engine.pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
            ASSERT_TRUE(pumped.has_value());
            for (Core::u32 index = 0; index < *pumped; ++index)
            {
                sawStopped = sawStopped || events[index].kind == AudioCompletionKind::Stopped;
            }
            auto live = bundle->engine.isVoiceLive(*eofVoice);
            ASSERT_TRUE(live.has_value());
            if (!*live)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        EXPECT_TRUE(sawStopped);
        auto eofLive = bundle->engine.isVoiceLive(*eofVoice);
        ASSERT_TRUE(eofLive.has_value());
        EXPECT_FALSE(*eofLive);

        auto cancelledVoice = bundle->engine.playPcmStream(AudioPcmStreamDesc{
            .channels = 1,
            .sampleRate = 48000,
            .bufferCapacityFrames = 128,
        });
        ASSERT_TRUE(cancelledVoice.has_value());
        ASSERT_TRUE(bundle->engine.pumpCompletions(4).has_value());
        const auto callbackDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
        const Core::u64 callbacksBeforeCancel = bundle->device.callbackInvocations();
        while (bundle->device.callbackInvocations() == callbacksBeforeCancel &&
               std::chrono::steady_clock::now() < callbackDeadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        ASSERT_TRUE(bundle->engine.cancelPcmStream(*cancelledVoice).has_value());

        bool sawCancelled = false;
        const auto cancelDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
        while (std::chrono::steady_clock::now() < cancelDeadline)
        {
            auto pumped = bundle->engine.pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
            ASSERT_TRUE(pumped.has_value());
            for (Core::u32 index = 0; index < *pumped; ++index)
            {
                sawCancelled = sawCancelled || events[index].kind == AudioCompletionKind::Cancelled;
            }
            auto live = bundle->engine.isVoiceLive(*cancelledVoice);
            ASSERT_TRUE(live.has_value());
            if (!*live)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        EXPECT_TRUE(sawCancelled);
        auto cancelledLive = bundle->engine.isVoiceLive(*cancelledVoice);
        ASSERT_TRUE(cancelledLive.has_value());
        EXPECT_FALSE(*cancelledLive);

        auto stats = bundle->engine.stats();
        ASSERT_TRUE(stats.has_value());
        EXPECT_EQ(stats->streamingVoices, 0U);
        EXPECT_EQ(stats->completedStopped, 1U);
        EXPECT_EQ(stats->completedCancelled, 1U);
        EXPECT_GT(stats->streamUnderrunFrames, 0U);

        bundle->device.stop();
        bundle->engine.shutdown();
        bundle->device.shutdown();
    }
    EXPECT_EQ(tracker.snapshot(Core::MemoryTag::Audio).currentBytes, 0U);
}

} // namespace
} // namespace Tina::Audio
