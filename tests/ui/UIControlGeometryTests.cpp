#include <gtest/gtest.h>

#include "detail/UIControlGeometry.hpp"

#include <limits>

namespace Tina::Tests {
namespace {

TEST(UIControlGeometryTests, VerticalScrollBarMapsViewportContentAndOffset)
{
    const UI::UIScrollViewMetrics metrics{
        .offset = {.x = 0.0F, .y = 50.0F},
        .viewportSize = {.width = 100.0F, .height = 100.0F},
        .contentSize = {.width = 100.0F, .height = 400.0F},
        .verticalScrollBarVisible = true,
    };
    const UI::UIScrollViewPaint paint{
        .thickness = 10.0F,
        .minThumbExtent = 20.0F,
    };

    const auto geometry = UI::Detail::makeScrollBarGeometry(
        metrics,
        UI::UILogicalRect{.x = 10.0F, .y = 20.0F, .width = 100.0F, .height = 100.0F},
        paint, UI::UIScrollAxes::Vertical);

    EXPECT_TRUE(geometry.visible);
    EXPECT_FLOAT_EQ(geometry.track.x, 110.0F);
    EXPECT_FLOAT_EQ(geometry.track.y, 20.0F);
    EXPECT_FLOAT_EQ(geometry.track.width, 10.0F);
    EXPECT_FLOAT_EQ(geometry.track.height, 100.0F);
    EXPECT_FLOAT_EQ(geometry.thumb.x, 110.0F);
    EXPECT_FLOAT_EQ(geometry.thumb.y, 32.5F);
    EXPECT_FLOAT_EQ(geometry.thumb.width, 10.0F);
    EXPECT_FLOAT_EQ(geometry.thumb.height, 25.0F);
}

TEST(UIControlGeometryTests, HiddenScrollBarProducesEmptyGeometry)
{
    const auto geometry = UI::Detail::makeScrollBarGeometry(
        UI::UIScrollViewMetrics{}, UI::UILogicalRect{},
        UI::UIScrollViewPaint{}, UI::UIScrollAxes::Horizontal);

    EXPECT_FALSE(geometry.visible);
    EXPECT_EQ(geometry.track, UI::UILogicalRect{});
    EXPECT_EQ(geometry.thumb, UI::UILogicalRect{});
}

TEST(UIControlGeometryTests, SliderGeometryUsesTrackAndThumbCenters)
{
    const UI::UISliderPaint paint{
        .contentInset = 4.0F,
        .thumbWidth = 8.0F,
    };
    const auto geometry = UI::Detail::sliderPaintGeometry(
        UI::UILogicalRect{.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 20.0F},
        0.0F, 10.0F, 5.0F, paint);

    EXPECT_FLOAT_EQ(geometry.fraction, 0.5F);
    EXPECT_EQ(geometry.filledTrack,
              (UI::UILogicalRect{.x = 4.0F, .y = 4.0F, .width = 46.0F, .height = 12.0F}));
    EXPECT_EQ(geometry.thumb,
              (UI::UILogicalRect{.x = 46.0F, .y = 0.0F, .width = 8.0F, .height = 20.0F}));
}

TEST(UIControlGeometryTests, RangeFractionRejectsInvalidAndClampsFiniteValues)
{
    EXPECT_FLOAT_EQ(UI::Detail::normalizedRangeFraction(-5.0F, 0.0F, 10.0F), 0.0F);
    EXPECT_FLOAT_EQ(UI::Detail::normalizedRangeFraction(15.0F, 0.0F, 10.0F), 1.0F);
    EXPECT_FLOAT_EQ(UI::Detail::normalizedRangeFraction(5.0F, 10.0F, 10.0F), 0.0F);
    EXPECT_FLOAT_EQ(
        UI::Detail::normalizedRangeFraction(
            (std::numeric_limits<float>::quiet_NaN)(), 0.0F, 10.0F),
        0.0F);
}

TEST(UIControlGeometryTests, TreeDisclosureUsesLevelAndClampsToRowHeight)
{
    UI::UITreeViewStyle style{};
    style.indentation = 12.0F;
    style.disclosureExtent = 20.0F;

    const auto rect = UI::Detail::makeTreeViewDisclosureRect(
        UI::UILogicalRect{.x = 10.0F, .y = 20.0F, .width = 100.0F, .height = 16.0F},
        style, 2);

    EXPECT_EQ(rect,
              (UI::UILogicalRect{.x = 42.0F, .y = 20.0F, .width = 16.0F, .height = 16.0F}));
}

} // namespace
} // namespace Tina::Tests
