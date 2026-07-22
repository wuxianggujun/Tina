#pragma once

#include <tina/core/base/Types.hpp>

#include <cmath>

namespace Tina::Scene {

struct Vec2 final {
    float x = 0.0F;
    float y = 0.0F;

    friend constexpr bool operator==(const Vec2&, const Vec2&) noexcept = default;
};

struct ColorRgba8 final {
    u8 red = 255;
    u8 green = 255;
    u8 blue = 255;
    u8 alpha = 255;

    friend constexpr bool operator==(const ColorRgba8&, const ColorRgba8&) noexcept = default;
};

enum class SpriteOverrideFlags : u8 {
    None = 0,
    Size = 1 << 0,
    Pivot = 1 << 1,
};

[[nodiscard]] constexpr SpriteOverrideFlags operator|(
    SpriteOverrideFlags left,
    SpriteOverrideFlags right) noexcept
{
    return static_cast<SpriteOverrideFlags>(
        static_cast<u8>(left) | static_cast<u8>(right));
}

[[nodiscard]] constexpr SpriteOverrideFlags operator&(
    SpriteOverrideFlags left,
    SpriteOverrideFlags right) noexcept
{
    return static_cast<SpriteOverrideFlags>(
        static_cast<u8>(left) & static_cast<u8>(right));
}

[[nodiscard]] constexpr bool hasFlag(
    SpriteOverrideFlags flags,
    SpriteOverrideFlags mask) noexcept
{
    return (static_cast<u8>(flags) & static_cast<u8>(mask)) != 0;
}

// Scene-owned 2D sprite draw component. Stores semantic fields only — no GPU
// handles. This slice extracts via fixtureSpriteKey (M8/M9 product seed /
// fixture resource id). Full AssetHandle + Cooked Sprite resolve is Deferred.
struct SpriteRenderer2D final {
    // Non-zero resolved render resource key written to RenderSprite2DInput.
    u32 fixtureSpriteKey = 0;
    SpriteOverrideFlags overrides = SpriteOverrideFlags::None;
    Vec2 sizeOverrideMeters{1.0F, 1.0F};
    // Pivot in [0,1] relative to sprite extents; geometric center is adjusted
    // from the entity WorldTransform position before writing RenderSprite2DInput.
    Vec2 pivotOverride{0.5F, 0.5F};
    ColorRgba8 color{};
    i16 sortingLayer = 0;
    i32 orderInLayer = 0;
    bool flipX = false;
    bool flipY = false;
    bool visible = true;

    friend constexpr bool operator==(const SpriteRenderer2D&, const SpriteRenderer2D&) noexcept = default;
};

[[nodiscard]] inline bool isValid(const SpriteRenderer2D& sprite) noexcept
{
    if (sprite.fixtureSpriteKey == 0) {
        return false;
    }
    if (!std::isfinite(sprite.sizeOverrideMeters.x)
        || !std::isfinite(sprite.sizeOverrideMeters.y)
        || !std::isfinite(sprite.pivotOverride.x)
        || !std::isfinite(sprite.pivotOverride.y)) {
        return false;
    }
    if (hasFlag(sprite.overrides, SpriteOverrideFlags::Size)) {
        if (sprite.sizeOverrideMeters.x <= 0.0F || sprite.sizeOverrideMeters.y <= 0.0F) {
            return false;
        }
    }
    return true;
}

} // namespace Tina::Scene
