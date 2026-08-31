#pragma once

#include <tina/core/base/Types.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::Math {

inline constexpr float Pi = 3.14159265358979323846F;
inline constexpr float TwoPi = 6.28318530717958647692F;
inline constexpr float HalfPi = 1.57079632679489661923F;
inline constexpr float DegreesToRadians = Pi / 180.0F;
inline constexpr float RadiansToDegrees = 180.0F / Pi;

// Relative tolerance used by approximatelyEqual and the degeneracy rejections in
// this module. Chosen to match the 1.0e-5F epsilon Scene transforms already use.
inline constexpr float DefaultRelativeEpsilon = 1.0e-5F;

[[nodiscard]] constexpr float radians(float degreeValue) noexcept
{
    return degreeValue * DegreesToRadians;
}

[[nodiscard]] constexpr float degrees(float radianValue) noexcept
{
    return radianValue * RadiansToDegrees;
}

// Scale-relative comparison. Absolute epsilon alone rejects large coordinates
// that differ only in their last representable bits; relative alone rejects
// values near zero. The larger of 1 and the operand magnitudes covers both.
[[nodiscard]] inline bool approximatelyEqual(
    float left,
    float right,
    float relativeEpsilon = DefaultRelativeEpsilon) noexcept
{
    if (!std::isfinite(left) || !std::isfinite(right)) {
        return false;
    }
    const float largest = std::max({1.0F, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= relativeEpsilon * largest;
}

} // namespace Tina::Math
