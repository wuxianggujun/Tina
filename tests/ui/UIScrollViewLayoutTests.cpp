#include <gtest/gtest.h>

#include "detail/UIScrollViewLayout.hpp"

namespace Tina::Tests {
namespace {

TEST(UIScrollViewLayoutTests, AutoScrollbarsReachOppositeAxisFixedPoint)
{
    const auto plan = UI::Detail::resolveScrollViewLayout({
        .availableRect = {.x = 5.0F, .y = 7.0F, .width = 100.0F, .height = 100.0F},
        .rawContentSize = {.width = 95.0F, .height = 105.0F},
        .style = {.axes = UI::UIScrollAxes::Both},
        .scrollBarThickness = 10.0F,
    });

    EXPECT_EQ(plan.viewportRect,
              (UI::UILogicalRect{.x = 5.0F, .y = 7.0F, .width = 90.0F, .height = 90.0F}));
    EXPECT_EQ(plan.metrics.contentSize,
              (UI::UILogicalSize{.width = 95.0F, .height = 105.0F}));
    EXPECT_TRUE(plan.metrics.horizontalScrollBarVisible);
    EXPECT_TRUE(plan.metrics.verticalScrollBarVisible);
}

TEST(UIScrollViewLayoutTests, DisabledAxisUsesViewportAndClearsOffset)
{
    const auto plan = UI::Detail::resolveScrollViewLayout({
        .availableRect = {.width = 100.0F, .height = 100.0F},
        .rawContentSize = {.width = 200.0F, .height = 200.0F},
        .style = {.axes = UI::UIScrollAxes::Vertical},
        .scrollBarThickness = 10.0F,
        .requestedOffset = {.x = 50.0F, .y = 150.0F},
    });

    EXPECT_EQ(plan.metrics.viewportSize,
              (UI::UILogicalSize{.width = 90.0F, .height = 100.0F}));
    EXPECT_EQ(plan.metrics.contentSize,
              (UI::UILogicalSize{.width = 90.0F, .height = 200.0F}));
    EXPECT_EQ(plan.metrics.offset, (UI::UIScrollOffset{.x = 0.0F, .y = 100.0F}));
    EXPECT_EQ(plan.contentRect,
              (UI::UILogicalRect{.x = 0.0F, .y = -100.0F, .width = 90.0F, .height = 200.0F}));
    EXPECT_FALSE(plan.metrics.horizontalScrollBarVisible);
    EXPECT_TRUE(plan.metrics.verticalScrollBarVisible);
}

TEST(UIScrollViewLayoutTests, HiddenScrollbarsPreserveFullViewport)
{
    const auto plan = UI::Detail::resolveScrollViewLayout({
        .availableRect = {.width = 100.0F, .height = 100.0F},
        .rawContentSize = {.width = 200.0F, .height = 200.0F},
        .style = {
            .axes = UI::UIScrollAxes::Both,
            .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
        },
        .scrollBarThickness = 10.0F,
    });

    EXPECT_EQ(plan.metrics.viewportSize,
              (UI::UILogicalSize{.width = 100.0F, .height = 100.0F}));
    EXPECT_FALSE(plan.metrics.horizontalScrollBarVisible);
    EXPECT_FALSE(plan.metrics.verticalScrollBarVisible);
}

TEST(UIScrollViewLayoutTests, AlwaysShowsOnlyEnabledAxisScrollbars)
{
    const auto plan = UI::Detail::resolveScrollViewLayout({
        .availableRect = {.width = 100.0F, .height = 100.0F},
        .style = {
            .axes = UI::UIScrollAxes::Horizontal,
            .scrollBarVisibility = UI::UIScrollBarVisibility::Always,
        },
        .scrollBarThickness = 10.0F,
    });

    EXPECT_EQ(plan.metrics.viewportSize,
              (UI::UILogicalSize{.width = 100.0F, .height = 90.0F}));
    EXPECT_TRUE(plan.metrics.horizontalScrollBarVisible);
    EXPECT_FALSE(plan.metrics.verticalScrollBarVisible);
}

} // namespace
} // namespace Tina::Tests
