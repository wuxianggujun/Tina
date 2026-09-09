#include "CascadedDirectionalShadowMath.hpp"
#include "ShadowProjectionMath.hpp"

#include <tina/render/RenderErrors.hpp>


#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace Tina::Render::Shadow {
namespace {

constexpr float Pi = 3.14159265358979323846F;

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

[[nodiscard]] Core::Result<CascadedDirectionalShadowCascade>
computeCascade(const CascadedDirectionalShadowInput& input,
               float nearDepthMeters,
               float farDepthMeters,
               usize cascadeIndex,
               bool homogeneousDepth,
               bool originBottomLeft) noexcept
{
    const RenderPerspectiveCamera& camera = input.camera;
    const Math::Vec3 forward = Detail::normalizeShadowAxis(
        {camera.forwardX, camera.forwardY, camera.forwardZ});
    const Math::Vec3 requestedUp = Detail::normalizeShadowAxis({camera.upX, camera.upY, camera.upZ});
    const Math::Vec3 right = Detail::normalizeShadowAxis(Math::cross(forward, requestedUp));
    const Math::Vec3 up = Detail::normalizeShadowAxis(Math::cross(right, forward));
    const Math::Vec3 towardLight = Detail::normalizeShadowAxis(
        {input.light.directionTowardLightX, input.light.directionTowardLightY,
         input.light.directionTowardLightZ});
    if (!Math::isFinite(forward) || !Math::isFinite(right) || !Math::isFinite(up) ||
        !Math::isFinite(towardLight) || Math::dot(forward, forward) <= 0.0F ||
        Math::dot(right, right) <= 0.0F || Math::dot(up, up) <= 0.0F ||
        Math::dot(towardLight, towardLight) <= 0.0F)
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

    const Math::Vec3 cameraPosition{camera.positionX, camera.positionY, camera.positionZ};

    // Bounding sphere of the frustum slice instead of a tight box around its corners. The
    // radius depends only on the split depths and the lens, never on where the camera is or
    // which way it points, and that invariance is the whole point: a window whose size
    // changes every frame has no fixed texel grid to snap to, and without snapping the
    // shadow map re-quantizes as the camera moves and every edge crawls.
    const float radialTangentSquared =
        tangent * tangent * (1.0F + camera.aspectRatio * camera.aspectRatio);
    float centerDepthMeters =
        (nearDepthMeters + farDepthMeters) * (1.0F + radialTangentSquared) * 0.5F;
    float radiusMeters = 0.0F;
    if (centerDepthMeters >= farDepthMeters)
    {
        // A wide lens pushes the point equidistant from both end caps past the far plane. The
        // far circle alone encloses the slice there, and that is not a separate case bolted
        // on: `centerDepth >= far` and `far - near <= k^2 * (far + near)` are the same
        // inequality, so this branch is exactly where the far circle is sufficient.
        centerDepthMeters = farDepthMeters;
        radiusMeters = std::sqrt(radialTangentSquared) * farDepthMeters;
    }
    else
    {
        const float farOffsetMeters = farDepthMeters - centerDepthMeters;
        radiusMeters = std::sqrt(farOffsetMeters * farOffsetMeters +
                                 radialTangentSquared * farDepthMeters * farDepthMeters);
    }
    if (!std::isfinite(radiusMeters) || radiusMeters <= 0.0F)
    {
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Cascaded directional shadow slice has no finite bounding sphere");
    }

    // Snapping leaves the window centre up to half a texel off the sphere centre, so the
    // window carries one texel of slack. The slack is folded in before the texel size is
    // derived, because the snapping grid has to be the atlas grid exactly -- a window that
    // steps by anything other than its own texel still swims, just more slowly.
    const float tileExtentTexels = static_cast<float>(input.tileExtent);
    const float halfExtentMeters = radiusMeters * (1.0F + 2.0F / tileExtentTexels);
    const float texelSizeMeters = 2.0F * halfExtentMeters / tileExtentTexels;
    if (!std::isfinite(texelSizeMeters) || texelSizeMeters <= 0.0F)
    {
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Cascaded directional shadow tile extent yields no usable texel size");
    }

    CascadedDirectionalShadowCascade cascade{
        .nearDepthMeters = nearDepthMeters,
        .farDepthMeters = farDepthMeters,
        .texelSizeMeters = texelSizeMeters,
    };
    const Math::Vec3 viewUp = std::abs(towardLight.y) < 0.98F
        ? Math::Vec3{0.0F, 1.0F, 0.0F} : Math::Vec3{1.0F, 0.0F, 0.0F};
    // The light view is anchored at the world origin, not at the camera. Bounds snapped in a
    // frame that itself slides with the camera would still slide; only a camera-independent
    // frame makes "an exact multiple of a texel" mean anything frame to frame. An orthographic
    // projection does not care where along the light axis the eye sits, so the anchor costs
    // nothing but the numeric magnitude of light-space coordinates.
    const auto lightView = Math::lookAtRightHanded(
        towardLight * input.maximumDistanceMeters, Math::Vec3{}, viewUp);
    if (!lightView)
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Cascaded directional shadow view is degenerate");
    cascade.lightView = lightView->columns;

    const Math::Vec3 centerWorld = cameraPosition + forward * centerDepthMeters;
    const Math::Vec3 centerLight = Math::transformPoint(*lightView, centerWorld);
    if (!Math::isFinite(centerLight))
    {
        return Core::failure(
            RenderErrorCode::InvalidMesh3DLighting,
            "Cascaded directional shadow view transform produced non-finite bounds");
    }

    const float snappedCenterX =
        std::round(centerLight.x / texelSizeMeters) * texelSizeMeters;
    const float snappedCenterY =
        std::round(centerLight.y / texelSizeMeters) * texelSizeMeters;
    cascade.bounds.minX = snappedCenterX - halfExtentMeters;
    cascade.bounds.maxX = snappedCenterX + halfExtentMeters;
    cascade.bounds.minY = snappedCenterY - halfExtentMeters;
    cascade.bounds.maxY = snappedCenterY + halfExtentMeters;

    // Depth stays tight against the real corners rather than the sphere. It is neither snapped
    // nor rasterized -- caster and receiver share this transform, so a continuous depth shift
    // cancels in the comparison -- so it gains nothing from the sphere's rotation invariance
    // and loses two things to it: depth precision, and the guarantee that geometry parked far
    // along the light axis (a sun billboard, say) stays outside the depth pass.
    const float nearHalfHeight = nearDepthMeters * tangent;
    const float farHalfHeight = farDepthMeters * tangent;
    const float nearHalfWidth = nearHalfHeight * camera.aspectRatio;
    const float farHalfWidth = farHalfHeight * camera.aspectRatio;
    const Math::Vec3 nearCenter = cameraPosition + forward * nearDepthMeters;
    const Math::Vec3 farCenter = cameraPosition + forward * farDepthMeters;
    const std::array<Math::Vec3, 8> sliceCorners{
        nearCenter + right * -nearHalfWidth + up * -nearHalfHeight,
        nearCenter + right * nearHalfWidth + up * -nearHalfHeight,
        nearCenter + right * -nearHalfWidth + up * nearHalfHeight,
        nearCenter + right * nearHalfWidth + up * nearHalfHeight,
        farCenter + right * -farHalfWidth + up * -farHalfHeight,
        farCenter + right * farHalfWidth + up * -farHalfHeight,
        farCenter + right * -farHalfWidth + up * farHalfHeight,
        farCenter + right * farHalfWidth + up * farHalfHeight,
    };
    cascade.bounds.minZ = (std::numeric_limits<float>::max)();
    cascade.bounds.maxZ = -(std::numeric_limits<float>::max)();
    for (const Math::Vec3& corner : sliceCorners)
    {
        const Math::Vec3 lightSpace = Math::transformPoint(*lightView, corner);
        if (!Math::isFinite(lightSpace))
        {
            return Core::failure(
                RenderErrorCode::InvalidMesh3DLighting,
                "Cascaded directional shadow view transform produced non-finite bounds");
        }
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

    const auto lightProjection = Math::orthographicRightHanded(cascade.bounds.minX,
        cascade.bounds.maxX, cascade.bounds.minY, cascade.bounds.maxY,
        -cascade.bounds.maxZ, -cascade.bounds.minZ, Detail::depthRange(homogeneousDepth));
    if (!lightProjection)
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Cascaded directional shadow projection is degenerate");
    cascade.lightProjection = lightProjection->columns;

    constexpr float TileScale = 0.5F;
    const float tileCenterX = (cascadeIndex % 2U == 0U) ? 0.25F : 0.75F;
    const float tileCenterY = (cascadeIndex < 2U) ? 0.25F : 0.75F;
    auto sampling = Detail::samplingTransform(*lightView, *lightProjection,
        homogeneousDepth, originBottomLeft, TileScale, {tileCenterX, tileCenterY});
    if (!sampling) return Core::failure(std::move(sampling.error()));
    cascade.samplingTransform = *sampling;
    return cascade;
}

} // namespace

Core::Result<std::array<float, CascadedDirectionalShadowCascadeCount>>
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

    std::array<float, CascadedDirectionalShadowCascadeCount> splits{};
    const float depthRatio = farDepthMeters / nearDepthMeters;
    for (usize cascadeIndex = 0; cascadeIndex < splits.size(); ++cascadeIndex)
    {
        const float partition = static_cast<float>(cascadeIndex + 1U) /
                                static_cast<float>(splits.size());
        const float logarithmic = nearDepthMeters * std::pow(depthRatio, partition);
        const float uniform = nearDepthMeters + (farDepthMeters - nearDepthMeters) * partition;
        splits[cascadeIndex] = CascadedDirectionalShadowSplitLambda * logarithmic +
                               (1.0F - CascadedDirectionalShadowSplitLambda) * uniform;
    }
    splits.back() = farDepthMeters;
    return splits;
}

Core::Result<CascadedDirectionalShadowProjection>
computeCascadedDirectionalShadowProjection(
    const CascadedDirectionalShadowInput& input,
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

    CascadedDirectionalShadowProjection projection{
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

} // namespace Tina::Render::Shadow
