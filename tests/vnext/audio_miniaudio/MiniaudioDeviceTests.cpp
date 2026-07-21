#include <tina/audio/miniaudio/MiniaudioDevice.hpp>

#include <tina/audio/AudioErrors.hpp>
#include <tina/core/memory/CountingMemoryResource.hpp>
#include <tina/core/memory/MemoryTracker.hpp>

#include <gtest/gtest.h>

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

} // namespace
} // namespace Tina::Audio
