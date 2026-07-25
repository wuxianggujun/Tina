#include <tina/scene/SceneErrors.hpp>
#include <tina/scene/SpriteAnimator2D.hpp>

#include <gtest/gtest.h>

#include <array>
#include <limits>

namespace Tina::Scene {
namespace {

[[nodiscard]] SpriteAnimationFrame2D frame(u32 spriteKey, double durationSeconds)
{
    return SpriteAnimationFrame2D{
        .sprite = SpriteRenderer2D{
            .spriteKey = spriteKey,
            .overrides = SpriteOverrideFlags::Size | SpriteOverrideFlags::UvRect,
            .sizeOverrideMeters = {1.0F, 1.0F},
            .uvRectOverride = {
                .u0 = static_cast<float>(spriteKey - 1U) * 0.1F,
                .v0 = 0.0F,
                .u1 = static_cast<float>(spriteKey) * 0.1F,
                .v1 = 1.0F,
            },
        },
        .duration = Core::Duration{durationSeconds},
    };
}

TEST(SpriteAnimator2DTest, RejectsEmptyInvalidFramesAndInvalidPlaybackMode)
{
    auto empty = SpriteAnimator2D::Create({});
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.error().code, SceneErrorCode::InvalidAnimation);

    const std::array badDuration{frame(1, 0.0)};
    auto invalidDuration = SpriteAnimator2D::Create({.frames = badDuration});
    ASSERT_FALSE(invalidDuration);
    EXPECT_EQ(invalidDuration.error().code, SceneErrorCode::InvalidAnimation);

    auto missingSpriteFrame = frame(1, 0.1);
    missingSpriteFrame.sprite.spriteKey = 0;
    const std::array missingSprite{missingSpriteFrame};
    auto invalidSprite = SpriteAnimator2D::Create({.frames = missingSprite});
    ASSERT_FALSE(invalidSprite);
    EXPECT_EQ(invalidSprite.error().code, SceneErrorCode::InvalidAnimation);

    const std::array validFrames{frame(1, 0.1)};
    auto invalidMode = SpriteAnimator2D::Create({
        .frames = validFrames,
        .playbackMode = static_cast<SpriteAnimationPlaybackMode>(255),
    });
    ASSERT_FALSE(invalidMode);
    EXPECT_EQ(invalidMode.error().code, SceneErrorCode::InvalidAnimation);
}

TEST(SpriteAnimator2DTest, LoopAdvancesAcrossMultipleFramesAndLargeDelta)
{
    const std::array frames{frame(1, 0.1), frame(2, 0.2), frame(3, 0.3)};
    auto animator = SpriteAnimator2D::Create({.frames = frames});
    ASSERT_TRUE(animator);
    ASSERT_NE(animator->currentSprite(), nullptr);
    EXPECT_EQ(animator->currentSprite()->spriteKey, 1U);

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
    EXPECT_EQ(animator->currentSprite()->spriteKey, 1U);
    EXPECT_TRUE(animator->isPlaying());
    EXPECT_FALSE(animator->isCompleted());
}

TEST(SpriteAnimator2DTest, OnceStopsOnLastFrameAndPlayRestarts)
{
    const std::array frames{frame(1, 0.1), frame(2, 0.2), frame(3, 0.3)};
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

TEST(SpriteAnimator2DTest, PingPongVisitsInteriorFramesInReverse)
{
    const std::array frames{frame(1, 0.1), frame(2, 0.1), frame(3, 0.1)};
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

TEST(SpriteAnimator2DTest, CopiesClipFramesAndHonorsPauseStopAndSpeed)
{
    std::array frames{frame(1, 0.1), frame(2, 0.1)};
    auto animator = SpriteAnimator2D::Create({.frames = frames});
    ASSERT_TRUE(animator);
    frames[0].sprite.spriteKey = 9;
    EXPECT_EQ(animator->currentSprite()->spriteKey, 1U);

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

TEST(SpriteAnimator2DTest, InvalidDeltaAndSpeedLeavePlaybackStateUnchanged)
{
    const std::array frames{frame(1, 0.1), frame(2, 0.1)};
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

} // namespace
} // namespace Tina::Scene
