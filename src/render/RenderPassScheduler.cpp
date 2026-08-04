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
    const auto append = [&schedule](RenderPassKind kind, bool clearColor, bool clearDepth) noexcept {
        if (schedule.m_passCount >= RenderPassSchedule::MaximumPassCount)
        {
            return false;
        }
        schedule.m_passes[schedule.m_passCount++] = RenderPassPlan{
            .kind = kind,
            .clearColor = clearColor,
            .clearDepth = clearDepth,
        };
        return true;
    };

    const bool hasOpaqueContent = frame.primaryWorldScene.perspectiveCamera().has_value() &&
                                  !frame.primaryWorldScene.meshes3D().empty();
    const bool hasSpriteContent = frame.primaryWorldScene.camera2D().has_value() &&
                                  !frame.primaryWorldScene.sprites2D().empty();
    const bool firstSurfaceContentNeedsFullSurfaceClear =
        (hasOpaqueContent &&
         !coversWholeSurface(frame.primaryWorldScene.perspectiveCamera()->normalizedViewport)) ||
        (!hasOpaqueContent && hasSpriteContent &&
         !coversWholeSurface(frame.primaryWorldScene.camera2D()->normalizedViewport));

    bool ownsClear = !firstSurfaceContentNeedsFullSurfaceClear;
    if (firstSurfaceContentNeedsFullSurfaceClear && !append(RenderPassKind::Clear, true, true))
    {
        return Core::failure(RenderErrorCode::RenderSceneCapacityExceeded,
                             "Render pass schedule exceeded its fixed pass capacity");
    }

    const auto appendContent = [&append, &ownsClear](RenderPassKind kind) noexcept {
        const bool clearColor = ownsClear;
        const bool clearDepth = ownsClear;
        ownsClear = false;
        return append(kind, clearColor, clearDepth);
    };

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
    if (schedule.empty() && !append(RenderPassKind::Clear, true, true))
    {
        return Core::failure(RenderErrorCode::RenderSceneCapacityExceeded,
                             "Render pass schedule exceeded its fixed pass capacity");
    }
    return schedule;
}

} // namespace Tina::Render
