#pragma once

#include "UILayoutPrimitives.hpp"

#include <algorithm>
#include <cmath>

namespace Tina::UI::Detail {

struct FlexLineSummary final {
    usize itemCount = 0;
    float totalMain = 0.0F;
    float totalCross = 0.0F;
    double totalGrow = 0.0;
    double totalShrinkWeight = 0.0;

    [[nodiscard]] constexpr UILogicalSize contentSize(
        UIFlexDirection direction) const noexcept
    {
        return direction == UIFlexDirection::Row
                   ? UILogicalSize{.width = totalMain, .height = totalCross}
                   : UILogicalSize{.width = totalCross, .height = totalMain};
    }
};

struct FlexItemMeasurement final {
    float outerMain = 0.0F;
    float outerCross = 0.0F;
    float baseMain = 0.0F;
};

[[nodiscard]] inline FlexItemMeasurement measureFlexItem(
    UIFlexDirection direction, float initialContentMain,
    const UILayoutStyle& childStyle,
    const LayoutScratchState& childScratch,
    LayoutPassStatistics& statistics) noexcept
{
    const bool row = direction == UIFlexDirection::Row;
    const float baseMain = flexBaseMainSize(
        childStyle, childScratch, row, initialContentMain, statistics);
    return FlexItemMeasurement{
        .outerMain = (row ? baseMain : childScratch.measuredSize.height) +
                     (row ? horizontalMargin(childStyle.margin)
                          : verticalMargin(childStyle.margin)),
        .outerCross = (row ? childScratch.measuredSize.height
                           : childScratch.measuredSize.width) +
                      (row ? verticalMargin(childStyle.margin)
                           : horizontalMargin(childStyle.margin)),
        .baseMain = baseMain,
    };
}

inline void appendFlexLineItem(
    FlexLineSummary& summary,
    UIFlexDirection direction,
    float configuredGap,
    float initialContentMain,
    const UILayoutStyle& childStyle,
    const LayoutScratchState& childScratch,
    LayoutPassStatistics& statistics) noexcept
{
    const FlexItemMeasurement item = measureFlexItem(
        direction, initialContentMain, childStyle, childScratch, statistics);
    if (summary.itemCount > 0)
    {
        summary.totalMain += configuredGap;
    }
    summary.totalMain += item.outerMain;
    summary.totalCross = (std::max)(summary.totalCross, item.outerCross);
    summary.totalGrow += static_cast<double>(childStyle.flexItem.grow);
    summary.totalShrinkWeight +=
        static_cast<double>(childStyle.flexItem.shrink) *
        static_cast<double>((std::max)(0.0F, item.baseMain));
    ++summary.itemCount;
}

[[nodiscard]] inline bool flexItemStartsNewLine(
    const FlexLineSummary& line, UIFlexWrap wrap, float availableMain,
    float configuredGap, const FlexItemMeasurement& item) noexcept
{
    return wrap == UIFlexWrap::Wrap && line.itemCount != 0U &&
           isFiniteNonNegative(availableMain) &&
           line.totalMain + configuredGap + item.outerMain > availableMain;
}

struct FlexWrapMeasurement final {
    FlexLineSummary currentLine{};
    usize itemCount = 0U;
    usize lineCount = 0U;
    float maximumMain = 0.0F;
    float totalCross = 0.0F;

    [[nodiscard]] constexpr UILogicalSize contentSize(
        UIFlexDirection direction) const noexcept
    {
        return direction == UIFlexDirection::Row
                   ? UILogicalSize{.width = maximumMain, .height = totalCross}
                   : UILogicalSize{.width = totalCross, .height = maximumMain};
    }
};

inline void finishFlexMeasuredLine(
    FlexWrapMeasurement& measurement, float crossGap) noexcept
{
    if (measurement.currentLine.itemCount == 0U)
    {
        return;
    }
    measurement.maximumMain = (std::max)(
        measurement.maximumMain, measurement.currentLine.totalMain);
    if (measurement.lineCount != 0U)
    {
        measurement.totalCross += crossGap;
    }
    measurement.totalCross += measurement.currentLine.totalCross;
    ++measurement.lineCount;
    measurement.currentLine = {};
}

inline void appendFlexMeasuredItem(
    FlexWrapMeasurement& measurement, UIFlexDirection direction,
    UIFlexWrap wrap, float availableMain, float mainGap, float crossGap,
    const UILayoutStyle& childStyle,
    const LayoutScratchState& childScratch,
    LayoutPassStatistics& statistics) noexcept
{
    const FlexItemMeasurement item = measureFlexItem(
        direction, availableMain, childStyle, childScratch, statistics);
    if (flexItemStartsNewLine(
            measurement.currentLine, wrap, availableMain, mainGap, item))
    {
        finishFlexMeasuredLine(measurement, crossGap);
    }
    if (measurement.currentLine.itemCount != 0U)
    {
        measurement.currentLine.totalMain += mainGap;
    }
    measurement.currentLine.totalMain += item.outerMain;
    measurement.currentLine.totalCross = (std::max)(
        measurement.currentLine.totalCross, item.outerCross);
    measurement.currentLine.totalGrow +=
        static_cast<double>(childStyle.flexItem.grow);
    measurement.currentLine.totalShrinkWeight +=
        static_cast<double>(childStyle.flexItem.shrink) *
        static_cast<double>((std::max)(0.0F, item.baseMain));
    ++measurement.currentLine.itemCount;
    ++measurement.itemCount;
}

inline void finishFlexMeasurement(
    FlexWrapMeasurement& measurement, float crossGap) noexcept
{
    finishFlexMeasuredLine(measurement, crossGap);
}

[[nodiscard]] inline bool isValidFlexWrapMeasurement(
    const FlexWrapMeasurement& measurement) noexcept
{
    const FlexLineSummary& line = measurement.currentLine;
    return isFiniteNonNegative(measurement.maximumMain) &&
           isFiniteNonNegative(measurement.totalCross) &&
           isFiniteNonNegative(line.totalMain) &&
           isFiniteNonNegative(line.totalCross) &&
           std::isfinite(line.totalGrow) && line.totalGrow >= 0.0 &&
           std::isfinite(line.totalShrinkWeight) &&
           line.totalShrinkWeight >= 0.0;
}

[[nodiscard]] inline bool isValidFlexLineSummary(
    const FlexLineSummary& summary) noexcept
{
    return isFiniteNonNegative(summary.totalMain) &&
           isFiniteNonNegative(summary.totalCross) &&
           std::isfinite(summary.totalGrow) && summary.totalGrow >= 0.0 &&
           std::isfinite(summary.totalShrinkWeight) &&
           summary.totalShrinkWeight >= 0.0;
}

struct FlexLinePlan final {
    UIFlexDirection direction = UIFlexDirection::Column;
    UIAxisAlignment alignItems = UIAxisAlignment::Stretch;
    UILogicalRect contentRect{};
    float contentMain = 0.0F;
    float contentCross = 0.0F;
    float growSpace = 0.0F;
    float shrinkSpace = 0.0F;
    double totalGrow = 0.0;
    double totalShrinkWeight = 0.0;
    float gap = 0.0F;
    float nextMainOffset = 0.0F;
};

[[nodiscard]] inline FlexLinePlan resolveFlexLinePlan(
    const UILayoutStyle& parentStyle,
    UILogicalRect contentRect,
    const FlexLineSummary& summary) noexcept
{
    const bool row =
        parentStyle.flexContainer.direction == UIFlexDirection::Row;
    const float contentMain = row ? contentRect.width : contentRect.height;
    const float freeSpace = contentMain - summary.totalMain;
    FlexLinePlan plan{
        .direction = parentStyle.flexContainer.direction,
        .alignItems = parentStyle.flexContainer.alignItems,
        .contentRect = contentRect,
        .contentMain = contentMain,
        .contentCross = row ? contentRect.height : contentRect.width,
        .growSpace = summary.totalGrow > 0.0
                         ? (std::max)(0.0F, freeSpace)
                         : 0.0F,
        .shrinkSpace = summary.totalShrinkWeight > 0.0
                           ? (std::max)(0.0F, -freeSpace)
                           : 0.0F,
        .totalGrow = summary.totalGrow,
        .totalShrinkWeight = summary.totalShrinkWeight,
        .gap = row ? parentStyle.flexContainer.gap.column
                   : parentStyle.flexContainer.gap.row,
    };
    if (summary.totalGrow == 0.0 && freeSpace > 0.0F)
    {
        switch (parentStyle.flexContainer.justifyContent)
        {
        case UIJustifyContent::Center:
            plan.nextMainOffset = freeSpace * 0.5F;
            break;
        case UIJustifyContent::End:
            plan.nextMainOffset = freeSpace;
            break;
        case UIJustifyContent::SpaceBetween:
            if (summary.itemCount > 1)
            {
                plan.gap +=
                    freeSpace / static_cast<float>(summary.itemCount - 1);
            }
            break;
        case UIJustifyContent::Start:
            break;
        }
    }
    return plan;
}

[[nodiscard]] inline UILogicalRect resolveFlexItemRect(
    FlexLinePlan& plan,
    const UILayoutStyle& childStyle,
    const LayoutScratchState& childScratch,
    LayoutPassStatistics& statistics) noexcept
{
    const bool row = plan.direction == UIFlexDirection::Row;
    float width = childScratch.measuredSize.width;
    float height = childScratch.measuredSize.height;
    const float baseMain = flexBaseMainSize(
        childStyle, childScratch, row, plan.contentMain, statistics);
    if (row)
    {
        width = baseMain;
    } else
    {
        height = baseMain;
    }

    if (plan.growSpace > 0.0F && childStyle.flexItem.grow > 0.0F)
    {
        const double growRatio =
            static_cast<double>(childStyle.flexItem.grow) / plan.totalGrow;
        const float share = plan.growSpace * static_cast<float>(growRatio);
        if (row)
        {
            width += share;
        } else
        {
            height += share;
        }
    } else if (plan.shrinkSpace > 0.0F &&
               childStyle.flexItem.shrink > 0.0F)
    {
        const double childWeight =
            static_cast<double>(childStyle.flexItem.shrink) *
            static_cast<double>((std::max)(0.0F, baseMain));
        const float reduction = plan.shrinkSpace *
                                static_cast<float>(
                                    childWeight / plan.totalShrinkWeight);
        if (row)
        {
            width = (std::max)(0.0F, width - reduction);
        } else
        {
            height = (std::max)(0.0F, height - reduction);
        }
    }

    const float crossAvailable = (std::max)(
        0.0F,
        plan.contentCross -
            (row ? verticalMargin(childStyle.margin)
                 : horizontalMargin(childStyle.margin)));
    const UIAxisAlignment crossAlignment = resolvedItemAlignment(
        plan.alignItems, childStyle.flexItem.alignSelf);
    if (crossAlignment == UIAxisAlignment::Stretch &&
        isCrossAxisAuto(childStyle, plan.direction))
    {
        if (row)
        {
            height = crossAvailable;
        } else
        {
            width = crossAvailable;
        }
    }
    width = clampWidth(width, childStyle, childScratch, statistics);
    height = clampHeight(height, childStyle, childScratch, statistics);

    const float childMainSize = row ? width : height;
    const float childCrossSize = row ? height : width;
    const float mainBefore =
        row ? childStyle.margin.left : childStyle.margin.top;
    const float mainAfter =
        row ? childStyle.margin.right : childStyle.margin.bottom;
    const float crossBefore =
        row ? childStyle.margin.top : childStyle.margin.left;
    const float crossAfter =
        row ? childStyle.margin.bottom : childStyle.margin.right;

    float crossOffset = crossBefore;
    const float crossFree =
        plan.contentCross - childCrossSize - crossBefore - crossAfter;
    if (crossFree > 0.0F)
    {
        switch (crossAlignment)
        {
        case UIAxisAlignment::Center:
            crossOffset = crossBefore + crossFree * 0.5F;
            break;
        case UIAxisAlignment::End:
            crossOffset = crossBefore + crossFree;
            break;
        case UIAxisAlignment::Stretch:
        case UIAxisAlignment::Start:
            break;
        }
    }

    const float x = row
                        ? plan.contentRect.x + plan.nextMainOffset + mainBefore
                        : plan.contentRect.x + crossOffset;
    const float y = row
                        ? plan.contentRect.y + crossOffset
                        : plan.contentRect.y + plan.nextMainOffset + mainBefore;
    plan.nextMainOffset +=
        childMainSize + mainBefore + mainAfter + plan.gap;
    return UILogicalRect{
        .x = normalizeFloat(x),
        .y = normalizeFloat(y),
        .width = normalizeFloat((std::max)(0.0F, width)),
        .height = normalizeFloat((std::max)(0.0F, height)),
    };
}

} // namespace Tina::UI::Detail
