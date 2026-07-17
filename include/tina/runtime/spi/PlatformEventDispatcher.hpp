#pragma once

#include <tina/runtime/PlatformEvents.hpp>

#include <memory>

namespace Tina::Detail {
class EngineHostImplementation;
}

namespace Tina {

// Runtime-owned main-thread lifecycle dispatcher. Platform input transitions
// and UI routed events intentionally use different channels.
class PlatformEventDispatcher final {
  public:
    [[nodiscard]] static Core::Result<PlatformEventDispatcher> Create(PlatformEventSubscriptionConfig config = {});

    ~PlatformEventDispatcher() noexcept;

    PlatformEventDispatcher(const PlatformEventDispatcher&) = delete;
    PlatformEventDispatcher& operator=(const PlatformEventDispatcher&) = delete;
    PlatformEventDispatcher(PlatformEventDispatcher&&) noexcept = default;
    PlatformEventDispatcher& operator=(PlatformEventDispatcher&& other) noexcept;

    [[nodiscard]] u32 subscriberCount() const noexcept;
    [[nodiscard]] u32 capacity() const noexcept;

  private:
    explicit PlatformEventDispatcher(std::shared_ptr<Detail::PlatformEventDispatcherState> state) noexcept;

    [[nodiscard]] Core::Status dispatch(std::span<const Platform::PlatformEvent> events,
                                        std::span<const Platform::WindowFrameSnapshot> windows,
                                        std::span<const Platform::GamepadSnapshot> gamepads) noexcept;
    void shutdown() noexcept;
    [[nodiscard]] Core::Result<PlatformEventSubscription> subscribe(PlatformEventCallback callback);

    std::shared_ptr<Detail::PlatformEventDispatcherState> m_state;

    friend class Detail::EngineHostImplementation;
    friend class PlatformEventSubscriptions;
    friend struct PlatformEventDispatcherTestAccess;
};

} // namespace Tina
