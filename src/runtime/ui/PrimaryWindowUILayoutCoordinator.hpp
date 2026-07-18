#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/platform/PlatformFrame.hpp>

#include <optional>
#include <thread>

namespace Tina::UI {
class UIContext;
}

namespace Tina::Runtime::Detail {

// Runtime-private phase boundary that publishes primary-window UI layout once
// for each accepted PlatformFrameId. A valid new id consumes the attempt even
// when identity validation or UIContext::commitLayout subsequently fails, so
// routed callbacks and retained mutations cannot be replayed in the same frame.
class PrimaryWindowUILayoutCoordinator final {
  public:
    PrimaryWindowUILayoutCoordinator() noexcept;

    PrimaryWindowUILayoutCoordinator(const PrimaryWindowUILayoutCoordinator&) = delete;
    PrimaryWindowUILayoutCoordinator& operator=(const PrimaryWindowUILayoutCoordinator&) = delete;
    PrimaryWindowUILayoutCoordinator(PrimaryWindowUILayoutCoordinator&&) = delete;
    PrimaryWindowUILayoutCoordinator& operator=(PrimaryWindowUILayoutCoordinator&&) = delete;

    // One-shot startup publication. It establishes the structure/layout/hit
    // snapshot consumed by the first routed Platform frame without consuming
    // a PlatformFrameId.
    [[nodiscard]] Core::Status commitForStartup(UI::UIContext* context,
                                                const std::optional<Platform::WindowMetricsSnapshot>& initialMetrics);

    [[nodiscard]] Core::Status commitForFrame(UI::UIContext* context, const Platform::PlatformFrameView& platformFrame);

  private:
    std::thread::id ownerThreadId_{};
    bool startupAttempted_ = false;
    Platform::PlatformFrameId lastAttemptedFrame_{};
};

} // namespace Tina::Runtime::Detail
