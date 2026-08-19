#include <gtest/gtest.h>

#include "detail/UILayoutMeasurement.hpp"

namespace Tina::Tests {
namespace {

TEST(UILayoutMeasurementTests, AggregatesRowChildrenWithMarginsAndColumnGap)
{
    UI::Detail::LayoutFlowMeasurement measurement{};
    UI::UILayoutStyle first{};
    first.margin = {.left = 2.0F, .top = 3.0F, .right = 5.0F, .bottom = 7.0F};
    UI::UILayoutStyle second{};
    second.margin = {.left = 11.0F, .top = 13.0F, .right = 17.0F, .bottom = 19.0F};

    UI::Detail::appendFlowMeasuredChild(
        measurement, UI::UIFlexDirection::Row, 23.0F, first,
        {.width = 29.0F, .height = 31.0F});
    UI::Detail::appendFlowMeasuredChild(
        measurement, UI::UIFlexDirection::Row, 23.0F, second,
        {.width = 37.0F, .height = 41.0F});

    EXPECT_EQ(measurement.childCount, 2U);
    EXPECT_EQ(measurement.contentSize,
              (UI::UILogicalSize{.width = 124.0F, .height = 73.0F}));
}

TEST(UILayoutMeasurementTests, AggregatesColumnChildrenWithMarginsAndRowGap)
{
    UI::Detail::LayoutFlowMeasurement measurement{};
    UI::UILayoutStyle first{};
    first.margin = {.left = 2.0F, .top = 3.0F, .right = 5.0F, .bottom = 7.0F};
    UI::UILayoutStyle second{};
    second.margin = {.left = 11.0F, .top = 13.0F, .right = 17.0F, .bottom = 19.0F};

    UI::Detail::appendFlowMeasuredChild(
        measurement, UI::UIFlexDirection::Column, 23.0F, first,
        {.width = 29.0F, .height = 31.0F});
    UI::Detail::appendFlowMeasuredChild(
        measurement, UI::UIFlexDirection::Column, 23.0F, second,
        {.width = 37.0F, .height = 41.0F});

    EXPECT_EQ(measurement.childCount, 2U);
    EXPECT_EQ(measurement.contentSize,
              (UI::UILogicalSize{.width = 65.0F, .height = 137.0F}));
}

TEST(UILayoutMeasurementTests, ResolvesAutoRootAgainstViewportAndAppliesPadding)
{
    UI::UILayoutStyle style{};
    style.padding = {.left = 2.0F, .top = 3.0F, .right = 5.0F, .bottom = 7.0F};
    UI::Detail::LayoutScratchState scratch{};
    UI::Detail::LayoutPassStatistics statistics{};

    const UI::UILogicalSize measured = UI::Detail::resolveMeasuredLayoutSize(
        style,
        scratch,
        {.width = 100.0F, .height = 50.0F},
        true,
        {.size = {.width = 120.0F, .height = 20.0F}},
        statistics);

    EXPECT_EQ(measured, (UI::UILogicalSize{.width = 127.0F, .height = 50.0F}));
    EXPECT_EQ(statistics.percentMeasureFallbackCount, 0U);
}

TEST(UILayoutMeasurementTests, ResolvesSquareIndicatorAfterHeightConstraints)
{
    UI::UILayoutStyle style{};
    style.minMax.minHeight = UI::UILayoutLength::Px(24.0F);
    UI::Detail::LayoutScratchState scratch{};
    UI::Detail::LayoutPassStatistics statistics{};

    const UI::UILogicalSize measured = UI::Detail::resolveMeasuredLayoutSize(
        style,
        scratch,
        {},
        false,
        {
            .size = {.width = 30.0F, .height = 10.0F},
            .indicatorLabelWidth = 30.0F,
            .indicatorLabelGap = 4.0F,
            .leadingIndicatorExtent = 24.0F,
            .hasIndicatorLabel = true,
        },
        statistics);

    EXPECT_EQ(measured, (UI::UILogicalSize{.width = 58.0F, .height = 24.0F}));
}

} // namespace
} // namespace Tina::Tests
