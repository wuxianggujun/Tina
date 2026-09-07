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

// One border-box resolver serves both intrinsic Measure and the final parent
// Arrange constraint. No child reads the window/viewport as a fallback.
[[nodiscard]] inline UILogicalSize resolveConstrainedBorderSize(
    const UILayoutStyle& style, const LayoutScratchState& scratch,
    UILogicalSize intrinsic, LayoutPassStatistics& statistics) noexcept
{
    float outerHeight = resolvedHeight(style, scratch, statistics);
    float outerWidth = resolvedWidth(style, scratch, statistics);
    if (outerHeight < 0.0F) { outerHeight = intrinsic.height; }
    if (outerWidth < 0.0F) { outerWidth = intrinsic.width; }
    const UILogicalSize constrained = scratch.measureConstraints.constrain({outerWidth, outerHeight});
    outerWidth = constrained.width;
    outerHeight = constrained.height;
    applyAspectRatio(style, outerWidth, outerHeight);
    outerHeight = clampHeight(outerHeight, style, scratch, statistics);
    outerWidth = clampWidth(outerWidth, style, scratch, statistics);

    return UILogicalSize{
        .width = normalizeFloat(outerWidth),
        .height = normalizeFloat(outerHeight),
    };
}

[[nodiscard]] inline UILogicalSize resolveMeasuredLayoutSize(
    const UILayoutStyle& style, const LayoutScratchState& scratch,
    LayoutNodeMeasureContent content, LayoutPassStatistics& statistics) noexcept
{
    return resolveConstrainedBorderSize(style, scratch, intrinsicOuterSize(content, style.padding), statistics);
}

[[nodiscard]] inline UILayoutConstraints prepareContentConstraints(
    const UILayoutStyle& style, const LayoutScratchState& scratch) noexcept
{
    ResolvedLength width = resolveLength(style.size.width, scratch.parentContentConstraints.width);
    ResolvedLength height = resolveLength(style.size.height, scratch.parentContentConstraints.height);
    if (!width.hasValue && scratch.measureConstraints.width.minimum > 0.0F)
    { width = {true, scratch.measureConstraints.width.minimum}; }
    if (!height.hasValue && scratch.measureConstraints.height.minimum > 0.0F)
    { height = {true, scratch.measureConstraints.height.minimum}; }
    if (style.aspectRatio && width.hasValue && style.size.height.isAuto())
    { height = {true, width.value / *style.aspectRatio}; }
    else if (style.aspectRatio && height.hasValue && style.size.width.isAuto())
    { width = {true, height.value * *style.aspectRatio}; }
    LayoutPassStatistics ignored;
    UILayoutConstraints content;
    if (width.hasValue)
    {
        content.width = UILayoutAxisConstraint::Tight((std::max)(0.0F,
            clampWidth(width.value, style, scratch, ignored) - horizontalMargin(style.padding)));
    }
    if (height.hasValue)
    {
        content.height = UILayoutAxisConstraint::Tight((std::max)(0.0F,
            clampHeight(height.value, style, scratch, ignored) - verticalMargin(style.padding)));
    }
    return content;
}

} // namespace Tina::UI::Detail
