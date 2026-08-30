#pragma once

#include <tina/core/base/MoveOnlyFunction.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/platform/PlatformFrame.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace Tina::Detail {
class PlatformEventDispatcherState;
}

namespace Tina {

class GameStartupContext;
class GameStateEnterContext;
class PlatformEventDispatcher;

struct PlatformEventSubscriptionConfig final {
    static constexpr u32 DefaultSubscriberCapacity = 64;
    static constexpr u32 MaximumSubscriberCapacity = 1024;

    u32 subscriberCapacity = DefaultSubscriberCapacity;
};

// Callback-only lifecycle view paired with one PlatformEvent. It exposes the
// final committed device/window registry state needed to recover from a stream
// reset, but never exposes raw input transitions or native backend objects.
class PlatformEventNotification final {
  public:
    PlatformEventNotification(const PlatformEventNotification&) = delete;
    PlatformEventNotification& operator=(const PlatformEventNotification&) = delete;
    PlatformEventNotification(PlatformEventNotification&&) = delete;
    PlatformEventNotification& operator=(PlatformEventNotification&&) = delete;

    [[nodiscard]] const Platform::PlatformEvent& event() const noexcept;
    [[nodiscard]] const Platform::WindowMetricsSnapshot* primaryWindowMetrics() const noexcept;
    [[nodiscard]] const Platform::WindowMetricsSnapshot* findWindowMetrics(Platform::WindowId window) const noexcept;
    [[nodiscard]] bool isGamepadConnected(Platform::GamepadId gamepad) const noexcept;
    [[nodiscard]] usize connectedGamepadCount() const noexcept;
    [[nodiscard]] std::optional<Platform::GamepadId> connectedGamepadId(usize index) const noexcept;

  private:
    PlatformEventNotification(const Platform::PlatformEvent& event,
                              std::span<const Platform::WindowFrameSnapshot> windows,
                              std::span<const Platform::GamepadSnapshot> gamepads) noexcept;

    const Platform::PlatformEvent* m_event = nullptr;
    std::span<const Platform::WindowFrameSnapshot> m_windows;
    std::span<const Platform::GamepadSnapshot> m_gamepads;

    friend class PlatformEventDispatcher;
};

using PlatformEventCallback = Core::MoveOnlyFunction<void(const PlatformEventNotification&)>;

class PlatformEventSubscription final {
  public:
    ~PlatformEventSubscription() noexcept;

    PlatformEventSubscription(const PlatformEventSubscription&) = delete;
    PlatformEventSubscription& operator=(const PlatformEventSubscription&) = delete;
    PlatformEventSubscription(PlatformEventSubscription&& other) noexcept;
    PlatformEventSubscription& operator=(PlatformEventSubscription&& other) noexcept;

    void reset() noexcept;
    [[nodiscard]] bool isActive() const noexcept;

  private:
    PlatformEventSubscription(std::weak_ptr<Detail::PlatformEventDispatcherState> state, u32 slot,
                              u32 generation) noexcept;

    std::weak_ptr<Detail::PlatformEventDispatcherState> m_state;
    u32 m_slot = 0;
    u32 m_generation = 0;

    friend class PlatformEventDispatcher;
};

// Callback-scope registration facade exposed to game startup/state entry.
// It deliberately has no dispatcher ownership, dispatch, shutdown, or move API.
class PlatformEventSubscriptions final {
  public:
    PlatformEventSubscriptions(const PlatformEventSubscriptions&) = delete;
    PlatformEventSubscriptions& operator=(const PlatformEventSubscriptions&) = delete;
    PlatformEventSubscriptions(PlatformEventSubscriptions&&) = delete;
    PlatformEventSubscriptions& operator=(PlatformEventSubscriptions&&) = delete;

    [[nodiscard]] Core::Result<PlatformEventSubscription> subscribe(PlatformEventCallback callback);

  private:
    explicit PlatformEventSubscriptions(PlatformEventDispatcher& dispatcher) noexcept;

    PlatformEventDispatcher* m_dispatcher = nullptr;

    friend class GameStartupContext;
    friend class GameStateEnterContext;
};

} // namespace Tina
