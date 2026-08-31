#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/math/Quaternion.hpp>
#include <tina/math/Vec.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::Scene {

struct LocalTransform final {
    Math::Vec3 position{};
    Math::Quaternion rotation{};
    Math::Vec3 scale{1.0F, 1.0F, 1.0F};

    friend constexpr bool operator==(const LocalTransform&, const LocalTransform&) noexcept = default;
};

struct WorldTransform final {
    Math::Vec3 position{};
    Math::Quaternion rotation{};
    Math::Vec3 scale{1.0F, 1.0F, 1.0F};

    friend constexpr bool operator==(const WorldTransform&, const WorldTransform&) noexcept = default;
};

[[nodiscard]] inline bool isFinite(const WorldTransform& value) noexcept
{
    return Math::isFinite(value.position) && Math::isFinite(value.rotation)
        && Math::isFinite(value.scale);
}

[[nodiscard]] inline bool isFinite(const LocalTransform& value) noexcept
{
    return Math::isFinite(value.position) && Math::isFinite(value.rotation)
        && Math::isFinite(value.scale);
}

[[nodiscard]] inline bool isValid(const LocalTransform& value) noexcept
{
    if (!isFinite(value)) {
        return false;
    }
    const float rotationLengthSquared = Math::lengthSquared(value.rotation);
    return rotationLengthSquared > 1.0e-12F && std::isfinite(rotationLengthSquared);
}

[[nodiscard]] inline bool isValid(const WorldTransform& value) noexcept
{
    if (!isFinite(value)) {
        return false;
    }
    const float rotationLengthSquared = Math::lengthSquared(value.rotation);
    return rotationLengthSquared > 1.0e-12F && std::isfinite(rotationLengthSquared);
}

[[nodiscard]] inline bool isUniformScale(Math::Vec3 value) noexcept
{
    const float largest = std::max({1.0F, std::abs(value.x), std::abs(value.y), std::abs(value.z)});
    constexpr float RelativeEpsilon = 1.0e-5F;
    return std::abs(value.x - value.y) <= RelativeEpsilon * largest
        && std::abs(value.x - value.z) <= RelativeEpsilon * largest;
}

// A position/quaternion/scale tuple cannot represent shear. Reject the
// ambiguous composition until Scene grows an affine transform representation.
[[nodiscard]] inline bool supportsTrsComposition(
    const WorldTransform& parent,
    const LocalTransform& local) noexcept
{
    return isUniformScale(parent.scale) || Math::isIdentity(local.rotation);
}

[[nodiscard]] inline bool tryCompose(
    const WorldTransform& parent,
    const LocalTransform& local,
    WorldTransform& output) noexcept
{
    if (!isValid(parent) || !isValid(local) || !supportsTrsComposition(parent, local)) {
        return false;
    }
    const WorldTransform candidate{
        parent.position + Math::rotate(parent.rotation, local.position * parent.scale),
        Math::normalized(parent.rotation * local.rotation),
        parent.scale * local.scale};
    if (!isValid(candidate)) {
        return false;
    }
    output = candidate;
    return true;
}

[[nodiscard]] inline WorldTransform toWorld(LocalTransform local) noexcept
{
    return {local.position, local.rotation, local.scale};
}

} // namespace Tina::Scene
