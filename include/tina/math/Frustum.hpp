#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/math/Constants.hpp>
#include <tina/math/Geometry3D.hpp>
#include <tina/math/Mat4.hpp>
#include <tina/math/Vec.hpp>

#include <array>
#include <cmath>
#include <optional>

namespace Tina::Math {

enum class FrustumPlane : u8 {
    Left = 0,
    Right = 1,
    Bottom = 2,
    Top = 3,
    Near = 4,
    Far = 5,
};

inline constexpr usize FrustumPlaneCount = 6;

// Six half-spaces with normals pointing INWARD, so a point is inside the frustum
// when its signed distance to every plane is non-negative. Inward normals make the
// culling test a plain sign check with no per-plane negation.
struct Frustum final {
    std::array<Plane, FrustumPlaneCount> planes{};

    [[nodiscard]] constexpr const Plane& plane(FrustumPlane which) const noexcept
    {
        return planes[static_cast<usize>(which)];
    }

    friend constexpr bool operator==(const Frustum&, const Frustum&) noexcept = default;
};

[[nodiscard]] inline bool isFinite(const Frustum& value) noexcept
{
    for (const Plane& plane : value.planes) {
        if (!isFinite(plane)) {
            return false;
        }
    }
    return true;
}

// Builds the frustum of a right-handed perspective camera from its basis directly,
// rather than from a view-projection matrix.
//
// `forward` and `up` need not be exactly orthogonal or unit length; the basis is
// re-orthogonalized here, matching how the render backend derives its own camera
// axes. A degenerate basis or invalid frustum parameter returns nullopt instead of
// a frustum that culls everything or nothing.
[[nodiscard]] inline std::optional<Frustum> frustumFromPerspective(
    Vec3 position,
    Vec3 forward,
    Vec3 up,
    float verticalFovRadians,
    float aspectRatio,
    float nearPlane,
    float farPlane) noexcept
{
    if (!isFinite(position) || !isFinite(forward) || !isFinite(up)
        || !std::isfinite(verticalFovRadians) || !std::isfinite(aspectRatio)
        || !std::isfinite(nearPlane) || !std::isfinite(farPlane)) {
        return std::nullopt;
    }
    if (verticalFovRadians <= 0.0F || verticalFovRadians >= Pi || aspectRatio <= 0.0F
        || nearPlane <= 0.0F || farPlane <= nearPlane) {
        return std::nullopt;
    }
    const Vec3 unitForward = normalized(forward);
    if (unitForward == Vec3{}) {
        return std::nullopt;
    }
    const Vec3 right = normalized(cross(unitForward, up));
    if (right == Vec3{}) {
        return std::nullopt;
    }
    const Vec3 orthogonalUp = normalized(cross(right, unitForward));
    if (orthogonalUp == Vec3{}) {
        return std::nullopt;
    }

    const float tangentY = std::tan(verticalFovRadians * 0.5F);
    if (!std::isfinite(tangentY) || tangentY <= 0.0F) {
        return std::nullopt;
    }
    const float tangentX = tangentY * aspectRatio;

    // Side plane normals: the inward normal of the left plane is the forward axis
    // tilted toward +right by the horizontal half-angle. Expressing it with the
    // tangent and normalizing avoids a trig call per plane.
    const Vec3 leftNormal = normalized(unitForward * tangentX + right);
    const Vec3 rightNormal = normalized(unitForward * tangentX - right);
    const Vec3 bottomNormal = normalized(unitForward * tangentY + orthogonalUp);
    const Vec3 topNormal = normalized(unitForward * tangentY - orthogonalUp);
    if (leftNormal == Vec3{} || rightNormal == Vec3{} || bottomNormal == Vec3{}
        || topNormal == Vec3{}) {
        return std::nullopt;
    }

    Frustum result{};
    result.planes[static_cast<usize>(FrustumPlane::Left)] =
        Plane{leftNormal, -dot(leftNormal, position)};
    result.planes[static_cast<usize>(FrustumPlane::Right)] =
        Plane{rightNormal, -dot(rightNormal, position)};
    result.planes[static_cast<usize>(FrustumPlane::Bottom)] =
        Plane{bottomNormal, -dot(bottomNormal, position)};
    result.planes[static_cast<usize>(FrustumPlane::Top)] =
        Plane{topNormal, -dot(topNormal, position)};
    result.planes[static_cast<usize>(FrustumPlane::Near)] =
        Plane{unitForward, -dot(unitForward, position + unitForward * nearPlane)};
    result.planes[static_cast<usize>(FrustumPlane::Far)] =
        Plane{-unitForward, dot(unitForward, position + unitForward * farPlane)};
    if (!isFinite(result)) {
        return std::nullopt;
    }
    return result;
}

// Extracts the six planes from a combined view-projection matrix (Gribb-Hartmann):
// each clip-space boundary w +/- coordinate >= 0 is a linear form in world space,
// and its coefficients are a sum or difference of two matrix rows.
//
// Independent of the frustum's origin and shape, so it also covers orthographic and
// off-center projections, and it is the cross-check that frustumFromPerspective and
// perspectiveRightHanded agree.
//
// `depthRange` must match the projection that produced the matrix: the near plane
// is row3 + row2 for [-1, 1] clip depth but plain row2 for [0, 1].
[[nodiscard]] inline std::optional<Frustum> frustumFromViewProjection(
    const Mat4& viewProjection,
    ClipDepthRange depthRange) noexcept
{
    if (!isFinite(viewProjection)) {
        return std::nullopt;
    }
    const Vec4 row0 = viewProjection.row(0);
    const Vec4 row1 = viewProjection.row(1);
    const Vec4 row2 = viewProjection.row(2);
    const Vec4 row3 = viewProjection.row(3);

    const std::array<Vec4, FrustumPlaneCount> forms{
        row3 + row0,
        row3 - row0,
        row3 + row1,
        row3 - row1,
        depthRange == ClipDepthRange::NegativeOneToOne ? Vec4{row3 + row2} : row2,
        row3 - row2,
    };

    Frustum result{};
    for (usize index = 0; index < FrustumPlaneCount; ++index) {
        const Plane unnormalized{xyz(forms[index]), forms[index].w};
        const std::optional<Plane> plane = normalizedPlane(unnormalized);
        if (!plane) {
            return std::nullopt;
        }
        result.planes[index] = *plane;
    }
    return result;
}

// Outside when the sphere is fully behind any single plane. Conservative: a sphere
// straddling two planes near a corner can report inside while missing the frustum.
// That is the standard trade — the exact test costs far more than drawing the
// occasional extra item.
[[nodiscard]] constexpr bool intersects(const Frustum& frustum, const Sphere& sphere) noexcept
{
    for (const Plane& plane : frustum.planes) {
        if (signedDistance(plane, sphere.center) < -sphere.radius) {
            return false;
        }
    }
    return true;
}

// Outside when the box's farthest corner along a plane's inward normal is still
// behind that plane. Same conservative corner caveat as the sphere overload.
[[nodiscard]] inline bool intersects(const Frustum& frustum, const Aabb3& box) noexcept
{
    const Vec3 boxCenter = center(box);
    const Vec3 boxHalf = halfExtents(box);
    for (const Plane& plane : frustum.planes) {
        const float projectedRadius = std::abs(plane.normal.x) * boxHalf.x
            + std::abs(plane.normal.y) * boxHalf.y
            + std::abs(plane.normal.z) * boxHalf.z;
        if (signedDistance(plane, boxCenter) + projectedRadius < 0.0F) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr bool contains(const Frustum& frustum, Vec3 point) noexcept
{
    for (const Plane& plane : frustum.planes) {
        if (signedDistance(plane, point) < 0.0F) {
            return false;
        }
    }
    return true;
}

// Sphere-versus-perspective-frustum test taken straight from the camera basis,
// without materializing a Frustum.
//
// Algebraically identical to building the frustum and calling intersects(): the
// left plane's inward normal is normalize(forward * tangentX + right), so dividing
// this comparison through by hypot(tangentX, 1) is exactly that plane's signed
// distance. It exists separately for two reasons that both matter inside a
// per-item extract loop: it accumulates in double where the plane form rounds to
// float, and it normalizes nothing per item.
//
// The field of view is in DEGREES here, unlike frustumFromPerspective. That is not
// an oversight: this overload mirrors the render camera contract, which stores
// degrees, and converting at the call site would round the angle through float
// before the double arithmetic below — enough to flip a sphere sitting exactly on a
// frustum boundary, and enough to change published culling counts.
//
// Callers culling many items against one camera should build a Frustum once
// instead; this overload is for when the camera basis is already at hand per item.
[[nodiscard]] inline bool sphereIntersectsPerspectiveFrustum(
    Vec3 sphereCenter,
    float sphereRadius,
    Vec3 cameraPosition,
    Vec3 cameraForward,
    Vec3 cameraUp,
    float verticalFovDegrees,
    float aspectRatio,
    float nearPlane,
    float farPlane) noexcept
{
    const Vec3 right = cross(cameraForward, cameraUp);
    const Vec3 relative = sphereCenter - cameraPosition;
    const double x = static_cast<double>(dot(relative, right));
    const double y = static_cast<double>(dot(relative, cameraUp));
    const double depth = static_cast<double>(dot(relative, cameraForward));
    const double radius = static_cast<double>(sphereRadius);

    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(depth)
        || !std::isfinite(radius) || depth + radius < nearPlane
        || depth - radius > farPlane) {
        return false;
    }

    // Half of (degrees * pi / 180) folded into a single constant, so the angle
    // never passes through float on its way to std::tan.
    const double tangentY =
        std::tan(static_cast<double>(verticalFovDegrees) * 3.14159265358979323846 / 360.0);
    const double tangentX = tangentY * static_cast<double>(aspectRatio);
    const double horizontalRadiusScale = std::hypot(tangentX, 1.0);
    const double verticalRadiusScale = std::hypot(tangentY, 1.0);
    return depth * tangentX + x >= -radius * horizontalRadiusScale
        && depth * tangentX - x >= -radius * horizontalRadiusScale
        && depth * tangentY + y >= -radius * verticalRadiusScale
        && depth * tangentY - y >= -radius * verticalRadiusScale;
}

} // namespace Tina::Math
