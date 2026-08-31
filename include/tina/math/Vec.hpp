#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/math/Constants.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::Math {

struct Vec2 final {
    float x = 0.0F;
    float y = 0.0F;

    friend constexpr bool operator==(const Vec2&, const Vec2&) noexcept = default;
};

struct Vec3 final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    friend constexpr bool operator==(const Vec3&, const Vec3&) noexcept = default;
};

// Homogeneous coordinate or RGBA-style tuple. w defaults to 0 so a default Vec4
// is the zero vector; point construction states w = 1 explicitly.
struct Vec4 final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;

    friend constexpr bool operator==(const Vec4&, const Vec4&) noexcept = default;
};

// --- Vec2 arithmetic ---

[[nodiscard]] constexpr Vec2 operator+(Vec2 left, Vec2 right) noexcept
{
    return {left.x + right.x, left.y + right.y};
}

[[nodiscard]] constexpr Vec2 operator-(Vec2 left, Vec2 right) noexcept
{
    return {left.x - right.x, left.y - right.y};
}

[[nodiscard]] constexpr Vec2 operator-(Vec2 value) noexcept
{
    return {-value.x, -value.y};
}

// Component-wise product, not a dot product. Used for non-uniform 2D scale.
[[nodiscard]] constexpr Vec2 operator*(Vec2 left, Vec2 right) noexcept
{
    return {left.x * right.x, left.y * right.y};
}

[[nodiscard]] constexpr Vec2 operator*(Vec2 value, float scalar) noexcept
{
    return {value.x * scalar, value.y * scalar};
}

[[nodiscard]] constexpr Vec2 operator*(float scalar, Vec2 value) noexcept
{
    return value * scalar;
}

[[nodiscard]] constexpr Vec2 operator/(Vec2 value, float scalar) noexcept
{
    return {value.x / scalar, value.y / scalar};
}

// --- Vec3 arithmetic ---

[[nodiscard]] constexpr Vec3 operator+(Vec3 left, Vec3 right) noexcept
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] constexpr Vec3 operator-(Vec3 left, Vec3 right) noexcept
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] constexpr Vec3 operator-(Vec3 value) noexcept
{
    return {-value.x, -value.y, -value.z};
}

[[nodiscard]] constexpr Vec3 operator*(Vec3 left, Vec3 right) noexcept
{
    return {left.x * right.x, left.y * right.y, left.z * right.z};
}

[[nodiscard]] constexpr Vec3 operator*(Vec3 value, float scalar) noexcept
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

[[nodiscard]] constexpr Vec3 operator*(float scalar, Vec3 value) noexcept
{
    return value * scalar;
}

[[nodiscard]] constexpr Vec3 operator/(Vec3 value, float scalar) noexcept
{
    return {value.x / scalar, value.y / scalar, value.z / scalar};
}

// --- Vec4 arithmetic ---

[[nodiscard]] constexpr Vec4 operator+(Vec4 left, Vec4 right) noexcept
{
    return {left.x + right.x, left.y + right.y, left.z + right.z, left.w + right.w};
}

[[nodiscard]] constexpr Vec4 operator-(Vec4 left, Vec4 right) noexcept
{
    return {left.x - right.x, left.y - right.y, left.z - right.z, left.w - right.w};
}

[[nodiscard]] constexpr Vec4 operator-(Vec4 value) noexcept
{
    return {-value.x, -value.y, -value.z, -value.w};
}

[[nodiscard]] constexpr Vec4 operator*(Vec4 value, float scalar) noexcept
{
    return {value.x * scalar, value.y * scalar, value.z * scalar, value.w * scalar};
}

[[nodiscard]] constexpr Vec4 operator*(float scalar, Vec4 value) noexcept
{
    return value * scalar;
}

// --- Conversions ---

[[nodiscard]] constexpr Vec2 xy(Vec3 value) noexcept
{
    return {value.x, value.y};
}

[[nodiscard]] constexpr Vec3 xyz(Vec4 value) noexcept
{
    return {value.x, value.y, value.z};
}

[[nodiscard]] constexpr Vec3 toVec3(Vec2 value, float z = 0.0F) noexcept
{
    return {value.x, value.y, z};
}

// A point carries w = 1 so a Mat4 translation applies; a direction carries w = 0
// so it does not. Naming them separately keeps that distinction at the call site.
[[nodiscard]] constexpr Vec4 toPoint(Vec3 value) noexcept
{
    return {value.x, value.y, value.z, 1.0F};
}

[[nodiscard]] constexpr Vec4 toDirection(Vec3 value) noexcept
{
    return {value.x, value.y, value.z, 0.0F};
}

// --- Products ---

[[nodiscard]] constexpr float dot(Vec2 left, Vec2 right) noexcept
{
    return left.x * right.x + left.y * right.y;
}

[[nodiscard]] constexpr float dot(Vec3 left, Vec3 right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] constexpr float dot(Vec4 left, Vec4 right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z + left.w * right.w;
}

[[nodiscard]] constexpr Vec3 cross(Vec3 left, Vec3 right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x};
}

// 2D analogue of the cross product: the z component of the 3D cross of two
// vectors in the XY plane. Positive when right is counter-clockwise from left.
[[nodiscard]] constexpr float crossZ(Vec2 left, Vec2 right) noexcept
{
    return left.x * right.y - left.y * right.x;
}

// --- Lengths ---

[[nodiscard]] constexpr float lengthSquared(Vec2 value) noexcept
{
    return dot(value, value);
}

[[nodiscard]] constexpr float lengthSquared(Vec3 value) noexcept
{
    return dot(value, value);
}

[[nodiscard]] constexpr float lengthSquared(Vec4 value) noexcept
{
    return dot(value, value);
}

[[nodiscard]] inline float length(Vec2 value) noexcept
{
    return std::sqrt(lengthSquared(value));
}

[[nodiscard]] inline float length(Vec3 value) noexcept
{
    return std::sqrt(lengthSquared(value));
}

[[nodiscard]] inline float length(Vec4 value) noexcept
{
    return std::sqrt(lengthSquared(value));
}

[[nodiscard]] inline float distance(Vec2 left, Vec2 right) noexcept
{
    return length(right - left);
}

[[nodiscard]] inline float distance(Vec3 left, Vec3 right) noexcept
{
    return length(right - left);
}

[[nodiscard]] inline float distanceSquared(Vec2 left, Vec2 right) noexcept
{
    return lengthSquared(right - left);
}

[[nodiscard]] inline float distanceSquared(Vec3 left, Vec3 right) noexcept
{
    return lengthSquared(right - left);
}

// --- Finiteness ---

[[nodiscard]] inline bool isFinite(Vec2 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] inline bool isFinite(Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] inline bool isFinite(Vec4 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z) && std::isfinite(value.w);
}

// --- Normalization ---
//
// A degenerate or non-finite input returns the zero vector rather than NaN or a
// silently unnormalized value. Callers that must distinguish "already unit" from
// "could not normalize" test the result against zero.
//
// The squared length accumulates in double to survive magnitudes whose float
// square would overflow: a coordinate of 1e20 squares to 1e40, which is +inf in
// float and would reject a perfectly ordinary vector.
//
// The threshold is on the SQUARED length, so any vector shorter than 1e-12 is
// treated as degenerate even though float could still represent its direction.
// That is deliberate: at Tina's scale (meters) such a vector carries no usable
// direction, and the same constant defines the quaternion contract that
// Scene::LocalTransform validation has always used.
inline constexpr double MinimumNormalizableLengthSquared = 1.0e-24;

[[nodiscard]] inline Vec2 normalized(Vec2 value) noexcept
{
    const double squared = static_cast<double>(value.x) * value.x
        + static_cast<double>(value.y) * value.y;
    if (!std::isfinite(squared) || squared <= MinimumNormalizableLengthSquared) {
        return {};
    }
    const float inverseLength = static_cast<float>(1.0 / std::sqrt(squared));
    return {value.x * inverseLength, value.y * inverseLength};
}

[[nodiscard]] inline Vec3 normalized(Vec3 value) noexcept
{
    const double squared = static_cast<double>(value.x) * value.x
        + static_cast<double>(value.y) * value.y
        + static_cast<double>(value.z) * value.z;
    if (!std::isfinite(squared) || squared <= MinimumNormalizableLengthSquared) {
        return {};
    }
    const float inverseLength = static_cast<float>(1.0 / std::sqrt(squared));
    return {value.x * inverseLength, value.y * inverseLength, value.z * inverseLength};
}

// --- Interpolation and component-wise helpers ---

[[nodiscard]] constexpr Vec2 lerp(Vec2 from, Vec2 to, float alpha) noexcept
{
    return from + (to - from) * alpha;
}

[[nodiscard]] constexpr Vec3 lerp(Vec3 from, Vec3 to, float alpha) noexcept
{
    return from + (to - from) * alpha;
}

[[nodiscard]] constexpr Vec2 minimum(Vec2 left, Vec2 right) noexcept
{
    return {(std::min)(left.x, right.x), (std::min)(left.y, right.y)};
}

[[nodiscard]] constexpr Vec3 minimum(Vec3 left, Vec3 right) noexcept
{
    return {(std::min)(left.x, right.x), (std::min)(left.y, right.y), (std::min)(left.z, right.z)};
}

[[nodiscard]] constexpr Vec2 maximum(Vec2 left, Vec2 right) noexcept
{
    return {(std::max)(left.x, right.x), (std::max)(left.y, right.y)};
}

[[nodiscard]] constexpr Vec3 maximum(Vec3 left, Vec3 right) noexcept
{
    return {(std::max)(left.x, right.x), (std::max)(left.y, right.y), (std::max)(left.z, right.z)};
}

[[nodiscard]] inline Vec2 absolute(Vec2 value) noexcept
{
    return {std::abs(value.x), std::abs(value.y)};
}

[[nodiscard]] inline Vec3 absolute(Vec3 value) noexcept
{
    return {std::abs(value.x), std::abs(value.y), std::abs(value.z)};
}

[[nodiscard]] constexpr float largestComponent(Vec3 value) noexcept
{
    return (std::max)({value.x, value.y, value.z});
}

[[nodiscard]] constexpr float smallestComponent(Vec3 value) noexcept
{
    return (std::min)({value.x, value.y, value.z});
}

// True when every component matches within a scale-relative tolerance. Used by
// uniform-scale checks, which cannot use operator== because authored scales
// arrive from float arithmetic.
[[nodiscard]] inline bool approximatelyEqual(
    Vec3 left,
    Vec3 right,
    float relativeEpsilon = DefaultRelativeEpsilon) noexcept
{
    return approximatelyEqual(left.x, right.x, relativeEpsilon)
        && approximatelyEqual(left.y, right.y, relativeEpsilon)
        && approximatelyEqual(left.z, right.z, relativeEpsilon);
}

[[nodiscard]] inline bool approximatelyEqual(
    Vec2 left,
    Vec2 right,
    float relativeEpsilon = DefaultRelativeEpsilon) noexcept
{
    return approximatelyEqual(left.x, right.x, relativeEpsilon)
        && approximatelyEqual(left.y, right.y, relativeEpsilon);
}

} // namespace Tina::Math
