#include <gtest/gtest.h>

#include "detail/UIGridLayout.hpp"

namespace Tina::Tests {
namespace {

TEST(UIGridLayoutTests, PlacesExplicitSpansAndRowMajorAutoItems)
{
    UI::UIGridContainerStyle grid{};
    grid.columns = UI::UIGridTrackList::Of({
        UI::UIGridTrack::Fr(), UI::UIGridTrack::Fr(),
        UI::UIGridTrack::Fr(),
    });
    auto placement = UI::Detail::beginGridPlacement(grid);

    UI::UIGridItemStyle spanning{};
    spanning.row = 0U;
    spanning.column = 0U;
    spanning.columnSpan = 2U;
    const auto first = UI::Detail::resolveGridArea(placement, spanning);
    ASSERT_TRUE(first.valid);
    EXPECT_EQ(first.row, 0U);
    EXPECT_EQ(first.column, 0U);

    const auto second = UI::Detail::resolveGridArea(placement, {});
    ASSERT_TRUE(second.valid);
    EXPECT_EQ(second.row, 0U);
    EXPECT_EQ(second.column, 2U);

    const auto third = UI::Detail::resolveGridArea(placement, {});
    ASSERT_TRUE(third.valid);
    EXPECT_EQ(third.row, 1U);
    EXPECT_EQ(third.column, 0U);
}

TEST(UIGridLayoutTests, ResolvesPixelAutoAndFractionTracksWithGap)
{
    UI::UIGridContainerStyle grid{};
    grid.columns = UI::UIGridTrackList::Of({
        UI::UIGridTrack::Px(20.0F), UI::UIGridTrack::Auto(),
        UI::UIGridTrack::Fr(2.0F),
    });
    grid.rows = UI::UIGridTrackList::Of({UI::UIGridTrack::Auto()});
    grid.gap.column = 5.0F;

    auto measurement = UI::Detail::beginGridMeasurement(grid);
    UI::UILayoutStyle first{};
    first.gridItem.column = 0U;
    first.gridItem.row = 0U;
    UI::UILayoutStyle second = first;
    second.gridItem.column = 1U;
    UI::UILayoutStyle third = first;
    third.gridItem.column = 2U;
    ASSERT_TRUE(UI::Detail::appendGridMeasuredChild(
        measurement, grid, first, {.width = 100.0F, .height = 10.0F}));
    ASSERT_TRUE(UI::Detail::appendGridMeasuredChild(
        measurement, grid, second, {.width = 30.0F, .height = 12.0F}));
    ASSERT_TRUE(UI::Detail::appendGridMeasuredChild(
        measurement, grid, third, {.width = 10.0F, .height = 8.0F}));
    EXPECT_EQ(UI::Detail::gridMeasuredContentSize(measurement, grid),
              (UI::UILogicalSize{.width = 70.0F, .height = 12.0F}));

    const auto plan = UI::Detail::resolveGridLayout(
        grid, {.x = 4.0F, .y = 6.0F, .width = 150.0F, .height = 20.0F},
        measurement);
    ASSERT_TRUE(plan.valid);
    EXPECT_FLOAT_EQ(plan.columns.offsets[0], 4.0F);
    EXPECT_FLOAT_EQ(plan.columns.sizes[0], 20.0F);
    EXPECT_FLOAT_EQ(plan.columns.offsets[1], 29.0F);
    EXPECT_FLOAT_EQ(plan.columns.sizes[1], 30.0F);
    EXPECT_FLOAT_EQ(plan.columns.offsets[2], 64.0F);
    EXPECT_FLOAT_EQ(plan.columns.sizes[2], 90.0F);
}

TEST(UIGridLayoutTests, SpanDemandExpandsOnlyNonPixelTracks)
{
    UI::UIGridContainerStyle grid{};
    grid.columns = UI::UIGridTrackList::Of({
        UI::UIGridTrack::Px(20.0F), UI::UIGridTrack::Auto(),
        UI::UIGridTrack::Fr(),
    });
    grid.gap.column = 5.0F;
    auto measurement = UI::Detail::beginGridMeasurement(grid);
    UI::UILayoutStyle child{};
    child.gridItem.row = 0U;
    child.gridItem.column = 0U;
    child.gridItem.columnSpan = 3U;

    ASSERT_TRUE(UI::Detail::appendGridMeasuredChild(
        measurement, grid, child, {.width = 100.0F, .height = 10.0F}));
    EXPECT_FLOAT_EQ(measurement.columnBases[0], 20.0F);
    EXPECT_FLOAT_EQ(measurement.columnBases[1], 35.0F);
    EXPECT_FLOAT_EQ(measurement.columnBases[2], 35.0F);
}

} // namespace
} // namespace Tina::Tests
