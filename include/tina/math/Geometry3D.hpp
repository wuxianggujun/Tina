#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/math/Constants.hpp>
#include <tina/math/Mat4.hpp>
#include <tina/math/Quaternion.hpp>
#include <tina/math/Vec.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace Tina::Math {

struct Aabb3 final {
    Vec3 lower{};
    Vec3 upper{};

    friend constexpr bool operator==(const Aabb3&, const Aabb3&) noexcept = default;
};

struct Sphere final {
    Vec3 center{};
    float radius = 0.0F;

    friend constexpr bool operator==(const Sphere&, const Sphere&) noexcept = default;
};

// Plane in constant-normal form: dot(normal, point) + distance == 0.
//
// `distance` is the signed distance from the origin along -normal, so a point's
// signed distance is a single dot product plus an add. `normal` is expected unit
// length; normalizedPlane() enforces that and the builders below produce it.
struct Plane final {
    Vec3 normal{0.0F, 1.0F, 0.0F};
    float distance = 0.0F;

    friend constexpr bool operator==(const Plane&, const Plane&) noexcept = default;
};

// Half-line from `origin` along `direction`. `direction` is expected unit length
// so hit distances come out in world units; makeRay() enforces that.
struct Ray final {
    Vec3 origin{};
    Vec3 direction{0.0F, 0.0F, -1.0F};

    friend constexpr bool operator==(const Ray&, const Ray&) noexcept = default;
};

struct RayHit final {
    // Distance along the ray direction, in world units for a unit direction.
    float distance = 0.0F;
    Vec3 point{};
    Vec3 normal{};

    friend constexpr bool operator==(const RayHit&, const RayHit&) noexcept = default;
};

// --- Aabb3 ---

[[nodiscard]] inline bool isFinite(const Aabb3& value) noexcept
{
    return isFinite(value.lower) && isFinite(value.upper);
}

[[nodiscard]] inline bool isValid(const Aabb3& value) noexcept
{
    return isFinite(value) && value.lower.x <= value.upper.x
        && value.lower.y <= value.upper.y && value.lower.z <= value.upper.z;
}

[[nodiscard]] constexpr Vec3 center(const Aabb3& value) noexcept
{
    return (value.lower + value.upper) * 0.5F;
}

[[nodiscard]] constexpr Vec3 extents(const Aabb3& value) noexcept
{
    return value.upper - value.lower;
}

[[nodiscard]] constexpr Vec3 halfExtents(const Aabb3& value) noexcept
{
    return extents(value) * 0.5F;
}

[[nodiscard]] constexpr Aabb3 fromCenterHalfExtents(Vec3 centerPoint, Vec3 half) noexcept
{
    return {centerPoint - half, centerPoint + half};
}

[[nodiscard]] constexpr Aabb3 fromPoints(Vec3 first, Vec3 second) noexcept
{
    return {minimum(first, second), maximum(first, second)};
}

// The identity for merge/expand: an inverted box that any real point widens into
// a correct bound. Starting from a default-constructed Aabb3 instead would wrongly
// include the origin.
[[nodiscard]] inline Aabb3 emptyAabb3() noexcept
{
    constexpr float Largest = 3.402823466e+38F;
    return {{Largest, Largest, Largest}, {-Largest, -Largest, -Largest}};
}

[[nodiscard]] constexpr bool contains(const Aabb3& value, Vec3 point) noexcept
{
    return point.x >= value.lower.x && point.x <= value.upper.x
        && point.y >= value.lower.y && point.y <= value.upper.y
        && point.z >= value.lower.z && point.z <= value.upper.z;
}

[[nodiscard]] constexpr bool contains(const Aabb3& outer, const Aabb3& inner) noexcept
{
    return inner.lower.x >= outer.lower.x && inner.upper.x <= outer.upper.x
        && inner.lower.y >= outer.lower.y && inner.upper.y <= outer.upper.y
        && inner.lower.z >= outer.lower.z && inner.upper.z <= outer.upper.z;
}

[[nodiscard]] constexpr bool intersects(const Aabb3& left, const Aabb3& right) noexcept
{
    return left.lower.x <= right.upper.x && left.upper.x >= right.lower.x
        && left.lower.y <= right.upper.y && left.upper.y >= right.lower.y
        && left.lower.z <= right.upper.z && left.upper.z >= right.lower.z;
}

[[nodiscard]] constexpr Aabb3 merge(const Aabb3& left, const Aabb3& right) noexcept
{
    return {minimum(left.lower, right.lower), maximum(left.upper, right.upper)};
}

[[nodiscard]] constexpr Aabb3 expand(const Aabb3& value, Vec3 point) noexcept
{
    return {minimum(value.lower, point), maximum(value.upper, point)};
}

[[nodiscard]] constexpr Aabb3 expand(const Aabb3& value, float margin) noexcept
{
    const Vec3 offset{margin, margin, margin};
    return {value.lower - offset, value.upper + offset};
}

[[nodiscard]] constexpr Vec3 clamped(const Aabb3& value, Vec3 point) noexcept
{
    return maximum(value.lower, minimum(value.upper, point));
}

[[nodiscard]] constexpr std::array<Vec3, 8> corners(const Aabb3& value) noexcept
{
    return {
        Vec3{value.lower.x, value.lower.y, value.lower.z},
        Vec3{value.upper.x, value.lower.y, value.lower.z},
        Vec3{value.lower.x, value.upper.y, value.lower.z},
        Vec3{value.upper.x, value.upper.y, value.lower.z},
        Vec3{value.lower.x, value.lower.y, value.upper.z},
        Vec3{value.upper.x, value.lower.y, value.upper.z},
        Vec3{value.lower.x, value.upper.y, value.upper.z},
        Vec3{value.upper.x, value.upper.y, value.upper.z}};
}

// Conservative bounds of the box after an arbitrary transform. Transforms all
// eight corners and re-bounds them, which stays correct under shear and
// projection; the |M| * halfExtents shortcut only holds for affine transforms.
[[nodiscard]] inline Aabb3 transformed(const Aabb3& value, const Mat4& transform) noexcept
{
    Aabb3 result = emptyAabb3();
    for (const Vec3& corner : corners(value)) {
        result = expand(result, transformPoint(transform, corner));
    }
    return result;
}

// --- Sphere ---

[[nodiscard]] inline bool isFinite(const Sphere& value) noexcept
{
    return isFinite(value.center) && std::isfinite(value.radius);
}

[[nodiscard]] inline bool isValid(const Sphere& value) noexcept
{
    return isFinite(value) && value.radius >= 0.0F;
}

[[nodiscard]] constexpr bool contains(const Sphere& value, Vec3 point) noexcept
{
    return lengthSquared(point - value.center) <= value.radius * value.radius;
}

[[nodiscard]] constexpr bool intersects(const Sphere& left, const Sphere& right) noexcept
{
    const float combined = left.radius + right.radius;
    return lengthSquared(right.center - left.center) <= combined * combined;
}

// Closest-point test: the nearest point of the box to the center is within radius.
[[nodiscard]] constexpr bool intersects(const Sphere& sphere, const Aabb3& box) noexcept
{
    const Vec3 closest = clamped(box, sphere.center);
    return lengthSquared(closest - sphere.center) <= sphere.radius * sphere.radius;
}

[[nodiscard]] constexpr bool intersects(const Aabb3& box, const Sphere& sphere) noexcept
{
    return intersects(sphere, box);
}

[[nodiscard]] constexpr Aabb3 bounds(const Sphere& value) noexcept
{
    const Vec3 offset{value.radius, value.radius, value.radius};
    return {value.center - offset, value.center + offset};
}

// Transformed bounding sphere. The radius scales by the largest axis scale, which
// is conservative under non-uniform scale — the exact result would no longer be a
// sphere. Rejects a non-finite or mirrored transform rather than inventing a
// negative radius.
[[nodiscard]] inline std::optional<Sphere> transformed(
    const Sphere& value,
    Vec3 translation,
    Quaternion rotation,
    Vec3 scale) noexcept
{
    if (!isValid(value) || !isFinite(translation) || !isFinite(rotation) || !isFinite(scale)) {
        return std::nullopt;
    }
    if (scale.x <= 0.0F || scale.y <= 0.0F || scale.z <= 0.0F) {
        return std::nullopt;
    }
    const Quaternion unitRotation = normalized(rotation);
    if (unitRotation == Quaternion{0.0F, 0.0F, 0.0F, 0.0F}) {
        return std::nullopt;
    }
    const Sphere result{
        translation + rotate(unitRotation, value.center * scale),
        value.radius * largestComponent(scale)};
    if (!isValid(result)) {
        return std::nullopt;
    }
    return result;
}

// --- Plane ---

[[nodiscard]] inline bool isFinite(const Plane& value) noexcept
{
    return isFinite(value.normal) && std::isfinite(value.distance);
}

[[nodiscard]] inline std::optional<Plane> normalizedPlane(const Plane& value) noexcept
{
    if (!isFinite(value)) {
        return std::nullopt;
    }
    const double squared = static_cast<double>(value.normal.x) * value.normal.x
        + static_cast<double>(value.normal.y) * value.normal.y
        + static_cast<double>(value.normal.z) * value.normal.z;
    if (!std::isfinite(squared) || squared <= MinimumNormalizableLengthSquared) {
        return std::nullopt;
    }
    const float inverseLength = static_cast<float>(1.0 / std::sqrt(squared));
    return Plane{value.normal * inverseLength, value.distance * inverseLength};
}

[[nodiscard]] inline std::optional<Plane> planeFromPointNormal(Vec3 point, Vec3 normal) noexcept
{
    const Vec3 unitNormal = normalized(normal);
    if (unitNormal == Vec3{}) {
        return std::nullopt;
    }
    return Plane{unitNormal, -dot(unitNormal, point)};
}

// Counter-clockwise winding produces a normal toward the viewer, matching the
// right-handed convention used everywhere else in this module.
[[nodiscard]] inline std::optional<Plane> planeFromPoints(Vec3 a, Vec3 b, Vec3 c) noexcept
{
    return planeFromPointNormal(a, cross(b - a, c - a));
}

// Positive in front of the plane (the normal's side), negative behind.
[[nodiscard]] constexpr float signedDistance(const Plane& plane, Vec3 point) noexcept
{
    return dot(plane.normal, point) + plane.distance;
}

[[nodiscard]] constexpr Vec3 projectOnto(const Plane& plane, Vec3 point) noexcept
{
    return point - plane.normal * signedDistance(plane, point);
}

// Signed distance from the plane to the closest and farthest points of the box.
// Frustum culling needs both: the near value decides intersection, the far value
// decides full containment, and computing them together costs one pass.
[[nodiscard]] inline float nearestSignedDistance(const Plane& plane, const Aabb3& box) noexcept
{
    const Vec3 boxCenter = center(box);
    const Vec3 boxHalf = halfExtents(box);
    const float projectedRadius = std::abs(plane.normal.x) * boxHalf.x
        + std::abs(plane.normal.y) * boxHalf.y
        + std::abs(plane.normal.z) * boxHalf.z;
    return signedDistance(plane, boxCenter) - projectedRadius;
}

// --- Ray ---

[[nodiscard]] inline bool isFinite(const Ray& value) noexcept
{
    return isFinite(value.origin) && isFinite(value.direction);
}

// Normalizes the direction so hit distances are in world units. Rejects a
// degenerate direction rather than returning a ray that reports every point as a
// hit at distance zero.
[[nodiscard]] inline std::optional<Ray> makeRay(Vec3 origin, Vec3 direction) noexcept
{
    if (!isFinite(origin)) {
        return std::nullopt;
    }
    const Vec3 unitDirection = normalized(direction);
    if (unitDirection == Vec3{}) {
        return std::nullopt;
    }
    return Ray{origin, unitDirection};
}

[[nodiscard]] constexpr Vec3 pointAt(const Ray& ray, float distance) noexcept
{
    return ray.origin + ray.direction * distance;
}

// Slab test. Returns the entry point; a ray starting inside the box reports
// distance 0 at its own origin.
//
// Division by a zero direction component is deliberate and correct here: IEEE
// gives +/-inf, and the min/max comparisons then reject or accept that axis
// exactly as an explicit parallel-axis branch would, without the branch.
[[nodiscard]] inline std::optional<RayHit> raycast(
    const Ray& ray,
    const Aabb3& box,
    float maximumDistance = 3.402823466e+38F) noexcept
{
    if (!isFinite(ray) || !isValid(box) || !(maximumDistance >= 0.0F)) {
        return std::nullopt;
    }
    float nearest = 0.0F;
    float farthest = maximumDistance;
    usize hitAxis = 0;
    bool hitOnUpperSide = false;

    const std::array<float, 3> origin{ray.origin.x, ray.origin.y, ray.origin.z};
    const std::array<float, 3> direction{ray.direction.x, ray.direction.y, ray.direction.z};
    const std::array<float, 3> lower{box.lower.x, box.lower.y, box.lower.z};
    const std::array<float, 3> upper{box.upper.x, box.upper.y, box.upper.z};

    for (usize axis = 0; axis < 3U; ++axis) {
        const float inverseDirection = 1.0F / direction[axis];
        float entry = (lower[axis] - origin[axis]) * inverseDirection;
        float exit = (upper[axis] - origin[axis]) * inverseDirection;
        bool upperSide = false;
        if (entry > exit) {
            std::swap(entry, exit);
            upperSide = true;
        }
        if (std::isnan(entry) || std::isnan(exit)) {
            // Origin exactly on a slab boundary with zero direction: 0 * inf.
            // Treat the axis as parallel and inside, matching the containment test.
            if (origin[axis] < lower[axis] || origin[axis] > upper[axis]) {
                return std::nullopt;
            }
            continue;
        }
        if (entry > nearest) {
            nearest = entry;
            hitAxis = axis;
            hitOnUpperSide = upperSide;
        }
        farthest = (std::min)(farthest, exit);
        if (nearest > farthest) {
            return std::nullopt;
        }
    }

    Vec3 normal{};
    const float sign = hitOnUpperSide ? 1.0F : -1.0F;
    if (hitAxis == 0U) {
        normal = {sign, 0.0F, 0.0F};
    } else if (hitAxis == 1U) {
        normal = {0.0F, sign, 0.0F};
    } else {
        normal = {0.0F, 0.0F, sign};
    }
    return RayHit{nearest, pointAt(ray, nearest), normal};
}

// Nearest intersection with the sphere surface. A ray starting inside reports the
// exit point, whose normal faces inward; that is the honest answer rather than
// silently claiming no hit.
[[nodiscard]] inline std::optional<RayHit> raycast(
    const Ray& ray,
    const Sphere& sphere,
    float maximumDistance = 3.402823466e+38F) noexcept
{
    if (!isFinite(ray) || !isValid(sphere) || !(maximumDistance >= 0.0F)) {
        return std::nullopt;
    }
    const Vec3 toCenter = ray.origin - sphere.center;
    const double b = static_cast<double>(dot(toCenter, ray.direction));
    const double c = static_cast<double>(lengthSquared(toCenter))
        - static_cast<double>(sphere.radius) * sphere.radius;
    const double discriminant = b * b - c;
    if (!std::isfinite(discriminant) || discriminant < 0.0) {
        return std::nullopt;
    }
    const double root = std::sqrt(discriminant);
    double distance = -b - root;
    if (distance < 0.0) {
        distance = -b + root;
    }
    if (distance < 0.0 || distance > static_cast<double>(maximumDistance)) {
        return std::nullopt;
    }
    const float hitDistance = static_cast<float>(distance);
    const Vec3 point = pointAt(ray, hitDistance);
    return RayHit{hitDistance, point, normalized(point - sphere.center)};
}

// Front and back faces both count. A ray parallel to the plane misses even when
// it lies exactly in the plane: there is no single intersection point to report.
[[nodiscard]] inline std::optional<RayHit> raycast(
    const Ray& ray,
    const Plane& plane,
    float maximumDistance = 3.402823466e+38F) noexcept
{
    if (!isFinite(ray) || !isFinite(plane) || !(maximumDistance >= 0.0F)) {
        return std::nullopt;
    }
    const float denominator = dot(plane.normal, ray.direction);
    if (std::abs(denominator) <= 1.0e-12F) {
        return std::nullopt;
    }
    const float distance = -signedDistance(plane, ray.origin) / denominator;
    if (!std::isfinite(distance) || distance < 0.0F || distance > maximumDistance) {
        return std::nullopt;
    }
    const Vec3 facingNormal = denominator < 0.0F ? plane.normal : -plane.normal;
    return RayHit{distance, pointAt(ray, distance), facingNormal};
}

} // namespace Tina::Math
