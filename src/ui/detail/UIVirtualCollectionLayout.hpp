#pragma once

#include <tina/ui/UIScrollView.hpp>

#include "UILayoutPrimitives.hpp"

#include <algorithm>
#include <cmath>
#include <expected>
#include <limits>

namespace Tina::UI::Detail {

struct VirtualCollectionLayoutInput final {
    u64 logicalItemCount = 0;
    u32 materializedItemCapacity = 0;
    float rowHeight = 0.0F;
    u32 overscanRows = 0;
    UIScrollBarVisibility scrollBarVisibility = UIScrollBarVisibility::Auto;
    float scrollBarThickness = 0.0F;
    float requestedScrollOffset = 0.0F;
    UILogicalRect availableRect{};
};

struct VirtualCollectionLayoutPlan final {
    UILogicalRect viewportRect{};
    UILogicalSize contentSize{};
    u64 firstVisibleIndex = 0;
    u64 visibleItemCount = 0;
    u64 firstMaterializedIndex = 0;
    u64 materializedItemCount = 0;
    float scrollOffset = 0.0F;
    float maximumScrollOffset = 0.0F;
    bool verticalScrollBarVisible = false;
};

enum class VirtualCollectionLayoutError : u8 {
    ContentHeightNotRepresentable,
    MaterializedRangeExceedsCapacity,
};

using VirtualCollectionLayoutResult =
    std::expected<VirtualCollectionLayoutPlan, VirtualCollectionLayoutError>;

[[nodiscard]] inline VirtualCollectionLayoutResult
resolveVirtualCollectionLayout(const VirtualCollectionLayoutInput& input) noexcept
{
    const double contentHeight64 =
        static_cast<double>(input.logicalItemCount) *
        static_cast<double>(input.rowHeight);
    if (!std::isfinite(contentHeight64) ||
        contentHeight64 > (std::numeric_limits<float>::max)())
    {
        return std::unexpected(
            VirtualCollectionLayoutError::ContentHeightNotRepresentable);
    }

    const float logicalContentHeight =
        normalizeFloat(static_cast<float>(contentHeight64));
    const bool barsHidden =
        input.scrollBarVisibility == UIScrollBarVisibility::Hidden;
    const bool verticalScrollBar =
        !barsHidden &&
        (input.scrollBarVisibility == UIScrollBarVisibility::Always ||
         logicalContentHeight > input.availableRect.height);
    const UILogicalRect viewportRect{
        .x = input.availableRect.x,
        .y = input.availableRect.y,
        .width = normalizeFloat((std::max)(
            0.0F,
            input.availableRect.width -
                (verticalScrollBar ? input.scrollBarThickness : 0.0F))),
        .height = input.availableRect.height,
    };
    const float contentHeight = normalizeFloat(
        (std::max)(logicalContentHeight, viewportRect.height));
    const float maximumScrollOffset = normalizeFloat(
        (std::max)(0.0F, contentHeight - viewportRect.height));
    const float scrollOffset = normalizeFloat((std::clamp)(
        input.requestedScrollOffset, 0.0F, maximumScrollOffset));

    u64 firstVisibleIndex = 0;
    u64 visibleItemCount = 0;
    if (input.logicalItemCount != 0 && viewportRect.height > 0.0F)
    {
        const double firstVisible = std::floor(
            static_cast<double>(scrollOffset) / input.rowHeight);
        firstVisibleIndex =
            firstVisible >= static_cast<double>(input.logicalItemCount)
                ? input.logicalItemCount - 1
                : static_cast<u64>((std::max)(0.0, firstVisible));
        const double visibleEnd = std::ceil(
            (static_cast<double>(scrollOffset) + viewportRect.height) /
            input.rowHeight);
        const u64 endIndex =
            visibleEnd >= static_cast<double>(input.logicalItemCount)
                ? input.logicalItemCount
                : static_cast<u64>((std::max)(0.0, visibleEnd));
        visibleItemCount =
            endIndex > firstVisibleIndex ? endIndex - firstVisibleIndex : 0;
    }

    const u64 overscanRows = input.overscanRows;
    const u64 firstMaterializedIndex =
        firstVisibleIndex > overscanRows
            ? firstVisibleIndex - overscanRows
            : 0;
    const u64 visibleEndIndex = firstVisibleIndex + visibleItemCount;
    const u64 trailingOverscan = (std::min)(
        overscanRows, input.logicalItemCount - visibleEndIndex);
    const u64 materializedEndIndex = visibleEndIndex + trailingOverscan;
    const u64 materializedItemCount =
        materializedEndIndex > firstMaterializedIndex
            ? materializedEndIndex - firstMaterializedIndex
            : 0;
    if (materializedItemCount > input.materializedItemCapacity)
    {
        return std::unexpected(
            VirtualCollectionLayoutError::MaterializedRangeExceedsCapacity);
    }

    return VirtualCollectionLayoutPlan{
        .viewportRect = viewportRect,
        .contentSize = {
            .width = viewportRect.width,
            .height = contentHeight,
        },
        .firstVisibleIndex = firstVisibleIndex,
        .visibleItemCount = visibleItemCount,
        .firstMaterializedIndex = firstMaterializedIndex,
        .materializedItemCount = materializedItemCount,
        .scrollOffset = scrollOffset,
        .maximumScrollOffset = maximumScrollOffset,
        .verticalScrollBarVisible = verticalScrollBar,
    };
}

} // namespace Tina::UI::Detail
