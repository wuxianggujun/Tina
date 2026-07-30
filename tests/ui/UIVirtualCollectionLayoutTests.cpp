#include <gtest/gtest.h>

#include "detail/UIVirtualCollectionLayout.hpp"

#include <limits>

namespace Tina::Tests {
namespace {

TEST(UIVirtualCollectionLayoutTests, ResolvesVisibleAndOverscanWindow)
{
    const auto result = UI::Detail::resolveVirtualCollectionLayout({
        .logicalItemCount = 100'000,
        .materializedItemCapacity = 16,
        .rowHeight = 20.0F,
        .overscanRows = 2,
        .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
        .scrollBarThickness = 10.0F,
        .requestedScrollOffset = 1'000'000.0F,
        .availableRect = {.x = 5.0F, .y = 7.0F, .width = 100.0F, .height = 100.0F},
    });

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->viewportRect,
              (UI::UILogicalRect{.x = 5.0F, .y = 7.0F, .width = 90.0F, .height = 100.0F}));
    EXPECT_EQ(result->contentSize,
              (UI::UILogicalSize{.width = 90.0F, .height = 2'000'000.0F}));
    EXPECT_EQ(result->firstVisibleIndex, 50'000U);
    EXPECT_EQ(result->visibleItemCount, 5U);
    EXPECT_EQ(result->firstMaterializedIndex, 49'998U);
    EXPECT_EQ(result->materializedItemCount, 9U);
    EXPECT_FLOAT_EQ(result->scrollOffset, 1'000'000.0F);
    EXPECT_FLOAT_EQ(result->maximumScrollOffset, 1'999'900.0F);
    EXPECT_TRUE(result->verticalScrollBarVisible);
}

TEST(UIVirtualCollectionLayoutTests, HiddenScrollbarPreservesAvailableWidth)
{
    const auto result = UI::Detail::resolveVirtualCollectionLayout({
        .logicalItemCount = 100,
        .materializedItemCapacity = 8,
        .rowHeight = 20.0F,
        .overscanRows = 1,
        .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
        .scrollBarThickness = 10.0F,
        .availableRect = {.width = 100.0F, .height = 100.0F},
    });

    ASSERT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(result->viewportRect.width, 100.0F);
    EXPECT_FALSE(result->verticalScrollBarVisible);
}

TEST(UIVirtualCollectionLayoutTests, RejectsRowPoolThatCannotCoverWindow)
{
    const auto result = UI::Detail::resolveVirtualCollectionLayout({
        .logicalItemCount = 100,
        .materializedItemCapacity = 6,
        .rowHeight = 20.0F,
        .overscanRows = 2,
        .availableRect = {.width = 100.0F, .height = 100.0F},
    });

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(),
              UI::Detail::VirtualCollectionLayoutError::MaterializedRangeExceedsCapacity);
}

TEST(UIVirtualCollectionLayoutTests, RejectsUnrepresentableLogicalContentHeight)
{
    const auto result = UI::Detail::resolveVirtualCollectionLayout({
        .logicalItemCount = (std::numeric_limits<u64>::max)(),
        .materializedItemCapacity = 64,
        .rowHeight = (std::numeric_limits<float>::max)(),
        .availableRect = {.width = 100.0F, .height = 100.0F},
    });

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(),
              UI::Detail::VirtualCollectionLayoutError::ContentHeightNotRepresentable);
}

} // namespace
} // namespace Tina::Tests
