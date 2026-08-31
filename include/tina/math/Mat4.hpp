#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/math/Constants.hpp>
#include <tina/math/Quaternion.hpp>
#include <tina/math/Vec.hpp>

#include <array>
#include <cmath>
#include <optional>

namespace Tina::Math {

// 4x4 affine/projective transform in COLUMN-MAJOR storage: element(row, column)
// lives at columns[column * 4 + row]. Translation therefore occupies indices
// 12..14, and the buffer can be handed to the render backend unchanged.
//
// This layout is not a preference. RenderMesh3DItem::columnMajorWorldTransform is
// already column-major and reaches the backend verbatim, so a second convention
// here would mean a transpose at every boundary and a class of bugs that only
// shows up as mirrored geometry.
//
// The coordinate system is right-handed, matching every projection this module
// produces and the backend's own view/projection setup.
struct Mat4 final {
    std::array<float, 16> columns{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};

    [[nodiscard]] constexpr float at(usize row, usize column) const noexcept
    {
        return columns[column * 4U + row];
    }

    [[nodiscard]] constexpr float& at(usize row, usize column) noexcept
    {
        return columns[column * 4U + row];
    }

    [[nodiscard]] constexpr Vec4 column(usize index) const noexcept
    {
        return {
            columns[index * 4U + 0U],
            columns[index * 4U + 1U],
            columns[index * 4U + 2U],
            columns[index * 4U + 3U]};
    }

    [[nodiscard]] constexpr Vec4 row(usize index) const noexcept
    {
        return {
            columns[0U * 4U + index],
            columns[1U * 4U + index],
            columns[2U * 4U + index],
            columns[3U * 4U + index]};
    }

    // Upper-left 3x3 basis columns. Named for the transform meaning rather than
    // the index so call sites read as intent.
    [[nodiscard]] constexpr Vec3 basisX() const noexcept
    {
        return {columns[0], columns[1], columns[2]};
    }

    [[nodiscard]] constexpr Vec3 basisY() const noexcept
    {
        return {columns[4], columns[5], columns[6]};
    }

    [[nodiscard]] constexpr Vec3 basisZ() const noexcept
    {
        return {columns[8], columns[9], columns[10]};
    }

    [[nodiscard]] constexpr Vec3 translation() const noexcept
    {
        return {columns[12], columns[13], columns[14]};
    }

    friend constexpr bool operator==(const Mat4&, const Mat4&) noexcept = default;
};

[[nodiscard]] constexpr Mat4 identityMat4() noexcept
{
    return Mat4{};
}

[[nodiscard]] inline bool isFinite(const Mat4& value) noexcept
{
    for (const float element : value.columns) {
        if (!std::isfinite(element)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr Mat4 translationMat4(Vec3 offset) noexcept
{
    Mat4 result{};
    result.columns[12] = offset.x;
    result.columns[13] = offset.y;
    result.columns[14] = offset.z;
    return result;
}

[[nodiscard]] constexpr Mat4 scaleMat4(Vec3 scale) noexcept
{
    Mat4 result{};
    result.columns[0] = scale.x;
    result.columns[5] = scale.y;
    result.columns[10] = scale.z;
    return result;
}

// Rotation basis of a unit quaternion. Non-unit input is used as given; callers
// that need a pure rotation normalize first (fromTrs does).
[[nodiscard]] constexpr Mat4 fromQuaternion(Quaternion rotation) noexcept
{
    const float xx = rotation.x * rotation.x;
    const float yy = rotation.y * rotation.y;
    const float zz = rotation.z * rotation.z;
    const float xy = rotation.x * rotation.y;
    const float xz = rotation.x * rotation.z;
    const float yz = rotation.y * rotation.z;
    const float wx = rotation.w * rotation.x;
    const float wy = rotation.w * rotation.y;
    const float wz = rotation.w * rotation.z;

    Mat4 result{};
    result.columns[0] = 1.0F - 2.0F * (yy + zz);
    result.columns[1] = 2.0F * (xy + wz);
    result.columns[2] = 2.0F * (xz - wy);
    result.columns[4] = 2.0F * (xy - wz);
    result.columns[5] = 1.0F - 2.0F * (xx + zz);
    result.columns[6] = 2.0F * (yz + wx);
    result.columns[8] = 2.0F * (xz + wy);
    result.columns[9] = 2.0F * (yz - wx);
    result.columns[10] = 1.0F - 2.0F * (xx + yy);
    return result;
}

// Translate * Rotate * Scale, the order Scene transforms compose in. Scale is
// applied first (innermost), so each basis column is the rotation column scaled
// by that axis. This reproduces the arithmetic the render scene builder used
// before this module existed, element for element.
[[nodiscard]] constexpr Mat4 fromTrs(Vec3 translation, Quaternion rotation, Vec3 scale) noexcept
{
    const float xx = rotation.x * rotation.x;
    const float yy = rotation.y * rotation.y;
    const float zz = rotation.z * rotation.z;
    const float xy = rotation.x * rotation.y;
    const float xz = rotation.x * rotation.z;
    const float yz = rotation.y * rotation.z;
    const float wx = rotation.w * rotation.x;
    const float wy = rotation.w * rotation.y;
    const float wz = rotation.w * rotation.z;

    return Mat4{{
        (1.0F - 2.0F * (yy + zz)) * scale.x,
        (2.0F * (xy + wz)) * scale.x,
        (2.0F * (xz - wy)) * scale.x,
        0.0F,
        (2.0F * (xy - wz)) * scale.y,
        (1.0F - 2.0F * (xx + zz)) * scale.y,
        (2.0F * (yz + wx)) * scale.y,
        0.0F,
        (2.0F * (xz + wy)) * scale.z,
        (2.0F * (yz - wx)) * scale.z,
        (1.0F - 2.0F * (xx + yy)) * scale.z,
        0.0F,
        translation.x,
        translation.y,
        translation.z,
        1.0F,
    }};
}

// Accumulates each element in double before rounding once. Chained joint and
// cascade matrices otherwise lose precision fast enough to show up as visible
// drift in a skinned pose.
[[nodiscard]] inline Mat4 multiply(const Mat4& left, const Mat4& right) noexcept
{
    Mat4 result{};
    for (usize column = 0; column < 4U; ++column) {
        for (usize row = 0; row < 4U; ++row) {
            double value = 0.0;
            for (usize element = 0; element < 4U; ++element) {
                value += static_cast<double>(left.columns[element * 4U + row])
                    * static_cast<double>(right.columns[column * 4U + element]);
            }
            result.columns[column * 4U + row] = static_cast<float>(value);
        }
    }
    return result;
}

[[nodiscard]] inline Mat4 operator*(const Mat4& left, const Mat4& right) noexcept
{
    return multiply(left, right);
}

// Applies rotation, scale and translation. Assumes an affine matrix; the
// projective w row is ignored, which is why projections use transformVec4.
[[nodiscard]] constexpr Vec3 transformPoint(const Mat4& transform, Vec3 point) noexcept
{
    return {
        point.x * transform.columns[0] + point.y * transform.columns[4]
            + point.z * transform.columns[8] + transform.columns[12],
        point.x * transform.columns[1] + point.y * transform.columns[5]
            + point.z * transform.columns[9] + transform.columns[13],
        point.x * transform.columns[2] + point.y * transform.columns[6]
            + point.z * transform.columns[10] + transform.columns[14]};
}

// Applies rotation and scale but not translation. Correct for directions only
// under uniform scale; non-uniform scale needs the inverse transpose for normals.
[[nodiscard]] constexpr Vec3 transformDirection(const Mat4& transform, Vec3 direction) noexcept
{
    return {
        direction.x * transform.columns[0] + direction.y * transform.columns[4]
            + direction.z * transform.columns[8],
        direction.x * transform.columns[1] + direction.y * transform.columns[5]
            + direction.z * transform.columns[9],
        direction.x * transform.columns[2] + direction.y * transform.columns[6]
            + direction.z * transform.columns[10]};
}

// Full projective transform, w included. Callers divide by w themselves so a
// point on or behind the near plane (w <= 0) stays detectable instead of being
// turned into a plausible-looking coordinate.
[[nodiscard]] constexpr Vec4 transformVec4(const Mat4& transform, Vec4 value) noexcept
{
    return {
        value.x * transform.columns[0] + value.y * transform.columns[4]
            + value.z * transform.columns[8] + value.w * transform.columns[12],
        value.x * transform.columns[1] + value.y * transform.columns[5]
            + value.z * transform.columns[9] + value.w * transform.columns[13],
        value.x * transform.columns[2] + value.y * transform.columns[6]
            + value.z * transform.columns[10] + value.w * transform.columns[14],
        value.x * transform.columns[3] + value.y * transform.columns[7]
            + value.z * transform.columns[11] + value.w * transform.columns[15]};
}

[[nodiscard]] constexpr Mat4 transpose(const Mat4& value) noexcept
{
    Mat4 result{};
    for (usize column = 0; column < 4U; ++column) {
        for (usize row = 0; row < 4U; ++row) {
            result.columns[row * 4U + column] = value.columns[column * 4U + row];
        }
    }
    return result;
}

// Determinant of the upper-left 3x3 basis. This is the value that decides whether
// a transform is invertible and whether it flips handedness (negative), which is
// what geometry and tangent-space code actually needs.
[[nodiscard]] constexpr float linearDeterminant(const Mat4& value) noexcept
{
    return dot(value.basisX(), cross(value.basisY(), value.basisZ()));
}

namespace Detail {

// Determinant of the 3x3 matrix left after deleting `skipRow` and `skipColumn`.
// Written as an explicit loop over the surviving indices rather than an unrolled
// index table: the table form is where a transposed or sign-flipped inverse hides,
// and it cannot be checked by reading it.
[[nodiscard]] inline double mat4Minor(
    const Mat4& value,
    usize skipRow,
    usize skipColumn) noexcept
{
    std::array<std::array<double, 3>, 3> minor{};
    usize targetRow = 0;
    for (usize row = 0; row < 4U; ++row) {
        if (row == skipRow) {
            continue;
        }
        usize targetColumn = 0;
        for (usize column = 0; column < 4U; ++column) {
            if (column == skipColumn) {
                continue;
            }
            minor[targetRow][targetColumn] =
                static_cast<double>(value.columns[column * 4U + row]);
            ++targetColumn;
        }
        ++targetRow;
    }
    return minor[0][0] * (minor[1][1] * minor[2][2] - minor[1][2] * minor[2][1])
        - minor[0][1] * (minor[1][0] * minor[2][2] - minor[1][2] * minor[2][0])
        + minor[0][2] * (minor[1][0] * minor[2][1] - minor[1][1] * minor[2][0]);
}

[[nodiscard]] inline double mat4Cofactor(
    const Mat4& value,
    usize row,
    usize column) noexcept
{
    const double minor = mat4Minor(value, row, column);
    return ((row + column) % 2U == 0U) ? minor : -minor;
}

} // namespace Detail

// Full 4x4 determinant, expanded along the first row.
[[nodiscard]] inline double determinant(const Mat4& value) noexcept
{
    double result = 0.0;
    for (usize column = 0; column < 4U; ++column) {
        result += static_cast<double>(value.columns[column * 4U])
            * Detail::mat4Cofactor(value, 0U, column);
    }
    return result;
}

// General 4x4 inverse, so it covers projections and not only affine transforms.
//
// Returns nullopt for a singular or non-finite matrix rather than producing
// infinities: an "inverse" full of NaN silently corrupts everything derived from
// it, and the caller has no way to notice.
//
// inverse = adjugate / determinant, where adjugate is the TRANSPOSE of the
// cofactor matrix — hence cofactor(column, row) when writing element(row, column).
[[nodiscard]] inline std::optional<Mat4> inverse(const Mat4& value) noexcept
{
    if (!isFinite(value)) {
        return std::nullopt;
    }
    const double determinantValue = determinant(value);
    if (!std::isfinite(determinantValue) || determinantValue == 0.0) {
        return std::nullopt;
    }
    const double inverseDeterminant = 1.0 / determinantValue;

    Mat4 result{};
    for (usize column = 0; column < 4U; ++column) {
        for (usize row = 0; row < 4U; ++row) {
            const double scaled =
                Detail::mat4Cofactor(value, column, row) * inverseDeterminant;
            if (!std::isfinite(scaled)) {
                return std::nullopt;
            }
            result.columns[column * 4U + row] = static_cast<float>(scaled);
        }
    }
    return result;
}

// --- View and projection ---
//
// Clip-space depth range is a device property, not a preference: OpenGL-family
// APIs use [-1, 1] and D3D/Metal/Vulkan use [0, 1]. The render backend already
// reads this from device capabilities and threads it through its shadow math, so
// these builders take it as a parameter instead of hardcoding one and silently
// producing a wrong depth range on half the platforms.
enum class ClipDepthRange : u8 {
    // [0, 1]. D3D11/D3D12, Metal, Vulkan.
    ZeroToOne,
    // [-1, 1]. OpenGL, OpenGL ES.
    NegativeOneToOne,
};

// Right-handed look-at view matrix. The view axis points from target to eye, so
// the camera looks down -Z in view space.
//
// A degenerate basis (eye == target, or up parallel to the view axis) returns
// nullopt rather than an arbitrary fallback orientation: a shadow cascade or
// camera silently snapping to some default axis is far harder to diagnose than a
// rejected build.
[[nodiscard]] inline std::optional<Mat4> lookAtRightHanded(
    Vec3 eye,
    Vec3 target,
    Vec3 up) noexcept
{
    if (!isFinite(eye) || !isFinite(target) || !isFinite(up)) {
        return std::nullopt;
    }
    const Vec3 view = normalized(eye - target);
    if (view == Vec3{}) {
        return std::nullopt;
    }
    const Vec3 right = normalized(cross(up, view));
    if (right == Vec3{}) {
        return std::nullopt;
    }
    const Vec3 orthogonalUp = cross(view, right);

    Mat4 result{};
    result.columns[0] = right.x;
    result.columns[1] = orthogonalUp.x;
    result.columns[2] = view.x;
    result.columns[3] = 0.0F;
    result.columns[4] = right.y;
    result.columns[5] = orthogonalUp.y;
    result.columns[6] = view.y;
    result.columns[7] = 0.0F;
    result.columns[8] = right.z;
    result.columns[9] = orthogonalUp.z;
    result.columns[10] = view.z;
    result.columns[11] = 0.0F;
    result.columns[12] = -dot(right, eye);
    result.columns[13] = -dot(orthogonalUp, eye);
    result.columns[14] = -dot(view, eye);
    result.columns[15] = 1.0F;
    if (!isFinite(result)) {
        return std::nullopt;
    }
    return result;
}

// Right-handed perspective projection from a vertical field of view.
[[nodiscard]] inline std::optional<Mat4> perspectiveRightHanded(
    float verticalFovRadians,
    float aspectRatio,
    float nearPlane,
    float farPlane,
    ClipDepthRange depthRange) noexcept
{
    if (!std::isfinite(verticalFovRadians) || !std::isfinite(aspectRatio)
        || !std::isfinite(nearPlane) || !std::isfinite(farPlane)) {
        return std::nullopt;
    }
    if (verticalFovRadians <= 0.0F || verticalFovRadians >= Pi || aspectRatio <= 0.0F
        || nearPlane <= 0.0F || farPlane <= nearPlane) {
        return std::nullopt;
    }
    const float tangent = std::tan(verticalFovRadians * 0.5F);
    if (!std::isfinite(tangent) || tangent <= 0.0F) {
        return std::nullopt;
    }
    const float height = 1.0F / tangent;
    const float width = height / aspectRatio;
    const float difference = farPlane - nearPlane;
    const bool homogeneous = depthRange == ClipDepthRange::NegativeOneToOne;
    const float aa = homogeneous ? (farPlane + nearPlane) / difference : farPlane / difference;
    const float bb =
        homogeneous ? (2.0F * farPlane * nearPlane) / difference : nearPlane * aa;

    Mat4 result{};
    result.columns.fill(0.0F);
    result.columns[0] = width;
    result.columns[5] = height;
    result.columns[10] = -aa;
    result.columns[11] = -1.0F;
    result.columns[14] = -bb;
    if (!isFinite(result)) {
        return std::nullopt;
    }
    return result;
}

// Right-handed orthographic projection.
[[nodiscard]] inline std::optional<Mat4> orthographicRightHanded(
    float left,
    float right,
    float bottom,
    float top,
    float nearPlane,
    float farPlane,
    ClipDepthRange depthRange) noexcept
{
    if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(bottom)
        || !std::isfinite(top) || !std::isfinite(nearPlane) || !std::isfinite(farPlane)) {
        return std::nullopt;
    }
    if (right == left || top == bottom || farPlane == nearPlane) {
        return std::nullopt;
    }
    const bool homogeneous = depthRange == ClipDepthRange::NegativeOneToOne;
    const float aa = 2.0F / (right - left);
    const float bb = 2.0F / (top - bottom);
    const float cc = (homogeneous ? 2.0F : 1.0F) / (farPlane - nearPlane);
    const float dd = (left + right) / (left - right);
    const float ee = (top + bottom) / (bottom - top);
    const float ff = homogeneous ? (nearPlane + farPlane) / (nearPlane - farPlane)
                                 : nearPlane / (nearPlane - farPlane);

    Mat4 result{};
    result.columns.fill(0.0F);
    result.columns[0] = aa;
    result.columns[5] = bb;
    result.columns[10] = -cc;
    result.columns[12] = dd;
    result.columns[13] = ee;
    result.columns[14] = ff;
    result.columns[15] = 1.0F;
    if (!isFinite(result)) {
        return std::nullopt;
    }
    return result;
}

} // namespace Tina::Math
