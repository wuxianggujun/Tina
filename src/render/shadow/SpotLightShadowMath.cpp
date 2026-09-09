#include "SpotLightShadowMath.hpp"
#include "ShadowProjectionMath.hpp"

#include <tina/render/RenderErrors.hpp>


#include <cmath>
#include <utility>

namespace Tina::Render::Shadow {
namespace {

constexpr float Pi = 3.14159265358979323846F;

[[nodiscard]] bool validInput(const SpotLightShadowInput& input) noexcept
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

Core::Result<SpotLightShadowProjection>
computeSpotLightShadowProjection(
    const SpotLightShadowInput& input,
    bool homogeneousDepth,
    bool originBottomLeft) noexcept
{
    if (!validInput(input))
    {
        return Core::failure(
            RenderErrorCode::InvalidMesh3DLighting,
            "Spot-light shadow projection requires a finite position, positive range, near plane inside the range, and an outer half-angle below 90 degrees");
    }

    const Math::Vec3 direction = Detail::normalizeShadowAxis({
        input.light.directionFromLightX,
        input.light.directionFromLightY,
        input.light.directionFromLightZ,
    });
    if (!Math::isFinite(direction) || Math::dot(direction, direction) <= 0.0F)
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

    SpotLightShadowProjection projection{
        .fieldOfViewDegrees = fieldOfViewDegrees,
        .nearPlaneMeters = input.nearPlaneMeters,
        .farPlaneMeters = input.light.influenceRadius,
    };
    const Math::Vec3 eye{
        input.light.positionX,
        input.light.positionY,
        input.light.positionZ,
    };
    const Math::Vec3 viewUp = std::abs(direction.y) < 0.98F
        ? Math::Vec3{0.0F, 1.0F, 0.0F} : Math::Vec3{1.0F, 0.0F, 0.0F};
    const auto lightView = Math::lookAtRightHanded(eye, eye + direction, viewUp);
    const auto lightProjection = Math::perspectiveRightHanded(
        fieldOfViewDegrees * (Pi / 180.0F), 1.0F, input.nearPlaneMeters,
        input.light.influenceRadius, Detail::depthRange(homogeneousDepth));
    if (!lightView || !lightProjection)
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Spot-light shadow view/projection is degenerate");
    auto sampling = Detail::samplingTransform(*lightView, *lightProjection,
        homogeneousDepth, originBottomLeft, 1.0F, {0.5F, 0.5F});
    if (!sampling) return Core::failure(std::move(sampling.error()));
    projection.lightView = lightView->columns;
    projection.lightProjection = lightProjection->columns;
    projection.samplingTransform = *sampling;
    return projection;
}

} // namespace Tina::Render::Shadow
