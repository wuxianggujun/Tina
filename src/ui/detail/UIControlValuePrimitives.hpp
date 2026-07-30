#pragma once

#include <tina/ui/UIListView.hpp>
#include <tina/ui/UIScrollView.hpp>
#include <tina/ui/UISlider.hpp>
#include <tina/ui/UITreeView.hpp>

#include "UIControlGeometry.hpp"
#include "UILayoutPrimitives.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace Tina::UI::Detail {

[[nodiscard]] inline float quantizeSliderValue(
    double value, float minValue, float maxValue, float step) noexcept
{
    const double minimum = static_cast<double>(minValue);
    const double maximum = static_cast<double>(maxValue);
    const double clamped = std::clamp(value, minimum, maximum);
    if (!(step > 0.0F) || !std::isfinite(step))
    {
        return static_cast<float>(clamped);
    }
    const double span = maximum - minimum;
    if (!(span > 0.0))
    {
        return minValue;
    }
    const double steps =
        std::round((clamped - minimum) / static_cast<double>(step));
    const double quantized = std::clamp(
        minimum + steps * static_cast<double>(step), minimum, maximum);
    return static_cast<float>(quantized);
}

[[nodiscard]] inline std::optional<float> resolveSliderValueFromPointer(
    UILogicalRect worldRect, const UISliderPaint& paint, float pointerX,
    float minValue, float maxValue, float step) noexcept
{
    const SliderTrackGeometry track = sliderTrackGeometry(worldRect, paint);
    const float centerSpan = track.endCenterX - track.startCenterX;
    if (!(centerSpan > 0.0F))
    {
        return std::nullopt;
    }
    const double fraction = std::clamp(
        (static_cast<double>(pointerX) - static_cast<double>(track.startCenterX)) /
            static_cast<double>(centerSpan),
        0.0, 1.0);
    const double rawValue = static_cast<double>(minValue) +
                            fraction * (static_cast<double>(maxValue) -
                                        static_cast<double>(minValue));
    return quantizeSliderValue(rawValue, minValue, maxValue, step);
}

[[nodiscard]] constexpr float scrollAxisOffset(
    UIScrollOffset offset, UIScrollAxes axis) noexcept
{
    return axis == UIScrollAxes::Horizontal ? offset.x : offset.y;
}

constexpr void setScrollAxisOffset(
    UIScrollOffset& offset, UIScrollAxes axis, float value) noexcept
{
    if (axis == UIScrollAxes::Horizontal)
    {
        offset.x = value;
    } else
    {
        offset.y = value;
    }
}

[[nodiscard]] constexpr float scrollAxisMaxOffset(
    const UIScrollViewMetrics& metrics, UIScrollAxes axis) noexcept
{
    return axis == UIScrollAxes::Horizontal ? metrics.maxOffsetX()
                                            : metrics.maxOffsetY();
}

[[nodiscard]] inline float resolveVirtualScrollWheelOffset(
    float requestedOffset, float maxOffset, float wheelStep,
    UILogicalPoint delta) noexcept
{
    const float wheelDelta = delta.y != 0.0F ? delta.y : delta.x;
    return normalizeFloat((std::clamp)(
        requestedOffset - wheelDelta * wheelStep, 0.0F, maxOffset));
}

[[nodiscard]] inline UIScrollOffset resolveScrollWheelOffset(
    UIScrollOffset requestedOffset, const UIScrollViewStyle& style,
    const UIScrollViewMetrics& metrics, UILogicalPoint delta) noexcept
{
    const bool horizontal = hasScrollAxis(style.axes, UIScrollAxes::Horizontal);
    const bool vertical = hasScrollAxis(style.axes, UIScrollAxes::Vertical);
    float horizontalDelta = delta.x;
    float verticalDelta = delta.y;
    if (horizontal && !vertical && horizontalDelta == 0.0F)
    {
        horizontalDelta = verticalDelta;
    }
    if (vertical && !horizontal && verticalDelta == 0.0F)
    {
        verticalDelta = horizontalDelta;
    }

    if (horizontal)
    {
        requestedOffset.x = normalizeFloat(
            requestedOffset.x - horizontalDelta * style.wheelStep);
    }
    if (vertical)
    {
        requestedOffset.y = normalizeFloat(
            requestedOffset.y - verticalDelta * style.wheelStep);
    }
    requestedOffset.x = horizontal
                            ? normalizeFloat((std::clamp)(
                                  requestedOffset.x, 0.0F, metrics.maxOffsetX()))
                            : 0.0F;
    requestedOffset.y = vertical
                            ? normalizeFloat((std::clamp)(
                                  requestedOffset.y, 0.0F, metrics.maxOffsetY()))
                            : 0.0F;
    return requestedOffset;
}

[[nodiscard]] inline std::optional<float> resolveScrollThumbOffset(
    const ScrollBarGeometry& geometry, UIScrollAxes axis,
    UILogicalPoint position, float grabOffset, float maxOffset) noexcept
{
    const float trackStart =
        axis == UIScrollAxes::Horizontal ? geometry.track.x : geometry.track.y;
    const float trackExtent = axis == UIScrollAxes::Horizontal
                                  ? geometry.track.width
                                  : geometry.track.height;
    const float thumbExtent = axis == UIScrollAxes::Horizontal
                                  ? geometry.thumb.width
                                  : geometry.thumb.height;
    const float pointer =
        axis == UIScrollAxes::Horizontal ? position.x : position.y;
    const float travel = (std::max)(0.0F, trackExtent - thumbExtent);
    if (!geometry.visible || !(maxOffset > 0.0F) || !(travel > 0.0F))
    {
        return std::nullopt;
    }
    const float thumbStart =
        (std::clamp)(pointer - grabOffset - trackStart, 0.0F, travel);
    return normalizeFloat(maxOffset * (thumbStart / travel));
}

[[nodiscard]] inline std::optional<float> resolveScrollTrackPageOffset(
    const ScrollBarGeometry& geometry, UIScrollAxes axis,
    UILogicalPoint position, float currentOffset, float pageExtent) noexcept
{
    if (!geometry.visible)
    {
        return std::nullopt;
    }
    const float pointer =
        axis == UIScrollAxes::Horizontal ? position.x : position.y;
    const float thumbStart =
        axis == UIScrollAxes::Horizontal ? geometry.thumb.x : geometry.thumb.y;
    const float direction = pointer < thumbStart ? -1.0F : 1.0F;
    return normalizeFloat(currentOffset + direction * pageExtent);
}

enum class VirtualRowScrollAlignment : u8 {
    Nearest,
    Start,
    Center,
    End,
};

[[nodiscard]] constexpr VirtualRowScrollAlignment toVirtualRowScrollAlignment(
    UIListViewScrollAlignment alignment) noexcept
{
    switch (alignment)
    {
    case UIListViewScrollAlignment::Nearest:
        return VirtualRowScrollAlignment::Nearest;
    case UIListViewScrollAlignment::Start:
        return VirtualRowScrollAlignment::Start;
    case UIListViewScrollAlignment::Center:
        return VirtualRowScrollAlignment::Center;
    case UIListViewScrollAlignment::End:
        return VirtualRowScrollAlignment::End;
    }
    return VirtualRowScrollAlignment::Nearest;
}

[[nodiscard]] constexpr VirtualRowScrollAlignment toVirtualRowScrollAlignment(
    UITreeViewScrollAlignment alignment) noexcept
{
    switch (alignment)
    {
    case UITreeViewScrollAlignment::Nearest:
        return VirtualRowScrollAlignment::Nearest;
    case UITreeViewScrollAlignment::Start:
        return VirtualRowScrollAlignment::Start;
    case UITreeViewScrollAlignment::Center:
        return VirtualRowScrollAlignment::Center;
    case UITreeViewScrollAlignment::End:
        return VirtualRowScrollAlignment::End;
    }
    return VirtualRowScrollAlignment::Nearest;
}

[[nodiscard]] inline std::optional<float> resolveVirtualRowScrollOffset(
    u64 logicalIndex, u64 logicalItemCount, float rowHeight,
    float viewportHeight, float requestedOffset,
    VirtualRowScrollAlignment alignment) noexcept
{
    const double contentHeight64 =
        static_cast<double>(logicalItemCount) * rowHeight;
    if (!std::isfinite(contentHeight64) ||
        contentHeight64 > (std::numeric_limits<float>::max)())
    {
        return std::nullopt;
    }

    const float rowStart =
        static_cast<float>(static_cast<double>(logicalIndex) * rowHeight);
    const float rowEnd = rowStart + rowHeight;
    const float maximumOffset = normalizeFloat(
        (std::max)(0.0F, static_cast<float>(contentHeight64) - viewportHeight));
    float nextOffset = requestedOffset;
    switch (alignment)
    {
    case VirtualRowScrollAlignment::Start:
        nextOffset = rowStart;
        break;
    case VirtualRowScrollAlignment::Center:
        nextOffset = rowStart - (viewportHeight - rowHeight) * 0.5F;
        break;
    case VirtualRowScrollAlignment::End:
        nextOffset = rowEnd - viewportHeight;
        break;
    case VirtualRowScrollAlignment::Nearest:
        if (rowStart < nextOffset)
        {
            nextOffset = rowStart;
        } else if (rowEnd > nextOffset + viewportHeight)
        {
            nextOffset = rowEnd - viewportHeight;
        }
        break;
    }
    return normalizeFloat(
        (std::clamp)(nextOffset, 0.0F, maximumOffset));
}

} // namespace Tina::UI::Detail
