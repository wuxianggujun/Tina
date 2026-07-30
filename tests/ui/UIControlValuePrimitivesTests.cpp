#include <gtest/gtest.h>

#include "detail/UIControlValuePrimitives.hpp"

#include <cmath>
#include <limits>

namespace Tina::Tests {
namespace {

TEST(UIControlValuePrimitivesTests, SliderValueClampsAndQuantizesFromMinimum)
{
    EXPECT_FLOAT_EQ(UI::Detail::quantizeSliderValue(-5.0, 0.0F, 10.0F, 2.0F),
                    0.0F);
    EXPECT_FLOAT_EQ(UI::Detail::quantizeSliderValue(15.0, 0.0F, 10.0F, 2.0F),
                    10.0F);
    EXPECT_FLOAT_EQ(UI::Detail::quantizeSliderValue(5.0, 0.0F, 10.0F, 2.0F),
                    6.0F);
    EXPECT_FLOAT_EQ(UI::Detail::quantizeSliderValue(5.0, 1.0F, 10.0F, 3.0F),
                    4.0F);
    EXPECT_FLOAT_EQ(UI::Detail::quantizeSliderValue(9.9, 1.0F, 10.0F, 3.0F),
                    10.0F);
}

TEST(UIControlValuePrimitivesTests, ContinuousAndDegenerateRangesRemainStable)
{
    EXPECT_FLOAT_EQ(UI::Detail::quantizeSliderValue(3.25, 0.0F, 10.0F, 0.0F),
                    3.25F);
    EXPECT_FLOAT_EQ(
        UI::Detail::quantizeSliderValue(
            3.25, 0.0F, 10.0F,
            (std::numeric_limits<float>::infinity)()),
        3.25F);
    EXPECT_FLOAT_EQ(UI::Detail::quantizeSliderValue(9.0, 4.0F, 4.0F, 1.0F),
                    4.0F);
}

TEST(UIControlValuePrimitivesTests, SliderPointerUsesCommittedTrackCentersAndRejectsZeroTravel)
{
    const UI::UILogicalRect rect{
        .x = 0.0F,
        .y = 0.0F,
        .width = 100.0F,
        .height = 20.0F,
    };
    const UI::UISliderPaint paint{};

    EXPECT_FLOAT_EQ(*UI::Detail::resolveSliderValueFromPointer(
                        rect, paint, 4.0F, 0.0F, 10.0F, 2.0F),
                    0.0F);
    EXPECT_FLOAT_EQ(*UI::Detail::resolveSliderValueFromPointer(
                        rect, paint, 50.0F, 0.0F, 10.0F, 2.0F),
                    6.0F);
    EXPECT_FLOAT_EQ(*UI::Detail::resolveSliderValueFromPointer(
                        rect, paint, 96.0F, 0.0F, 10.0F, 2.0F),
                    10.0F);
    EXPECT_FALSE(UI::Detail::resolveSliderValueFromPointer(
        {.x = 0.0F, .y = 0.0F, .width = 8.0F, .height = 20.0F},
        paint, 4.0F, 0.0F, 10.0F, 2.0F));
}

TEST(UIControlValuePrimitivesTests, ScrollAxisAccessorsUseIndependentAxes)
{
    UI::UIScrollOffset offset{.x = 4.0F, .y = 7.0F};
    const UI::UIScrollViewMetrics metrics{
        .viewportSize = {.width = 100.0F, .height = 80.0F},
        .contentSize = {.width = 250.0F, .height = 200.0F},
    };

    EXPECT_FLOAT_EQ(
        UI::Detail::scrollAxisOffset(offset, UI::UIScrollAxes::Horizontal),
        4.0F);
    EXPECT_FLOAT_EQ(
        UI::Detail::scrollAxisOffset(offset, UI::UIScrollAxes::Vertical), 7.0F);
    EXPECT_FLOAT_EQ(UI::Detail::scrollAxisMaxOffset(
                        metrics, UI::UIScrollAxes::Horizontal),
                    150.0F);
    EXPECT_FLOAT_EQ(UI::Detail::scrollAxisMaxOffset(
                        metrics, UI::UIScrollAxes::Vertical),
                    120.0F);

    UI::Detail::setScrollAxisOffset(offset, UI::UIScrollAxes::Horizontal, 9.0F);
    UI::Detail::setScrollAxisOffset(offset, UI::UIScrollAxes::Vertical, 11.0F);
    EXPECT_EQ(offset, (UI::UIScrollOffset{.x = 9.0F, .y = 11.0F}));
}

TEST(UIControlValuePrimitivesTests, WheelOffsetsRespectAxisFallbackBoundsAndPositiveZero)
{
    const UI::UIScrollViewMetrics metrics{
        .viewportSize = {.width = 100.0F, .height = 80.0F},
        .contentSize = {.width = 250.0F, .height = 200.0F},
    };

    EXPECT_FLOAT_EQ(UI::Detail::resolveVirtualScrollWheelOffset(
                        50.0F, 100.0F, 10.0F,
                        {.x = 2.0F, .y = 0.0F}),
                    30.0F);
    EXPECT_FLOAT_EQ(UI::Detail::resolveVirtualScrollWheelOffset(
                        50.0F, 100.0F, 10.0F,
                        {.x = 0.0F, .y = -20.0F}),
                    100.0F);
    const float clampedZero = UI::Detail::resolveVirtualScrollWheelOffset(
        0.0F, 100.0F, 10.0F, {.x = 0.0F, .y = 2.0F});
    EXPECT_FLOAT_EQ(clampedZero, 0.0F);
    EXPECT_FALSE(std::signbit(clampedZero));

    const auto horizontal = UI::Detail::resolveScrollWheelOffset(
        {.x = 50.0F, .y = 70.0F},
        {.axes = UI::UIScrollAxes::Horizontal, .wheelStep = 10.0F},
        metrics, {.x = 0.0F, .y = 2.0F});
    EXPECT_EQ(horizontal, (UI::UIScrollOffset{.x = 30.0F, .y = 0.0F}));

    const auto vertical = UI::Detail::resolveScrollWheelOffset(
        {.x = 50.0F, .y = 70.0F},
        {.axes = UI::UIScrollAxes::Vertical, .wheelStep = 10.0F},
        metrics, {.x = 2.0F, .y = 0.0F});
    EXPECT_EQ(vertical, (UI::UIScrollOffset{.x = 0.0F, .y = 50.0F}));

    const auto both = UI::Detail::resolveScrollWheelOffset(
        {.x = 50.0F, .y = 70.0F},
        {.axes = UI::UIScrollAxes::Both, .wheelStep = 10.0F},
        metrics, {.x = 2.0F, .y = 3.0F});
    EXPECT_EQ(both, (UI::UIScrollOffset{.x = 30.0F, .y = 40.0F}));

    const auto none = UI::Detail::resolveScrollWheelOffset(
        {.x = 50.0F, .y = 70.0F},
        {.axes = UI::UIScrollAxes::None, .wheelStep = 10.0F},
        metrics, {.x = 2.0F, .y = 3.0F});
    EXPECT_EQ(none, (UI::UIScrollOffset{}));
}

TEST(UIControlValuePrimitivesTests, ThumbAndTrackPageOffsetsUseAxisGeometry)
{
    const UI::Detail::ScrollBarGeometry geometry{
        .track = {.x = 10.0F, .y = 10.0F, .width = 100.0F, .height = 100.0F},
        .thumb = {.x = 30.0F, .y = 40.0F, .width = 20.0F, .height = 20.0F},
        .visible = true,
    };

    const auto verticalThumb = UI::Detail::resolveScrollThumbOffset(
        geometry, UI::UIScrollAxes::Vertical,
        {.x = 0.0F, .y = 60.0F}, 5.0F, 160.0F);
    ASSERT_TRUE(verticalThumb.has_value());
    EXPECT_FLOAT_EQ(*verticalThumb, 90.0F);
    const auto horizontalThumb = UI::Detail::resolveScrollThumbOffset(
        geometry, UI::UIScrollAxes::Horizontal,
        {.x = 75.0F, .y = 0.0F}, 5.0F, 160.0F);
    ASSERT_TRUE(horizontalThumb.has_value());
    EXPECT_FLOAT_EQ(*horizontalThumb, 120.0F);

    EXPECT_FALSE(UI::Detail::resolveScrollThumbOffset(
        geometry, UI::UIScrollAxes::Vertical,
        {.x = 0.0F, .y = 60.0F}, 5.0F, 0.0F));
    auto hidden = geometry;
    hidden.visible = false;
    EXPECT_FALSE(UI::Detail::resolveScrollThumbOffset(
        hidden, UI::UIScrollAxes::Vertical,
        {.x = 0.0F, .y = 60.0F}, 5.0F, 160.0F));

    const auto pageBefore = UI::Detail::resolveScrollTrackPageOffset(
        geometry, UI::UIScrollAxes::Vertical,
        {.x = 0.0F, .y = 20.0F}, 60.0F, 30.0F);
    ASSERT_TRUE(pageBefore.has_value());
    EXPECT_FLOAT_EQ(*pageBefore, 30.0F);
    const auto pageAtThumb = UI::Detail::resolveScrollTrackPageOffset(
        geometry, UI::UIScrollAxes::Vertical,
        {.x = 0.0F, .y = 40.0F}, 60.0F, 30.0F);
    ASSERT_TRUE(pageAtThumb.has_value());
    EXPECT_FLOAT_EQ(*pageAtThumb, 90.0F);
    EXPECT_FALSE(UI::Detail::resolveScrollTrackPageOffset(
        hidden, UI::UIScrollAxes::Vertical,
        {.x = 0.0F, .y = 20.0F}, 60.0F, 30.0F));
}

TEST(UIControlValuePrimitivesTests, VirtualRowsResolveAllAlignmentsAndRejectUnrepresentableContent)
{
    using UI::Detail::VirtualRowScrollAlignment;

    EXPECT_EQ(UI::Detail::toVirtualRowScrollAlignment(
                  UI::UIListViewScrollAlignment::Center),
              VirtualRowScrollAlignment::Center);
    EXPECT_EQ(UI::Detail::toVirtualRowScrollAlignment(
                  UI::UITreeViewScrollAlignment::End),
              VirtualRowScrollAlignment::End);

    EXPECT_FLOAT_EQ(*UI::Detail::resolveVirtualRowScrollOffset(
                        5, 10, 10.0F, 30.0F, 0.0F,
                        VirtualRowScrollAlignment::Start),
                    50.0F);
    EXPECT_FLOAT_EQ(*UI::Detail::resolveVirtualRowScrollOffset(
                        5, 10, 10.0F, 30.0F, 0.0F,
                        VirtualRowScrollAlignment::Center),
                    40.0F);
    EXPECT_FLOAT_EQ(*UI::Detail::resolveVirtualRowScrollOffset(
                        5, 10, 10.0F, 30.0F, 0.0F,
                        VirtualRowScrollAlignment::End),
                    30.0F);
    EXPECT_FLOAT_EQ(*UI::Detail::resolveVirtualRowScrollOffset(
                        5, 10, 10.0F, 30.0F, 45.0F,
                        VirtualRowScrollAlignment::Nearest),
                    45.0F);
    EXPECT_FLOAT_EQ(*UI::Detail::resolveVirtualRowScrollOffset(
                        5, 10, 10.0F, 30.0F, 0.0F,
                        VirtualRowScrollAlignment::Nearest),
                    30.0F);
    EXPECT_FLOAT_EQ(*UI::Detail::resolveVirtualRowScrollOffset(
                        5, 10, 10.0F, 30.0F, 60.0F,
                        VirtualRowScrollAlignment::Nearest),
                    50.0F);
    EXPECT_FLOAT_EQ(*UI::Detail::resolveVirtualRowScrollOffset(
                        9, 10, 10.0F, 30.0F, 0.0F,
                        VirtualRowScrollAlignment::Start),
                    70.0F);

    EXPECT_FALSE(UI::Detail::resolveVirtualRowScrollOffset(
        0, (std::numeric_limits<u64>::max)(),
        (std::numeric_limits<float>::max)(), 30.0F, 0.0F,
        VirtualRowScrollAlignment::Start));
    EXPECT_FALSE(UI::Detail::resolveVirtualRowScrollOffset(
        0, 1, (std::numeric_limits<float>::quiet_NaN)(),
        30.0F, 0.0F, VirtualRowScrollAlignment::Start));
}

} // namespace
} // namespace Tina::Tests
