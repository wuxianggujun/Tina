#include <tina/audio/AudioEngine.hpp>

#include <tina/audio/AudioErrors.hpp>
#include <tina/core/memory/CountingMemoryResource.hpp>
#include <tina/core/memory/MemoryTracker.hpp>

#include <gtest/gtest.h>

#include <thread>
#include <utility>

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

TEST(AudioEngineTest, CreateDisabledReportsStatsAndRejectsZeroCapacity)
{
    expectFailureCode(AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 0}),
                      AudioErrorCode::InvalidConfiguration);

    auto engine = AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 4});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    EXPECT_EQ(engine->state(), AudioEngineState::Disabled);

    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value()) << (stats ? "" : stats.error().message);
    EXPECT_EQ(stats->voiceCapacity, 4U);
    EXPECT_EQ(stats->liveVoices, 0U);
    EXPECT_EQ(stats->commandCapacity, 64U);
    EXPECT_EQ(stats->completionCapacity, 64U);

    auto pumped = engine->pumpCompletions(8);
    ASSERT_TRUE(pumped.has_value()) << (pumped ? "" : pumped.error().message);
    EXPECT_EQ(*pumped, 0U);
}

TEST(AudioEngineTest, VoiceGenerationAndCapacity)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 2});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);

    auto first = engine->createVoice();
    auto second = engine->createVoice();
    ASSERT_TRUE(first.has_value()) << (first ? "" : first.error().message);
    ASSERT_TRUE(second.has_value()) << (second ? "" : second.error().message);
    EXPECT_NE(*first, *second);

    auto live = engine->isVoiceLive(*first);
    ASSERT_TRUE(live.has_value());
    EXPECT_TRUE(*live);

    expectFailureCode(engine->createVoice(), AudioErrorCode::CapacityExceeded);

    ASSERT_TRUE(engine->destroyVoice(*first).has_value());
    auto recycled = engine->createVoice();
    ASSERT_TRUE(recycled.has_value()) << (recycled ? "" : recycled.error().message);
    EXPECT_EQ(recycled->index(), first->index());
    EXPECT_NE(recycled->generation(), first->generation());

    expectFailureCode(engine->destroyVoice(*first), AudioErrorCode::StaleVoice);
    auto staleLive = engine->isVoiceLive(*first);
    ASSERT_TRUE(staleLive.has_value());
    EXPECT_FALSE(*staleLive);
}

TEST(AudioEngineTest, ShutdownIsIdempotentAndClosesApi)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 2});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    ASSERT_TRUE(engine->createVoice().has_value());

    engine->shutdown();
    EXPECT_EQ(engine->state(), AudioEngineState::Stopped);
    engine->shutdown();
    EXPECT_EQ(engine->state(), AudioEngineState::Stopped);

    expectFailureCode(engine->createVoice(), AudioErrorCode::EngineClosed);
    expectFailureCode(engine->pumpCompletions(), AudioErrorCode::EngineClosed);
    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->liveVoices, 0U);
}

TEST(AudioEngineTest, WrongThreadIsRejected)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 2});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);

    Core::Status destroyStatus = Core::success();
    Core::ErrorCode createCode = Core::CoreErrorCode::Internal;
    std::thread worker([&] {
        auto created = engine->createVoice();
        if (!created)
        {
            createCode = created.error().code;
        }
        destroyStatus = engine->destroyVoice(AudioVoiceId{});
    });
    worker.join();

    EXPECT_EQ(createCode, AudioErrorCode::WrongOwnerThread);
    EXPECT_FALSE(destroyStatus);
    if (!destroyStatus)
    {
        EXPECT_EQ(destroyStatus.error().code, AudioErrorCode::WrongOwnerThread);
    }
}

TEST(AudioEngineTest, CountingResourceBytesReturnToZero)
{
    Core::MemoryTracker tracker;
    Core::CountingMemoryResource resource(tracker, Core::MemoryTag::Audio, *std::pmr::new_delete_resource());
    {
        auto engine = AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 8}, resource);
        ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
        ASSERT_TRUE(engine->createVoice().has_value());
        EXPECT_GT(tracker.snapshot(Core::MemoryTag::Audio).currentBytes, 0U);
        engine->shutdown();
    }
    const Core::MemoryStatistics statistics = tracker.snapshot(Core::MemoryTag::Audio);
    EXPECT_EQ(statistics.currentBytes, 0U);
    EXPECT_EQ(statistics.allocationCount, statistics.deallocationCount);
}

TEST(AudioEngineTest, PlayStopProduceCompletionsOnPump)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 2,
        .commandCapacity = 8,
        .completionCapacity = 8,
    });
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    auto voice = engine->createVoice();
    ASSERT_TRUE(voice.has_value()) << (voice ? "" : voice.error().message);

    const float pcm[4] = {0.0F, 0.25F, 0.0F, -0.25F};
    ASSERT_TRUE(engine
                    ->bindVoiceClip(*voice, AudioPcmClipView{.frames = pcm, .frameCount = 4, .channels = 1,
                                                             .sampleRate = 8000})
                    .has_value());

    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    auto playingBefore = engine->isVoicePlaying(*voice);
    ASSERT_TRUE(playingBefore.has_value());
    EXPECT_FALSE(*playingBefore);

    AudioCompletionEvent events[4]{};
    auto pumped = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(pumped.has_value()) << (pumped ? "" : pumped.error().message);
    ASSERT_EQ(*pumped, 1U);
    EXPECT_EQ(events[0].kind, AudioCompletionKind::Started);
    EXPECT_EQ(events[0].voice, *voice);
    EXPECT_GE(events[0].commandSequence, 1U);

    auto playingAfter = engine->isVoicePlaying(*voice);
    ASSERT_TRUE(playingAfter.has_value());
    EXPECT_TRUE(*playingAfter);

    ASSERT_TRUE(engine->enqueueStop(*voice).has_value());
    auto stopPump = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(stopPump.has_value()) << (stopPump ? "" : stopPump.error().message);
    ASSERT_EQ(*stopPump, 1U);
    EXPECT_EQ(events[0].kind, AudioCompletionKind::Stopped);

    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->completedStarted, 1U);
    EXPECT_EQ(stats->completedStopped, 1U);
    EXPECT_EQ(stats->pendingCommands, 0U);
    EXPECT_EQ(stats->pendingCompletions, 0U);
}

TEST(AudioEngineTest, StaleVoiceEnqueueFailsAndAppliedCommandRejects)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 2,
        .commandCapacity = 4,
        .completionCapacity = 4,
    });
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    auto voice = engine->createVoice();
    ASSERT_TRUE(voice.has_value()) << (voice ? "" : voice.error().message);

    expectFailureCode(engine->enqueuePlay(AudioVoiceId{}), AudioErrorCode::InvalidVoice);

    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    ASSERT_TRUE(engine->destroyVoice(*voice).has_value());
    // Command still pending against destroyed generation -> RejectedStale on pump.
    AudioCompletionEvent events[2]{};
    auto pumped = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(pumped.has_value()) << (pumped ? "" : pumped.error().message);
    ASSERT_EQ(*pumped, 1U);
    EXPECT_EQ(events[0].kind, AudioCompletionKind::RejectedStale);
    EXPECT_EQ(events[0].voice, *voice);

    expectFailureCode(engine->enqueueStop(*voice), AudioErrorCode::StaleVoice);
}

TEST(AudioEngineTest, FullCommandQueueReturnsCapacityExceeded)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 1,
        .commandCapacity = 2,
        .completionCapacity = 8,
    });
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    auto voice = engine->createVoice();
    ASSERT_TRUE(voice.has_value()) << (voice ? "" : voice.error().message);

    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    ASSERT_TRUE(engine->enqueueStop(*voice).has_value());
    expectFailureCode(engine->enqueuePlay(*voice), AudioErrorCode::CapacityExceeded);

    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->pendingCommands, 2U);
    EXPECT_EQ(stats->rejectedCommands, 1U);

    auto drained = engine->pumpCompletions(8);
    ASSERT_TRUE(drained.has_value()) << (drained ? "" : drained.error().message);
    EXPECT_EQ(*drained, 2U);
}

TEST(AudioEngineTest, BusVolumeMuteAndEffectiveGain)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 1});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);

    auto defaultMaster = engine->busState(AudioBusId::Master);
    ASSERT_TRUE(defaultMaster.has_value());
    EXPECT_FLOAT_EQ(defaultMaster->volume, 1.0F);
    EXPECT_FALSE(defaultMaster->muted);

    ASSERT_TRUE(engine->setBusVolume(AudioBusId::Master, 0.5F).has_value());
    ASSERT_TRUE(engine->setBusVolume(AudioBusId::Sfx, 0.8F).has_value());
    auto sfxGain = engine->effectiveBusGain(AudioBusId::Sfx);
    ASSERT_TRUE(sfxGain.has_value());
    EXPECT_FLOAT_EQ(*sfxGain, 0.4F);

    ASSERT_TRUE(engine->setBusMuted(AudioBusId::Master, true).has_value());
    auto mutedGain = engine->effectiveBusGain(AudioBusId::Sfx);
    ASSERT_TRUE(mutedGain.has_value());
    EXPECT_FLOAT_EQ(*mutedGain, 0.0F);

    auto sfxState = engine->busState(AudioBusId::Sfx);
    ASSERT_TRUE(sfxState.has_value());
    EXPECT_FLOAT_EQ(sfxState->volume, 0.8F);
    EXPECT_FALSE(sfxState->muted);

    expectFailureCode(engine->setBusVolume(AudioBusId::Music, -0.1F), AudioErrorCode::InvalidConfiguration);
    expectFailureCode(engine->setBusVolume(AudioBusId::Music, 1.5F), AudioErrorCode::InvalidConfiguration);
}

TEST(AudioEngineTest, PlayWithoutClipYieldsRejectedNoClip)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 2, .commandCapacity = 4, .completionCapacity = 4});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    auto voice = engine->createVoice();
    ASSERT_TRUE(voice.has_value());

    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    AudioCompletionEvent events[2]{};
    auto pumped = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(pumped.has_value());
    ASSERT_EQ(*pumped, 1U);
    EXPECT_EQ(events[0].kind, AudioCompletionKind::RejectedNoClip);
    auto playing = engine->isVoicePlaying(*voice);
    ASSERT_TRUE(playing.has_value());
    EXPECT_FALSE(*playing);

    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->completedRejectedNoClip, 1U);
}

TEST(AudioEngineTest, BindClipThenPlayStop)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 2, .commandCapacity = 8, .completionCapacity = 8});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    auto voice = engine->createVoice();
    ASSERT_TRUE(voice.has_value());

    const float pcm[8] = {0.0F, 0.1F, 0.2F, 0.1F, 0.0F, -0.1F, -0.2F, -0.1F};
    const AudioPcmClipView clip{
        .frames = pcm,
        .frameCount = 8,
        .channels = 1,
        .sampleRate = 8000,
    };
    ASSERT_TRUE(engine->bindVoiceClip(*voice, clip).has_value());
    auto bound = engine->voiceClip(*voice);
    ASSERT_TRUE(bound.has_value());
    EXPECT_EQ(bound->frameCount, 8U);
    EXPECT_EQ(bound->frames, pcm);

    auto statsBound = engine->stats();
    ASSERT_TRUE(statsBound.has_value());
    EXPECT_EQ(statsBound->boundClipVoices, 1U);

    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    AudioCompletionEvent events[2]{};
    auto startPump = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(startPump.has_value());
    ASSERT_EQ(*startPump, 1U);
    EXPECT_EQ(events[0].kind, AudioCompletionKind::Started);
    auto playing = engine->isVoicePlaying(*voice);
    ASSERT_TRUE(playing.has_value());
    EXPECT_TRUE(*playing);

    expectFailureCode(engine->bindVoiceClip(*voice, clip), AudioErrorCode::InvalidConfiguration);
    expectFailureCode(engine->clearVoiceClip(*voice), AudioErrorCode::InvalidConfiguration);

    ASSERT_TRUE(engine->enqueueStop(*voice).has_value());
    auto stopPump = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(stopPump.has_value());
    ASSERT_EQ(*stopPump, 1U);
    EXPECT_EQ(events[0].kind, AudioCompletionKind::Stopped);

    ASSERT_TRUE(engine->clearVoiceClip(*voice).has_value());
    auto cleared = engine->voiceClip(*voice);
    ASSERT_TRUE(cleared.has_value());
    EXPECT_TRUE(cleared->empty());
    auto statsClear = engine->stats();
    ASSERT_TRUE(statsClear.has_value());
    EXPECT_EQ(statsClear->boundClipVoices, 0U);
}

TEST(AudioEngineTest, RejectsInvalidClipView)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 1});
    ASSERT_TRUE(engine.has_value());
    auto voice = engine->createVoice();
    ASSERT_TRUE(voice.has_value());

    const float sample = 0.0F;
    expectFailureCode(engine->bindVoiceClip(*voice, AudioPcmClipView{}), AudioErrorCode::InvalidConfiguration);
    expectFailureCode(engine->bindVoiceClip(*voice,
                                            AudioPcmClipView{.frames = &sample, .frameCount = 1, .channels = 0,
                                                             .sampleRate = 8000}),
                      AudioErrorCode::InvalidConfiguration);
    expectFailureCode(engine->bindVoiceClip(*voice,
                                            AudioPcmClipView{.frames = &sample, .frameCount = 1, .channels = 1,
                                                             .sampleRate = 10}),
                      AudioErrorCode::InvalidConfiguration);
}

TEST(AudioEngineTest, MixRealtimeSumsClipAndNaturalEndStops)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 2, .commandCapacity = 8, .completionCapacity = 8});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    auto voice = engine->createVoice();
    ASSERT_TRUE(voice.has_value());

    // 4 mono frames @ 48000, constant 0.5 — same rate as mix out.
    const float pcm[4] = {0.5F, 0.5F, 0.5F, 0.5F};
    ASSERT_TRUE(engine
                    ->bindVoiceClip(*voice, AudioPcmClipView{.frames = pcm, .frameCount = 4, .channels = 1,
                                                             .sampleRate = 48000})
                    .has_value());
    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());

    float out[8]{};
    engine->mixRealtime(out, /*outFrames=*/2, /*outChannels=*/2, /*outSampleRate=*/48000);
    EXPECT_NEAR(out[0], 0.5F, 1.0e-4F);
    EXPECT_NEAR(out[1], 0.5F, 1.0e-4F);
    EXPECT_NEAR(out[2], 0.5F, 1.0e-4F);
    EXPECT_NEAR(out[3], 0.5F, 1.0e-4F);

    float out2[8]{};
    engine->mixRealtime(out2, 2, 2, 48000);
    EXPECT_NEAR(out2[0], 0.5F, 1.0e-4F);

    // Clip exhausted → natural end → Stopped on pump.
    float silence[8]{};
    engine->mixRealtime(silence, 2, 2, 48000);
    for (float sample : silence)
    {
        EXPECT_FLOAT_EQ(sample, 0.0F);
    }

    AudioCompletionEvent events[4]{};
    auto pumped = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(pumped.has_value());
    ASSERT_GE(*pumped, 1U);
    bool sawStop = false;
    for (Core::u32 i = 0; i < *pumped; ++i)
    {
        if (events[i].kind == AudioCompletionKind::Stopped)
        {
            sawStop = true;
        }
    }
    EXPECT_TRUE(sawStop);
    auto playing = engine->isVoicePlaying(*voice);
    ASSERT_TRUE(playing.has_value());
    EXPECT_FALSE(*playing);

    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_GE(stats->mixFramesRendered, 4U);
}

TEST(AudioEngineTest, MixRealtimeMuteWhenMasterMuted)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 1, .commandCapacity = 4, .completionCapacity = 4});
    ASSERT_TRUE(engine.has_value());
    auto voice = engine->createVoice();
    ASSERT_TRUE(voice.has_value());
    const float pcm[2] = {1.0F, 1.0F};
    ASSERT_TRUE(engine
                    ->bindVoiceClip(*voice, AudioPcmClipView{.frames = pcm, .frameCount = 2, .channels = 1,
                                                             .sampleRate = 48000})
                    .has_value());
    ASSERT_TRUE(engine->setBusMuted(AudioBusId::Master, true).has_value());
    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    ASSERT_TRUE(engine->pumpCompletions(2).has_value());

    float out[4]{};
    engine->mixRealtime(out, 2, 2, 48000);
    EXPECT_FLOAT_EQ(out[0], 0.0F);
    EXPECT_FLOAT_EQ(out[1], 0.0F);
}

TEST(AudioEngineTest, PlayOneShotPcmBindsAndStarts)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 2, .commandCapacity = 4, .completionCapacity = 4});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    const float pcm[4] = {0.25F, 0.25F, 0.25F, 0.25F};
    auto voice = engine->playOneShotPcm(
        AudioPcmClipView{.frames = pcm, .frameCount = 4, .channels = 1, .sampleRate = 48000});
    ASSERT_TRUE(voice.has_value()) << (voice ? "" : voice.error().message);

    AudioCompletionEvent events[2]{};
    auto pumped = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(pumped.has_value());
    ASSERT_EQ(*pumped, 1U);
    EXPECT_EQ(events[0].kind, AudioCompletionKind::Started);
    EXPECT_EQ(events[0].voice, *voice);

    float out[4]{};
    engine->mixRealtime(out, 2, 1, 48000);
    EXPECT_NEAR(out[0], 0.25F, 1.0e-4F);
    EXPECT_NEAR(out[1], 0.25F, 1.0e-4F);

    expectFailureCode(engine->playOneShotPcm(AudioPcmClipView{}), AudioErrorCode::InvalidConfiguration);
}

} // namespace
} // namespace Tina::Audio
