#pragma once

#include <tina/ui/UIVirtualGridView.hpp>

#include "UILayoutPrimitives.hpp"

#include <algorithm>
#include <cmath>
#include <expected>
#include <limits>

namespace Tina::UI::Detail {

struct VirtualGridLayoutInput final {
    u64 logicalItemCount = 0;
    u32 materializedItemCapacity = 0;
    float minimumItemWidth = 0.0F;
    float itemHeight = 0.0F;
    float columnGap = 0.0F;
    float rowGap = 0.0F;
    u32 maximumColumnCount = 0;
    u32 overscanRows = 0;
    UIScrollBarVisibility scrollBarVisibility = UIScrollBarVisibility::Auto;
    float scrollBarThickness = 0.0F;
    float requestedScrollOffset = 0.0F;
    UILogicalRect availableRect{};
};

struct VirtualGridLayoutPlan final {
    UILogicalRect viewportRect{};
    UILogicalRect contentRect{};
    UILogicalSize contentSize{};
    u64 logicalItemCount = 0;
    u64 logicalRowCount = 0;
    u32 logicalColumnCount = 0;
    u64 firstVisibleRow = 0;
    u64 visibleRowCount = 0;
    u64 firstMaterializedRow = 0;
    u64 materializedRowCount = 0;
    u64 firstMaterializedIndex = 0;
    u64 materializedItemCount = 0;
    float itemWidth = 0.0F;
    float itemHeight = 0.0F;
    float columnGap = 0.0F;
    float rowGap = 0.0F;
    float scrollOffset = 0.0F;
    float maximumScrollOffset = 0.0F;
    bool verticalScrollBarVisible = false;
};

enum class VirtualGridLayoutError : u8 {
    InvalidGeometry,
    ContentHeightNotRepresentable,
    MaterializedRangeExceedsCapacity,
};

using VirtualGridLayoutResult =
    std::expected<VirtualGridLayoutPlan, VirtualGridLayoutError>;

namespace VirtualGridLayoutDetail {

[[nodiscard]] inline u32 resolveColumnCount(
    u64 logicalItemCount, float viewportWidth, float minimumItemWidth,
    float columnGap, u32 maximumColumnCount) noexcept
{
    if (logicalItemCount == 0)
    {
        return 0;
    }

    const double stride =
        static_cast<double>(minimumItemWidth) + static_cast<double>(columnGap);
    const double widthWithTrailingGap =
        static_cast<double>((std::max)(0.0F, viewportWidth)) +
        static_cast<double>(columnGap);
    const double fitted64 = std::floor(widthWithTrailingGap / stride);
    const double u32Maximum =
        static_cast<double>((std::numeric_limits<u32>::max)());
    u32 fitted = fitted64 >= u32Maximum
                     ? (std::numeric_limits<u32>::max)()
                     : static_cast<u32>((std::max)(1.0, fitted64));
    if (maximumColumnCount != 0)
    {
        fitted = (std::min)(fitted, maximumColumnCount);
    }
    if (logicalItemCount < fitted)
    {
        fitted = static_cast<u32>(logicalItemCount);
    }
    return fitted;
}

[[nodiscard]] constexpr u64 divideRoundUp(u64 value, u32 divisor) noexcept
{
    return value == 0 ? 0 : 1 + ((value - 1) / divisor);
}

[[nodiscard]] inline std::expected<float, VirtualGridLayoutError>
resolveLogicalContentHeight(u64 rowCount, float itemHeight, float rowGap) noexcept
{
    const double height64 =
        rowCount == 0
            ? 0.0
            : static_cast<double>(rowCount) * static_cast<double>(itemHeight) +
                  static_cast<double>(rowCount - 1) * static_cast<double>(rowGap);
    if (!std::isfinite(height64) ||
        height64 > static_cast<double>((std::numeric_limits<float>::max)()))
    {
        return std::unexpected(
            VirtualGridLayoutError::ContentHeightNotRepresentable);
    }
    return normalizeFloat(static_cast<float>(height64));
}

[[nodiscard]] constexpr u64 itemIndexForRow(
    u64 row, u64 rowCount, u32 columnCount, u64 itemCount) noexcept
{
    return row >= rowCount ? itemCount : row * columnCount;
}

} // namespace VirtualGridLayoutDetail

[[nodiscard]] inline VirtualGridLayoutResult
resolveVirtualGridLayout(const VirtualGridLayoutInput& input) noexcept
{
    if (!std::isfinite(input.minimumItemWidth) ||
        !std::isfinite(input.itemHeight) ||
        !std::isfinite(input.columnGap) || !std::isfinite(input.rowGap) ||
        !std::isfinite(input.scrollBarThickness) ||
        input.minimumItemWidth <= 0.0F || input.itemHeight <= 0.0F ||
        input.columnGap < 0.0F || input.rowGap < 0.0F ||
        input.scrollBarThickness < 0.0F)
    {
        return std::unexpected(VirtualGridLayoutError::InvalidGeometry);
    }

    const float availableWidth = (std::max)(0.0F, input.availableRect.width);
    const float availableHeight = (std::max)(0.0F, input.availableRect.height);
    const bool barsHidden =
        input.scrollBarVisibility == UIScrollBarVisibility::Hidden;
    bool verticalBar =
        !barsHidden &&
        input.scrollBarVisibility == UIScrollBarVisibility::Always;
    u32 columnCount = 0;
    u64 rowCount = 0;
    float logicalContentHeight = 0.0F;

    // Auto visibility is monotonic: adding the vertical bar can reduce the
    // fitted column count and therefore can only increase the row count.
    for (usize passIndex = 0; passIndex < 2; ++passIndex)
    {
        const float viewportWidth = (std::max)(
            0.0F,
            availableWidth - (verticalBar ? input.scrollBarThickness : 0.0F));
        columnCount = VirtualGridLayoutDetail::resolveColumnCount(
            input.logicalItemCount, viewportWidth, input.minimumItemWidth,
            input.columnGap, input.maximumColumnCount);
        rowCount = columnCount == 0
                       ? 0
                       : VirtualGridLayoutDetail::divideRoundUp(
                             input.logicalItemCount, columnCount);
        const auto resolvedHeight =
            VirtualGridLayoutDetail::resolveLogicalContentHeight(
                rowCount, input.itemHeight, input.rowGap);
        if (!resolvedHeight)
        {
            return std::unexpected(resolvedHeight.error());
        }
        logicalContentHeight = *resolvedHeight;
        if (!barsHidden &&
            input.scrollBarVisibility == UIScrollBarVisibility::Auto)
        {
            verticalBar = logicalContentHeight > availableHeight;
        }
    }

    const UILogicalRect viewportRect{
        .x = input.availableRect.x,
        .y = input.availableRect.y,
        .width = normalizeFloat((std::max)(
            0.0F,
            availableWidth - (verticalBar ? input.scrollBarThickness : 0.0F))),
        .height = normalizeFloat(availableHeight),
    };
    const float contentHeight = normalizeFloat(
        (std::max)(logicalContentHeight, viewportRect.height));
    const float maximumScrollOffset = normalizeFloat(
        (std::max)(0.0F, contentHeight - viewportRect.height));
    const float requestedScrollOffset =
        std::isfinite(input.requestedScrollOffset)
            ? input.requestedScrollOffset
            : 0.0F;
    const float scrollOffset = normalizeFloat((std::clamp)(
        requestedScrollOffset, 0.0F, maximumScrollOffset));

    float itemWidth = 0.0F;
    if (columnCount != 0)
    {
        const double totalGapWidth =
            static_cast<double>(columnCount - 1) *
            static_cast<double>(input.columnGap);
        itemWidth = normalizeFloat(static_cast<float>((std::max)(
            0.0,
            (static_cast<double>(viewportRect.width) - totalGapWidth) /
                static_cast<double>(columnCount))));
    }

    u64 firstVisibleRow = 0;
    u64 visibleRowCount = 0;
    if (rowCount != 0 && viewportRect.height > 0.0F)
    {
        const double rowStride =
            static_cast<double>(input.itemHeight) +
            static_cast<double>(input.rowGap);
        const double firstCandidate64 =
            std::floor(static_cast<double>(scrollOffset) / rowStride);
        firstVisibleRow =
            firstCandidate64 >= static_cast<double>(rowCount)
                ? rowCount
                : static_cast<u64>((std::max)(0.0, firstCandidate64));
        const double firstCandidateStart =
            static_cast<double>(firstVisibleRow) * rowStride;
        if (firstVisibleRow < rowCount &&
            static_cast<double>(scrollOffset) >=
                firstCandidateStart + static_cast<double>(input.itemHeight))
        {
            ++firstVisibleRow;
        }
        const double visibleEnd64 = std::ceil(
            (static_cast<double>(scrollOffset) + viewportRect.height) /
            rowStride);
        const u64 visibleEndRow =
            visibleEnd64 >= static_cast<double>(rowCount)
                ? rowCount
                : static_cast<u64>((std::max)(0.0, visibleEnd64));
        visibleRowCount = visibleEndRow > firstVisibleRow
                              ? visibleEndRow - firstVisibleRow
                              : 0;
    }

    const u64 overscanRows = input.overscanRows;
    const u64 firstMaterializedRow =
        firstVisibleRow > overscanRows ? firstVisibleRow - overscanRows : 0;
    const u64 visibleEndRow = firstVisibleRow + visibleRowCount;
    const u64 trailingOverscan =
        (std::min)(overscanRows, rowCount - visibleEndRow);
    const u64 materializedEndRow = visibleEndRow + trailingOverscan;
    const u64 materializedRowCount =
        materializedEndRow > firstMaterializedRow
            ? materializedEndRow - firstMaterializedRow
            : 0;
    const u64 firstMaterializedIndex =
        VirtualGridLayoutDetail::itemIndexForRow(
            firstMaterializedRow, rowCount, columnCount,
            input.logicalItemCount);
    const u64 materializedEndIndex =
        VirtualGridLayoutDetail::itemIndexForRow(
            materializedEndRow, rowCount, columnCount,
            input.logicalItemCount);
    const u64 materializedItemCount =
        materializedEndIndex - firstMaterializedIndex;
    if (materializedItemCount > input.materializedItemCapacity)
    {
        return std::unexpected(
            VirtualGridLayoutError::MaterializedRangeExceedsCapacity);
    }

    const UILogicalSize contentSize{
        .width = viewportRect.width,
        .height = contentHeight,
    };
    return VirtualGridLayoutPlan{
        .viewportRect = viewportRect,
        .contentRect =
            UILogicalRect{
                .x = viewportRect.x,
                .y = normalizeFloat(viewportRect.y - scrollOffset),
                .width = contentSize.width,
                .height = contentSize.height,
            },
        .contentSize = contentSize,
        .logicalItemCount = input.logicalItemCount,
        .logicalRowCount = rowCount,
        .logicalColumnCount = columnCount,
        .firstVisibleRow = firstVisibleRow,
        .visibleRowCount = visibleRowCount,
        .firstMaterializedRow = firstMaterializedRow,
        .materializedRowCount = materializedRowCount,
        .firstMaterializedIndex = firstMaterializedIndex,
        .materializedItemCount = materializedItemCount,
        .itemWidth = itemWidth,
        .itemHeight = input.itemHeight,
        .columnGap = input.columnGap,
        .rowGap = input.rowGap,
        .scrollOffset = scrollOffset,
        .maximumScrollOffset = maximumScrollOffset,
        .verticalScrollBarVisible = verticalBar,
    };
}

[[nodiscard]] inline UILogicalRect resolveVirtualGridItemRect(
    const VirtualGridLayoutPlan& plan, u64 logicalIndex) noexcept
{
    if (plan.logicalColumnCount == 0 ||
        logicalIndex >= plan.logicalItemCount)
    {
        return {};
    }

    const u64 row = logicalIndex / plan.logicalColumnCount;
    const u32 column = static_cast<u32>(
        logicalIndex % plan.logicalColumnCount);
    return UILogicalRect{
        .x = normalizeFloat(
            plan.contentRect.x +
            static_cast<float>(column) * (plan.itemWidth + plan.columnGap)),
        .y = normalizeFloat(
            plan.contentRect.y +
            static_cast<float>(row) * (plan.itemHeight + plan.rowGap)),
        .width = plan.itemWidth,
        .height = plan.itemHeight,
    };
}

} // namespace Tina::UI::Detail
