#include <gtest/gtest.h>

#include "detail/UILayoutPrimitives.hpp"

#include <cmath>
#include <limits>

namespace Tina::Tests {
namespace {

TEST(UILayoutPrimitivesTests, NormalizesNegativeZeroAndChecksFiniteExtent)
{
    const float normalized = UI::Detail::normalizeFloat(-0.0F);

    EXPECT_FLOAT_EQ(normalized, 0.0F);
    EXPECT_FALSE(std::signbit(normalized));
    EXPECT_TRUE(UI::Detail::isFiniteNonNegative(0.0F));
    EXPECT_FALSE(UI::Detail::isFiniteNonNegative(-1.0F));
    EXPECT_FALSE(UI::Detail::isFiniteNonNegative(
        (std::numeric_limits<float>::infinity)()));
}

TEST(UILayoutPrimitivesTests, ResolvesPixelsAndPercentWithExplicitFallbackCount)
{
    UI::Detail::LayoutPassStatistics statistics{};

    const auto pixels = UI::Detail::resolveLength(
        UI::UILayoutLength::Px(12.0F), false, 0.0F, statistics);
    const auto percent = UI::Detail::resolveLength(
        UI::UILayoutLength::Percent(25.0F), true, 200.0F, statistics);
    const auto fallback = UI::Detail::resolveLength(
        UI::UILayoutLength::Percent(25.0F), false, 200.0F, statistics);

    EXPECT_TRUE(pixels.hasValue);
    EXPECT_FLOAT_EQ(pixels.value, 12.0F);
    EXPECT_TRUE(percent.hasValue);
    EXPECT_FLOAT_EQ(percent.value, 50.0F);
    EXPECT_FALSE(fallback.hasValue);
    EXPECT_EQ(statistics.percentMeasureFallbackCount, 1U);
}

TEST(UILayoutPrimitivesTests, NoFallbackResolverRejectsUnresolvedLengths)
{
    const auto unresolved = UI::Detail::resolveLengthNoFallbackCount(
        UI::UILayoutLength::Percent(50.0F), false, 100.0F);
    const auto automatic = UI::Detail::resolveLengthNoFallbackCount(
        UI::UILayoutLength::Auto(), true, 100.0F);

    EXPECT_FALSE(unresolved.hasValue);
    EXPECT_FALSE(automatic.hasValue);
}

TEST(UILayoutPrimitivesTests, MinimumWinsConflictingRangeAndOutputIsNonNegative)
{
    UI::Detail::LayoutPassStatistics statistics{};

    EXPECT_FLOAT_EQ(UI::Detail::clampWithMinMax(
                        15.0F, UI::UILayoutLength::Px(20.0F),
                        UI::UILayoutLength::Px(10.0F), false, 0.0F, statistics),
                    20.0F);
    EXPECT_FLOAT_EQ(UI::Detail::clampWithMinMax(
                        -10.0F, UI::UILayoutLength::Auto(),
                        UI::UILayoutLength::Auto(), false, 0.0F, statistics),
                    0.0F);
}

TEST(UILayoutPrimitivesTests, CombinesSpacingVisibilityAndIntersection)
{
    const UI::UIEdgeSpacing spacing{
        .left = 2.0F,
        .top = 3.0F,
        .right = 5.0F,
        .bottom = 7.0F,
    };
    const auto intersection = UI::Detail::intersectRects(
        UI::UILogicalRect{.x = 0.0F, .y = 0.0F, .width = 20.0F, .height = 10.0F},
        UI::UILogicalRect{.x = 5.0F, .y = 4.0F, .width = 20.0F, .height = 10.0F});

    EXPECT_FLOAT_EQ(UI::Detail::horizontalMargin(spacing), 7.0F);
    EXPECT_FLOAT_EQ(UI::Detail::verticalMargin(spacing), 10.0F);
    EXPECT_EQ(intersection,
              (UI::UILogicalRect{.x = 5.0F, .y = 4.0F, .width = 15.0F, .height = 6.0F}));
    EXPECT_EQ(UI::Detail::combineVisibility(UI::UIVisibility::Visible,
                                            UI::UIVisibility::Hidden),
              UI::UIVisibility::Hidden);
    EXPECT_EQ(UI::Detail::combineVisibility(UI::UIVisibility::Hidden,
                                            UI::UIVisibility::Collapsed),
              UI::UIVisibility::Collapsed);
}

TEST(UILayoutPrimitivesTests, HalfOpenPointContainmentExcludesRightAndBottom)
{
    constexpr UI::UILogicalRect Rect{
        .x = 10.0F,
        .y = 20.0F,
        .width = 30.0F,
        .height = 40.0F,
    };

    EXPECT_TRUE(UI::Detail::containsPointHalfOpen(Rect, {.x = 10.0F, .y = 20.0F}));
    EXPECT_TRUE(UI::Detail::containsPointHalfOpen(Rect, {.x = 39.0F, .y = 59.0F}));
    EXPECT_FALSE(UI::Detail::containsPointHalfOpen(Rect, {.x = 40.0F, .y = 20.0F}));
    EXPECT_FALSE(UI::Detail::containsPointHalfOpen(Rect, {.x = 10.0F, .y = 60.0F}));
}

TEST(UILayoutPrimitivesTests, WorkMaskPublishesOnlyRequestedCompletionBits)
{
    constexpr u8 Work = UI::Detail::LayoutWorkMeasure |
                        UI::Detail::LayoutWorkArrange;
    constexpr u8 Completion = UI::Detail::LayoutWorkMeasureComplete |
                              UI::Detail::LayoutWorkArrangeComplete;

    EXPECT_TRUE(UI::Detail::hasLayoutWork(Work, UI::Detail::LayoutWorkMeasure));
    EXPECT_TRUE(UI::Detail::hasLayoutWork(Work, UI::Detail::LayoutWorkArrange));
    EXPECT_EQ(UI::Detail::layoutSubtreeCompletionMask(Work), Completion);
    EXPECT_EQ(UI::Detail::layoutSubtreeCompletionMask(0), 0U);
}

} // namespace
} // namespace Tina::Tests
