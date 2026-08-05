#include <tina/render/RenderPassScheduler.hpp>

#include <tina/render/RenderErrors.hpp>

namespace Tina::Render {

namespace {

[[nodiscard]] bool coversWholeSurface(const RenderNormalizedViewport& viewport) noexcept
{
    return viewport.x == 0.0F && viewport.y == 0.0F && viewport.width == 1.0F && viewport.height == 1.0F;
}

} // namespace

Core::Result<RenderPassSchedule> buildRenderPassSchedule(const RenderFrame& frame) noexcept
{
    if (!frame.primaryWindowSurface.has_value())
    {
        return Core::failure(RenderErrorCode::InvalidSurfaceState,
                             "Render pass scheduling requires a primary window surface");
    }

    const RenderSurfaceState& surface = *frame.primaryWindowSurface;
    if (surface.availability == RenderSurfaceAvailability::Suspended)
    {
        return RenderPassSchedule{};
    }
    if (surface.availability != RenderSurfaceAvailability::Active)
    {
        return Core::failure(RenderErrorCode::InvalidSurfaceState,
                             "Render pass scheduling received an invalid surface availability");
    }

    RenderPassSchedule schedule{};
    const auto append = [&schedule](RenderPassKind kind, RenderPassResource resource,
                                    bool clearColor, bool clearDepth,
                                    u32 cascadeIndex = 0,
                                    u32 faceIndex = 0) noexcept {
        if (schedule.m_passCount >= RenderPassSchedule::MaximumPassCount)
        {
            return false;
        }
        schedule.m_passes[schedule.m_passCount++] = RenderPassPlan{
            .kind = kind,
            .resource = resource,
            .cascadeIndex = cascadeIndex,
            .faceIndex = faceIndex,
            .clearColor = clearColor,
            .clearDepth = clearDepth,
        };
        return true;
    };

    const bool hasOpaqueContent = frame.primaryWorldScene.perspectiveCamera().has_value() &&
                                  !frame.primaryWorldScene.meshes3D().empty();
    const bool hasSpriteContent = frame.primaryWorldScene.camera2D().has_value() &&
                                  !frame.primaryWorldScene.sprites2D().empty();
    const bool hasCascadedDirectionalShadow =
        hasOpaqueContent && frame.primaryWorldScene.mesh3DLighting().has_value() &&
        frame.primaryWorldScene.mesh3DLighting()->cascadedDirectionalShadow().has_value();
    const bool hasSpotLightShadow =
        hasOpaqueContent && frame.primaryWorldScene.mesh3DLighting().has_value() &&
        frame.primaryWorldScene.mesh3DLighting()->spotLightShadow().has_value();
    const bool hasPointLightShadow =
        hasOpaqueContent && frame.primaryWorldScene.mesh3DLighting().has_value() &&
        frame.primaryWorldScene.mesh3DLighting()->pointLightShadow().has_value();
    const bool firstSurfaceContentNeedsFullSurfaceClear =
        (hasOpaqueContent &&
         !coversWholeSurface(frame.primaryWorldScene.perspectiveCamera()->normalizedViewport)) ||
        (!hasOpaqueContent && hasSpriteContent &&
         !coversWholeSurface(frame.primaryWorldScene.camera2D()->normalizedViewport));

    bool ownsClear = !firstSurfaceContentNeedsFullSurfaceClear;
    if (firstSurfaceContentNeedsFullSurfaceClear &&
        !append(RenderPassKind::Clear, RenderPassResource::PrimarySurface, true, true))
    {
        return Core::failure(RenderErrorCode::RenderSceneCapacityExceeded,
                             "Render pass schedule exceeded its fixed pass capacity");
    }

    const auto appendContent = [&append, &ownsClear](RenderPassKind kind) noexcept {
        const bool clearColor = ownsClear;
        const bool clearDepth = ownsClear;
        ownsClear = false;
        return append(kind, RenderPassResource::PrimarySurface, clearColor, clearDepth);
    };

    for (u32 cascadeIndex = 0;
         hasCascadedDirectionalShadow &&
         cascadeIndex < Mesh3DCascadedDirectionalShadow::CascadeCount;
         ++cascadeIndex)
    {
        if (!append(RenderPassKind::CascadedDirectionalShadowDepth,
                    RenderPassResource::DirectionalShadowAtlas,
                    false, true, cascadeIndex))
        {
            return Core::failure(RenderErrorCode::RenderSceneCapacityExceeded,
                                 "Render pass schedule exceeded its fixed pass capacity");
        }
    }
    if (hasSpotLightShadow &&
        !append(RenderPassKind::SpotLightShadowDepth,
                RenderPassResource::SpotLightShadowMap,
                false, true))
    {
        return Core::failure(RenderErrorCode::RenderSceneCapacityExceeded,
                             "Render pass schedule exceeded its fixed pass capacity");
    }
    for (u32 faceIndex = 0;
         hasPointLightShadow && faceIndex < Mesh3DPointLightShadow::FaceCount;
         ++faceIndex)
    {
        if (!append(RenderPassKind::PointLightShadowDepth,
                    RenderPassResource::PointLightShadowMap,
                    false, true, 0, faceIndex))
        {
            return Core::failure(RenderErrorCode::RenderSceneCapacityExceeded,
                                 "Render pass schedule exceeded its fixed pass capacity");
        }
    }
    if (hasOpaqueContent && !appendContent(RenderPassKind::Opaque3D))
    {
        return Core::failure(RenderErrorCode::RenderSceneCapacityExceeded,
                             "Render pass schedule exceeded its fixed pass capacity");
    }
    if (hasSpriteContent && !appendContent(RenderPassKind::Sprite2D))
    {
        return Core::failure(RenderErrorCode::RenderSceneCapacityExceeded,
                             "Render pass schedule exceeded its fixed pass capacity");
    }
    if (!frame.primaryWindowUIDisplayList.empty() && !appendContent(RenderPassKind::UI))
    {
        return Core::failure(RenderErrorCode::RenderSceneCapacityExceeded,
                             "Render pass schedule exceeded its fixed pass capacity");
    }
    if (schedule.empty() &&
        !append(RenderPassKind::Clear, RenderPassResource::PrimarySurface, true, true))
    {
        return Core::failure(RenderErrorCode::RenderSceneCapacityExceeded,
                             "Render pass schedule exceeded its fixed pass capacity");
    }
    return schedule;
}

} // namespace Tina::Render
