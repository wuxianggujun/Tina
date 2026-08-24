#include <gtest/gtest.h>

#include "detail/UIFlexLayout.hpp"

namespace Tina::Tests {
namespace {

UI::Detail::LayoutScratchState measured(float width, float height)
{
    UI::Detail::LayoutScratchState scratch{};
    scratch.measuredSize = {.width = width, .height = height};
    return scratch;
}

TEST(UIFlexLayoutTests, SummarizesMainCrossGrowAndShrinkWeights)
{
    UI::Detail::FlexLineSummary summary{};
    UI::Detail::LayoutPassStatistics statistics{};
    UI::UILayoutStyle first{};
    first.margin = {.left = 1.0F, .top = 3.0F, .right = 2.0F, .bottom = 4.0F};
    first.flexItem.grow = 1.0F;
    first.flexItem.shrink = 1.0F;
    UI::UILayoutStyle second{};
    second.flexItem.grow = 2.0F;
    second.flexItem.shrink = 0.5F;

    UI::Detail::appendFlexLineItem(
        summary, UI::UIFlexDirection::Row, 5.0F, 100.0F,
        first, measured(20.0F, 10.0F), statistics);
    UI::Detail::appendFlexLineItem(
        summary, UI::UIFlexDirection::Row, 5.0F, 100.0F,
        second, measured(30.0F, 8.0F), statistics);

    EXPECT_EQ(summary.itemCount, 2U);
    EXPECT_FLOAT_EQ(summary.totalMain, 58.0F);
    EXPECT_FLOAT_EQ(summary.totalCross, 17.0F);
    EXPECT_DOUBLE_EQ(summary.totalGrow, 3.0);
    EXPECT_DOUBLE_EQ(summary.totalShrinkWeight, 35.0);
    EXPECT_EQ(summary.contentSize(UI::UIFlexDirection::Row),
              (UI::UILogicalSize{.width = 58.0F, .height = 17.0F}));
    EXPECT_TRUE(UI::Detail::isValidFlexLineSummary(summary));
}

TEST(UIFlexLayoutTests, SpaceBetweenConsumesRemainingMainAxisSpace)
{
    UI::UILayoutStyle parent{};
    parent.flexContainer.direction = UI::UIFlexDirection::Row;
    parent.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
    parent.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    parent.flexContainer.gap.column = 5.0F;
    UI::UILayoutStyle child{};
    UI::Detail::LayoutPassStatistics statistics{};
    UI::Detail::FlexLineSummary summary{};
    const auto first = measured(20.0F, 10.0F);
    const auto other = measured(15.0F, 10.0F);
    UI::Detail::appendFlexLineItem(summary, parent.flexContainer.direction,
                                   5.0F, 100.0F, child, first, statistics);
    UI::Detail::appendFlexLineItem(summary, parent.flexContainer.direction,
                                   5.0F, 100.0F, child, other, statistics);
    UI::Detail::appendFlexLineItem(summary, parent.flexContainer.direction,
                                   5.0F, 100.0F, child, other, statistics);
    auto plan = UI::Detail::resolveFlexLinePlan(
        parent, {.width = 100.0F, .height = 20.0F}, summary);

    const auto firstRect = UI::Detail::resolveFlexItemRect(
        plan, child, first, statistics);
    const auto secondRect = UI::Detail::resolveFlexItemRect(
        plan, child, other, statistics);
    const auto thirdRect = UI::Detail::resolveFlexItemRect(
        plan, child, other, statistics);

    EXPECT_EQ(firstRect, (UI::UILogicalRect{.x = 0.0F, .y = 0.0F, .width = 20.0F, .height = 10.0F}));
    EXPECT_EQ(secondRect, (UI::UILogicalRect{.x = 45.0F, .y = 0.0F, .width = 15.0F, .height = 10.0F}));
    EXPECT_EQ(thirdRect, (UI::UILogicalRect{.x = 85.0F, .y = 0.0F, .width = 15.0F, .height = 10.0F}));
}

TEST(UIFlexLayoutTests, GrowAndShrinkDistributeByConfiguredWeights)
{
    UI::UILayoutStyle parent{};
    parent.flexContainer.direction = UI::UIFlexDirection::Row;
    parent.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    UI::UILayoutStyle growChild{};
    growChild.flexItem.grow = 1.0F;
    growChild.flexItem.shrink = 0.0F;
    const auto scratch = measured(20.0F, 10.0F);
    UI::Detail::LayoutPassStatistics statistics{};
    UI::Detail::FlexLineSummary growSummary{};
    UI::Detail::appendFlexLineItem(growSummary, parent.flexContainer.direction,
                                   0.0F, 100.0F, growChild, scratch, statistics);
    UI::Detail::appendFlexLineItem(growSummary, parent.flexContainer.direction,
                                   0.0F, 100.0F, growChild, scratch, statistics);
    auto growPlan = UI::Detail::resolveFlexLinePlan(
        parent, {.width = 100.0F, .height = 20.0F}, growSummary);

    EXPECT_EQ(UI::Detail::resolveFlexItemRect(growPlan, growChild, scratch, statistics),
              (UI::UILogicalRect{.x = 0.0F, .y = 0.0F, .width = 50.0F, .height = 10.0F}));
    EXPECT_EQ(UI::Detail::resolveFlexItemRect(growPlan, growChild, scratch, statistics),
              (UI::UILogicalRect{.x = 50.0F, .y = 0.0F, .width = 50.0F, .height = 10.0F}));

    UI::UILayoutStyle shrinkChild{};
    shrinkChild.flexItem.shrink = 1.0F;
    UI::Detail::FlexLineSummary shrinkSummary{};
    UI::Detail::appendFlexLineItem(shrinkSummary, parent.flexContainer.direction,
                                   0.0F, 30.0F, shrinkChild, scratch, statistics);
    UI::Detail::appendFlexLineItem(shrinkSummary, parent.flexContainer.direction,
                                   0.0F, 30.0F, shrinkChild, scratch, statistics);
    auto shrinkPlan = UI::Detail::resolveFlexLinePlan(
        parent, {.width = 30.0F, .height = 20.0F}, shrinkSummary);

    EXPECT_EQ(UI::Detail::resolveFlexItemRect(shrinkPlan, shrinkChild, scratch, statistics),
              (UI::UILogicalRect{.x = 0.0F, .y = 0.0F, .width = 15.0F, .height = 10.0F}));
    EXPECT_EQ(UI::Detail::resolveFlexItemRect(shrinkPlan, shrinkChild, scratch, statistics),
              (UI::UILogicalRect{.x = 15.0F, .y = 0.0F, .width = 15.0F, .height = 10.0F}));
}

TEST(UIFlexLayoutTests, AlignSelfOverridesContainerAndStretchUsesAvailableCrossAxis)
{
    UI::UILayoutStyle parent{};
    parent.flexContainer.direction = UI::UIFlexDirection::Row;
    parent.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    UI::UILayoutStyle endChild{};
    endChild.flexItem.alignSelf = UI::UIAlignSelf::End;
    endChild.margin.top = 2.0F;
    endChild.margin.bottom = 3.0F;
    const auto scratch = measured(20.0F, 10.0F);
    UI::Detail::LayoutPassStatistics statistics{};
    UI::Detail::FlexLineSummary endSummary{};
    UI::Detail::appendFlexLineItem(endSummary, parent.flexContainer.direction,
                                   0.0F, 20.0F, endChild, scratch, statistics);
    auto endPlan = UI::Detail::resolveFlexLinePlan(
        parent, {.width = 20.0F, .height = 40.0F}, endSummary);

    EXPECT_EQ(UI::Detail::resolveFlexItemRect(endPlan, endChild, scratch, statistics),
              (UI::UILogicalRect{.x = 0.0F, .y = 27.0F, .width = 20.0F, .height = 10.0F}));

    parent.flexContainer.alignItems = UI::UIAxisAlignment::Stretch;
    UI::UILayoutStyle stretchChild{};
    stretchChild.margin.top = 2.0F;
    stretchChild.margin.bottom = 3.0F;
    UI::Detail::FlexLineSummary stretchSummary{};
    UI::Detail::appendFlexLineItem(stretchSummary, parent.flexContainer.direction,
                                   0.0F, 20.0F, stretchChild, scratch, statistics);
    auto stretchPlan = UI::Detail::resolveFlexLinePlan(
        parent, {.width = 20.0F, .height = 40.0F}, stretchSummary);

    EXPECT_EQ(UI::Detail::resolveFlexItemRect(stretchPlan, stretchChild, scratch, statistics),
              (UI::UILogicalRect{.x = 0.0F, .y = 2.0F, .width = 20.0F, .height = 35.0F}));
}

TEST(UIFlexLayoutTests, WrapBuildsBoundedLinesWithIndependentCrossExtentAndGap)
{
    UI::UILayoutStyle child{};
    UI::Detail::LayoutPassStatistics statistics{};
    UI::Detail::FlexWrapMeasurement measurement{};

    UI::Detail::appendFlexMeasuredItem(
        measurement, UI::UIFlexDirection::Row, UI::UIFlexWrap::Wrap,
        90.0F, 10.0F, 5.0F, child, measured(40.0F, 10.0F), statistics);
    UI::Detail::appendFlexMeasuredItem(
        measurement, UI::UIFlexDirection::Row, UI::UIFlexWrap::Wrap,
        90.0F, 10.0F, 5.0F, child, measured(40.0F, 20.0F), statistics);
    UI::Detail::appendFlexMeasuredItem(
        measurement, UI::UIFlexDirection::Row, UI::UIFlexWrap::Wrap,
        90.0F, 10.0F, 5.0F, child, measured(40.0F, 30.0F), statistics);
    UI::Detail::finishFlexMeasurement(measurement, 5.0F);

    EXPECT_EQ(measurement.itemCount, 3U);
    EXPECT_EQ(measurement.lineCount, 2U);
    EXPECT_FLOAT_EQ(measurement.maximumMain, 90.0F);
    EXPECT_FLOAT_EQ(measurement.totalCross, 55.0F);
    EXPECT_EQ(measurement.contentSize(UI::UIFlexDirection::Row),
              (UI::UILogicalSize{.width = 90.0F, .height = 55.0F}));
    EXPECT_TRUE(UI::Detail::isValidFlexWrapMeasurement(measurement));
}

} // namespace
} // namespace Tina::Tests
