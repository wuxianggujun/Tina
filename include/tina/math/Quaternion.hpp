#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/math/Constants.hpp>
#include <tina/math/Vec.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::Math {

// Unit quaternion in (x, y, z, w) order, w scalar last. Rotations compose
// left-to-right through operator*: (parent * local) applies local first.
struct Quaternion final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;

    friend constexpr bool operator==(const Quaternion&, const Quaternion&) noexcept = default;
};

[[nodiscard]] constexpr Quaternion operator*(Quaternion left, Quaternion right) noexcept
{
    return {
        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z};
}

[[nodiscard]] constexpr Quaternion conjugate(Quaternion value) noexcept
{
    return {-value.x, -value.y, -value.z, value.w};
}

[[nodiscard]] constexpr float lengthSquared(Quaternion value) noexcept
{
    return value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
}

[[nodiscard]] inline bool isFinite(Quaternion value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z) && std::isfinite(value.w);
}

// Returns the zero quaternion — not identity — when the input cannot be
// normalized. Identity would silently turn a degenerate rotation into a valid
// one; zero is representable, detectable, and propagates as invalid.
[[nodiscard]] inline Quaternion normalized(Quaternion value) noexcept
{
    const double squared = static_cast<double>(value.x) * value.x
        + static_cast<double>(value.y) * value.y
        + static_cast<double>(value.z) * value.z
        + static_cast<double>(value.w) * value.w;
    if (!std::isfinite(squared) || squared <= MinimumNormalizableLengthSquared) {
        return {0.0F, 0.0F, 0.0F, 0.0F};
    }
    const float inverseLength = static_cast<float>(1.0 / std::sqrt(squared));
    return {value.x * inverseLength, value.y * inverseLength,
        value.z * inverseLength, value.w * inverseLength};
}

// Inverse of a rotation. For unit input this is the conjugate; the division by
// squared length also covers non-unit input rather than returning a wrong result.
[[nodiscard]] inline Quaternion inverse(Quaternion value) noexcept
{
    const double squared = static_cast<double>(value.x) * value.x
        + static_cast<double>(value.y) * value.y
        + static_cast<double>(value.z) * value.z
        + static_cast<double>(value.w) * value.w;
    if (!std::isfinite(squared) || squared <= MinimumNormalizableLengthSquared) {
        return {0.0F, 0.0F, 0.0F, 0.0F};
    }
    const float inverseSquared = static_cast<float>(1.0 / squared);
    return {-value.x * inverseSquared, -value.y * inverseSquared,
        -value.z * inverseSquared, value.w * inverseSquared};
}

[[nodiscard]] constexpr Vec3 rotate(Quaternion rotation, Vec3 value) noexcept
{
    const Quaternion vectorQuaternion{value.x, value.y, value.z, 0.0F};
    const Quaternion rotated = rotation * vectorQuaternion * conjugate(rotation);
    return {rotated.x, rotated.y, rotated.z};
}

[[nodiscard]] inline bool isIdentity(
    Quaternion value,
    float epsilon = DefaultRelativeEpsilon) noexcept
{
    if (!isFinite(value)) {
        return false;
    }
    value = normalized(value);
    return std::abs(value.x) <= epsilon && std::abs(value.y) <= epsilon
        && std::abs(value.z) <= epsilon && std::abs(std::abs(value.w) - 1.0F) <= epsilon;
}

// axis need not be unit length; it is normalized here. A degenerate axis yields
// the zero quaternion so the caller's validity check rejects it.
[[nodiscard]] inline Quaternion fromAxisAngle(Vec3 axis, float angleRadians) noexcept
{
    const Vec3 unitAxis = normalized(axis);
    if (unitAxis == Vec3{}) {
        return {0.0F, 0.0F, 0.0F, 0.0F};
    }
    const float halfAngle = angleRadians * 0.5F;
    const float sine = std::sin(halfAngle);
    return {unitAxis.x * sine, unitAxis.y * sine, unitAxis.z * sine, std::cos(halfAngle)};
}

// Shortest-path spherical interpolation. Both inputs are normalized first, and a
// negative dot flips `to` so the result never takes the long way around.
//
// Nearly parallel inputs fall back to normalized lerp: the sin(angle) divisor
// below approaches zero there, so slerp loses precision exactly where the linear
// path is already indistinguishable from the arc.
[[nodiscard]] inline Quaternion slerp(Quaternion from, Quaternion to, float alpha) noexcept
{
    from = normalized(from);
    to = normalized(to);
    double cosine = static_cast<double>(from.x) * to.x
        + static_cast<double>(from.y) * to.y
        + static_cast<double>(from.z) * to.z
        + static_cast<double>(from.w) * to.w;
    if (cosine < 0.0) {
        to = {-to.x, -to.y, -to.z, -to.w};
        cosine = -cosine;
    }
    cosine = std::clamp(cosine, 0.0, 1.0);
    if (cosine > 0.9995) {
        return normalized(Quaternion{
            from.x + (to.x - from.x) * alpha,
            from.y + (to.y - from.y) * alpha,
            from.z + (to.z - from.z) * alpha,
            from.w + (to.w - from.w) * alpha,
        });
    }
    const double angle = std::acos(cosine);
    const double sine = std::sin(angle);
    const double fromScale = std::sin((1.0 - static_cast<double>(alpha)) * angle) / sine;
    const double toScale = std::sin(static_cast<double>(alpha) * angle) / sine;
    return normalized(Quaternion{
        static_cast<float>(fromScale * from.x + toScale * to.x),
        static_cast<float>(fromScale * from.y + toScale * to.y),
        static_cast<float>(fromScale * from.z + toScale * to.z),
        static_cast<float>(fromScale * from.w + toScale * to.w),
    });
}

} // namespace Tina::Math
