#pragma once

#include <tina/core/base/Types.hpp>

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

// First paint schema: a box may optionally emit one solid fill. Default and
// explicitly cleared paint emit nothing.
struct UIBoxPaint final {
    std::optional<UISolidFill> solidFill{};

    auto operator<=>(const UIBoxPaint&) const = default;
};

} // namespace Tina::UI
