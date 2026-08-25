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

inline void appendWrappedMinContentChild(
    LayoutFlowMeasurement& measurement, UIFlexDirection direction,
    float crossGap, const UILayoutStyle& childStyle,
    UILogicalSize childMinContentSize) noexcept
{
    const float childOuterWidth =
        childMinContentSize.width + horizontalMargin(childStyle.margin);
    const float childOuterHeight =
        childMinContentSize.height + verticalMargin(childStyle.margin);
    if (direction == UIFlexDirection::Row)
    {
        measurement.contentSize.width =
            (std::max)(measurement.contentSize.width, childOuterWidth);
        if (measurement.childCount > 0)
        {
            measurement.contentSize.height += crossGap;
        }
        measurement.contentSize.height += childOuterHeight;
    }
    else
    {
        if (measurement.childCount > 0)
        {
            measurement.contentSize.width += crossGap;
        }
        measurement.contentSize.width += childOuterWidth;
        measurement.contentSize.height =
            (std::max)(measurement.contentSize.height, childOuterHeight);
    }
    ++measurement.childCount;
}

struct LayoutNodeMeasureContent final {
    UILogicalSize size{};
    float indicatorLabelWidth = 0.0F;
    float indicatorLabelGap = 0.0F;
    float leadingIndicatorExtent = 0.0F;
    bool hasIndicatorLabel = false;
};

[[nodiscard]] inline UILogicalSize intrinsicOuterSize(
    LayoutNodeMeasureContent content,
    const UIEdgeSpacing& padding) noexcept
{
    if (content.leadingIndicatorExtent > 0.0F)
    {
        content.size.height =
            (std::max)(content.size.height, content.leadingIndicatorExtent);
        content.size.width = content.leadingIndicatorExtent;
        if (content.hasIndicatorLabel)
        {
            content.size.width +=
                content.indicatorLabelWidth + content.indicatorLabelGap;
        }
    }
    return UILogicalSize{
        .width = normalizeFloat(
            content.size.width + horizontalMargin(padding)),
        .height = normalizeFloat(
            content.size.height + verticalMargin(padding)),
    };
}

[[nodiscard]] inline UILogicalSize resolveMeasuredLayoutSize(
    const UILayoutStyle& style,
    const LayoutScratchState& scratch,
    UILogicalSize viewportSize,
    bool isRoot,
    LayoutNodeMeasureContent content,
    LayoutPassStatistics& statistics) noexcept
{
    if (content.leadingIndicatorExtent > 0.0F)
    {
        content.size.height =
            (std::max)(content.size.height, content.leadingIndicatorExtent);
    }
    float outerHeight = resolvedHeight(style, scratch, statistics);
    if (outerHeight < 0.0F)
    {
        const float intrinsicHeight =
            content.size.height + verticalMargin(style.padding);
        outerHeight = isRoot
                          ? (std::max)(intrinsicHeight, viewportSize.height)
                          : intrinsicHeight;
    }
    if (content.leadingIndicatorExtent > 0.0F)
    {
        content.size.width = content.leadingIndicatorExtent;
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
    applyAspectRatio(style, outerWidth, outerHeight);
    outerHeight = clampHeight(outerHeight, style, scratch, statistics);
    outerWidth = clampWidth(outerWidth, style, scratch, statistics);

    return UILogicalSize{
        .width = normalizeFloat(outerWidth),
        .height = normalizeFloat(outerHeight),
    };
}

} // namespace Tina::UI::Detail
