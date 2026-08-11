#include <tina/asset/AssetStore.hpp>
#include <tina/scene/SceneErrors.hpp>
#include <tina/scene/SpriteAnimator2D.hpp>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <memory_resource>
#include <optional>
#include <vector>

namespace Tina::Scene {
namespace {

[[nodiscard]] Core::AssetId fixtureAssetId(u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] SpriteAnimationFrame2D frame(
    Asset::AssetHandle sprite,
    u32 frameNumber,
    double durationSeconds)
{
    return SpriteAnimationFrame2D{
        .sprite = SpriteRenderer2D{
            .sprite = sprite,
            .overrides = SpriteOverrideFlags::Size | SpriteOverrideFlags::UvRect,
            .sizeOverrideMeters = {1.0F, 1.0F},
            .uvRectOverride = {
                .u0 = static_cast<float>(frameNumber - 1U) * 0.1F,
                .v0 = 0.0F,
                .u1 = static_cast<float>(frameNumber) * 0.1F,
                .v1 = 1.0F,
            },
        },
        .duration = Core::Duration{durationSeconds},
    };
}

class SpriteAnimator2DAssetTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto store = Asset::AssetStore::Create({.capacity = 3, .memoryResource = &memory_});
        ASSERT_TRUE(store.has_value()) << (store ? "" : store.error().message);
        store_.emplace(std::move(*store));
        for (usize index = 0; index < sprites_.size(); ++index)
        {
            auto sprite = store_->beginQueued(
                fixtureAssetId(static_cast<u8>(index + 1U)),
                AssetFormat::AssetKind::Sprite);
            ASSERT_TRUE(sprite.has_value());
            sprites_[index] = *sprite;
        }
    }

    [[nodiscard]] SpriteAnimationFrame2D testFrame(u32 frameNumber, double durationSeconds) const
    {
        return frame(sprites_[frameNumber - 1U], frameNumber, durationSeconds);
    }

    std::pmr::unsynchronized_pool_resource memory_{};
    std::optional<Asset::AssetStore> store_{};
    std::array<Asset::AssetHandle, 3> sprites_{};
};

TEST_F(SpriteAnimator2DAssetTest, RejectsEmptyInvalidFramesAndInvalidPlaybackMode)
{
    auto empty = SpriteAnimator2D::Create({});
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.error().code, SceneErrorCode::InvalidAnimation);

    const std::array badDuration{testFrame(1, 0.0)};
    auto invalidDuration = SpriteAnimator2D::Create({.frames = badDuration});
    ASSERT_FALSE(invalidDuration);
    EXPECT_EQ(invalidDuration.error().code, SceneErrorCode::InvalidAnimation);

    auto missingSpriteFrame = testFrame(1, 0.1);
    missingSpriteFrame.sprite.sprite = {};
    const std::array missingSprite{missingSpriteFrame};
    auto invalidSprite = SpriteAnimator2D::Create({.frames = missingSprite});
    ASSERT_FALSE(invalidSprite);
    EXPECT_EQ(invalidSprite.error().code, SceneErrorCode::InvalidAnimation);

    const std::array validFrames{testFrame(1, 0.1)};
    auto invalidMode = SpriteAnimator2D::Create({
        .frames = validFrames,
        .playbackMode = static_cast<SpriteAnimationPlaybackMode>(255),
    });
    ASSERT_FALSE(invalidMode);
    EXPECT_EQ(invalidMode.error().code, SceneErrorCode::InvalidAnimation);
}

TEST_F(SpriteAnimator2DAssetTest, LoopAdvancesAcrossMultipleFramesAndLargeDelta)
{
    const std::array frames{testFrame(1, 0.1), testFrame(2, 0.2), testFrame(3, 0.3)};
    auto animator = SpriteAnimator2D::Create({.frames = frames});
    ASSERT_TRUE(animator);
    ASSERT_NE(animator->currentSprite(), nullptr);
    EXPECT_EQ(animator->currentSprite()->sprite, sprites_[0]);

    auto first = animator->update(Core::Duration{0.1});
    ASSERT_TRUE(first);
    EXPECT_EQ(first->currentFrameIndex, 1U);
    EXPECT_TRUE(first->currentFrameChanged);

    auto second = animator->update(Core::Duration{0.2});
    ASSERT_TRUE(second);
    EXPECT_EQ(second->currentFrameIndex, 2U);

    auto wrapped = animator->update(Core::Duration{120.35});
    ASSERT_TRUE(wrapped);
    EXPECT_EQ(wrapped->currentFrameIndex, 0U);
    EXPECT_EQ(animator->currentSprite()->sprite, sprites_[0]);
    EXPECT_TRUE(animator->isPlaying());
    EXPECT_FALSE(animator->isCompleted());
}

TEST_F(SpriteAnimator2DAssetTest, OnceStopsOnLastFrameAndPlayRestarts)
{
    const std::array frames{testFrame(1, 0.1), testFrame(2, 0.2), testFrame(3, 0.3)};
    auto animator = SpriteAnimator2D::Create({
        .frames = frames,
        .playbackMode = SpriteAnimationPlaybackMode::Once,
    });
    ASSERT_TRUE(animator);

    auto completed = animator->update(Core::Duration{3.0});
    ASSERT_TRUE(completed);
    EXPECT_TRUE(completed->completedThisUpdate);
    EXPECT_EQ(animator->frameIndex(), 2U);
    EXPECT_FALSE(animator->isPlaying());
    EXPECT_TRUE(animator->isCompleted());

    animator->play();
    EXPECT_EQ(animator->frameIndex(), 0U);
    EXPECT_TRUE(animator->isPlaying());
    EXPECT_FALSE(animator->isCompleted());
}

TEST_F(SpriteAnimator2DAssetTest, PingPongVisitsInteriorFramesInReverse)
{
    const std::array frames{testFrame(1, 0.1), testFrame(2, 0.1), testFrame(3, 0.1)};
    auto animator = SpriteAnimator2D::Create({
        .frames = frames,
        .playbackMode = SpriteAnimationPlaybackMode::PingPong,
    });
    ASSERT_TRUE(animator);

    ASSERT_TRUE(animator->update(Core::Duration{0.1}));
    EXPECT_EQ(animator->frameIndex(), 1U);
    ASSERT_TRUE(animator->update(Core::Duration{0.1}));
    EXPECT_EQ(animator->frameIndex(), 2U);
    ASSERT_TRUE(animator->update(Core::Duration{0.1}));
    EXPECT_EQ(animator->frameIndex(), 1U);
    ASSERT_TRUE(animator->update(Core::Duration{0.1}));
    EXPECT_EQ(animator->frameIndex(), 0U);
}

TEST_F(SpriteAnimator2DAssetTest, CopiesClipFramesAndHonorsPauseStopAndSpeed)
{
    std::array frames{testFrame(1, 0.1), testFrame(2, 0.1)};
    auto animator = SpriteAnimator2D::Create({.frames = frames});
    ASSERT_TRUE(animator);
    frames[0].sprite.sprite = sprites_[2];
    EXPECT_EQ(animator->currentSprite()->sprite, sprites_[0]);

    animator->pause();
    ASSERT_TRUE(animator->update(Core::Duration{1.0}));
    EXPECT_EQ(animator->frameIndex(), 0U);

    ASSERT_TRUE(animator->setPlaybackSpeed(2.0F));
    animator->play();
    ASSERT_TRUE(animator->update(Core::Duration{0.05}));
    EXPECT_EQ(animator->frameIndex(), 1U);

    animator->stop();
    EXPECT_EQ(animator->frameIndex(), 0U);
    EXPECT_FALSE(animator->isPlaying());
}

TEST_F(SpriteAnimator2DAssetTest, InvalidDeltaAndSpeedLeavePlaybackStateUnchanged)
{
    const std::array frames{testFrame(1, 0.1), testFrame(2, 0.1)};
    auto animator = SpriteAnimator2D::Create({.frames = frames});
    ASSERT_TRUE(animator);

    auto negative = animator->update(Core::Duration{-0.1});
    ASSERT_FALSE(negative);
    EXPECT_EQ(animator->frameIndex(), 0U);

    auto infinite = animator->update(Core::Duration{(std::numeric_limits<double>::infinity)()});
    ASSERT_FALSE(infinite);
    EXPECT_EQ(animator->frameIndex(), 0U);

    EXPECT_FALSE(animator->setPlaybackSpeed(0.0F));
    EXPECT_FALSE(animator->setPlaybackSpeed((std::numeric_limits<float>::quiet_NaN)()));
    EXPECT_FLOAT_EQ(animator->playbackSpeed(), 1.0F);
}

// ---------------------------------------------------------------------------
// Animation events
// ---------------------------------------------------------------------------

[[nodiscard]] std::vector<u32> crossedTags(const SpriteAnimator2DUpdate& update)
{
    std::vector<u32> tags;
    tags.reserve(update.crossedEvents.size());
    for (const SpriteAnimationEventCrossing2D& crossing : update.crossedEvents) {
        tags.push_back(crossing.tag);
    }
    return tags;
}

TEST_F(SpriteAnimator2DAssetTest, RejectsInvalidEventTagsAndOffsets)
{
    const std::array zeroTag{SpriteAnimationEvent2D{.tag = 0, .normalizedOffset = 0.5F}};
    std::array framesZeroTag{testFrame(1, 0.1)};
    framesZeroTag[0].events = zeroTag;
    EXPECT_FALSE(SpriteAnimator2D::Create({.frames = framesZeroTag}));

    for (const float offset : {-0.01F,
                               1.01F,
                               (std::numeric_limits<float>::quiet_NaN)(),
                               (std::numeric_limits<float>::infinity)()}) {
        const std::array bad{SpriteAnimationEvent2D{.tag = 7, .normalizedOffset = offset}};
        std::array badFrames{testFrame(1, 0.1)};
        badFrames[0].events = bad;
        EXPECT_FALSE(SpriteAnimator2D::Create({.frames = badFrames}));
    }

    // Zero capacity would make every update overflow, so it is rejected up front.
    const std::array ok{SpriteAnimationEvent2D{.tag = 7, .normalizedOffset = 0.5F}};
    std::array okFrames{testFrame(1, 0.1)};
    okFrames[0].events = ok;
    EXPECT_FALSE(SpriteAnimator2D::Create(
        {.frames = okFrames}, *std::pmr::get_default_resource(), {.eventCapacity = 0}));
    EXPECT_TRUE(SpriteAnimator2D::Create({.frames = okFrames}));
}

TEST_F(SpriteAnimator2DAssetTest, FiresEventsInTemporalOrderWithinOneFrame)
{
    // Deliberately authored out of order; crossings must still be temporal.
    const std::array events{
        SpriteAnimationEvent2D{.tag = 30, .normalizedOffset = 0.75F},
        SpriteAnimationEvent2D{.tag = 10, .normalizedOffset = 0.25F},
        SpriteAnimationEvent2D{.tag = 20, .normalizedOffset = 0.5F},
    };
    std::array frames{testFrame(1, 1.0), testFrame(2, 1.0)};
    frames[0].events = events;
    auto animator = SpriteAnimator2D::Create({.frames = frames});
    ASSERT_TRUE(animator);

    // 0.0 -> 0.4 crosses only the 0.25 marker.
    auto first = animator->update(Core::Duration{0.4});
    ASSERT_TRUE(first);
    EXPECT_EQ(crossedTags(*first), (std::vector<u32>{10}));
    EXPECT_FALSE(first->crossedEventOverflow);
    EXPECT_EQ(first->crossedEvents[0].frameIndex, 0U);
    EXPECT_TRUE(first->crossedEvents[0].forward);
    EXPECT_FLOAT_EQ(first->crossedEvents[0].normalizedOffset, 0.25F);

    // 0.4 -> 0.9 crosses 0.5 then 0.75, in that order.
    auto second = animator->update(Core::Duration{0.5});
    ASSERT_TRUE(second);
    EXPECT_EQ(crossedTags(*second), (std::vector<u32>{20, 30}));

    // 0.9 -> 1.4 leaves the frame; nothing remains in it.
    auto third = animator->update(Core::Duration{0.5});
    ASSERT_TRUE(third);
    EXPECT_TRUE(third->crossedEvents.empty());
}

TEST_F(SpriteAnimator2DAssetTest, EventsFireExactlyOnceAcrossConsecutiveUpdates)
{
    // Offset 0.0 sits exactly on the frame boundary: the half-open crossing
    // window must not report it twice when an update lands on that instant.
    const std::array first{SpriteAnimationEvent2D{.tag = 1, .normalizedOffset = 0.0F}};
    const std::array second{SpriteAnimationEvent2D{.tag = 2, .normalizedOffset = 0.0F}};
    std::array frames{testFrame(1, 0.5), testFrame(2, 0.5)};
    frames[0].events = first;
    frames[1].events = second;
    auto animator = SpriteAnimator2D::Create({.frames = frames});
    ASSERT_TRUE(animator);

    const auto step = [&animator](std::vector<u32>& seen) {
        auto update = animator->update(Core::Duration{0.25});
        ASSERT_TRUE(update);
        for (const u32 tag : crossedTags(*update)) {
            seen.push_back(tag);
        }
    };

    // Four 0.25s steps cover exactly one 1.0s cycle. Each boundary event is
    // crossed exactly once: an event sitting on the instant the playhead lands
    // on does not fire yet, it fires on the update that starts from it. That is
    // what keeps a boundary event from firing twice on consecutive updates.
    std::vector<u32> firstCycle;
    for (int index = 0; index < 4; ++index) {
        step(firstCycle);
    }
    EXPECT_EQ(firstCycle, (std::vector<u32>{1, 2}));

    // The wrapped playhead is back on frame 0's head, so the next step fires it
    // again -- once per cycle, never twice.
    std::vector<u32> nextCycle;
    step(nextCycle);
    EXPECT_EQ(nextCycle, (std::vector<u32>{1}));
}

TEST_F(SpriteAnimator2DAssetTest, LoopWrapEmitsTailThenHeadInOrder)
{
    const std::array tail{SpriteAnimationEvent2D{.tag = 99, .normalizedOffset = 0.9F}};
    const std::array head{SpriteAnimationEvent2D{.tag = 11, .normalizedOffset = 0.1F}};
    std::array frames{testFrame(1, 0.5), testFrame(2, 0.5)};
    frames[0].events = head;
    frames[1].events = tail;
    auto animator = SpriteAnimator2D::Create({.frames = frames});
    ASSERT_TRUE(animator);

    // Land just past the frame-1 tail event at t=0.95 without wrapping.
    auto warmup = animator->update(Core::Duration{0.97});
    ASSERT_TRUE(warmup);
    EXPECT_EQ(crossedTags(*warmup), (std::vector<u32>{11, 99}));

    // 0.97 -> 1.10 wraps: nothing left in this cycle, then frame 0's 0.05 head.
    auto wrapped = animator->update(Core::Duration{0.13});
    ASSERT_TRUE(wrapped);
    EXPECT_EQ(crossedTags(*wrapped), (std::vector<u32>{11}));
}

TEST_F(SpriteAnimator2DAssetTest, MultiLapDeltaReportsEachEventOnce)
{
    const std::array a{SpriteAnimationEvent2D{.tag = 1, .normalizedOffset = 0.5F}};
    const std::array b{SpriteAnimationEvent2D{.tag = 2, .normalizedOffset = 0.5F}};
    std::array frames{testFrame(1, 0.1), testFrame(2, 0.1)};
    frames[0].events = a;
    frames[1].events = b;
    auto animator = SpriteAnimator2D::Create({.frames = frames});
    ASSERT_TRUE(animator);

    // 2.0s over a 0.2s cycle is ten laps. Without lap clamping this would report
    // twenty crossings; the contract is at most one per authored event.
    auto update = animator->update(Core::Duration{2.0});
    ASSERT_TRUE(update);
    EXPECT_EQ(crossedTags(*update), (std::vector<u32>{1, 2}));
    EXPECT_FALSE(update->crossedEventOverflow);
}

TEST_F(SpriteAnimator2DAssetTest, OnceModeFiresFinalFrameEndEventOnCompletion)
{
    const std::array mid{SpriteAnimationEvent2D{.tag = 5, .normalizedOffset = 0.5F}};
    // Offset 1.0 on the last frame is the clip's final instant.
    const std::array end{SpriteAnimationEvent2D{.tag = 6, .normalizedOffset = 1.0F}};
    std::array frames{testFrame(1, 0.1), testFrame(2, 0.1)};
    frames[0].events = mid;
    frames[1].events = end;
    auto animator = SpriteAnimator2D::Create(
        {.frames = frames, .playbackMode = SpriteAnimationPlaybackMode::Once});
    ASSERT_TRUE(animator);

    auto update = animator->update(Core::Duration{5.0});
    ASSERT_TRUE(update);
    EXPECT_TRUE(update->completedThisUpdate);
    EXPECT_EQ(crossedTags(*update), (std::vector<u32>{5, 6}));

    // Completed and stopped: no further crossings, and no re-fire.
    auto after = animator->update(Core::Duration{1.0});
    ASSERT_TRUE(after);
    EXPECT_TRUE(after->crossedEvents.empty());
}

TEST_F(SpriteAnimator2DAssetTest, PingPongReverseSegmentFiresMirroredEvents)
{
    // 3 frames of 1.0s: forward 0,1,2 then reverse 1. Total timeline 4.0s.
    // The event at offset 0.25 of frame 1 is crossed at t=1.25 going forward and
    // again at t=3.75 going backwards, because reverse mirrors the offset.
    const std::array events{SpriteAnimationEvent2D{.tag = 42, .normalizedOffset = 0.25F}};
    std::array frames{testFrame(1, 1.0), testFrame(2, 1.0), testFrame(3, 1.0)};
    frames[1].events = events;
    auto animator = SpriteAnimator2D::Create(
        {.frames = frames, .playbackMode = SpriteAnimationPlaybackMode::PingPong});
    ASSERT_TRUE(animator);

    auto forward = animator->update(Core::Duration{1.5});
    ASSERT_TRUE(forward);
    ASSERT_EQ(forward->crossedEvents.size(), 1U);
    EXPECT_EQ(forward->crossedEvents[0].tag, 42U);
    EXPECT_EQ(forward->crossedEvents[0].frameIndex, 1U);
    EXPECT_TRUE(forward->crossedEvents[0].forward);

    // 1.5 -> 3.5 has not yet reached the mirrored position at 3.75.
    auto middle = animator->update(Core::Duration{2.0});
    ASSERT_TRUE(middle);
    EXPECT_TRUE(middle->crossedEvents.empty());

    // 3.5 -> 3.9 crosses the reverse traversal of the same authored event.
    auto reverse = animator->update(Core::Duration{0.4});
    ASSERT_TRUE(reverse);
    ASSERT_EQ(reverse->crossedEvents.size(), 1U);
    EXPECT_EQ(reverse->crossedEvents[0].tag, 42U);
    EXPECT_EQ(reverse->crossedEvents[0].frameIndex, 1U);
    EXPECT_FALSE(reverse->crossedEvents[0].forward);
    // The reported offset stays the authored one, not the mirrored position.
    EXPECT_FLOAT_EQ(reverse->crossedEvents[0].normalizedOffset, 0.25F);
}

TEST_F(SpriteAnimator2DAssetTest, OverflowTruncatesInOrderAndReportsFlag)
{
    // Six events, capacity two: the first two in temporal order survive.
    const std::array events{
        SpriteAnimationEvent2D{.tag = 1, .normalizedOffset = 0.1F},
        SpriteAnimationEvent2D{.tag = 2, .normalizedOffset = 0.2F},
        SpriteAnimationEvent2D{.tag = 3, .normalizedOffset = 0.3F},
        SpriteAnimationEvent2D{.tag = 4, .normalizedOffset = 0.4F},
        SpriteAnimationEvent2D{.tag = 5, .normalizedOffset = 0.5F},
        SpriteAnimationEvent2D{.tag = 6, .normalizedOffset = 0.6F},
    };
    std::array frames{testFrame(1, 1.0)};
    frames[0].events = events;
    auto animator = SpriteAnimator2D::Create(
        {.frames = frames}, *std::pmr::get_default_resource(), {.eventCapacity = 2});
    ASSERT_TRUE(animator);
    EXPECT_EQ(animator->eventCapacity(), 2U);
    EXPECT_EQ(animator->clipEventCount(), 6U);

    auto update = animator->update(Core::Duration{0.95});
    ASSERT_TRUE(update);
    EXPECT_TRUE(update->crossedEventOverflow);
    EXPECT_EQ(crossedTags(*update), (std::vector<u32>{1, 2}));
}

TEST_F(SpriteAnimator2DAssetTest, PausedZeroDeltaAndEventlessClipsReportNoCrossings)
{
    const std::array events{SpriteAnimationEvent2D{.tag = 8, .normalizedOffset = 0.5F}};
    std::array frames{testFrame(1, 1.0), testFrame(2, 1.0)};
    frames[0].events = events;
    auto animator = SpriteAnimator2D::Create({.frames = frames});
    ASSERT_TRUE(animator);

    auto zero = animator->update(Core::Duration{0.0});
    ASSERT_TRUE(zero);
    EXPECT_TRUE(zero->crossedEvents.empty());

    animator->pause();
    auto paused = animator->update(Core::Duration{5.0});
    ASSERT_TRUE(paused);
    EXPECT_TRUE(paused->crossedEvents.empty());

    // A clip with no events at all keeps the span empty rather than failing.
    const std::array plain{testFrame(1, 0.1), testFrame(2, 0.1)};
    auto plainAnimator = SpriteAnimator2D::Create({.frames = plain});
    ASSERT_TRUE(plainAnimator);
    EXPECT_EQ(plainAnimator->clipEventCount(), 0U);
    auto plainUpdate = plainAnimator->update(Core::Duration{0.15});
    ASSERT_TRUE(plainUpdate);
    EXPECT_TRUE(plainUpdate->crossedEvents.empty());
}

TEST_F(SpriteAnimator2DAssetTest, SetClipReplacesEventStorageWithoutBorrowingCallerSpans)
{
    std::vector<SpriteAnimationEvent2D> scoped{
        SpriteAnimationEvent2D{.tag = 77, .normalizedOffset = 0.5F}};
    std::array frames{testFrame(1, 1.0)};
    frames[0].events = scoped;
    auto animator = SpriteAnimator2D::Create({.frames = frames});
    ASSERT_TRUE(animator);
    ASSERT_EQ(animator->clipEventCount(), 1U);

    // Destroy the caller's storage; the animator must have copied it.
    scoped.clear();
    scoped.shrink_to_fit();
    auto update = animator->update(Core::Duration{0.75});
    ASSERT_TRUE(update);
    ASSERT_EQ(update->crossedEvents.size(), 1U);
    EXPECT_EQ(update->crossedEvents[0].tag, 77U);

    // Replacing the clip drops the old events entirely.
    const std::array plain{testFrame(2, 0.1)};
    ASSERT_TRUE(animator->setClip({.frames = plain}));
    EXPECT_EQ(animator->clipEventCount(), 0U);
    auto after = animator->update(Core::Duration{0.05});
    ASSERT_TRUE(after);
    EXPECT_TRUE(after->crossedEvents.empty());
}

} // namespace
} // namespace Tina::Scene
