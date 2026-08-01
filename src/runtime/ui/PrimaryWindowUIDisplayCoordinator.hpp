#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/integration/UIRenderDisplayList.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/render/RenderSurface.hpp>
#include <tina/render/UIDisplayList.hpp>

#include <memory_resource>
#include <optional>
#include <thread>
#include <vector>

namespace Tina::UI {
class UIContext;
}

namespace Tina::Runtime::Detail {

class PrimaryWindowUICapabilityState;

struct PrimaryWindowUIDisplayBuild final {
    Render::UIDisplayListView displayList{};
    Integration::UIRenderDisplayListBuildStatistics conversionStatistics{};
};

// Runtime-private one-frame extraction boundary. It owns one fixed-capacity
// DisplayList builder and publishes at most one replacement for each accepted
// PlatformFrameId. Returned views borrow that builder and are valid only until
// the next build attempt, move, or destruction of this coordinator.
class PrimaryWindowUIDisplayCoordinator final {
  public:
    [[nodiscard]] static Core::Result<PrimaryWindowUIDisplayCoordinator>
    Create(Render::UIDisplayListCapacity capacity,
           std::pmr::memory_resource& storage = *std::pmr::get_default_resource());

    PrimaryWindowUIDisplayCoordinator(const PrimaryWindowUIDisplayCoordinator&) = delete;
    PrimaryWindowUIDisplayCoordinator& operator=(const PrimaryWindowUIDisplayCoordinator&) = delete;
    PrimaryWindowUIDisplayCoordinator(PrimaryWindowUIDisplayCoordinator&&) noexcept = default;
    PrimaryWindowUIDisplayCoordinator& operator=(PrimaryWindowUIDisplayCoordinator&&) = delete;

    [[nodiscard]] Core::Result<PrimaryWindowUIDisplayBuild>
    buildForFrame(UI::UIContext* context, const Platform::PlatformFrameView& platformFrame,
                  const std::optional<Render::RenderSurfaceState>& primaryWindowSurface,
                  const PrimaryWindowUICapabilityState& capabilityState,
                  Render::FrameResourceSink& resourceSink);

    [[nodiscard]] Render::UIDisplayListView publishedView() const noexcept;
    [[nodiscard]] Render::UIDisplayListBuilderStatistics builderStatistics() const noexcept;

  private:
    PrimaryWindowUIDisplayCoordinator(
        Render::UIDisplayListBuilder builder,
        std::pmr::vector<Integration::UIRenderImageResolutionCacheEntry> imageResolutionCache) noexcept;

    [[nodiscard]] Core::Result<PrimaryWindowUIDisplayBuild> failAttempt(Core::Error error);
    void invalidatePublishedView() noexcept;

    Render::UIDisplayListBuilder builder_;
    std::pmr::vector<Integration::UIRenderImageResolutionCacheEntry> imageResolutionCache_;
    std::thread::id ownerThreadId_{};
    Platform::PlatformFrameId lastAttemptedFrame_{};
    u64 lastMetricsRevision_ = 0;
};

} // namespace Tina::Runtime::Detail
