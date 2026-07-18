#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/ui/UIContext.hpp>

#include <memory>
#include <memory_resource>
#include <optional>
#include <thread>

namespace Tina::Runtime::Detail {

// Runtime-private owner that binds one vNext UIContext from the startup
// primary-window metrics seed. It owns only identity and lifetime; layout,
// retained roots, routing, and rendering remain responsibilities of their
// respective frame phases.
class PrimaryWindowUIContextOwner final {
  public:
    explicit PrimaryWindowUIContextOwner(
        UI::UIContextCapacityConfig capacities = {},
        std::pmr::memory_resource& memoryResource = *std::pmr::get_default_resource()) noexcept;
    ~PrimaryWindowUIContextOwner() noexcept;

    PrimaryWindowUIContextOwner(const PrimaryWindowUIContextOwner&) = delete;
    PrimaryWindowUIContextOwner& operator=(const PrimaryWindowUIContextOwner&) = delete;
    PrimaryWindowUIContextOwner(PrimaryWindowUIContextOwner&&) = delete;
    PrimaryWindowUIContextOwner& operator=(PrimaryWindowUIContextOwner&&) = delete;

    // One-shot startup binding. An empty seed explicitly selects Headless;
    // failures before publication leave the owner awaiting startup so focused
    // tests can prove atomic rollback.
    [[nodiscard]] Core::Result<UI::UIContext*>
    bindForStartup(const std::optional<Platform::WindowMetricsSnapshot>& initialMetrics);

    // The returned pointer is owned by this object and remains valid until
    // shutdown(). Frame selection validates the startup identity/revision and
    // never creates or migrates a context.
    [[nodiscard]] Core::Result<UI::UIContext*> selectForFrame(const Platform::PlatformFrameView& platformFrame);

    // Idempotent owner-thread phase-boundary shutdown. This must run before the
    // Platform module releases the window identity bound to the UIContext.
    void shutdown() noexcept;

  private:
    enum class State : u8 {
        AwaitingStartup,
        Headless,
        Bound,
        Stopped,
    };

    UI::UIContextCapacityConfig capacities_{};
    std::pmr::memory_resource* memoryResource_ = nullptr;
    std::thread::id ownerThreadId_{};
    std::unique_ptr<UI::UIContext> context_{};
    Platform::WindowId boundWindow_{};
    u64 lastMetricsRevision_ = 0;
    State state_ = State::AwaitingStartup;
};

} // namespace Tina::Runtime::Detail
