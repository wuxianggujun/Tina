#pragma once

#include <tina/core/base/Types.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::Scene {

struct Vec3 final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    friend constexpr bool operator==(const Vec3&, const Vec3&) noexcept = default;
};

[[nodiscard]] constexpr Vec3 operator+(Vec3 left, Vec3 right) noexcept
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] constexpr Vec3 operator-(Vec3 left, Vec3 right) noexcept
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] constexpr Vec3 operator*(Vec3 value, float scalar) noexcept
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

[[nodiscard]] constexpr Vec3 operator*(Vec3 left, Vec3 right) noexcept
{
    return {left.x * right.x, left.y * right.y, left.z * right.z};
}

[[nodiscard]] constexpr float dot(Vec3 left, Vec3 right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

struct Quaternion final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;

    friend constexpr bool operator==(const Quaternion&, const Quaternion&) noexcept = default;
};

[[nodiscard]] constexpr Quaternion quaternionMultiply(
    Quaternion left,
    Quaternion right) noexcept
{
    return {
        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z};
}

[[nodiscard]] constexpr Quaternion quaternionConjugate(Quaternion value) noexcept
{
    return {-value.x, -value.y, -value.z, value.w};
}

[[nodiscard]] constexpr Vec3 rotate(Quaternion rotation, Vec3 value) noexcept
{
    const Quaternion vectorQuaternion{value.x, value.y, value.z, 0.0F};
    const Quaternion rotated = quaternionMultiply(
        quaternionMultiply(rotation, vectorQuaternion),
        quaternionConjugate(rotation));
    return {rotated.x, rotated.y, rotated.z};
}

struct LocalTransform final {
    Vec3 position{};
    Quaternion rotation{};
    Vec3 scale{1.0F, 1.0F, 1.0F};

    friend constexpr bool operator==(const LocalTransform&, const LocalTransform&) noexcept = default;
};

struct WorldTransform final {
    Vec3 position{};
    Quaternion rotation{};
    Vec3 scale{1.0F, 1.0F, 1.0F};

    friend constexpr bool operator==(const WorldTransform&, const WorldTransform&) noexcept = default;
};

[[nodiscard]] inline bool isFinite(Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] inline bool isFinite(Quaternion value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] inline bool isFinite(WorldTransform value) noexcept
{
    return isFinite(value.position) && isFinite(value.rotation) && isFinite(value.scale);
}

[[nodiscard]] inline bool isValid(LocalTransform value) noexcept
{
    if (!isFinite(value.position) || !isFinite(value.rotation) || !isFinite(value.scale)) {
        return false;
    }
    const float rotationLengthSquared = dot(
        Vec3{value.rotation.x, value.rotation.y, value.rotation.z},
        Vec3{value.rotation.x, value.rotation.y, value.rotation.z})
        + value.rotation.w * value.rotation.w;
    return rotationLengthSquared > 1.0e-12F && std::isfinite(rotationLengthSquared);
}

[[nodiscard]] inline bool isValid(WorldTransform value) noexcept
{
    if (!isFinite(value)) {
        return false;
    }
    const float rotationLengthSquared = dot(
        Vec3{value.rotation.x, value.rotation.y, value.rotation.z},
        Vec3{value.rotation.x, value.rotation.y, value.rotation.z})
        + value.rotation.w * value.rotation.w;
    return rotationLengthSquared > 1.0e-12F && std::isfinite(rotationLengthSquared);
}

[[nodiscard]] inline Quaternion normalized(Quaternion value) noexcept
{
    const double lengthSquared = static_cast<double>(value.x) * value.x
        + static_cast<double>(value.y) * value.y
        + static_cast<double>(value.z) * value.z
        + static_cast<double>(value.w) * value.w;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-24) {
        return {0.0F, 0.0F, 0.0F, 0.0F};
    }
    const float inverseLength = static_cast<float>(1.0 / std::sqrt(lengthSquared));
    return {value.x * inverseLength, value.y * inverseLength,
        value.z * inverseLength, value.w * inverseLength};
}

[[nodiscard]] inline bool isUniformScale(Vec3 value) noexcept
{
    const float largest = std::max({1.0F, std::abs(value.x), std::abs(value.y), std::abs(value.z)});
    constexpr float RelativeEpsilon = 1.0e-5F;
    return std::abs(value.x - value.y) <= RelativeEpsilon * largest
        && std::abs(value.x - value.z) <= RelativeEpsilon * largest;
}

[[nodiscard]] inline bool isIdentityRotation(Quaternion value) noexcept
{
    if (!isFinite(value)) {
        return false;
    }
    value = normalized(value);
    constexpr float Epsilon = 1.0e-5F;
    return std::abs(value.x) <= Epsilon && std::abs(value.y) <= Epsilon
        && std::abs(value.z) <= Epsilon && std::abs(std::abs(value.w) - 1.0F) <= Epsilon;
}

// A position/quaternion/scale tuple cannot represent shear. Reject the
// ambiguous composition until Scene grows an affine transform representation.
[[nodiscard]] inline bool supportsTrsComposition(
    const WorldTransform& parent,
    const LocalTransform& local) noexcept
{
    return isUniformScale(parent.scale) || isIdentityRotation(local.rotation);
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
        parent.position + rotate(parent.rotation, local.position * parent.scale),
        normalized(quaternionMultiply(parent.rotation, local.rotation)),
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
