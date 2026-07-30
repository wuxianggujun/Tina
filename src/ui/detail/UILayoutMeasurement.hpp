#pragma once

#include "UILayoutPrimitives.hpp"

#include <algorithm>

namespace Tina::UI::Detail {

struct LayoutFlowMeasurement final {
    UILogicalSize contentSize{};
    usize childCount = 0;
};

inline void appendFlowMeasuredChild(
    LayoutFlowMeasurement& measurement,
    UIFlexDirection direction,
    float gap,
    const UILayoutStyle& childStyle,
    UILogicalSize childMeasuredSize) noexcept
{
    const float childOuterWidth =
        childMeasuredSize.width + horizontalMargin(childStyle.margin);
    const float childOuterHeight =
        childMeasuredSize.height + verticalMargin(childStyle.margin);
    if (direction == UIFlexDirection::Row)
    {
        if (measurement.childCount > 0)
        {
            measurement.contentSize.width += gap;
        }
        measurement.contentSize.width += childOuterWidth;
        measurement.contentSize.height =
            (std::max)(measurement.contentSize.height, childOuterHeight);
    } else
    {
        measurement.contentSize.width =
            (std::max)(measurement.contentSize.width, childOuterWidth);
        if (measurement.childCount > 0)
        {
            measurement.contentSize.height += gap;
        }
        measurement.contentSize.height += childOuterHeight;
    }
    ++measurement.childCount;
}

struct LayoutNodeMeasureContent final {
    UILogicalSize size{};
    float indicatorLabelWidth = 0.0F;
    float indicatorLabelGap = 0.0F;
    bool squareLeadingIndicator = false;
    bool hasIndicatorLabel = false;
};

[[nodiscard]] inline UILogicalSize resolveMeasuredLayoutSize(
    const UILayoutStyle& style,
    const LayoutScratchState& scratch,
    UILogicalSize viewportSize,
    bool isRoot,
    LayoutNodeMeasureContent content,
    LayoutPassStatistics& statistics) noexcept
{
    float outerHeight = resolvedHeight(style, scratch, statistics);
    if (outerHeight < 0.0F)
    {
        const float intrinsicHeight =
            content.size.height + verticalMargin(style.padding);
        outerHeight = isRoot
                          ? (std::max)(intrinsicHeight, viewportSize.height)
                          : intrinsicHeight;
    }
    outerHeight = clampHeight(outerHeight, style, scratch, statistics);

    if (content.squareLeadingIndicator)
    {
        content.size.width = outerHeight;
        if (content.hasIndicatorLabel)
        {
            content.size.width +=
                content.indicatorLabelWidth + content.indicatorLabelGap;
        }
    }

    float outerWidth = resolvedWidth(style, scratch, statistics);
    if (outerWidth < 0.0F)
    {
        const float intrinsicWidth =
            content.size.width + horizontalMargin(style.padding);
        outerWidth = isRoot
                         ? (std::max)(intrinsicWidth, viewportSize.width)
                         : intrinsicWidth;
    }
    outerWidth = clampWidth(outerWidth, style, scratch, statistics);

    return UILogicalSize{
        .width = normalizeFloat(outerWidth),
        .height = normalizeFloat(outerHeight),
    };
}

} // namespace Tina::UI::Detail
