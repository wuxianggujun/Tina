#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderSurface.hpp>

#include <limits>

namespace Tina::Render::Bgfx {

enum class BgfxSurfaceFramePlanKind : u8 {
    Skip,
    Submit,
};

struct BgfxSurfaceFramePlan final {
    BgfxSurfaceFramePlanKind kind = BgfxSurfaceFramePlanKind::Skip;
    bool resetBackbuffer = false;
    RenderSurfaceExtent targetExtent{};

    [[nodiscard]] constexpr bool shouldSubmit() const noexcept
    {
        return kind == BgfxSurfaceFramePlanKind::Submit;
    }
};

class BgfxSurfaceFramePlanner final {
  public:
    inline static constexpr RenderSurfaceExtent BootstrapBackbufferExtent{1U, 1U};
    inline static constexpr u32 MaxViewRectExtent = (std::numeric_limits<u16>::max)();

    [[nodiscard]] static constexpr RenderSurfaceExtent
    bootstrapBackbufferExtent(const RenderSurfaceState& initialSurface) noexcept
    {
        if (initialSurface.availability != RenderSurfaceAvailability::Active)
        {
            return BootstrapBackbufferExtent;
        }
        return initialSurface.framebufferExtent;
    }

    [[nodiscard]] static Core::Status validateViewExtent(const RenderSurfaceState& surface);

    [[nodiscard]] static Core::Result<BgfxSurfaceFramePlan> planFrame(
        const RenderSurfaceState& previousSurface,
        const RenderSurfaceState& currentSurface,
        RenderSurfaceExtent appliedBackbufferExtent);
};

} // namespace Tina::Render::Bgfx
