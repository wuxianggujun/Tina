#include <gtest/gtest.h>

#include "detail/UIControlValuePrimitives.hpp"

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

} // namespace
} // namespace Tina::Tests
