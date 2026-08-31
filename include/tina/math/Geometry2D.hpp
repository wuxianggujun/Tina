#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/math/Constants.hpp>
#include <tina/math/Vec.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::Math {

// Axis-aligned 2D box stored as opposite corners. `lower` must not exceed `upper`
// on either axis; isValid() states that, and the constructors below maintain it.
//
// Corner storage rather than center/half-extents because merge, intersect and
// clip — the operations this type exists for — are componentwise min/max on
// corners, while center form would round-trip through a division each time.
struct Aabb2 final {
    Vec2 lower{};
    Vec2 upper{};

    friend constexpr bool operator==(const Aabb2&, const Aabb2&) noexcept = default;
};

// Position/extent rectangle for 2D screen and world space. Distinct from Aabb2 on
// purpose: this is the form authored content and layout use, and converting
// implicitly between the two is how a width ends up read as an x coordinate.
struct Rect final {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    friend constexpr bool operator==(const Rect&, const Rect&) noexcept = default;
};

// --- Aabb2 ---

[[nodiscard]] inline bool isFinite(const Aabb2& value) noexcept
{
    return isFinite(value.lower) && isFinite(value.upper);
}

// An empty box (lower == upper on an axis) is valid: it is the identity for merge
// and the correct result of clipping a box away entirely.
[[nodiscard]] inline bool isValid(const Aabb2& value) noexcept
{
    return isFinite(value) && value.lower.x <= value.upper.x && value.lower.y <= value.upper.y;
}

[[nodiscard]] constexpr Vec2 center(const Aabb2& value) noexcept
{
    return (value.lower + value.upper) * 0.5F;
}

[[nodiscard]] constexpr Vec2 extents(const Aabb2& value) noexcept
{
    return value.upper - value.lower;
}

[[nodiscard]] constexpr Vec2 halfExtents(const Aabb2& value) noexcept
{
    return extents(value) * 0.5F;
}

[[nodiscard]] constexpr float area(const Aabb2& value) noexcept
{
    const Vec2 size = extents(value);
    return size.x * size.y;
}

[[nodiscard]] constexpr Aabb2 fromCenterHalfExtents(Vec2 centerPoint, Vec2 half) noexcept
{
    return {centerPoint - half, centerPoint + half};
}

// Normalizes swapped corners rather than rejecting them, so a box built from two
// arbitrary points (a drag rectangle, for instance) is always valid.
[[nodiscard]] constexpr Aabb2 fromPoints(Vec2 first, Vec2 second) noexcept
{
    return {minimum(first, second), maximum(first, second)};
}

// Boundary counts as inside. Touching boxes therefore report as intersecting;
// callers needing strict separation compare extents themselves.
[[nodiscard]] constexpr bool contains(const Aabb2& value, Vec2 point) noexcept
{
    return point.x >= value.lower.x && point.x <= value.upper.x
        && point.y >= value.lower.y && point.y <= value.upper.y;
}

[[nodiscard]] constexpr bool contains(const Aabb2& outer, const Aabb2& inner) noexcept
{
    return inner.lower.x >= outer.lower.x && inner.upper.x <= outer.upper.x
        && inner.lower.y >= outer.lower.y && inner.upper.y <= outer.upper.y;
}

[[nodiscard]] constexpr bool intersects(const Aabb2& left, const Aabb2& right) noexcept
{
    return left.lower.x <= right.upper.x && left.upper.x >= right.lower.x
        && left.lower.y <= right.upper.y && left.upper.y >= right.lower.y;
}

[[nodiscard]] constexpr Aabb2 merge(const Aabb2& left, const Aabb2& right) noexcept
{
    return {minimum(left.lower, right.lower), maximum(left.upper, right.upper)};
}

[[nodiscard]] constexpr Aabb2 expand(const Aabb2& value, Vec2 point) noexcept
{
    return {minimum(value.lower, point), maximum(value.upper, point)};
}

[[nodiscard]] constexpr Aabb2 expand(const Aabb2& value, float margin) noexcept
{
    return {value.lower - Vec2{margin, margin}, value.upper + Vec2{margin, margin}};
}

// Componentwise clip. Produces an inverted box when the inputs do not overlap, so
// callers test intersects() first; this keeps the common case branch-free.
[[nodiscard]] constexpr Aabb2 intersection(const Aabb2& left, const Aabb2& right) noexcept
{
    return {maximum(left.lower, right.lower), minimum(left.upper, right.upper)};
}

[[nodiscard]] constexpr Vec2 clamped(const Aabb2& value, Vec2 point) noexcept
{
    return maximum(value.lower, minimum(value.upper, point));
}

// Conservative axis-aligned bounds of `value` rotated by `angleRadians` about the
// origin, then translated by `translation`.
//
// Conservative, not exact: the result contains the rotated box but is generally
// larger than its true bounds. That is the useful guarantee for broadphase and
// grid rasterization, where missing a cell is a correctness bug and covering one
// extra cell is not.
//
// The |cos| / |sin| form is the standard identity for the extent of a rotated box
// projected onto each axis; it avoids materializing and scanning four corners.
[[nodiscard]] inline Aabb2 rotatedBounds(
    const Aabb2& value,
    float angleRadians,
    Vec2 translation = {}) noexcept
{
    const Vec2 localCenter = center(value);
    const Vec2 localHalf = halfExtents(value);
    const float cosine = std::cos(angleRadians);
    const float sine = std::sin(angleRadians);
    const Vec2 worldCenter{
        translation.x + cosine * localCenter.x - sine * localCenter.y,
        translation.y + sine * localCenter.x + cosine * localCenter.y};
    const Vec2 worldHalf{
        std::abs(cosine) * localHalf.x + std::abs(sine) * localHalf.y,
        std::abs(sine) * localHalf.x + std::abs(cosine) * localHalf.y};
    return fromCenterHalfExtents(worldCenter, worldHalf);
}

// --- Rect ---

[[nodiscard]] inline bool isFinite(const Rect& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.width) && std::isfinite(value.height);
}

[[nodiscard]] inline bool isValid(const Rect& value) noexcept
{
    return isFinite(value) && value.width >= 0.0F && value.height >= 0.0F;
}

[[nodiscard]] constexpr Vec2 origin(const Rect& value) noexcept
{
    return {value.x, value.y};
}

[[nodiscard]] constexpr Vec2 size(const Rect& value) noexcept
{
    return {value.width, value.height};
}

[[nodiscard]] constexpr Vec2 center(const Rect& value) noexcept
{
    return {value.x + value.width * 0.5F, value.y + value.height * 0.5F};
}

[[nodiscard]] constexpr float right(const Rect& value) noexcept
{
    return value.x + value.width;
}

[[nodiscard]] constexpr float bottom(const Rect& value) noexcept
{
    return value.y + value.height;
}

[[nodiscard]] constexpr bool contains(const Rect& value, Vec2 point) noexcept
{
    return point.x >= value.x && point.x <= right(value)
        && point.y >= value.y && point.y <= bottom(value);
}

[[nodiscard]] constexpr bool intersects(const Rect& first, const Rect& second) noexcept
{
    return first.x <= right(second) && right(first) >= second.x
        && first.y <= bottom(second) && bottom(first) >= second.y;
}

// Explicit conversions in both directions. Rect uses position/extent while Aabb2
// uses corners; going through a named function keeps the two from being mixed up
// at an aggregate initialization.
[[nodiscard]] constexpr Aabb2 toAabb2(const Rect& value) noexcept
{
    return {{value.x, value.y}, {right(value), bottom(value)}};
}

[[nodiscard]] constexpr Rect toRect(const Aabb2& value) noexcept
{
    const Vec2 boxExtents = extents(value);
    return {value.lower.x, value.lower.y, boxExtents.x, boxExtents.y};
}

} // namespace Tina::Math
