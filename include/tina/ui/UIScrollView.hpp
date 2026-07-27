#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIPaint.hpp>

#include <compare>

namespace Tina::UI {

enum class UIScrollAxes : u8 {
    None = 0,
    Horizontal = 1U << 0U,
    Vertical = 1U << 1U,
    Both = (1U << 0U) | (1U << 1U),
};

[[nodiscard]] constexpr UIScrollAxes operator|(UIScrollAxes left, UIScrollAxes right) noexcept
{
    return static_cast<UIScrollAxes>(static_cast<u8>(left) | static_cast<u8>(right));
}

[[nodiscard]] constexpr bool hasScrollAxis(UIScrollAxes axes, UIScrollAxes axis) noexcept
{
    return (static_cast<u8>(axes) & static_cast<u8>(axis)) != 0;
}

enum class UIScrollBarVisibility : u8 {
    Auto,
    Always,
    Hidden,
};

struct UIScrollOffset final {
    float x = 0.0F;
    float y = 0.0F;

    auto operator<=>(const UIScrollOffset&) const = default;
};

struct UIScrollViewStyle final {
    UIScrollAxes axes = UIScrollAxes::Vertical;
    UIScrollBarVisibility scrollBarVisibility = UIScrollBarVisibility::Auto;
    // One platform wheel unit advances this many logical pixels.
    float wheelStep = 48.0F;

    auto operator<=>(const UIScrollViewStyle&) const = default;
};

struct UIScrollViewPaint final {
    UIStraightSrgba8Color trackColor{};
    UIStraightSrgba8Color thumbColor{};
    UIStraightSrgba8Color draggingThumbColor{};
    float thickness = 10.0F;
    float minThumbExtent = 24.0F;

    auto operator<=>(const UIScrollViewPaint&) const = default;
};

// Geometry from the last successful layout publication. A newly created view
// reports zero extents until its first successful commitLayout().
struct UIScrollViewMetrics final {
    UIScrollOffset offset{};
    UILogicalSize viewportSize{};
    UILogicalSize contentSize{};
    bool horizontalScrollBarVisible = false;
    bool verticalScrollBarVisible = false;

    [[nodiscard]] constexpr float maxOffsetX() const noexcept
    {
        return contentSize.width > viewportSize.width ? contentSize.width - viewportSize.width : 0.0F;
    }

    [[nodiscard]] constexpr float maxOffsetY() const noexcept
    {
        return contentSize.height > viewportSize.height ? contentSize.height - viewportSize.height : 0.0F;
    }

    auto operator<=>(const UIScrollViewMetrics&) const = default;
};

} // namespace Tina::UI
