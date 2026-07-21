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

} // namespace
} // namespace Tina::Audio
