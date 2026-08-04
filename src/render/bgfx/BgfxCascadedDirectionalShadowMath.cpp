#include "BgfxCascadedDirectionalShadowMath.hpp"

#include <tina/render/RenderErrors.hpp>

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

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

[[nodiscard]] bool validCamera(const RenderPerspectiveCamera& camera) noexcept
{
    return std::isfinite(camera.positionX) && std::isfinite(camera.positionY) &&
           std::isfinite(camera.positionZ) && std::isfinite(camera.forwardX) &&
           std::isfinite(camera.forwardY) && std::isfinite(camera.forwardZ) &&
           std::isfinite(camera.upX) && std::isfinite(camera.upY) &&
           std::isfinite(camera.upZ) && std::isfinite(camera.verticalFovDegrees) &&
           camera.verticalFovDegrees > 0.0F && camera.verticalFovDegrees < 179.0F &&
           std::isfinite(camera.nearPlaneMeters) && std::isfinite(camera.farPlaneMeters) &&
           camera.nearPlaneMeters > 0.0F && camera.farPlaneMeters > camera.nearPlaneMeters &&
           std::isfinite(camera.aspectRatio) && camera.aspectRatio > 0.0F;
}

[[nodiscard]] Core::Result<BgfxCascadedDirectionalShadowCascade>
computeCascade(const BgfxCascadedDirectionalShadowInput& input,
               float nearDepthMeters,
               float farDepthMeters,
               usize cascadeIndex,
               bool homogeneousDepth,
               bool originBottomLeft) noexcept
{
    const RenderPerspectiveCamera& camera = input.camera;
    const bx::Vec3 forward = normalizeOrZero(
        {camera.forwardX, camera.forwardY, camera.forwardZ});
    const bx::Vec3 requestedUp = normalizeOrZero({camera.upX, camera.upY, camera.upZ});
    const bx::Vec3 right = normalizeOrZero(bx::cross(forward, requestedUp));
    const bx::Vec3 up = normalizeOrZero(bx::cross(right, forward));
    const bx::Vec3 towardLight = normalizeOrZero(
        {input.light.directionTowardLightX, input.light.directionTowardLightY,
         input.light.directionTowardLightZ});
    if (!finiteVector(forward) || !finiteVector(right) || !finiteVector(up) ||
        !finiteVector(towardLight) || bx::dot(forward, forward) <= 0.0F ||
        bx::dot(right, right) <= 0.0F || bx::dot(up, up) <= 0.0F ||
        bx::dot(towardLight, towardLight) <= 0.0F)
    {
        return Core::failure(
            RenderErrorCode::InvalidMesh3DLighting,
            "Cascaded directional shadow projection requires non-degenerate camera/light axes");
    }

    const float tangent = std::tan(camera.verticalFovDegrees * Pi / 360.0F);
    if (!std::isfinite(tangent) || tangent <= 0.0F)
    {
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Cascaded directional shadow camera field of view is invalid");
    }

    const bx::Vec3 cameraPosition{camera.positionX, camera.positionY, camera.positionZ};
    const bx::Vec3 nearCenter = bx::add(cameraPosition, bx::mul(forward, nearDepthMeters));
    const bx::Vec3 farCenter = bx::add(cameraPosition, bx::mul(forward, farDepthMeters));
    const float nearHalfHeight = nearDepthMeters * tangent;
    const float farHalfHeight = farDepthMeters * tangent;
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

    BgfxCascadedDirectionalShadowCascade cascade{
        .bounds = {
            .minX = (std::numeric_limits<float>::max)(),
            .maxX = -(std::numeric_limits<float>::max)(),
            .minY = (std::numeric_limits<float>::max)(),
            .maxY = -(std::numeric_limits<float>::max)(),
            .minZ = (std::numeric_limits<float>::max)(),
            .maxZ = -(std::numeric_limits<float>::max)(),
        },
        .nearDepthMeters = nearDepthMeters,
        .farDepthMeters = farDepthMeters,
    };
    const bx::Vec3 center = bx::mul(bx::add(nearCenter, farCenter), 0.5F);
    const bx::Vec3 eye = bx::add(center, bx::mul(towardLight, input.maximumDistanceMeters));
    const bx::Vec3 viewUp =
        std::abs(bx::dot(towardLight, bx::Vec3{0.0F, 1.0F, 0.0F})) < 0.98F
            ? bx::Vec3{0.0F, 1.0F, 0.0F}
            : bx::Vec3{1.0F, 0.0F, 0.0F};
    bx::mtxLookAt(cascade.lightView.data(), eye, center, viewUp, bx::Handedness::Right);

    for (const bx::Vec3& corner : corners)
    {
        const bx::Vec3 lightSpace = transformPoint(cascade.lightView.data(), corner);
        if (!finiteVector(lightSpace))
        {
            return Core::failure(
                RenderErrorCode::InvalidMesh3DLighting,
                "Cascaded directional shadow view transform produced non-finite bounds");
        }
        cascade.bounds.minX = (std::min)(cascade.bounds.minX, lightSpace.x);
        cascade.bounds.maxX = (std::max)(cascade.bounds.maxX, lightSpace.x);
        cascade.bounds.minY = (std::min)(cascade.bounds.minY, lightSpace.y);
        cascade.bounds.maxY = (std::max)(cascade.bounds.maxY, lightSpace.y);
        cascade.bounds.minZ = (std::min)(cascade.bounds.minZ, lightSpace.z);
        cascade.bounds.maxZ = (std::max)(cascade.bounds.maxZ, lightSpace.z);
    }
    cascade.bounds.minZ -= input.depthPaddingMeters;
    cascade.bounds.maxZ += input.depthPaddingMeters;
    if (!(cascade.bounds.width() > 1.0e-5F) || !(cascade.bounds.height() > 1.0e-5F) ||
        !(cascade.bounds.depth() > 1.0e-5F))
    {
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Cascaded directional shadow bounds are degenerate");
    }

    bx::mtxOrtho(cascade.lightProjection.data(), cascade.bounds.minX,
                 cascade.bounds.maxX, cascade.bounds.minY, cascade.bounds.maxY,
                 -cascade.bounds.maxZ, -cascade.bounds.minZ, 0.0F,
                 homogeneousDepth, bx::Handedness::Right);

    constexpr float TileScale = 0.5F;
    const float tileCenterX = (cascadeIndex % 2U == 0U) ? 0.25F : 0.75F;
    const float tileCenterY = (cascadeIndex < 2U) ? 0.25F : 0.75F;
    const float yScale = originBottomLeft ? 0.5F * TileScale : -0.5F * TileScale;
    const float depthScale = homogeneousDepth ? 0.5F : 1.0F;
    const float depthOffset = homogeneousDepth ? 0.5F : 0.0F;
    const float crop[16]{
        0.5F * TileScale, 0.0F, 0.0F, 0.0F,
        0.0F, yScale, 0.0F, 0.0F,
        0.0F, 0.0F, depthScale, 0.0F,
        tileCenterX, tileCenterY, depthOffset, 1.0F,
    };
    float projectionCrop[16]{};
    bx::mtxMul(projectionCrop, cascade.lightProjection.data(), crop);
    bx::mtxMul(cascade.samplingTransform.data(), cascade.lightView.data(), projectionCrop);
    return cascade;
}

} // namespace

Core::Result<std::array<float, BgfxCascadedDirectionalShadowCascadeCount>>
computeCascadedDirectionalShadowSplitDepths(float nearDepthMeters,
                                            float farDepthMeters) noexcept
{
    if (!std::isfinite(nearDepthMeters) || !std::isfinite(farDepthMeters) ||
        nearDepthMeters <= 0.0F || farDepthMeters <= nearDepthMeters)
    {
        return Core::failure(
            RenderErrorCode::InvalidMesh3DLighting,
            "Cascaded directional shadow splits require a finite positive depth range");
    }

    std::array<float, BgfxCascadedDirectionalShadowCascadeCount> splits{};
    const float depthRatio = farDepthMeters / nearDepthMeters;
    for (usize cascadeIndex = 0; cascadeIndex < splits.size(); ++cascadeIndex)
    {
        const float partition = static_cast<float>(cascadeIndex + 1U) /
                                static_cast<float>(splits.size());
        const float logarithmic = nearDepthMeters * std::pow(depthRatio, partition);
        const float uniform = nearDepthMeters + (farDepthMeters - nearDepthMeters) * partition;
        splits[cascadeIndex] = BgfxCascadedDirectionalShadowSplitLambda * logarithmic +
                               (1.0F - BgfxCascadedDirectionalShadowSplitLambda) * uniform;
    }
    splits.back() = farDepthMeters;
    return splits;
}

Core::Result<BgfxCascadedDirectionalShadowProjection>
computeCascadedDirectionalShadowProjection(
    const BgfxCascadedDirectionalShadowInput& input,
    bool homogeneousDepth,
    bool originBottomLeft) noexcept
{
    if (!validCamera(input.camera) || !std::isfinite(input.maximumDistanceMeters) ||
        input.maximumDistanceMeters <= input.camera.nearPlaneMeters ||
        !std::isfinite(input.depthPaddingMeters) || input.depthPaddingMeters < 0.0F)
    {
        return Core::failure(
            RenderErrorCode::InvalidMesh3DLighting,
            "Cascaded directional shadow projection requires a finite camera and positive range");
    }

    const float farDepthMeters =
        (std::min)(input.camera.farPlaneMeters, input.maximumDistanceMeters);
    auto splits = computeCascadedDirectionalShadowSplitDepths(
        input.camera.nearPlaneMeters, farDepthMeters);
    if (!splits)
    {
        return Core::failure(std::move(splits.error()));
    }

    BgfxCascadedDirectionalShadowProjection projection{
        .splitDepthsMeters = *splits,
    };
    float nearDepthMeters = input.camera.nearPlaneMeters;
    for (usize cascadeIndex = 0; cascadeIndex < projection.cascades.size(); ++cascadeIndex)
    {
        auto cascade = computeCascade(input, nearDepthMeters,
                                      projection.splitDepthsMeters[cascadeIndex], cascadeIndex,
                                      homogeneousDepth, originBottomLeft);
        if (!cascade)
        {
            return Core::failure(std::move(cascade.error()));
        }
        projection.cascades[cascadeIndex] = *cascade;
        nearDepthMeters = projection.splitDepthsMeters[cascadeIndex];
    }
    return projection;
}

} // namespace Tina::Render::Bgfx
