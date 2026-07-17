#include "BgfxSurfaceFramePlanner.hpp"

#include <tina/render/RenderErrors.hpp>

#include <utility>

namespace Tina::Render::Bgfx {

Core::Status BgfxSurfaceFramePlanner::validateViewExtent(const RenderSurfaceState& surface)
{
    if (surface.availability != RenderSurfaceAvailability::Active)
    {
        return Core::success();
    }
    if (surface.framebufferExtent.width > MaxViewRectExtent ||
        surface.framebufferExtent.height > MaxViewRectExtent)
    {
        return Core::failure(RenderErrorCode::SurfaceReconfigureFailed,
                             "The primary surface extent exceeds bgfx view rectangle limits");
    }
    return Core::success();
}

Core::Result<BgfxSurfaceFramePlan> BgfxSurfaceFramePlanner::planFrame(
    const RenderSurfaceState& previousSurface,
    const RenderSurfaceState& currentSurface,
    RenderSurfaceExtent appliedBackbufferExtent)
{
    if (auto status = validateViewExtent(currentSurface); !status)
    {
        return Core::failure(std::move(status.error()));
    }

    switch (currentSurface.availability)
    {
    case RenderSurfaceAvailability::Suspended:
        return BgfxSurfaceFramePlan{
            .kind = BgfxSurfaceFramePlanKind::Skip,
            .resetBackbuffer = false,
            .targetExtent = appliedBackbufferExtent,
        };
    case RenderSurfaceAvailability::Active:
        break;
    default:
        return Core::failure(RenderErrorCode::InvalidSurfaceState,
                             "A render surface contains an invalid availability value");
    }

    const bool resumedFromSuspended = previousSurface.availability == RenderSurfaceAvailability::Suspended;
    const bool backbufferExtentChanged =
        currentSurface.framebufferExtent.width != appliedBackbufferExtent.width ||
        currentSurface.framebufferExtent.height != appliedBackbufferExtent.height;

    return BgfxSurfaceFramePlan{
        .kind = BgfxSurfaceFramePlanKind::Submit,
        .resetBackbuffer = resumedFromSuspended || backbufferExtentChanged,
        .targetExtent = currentSurface.framebufferExtent,
    };
}

} // namespace Tina::Render::Bgfx
