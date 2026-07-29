#pragma once

#include <tina/ui/UIScrollView.hpp>

#include <algorithm>
#include <cmath>

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

} // namespace Tina::UI::Detail
