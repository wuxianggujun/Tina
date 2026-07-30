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

inline void appendFlexLineItem(
    FlexLineSummary& summary,
    UIFlexDirection direction,
    float configuredGap,
    float initialContentMain,
    const UILayoutStyle& childStyle,
    const LayoutScratchState& childScratch,
    LayoutPassStatistics& statistics) noexcept
{
    const bool row = direction == UIFlexDirection::Row;
    if (summary.itemCount > 0)
    {
        summary.totalMain += configuredGap;
    }
    const float baseMain = flexBaseMainSize(
        childStyle, childScratch, row, initialContentMain, statistics);
    const float childOuterWidth =
        (row ? baseMain : childScratch.measuredSize.width) +
        horizontalMargin(childStyle.margin);
    const float childOuterHeight =
        (row ? childScratch.measuredSize.height : baseMain) +
        verticalMargin(childStyle.margin);
    summary.totalMain += row ? childOuterWidth : childOuterHeight;
    summary.totalCross = (std::max)(
        summary.totalCross, row ? childOuterHeight : childOuterWidth);
    summary.totalGrow += static_cast<double>(childStyle.flexItem.grow);
    summary.totalShrinkWeight +=
        static_cast<double>(childStyle.flexItem.shrink) *
        static_cast<double>((std::max)(0.0F, baseMain));
    ++summary.itemCount;
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
