#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderFrame.hpp>

#include <array>
#include <span>

namespace Tina::Render {

enum class RenderPassKind : u8 {
    Clear,
    DirectionalShadowDepth,
    Opaque3D,
    Sprite2D,
    UI,
};

enum class RenderPassTarget : u8 {
    PrimarySurface,
    DirectionalShadowMap,
};

struct RenderPassPlan final {
    RenderPassKind kind = RenderPassKind::Clear;
    RenderPassTarget target = RenderPassTarget::PrimarySurface;
    bool clearColor = false;
    bool clearDepth = false;
};

class RenderPassSchedule final {
  public:
    // Optional full-surface clear plus shadow depth, Opaque3D, Sprite2D and UI.
    static constexpr u32 MaximumPassCount = 5;

    [[nodiscard]] constexpr std::span<const RenderPassPlan> passes() const noexcept
    {
        return {m_passes.data(), m_passCount};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return m_passCount == 0; }

  private:
    friend Core::Result<RenderPassSchedule> buildRenderPassSchedule(const RenderFrame& frame) noexcept;

    std::array<RenderPassPlan, MaximumPassCount> m_passes{};
    u32 m_passCount = 0;
};

// Builds the deterministic pass order shared by all render backends. The first
// enabled full-surface primary-surface content pass owns the color/depth clear.
// A directional shadow caster inserts one backend-owned offscreen depth pass;
// its clear never consumes primary-surface clear ownership.
// When the first primary-surface content pass uses a partial viewport, a
// full-surface clear pass precedes it; an active surface with no content also
// receives one clear-only pass.
// Suspended surfaces produce an empty schedule and must skip submit/present.
[[nodiscard]] Core::Result<RenderPassSchedule>
buildRenderPassSchedule(const RenderFrame& frame) noexcept;

} // namespace Tina::Render
