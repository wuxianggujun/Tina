#pragma once

#include <tina/ui/UIDataGrid.hpp>

#include "UILayoutPrimitives.hpp"

#include <algorithm>
#include <cmath>
#include <expected>
#include <limits>
#include <span>

namespace Tina::UI::Detail {

struct DataGridLayoutInput final {
    u64 logicalRowCount = 0;
    u32 columnCapacity = 0;
    u32 materializedRowCapacity = 0;
    std::span<const float> columnWidths{};
    float columnHeaderHeight = 0.0F;
    float rowHeight = 0.0F;
    u32 overscanRows = 0;
    UIScrollBarVisibility scrollBarVisibility = UIScrollBarVisibility::Auto;
    float scrollBarThickness = 0.0F;
    UIScrollOffset requestedOffset{};
    UILogicalRect availableRect{};
};

struct DataGridLayoutPlan final {
    UILogicalRect headerViewportRect{};
    UILogicalRect headerContentRect{};
    UILogicalRect bodyViewportRect{};
    UILogicalRect bodyContentRect{};
    UILogicalSize contentSize{};
    u64 logicalRowCount = 0;
    u32 logicalColumnCount = 0;
    u64 firstVisibleRow = 0;
    u64 visibleRowCount = 0;
    u64 firstMaterializedRow = 0;
    u64 materializedRowCount = 0;
    float rowHeight = 0.0F;
    UIScrollOffset scrollOffset{};
    UIScrollOffset maximumScrollOffset{};
    bool horizontalScrollBarVisible = false;
    bool verticalScrollBarVisible = false;
};

enum class DataGridLayoutError : u8 {
    InvalidGeometry,
    ColumnRangeExceedsCapacity,
    ContentWidthNotRepresentable,
    ContentHeightNotRepresentable,
    MaterializedRangeExceedsCapacity,
};

using DataGridLayoutResult =
    std::expected<DataGridLayoutPlan, DataGridLayoutError>;

[[nodiscard]] inline DataGridLayoutResult
resolveDataGridLayout(const DataGridLayoutInput& input) noexcept
{
    if (!std::isfinite(input.columnHeaderHeight) ||
        !std::isfinite(input.rowHeight) ||
        !std::isfinite(input.scrollBarThickness) ||
        input.columnHeaderHeight < 0.0F || input.rowHeight <= 0.0F ||
        input.scrollBarThickness < 0.0F)
    {
        return std::unexpected(DataGridLayoutError::InvalidGeometry);
    }
    if (input.columnWidths.size() > input.columnCapacity)
    {
        return std::unexpected(
            DataGridLayoutError::ColumnRangeExceedsCapacity);
    }

    double logicalContentWidth64 = 0.0;
    for (const float columnWidth : input.columnWidths)
    {
        if (!std::isfinite(columnWidth) || columnWidth <= 0.0F)
        {
            return std::unexpected(DataGridLayoutError::InvalidGeometry);
        }
        logicalContentWidth64 += static_cast<double>(columnWidth);
    }
    if (!std::isfinite(logicalContentWidth64) ||
        logicalContentWidth64 >
            static_cast<double>((std::numeric_limits<float>::max)()))
    {
        return std::unexpected(
            DataGridLayoutError::ContentWidthNotRepresentable);
    }

    const double logicalContentHeight64 =
        static_cast<double>(input.logicalRowCount) *
        static_cast<double>(input.rowHeight);
    if (!std::isfinite(logicalContentHeight64) ||
        logicalContentHeight64 >
            static_cast<double>((std::numeric_limits<float>::max)()))
    {
        return std::unexpected(
            DataGridLayoutError::ContentHeightNotRepresentable);
    }

    const float logicalContentWidth =
        normalizeFloat(static_cast<float>(logicalContentWidth64));
    const float logicalContentHeight =
        normalizeFloat(static_cast<float>(logicalContentHeight64));
    const float availableWidth = (std::max)(0.0F, input.availableRect.width);
    const float availableHeight = (std::max)(0.0F, input.availableRect.height);
    const float headerHeight = normalizeFloat((std::min)(
        input.columnHeaderHeight, availableHeight));
    const float bodyAvailableHeight = normalizeFloat((std::max)(
        0.0F, availableHeight - headerHeight));
    const bool barsHidden =
        input.scrollBarVisibility == UIScrollBarVisibility::Hidden;
    bool horizontalBar =
        !barsHidden &&
        input.scrollBarVisibility == UIScrollBarVisibility::Always;
    bool verticalBar = horizontalBar;

    // Auto bars can cause one another by reducing the opposite axis.
    for (usize passIndex = 0; passIndex < 2; ++passIndex)
    {
        const float viewportWidth = (std::max)(
            0.0F,
            availableWidth - (verticalBar ? input.scrollBarThickness : 0.0F));
        const float viewportHeight = (std::max)(
            0.0F,
            bodyAvailableHeight -
                (horizontalBar ? input.scrollBarThickness : 0.0F));
        if (!barsHidden &&
            input.scrollBarVisibility == UIScrollBarVisibility::Auto)
        {
            horizontalBar = logicalContentWidth > viewportWidth;
            verticalBar = logicalContentHeight > viewportHeight;
        }
    }

    const float viewportWidth = normalizeFloat((std::max)(
        0.0F,
        availableWidth - (verticalBar ? input.scrollBarThickness : 0.0F)));
    const float bodyViewportHeight = normalizeFloat((std::max)(
        0.0F,
        bodyAvailableHeight -
            (horizontalBar ? input.scrollBarThickness : 0.0F)));
    const UILogicalRect headerViewportRect{
        .x = input.availableRect.x,
        .y = input.availableRect.y,
        .width = viewportWidth,
        .height = headerHeight,
    };
    const UILogicalRect bodyViewportRect{
        .x = input.availableRect.x,
        .y = normalizeFloat(input.availableRect.y + headerHeight),
        .width = viewportWidth,
        .height = bodyViewportHeight,
    };
    const UILogicalSize contentSize{
        .width = normalizeFloat((std::max)(
            logicalContentWidth, viewportWidth)),
        .height = normalizeFloat((std::max)(
            logicalContentHeight, bodyViewportHeight)),
    };
    const UIScrollOffset maximumScrollOffset{
        .x = normalizeFloat((std::max)(
            0.0F, contentSize.width - bodyViewportRect.width)),
        .y = normalizeFloat((std::max)(
            0.0F, contentSize.height - bodyViewportRect.height)),
    };
    const float requestedOffsetX = std::isfinite(input.requestedOffset.x)
                                       ? input.requestedOffset.x
                                       : 0.0F;
    const float requestedOffsetY = std::isfinite(input.requestedOffset.y)
                                       ? input.requestedOffset.y
                                       : 0.0F;
    const UIScrollOffset scrollOffset{
        .x = normalizeFloat((std::clamp)(
            requestedOffsetX, 0.0F, maximumScrollOffset.x)),
        .y = normalizeFloat((std::clamp)(
            requestedOffsetY, 0.0F, maximumScrollOffset.y)),
    };

    u64 firstVisibleRow = 0;
    u64 visibleRowCount = 0;
    if (input.logicalRowCount != 0 && bodyViewportRect.height > 0.0F)
    {
        const double firstVisible64 = std::floor(
            static_cast<double>(scrollOffset.y) / input.rowHeight);
        firstVisibleRow =
            firstVisible64 >= static_cast<double>(input.logicalRowCount)
                ? input.logicalRowCount - 1
                : static_cast<u64>((std::max)(0.0, firstVisible64));
        const double visibleEnd64 = std::ceil(
            (static_cast<double>(scrollOffset.y) + bodyViewportRect.height) /
            input.rowHeight);
        const u64 visibleEndRow =
            visibleEnd64 >= static_cast<double>(input.logicalRowCount)
                ? input.logicalRowCount
                : static_cast<u64>((std::max)(0.0, visibleEnd64));
        visibleRowCount = visibleEndRow > firstVisibleRow
                              ? visibleEndRow - firstVisibleRow
                              : 0;
    }

    const u64 overscanRows = input.overscanRows;
    const u64 firstMaterializedRow =
        firstVisibleRow > overscanRows ? firstVisibleRow - overscanRows : 0;
    const u64 visibleEndRow = firstVisibleRow + visibleRowCount;
    const u64 trailingOverscan = (std::min)(
        overscanRows, input.logicalRowCount - visibleEndRow);
    const u64 materializedEndRow = visibleEndRow + trailingOverscan;
    const u64 materializedRowCount =
        materializedEndRow > firstMaterializedRow
            ? materializedEndRow - firstMaterializedRow
            : 0;
    if (materializedRowCount > input.materializedRowCapacity)
    {
        return std::unexpected(
            DataGridLayoutError::MaterializedRangeExceedsCapacity);
    }

    return DataGridLayoutPlan{
        .headerViewportRect = headerViewportRect,
        .headerContentRect =
            UILogicalRect{
                .x = normalizeFloat(headerViewportRect.x - scrollOffset.x),
                .y = headerViewportRect.y,
                .width = contentSize.width,
                .height = headerViewportRect.height,
            },
        .bodyViewportRect = bodyViewportRect,
        .bodyContentRect =
            UILogicalRect{
                .x = normalizeFloat(bodyViewportRect.x - scrollOffset.x),
                .y = normalizeFloat(bodyViewportRect.y - scrollOffset.y),
                .width = contentSize.width,
                .height = contentSize.height,
            },
        .contentSize = contentSize,
        .logicalRowCount = input.logicalRowCount,
        .logicalColumnCount = static_cast<u32>(input.columnWidths.size()),
        .firstVisibleRow = firstVisibleRow,
        .visibleRowCount = visibleRowCount,
        .firstMaterializedRow = firstMaterializedRow,
        .materializedRowCount = materializedRowCount,
        .rowHeight = input.rowHeight,
        .scrollOffset = scrollOffset,
        .maximumScrollOffset = maximumScrollOffset,
        .horizontalScrollBarVisible = horizontalBar,
        .verticalScrollBarVisible = verticalBar,
    };
}

[[nodiscard]] inline float resolveDataGridColumnOffset(
    std::span<const float> columnWidths, u32 logicalColumn) noexcept
{
    double offset = 0.0;
    const usize endColumn = (std::min)(
        static_cast<usize>(logicalColumn), columnWidths.size());
    for (usize columnIndex = 0; columnIndex < endColumn; ++columnIndex)
    {
        offset += static_cast<double>(columnWidths[columnIndex]);
    }
    return normalizeFloat(static_cast<float>(offset));
}

[[nodiscard]] inline UILogicalRect resolveDataGridHeaderCellRect(
    const DataGridLayoutPlan& plan, std::span<const float> columnWidths,
    u32 logicalColumn) noexcept
{
    if (logicalColumn >= columnWidths.size() ||
        logicalColumn >= plan.logicalColumnCount)
    {
        return {};
    }
    return UILogicalRect{
        .x = normalizeFloat(
            plan.headerContentRect.x +
            resolveDataGridColumnOffset(columnWidths, logicalColumn)),
        .y = plan.headerContentRect.y,
        .width = columnWidths[logicalColumn],
        .height = plan.headerContentRect.height,
    };
}

[[nodiscard]] inline UILogicalRect resolveDataGridCellRect(
    const DataGridLayoutPlan& plan, std::span<const float> columnWidths,
    u64 logicalRow, u32 logicalColumn) noexcept
{
    if (logicalRow >= plan.logicalRowCount ||
        logicalColumn >= columnWidths.size() ||
        logicalColumn >= plan.logicalColumnCount)
    {
        return {};
    }
    return UILogicalRect{
        .x = normalizeFloat(
            plan.bodyContentRect.x +
            resolveDataGridColumnOffset(columnWidths, logicalColumn)),
        .y = normalizeFloat(
            plan.bodyContentRect.y +
            static_cast<float>(logicalRow) * plan.rowHeight),
        .width = columnWidths[logicalColumn],
        .height = plan.rowHeight,
    };
}

} // namespace Tina::UI::Detail
