#include "BgfxDirectionalShadowMath.hpp"

#include <tina/render/RenderErrors.hpp>

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Tina::Render::Bgfx {
namespace {

constexpr float Pi = 3.14159265358979323846F;

[[nodiscard]] bool finiteVector(const bx::Vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bx::Vec3 normalizeOrZero(const bx::Vec3& value) noexcept
{
    const float lengthSquared = bx::dot(value, value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12F)
    {
        return bx::Vec3{0.0F, 0.0F, 0.0F};
    }
    return bx::mul(value, 1.0F / std::sqrt(lengthSquared));
}

[[nodiscard]] bx::Vec3 transformPoint(const float matrix[16], const bx::Vec3& point) noexcept
{
    return bx::Vec3{
        point.x * matrix[0] + point.y * matrix[4] + point.z * matrix[8] + matrix[12],
        point.x * matrix[1] + point.y * matrix[5] + point.z * matrix[9] + matrix[13],
        point.x * matrix[2] + point.y * matrix[6] + point.z * matrix[10] + matrix[14],
    };
}

} // namespace

Core::Result<BgfxDirectionalShadowBounds>
computeDirectionalShadowBounds(const BgfxDirectionalShadowBoundsInput& input) noexcept
{
    const RenderPerspectiveCamera& camera = input.camera;
    if (!std::isfinite(camera.positionX) || !std::isfinite(camera.positionY) ||
        !std::isfinite(camera.positionZ) || !std::isfinite(camera.forwardX) ||
        !std::isfinite(camera.forwardY) || !std::isfinite(camera.forwardZ) || !std::isfinite(camera.upX) ||
        !std::isfinite(camera.upY) || !std::isfinite(camera.upZ) || !std::isfinite(camera.verticalFovDegrees) ||
        camera.verticalFovDegrees <= 0.0F || camera.verticalFovDegrees >= 179.0F ||
        !std::isfinite(camera.nearPlaneMeters) || !std::isfinite(camera.farPlaneMeters) ||
        camera.nearPlaneMeters <= 0.0F || camera.farPlaneMeters <= camera.nearPlaneMeters ||
        !std::isfinite(camera.aspectRatio) || camera.aspectRatio <= 0.0F ||
        !std::isfinite(input.shadowDistance) || input.shadowDistance <= camera.nearPlaneMeters ||
        !std::isfinite(input.depthPadding) || input.depthPadding < 0.0F)
    {
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Directional shadow bounds require a finite camera and positive range");
    }

    const bx::Vec3 forward = normalizeOrZero({camera.forwardX, camera.forwardY, camera.forwardZ});
    const bx::Vec3 requestedUp = normalizeOrZero({camera.upX, camera.upY, camera.upZ});
    const bx::Vec3 right = normalizeOrZero(bx::cross(forward, requestedUp));
    const bx::Vec3 up = normalizeOrZero(bx::cross(right, forward));
    const bx::Vec3 towardLight = normalizeOrZero(
        {input.light.directionTowardLightX, input.light.directionTowardLightY,
         input.light.directionTowardLightZ});
    if (!finiteVector(forward) || !finiteVector(right) || !finiteVector(up) || !finiteVector(towardLight) ||
        bx::dot(forward, forward) <= 0.0F || bx::dot(right, right) <= 0.0F || bx::dot(up, up) <= 0.0F ||
        bx::dot(towardLight, towardLight) <= 0.0F)
    {
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Directional shadow bounds require non-degenerate camera/light axes");
    }

    const float farDistance = (std::min)(camera.farPlaneMeters, input.shadowDistance);
    const float tangent = std::tan(camera.verticalFovDegrees * Pi / 360.0F);
    if (!std::isfinite(tangent) || tangent <= 0.0F)
    {
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Directional shadow camera field of view is invalid");
    }

    const bx::Vec3 cameraPosition{camera.positionX, camera.positionY, camera.positionZ};
    const bx::Vec3 nearCenter = bx::add(cameraPosition, bx::mul(forward, camera.nearPlaneMeters));
    const bx::Vec3 farCenter = bx::add(cameraPosition, bx::mul(forward, farDistance));
    const float nearHalfHeight = camera.nearPlaneMeters * tangent;
    const float farHalfHeight = farDistance * tangent;
    const float nearHalfWidth = nearHalfHeight * camera.aspectRatio;
    const float farHalfWidth = farHalfHeight * camera.aspectRatio;
    const std::array<bx::Vec3, 8> corners{
        bx::add(bx::add(nearCenter, bx::mul(right, -nearHalfWidth)), bx::mul(up, -nearHalfHeight)),
        bx::add(bx::add(nearCenter, bx::mul(right, nearHalfWidth)), bx::mul(up, -nearHalfHeight)),
        bx::add(bx::add(nearCenter, bx::mul(right, -nearHalfWidth)), bx::mul(up, nearHalfHeight)),
        bx::add(bx::add(nearCenter, bx::mul(right, nearHalfWidth)), bx::mul(up, nearHalfHeight)),
        bx::add(bx::add(farCenter, bx::mul(right, -farHalfWidth)), bx::mul(up, -farHalfHeight)),
        bx::add(bx::add(farCenter, bx::mul(right, farHalfWidth)), bx::mul(up, -farHalfHeight)),
        bx::add(bx::add(farCenter, bx::mul(right, -farHalfWidth)), bx::mul(up, farHalfHeight)),
        bx::add(bx::add(farCenter, bx::mul(right, farHalfWidth)), bx::mul(up, farHalfHeight)),
    };

    const bx::Vec3 center = bx::mul(bx::add(nearCenter, farCenter), 0.5F);
    const bx::Vec3 eye = bx::add(center, bx::mul(towardLight, farDistance));
    const bx::Vec3 viewUp = std::abs(bx::dot(towardLight, bx::Vec3{0.0F, 1.0F, 0.0F})) < 0.98F
                                ? bx::Vec3{0.0F, 1.0F, 0.0F}
                                : bx::Vec3{1.0F, 0.0F, 0.0F};
    float view[16]{};
    bx::mtxLookAt(view, eye, center, viewUp, bx::Handedness::Right);

    BgfxDirectionalShadowBounds bounds{
        .minX = (std::numeric_limits<float>::max)(),
        .maxX = -(std::numeric_limits<float>::max)(),
        .minY = (std::numeric_limits<float>::max)(),
        .maxY = -(std::numeric_limits<float>::max)(),
        .minZ = (std::numeric_limits<float>::max)(),
        .maxZ = -(std::numeric_limits<float>::max)(),
    };
    for (const bx::Vec3& corner : corners)
    {
        const bx::Vec3 lightSpace = transformPoint(view, corner);
        if (!finiteVector(lightSpace))
        {
            return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                                 "Directional shadow view transform produced non-finite bounds");
        }
        bounds.minX = (std::min)(bounds.minX, lightSpace.x);
        bounds.maxX = (std::max)(bounds.maxX, lightSpace.x);
        bounds.minY = (std::min)(bounds.minY, lightSpace.y);
        bounds.maxY = (std::max)(bounds.maxY, lightSpace.y);
        bounds.minZ = (std::min)(bounds.minZ, lightSpace.z);
        bounds.maxZ = (std::max)(bounds.maxZ, lightSpace.z);
    }
    bounds.minZ -= input.depthPadding;
    bounds.maxZ += input.depthPadding;
    if (!(bounds.width() > 1.0e-5F) || !(bounds.height() > 1.0e-5F) || !(bounds.depth() > 1.0e-5F))
    {
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Directional shadow bounds are degenerate");
    }
    return bounds;
}

} // namespace Tina::Render::Bgfx
