#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/ui/UIContext.hpp>

#include <memory>
#include <memory_resource>
#include <thread>

namespace Tina::Runtime::Detail {

// Runtime-private owner that binds one vNext UIContext to the first primary
// window observed by EngineHost. It owns only identity and lifetime; layout,
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

    // The returned pointer is owned by this object and remains valid until
    // shutdown(). Headless frames before the first window bind return nullptr.
    [[nodiscard]] Core::Result<UI::UIContext*> selectForFrame(const Platform::PlatformFrameView& platformFrame);

    // Idempotent owner-thread phase-boundary shutdown. This must run before the
    // Platform module releases the window identity bound to the UIContext.
    void shutdown() noexcept;

  private:
    enum class State : u8 {
        Unbound,
        Bound,
        Stopped,
    };

    UI::UIContextCapacityConfig capacities_{};
    std::pmr::memory_resource* memoryResource_ = nullptr;
    std::thread::id ownerThreadId_{};
    std::unique_ptr<UI::UIContext> context_{};
    Platform::WindowId boundWindow_{};
    State state_ = State::Unbound;
};

} // namespace Tina::Runtime::Detail
