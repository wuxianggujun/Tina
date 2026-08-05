#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderFrame.hpp>

#include <array>
#include <span>

namespace Tina::Render {

enum class RenderPassKind : u8 {
    Clear,
    CascadedDirectionalShadowDepth,
    SpotLightShadowDepth,
    PointLightShadowDepth,
    Opaque3D,
    Sprite2D,
    UI,
};

enum class RenderPassResource : u8 {
    PrimarySurface,
    DirectionalShadowAtlas,
    SpotLightShadowMap,
    PointLightShadowMap,
};

struct RenderPassPlan final {
    RenderPassKind kind = RenderPassKind::Clear;
    RenderPassResource resource = RenderPassResource::PrimarySurface;
    u32 cascadeIndex = 0;
    u32 faceIndex = 0;
    bool clearColor = false;
    bool clearDepth = false;
};

class RenderPassSchedule final {
  public:
    // Optional full-surface clear plus four shadow cascades, one spot shadow,
    // six point-light faces, Opaque3D, Sprite2D and UI.
    static constexpr u32 MaximumPassCount = 15;

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
// Cascaded directional, spot and point shadows insert backend-owned depth passes;
// their clears never consume primary-surface clear ownership. The spot pass is
// ordered after all directional cascades, followed by point faces in
// +X/-X/+Y/-Y/+Z/-Z order, then Opaque3D.
// When the first primary-surface content pass uses a partial viewport, a
// full-surface clear pass precedes it; an active surface with no content also
// receives one clear-only pass.
// Suspended surfaces produce an empty schedule and must skip submit/present.
[[nodiscard]] Core::Result<RenderPassSchedule>
buildRenderPassSchedule(const RenderFrame& frame) noexcept;

} // namespace Tina::Render
