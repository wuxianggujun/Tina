#include "BgfxSpotLightShadowMath.hpp"

#include <tina/render/RenderErrors.hpp>

#include <bx/math.h>

#include <cmath>

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
        return {0.0F, 0.0F, 0.0F};
    }
    return bx::mul(value, 1.0F / std::sqrt(lengthSquared));
}

[[nodiscard]] bool validInput(const BgfxSpotLightShadowInput& input) noexcept
{
    const Mesh3DSpotLight& light = input.light;
    return std::isfinite(light.positionX) && std::isfinite(light.positionY) &&
           std::isfinite(light.positionZ) && std::isfinite(light.influenceRadius) &&
           light.influenceRadius > 0.0F &&
           std::isfinite(light.directionFromLightX) &&
           std::isfinite(light.directionFromLightY) &&
           std::isfinite(light.directionFromLightZ) &&
           std::isfinite(light.outerConeCosine) &&
           light.outerConeCosine > 0.0F && light.outerConeCosine < 1.0F &&
           std::isfinite(input.nearPlaneMeters) && input.nearPlaneMeters > 0.0F &&
           input.nearPlaneMeters < light.influenceRadius;
}

} // namespace

Core::Result<BgfxSpotLightShadowProjection>
computeSpotLightShadowProjection(
    const BgfxSpotLightShadowInput& input,
    bool homogeneousDepth,
    bool originBottomLeft) noexcept
{
    if (!validInput(input))
    {
        return Core::failure(
            RenderErrorCode::InvalidMesh3DLighting,
            "Spot-light shadow projection requires a finite position, positive range, near plane inside the range, and an outer half-angle below 90 degrees");
    }

    const bx::Vec3 direction = normalizeOrZero({
        input.light.directionFromLightX,
        input.light.directionFromLightY,
        input.light.directionFromLightZ,
    });
    if (!finiteVector(direction) || bx::dot(direction, direction) <= 0.0F)
    {
        return Core::failure(
            RenderErrorCode::InvalidMesh3DLighting,
            "Spot-light shadow projection requires a non-degenerate light direction");
    }

    const float fieldOfViewDegrees =
        2.0F * std::acos(input.light.outerConeCosine) * 180.0F / Pi;
    if (!std::isfinite(fieldOfViewDegrees) || fieldOfViewDegrees <= 0.0F ||
        fieldOfViewDegrees >= 180.0F)
    {
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Spot-light shadow projection field of view is invalid");
    }

    BgfxSpotLightShadowProjection projection{
        .fieldOfViewDegrees = fieldOfViewDegrees,
        .nearPlaneMeters = input.nearPlaneMeters,
        .farPlaneMeters = input.light.influenceRadius,
    };
    const bx::Vec3 eye{
        input.light.positionX,
        input.light.positionY,
        input.light.positionZ,
    };
    const bx::Vec3 target = bx::add(eye, direction);
    const bx::Vec3 viewUp =
        std::abs(bx::dot(direction, bx::Vec3{0.0F, 1.0F, 0.0F})) < 0.98F
            ? bx::Vec3{0.0F, 1.0F, 0.0F}
            : bx::Vec3{1.0F, 0.0F, 0.0F};
    bx::mtxLookAt(projection.lightView.data(), eye, target, viewUp,
                  bx::Handedness::Right);
    bx::mtxProj(projection.lightProjection.data(), fieldOfViewDegrees, 1.0F,
                input.nearPlaneMeters, input.light.influenceRadius,
                homogeneousDepth, bx::Handedness::Right);

    const float yScale = originBottomLeft ? 0.5F : -0.5F;
    const float depthScale = homogeneousDepth ? 0.5F : 1.0F;
    const float depthOffset = homogeneousDepth ? 0.5F : 0.0F;
    const float crop[16]{
        0.5F, 0.0F, 0.0F, 0.0F,
        0.0F, yScale, 0.0F, 0.0F,
        0.0F, 0.0F, depthScale, 0.0F,
        0.5F, 0.5F, depthOffset, 1.0F,
    };
    float projectionCrop[16]{};
    bx::mtxMul(projectionCrop, projection.lightProjection.data(), crop);
    bx::mtxMul(projection.samplingTransform.data(), projection.lightView.data(),
               projectionCrop);
    return projection;
}

} // namespace Tina::Render::Bgfx
