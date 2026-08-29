#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderSurface.hpp>

#include <optional>
#include <utility>

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

    // True when the last successful commit replaced the native window behind the
    // surface (ADR 0034). A backend reads this to rebind its backbuffer instead of
    // inferring a rebind from geometry, which cannot distinguish a new window from a
    // resize to the same size. Cleared by the next commit that does not rebind.
    [[nodiscard]] bool consumeNativeBindingChanged() noexcept
    {
        return std::exchange(nativeBindingChanged_, false);
    }

  private:
    explicit RenderSurfaceStateTracker(const std::optional<RenderSurfaceState>& initialState) noexcept;

    bool compositionPresent_ = false;
    bool nativeBindingChanged_ = false;
    std::optional<RenderSurfaceState> committedState_{};
};

} // namespace Tina::Render::Detail
