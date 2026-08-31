#pragma once

#include <tina/asset/AssetHandle.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/math/Vec.hpp>

#include <cmath>

namespace Tina::Scene {

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
    // Optional normalized UV rect on the bound texture/atlas.
    // Without UvRect, extract writes full texture [0,1] like RenderSprite2DInput defaults.
    UvRect = 1 << 2,
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

// Normalized texture-space UV rectangle [u0,v0]→[u1,v1], same contract as
// RenderSprite2DInput (finite, 0..1 inclusive, strict u0<u1 and v0<v1).
struct SpriteUvRect final {
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 1.0F;
    float v1 = 1.0F;

    friend constexpr bool operator==(const SpriteUvRect&, const SpriteUvRect&) noexcept = default;
};

// Scene-owned 2D sprite draw component. Stores a copyable weak AssetHandle and
// semantic fields only; it never owns an AssetLease or backend/GPU handle.
struct SpriteRenderer2D final {
    Asset::AssetHandle sprite{};
    // Optional weak Texture2D asset. Extraction resolves it independently from
    // the Sprite asset and never retains a lease or backend binding here.
    Asset::AssetHandle normalTexture{};
    SpriteOverrideFlags overrides = SpriteOverrideFlags::None;
    Math::Vec2 sizeOverrideMeters{1.0F, 1.0F};
    // Pivot in [0,1] relative to sprite extents; geometric center is adjusted
    // from the entity WorldTransform position before writing RenderSprite2DInput.
    Math::Vec2 pivotOverride{0.5F, 0.5F};
    // Used only when overrides includes UvRect; otherwise extract uses full [0,1].
    SpriteUvRect uvRectOverride{};
    ColorRgba8 color{};
    i16 sortingLayer = 0;
    i32 orderInLayer = 0;
    bool flipX = false;
    bool flipY = false;
    bool visible = true;

    friend constexpr bool operator==(const SpriteRenderer2D&, const SpriteRenderer2D&) noexcept = default;
};

[[nodiscard]] inline bool isValidUvRect(const SpriteUvRect& uv) noexcept
{
    if (!std::isfinite(uv.u0) || !std::isfinite(uv.v0) || !std::isfinite(uv.u1) || !std::isfinite(uv.v1)) {
        return false;
    }
    if (uv.u0 < 0.0F || uv.v0 < 0.0F || uv.u1 > 1.0F || uv.v1 > 1.0F) {
        return false;
    }
    return (uv.u0 < uv.u1) && (uv.v0 < uv.v1);
}

[[nodiscard]] inline bool isValid(const SpriteRenderer2D& sprite) noexcept
{
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
    if (hasFlag(sprite.overrides, SpriteOverrideFlags::UvRect)) {
        if (!isValidUvRect(sprite.uvRectOverride)) {
            return false;
        }
    }
    return true;
}

} // namespace Tina::Scene
