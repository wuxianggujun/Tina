#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderSurface.hpp>

#include <optional>

namespace Tina::Render::Detail {

// Shared private protocol guard for backend-neutral render surface state.
class RenderSurfaceStateTracker final {
  public:
    RenderSurfaceStateTracker(const RenderSurfaceStateTracker&) = delete;
    RenderSurfaceStateTracker& operator=(const RenderSurfaceStateTracker&) = delete;
    RenderSurfaceStateTracker(RenderSurfaceStateTracker&&) noexcept = default;
    RenderSurfaceStateTracker& operator=(RenderSurfaceStateTracker&&) noexcept = default;

    [[nodiscard]] static Core::Result<RenderSurfaceStateTracker>
    create(const std::optional<RenderSurfaceState>& initialState);

    [[nodiscard]] Core::Status validateAndCommit(const std::optional<RenderSurfaceState>& state);

  private:
    explicit RenderSurfaceStateTracker(const std::optional<RenderSurfaceState>& initialState) noexcept;

    bool compositionPresent_ = false;
    std::optional<RenderSurfaceState> committedState_{};
};

} // namespace Tina::Render::Detail
