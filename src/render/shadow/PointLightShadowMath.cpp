#include "PointLightShadowMath.hpp"
#include "ShadowProjectionMath.hpp"

#include <tina/render/RenderErrors.hpp>


#include <array>
#include <cmath>
#include <utility>

namespace Tina::Render::Shadow {
namespace {

struct FaceBasis final {
    Math::Vec3 direction;
    Math::Vec3 up;
};

constexpr std::array<FaceBasis, PointLightShadowFaceCount> FaceBases{
    FaceBasis{.direction = {1.0F, 0.0F, 0.0F}, .up = {0.0F, -1.0F, 0.0F}},
    FaceBasis{.direction = {-1.0F, 0.0F, 0.0F}, .up = {0.0F, -1.0F, 0.0F}},
    FaceBasis{.direction = {0.0F, 1.0F, 0.0F}, .up = {0.0F, 0.0F, 1.0F}},
    FaceBasis{.direction = {0.0F, -1.0F, 0.0F}, .up = {0.0F, 0.0F, -1.0F}},
    FaceBasis{.direction = {0.0F, 0.0F, 1.0F}, .up = {0.0F, -1.0F, 0.0F}},
    FaceBasis{.direction = {0.0F, 0.0F, -1.0F}, .up = {0.0F, -1.0F, 0.0F}},
};

[[nodiscard]] bool validInput(const PointLightShadowInput& input) noexcept
{
    const Mesh3DPointLight& light = input.light;
    return std::isfinite(light.positionX) && std::isfinite(light.positionY) &&
           std::isfinite(light.positionZ) && std::isfinite(light.influenceRadius) &&
           light.influenceRadius > 0.0F && std::isfinite(input.nearPlaneMeters) &&
           input.nearPlaneMeters > 0.0F && input.nearPlaneMeters < light.influenceRadius;
}

} // namespace

Core::Result<PointLightShadowProjection>
computePointLightShadowProjection(
    const PointLightShadowInput& input,
    bool homogeneousDepth,
    bool originBottomLeft) noexcept
{
    if (!validInput(input))
    {
        return Core::failure(
            RenderErrorCode::InvalidMesh3DLighting,
            "Point-light shadow projection requires a finite position, positive range, and near plane inside the range");
    }

    PointLightShadowProjection projection{
        .nearPlaneMeters = input.nearPlaneMeters,
        .farPlaneMeters = input.light.influenceRadius,
    };
    const Math::Vec3 eye{input.light.positionX, input.light.positionY, input.light.positionZ};
    const auto lightProjection = Math::perspectiveRightHanded(
        Math::Pi * 0.5F, 1.0F, input.nearPlaneMeters, input.light.influenceRadius,
        Detail::depthRange(homogeneousDepth));
    if (!lightProjection)
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Point-light shadow projection is not finite");

    for (usize faceIndex = 0; faceIndex < projection.faces.size(); ++faceIndex)
    {
        PointLightShadowFace& face = projection.faces[faceIndex];
        const FaceBasis& basis = FaceBases[faceIndex];
        const auto lightView = Math::lookAtRightHanded(eye, eye + basis.direction, basis.up);
        if (!lightView)
            return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                                 "Point-light shadow view is degenerate");
        auto sampling = Detail::samplingTransform(*lightView, *lightProjection,
            homogeneousDepth, originBottomLeft, 1.0F, {0.5F, 0.5F});
        if (!sampling) return Core::failure(std::move(sampling.error()));
        face.lightView = lightView->columns;
        face.lightProjection = lightProjection->columns;
        face.samplingTransform = *sampling;
    }
    return projection;
}

} // namespace Tina::Render::Shadow
