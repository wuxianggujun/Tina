#include "BgfxPointLightShadowMath.hpp"

#include <tina/render/RenderErrors.hpp>

#include <bx/math.h>

#include <array>
#include <cmath>

namespace Tina::Render::Bgfx {
namespace {

struct FaceBasis final {
    bx::Vec3 direction;
    bx::Vec3 up;
};

constexpr std::array<FaceBasis, BgfxPointLightShadowFaceCount> FaceBases{
    FaceBasis{.direction = {1.0F, 0.0F, 0.0F}, .up = {0.0F, -1.0F, 0.0F}},
    FaceBasis{.direction = {-1.0F, 0.0F, 0.0F}, .up = {0.0F, -1.0F, 0.0F}},
    FaceBasis{.direction = {0.0F, 1.0F, 0.0F}, .up = {0.0F, 0.0F, 1.0F}},
    FaceBasis{.direction = {0.0F, -1.0F, 0.0F}, .up = {0.0F, 0.0F, -1.0F}},
    FaceBasis{.direction = {0.0F, 0.0F, 1.0F}, .up = {0.0F, -1.0F, 0.0F}},
    FaceBasis{.direction = {0.0F, 0.0F, -1.0F}, .up = {0.0F, -1.0F, 0.0F}},
};

[[nodiscard]] bool validInput(const BgfxPointLightShadowInput& input) noexcept
{
    const Mesh3DPointLight& light = input.light;
    return std::isfinite(light.positionX) && std::isfinite(light.positionY) &&
           std::isfinite(light.positionZ) && std::isfinite(light.influenceRadius) &&
           light.influenceRadius > 0.0F && std::isfinite(input.nearPlaneMeters) &&
           input.nearPlaneMeters > 0.0F && input.nearPlaneMeters < light.influenceRadius;
}

} // namespace

Core::Result<BgfxPointLightShadowProjection>
computePointLightShadowProjection(
    const BgfxPointLightShadowInput& input,
    bool homogeneousDepth,
    bool originBottomLeft) noexcept
{
    if (!validInput(input))
    {
        return Core::failure(
            RenderErrorCode::InvalidMesh3DLighting,
            "Point-light shadow projection requires a finite position, positive range, and near plane inside the range");
    }

    BgfxPointLightShadowProjection projection{
        .nearPlaneMeters = input.nearPlaneMeters,
        .farPlaneMeters = input.light.influenceRadius,
    };
    const bx::Vec3 eye{input.light.positionX, input.light.positionY, input.light.positionZ};
    const float yScale = originBottomLeft ? 0.5F : -0.5F;
    const float depthScale = homogeneousDepth ? 0.5F : 1.0F;
    const float depthOffset = homogeneousDepth ? 0.5F : 0.0F;
    const float crop[16]{
        0.5F, 0.0F, 0.0F, 0.0F,
        0.0F, yScale, 0.0F, 0.0F,
        0.0F, 0.0F, depthScale, 0.0F,
        0.5F, 0.5F, depthOffset, 1.0F,
    };

    for (usize faceIndex = 0; faceIndex < projection.faces.size(); ++faceIndex)
    {
        BgfxPointLightShadowFace& face = projection.faces[faceIndex];
        const FaceBasis& basis = FaceBases[faceIndex];
        bx::mtxLookAt(face.lightView.data(), eye, bx::add(eye, basis.direction), basis.up,
                      bx::Handedness::Right);
        bx::mtxProj(face.lightProjection.data(), 90.0F, 1.0F, input.nearPlaneMeters,
                    input.light.influenceRadius, homogeneousDepth, bx::Handedness::Right);
        float projectionCrop[16]{};
        bx::mtxMul(projectionCrop, face.lightProjection.data(), crop);
        bx::mtxMul(face.samplingTransform.data(), face.lightView.data(), projectionCrop);
    }
    return projection;
}

} // namespace Tina::Render::Bgfx
