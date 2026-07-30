#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UILayout.hpp>

#include <compare>
#include <optional>

namespace Tina::UI {

// Authoring color in straight (non-premultiplied) sRGBA8 form. Integer input
// keeps retained paint comparison deterministic and excludes NaN payloads.
struct UIStraightSrgba8Color final {
    u8 red = 0;
    u8 green = 0;
    u8 blue = 0;
    u8 alpha = 0;

    auto operator<=>(const UIStraightSrgba8Color&) const = default;
};

// Authoring helpers: storage remains channel sRGBA8; hex is only an input sugar.
[[nodiscard]] constexpr UIStraightSrgba8Color rgba8(u8 red, u8 green, u8 blue, u8 alpha = 255) noexcept
{
    return UIStraightSrgba8Color{.red = red, .green = green, .blue = blue, .alpha = alpha};
}

// 0xRRGGBB with optional alpha (default opaque).
[[nodiscard]] constexpr UIStraightSrgba8Color rgb(u32 hexRgb, u8 alpha = 255) noexcept
{
    return rgba8(static_cast<u8>((hexRgb >> 16) & 0xFFU),
                 static_cast<u8>((hexRgb >> 8) & 0xFFU),
                 static_cast<u8>(hexRgb & 0xFFU),
                 alpha);
}

// 0xAARRGGBB
[[nodiscard]] constexpr UIStraightSrgba8Color argb(u32 hexArgb) noexcept
{
    return rgba8(static_cast<u8>((hexArgb >> 16) & 0xFFU),
                 static_cast<u8>((hexArgb >> 8) & 0xFFU),
                 static_cast<u8>(hexArgb & 0xFFU),
                 static_cast<u8>((hexArgb >> 24) & 0xFFU));
}

// Cached/snapshot color ready for conventional premultiplied-alpha blending.
struct UIPremultipliedRgba8Color final {
    u8 red = 0;
    u8 green = 0;
    u8 blue = 0;
    u8 alpha = 0;

    [[nodiscard]] constexpr bool isTransparent() const noexcept
    {
        return alpha == 0;
    }

    auto operator<=>(const UIPremultipliedRgba8Color&) const = default;
};

[[nodiscard]] constexpr UIPremultipliedRgba8Color premultiply(
    UIStraightSrgba8Color color) noexcept
{
    const auto multiplyChannel = [alpha = static_cast<u16>(color.alpha)](
                                     u8 channel) constexpr noexcept -> u8 {
        return static_cast<u8>(
            (static_cast<u16>(channel) * alpha + u16{127}) / u16{255});
    };
    return UIPremultipliedRgba8Color{
        .red = multiplyChannel(color.red),
        .green = multiplyChannel(color.green),
        .blue = multiplyChannel(color.blue),
        .alpha = color.alpha,
    };
}

struct UISolidFill final {
    UIStraightSrgba8Color color{};

    auto operator<=>(const UISolidFill&) const = default;
};

// Box paint: optional fill, dual-tone border (Phase A), optional shadow (Phase B),
// and a uniform corner radius (Phase C1). Rounded chrome affects this box only;
// descendant clipping remains axis-aligned until rounded clip is implemented.
struct UIBoxPaint final {
    std::optional<UISolidFill> solidFill{};
    // Top/left edge color when borderWidth > 0 and alpha != 0.
    UIStraightSrgba8Color borderLight{};
    // Bottom/right edge color when borderWidth > 0 and alpha != 0.
    UIStraightSrgba8Color borderDark{};
    float borderWidth = 0.0F;
    // Fake drop shadow drawn before fill (does not receive hit).
    UIStraightSrgba8Color shadow{};
    float shadowOffsetX = 0.0F;
    float shadowOffsetY = 0.0F;
    float cornerRadius = 0.0F;

    auto operator<=>(const UIBoxPaint&) const = default;
};

// Backend-neutral bounded canvas command. Bounds are local to the Element's
// border box and are clipped by the Element's committed effective clip. The
// first command slice intentionally supports solid rectangles, which map to the
// existing backend-neutral SolidQuad display-list command.
enum class UICanvasCommandKind : u8 {
    SolidRect = 0,
};

struct UICanvasCommand final {
    UICanvasCommandKind kind = UICanvasCommandKind::SolidRect;
    UILogicalRect bounds{};
    UIStraightSrgba8Color color{};
    // Rounded SolidRect radius in logical pixels. Rendering clamps it to half
    // the smallest projected extent; it does not establish a rounded clip.
    float cornerRadius = 0.0F;

    auto operator<=>(const UICanvasCommand&) const = default;
};

} // namespace Tina::UI
