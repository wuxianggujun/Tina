#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UIImageSource.hpp>
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

// Geometry primitive used by an Element's box paint. Rectangle preserves the
// normal box chrome path; Ellipse uses the layout rect as its ellipse bounds;
// Line uses the local endpoints and thickness below.
enum class UIBoxPrimitiveKind : u8 {
    Rectangle = 0,
    Ellipse,
    Line,
};

struct UILineGeometry final {
    // Endpoints are local to the Element border box. The renderer applies the
    // logical-to-framebuffer transform to every corner independently.
    UILogicalPoint start{};
    UILogicalPoint end{};
    float thickness = 0.0F;

    auto operator<=>(const UILineGeometry&) const = default;
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
    UIBoxPrimitiveKind primitive = UIBoxPrimitiveKind::Rectangle;
    UILineGeometry line{};
    // Ellipse stroke width in logical pixels. Zero draws a filled ellipse;
    // positive values draw an inward ring. This field is ignored by other
    // primitive kinds.
    float ellipseStrokeWidth = 0.0F;

    auto operator<=>(const UIBoxPaint&) const = default;
};

[[nodiscard]] constexpr UIBoxPaint makeSolidBox(
    UIStraightSrgba8Color color,
    float cornerRadius = 0.0F) noexcept
{
    UIBoxPaint paint{.solidFill = UISolidFill{.color = color}};
    paint.cornerRadius = cornerRadius;
    return paint;
}

[[nodiscard]] constexpr UIBoxPaint makeSolidEllipse(
    UIStraightSrgba8Color color, float strokeWidth = 0.0F) noexcept
{
    UIBoxPaint paint = makeSolidBox(color);
    paint.primitive = UIBoxPrimitiveKind::Ellipse;
    paint.ellipseStrokeWidth = strokeWidth;
    return paint;
}

[[nodiscard]] constexpr UIBoxPaint makeEllipseOutline(
    UIStraightSrgba8Color color, float strokeWidth) noexcept
{
    return makeSolidEllipse(color, strokeWidth);
}

[[nodiscard]] constexpr UIBoxPaint makeSolidLine(
    UIStraightSrgba8Color color, UILogicalPoint start,
    UILogicalPoint end, float thickness) noexcept
{
    UIBoxPaint paint = makeSolidBox(color);
    paint.primitive = UIBoxPrimitiveKind::Line;
    paint.line = UILineGeometry{
        .start = start,
        .end = end,
        .thickness = thickness,
    };
    return paint;
}

// Backend-neutral bounded canvas command. Geometry is local to the Element's
// border box and is clipped by that box intersected with its committed effective
// clip. Image and NineSlice reuse the same retained source and eventually emit
// ImageQuad; NineSlice is expanded before the DisplayList boundary.
enum class UICanvasCommandKind : u8 {
    SolidRect = 0,
    Image,
    NineSlice,
    SolidEllipse,
    SolidLine,
};

struct UICanvasCommand final {
    UICanvasCommandKind kind = UICanvasCommandKind::SolidRect;
    UILogicalRect bounds{};
    // Solid shape fill or Image/NineSlice tint.
    UIStraightSrgba8Color color{};
    // Rounded SolidRect radius in logical pixels. Rendering clamps it to half
    // the smallest projected extent; it does not establish a rounded clip.
    float cornerRadius = 0.0F;
    // Image metadata is ignored for solid shapes. Image requires zero insets;
    // NineSlice interprets source insets in pixels and destination insets in
    // logical pixels. The first NineSlice slice supports Stretch only.
    UIImageSource imageSource{};
    UIImagePixelInsets imageSourceInsets{};
    UIEdgeSpacing imageDestinationInsets{};
    UIImageSampling imageSampling = UIImageSampling::Linear;
    // SolidEllipse uses bounds as its local ellipse rect and
    // ellipseStrokeWidth (zero means filled). SolidLine uses lineStart/end and
    // lineThickness; bounds is ignored for that kind.
    UILogicalPoint lineStart{};
    UILogicalPoint lineEnd{};
    float lineThickness = 0.0F;
    float ellipseStrokeWidth = 0.0F;

    auto operator<=>(const UICanvasCommand&) const = default;
};

[[nodiscard]] constexpr UICanvasCommand makeCanvasEllipse(
    UILogicalRect bounds, UIStraightSrgba8Color color,
    float strokeWidth = 0.0F) noexcept
{
    return UICanvasCommand{
        .kind = UICanvasCommandKind::SolidEllipse,
        .bounds = bounds,
        .color = color,
        .ellipseStrokeWidth = strokeWidth,
    };
}

[[nodiscard]] constexpr UICanvasCommand makeCanvasLine(
    UILogicalPoint start, UILogicalPoint end, float thickness,
    UIStraightSrgba8Color color) noexcept
{
    return UICanvasCommand{
        .kind = UICanvasCommandKind::SolidLine,
        .color = color,
        .lineStart = start,
        .lineEnd = end,
        .lineThickness = thickness,
    };
}

} // namespace Tina::UI
