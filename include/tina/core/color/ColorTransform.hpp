#pragma once

#include <tina/core/base/Types.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace Tina::Core {

// Compact byte color for APIs that deliberately retain normalized RGBA8 (for
// example 3D particles). Sprite2D uses the float transform below, never this type.
struct ColorRgba8 final {
    u8 red = 255, green = 255, blue = 255, alpha = 255;
    friend constexpr bool operator==(const ColorRgba8&, const ColorRgba8&) noexcept = default;
};

// Linear, straight-alpha channels. Color transforms deliberately allow signed
// RGB and values above one; quantization belongs at the final output boundary.
struct ColorRgba final {
    float red = 1.0F;
    float green = 1.0F;
    float blue = 1.0F;
    float alpha = 1.0F;

    [[nodiscard]] static constexpr ColorRgba fromBytes(u8 red, u8 green, u8 blue, u8 alpha = 255) noexcept
    {
        constexpr float Unit = 1.0F / 255.0F;
        return {red * Unit, green * Unit, blue * Unit, alpha * Unit};
    }

    friend constexpr bool operator==(const ColorRgba&, const ColorRgba&) noexcept = default;
};

struct ColorTransform final {
    ColorRgba multiply{};
    ColorRgba add{0.0F, 0.0F, 0.0F, 0.0F};

    [[nodiscard]] constexpr std::array<float, 8> channels() const noexcept
    {
        return {multiply.red, multiply.green, multiply.blue, multiply.alpha,
                add.red, add.green, add.blue, add.alpha};
    }

    [[nodiscard]] static constexpr ColorTransform fromChannels(const std::array<float, 8>& values) noexcept
    {
        return {{values[0], values[1], values[2], values[3]},
                {values[4], values[5], values[6], values[7]}};
    }

    friend constexpr bool operator==(const ColorTransform&, const ColorTransform&) noexcept = default;
};

[[nodiscard]] inline bool isValidColorTransform(const ColorTransform& transform) noexcept
{
    const auto channels = transform.channels();
    for (float channel : channels) {
        if (!std::isfinite(channel)) return false;
    }
    for (usize index = 0; index < 4; ++index) {
        if (!std::isfinite(channels[index] + channels[index + 4])) return false;
    }
    return true;
}

// Conservative for every sampled alpha in [0,1], including negative multipliers.
[[nodiscard]] constexpr bool isFullyTransparent(const ColorTransform& transform) noexcept
{
    return (std::max)(transform.add.alpha, transform.multiply.alpha + transform.add.alpha) <= 0.0F;
}

[[nodiscard]] inline ColorTransform interpolateColorTransform(
    const ColorTransform& first, const ColorTransform& second, float amount) noexcept
{
    auto result = first.channels();
    const auto target = second.channels();
    for (usize index = 0; index < result.size(); ++index) {
        result[index] = std::lerp(result[index], target[index], amount);
    }
    return ColorTransform::fromChannels(result);
}

} // namespace Tina::Core
