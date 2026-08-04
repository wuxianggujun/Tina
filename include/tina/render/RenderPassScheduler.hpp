#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderFrame.hpp>

#include <array>
#include <span>

namespace Tina::Render {

enum class RenderPassKind : u8 {
    Clear,
    Opaque3D,
    Sprite2D,
    UI,
};

struct RenderPassPlan final {
    RenderPassKind kind = RenderPassKind::Clear;
    bool clearColor = false;
    bool clearDepth = false;
};

class RenderPassSchedule final {
  public:
    // Optional full-surface clear plus Opaque3D, Sprite2D and UI.
    static constexpr u32 MaximumPassCount = 4;

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
// When the first primary-surface content pass uses a partial viewport, a
// full-surface clear pass precedes it; an active surface with no content also
// receives one clear-only pass.
// Suspended surfaces produce an empty schedule and must skip submit/present.
[[nodiscard]] Core::Result<RenderPassSchedule>
buildRenderPassSchedule(const RenderFrame& frame) noexcept;

} // namespace Tina::Render
