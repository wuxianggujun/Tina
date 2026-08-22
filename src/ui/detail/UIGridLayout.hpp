#pragma once

#include "UILayoutPrimitives.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace Tina::UI::Detail {

struct GridArea final {
    u8 row = 0U;
    u8 column = 0U;
    u8 rowSpan = 1U;
    u8 columnSpan = 1U;
    bool valid = false;
};

struct GridPlacementState final {
    u64 occupiedCells = 0U;
    u8 columnCount = 1U;
    u8 rowCount = 0U;
    bool valid = true;
};

[[nodiscard]] constexpr GridPlacementState beginGridPlacement(
    const UIGridContainerStyle& style) noexcept
{
    return GridPlacementState{
        .columnCount = static_cast<u8>((std::max)(usize{1U},
                                                  static_cast<usize>(style.columns.count))),
        .rowCount = style.rows.count,
    };
}

[[nodiscard]] constexpr bool gridAreaIsFree(
    const GridPlacementState& state, u8 row, u8 column,
    u8 rowSpan, u8 columnSpan) noexcept
{
    for (u8 rowOffset = 0U; rowOffset < rowSpan; ++rowOffset)
    {
        for (u8 columnOffset = 0U; columnOffset < columnSpan; ++columnOffset)
        {
            const usize bitIndex =
                static_cast<usize>(row + rowOffset) * UIGridTrackCapacity +
                static_cast<usize>(column + columnOffset);
            if ((state.occupiedCells & (u64{1U} << bitIndex)) != 0U)
            {
                return false;
            }
        }
    }
    return true;
}

constexpr void occupyGridArea(GridPlacementState& state,
                              const GridArea& area) noexcept
{
    for (u8 rowOffset = 0U; rowOffset < area.rowSpan; ++rowOffset)
    {
        for (u8 columnOffset = 0U; columnOffset < area.columnSpan;
             ++columnOffset)
        {
            const usize bitIndex =
                static_cast<usize>(area.row + rowOffset) * UIGridTrackCapacity +
                static_cast<usize>(area.column + columnOffset);
            state.occupiedCells |= u64{1U} << bitIndex;
        }
    }
    state.rowCount = static_cast<u8>((std::max)(
        static_cast<usize>(state.rowCount),
        static_cast<usize>(area.row) + area.rowSpan));
    state.columnCount = static_cast<u8>((std::max)(
        static_cast<usize>(state.columnCount),
        static_cast<usize>(area.column) + area.columnSpan));
}

[[nodiscard]] constexpr GridArea resolveGridArea(
    GridPlacementState& state, const UIGridItemStyle& item) noexcept
{
    if (!state.valid)
    {
        return {};
    }
    const bool rowAuto = item.row == UIGridAutoIndex;
    const bool columnAuto = item.column == UIGridAutoIndex;
    GridArea area{
        .row = rowAuto ? 0U : item.row,
        .column = columnAuto ? 0U : item.column,
        .rowSpan = item.rowSpan,
        .columnSpan = item.columnSpan,
    };

    if (!rowAuto && !columnAuto)
    {
        area.valid = true;
        occupyGridArea(state, area);
        return area;
    }

    if (!rowAuto)
    {
        for (usize column = 0U;
             column + item.columnSpan <= UIGridTrackCapacity; ++column)
        {
            if (gridAreaIsFree(state, item.row, static_cast<u8>(column),
                               item.rowSpan, item.columnSpan))
            {
                area.column = static_cast<u8>(column);
                area.valid = true;
                occupyGridArea(state, area);
                return area;
            }
        }
    }
    else if (!columnAuto)
    {
        for (usize row = 0U; row + item.rowSpan <= UIGridTrackCapacity; ++row)
        {
            if (gridAreaIsFree(state, static_cast<u8>(row), item.column,
                               item.rowSpan, item.columnSpan))
            {
                area.row = static_cast<u8>(row);
                area.valid = true;
                occupyGridArea(state, area);
                return area;
            }
        }
    }
    else
    {
        state.columnCount = static_cast<u8>((std::max)(
            static_cast<usize>(state.columnCount),
            static_cast<usize>(item.columnSpan)));
        const usize columnCount = state.columnCount;
        for (usize row = 0U; row + item.rowSpan <= UIGridTrackCapacity; ++row)
        {
            for (usize column = 0U;
                 column + item.columnSpan <= columnCount; ++column)
            {
                if (gridAreaIsFree(state, static_cast<u8>(row),
                                   static_cast<u8>(column), item.rowSpan,
                                   item.columnSpan))
                {
                    area.row = static_cast<u8>(row);
                    area.column = static_cast<u8>(column);
                    area.valid = true;
                    occupyGridArea(state, area);
                    return area;
                }
            }
        }
    }

    state.valid = false;
    return {};
}

[[nodiscard]] constexpr UIGridTrack gridTrackAt(
    const UIGridTrackList& tracks, usize index) noexcept
{
    return index < tracks.count ? tracks.tracks[index]
                                : UIGridTrack::Auto();
}

struct GridMeasurement final {
    GridPlacementState placement{};
    std::array<float, UIGridTrackCapacity> columnBases{};
    std::array<float, UIGridTrackCapacity> rowBases{};
    usize childCount = 0U;
};

[[nodiscard]] constexpr GridMeasurement beginGridMeasurement(
    const UIGridContainerStyle& style) noexcept
{
    GridMeasurement measurement{
        .placement = beginGridPlacement(style),
    };
    for (usize column = 0U; column < style.columns.count; ++column)
    {
        const UIGridTrack track = style.columns.tracks[column];
        if (track.unit == UIGridTrackUnit::Px)
        {
            measurement.columnBases[column] = track.value;
        }
    }
    for (usize row = 0U; row < style.rows.count; ++row)
    {
        const UIGridTrack track = style.rows.tracks[row];
        if (track.unit == UIGridTrackUnit::Px)
        {
            measurement.rowBases[row] = track.value;
        }
    }
    return measurement;
}

inline void accumulateGridTrackDemand(
    std::array<float, UIGridTrackCapacity>& bases,
    const UIGridTrackList& definitions, u8 start, u8 span,
    float gap, float demand) noexcept
{
    float current = gap * static_cast<float>(span - 1U);
    usize flexibleCount = 0U;
    for (usize offset = 0U; offset < span; ++offset)
    {
        const usize index = static_cast<usize>(start) + offset;
        current += bases[index];
        if (gridTrackAt(definitions, index).unit != UIGridTrackUnit::Px)
        {
            ++flexibleCount;
        }
    }
    if (demand <= current || flexibleCount == 0U)
    {
        return;
    }

    const float share = (demand - current) /
                        static_cast<float>(flexibleCount);
    for (usize offset = 0U; offset < span; ++offset)
    {
        const usize index = static_cast<usize>(start) + offset;
        if (gridTrackAt(definitions, index).unit != UIGridTrackUnit::Px)
        {
            bases[index] += share;
        }
    }
}

[[nodiscard]] inline bool appendGridMeasuredChild(
    GridMeasurement& measurement,
    const UIGridContainerStyle& parentStyle,
    const UILayoutStyle& childStyle,
    UILogicalSize childMeasuredSize) noexcept
{
    const GridArea area = resolveGridArea(
        measurement.placement, childStyle.gridItem);
    if (!area.valid)
    {
        return false;
    }
    const float outerWidth =
        childMeasuredSize.width + horizontalMargin(childStyle.margin);
    const float outerHeight =
        childMeasuredSize.height + verticalMargin(childStyle.margin);
    if (!isFiniteNonNegative(outerWidth) ||
        !isFiniteNonNegative(outerHeight))
    {
        measurement.placement.valid = false;
        return false;
    }
    accumulateGridTrackDemand(
        measurement.columnBases, parentStyle.columns,
        area.column, area.columnSpan, parentStyle.gap.column, outerWidth);
    accumulateGridTrackDemand(
        measurement.rowBases, parentStyle.rows,
        area.row, area.rowSpan, parentStyle.gap.row, outerHeight);
    ++measurement.childCount;
    return true;
}

[[nodiscard]] inline float gridAxisContentExtent(
    const std::array<float, UIGridTrackCapacity>& bases,
    usize count, float gap) noexcept
{
    float extent = count > 1U ? gap * static_cast<float>(count - 1U) : 0.0F;
    for (usize index = 0U; index < count; ++index)
    {
        extent += bases[index];
    }
    return extent;
}

[[nodiscard]] inline UILogicalSize gridMeasuredContentSize(
    const GridMeasurement& measurement,
    const UIGridContainerStyle& style) noexcept
{
    return UILogicalSize{
        .width = gridAxisContentExtent(
            measurement.columnBases, measurement.placement.columnCount,
            style.gap.column),
        .height = gridAxisContentExtent(
            measurement.rowBases, measurement.placement.rowCount,
            style.gap.row),
    };
}

[[nodiscard]] inline bool isValidGridMeasurement(
    const GridMeasurement& measurement,
    const UIGridContainerStyle& style) noexcept
{
    if (!measurement.placement.valid ||
        measurement.placement.columnCount > UIGridTrackCapacity ||
        measurement.placement.rowCount > UIGridTrackCapacity)
    {
        return false;
    }
    const UILogicalSize size = gridMeasuredContentSize(measurement, style);
    return isFiniteNonNegative(size.width) &&
           isFiniteNonNegative(size.height);
}

struct GridAxisLayout final {
    std::array<float, UIGridTrackCapacity> offsets{};
    std::array<float, UIGridTrackCapacity> sizes{};
    u8 count = 0U;
    bool valid = true;
};

[[nodiscard]] inline GridAxisLayout resolveGridAxisLayout(
    const UIGridTrackList& definitions,
    const std::array<float, UIGridTrackCapacity>& bases,
    u8 count, float origin, float availableExtent, float gap) noexcept
{
    GridAxisLayout layout{.count = count};
    float baseExtent = count > 1U
                           ? gap * static_cast<float>(count - 1U)
                           : 0.0F;
    double totalFraction = 0.0;
    for (usize index = 0U; index < count; ++index)
    {
        layout.sizes[index] = bases[index];
        baseExtent += bases[index];
        const UIGridTrack track = gridTrackAt(definitions, index);
        if (track.unit == UIGridTrackUnit::Fraction)
        {
            totalFraction += static_cast<double>(track.value);
        }
    }
    const float remaining = (std::max)(0.0F, availableExtent - baseExtent);
    if (remaining > 0.0F && totalFraction > 0.0)
    {
        for (usize index = 0U; index < count; ++index)
        {
            const UIGridTrack track = gridTrackAt(definitions, index);
            if (track.unit == UIGridTrackUnit::Fraction)
            {
                layout.sizes[index] += remaining * static_cast<float>(
                    static_cast<double>(track.value) / totalFraction);
            }
        }
    }

    float cursor = origin;
    for (usize index = 0U; index < count; ++index)
    {
        layout.offsets[index] = normalizeFloat(cursor);
        cursor += layout.sizes[index] + gap;
        if (!std::isfinite(cursor) ||
            !isFiniteNonNegative(layout.sizes[index]))
        {
            layout.valid = false;
            break;
        }
    }
    return layout;
}

struct GridLayoutPlan final {
    GridAxisLayout columns{};
    GridAxisLayout rows{};
    GridPlacementState placement{};
    bool valid = true;
};

[[nodiscard]] inline GridLayoutPlan resolveGridLayout(
    const UIGridContainerStyle& style, UILogicalRect contentRect,
    const GridMeasurement& measurement) noexcept
{
    GridLayoutPlan plan{
        .columns = resolveGridAxisLayout(
            style.columns, measurement.columnBases,
            measurement.placement.columnCount, contentRect.x,
            contentRect.width, style.gap.column),
        .rows = resolveGridAxisLayout(
            style.rows, measurement.rowBases,
            measurement.placement.rowCount, contentRect.y,
            contentRect.height, style.gap.row),
        .placement = beginGridPlacement(style),
    };
    plan.valid = measurement.placement.valid && plan.columns.valid &&
                 plan.rows.valid;
    return plan;
}

[[nodiscard]] inline UILogicalRect gridAreaRect(
    const GridLayoutPlan& plan, const GridArea& area) noexcept
{
    const usize lastColumn =
        static_cast<usize>(area.column) + area.columnSpan - 1U;
    const usize lastRow = static_cast<usize>(area.row) + area.rowSpan - 1U;
    return UILogicalRect{
        .x = plan.columns.offsets[area.column],
        .y = plan.rows.offsets[area.row],
        .width = normalizeFloat(
            plan.columns.offsets[lastColumn] +
            plan.columns.sizes[lastColumn] -
            plan.columns.offsets[area.column]),
        .height = normalizeFloat(
            plan.rows.offsets[lastRow] + plan.rows.sizes[lastRow] -
            plan.rows.offsets[area.row]),
    };
}

[[nodiscard]] inline UILogicalRect resolveGridItemRectInArea(
    const GridLayoutPlan& plan, const GridArea& area,
    const UIGridContainerStyle& parentStyle,
    const UILayoutStyle& childStyle,
    const LayoutScratchState& childScratch,
    LayoutPassStatistics& statistics) noexcept
{
    const UILogicalRect cell = gridAreaRect(plan, area);
    const float availableWidth = (std::max)(
        0.0F, cell.width - horizontalMargin(childStyle.margin));
    const float availableHeight = (std::max)(
        0.0F, cell.height - verticalMargin(childStyle.margin));
    const UIAxisAlignment horizontalAlignment = resolvedItemAlignment(
        parentStyle.justifyItems, childStyle.gridItem.justifySelf);
    const UIAxisAlignment verticalAlignment = resolvedItemAlignment(
        parentStyle.alignItems, childStyle.gridItem.alignSelf);

    float width = childScratch.measuredSize.width;
    float height = childScratch.measuredSize.height;
    if (horizontalAlignment == UIAxisAlignment::Stretch &&
        childStyle.size.width.isAuto())
    {
        width = availableWidth;
    }
    if (verticalAlignment == UIAxisAlignment::Stretch &&
        childStyle.size.height.isAuto())
    {
        height = availableHeight;
    }
    width = clampWidth(width, childStyle, childScratch, statistics);
    height = clampHeight(height, childStyle, childScratch, statistics);

    const auto alignedOffset = [](UIAxisAlignment alignment,
                                  float freeSpace) noexcept {
        if (freeSpace <= 0.0F)
        {
            return 0.0F;
        }
        switch (alignment)
        {
        case UIAxisAlignment::Center:
            return freeSpace * 0.5F;
        case UIAxisAlignment::End:
            return freeSpace;
        case UIAxisAlignment::Start:
        case UIAxisAlignment::Stretch:
            return 0.0F;
        }
        return 0.0F;
    };
    return UILogicalRect{
        .x = normalizeFloat(
            cell.x + childStyle.margin.left +
            alignedOffset(horizontalAlignment, availableWidth - width)),
        .y = normalizeFloat(
            cell.y + childStyle.margin.top +
            alignedOffset(verticalAlignment, availableHeight - height)),
        .width = normalizeFloat((std::max)(0.0F, width)),
        .height = normalizeFloat((std::max)(0.0F, height)),
    };
}

} // namespace Tina::UI::Detail
