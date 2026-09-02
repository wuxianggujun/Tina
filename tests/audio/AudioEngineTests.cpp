#include <tina/audio/AudioEngine.hpp>

#include <tina/audio/AudioErrors.hpp>
#include <tina/core/memory/CountingMemoryResource.hpp>
#include <tina/core/memory/MemoryTracker.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

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
    expectFailureCode(AudioEngine::Create(AudioEngineConfig{.voiceCapacity = AudioMaxRealtimeVoices + 1}),
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
    const float pcm[1] = {0.25F};
    ASSERT_TRUE(engine
                    ->playOneShotPcm(
                        AudioPcmClipView{.frames = pcm, .frameCount = 1, .channels = 1, .sampleRate = 48000})
                    .has_value());

    auto beforeShutdown = engine->stats();
    ASSERT_TRUE(beforeShutdown.has_value());
    EXPECT_EQ(beforeShutdown->liveVoices, 2U);
    EXPECT_EQ(beforeShutdown->boundClipVoices, 1U);

    engine->shutdown();
    EXPECT_EQ(engine->state(), AudioEngineState::Stopped);
    engine->shutdown();
    EXPECT_EQ(engine->state(), AudioEngineState::Stopped);

    expectFailureCode(engine->createVoice(), AudioErrorCode::EngineClosed);
    expectFailureCode(engine->pumpCompletions(), AudioErrorCode::EngineClosed);
    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->liveVoices, 0U);
    EXPECT_EQ(stats->boundClipVoices, 0U);
    EXPECT_EQ(stats->activeMixVoices, 0U);
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
    EXPECT_EQ(stats->liveVoices, 1U);
    EXPECT_EQ(stats->boundClipVoices, 1U);
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

// The Music bus used to be inert: every active voice was published the Sfx gain, so
// a Music volume of 0.5 left playback at full scale. This covers the activateMixSlot
// path, where the gain is resolved as the voice starts.
TEST(AudioEngineTest, MusicBusVolumeScalesMusicVoiceWhenSetBeforePlay)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 1, .commandCapacity = 4, .completionCapacity = 4});
    ASSERT_TRUE(engine.has_value());
    auto voice = engine->createVoice(AudioBusId::Music);
    ASSERT_TRUE(voice.has_value());
    const float pcm[2] = {1.0F, 1.0F};
    ASSERT_TRUE(engine
                    ->bindVoiceClip(*voice, AudioPcmClipView{.frames = pcm, .frameCount = 2, .channels = 1,
                                                             .sampleRate = 48000})
                    .has_value());
    ASSERT_TRUE(engine->setBusVolume(AudioBusId::Music, 0.5F).has_value());
    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    ASSERT_TRUE(engine->pumpCompletions(2).has_value());

    float out[4]{};
    engine->mixRealtime(out, 2, 2, 48000);
    EXPECT_FLOAT_EQ(out[0], 0.5F);
}

// Same defect via the other path: publishBusGainToActiveSlots must resolve each
// slot's bus instead of publishing one engine-wide value to every active slot.
TEST(AudioEngineTest, MusicMuteSilencesMusicVoiceWhenSetAfterPlay)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 1, .commandCapacity = 4, .completionCapacity = 4});
    ASSERT_TRUE(engine.has_value());
    auto voice = engine->createVoice(AudioBusId::Music);
    ASSERT_TRUE(voice.has_value());
    const float pcm[2] = {1.0F, 1.0F};
    ASSERT_TRUE(engine
                    ->bindVoiceClip(*voice, AudioPcmClipView{.frames = pcm, .frameCount = 2, .channels = 1,
                                                             .sampleRate = 48000})
                    .has_value());
    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    ASSERT_TRUE(engine->pumpCompletions(2).has_value());
    ASSERT_TRUE(engine->setBusMuted(AudioBusId::Music, true).has_value());

    float out[4]{};
    engine->mixRealtime(out, 2, 2, 48000);
    EXPECT_FLOAT_EQ(out[0], 0.0F);
}

// Guards the reverse leak: per-slot resolution must not let a Music change reach an
// Sfx voice.
TEST(AudioEngineTest, MusicMuteLeavesSfxVoiceAudible)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 1, .commandCapacity = 4, .completionCapacity = 4});
    ASSERT_TRUE(engine.has_value());
    auto voice = engine->createVoice(AudioBusId::Sfx);
    ASSERT_TRUE(voice.has_value());
    const float pcm[2] = {1.0F, 1.0F};
    ASSERT_TRUE(engine
                    ->bindVoiceClip(*voice, AudioPcmClipView{.frames = pcm, .frameCount = 2, .channels = 1,
                                                             .sampleRate = 48000})
                    .has_value());
    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    ASSERT_TRUE(engine->pumpCompletions(2).has_value());
    ASSERT_TRUE(engine->setBusMuted(AudioBusId::Music, true).has_value());

    float out[4]{};
    engine->mixRealtime(out, 2, 2, 48000);
    EXPECT_FLOAT_EQ(out[0], 1.0F);
}

TEST(AudioEngineTest, VoiceBusRoundTripsAndRejectsOutOfRangeBus)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 2, .commandCapacity = 4, .completionCapacity = 4});
    ASSERT_TRUE(engine.has_value());
    auto music = engine->createVoice(AudioBusId::Music);
    ASSERT_TRUE(music.has_value());
    auto reported = engine->voiceBus(*music);
    ASSERT_TRUE(reported.has_value());
    EXPECT_EQ(*reported, AudioBusId::Music);

    auto sfx = engine->createVoice();
    ASSERT_TRUE(sfx.has_value());
    auto sfxBus = engine->voiceBus(*sfx);
    ASSERT_TRUE(sfxBus.has_value());
    EXPECT_EQ(*sfxBus, AudioBusId::Sfx);

    expectFailureCode(engine->createVoice(static_cast<AudioBusId>(99)),
                      AudioErrorCode::InvalidConfiguration);
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

TEST(AudioEngineTest, OneShotNaturalEndRetiresVoiceAndReusesCapacity)
{
    auto engine = AudioEngine::Create(
        AudioEngineConfig{.voiceCapacity = 1, .commandCapacity = 4, .completionCapacity = 4});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    const float pcm[1] = {0.5F};
    AudioVoiceId previousVoice{};

    for (Core::u32 iteration = 0; iteration < 4; ++iteration)
    {
        auto voice = engine->playOneShotPcm(
            AudioPcmClipView{.frames = pcm, .frameCount = 1, .channels = 1, .sampleRate = 48000});
        ASSERT_TRUE(voice.has_value()) << (voice ? "" : voice.error().message);
        if (previousVoice.hasValue())
        {
            EXPECT_EQ(voice->index(), previousVoice.index());
            EXPECT_NE(voice->generation(), previousVoice.generation());
        }

        AudioCompletionEvent events[2]{};
        auto started = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
        ASSERT_TRUE(started.has_value());
        ASSERT_EQ(*started, 1U);
        EXPECT_EQ(events[0].kind, AudioCompletionKind::Started);
        EXPECT_EQ(events[0].voice, *voice);

        auto activeStats = engine->stats();
        ASSERT_TRUE(activeStats.has_value());
        EXPECT_EQ(activeStats->liveVoices, 1U);
        EXPECT_EQ(activeStats->boundClipVoices, 1U);
        EXPECT_EQ(activeStats->activeMixVoices, 1U);

        float out[1]{};
        engine->mixRealtime(out, 1, 1, 48000);
        EXPECT_NEAR(out[0], 0.5F, 1.0e-4F);

        auto stopped = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
        ASSERT_TRUE(stopped.has_value());
        ASSERT_EQ(*stopped, 1U);
        EXPECT_EQ(events[0].kind, AudioCompletionKind::Stopped);
        EXPECT_EQ(events[0].voice, *voice);

        auto live = engine->isVoiceLive(*voice);
        ASSERT_TRUE(live.has_value());
        EXPECT_FALSE(*live);
        auto retiredStats = engine->stats();
        ASSERT_TRUE(retiredStats.has_value());
        EXPECT_EQ(retiredStats->liveVoices, 0U);
        EXPECT_EQ(retiredStats->boundClipVoices, 0U);
        EXPECT_EQ(retiredStats->activeMixVoices, 0U);
        previousVoice = *voice;
    }
}

bool observeRealtimeCallbackOverlap(AudioEngine& engine,
                                    const std::atomic<bool>& callbackReady,
                                    const std::atomic<bool>& callbackDone)
{
    callbackReady.wait(false, std::memory_order_acquire);
    for (Core::u32 attempt = 0; attempt < 10000U; ++attempt)
    {
        auto beforeProbe = engine.stats();
        if (!beforeProbe)
        {
            return false;
        }
        float overlapProbe = 1.0F;
        engine.mixRealtime(&overlapProbe, 1, 1, 48000);
        auto afterProbe = engine.stats();
        if (!afterProbe)
        {
            return false;
        }
        if (!callbackDone.load(std::memory_order_acquire) &&
            afterProbe->mixFramesRendered == beforeProbe->mixFramesRendered)
        {
            return overlapProbe == 0.0F;
        }
        std::this_thread::yield();
    }
    return false;
}

TEST(AudioEngineTest, ExplicitStopRetiresOneShotVoice)
{
    auto engine = AudioEngine::Create(
        AudioEngineConfig{.voiceCapacity = 1, .commandCapacity = 4, .completionCapacity = 4});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    const float pcm[4] = {0.25F, 0.25F, 0.25F, 0.25F};
    auto voice = engine->playOneShotPcm(
        AudioPcmClipView{.frames = pcm, .frameCount = 4, .channels = 1, .sampleRate = 48000});
    ASSERT_TRUE(voice.has_value()) << (voice ? "" : voice.error().message);
    ASSERT_TRUE(engine->pumpCompletions(2).has_value());

    ASSERT_TRUE(engine->enqueueStop(*voice).has_value());
    AudioCompletionEvent events[2]{};
    auto stopped = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(stopped.has_value());
    ASSERT_EQ(*stopped, 1U);
    EXPECT_EQ(events[0].kind, AudioCompletionKind::Stopped);
    EXPECT_EQ(events[0].voice, *voice);

    auto live = engine->isVoiceLive(*voice);
    ASSERT_TRUE(live.has_value());
    EXPECT_FALSE(*live);
    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->liveVoices, 0U);
    EXPECT_EQ(stats->boundClipVoices, 0U);
    EXPECT_EQ(stats->activeMixVoices, 0U);
}

TEST(AudioEngineTest, OneShotRetiresWhenStoppedCompletionCannotBeQueued)
{
    auto engine = AudioEngine::Create(
        AudioEngineConfig{.voiceCapacity = 1, .commandCapacity = 2, .completionCapacity = 1});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    const float pcm[1] = {0.25F};
    auto voice = engine->playOneShotPcm(
        AudioPcmClipView{.frames = pcm, .frameCount = 1, .channels = 1, .sampleRate = 48000});
    ASSERT_TRUE(voice.has_value()) << (voice ? "" : voice.error().message);

    auto applied = engine->pumpCompletions(std::span<AudioCompletionEvent>{}, 0);
    ASSERT_TRUE(applied.has_value());
    EXPECT_EQ(*applied, 0U);
    float out[1]{};
    engine->mixRealtime(out, 1, 1, 48000);

    auto harvested = engine->pumpCompletions(std::span<AudioCompletionEvent>{}, 0);
    ASSERT_TRUE(harvested.has_value());
    EXPECT_EQ(*harvested, 0U);
    auto live = engine->isVoiceLive(*voice);
    ASSERT_TRUE(live.has_value());
    EXPECT_FALSE(*live);

    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->liveVoices, 0U);
    EXPECT_EQ(stats->boundClipVoices, 0U);
    EXPECT_EQ(stats->activeMixVoices, 0U);
    EXPECT_EQ(stats->pendingCompletions, 1U);
    EXPECT_EQ(stats->rejectedCommands, 1U);
}

// A clip voice's natural end lives only in its mix slot. applyCommands runs before
// harvestNaturalEnds inside one pump, so a Play queued in that same pump used to take
// the finished slot and zero the token: the first voice never received Stopped, stayed
// playing forever, could not be rebound or cleared, and -- because its mixSlot still
// pointed at the reused slot -- its gain/pitch/pan setters silently retargeted the new
// voice. Streams were already safe because their terminal is parked outside the slot.
TEST(AudioEngineTest, ReusingAFinishedClipSlotStillReportsThePreviousStopped)
{
    // Two voices must coexist, so the pool needs two slots -- but voiceCapacity also
    // sizes the mix slot table, and the contention this test needs is *one* mix slot.
    // The first voice therefore has to finish before the second can be given a slot,
    // which is exactly the interleaving under test.
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 2,
        .commandCapacity = 8,
        .completionCapacity = 8,
    });
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);

    const float pcm[1] = {0.25F};
    const AudioPcmClipView clip{.frames = pcm, .frameCount = 1, .channels = 1, .sampleRate = 48000};

    auto first = engine->createVoice();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(engine->bindVoiceClip(*first, clip).has_value());
    ASSERT_TRUE(engine->enqueuePlay(*first).has_value());
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());

    // Run the clip past its single frame so the callback flags a natural end. The
    // owner has not harvested it yet.
    float out[1]{};
    engine->mixRealtime(out, 1, 1, 48000);

    auto second = engine->createVoice();
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(engine->bindVoiceClip(*second, clip).has_value());
    ASSERT_TRUE(engine->enqueuePlay(*second).has_value());

    // One pump: applyCommands wants the only slot, which still holds the first
    // voice's terminal.
    AudioCompletionEvent events[4]{};
    auto drained = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(drained.has_value());

    bool sawFirstStopped = false;
    for (Core::u32 index = 0; index < *drained; ++index)
    {
        if (events[index].voice == *first && events[index].kind == AudioCompletionKind::Stopped)
        {
            sawFirstStopped = true;
        }
    }
    EXPECT_TRUE(sawFirstStopped)
        << "the first voice's Stopped was lost when its slot was reused";

    // And the first voice is genuinely finished rather than wedged: not playing, and
    // its clip can be cleared again -- both impossible while playing stayed true.
    auto playing = engine->isVoicePlaying(*first);
    ASSERT_TRUE(playing.has_value());
    EXPECT_FALSE(*playing);
    EXPECT_TRUE(engine->clearVoiceClip(*first).has_value());
}

// Deferring a clip terminal means `playing` goes false while the mixer may still be
// reading the caller's frames. Three APIs hand the caller permission to free that
// memory, and all three used `playing` as the predicate, so all three said yes inside
// the deferral window. A full completion ring makes the window deterministic: the
// terminal is chosen and parked, but cannot be published.
TEST(AudioEngineTest, PayloadCannotBeReleasedWhileAClipTerminalIsStillParked)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 1,
        .commandCapacity = 4,
        // One slot, consumed by Started, so the parked Stopped cannot be published.
        .completionCapacity = 1,
    });
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);

    const float pcm[4] = {0.25F, 0.25F, 0.25F, 0.25F};
    const AudioPcmClipView clip{.frames = pcm, .frameCount = 4, .channels = 1, .sampleRate = 48000};

    auto voice = engine->createVoice();
    ASSERT_TRUE(voice.has_value());
    ASSERT_TRUE(engine->bindVoiceClip(*voice, clip).has_value());
    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    ASSERT_TRUE(engine->enqueueStop(*voice).has_value());

    // Apply both commands without draining: Started fills the ring, so Stop's terminal
    // parks. Do not pump with a drain here -- that would free the slot immediately.
    auto applied = engine->pumpCompletions(std::span<AudioCompletionEvent>{}, 0);
    ASSERT_TRUE(applied.has_value());
    EXPECT_EQ(*applied, 0U);

    auto parkedStats = engine->stats();
    ASSERT_TRUE(parkedStats.has_value());
    ASSERT_EQ(parkedStats->pendingCompletions, 1U)
        << "test needs the ring full so the terminal is provably still parked";

    // Stop already cleared `playing`, which is exactly why it is the wrong predicate.
    auto playing = engine->isVoicePlaying(*voice);
    ASSERT_TRUE(playing.has_value());
    ASSERT_FALSE(*playing);

    // All three refuse rather than telling the caller its PCM is free.
    expectFailureCode(engine->clearVoiceClip(*voice), AudioErrorCode::InvalidConfiguration);
    expectFailureCode(engine->bindVoiceClip(*voice, clip), AudioErrorCode::InvalidConfiguration);
    expectFailureCode(engine->destroyVoice(*voice), AudioErrorCode::InvalidConfiguration);

    // Refusal must be atomic: the clip is still the original binding, and the voice is
    // still live and still owns its pending terminal.
    auto bound = engine->voiceClip(*voice);
    ASSERT_TRUE(bound.has_value());
    EXPECT_EQ(bound->frames, pcm);
    auto live = engine->isVoiceLive(*voice);
    ASSERT_TRUE(live.has_value());
    EXPECT_TRUE(*live);

    // A Play arriving inside the window must not reactivate the voice out from under
    // the parked Stopped, or that Stopped would later be published for a playing voice.
    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    AudioCompletionEvent drainOne[1]{};
    auto firstDrain = engine->pumpCompletions(std::span<AudioCompletionEvent>{drainOne}, 0);
    ASSERT_TRUE(firstDrain.has_value());
    ASSERT_EQ(*firstDrain, 1U);
    EXPECT_EQ(drainOne[0].kind, AudioCompletionKind::Started);
    auto replayed = engine->isVoicePlaying(*voice);
    ASSERT_TRUE(replayed.has_value());
    EXPECT_FALSE(*replayed) << "Play must be absorbed while a clip terminal is parked";

    // Once the ring has room the terminal drains, and only then does the caller get
    // its release signal and the right to reuse the voice.
    AudioCompletionEvent events[4]{};
    auto drained = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(drained.has_value());
    bool sawStopped = false;
    for (Core::u32 index = 0; index < *drained; ++index)
    {
        if (events[index].voice == *voice && events[index].kind == AudioCompletionKind::Stopped)
        {
            sawStopped = true;
        }
    }
    EXPECT_TRUE(sawStopped) << "the deferred terminal must still be delivered";
    EXPECT_TRUE(engine->clearVoiceClip(*voice).has_value());
    EXPECT_TRUE(engine->destroyVoice(*voice).has_value());
}

// destroyVoice on a playing clip voice erased the record while the callback was still
// reading the frames, and published nothing at all -- the caller never learned when
// its PCM stopped being read. Streams were already refused here for the same reason.
TEST(AudioEngineTest, DestroyingAPlayingClipVoiceIsRefusedSoTheOwnerLearnsWhenPcmIsFree)
{
    auto engine = AudioEngine::Create(
        AudioEngineConfig{.voiceCapacity = 1, .commandCapacity = 4, .completionCapacity = 4});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);

    const float pcm[4] = {0.5F, 0.5F, 0.5F, 0.5F};
    auto voice = engine->createVoice();
    ASSERT_TRUE(voice.has_value());
    ASSERT_TRUE(engine
                    ->bindVoiceClip(*voice,
                                    AudioPcmClipView{.frames = pcm,
                                                     .frameCount = 4,
                                                     .channels = 1,
                                                     .sampleRate = 48000})
                    .has_value());
    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());
    auto playing = engine->isVoicePlaying(*voice);
    ASSERT_TRUE(playing.has_value());
    ASSERT_TRUE(*playing);

    expectFailureCode(engine->destroyVoice(*voice), AudioErrorCode::InvalidConfiguration);
    auto stillLive = engine->isVoiceLive(*voice);
    ASSERT_TRUE(stillLive.has_value());
    EXPECT_TRUE(*stillLive) << "a refused destroy must not erase the record";

    // The supported route delivers the signal, and only then does destroy succeed.
    ASSERT_TRUE(engine->enqueueStop(*voice).has_value());
    AudioCompletionEvent events[2]{};
    auto stopped = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(stopped.has_value());
    ASSERT_EQ(*stopped, 1U);
    EXPECT_EQ(events[0].kind, AudioCompletionKind::Stopped);
    EXPECT_EQ(events[0].voice, *voice);
    EXPECT_TRUE(engine->destroyVoice(*voice).has_value());
}

TEST(AudioEngineTest, VoiceControlsRejectInvalidValuesWithoutMutation)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 1});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    auto voice = engine->createVoice();
    ASSERT_TRUE(voice.has_value());

    ASSERT_TRUE(engine->setVoiceGain(*voice, 0.4F).has_value());
    ASSERT_TRUE(engine->setVoicePitch(*voice, 1.5F).has_value());
    ASSERT_TRUE(engine->setVoicePan(*voice, -0.25F).has_value());

    const float nan = (std::numeric_limits<float>::quiet_NaN)();
    const float infinity = (std::numeric_limits<float>::infinity)();
    expectFailureCode(engine->setVoiceGain(*voice, nan), AudioErrorCode::InvalidConfiguration);
    expectFailureCode(engine->setVoiceGain(*voice, 1.01F), AudioErrorCode::InvalidConfiguration);
    expectFailureCode(engine->setVoicePitch(*voice, infinity), AudioErrorCode::InvalidConfiguration);
    expectFailureCode(engine->setVoicePitch(*voice, AudioVoiceMinPitch - 0.01F),
                      AudioErrorCode::InvalidConfiguration);
    expectFailureCode(engine->setVoicePan(*voice, -1.01F), AudioErrorCode::InvalidConfiguration);
    expectFailureCode(engine->setVoicePan(*voice, infinity), AudioErrorCode::InvalidConfiguration);

    auto state = engine->voicePlaybackState(*voice);
    ASSERT_TRUE(state.has_value());
    EXPECT_FLOAT_EQ(state->gain, 0.4F);
    EXPECT_FLOAT_EQ(state->pitch, 1.5F);
    EXPECT_FLOAT_EQ(state->pan, -0.25F);
    EXPECT_FALSE(state->playing);
    EXPECT_FALSE(state->fadeActive);

    auto otherEngine = AudioEngine::Create(AudioEngineConfig{.voiceCapacity = 1});
    ASSERT_TRUE(otherEngine.has_value());
    auto otherVoice = otherEngine->createVoice();
    ASSERT_TRUE(otherVoice.has_value());
    expectFailureCode(engine->setVoiceGain(*otherVoice, 0.5F), AudioErrorCode::StaleVoice);

    Core::Status wrongThreadStatus = Core::success();
    std::thread worker([&] { wrongThreadStatus = engine->setVoicePan(*voice, 0.5F); });
    worker.join();
    EXPECT_FALSE(wrongThreadStatus);
    if (!wrongThreadStatus)
    {
        EXPECT_EQ(wrongThreadStatus.error().code, AudioErrorCode::WrongOwnerThread);
    }
}

TEST(AudioEngineTest, PitchLinearlyResamplesAndChangesPlaybackRate)
{
    auto engine = AudioEngine::Create(
        AudioEngineConfig{.voiceCapacity = 1, .commandCapacity = 8, .completionCapacity = 8});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    auto voice = engine->createVoice();
    ASSERT_TRUE(voice.has_value());
    const float pcm[6] = {0.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F};
    ASSERT_TRUE(engine
                    ->bindVoiceClip(*voice,
                                    AudioPcmClipView{
                                        .frames = pcm,
                                        .frameCount = 6,
                                        .channels = 1,
                                        .sampleRate = 24000,
                                    })
                    .has_value());

    ASSERT_TRUE(engine->setVoicePitch(*voice, 1.0F).has_value());
    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());
    float halfRateOut[4]{};
    engine->mixRealtime(halfRateOut, 4, 1, 48000);
    EXPECT_NEAR(halfRateOut[0], 0.0F, 1.0e-4F);
    EXPECT_NEAR(halfRateOut[1], 0.5F, 1.0e-4F);
    EXPECT_NEAR(halfRateOut[2], 1.0F, 1.0e-4F);
    EXPECT_NEAR(halfRateOut[3], 1.5F, 1.0e-4F);

    ASSERT_TRUE(engine->enqueueStop(*voice).has_value());
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());
    ASSERT_TRUE(engine->setVoicePitch(*voice, 2.0F).has_value());
    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());
    float doubledPitchOut[4]{};
    engine->mixRealtime(doubledPitchOut, 4, 1, 48000);
    EXPECT_NEAR(doubledPitchOut[0], 0.0F, 1.0e-4F);
    EXPECT_NEAR(doubledPitchOut[1], 1.0F, 1.0e-4F);
    EXPECT_NEAR(doubledPitchOut[2], 2.0F, 1.0e-4F);
    EXPECT_NEAR(doubledPitchOut[3], 3.0F, 1.0e-4F);
}

TEST(AudioEngineTest, PanUsesCenterCompatibleLinearBalanceAndMonoIgnoresPan)
{
    auto engine = AudioEngine::Create(
        AudioEngineConfig{.voiceCapacity = 1, .commandCapacity = 16, .completionCapacity = 16});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    auto voice = engine->createVoice();
    ASSERT_TRUE(voice.has_value());
    const float pcm[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    ASSERT_TRUE(engine
                    ->bindVoiceClip(*voice,
                                    AudioPcmClipView{
                                        .frames = pcm,
                                        .frameCount = 4,
                                        .channels = 1,
                                        .sampleRate = 48000,
                                    })
                    .has_value());

    const auto mixStereoAtPan = [&](float pan, float expectedLeft, float expectedRight) {
        ASSERT_TRUE(engine->setVoicePan(*voice, pan).has_value());
        ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
        ASSERT_TRUE(engine->pumpCompletions(4).has_value());
        float out[2]{};
        engine->mixRealtime(out, 1, 2, 48000);
        EXPECT_NEAR(out[0], expectedLeft, 1.0e-4F);
        EXPECT_NEAR(out[1], expectedRight, 1.0e-4F);
        ASSERT_TRUE(engine->enqueueStop(*voice).has_value());
        ASSERT_TRUE(engine->pumpCompletions(4).has_value());
    };

    mixStereoAtPan(-1.0F, 1.0F, 0.0F);
    mixStereoAtPan(0.0F, 1.0F, 1.0F);
    mixStereoAtPan(1.0F, 0.0F, 1.0F);

    ASSERT_TRUE(engine->setVoicePan(*voice, 1.0F).has_value());
    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());
    float monoOut[1]{};
    engine->mixRealtime(monoOut, 1, 1, 48000);
    EXPECT_NEAR(monoOut[0], 1.0F, 1.0e-4F);
}

TEST(AudioEngineTest, FadeReachesTargetOnExactRenderedFrame)
{
    auto engine = AudioEngine::Create(
        AudioEngineConfig{.voiceCapacity = 1, .commandCapacity = 8, .completionCapacity = 8});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    auto voice = engine->createVoice();
    ASSERT_TRUE(voice.has_value());
    const float pcm[8] = {1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
    ASSERT_TRUE(engine
                    ->bindVoiceClip(*voice,
                                    AudioPcmClipView{
                                        .frames = pcm,
                                        .frameCount = 8,
                                        .channels = 1,
                                        .sampleRate = 48000,
                                    })
                    .has_value());
    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());

    ASSERT_TRUE(engine
                    ->startVoiceFade(*voice,
                                     AudioVoiceFadeDesc{
                                         .targetGain = 0.0F,
                                         .duration = Core::Duration{4.0 / 48000.0},
                                         .endAction = AudioFadeEndAction::KeepPlaying,
                                     })
                    .has_value());
    float out[4]{};
    engine->mixRealtime(out, 4, 1, 48000);
    EXPECT_NEAR(out[0], 0.75F, 1.0e-4F);
    EXPECT_NEAR(out[1], 0.50F, 1.0e-4F);
    EXPECT_NEAR(out[2], 0.25F, 1.0e-4F);
    EXPECT_NEAR(out[3], 0.00F, 1.0e-4F);

    auto state = engine->voicePlaybackState(*voice);
    ASSERT_TRUE(state.has_value());
    EXPECT_NEAR(state->gain, 0.0F, 1.0e-4F);
    EXPECT_TRUE(state->playing);
    EXPECT_FALSE(state->fadeActive);
}

TEST(AudioEngineTest, FadeCancelKeepsLastRenderedGain)
{
    auto engine = AudioEngine::Create(
        AudioEngineConfig{.voiceCapacity = 1, .commandCapacity = 8, .completionCapacity = 8});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    auto voice = engine->createVoice();
    ASSERT_TRUE(voice.has_value());
    const float pcm[12] = {1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F,
                           1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
    ASSERT_TRUE(engine
                    ->bindVoiceClip(*voice,
                                    AudioPcmClipView{
                                        .frames = pcm,
                                        .frameCount = 12,
                                        .channels = 1,
                                        .sampleRate = 48000,
                                    })
                    .has_value());
    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());
    ASSERT_TRUE(engine
                    ->startVoiceFade(*voice,
                                     AudioVoiceFadeDesc{
                                         .targetGain = 0.0F,
                                         .duration = Core::Duration{4.0 / 48000.0},
                                     })
                    .has_value());

    float fadingOut[2]{};
    engine->mixRealtime(fadingOut, 2, 1, 48000);
    EXPECT_NEAR(fadingOut[0], 0.75F, 1.0e-4F);
    EXPECT_NEAR(fadingOut[1], 0.50F, 1.0e-4F);
    auto fadingState = engine->voicePlaybackState(*voice);
    ASSERT_TRUE(fadingState.has_value());
    EXPECT_TRUE(fadingState->fadeActive);
    EXPECT_NEAR(fadingState->gain, 0.50F, 1.0e-4F);

    ASSERT_TRUE(engine->cancelVoiceFade(*voice).has_value());
    float cancelledOut[2]{};
    engine->mixRealtime(cancelledOut, 2, 1, 48000);
    EXPECT_NEAR(cancelledOut[0], 0.50F, 1.0e-4F);
    EXPECT_NEAR(cancelledOut[1], 0.50F, 1.0e-4F);
    auto cancelledState = engine->voicePlaybackState(*voice);
    ASSERT_TRUE(cancelledState.has_value());
    EXPECT_FALSE(cancelledState->fadeActive);
    EXPECT_NEAR(cancelledState->gain, 0.50F, 1.0e-4F);
}

TEST(AudioEngineTest, FadeStopProducesSingleStoppedAndRetiresOneShot)
{
    auto engine = AudioEngine::Create(
        AudioEngineConfig{.voiceCapacity = 1, .commandCapacity = 8, .completionCapacity = 8});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    const float pcm[8] = {1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
    auto voice = engine->playOneShotPcm(
        AudioPcmClipView{.frames = pcm, .frameCount = 8, .channels = 1, .sampleRate = 48000});
    ASSERT_TRUE(voice.has_value()) << (voice ? "" : voice.error().message);
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());
    ASSERT_TRUE(engine
                    ->startVoiceFade(*voice,
                                     AudioVoiceFadeDesc{
                                         .targetGain = 0.0F,
                                         .duration = Core::Duration{2.0 / 48000.0},
                                         .endAction = AudioFadeEndAction::StopVoice,
                                     })
                    .has_value());

    float out[4]{};
    engine->mixRealtime(out, 4, 1, 48000);
    EXPECT_NEAR(out[0], 0.5F, 1.0e-4F);
    EXPECT_NEAR(out[1], 0.0F, 1.0e-4F);
    EXPECT_NEAR(out[2], 0.0F, 1.0e-4F);
    EXPECT_NEAR(out[3], 0.0F, 1.0e-4F);

    AudioCompletionEvent events[2]{};
    auto stopped = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(stopped.has_value());
    ASSERT_EQ(*stopped, 1U);
    EXPECT_EQ(events[0].kind, AudioCompletionKind::Stopped);
    EXPECT_EQ(events[0].voice, *voice);
    auto secondPump = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(secondPump.has_value());
    EXPECT_EQ(*secondPump, 0U);

    auto live = engine->isVoiceLive(*voice);
    ASSERT_TRUE(live.has_value());
    EXPECT_FALSE(*live);
    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->liveVoices, 0U);
    EXPECT_EQ(stats->boundClipVoices, 0U);
    EXPECT_EQ(stats->activeMixVoices, 0U);
}

TEST(AudioEngineTest, FadeValidationAndShutdownDuringFadeAreTransactional)
{
    auto engine = AudioEngine::Create(
        AudioEngineConfig{.voiceCapacity = 1, .commandCapacity = 8, .completionCapacity = 8});
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
    auto voice = engine->createVoice();
    ASSERT_TRUE(voice.has_value());
    const float pcm[8] = {1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
    ASSERT_TRUE(engine
                    ->bindVoiceClip(*voice,
                                    AudioPcmClipView{
                                        .frames = pcm,
                                        .frameCount = 8,
                                        .channels = 1,
                                        .sampleRate = 48000,
                                    })
                    .has_value());

    const AudioVoiceFadeDesc validFade{
        .targetGain = 0.25F,
        .duration = Core::Duration{0.25},
    };
    expectFailureCode(engine->startVoiceFade(*voice, validFade), AudioErrorCode::InvalidConfiguration);
    expectFailureCode(engine->startVoiceFade(
                          *voice,
                          AudioVoiceFadeDesc{
                              .targetGain = -0.1F,
                              .duration = Core::Duration{0.25},
                          }),
                      AudioErrorCode::InvalidConfiguration);
    expectFailureCode(engine->startVoiceFade(
                          *voice,
                          AudioVoiceFadeDesc{
                              .targetGain = 0.5F,
                              .duration = Core::Duration::zero(),
                          }),
                      AudioErrorCode::InvalidConfiguration);
    expectFailureCode(engine->startVoiceFade(
                          *voice,
                          AudioVoiceFadeDesc{
                              .targetGain = 0.5F,
                              .duration = Core::Duration{0.25},
                              .endAction = static_cast<AudioFadeEndAction>(255),
                          }),
                      AudioErrorCode::InvalidConfiguration);

    ASSERT_TRUE(engine->enqueuePlay(*voice).has_value());
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());
    ASSERT_TRUE(engine->startVoiceFade(*voice, validFade).has_value());
    float out[1]{};
    engine->mixRealtime(out, 1, 1, 48000);
    auto state = engine->voicePlaybackState(*voice);
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(state->fadeActive);

    engine->shutdown();
    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->liveVoices, 0U);
    EXPECT_EQ(stats->boundClipVoices, 0U);
    EXPECT_EQ(stats->activeMixVoices, 0U);
}

TEST(AudioEngineTest, PcmStreamSubmitsMixesSignalsEofAndRetires)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 1,
        .commandCapacity = 8,
        .completionCapacity = 8,
        .streamBufferFrameCapacity = 4,
    });
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);

    auto voice = engine->playPcmStream(AudioPcmStreamDesc{
        .channels = 1,
        .sampleRate = 48000,
        .bufferCapacityFrames = 4,
    });
    ASSERT_TRUE(voice.has_value()) << (voice ? "" : voice.error().message);
    const float frames[3] = {0.25F, 0.5F, 0.75F};
    ASSERT_TRUE(engine
                    ->submitPcmStreamFrames(
                        *voice,
                        AudioPcmStreamChunkView{.frames = frames, .frameCount = 3})
                    .has_value());

    auto beforeStart = engine->pcmStreamState(*voice);
    ASSERT_TRUE(beforeStart.has_value());
    EXPECT_FALSE(beforeStart->playing);
    EXPECT_EQ(beforeStart->bufferedFrames, 3U);
    EXPECT_EQ(beforeStart->submittedFrames, 3U);

    AudioCompletionEvent events[2]{};
    auto started = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(started.has_value());
    ASSERT_EQ(*started, 1U);
    EXPECT_EQ(events[0].kind, AudioCompletionKind::Started);
    ASSERT_TRUE(engine->signalPcmStreamEof(*voice).has_value());
    ASSERT_TRUE(engine->signalPcmStreamEof(*voice).has_value());

    float out[3]{};
    engine->mixRealtime(out, 3, 1, 48000);
    EXPECT_FLOAT_EQ(out[0], 0.25F);
    EXPECT_FLOAT_EQ(out[1], 0.5F);
    EXPECT_FLOAT_EQ(out[2], 0.75F);

    auto stopped = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(stopped.has_value());
    ASSERT_EQ(*stopped, 1U);
    EXPECT_EQ(events[0].kind, AudioCompletionKind::Stopped);
    EXPECT_EQ(events[0].voice, *voice);
    auto live = engine->isVoiceLive(*voice);
    ASSERT_TRUE(live.has_value());
    EXPECT_FALSE(*live);

    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->streamingVoices, 0U);
    EXPECT_EQ(stats->streamBufferedFrames, 0U);
    EXPECT_EQ(stats->completedStopped, 1U);
}

TEST(AudioEngineTest, PcmStreamUnderrunOutputsSilenceWithoutFakingEof)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 1,
        .commandCapacity = 8,
        .completionCapacity = 8,
        .streamBufferFrameCapacity = 4,
    });
    ASSERT_TRUE(engine.has_value());
    auto voice = engine->playPcmStream(AudioPcmStreamDesc{
        .channels = 1,
        .sampleRate = 48000,
        .bufferCapacityFrames = 4,
    });
    ASSERT_TRUE(voice.has_value());
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());

    float silence[3] = {1.0F, 1.0F, 1.0F};
    engine->mixRealtime(silence, 3, 1, 48000);
    EXPECT_FLOAT_EQ(silence[0], 0.0F);
    EXPECT_FLOAT_EQ(silence[1], 0.0F);
    EXPECT_FLOAT_EQ(silence[2], 0.0F);

    auto state = engine->pcmStreamState(*voice);
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(state->playing);
    EXPECT_FALSE(state->eofSignaled);
    EXPECT_EQ(state->underrunFrames, 3U);
    EXPECT_EQ(state->consumedFrames, 0U);
    auto noCompletion = engine->pumpCompletions(4);
    ASSERT_TRUE(noCompletion.has_value());
    EXPECT_EQ(*noCompletion, 0U);

    const float frames[2] = {-0.25F, 0.5F};
    ASSERT_TRUE(engine
                    ->submitPcmStreamFrames(
                        *voice,
                        AudioPcmStreamChunkView{.frames = frames, .frameCount = 2})
                    .has_value());
    float recovered[2]{};
    engine->mixRealtime(recovered, 2, 1, 48000);
    EXPECT_FLOAT_EQ(recovered[0], -0.25F);
    EXPECT_FLOAT_EQ(recovered[1], 0.5F);

    state = engine->pcmStreamState(*voice);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->consumedFrames, 2U);
    EXPECT_EQ(state->underrunFrames, 3U);
}

TEST(AudioEngineTest, PcmStreamSubmitIsWholeChunkAtomicAndWraps)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 1,
        .commandCapacity = 8,
        .completionCapacity = 8,
        .streamBufferFrameCapacity = 4,
    });
    ASSERT_TRUE(engine.has_value());
    auto voice = engine->playPcmStream(AudioPcmStreamDesc{
        .channels = 1,
        .sampleRate = 48000,
        .bufferCapacityFrames = 4,
    });
    ASSERT_TRUE(voice.has_value());

    const float first[4] = {1.0F, 2.0F, 3.0F, 4.0F};
    ASSERT_TRUE(engine
                    ->submitPcmStreamFrames(
                        *voice,
                        AudioPcmStreamChunkView{.frames = first, .frameCount = 4})
                    .has_value());
    const float rejectedFrame[1] = {99.0F};
    expectFailureCode(engine->submitPcmStreamFrames(
                          *voice,
                          AudioPcmStreamChunkView{.frames = rejectedFrame, .frameCount = 1}),
                      AudioErrorCode::CapacityExceeded);
    auto fullState = engine->pcmStreamState(*voice);
    ASSERT_TRUE(fullState.has_value());
    EXPECT_EQ(fullState->submittedFrames, 4U);
    EXPECT_EQ(fullState->bufferedFrames, 4U);

    ASSERT_TRUE(engine->pumpCompletions(4).has_value());
    float firstOut[2]{};
    engine->mixRealtime(firstOut, 2, 1, 48000);
    EXPECT_FLOAT_EQ(firstOut[0], 1.0F);
    EXPECT_FLOAT_EQ(firstOut[1], 2.0F);

    const float wrapped[2] = {5.0F, 6.0F};
    ASSERT_TRUE(engine
                    ->submitPcmStreamFrames(
                        *voice,
                        AudioPcmStreamChunkView{.frames = wrapped, .frameCount = 2})
                    .has_value());
    ASSERT_TRUE(engine->signalPcmStreamEof(*voice).has_value());
    float wrappedOut[4]{};
    engine->mixRealtime(wrappedOut, 4, 1, 48000);
    EXPECT_FLOAT_EQ(wrappedOut[0], 3.0F);
    EXPECT_FLOAT_EQ(wrappedOut[1], 4.0F);
    EXPECT_FLOAT_EQ(wrappedOut[2], 5.0F);
    EXPECT_FLOAT_EQ(wrappedOut[3], 6.0F);
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());
}

TEST(AudioEngineTest, PcmStreamCancelCompletionSurvivesFullCompletionRing)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 1,
        .commandCapacity = 4,
        .completionCapacity = 1,
        .streamBufferFrameCapacity = 4,
    });
    ASSERT_TRUE(engine.has_value());
    auto voice = engine->playPcmStream(AudioPcmStreamDesc{
        .channels = 1,
        .sampleRate = 48000,
        .bufferCapacityFrames = 4,
    });
    ASSERT_TRUE(voice.has_value());

    auto appliedWithoutDrain =
        engine->pumpCompletions(std::span<AudioCompletionEvent>{}, 0);
    ASSERT_TRUE(appliedWithoutDrain.has_value());
    EXPECT_EQ(*appliedWithoutDrain, 0U);
    ASSERT_TRUE(engine->cancelPcmStream(*voice).has_value());
    ASSERT_TRUE(engine->cancelPcmStream(*voice).has_value());
    ASSERT_TRUE(engine->pumpCompletions(std::span<AudioCompletionEvent>{}, 0).has_value());

    auto pending = engine->pcmStreamState(*voice);
    ASSERT_TRUE(pending.has_value());
    EXPECT_TRUE(pending->cancelPending);
    auto live = engine->isVoiceLive(*voice);
    ASSERT_TRUE(live.has_value());
    EXPECT_TRUE(*live);

    AudioCompletionEvent event[1]{};
    auto started = engine->pumpCompletions(std::span<AudioCompletionEvent>{event}, 0);
    ASSERT_TRUE(started.has_value());
    ASSERT_EQ(*started, 1U);
    EXPECT_EQ(event[0].kind, AudioCompletionKind::Started);
    live = engine->isVoiceLive(*voice);
    ASSERT_TRUE(live.has_value());
    EXPECT_TRUE(*live);

    auto cancelled = engine->pumpCompletions(std::span<AudioCompletionEvent>{event}, 0);
    ASSERT_TRUE(cancelled.has_value());
    ASSERT_EQ(*cancelled, 1U);
    EXPECT_EQ(event[0].kind, AudioCompletionKind::Cancelled);
    EXPECT_EQ(event[0].voice, *voice);
    live = engine->isVoiceLive(*voice);
    ASSERT_TRUE(live.has_value());
    EXPECT_FALSE(*live);

    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->completedCancelled, 1U);
    EXPECT_EQ(stats->streamingVoices, 0U);
}

TEST(AudioEngineTest, PcmStreamEofRemainsIdempotentWhileStoppedCompletionIsDeferred)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 1,
        .commandCapacity = 4,
        .completionCapacity = 1,
        .streamBufferFrameCapacity = 2,
    });
    ASSERT_TRUE(engine.has_value());
    auto voice = engine->playPcmStream(AudioPcmStreamDesc{
        .channels = 1,
        .sampleRate = 48000,
        .bufferCapacityFrames = 2,
    });
    ASSERT_TRUE(voice.has_value());
    const float frames[2] = {0.25F, 0.75F};
    ASSERT_TRUE(engine
                    ->submitPcmStreamFrames(
                        *voice,
                        AudioPcmStreamChunkView{.frames = frames, .frameCount = 2})
                    .has_value());
    ASSERT_TRUE(engine->signalPcmStreamEof(*voice).has_value());

    // Keep Started in the only completion slot, then finish the stream so
    // Stopped remains as terminal debt.
    ASSERT_TRUE(engine->pumpCompletions(std::span<AudioCompletionEvent>{}, 0).has_value());
    float out[2]{};
    engine->mixRealtime(out, 2, 1, 48000);
    ASSERT_TRUE(engine->pumpCompletions(std::span<AudioCompletionEvent>{}, 0).has_value());
    auto pending = engine->pcmStreamState(*voice);
    ASSERT_TRUE(pending.has_value());
    EXPECT_TRUE(pending->eofSignaled);
    EXPECT_TRUE(pending->terminalCompletionPending);

    // Idempotence is stable for the full lifetime, including backpressure.
    EXPECT_TRUE(engine->signalPcmStreamEof(*voice).has_value());

    AudioCompletionEvent event[1]{};
    auto started = engine->pumpCompletions(std::span<AudioCompletionEvent>{event}, 0);
    ASSERT_TRUE(started.has_value());
    ASSERT_EQ(*started, 1U);
    EXPECT_EQ(event[0].kind, AudioCompletionKind::Started);
    auto stopped = engine->pumpCompletions(std::span<AudioCompletionEvent>{event}, 0);
    ASSERT_TRUE(stopped.has_value());
    ASSERT_EQ(*stopped, 1U);
    EXPECT_EQ(event[0].kind, AudioCompletionKind::Stopped);
}

TEST(AudioEngineTest, DeferredStreamTerminalDoesNotClearReusedMixSlot)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 2,
        .commandCapacity = 8,
        .completionCapacity = 1,
        .streamBufferFrameCapacity = 4,
    });
    ASSERT_TRUE(engine.has_value());
    auto first = engine->playPcmStream(AudioPcmStreamDesc{
        .channels = 1,
        .sampleRate = 48000,
        .bufferCapacityFrames = 4,
    });
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(engine->pumpCompletions(std::span<AudioCompletionEvent>{}, 0).has_value());
    ASSERT_TRUE(engine->cancelPcmStream(*first).has_value());
    ASSERT_TRUE(engine->pumpCompletions(std::span<AudioCompletionEvent>{}, 0).has_value());

    auto second = engine->playPcmStream(AudioPcmStreamDesc{
        .channels = 1,
        .sampleRate = 48000,
        .bufferCapacityFrames = 4,
    });
    ASSERT_TRUE(second.has_value());
    const float frame[1] = {0.625F};
    ASSERT_TRUE(engine
                    ->submitPcmStreamFrames(
                        *second,
                        AudioPcmStreamChunkView{.frames = frame, .frameCount = 1})
                    .has_value());
    ASSERT_TRUE(engine->signalPcmStreamEof(*second).has_value());

    AudioCompletionEvent event[1]{};
    auto drainedStarted = engine->pumpCompletions(std::span<AudioCompletionEvent>{event}, 0);
    ASSERT_TRUE(drainedStarted.has_value());
    ASSERT_EQ(*drainedStarted, 1U);
    EXPECT_EQ(event[0].kind, AudioCompletionKind::Started);

    auto drainedCancelled = engine->pumpCompletions(std::span<AudioCompletionEvent>{event}, 0);
    ASSERT_TRUE(drainedCancelled.has_value());
    ASSERT_EQ(*drainedCancelled, 1U);
    EXPECT_EQ(event[0].kind, AudioCompletionKind::Cancelled);
    EXPECT_EQ(event[0].voice, *first);

    auto secondState = engine->pcmStreamState(*second);
    ASSERT_TRUE(secondState.has_value());
    EXPECT_TRUE(secondState->playing);
    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->activeMixVoices, 1U);

    float out[1]{};
    engine->mixRealtime(out, 1, 1, 48000);
    EXPECT_FLOAT_EQ(out[0], 0.625F);
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());
}

TEST(AudioEngineTest, StreamTerminalGenerationMismatchPreservesSameBatchReplacement)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 2,
        .commandCapacity = 8,
        .completionCapacity = 8,
        .streamBufferFrameCapacity = 2,
    });
    ASSERT_TRUE(engine.has_value());
    auto stream = engine->playPcmStream(AudioPcmStreamDesc{
        .channels = 1,
        .sampleRate = 48000,
        .bufferCapacityFrames = 2,
    });
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());

    auto replacement = engine->createVoice();
    ASSERT_TRUE(replacement.has_value());
    const float clip[2] = {0.75F, 0.75F};
    ASSERT_TRUE(engine
                    ->bindVoiceClip(*replacement,
                                    AudioPcmClipView{
                                        .frames = clip,
                                        .frameCount = 2,
                                        .channels = 1,
                                        .sampleRate = 48000,
                                    })
                    .has_value());

    // Stop queues a terminal token for the old publication. Play then reuses
    // that inactive mix slot before applyCommands performs its final flush.
    ASSERT_TRUE(engine->enqueueStop(*stream).has_value());
    ASSERT_TRUE(engine->enqueuePlay(*replacement).has_value());
    AudioCompletionEvent events[4]{};
    auto pumped = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(pumped.has_value());
    ASSERT_EQ(*pumped, 2U);
    EXPECT_EQ(events[0].kind, AudioCompletionKind::Started);
    EXPECT_EQ(events[0].voice, *replacement);
    EXPECT_EQ(events[1].kind, AudioCompletionKind::Stopped);
    EXPECT_EQ(events[1].voice, *stream);

    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->activeMixVoices, 1U);
    float out[1]{};
    engine->mixRealtime(out, 1, 1, 48000);
    EXPECT_FLOAT_EQ(out[0], 0.75F);
}

TEST(AudioEngineTest, PcmStreamValidationWrongThreadAndShutdownAreBounded)
{
    expectFailureCode(AudioEngine::Create(AudioEngineConfig{.streamBufferFrameCapacity = 0}),
                      AudioErrorCode::InvalidConfiguration);
    expectFailureCode(AudioEngine::Create(AudioEngineConfig{.streamBufferFrameCapacity = 1}),
                      AudioErrorCode::InvalidConfiguration);
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 1,
        .commandCapacity = 8,
        .completionCapacity = 8,
        .streamBufferFrameCapacity = 4,
    });
    ASSERT_TRUE(engine.has_value());
    expectFailureCode(engine->playPcmStream(AudioPcmStreamDesc{
                          .channels = 1,
                          .sampleRate = 48000,
                          .bufferCapacityFrames = 1,
                      }),
                      AudioErrorCode::InvalidConfiguration);
    expectFailureCode(engine->playPcmStream(AudioPcmStreamDesc{
                          .channels = 3,
                          .sampleRate = 48000,
                          .bufferCapacityFrames = 4,
                      }),
                      AudioErrorCode::InvalidConfiguration);
    expectFailureCode(engine->playPcmStream(AudioPcmStreamDesc{
                          .channels = 1,
                          .sampleRate = 48000,
                          .bufferCapacityFrames = 5,
                      }),
                      AudioErrorCode::InvalidConfiguration);

    auto voice = engine->playPcmStream(AudioPcmStreamDesc{
        .channels = 1,
        .sampleRate = 48000,
        .bufferCapacityFrames = 4,
    });
    ASSERT_TRUE(voice.has_value());
    const float frame[1] = {0.25F};
    Core::ErrorCode workerCode = Core::CoreErrorCode::Internal;
    std::thread worker([&] {
        auto submitted = engine->submitPcmStreamFrames(
            *voice,
            AudioPcmStreamChunkView{.frames = frame, .frameCount = 1});
        if (!submitted)
        {
            workerCode = submitted.error().code;
        }
    });
    worker.join();
    EXPECT_EQ(workerCode, AudioErrorCode::WrongOwnerThread);

    engine->shutdown();
    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->liveVoices, 0U);
    EXPECT_EQ(stats->streamingVoices, 0U);
    EXPECT_EQ(stats->streamBufferedFrames, 0U);
    EXPECT_EQ(stats->activeMixVoices, 0U);
    expectFailureCode(engine->submitPcmStreamFrames(
                          *voice,
                          AudioPcmStreamChunkView{.frames = frame, .frameCount = 1}),
                      AudioErrorCode::EngineClosed);
}

TEST(AudioEngineTest, PcmStreamCancelConcurrentWithRealtimeMixRetiresExactlyOnceBeforeReuse)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 1,
        .commandCapacity = 8,
        .completionCapacity = 8,
        .streamBufferFrameCapacity = 4,
    });
    ASSERT_TRUE(engine.has_value());
    auto voice = engine->playPcmStream(AudioPcmStreamDesc{
        .channels = 1,
        .sampleRate = 48000,
        .bufferCapacityFrames = 4,
    });
    ASSERT_TRUE(voice.has_value());
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());

    std::vector<float> output(65'536U, 1.0F);
    std::atomic<bool> callbackReady{false};
    std::atomic<bool> stopCallback{false};
    std::atomic<bool> callbackDone{false};
    std::thread callback([&] {
        callbackReady.store(true, std::memory_order_release);
        callbackReady.notify_one();
        do
        {
            engine->mixRealtime(output.data(), static_cast<Core::u32>(output.size()), 1, 48000);
        } while (!stopCallback.load(std::memory_order_acquire));
        callbackDone.store(true, std::memory_order_release);
    });

    const bool observedActiveReader =
        observeRealtimeCallbackOverlap(*engine, callbackReady, callbackDone);
    if (!observedActiveReader)
    {
        stopCallback.store(true, std::memory_order_release);
        callback.join();
        FAIL() << "Could not overlap the owner probe with the realtime callback";
        return;
    }
    auto cancelStatus = engine->cancelPcmStream(*voice);
    if (!cancelStatus)
    {
        stopCallback.store(true, std::memory_order_release);
        callback.join();
        FAIL() << cancelStatus.error().message;
        return;
    }
    AudioCompletionEvent events[2]{};
    auto beforeJoin = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    if (!beforeJoin)
    {
        stopCallback.store(true, std::memory_order_release);
        callback.join();
        FAIL() << beforeJoin.error().message;
        return;
    }
    if (*beforeJoin > 1U)
    {
        stopCallback.store(true, std::memory_order_release);
        callback.join();
        FAIL() << "Cancellation published more than one completion before callback join";
        return;
    }
    if (*beforeJoin == 1U)
    {
        EXPECT_EQ(events[0].kind, AudioCompletionKind::Cancelled);
        EXPECT_EQ(events[0].voice, *voice);
    }

    stopCallback.store(true, std::memory_order_release);
    callback.join();
    auto afterJoin = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(afterJoin.has_value());
    ASSERT_LE(*afterJoin, 1U);
    if (*afterJoin == 1U)
    {
        EXPECT_EQ(events[0].kind, AudioCompletionKind::Cancelled);
        EXPECT_EQ(events[0].voice, *voice);
    }
    EXPECT_EQ(*beforeJoin + *afterJoin, 1U);
    auto live = engine->isVoiceLive(*voice);
    ASSERT_TRUE(live.has_value());
    EXPECT_FALSE(*live);

    auto replacement = engine->playPcmStream(AudioPcmStreamDesc{
        .channels = 1,
        .sampleRate = 48000,
        .bufferCapacityFrames = 4,
    });
    ASSERT_TRUE(replacement.has_value());
    EXPECT_EQ(replacement->index(), voice->index());
    EXPECT_NE(replacement->generation(), voice->generation());
    const float replacementFrame[1] = {0.75F};
    ASSERT_TRUE(engine
                    ->submitPcmStreamFrames(
                        *replacement,
                        AudioPcmStreamChunkView{.frames = replacementFrame, .frameCount = 1})
                    .has_value());
    ASSERT_TRUE(engine->signalPcmStreamEof(*replacement).has_value());
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());
    float replacementOut[1]{};
    engine->mixRealtime(replacementOut, 1, 1, 48000);
    EXPECT_FLOAT_EQ(replacementOut[0], 0.75F);
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());
}

TEST(AudioEngineTest, PcmStreamEofPublishedDuringRealtimeMixDoesNotLoseTailFrame)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 1,
        .commandCapacity = 8,
        .completionCapacity = 8,
        .streamBufferFrameCapacity = 2,
    });
    ASSERT_TRUE(engine.has_value());
    auto voice = engine->playPcmStream(AudioPcmStreamDesc{
        .channels = 1,
        .sampleRate = 48000,
        .bufferCapacityFrames = 2,
    });
    ASSERT_TRUE(voice.has_value());
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());

    std::vector<float> output(65'536U, 0.0F);
    std::atomic<bool> callbackReady{false};
    std::atomic<bool> stopCallback{false};
    std::atomic<bool> callbackDone{false};
    std::thread callback([&] {
        callbackReady.store(true, std::memory_order_release);
        callbackReady.notify_one();
        do
        {
            engine->mixRealtime(output.data(), static_cast<Core::u32>(output.size()), 1, 48000);
        } while (!stopCallback.load(std::memory_order_acquire));
        callbackDone.store(true, std::memory_order_release);
    });

    const bool observedActiveReader =
        observeRealtimeCallbackOverlap(*engine, callbackReady, callbackDone);
    if (!observedActiveReader)
    {
        stopCallback.store(true, std::memory_order_release);
        callback.join();
        FAIL() << "Could not overlap the owner probe with the realtime callback";
        return;
    }

    constexpr float TailSample = 0.875F;
    const float tail[1] = {TailSample};
    auto submitStatus = engine->submitPcmStreamFrames(
        *voice, AudioPcmStreamChunkView{.frames = tail, .frameCount = 1});
    if (!submitStatus)
    {
        stopCallback.store(true, std::memory_order_release);
        callback.join();
        FAIL() << submitStatus.error().message;
        return;
    }
    auto eofStatus = engine->signalPcmStreamEof(*voice);
    if (!eofStatus)
    {
        stopCallback.store(true, std::memory_order_release);
        callback.join();
        FAIL() << eofStatus.error().message;
        return;
    }
    stopCallback.store(true, std::memory_order_release);
    callback.join();

    float afterCallback[1] = {0.0F};
    engine->mixRealtime(afterCallback, 1, 1, 48000);
    EXPECT_TRUE(afterCallback[0] == 0.0F || afterCallback[0] == TailSample);

    auto state = engine->pcmStreamState(*voice);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->submittedFrames, 1U);
    EXPECT_EQ(state->consumedFrames, 1U);

    float afterEof[1] = {1.0F};
    engine->mixRealtime(afterEof, 1, 1, 48000);
    EXPECT_FLOAT_EQ(afterEof[0], 0.0F);

    AudioCompletionEvent event[1]{};
    auto stopped = engine->pumpCompletions(std::span<AudioCompletionEvent>{event}, 0);
    ASSERT_TRUE(stopped.has_value());
    ASSERT_EQ(*stopped, 1U);
    EXPECT_EQ(event[0].kind, AudioCompletionKind::Stopped);
    EXPECT_EQ(event[0].voice, *voice);
    auto duplicate = engine->pumpCompletions(std::span<AudioCompletionEvent>{event}, 0);
    ASSERT_TRUE(duplicate.has_value());
    EXPECT_EQ(*duplicate, 0U);
}

TEST(AudioEngineTest, PcmStreamMinimumCapacityRecoversFractionalPitchAfterProducerRefill)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 1,
        .commandCapacity = 8,
        .completionCapacity = 8,
        .streamBufferFrameCapacity = AudioPcmStreamMinBufferFrames,
    });
    ASSERT_TRUE(engine.has_value());
    auto voice = engine->playPcmStream(AudioPcmStreamDesc{
        .channels = 1,
        .sampleRate = 48000,
        .bufferCapacityFrames = AudioPcmStreamMinBufferFrames,
    });
    ASSERT_TRUE(voice.has_value());
    ASSERT_TRUE(engine->setVoicePitch(*voice, 0.5F).has_value());
    const float initial[2] = {0.0F, 1.0F};
    ASSERT_TRUE(engine
                    ->submitPcmStreamFrames(
                        *voice,
                        AudioPcmStreamChunkView{.frames = initial, .frameCount = 2})
                    .has_value());
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());

    float firstHalf[1]{};
    engine->mixRealtime(firstHalf, 1, 1, 48000);
    EXPECT_FLOAT_EQ(firstHalf[0], 0.0F);
    const float refill[1] = {2.0F};
    expectFailureCode(engine->submitPcmStreamFrames(
                          *voice,
                          AudioPcmStreamChunkView{.frames = refill, .frameCount = 1}),
                      AudioErrorCode::CapacityExceeded);

    float secondHalf[1]{};
    engine->mixRealtime(secondHalf, 1, 1, 48000);
    EXPECT_FLOAT_EQ(secondHalf[0], 0.5F);
    ASSERT_TRUE(engine
                    ->submitPcmStreamFrames(
                        *voice,
                        AudioPcmStreamChunkView{.frames = refill, .frameCount = 1})
                    .has_value());
    ASSERT_TRUE(engine->signalPcmStreamEof(*voice).has_value());

    float tail[4]{};
    engine->mixRealtime(tail, 4, 1, 48000);
    EXPECT_FLOAT_EQ(tail[0], 1.0F);
    EXPECT_FLOAT_EQ(tail[1], 1.5F);
    EXPECT_FLOAT_EQ(tail[2], 2.0F);
    EXPECT_FLOAT_EQ(tail[3], 2.0F);
    AudioCompletionEvent events[2]{};
    auto stopped = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(stopped.has_value());
    ASSERT_EQ(*stopped, 1U);
    EXPECT_EQ(events[0].kind, AudioCompletionKind::Stopped);
    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->activeMixVoices, 0U);
    EXPECT_EQ(stats->streamUnderrunFrames, 0U);
}

TEST(AudioEngineTest, StreamTerminalDebtHasPriorityOverContinuousOrdinaryCompletions)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 2,
        .commandCapacity = 8,
        .completionCapacity = 1,
        .streamBufferFrameCapacity = 2,
    });
    ASSERT_TRUE(engine.has_value());
    auto stream = engine->playPcmStream(AudioPcmStreamDesc{
        .channels = 1,
        .sampleRate = 48000,
        .bufferCapacityFrames = 2,
    });
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(engine->pumpCompletions(std::span<AudioCompletionEvent>{}, 0).has_value());
    ASSERT_TRUE(engine->cancelPcmStream(*stream).has_value());
    ASSERT_TRUE(engine->pumpCompletions(std::span<AudioCompletionEvent>{}, 0).has_value());

    auto ordinary = engine->createVoice();
    ASSERT_TRUE(ordinary.has_value());
    const float clip[2] = {0.25F, 0.25F};
    ASSERT_TRUE(engine
                    ->bindVoiceClip(*ordinary,
                                    AudioPcmClipView{
                                        .frames = clip,
                                        .frameCount = 2,
                                        .channels = 1,
                                        .sampleRate = 48000,
                                    })
                    .has_value());
    ASSERT_TRUE(engine->enqueuePlay(*ordinary).has_value());

    AudioCompletionEvent event[1]{};
    auto firstDrain = engine->pumpCompletions(std::span<AudioCompletionEvent>{event}, 0);
    ASSERT_TRUE(firstDrain.has_value());
    ASSERT_EQ(*firstDrain, 1U);
    EXPECT_EQ(event[0].kind, AudioCompletionKind::Started);
    EXPECT_EQ(event[0].voice, *stream);

    ASSERT_TRUE(engine->enqueueStop(*ordinary).has_value());
    ASSERT_TRUE(engine->enqueuePlay(*ordinary).has_value());
    auto terminalDrain = engine->pumpCompletions(std::span<AudioCompletionEvent>{event}, 0);
    ASSERT_TRUE(terminalDrain.has_value());
    ASSERT_EQ(*terminalDrain, 1U);
    EXPECT_EQ(event[0].kind, AudioCompletionKind::Cancelled);
    EXPECT_EQ(event[0].voice, *stream);
}

TEST(AudioEngineTest, StreamStopAndEofAbsorbLaterPlayAndFinishExactlyOnce)
{
    {
        auto engine = AudioEngine::Create(AudioEngineConfig{
            .voiceCapacity = 1,
            .commandCapacity = 8,
            .completionCapacity = 8,
            .streamBufferFrameCapacity = 2,
        });
        ASSERT_TRUE(engine.has_value());
        auto voice = engine->playPcmStream(AudioPcmStreamDesc{
            .channels = 1,
            .sampleRate = 48000,
            .bufferCapacityFrames = 2,
        });
        ASSERT_TRUE(voice.has_value());
        ASSERT_TRUE(engine->enqueueStop(*voice).has_value());
        expectFailureCode(engine->enqueuePlay(*voice), AudioErrorCode::InvalidConfiguration);
        const float frame[1] = {0.5F};
        expectFailureCode(engine->submitPcmStreamFrames(
                              *voice,
                              AudioPcmStreamChunkView{.frames = frame, .frameCount = 1}),
                          AudioErrorCode::InvalidConfiguration);

        AudioCompletionEvent events[2]{};
        auto stopped = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
        ASSERT_TRUE(stopped.has_value());
        ASSERT_EQ(*stopped, 1U);
        EXPECT_EQ(events[0].kind, AudioCompletionKind::Stopped);
        auto second = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(*second, 0U);
        auto stats = engine->stats();
        ASSERT_TRUE(stats.has_value());
        EXPECT_EQ(stats->completedStarted, 0U);
        EXPECT_EQ(stats->completedStopped, 1U);
        EXPECT_EQ(stats->activeMixVoices, 0U);
    }

    {
        auto engine = AudioEngine::Create(AudioEngineConfig{
            .voiceCapacity = 1,
            .commandCapacity = 8,
            .completionCapacity = 8,
            .streamBufferFrameCapacity = 2,
        });
        ASSERT_TRUE(engine.has_value());
        auto voice = engine->playPcmStream(AudioPcmStreamDesc{
            .channels = 1,
            .sampleRate = 48000,
            .bufferCapacityFrames = 2,
        });
        ASSERT_TRUE(voice.has_value());
        const float frames[2] = {0.25F, 0.75F};
        ASSERT_TRUE(engine
                        ->submitPcmStreamFrames(
                            *voice,
                            AudioPcmStreamChunkView{.frames = frames, .frameCount = 2})
                        .has_value());
        ASSERT_TRUE(engine->signalPcmStreamEof(*voice).has_value());
        expectFailureCode(engine->enqueuePlay(*voice), AudioErrorCode::InvalidConfiguration);
        expectFailureCode(engine->submitPcmStreamFrames(
                              *voice,
                              AudioPcmStreamChunkView{.frames = frames, .frameCount = 1}),
                          AudioErrorCode::InvalidConfiguration);
        ASSERT_TRUE(engine->pumpCompletions(4).has_value());
        float out[2]{};
        engine->mixRealtime(out, 2, 1, 48000);

        AudioCompletionEvent events[2]{};
        auto stopped = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
        ASSERT_TRUE(stopped.has_value());
        ASSERT_EQ(*stopped, 1U);
        EXPECT_EQ(events[0].kind, AudioCompletionKind::Stopped);
        auto second = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(*second, 0U);
        auto stats = engine->stats();
        ASSERT_TRUE(stats.has_value());
        EXPECT_EQ(stats->completedStopped, 1U);
        EXPECT_EQ(stats->activeMixVoices, 0U);
    }
}

TEST(AudioEngineTest, CancelWinsAgainstAlreadyFinishedEofCallbackWithoutSecondStop)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 1,
        .commandCapacity = 8,
        .completionCapacity = 8,
        .streamBufferFrameCapacity = 2,
    });
    ASSERT_TRUE(engine.has_value());
    auto voice = engine->playPcmStream(AudioPcmStreamDesc{
        .channels = 1,
        .sampleRate = 48000,
        .bufferCapacityFrames = 2,
    });
    ASSERT_TRUE(voice.has_value());
    const float frames[2] = {0.25F, 0.75F};
    ASSERT_TRUE(engine
                    ->submitPcmStreamFrames(
                        *voice,
                        AudioPcmStreamChunkView{.frames = frames, .frameCount = 2})
                    .has_value());
    ASSERT_TRUE(engine->signalPcmStreamEof(*voice).has_value());
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());
    float out[2]{};
    engine->mixRealtime(out, 2, 1, 48000);
    ASSERT_TRUE(engine->cancelPcmStream(*voice).has_value());

    AudioCompletionEvent events[2]{};
    auto cancelled = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(cancelled.has_value());
    ASSERT_EQ(*cancelled, 1U);
    EXPECT_EQ(events[0].kind, AudioCompletionKind::Cancelled);
    auto second = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, 0U);
    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->completedCancelled, 1U);
    EXPECT_EQ(stats->completedStopped, 0U);
    EXPECT_EQ(stats->activeMixVoices, 0U);
}

TEST(AudioEngineTest, OverlappingRealtimeConsumerReturnsSilenceAndShutdownClosesRealtimeGate)
{
    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 1,
        .commandCapacity = 8,
        .completionCapacity = 8,
        .streamBufferFrameCapacity = 2,
    });
    ASSERT_TRUE(engine.has_value());
    auto voice = engine->playPcmStream(AudioPcmStreamDesc{
        .channels = 1,
        .sampleRate = 48000,
        .bufferCapacityFrames = 2,
    });
    ASSERT_TRUE(voice.has_value());
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());

    std::vector<float> longOutput(65'536U, 1.0F);
    std::atomic<bool> callbackReady{false};
    std::atomic<bool> stopCallback{false};
    std::atomic<bool> callbackDone{false};
    std::thread callback([&] {
        callbackReady.store(true, std::memory_order_release);
        callbackReady.notify_one();
        do
        {
            engine->mixRealtime(longOutput.data(),
                                static_cast<Core::u32>(longOutput.size()),
                                1,
                                48000);
        } while (!stopCallback.load(std::memory_order_acquire));
        callbackDone.store(true, std::memory_order_release);
    });

    const bool callbackActive =
        observeRealtimeCallbackOverlap(*engine, callbackReady, callbackDone);
    if (!callbackActive)
    {
        stopCallback.store(true, std::memory_order_release);
        callback.join();
        FAIL() << "Could not overlap the owner probe with the realtime callback";
        return;
    }

    engine->shutdown();
    auto statsAtClose = engine->stats();
    ASSERT_TRUE(statsAtClose.has_value());
    for (Core::u32 attempt = 0; attempt < 1000U; ++attempt)
    {
        std::this_thread::yield();
    }
    auto statsAfterClosedCalls = engine->stats();
    ASSERT_TRUE(statsAfterClosedCalls.has_value());
    EXPECT_EQ(statsAfterClosedCalls->mixFramesRendered, statsAtClose->mixFramesRendered);
    stopCallback.store(true, std::memory_order_release);
    callback.join();
    EXPECT_TRUE(callbackDone.load(std::memory_order_acquire));
    EXPECT_EQ(engine->state(), AudioEngineState::Stopped);
    auto stats = engine->stats();
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->liveVoices, 0U);
    EXPECT_EQ(stats->streamingVoices, 0U);
    EXPECT_EQ(stats->activeMixVoices, 0U);
}

} // namespace
} // namespace Tina::Audio
