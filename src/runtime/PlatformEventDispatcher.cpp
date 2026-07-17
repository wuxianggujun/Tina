#include <tina/runtime/spi/PlatformEventDispatcher.hpp>

#include <tina/runtime/RuntimeErrors.hpp>

#include <algorithm>
#include <exception>
#include <limits>
#include <new>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Tina::Detail {

class PlatformEventDispatcherState final {
  public:
    struct DispatchEntry final {
        u64 activationEpoch = 0;
        u32 slot = 0;
        u32 generation = 0;
    };

    struct Slot final {
        std::shared_ptr<PlatformEventCallback> callback;
        u64 activationEpoch = 0;
        u32 generation = 1;
        bool retired = false;
    };

    explicit PlatformEventDispatcherState(u32 capacity) : slots(capacity)
    {
        dispatchEntries.reserve(capacity);
    }

    [[nodiscard]] bool isActive(u32 slot, u32 generation) const noexcept
    {
        return slot < slots.size() && generation != 0 && slots[slot].generation == generation &&
               slots[slot].callback != nullptr;
    }

    void unsubscribe(u32 slot, u32 generation) noexcept
    {
        if (!isActive(slot, generation))
        {
            return;
        }

        Slot& entry = slots[slot];
        entry.callback.reset();
        entry.activationEpoch = 0;
        --activeCount;
        if (entry.generation == (std::numeric_limits<u32>::max)())
        {
            entry.retired = true;
            return;
        }
        ++entry.generation;
    }

    void close() noexcept
    {
        closed = true;
        activeCount = 0;
        for (Slot& slot : slots)
        {
            slot.callback.reset();
            slot.activationEpoch = 0;
            if (!slot.retired && slot.generation != (std::numeric_limits<u32>::max)())
            {
                ++slot.generation;
            } else
            {
                slot.retired = true;
            }
        }
    }

    std::vector<Slot> slots;
    std::vector<DispatchEntry> dispatchEntries;
    u64 nextActivationEpoch = 1;
    u32 activeCount = 0;
    bool dispatching = false;
    bool closed = false;
};

} // namespace Tina::Detail

namespace Tina {
namespace {

[[nodiscard]] Core::Status dispatcherError(Core::ErrorCode code, std::string_view message) noexcept
{
    try
    {
        return Core::failure(code, message);
    } catch (...)
    {
        std::terminate();
    }
}

} // namespace

PlatformEventNotification::PlatformEventNotification(const Platform::PlatformEvent& event,
                                                     std::span<const Platform::WindowFrameSnapshot> windows,
                                                     std::span<const Platform::GamepadSnapshot> gamepads) noexcept
    : m_event(&event), m_windows(windows), m_gamepads(gamepads)
{
}

const Platform::PlatformEvent& PlatformEventNotification::event() const noexcept
{
    return *m_event;
}

const Platform::WindowMetricsSnapshot* PlatformEventNotification::primaryWindowMetrics() const noexcept
{
    return m_windows.empty() ? nullptr : &m_windows.front().metrics;
}

const Platform::WindowMetricsSnapshot*
PlatformEventNotification::findWindowMetrics(Platform::WindowId window) const noexcept
{
    const auto iterator = std::ranges::find(
        m_windows, window, [](const Platform::WindowFrameSnapshot& snapshot) { return snapshot.metrics.window; });
    return iterator == m_windows.end() ? nullptr : &iterator->metrics;
}

bool PlatformEventNotification::isGamepadConnected(Platform::GamepadId gamepad) const noexcept
{
    return std::ranges::any_of(
        m_gamepads, [gamepad](const Platform::GamepadSnapshot& snapshot) { return snapshot.gamepad == gamepad; });
}

usize PlatformEventNotification::connectedGamepadCount() const noexcept
{
    return m_gamepads.size();
}

std::optional<Platform::GamepadId> PlatformEventNotification::connectedGamepadId(usize index) const noexcept
{
    return index < m_gamepads.size() ? std::optional<Platform::GamepadId>{m_gamepads[index].gamepad} : std::nullopt;
}

PlatformEventSubscription::PlatformEventSubscription(std::weak_ptr<Detail::PlatformEventDispatcherState> state,
                                                     u32 slot, u32 generation) noexcept
    : m_state(std::move(state)), m_slot(slot), m_generation(generation)
{
}

PlatformEventSubscription::~PlatformEventSubscription() noexcept
{
    reset();
}

PlatformEventSubscription::PlatformEventSubscription(PlatformEventSubscription&& other) noexcept
    : m_state(std::move(other.m_state)), m_slot(std::exchange(other.m_slot, 0)),
      m_generation(std::exchange(other.m_generation, 0))
{
}

PlatformEventSubscription& PlatformEventSubscription::operator=(PlatformEventSubscription&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    reset();
    m_state = std::move(other.m_state);
    m_slot = std::exchange(other.m_slot, 0);
    m_generation = std::exchange(other.m_generation, 0);
    return *this;
}

void PlatformEventSubscription::reset() noexcept
{
    if (m_generation == 0)
    {
        return;
    }
    if (auto state = m_state.lock())
    {
        state->unsubscribe(m_slot, m_generation);
    }
    m_state.reset();
    m_slot = 0;
    m_generation = 0;
}

bool PlatformEventSubscription::isActive() const noexcept
{
    if (m_generation == 0)
    {
        return false;
    }
    const auto state = m_state.lock();
    return state != nullptr && state->isActive(m_slot, m_generation);
}

PlatformEventSubscriptions::PlatformEventSubscriptions(PlatformEventDispatcher& dispatcher) noexcept
    : m_dispatcher(&dispatcher)
{
}

Core::Result<PlatformEventSubscription> PlatformEventSubscriptions::subscribe(PlatformEventCallback callback)
{
    return m_dispatcher->subscribe(std::move(callback));
}

Core::Result<PlatformEventDispatcher> PlatformEventDispatcher::Create(PlatformEventSubscriptionConfig config)
{
    if (config.subscriberCapacity == 0 ||
        config.subscriberCapacity > PlatformEventSubscriptionConfig::MaximumSubscriberCapacity)
    {
        return Core::failure(ConfigurationErrorCode::InvalidEngineConfig,
                             "Platform event subscriber capacity is outside the supported range");
    }

    try
    {
        return PlatformEventDispatcher{
            std::make_shared<Detail::PlatformEventDispatcherState>(config.subscriberCapacity)};
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "Platform event subscriber storage allocation failed");
    } catch (...)
    {
        return Core::failure(Core::CoreErrorCode::Internal, "Platform event subscriber storage construction failed");
    }
}

PlatformEventDispatcher::PlatformEventDispatcher(std::shared_ptr<Detail::PlatformEventDispatcherState> state) noexcept
    : m_state(std::move(state))
{
}

PlatformEventDispatcher::~PlatformEventDispatcher() noexcept
{
    if (m_state != nullptr)
    {
        m_state->close();
    }
}

PlatformEventDispatcher& PlatformEventDispatcher::operator=(PlatformEventDispatcher&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    if (m_state != nullptr)
    {
        m_state->close();
    }
    m_state = std::move(other.m_state);
    return *this;
}

Core::Result<PlatformEventSubscription> PlatformEventDispatcher::subscribe(PlatformEventCallback callback)
{
    if (!callback)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "Platform event callback must not be empty");
    }
    if (m_state == nullptr || m_state->closed)
    {
        return Core::failure(RuntimeErrorCode::PlatformEventDispatcherStopped, "Platform event dispatcher is stopped");
    }
    if (m_state->nextActivationEpoch == (std::numeric_limits<u64>::max)())
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded, "Platform event subscription epoch is exhausted");
    }

    for (u32 index = 0; index < m_state->slots.size(); ++index)
    {
        auto& slot = m_state->slots[index];
        if (slot.callback != nullptr || slot.retired)
        {
            continue;
        }

        try
        {
            slot.callback = std::make_shared<PlatformEventCallback>(std::move(callback));
        } catch (const std::bad_alloc&)
        {
            return Core::failure(Core::CoreErrorCode::OutOfMemory, "Platform event callback allocation failed");
        }
        slot.activationEpoch = m_state->nextActivationEpoch++;
        ++m_state->activeCount;
        return PlatformEventSubscription{m_state, index, slot.generation};
    }

    return Core::failure(Core::CoreErrorCode::CapacityExceeded, "Platform event subscriber capacity is exhausted");
}

u32 PlatformEventDispatcher::subscriberCount() const noexcept
{
    return m_state == nullptr ? 0 : m_state->activeCount;
}

u32 PlatformEventDispatcher::capacity() const noexcept
{
    return m_state == nullptr ? 0 : static_cast<u32>(m_state->slots.size());
}

Core::Status PlatformEventDispatcher::dispatch(std::span<const Platform::PlatformEvent> events,
                                               std::span<const Platform::WindowFrameSnapshot> windows,
                                               std::span<const Platform::GamepadSnapshot> gamepads) noexcept
{
    // A callback may move-assign or destroy the dispatcher object itself. Pin the
    // entry state and never read `this` again until dispatch returns.
    const std::shared_ptr<Detail::PlatformEventDispatcherState> dispatchState = m_state;
    if (dispatchState == nullptr || dispatchState->closed)
    {
        return dispatcherError(RuntimeErrorCode::PlatformEventDispatcherStopped,
                               "Platform event dispatcher is stopped");
    }
    if (dispatchState->dispatching)
    {
        return dispatcherError(RuntimeErrorCode::RecursivePlatformEventDispatch,
                               "Recursive platform event dispatch is not allowed");
    }

    dispatchState->dispatching = true;
    struct DispatchGuard final {
        Detail::PlatformEventDispatcherState& state;
        ~DispatchGuard() noexcept
        {
            state.dispatching = false;
        }
    } guard{*dispatchState};

    for (const Platform::PlatformEvent& event : events)
    {
        const PlatformEventNotification notification{event, windows, gamepads};
        dispatchState->dispatchEntries.clear();
        for (u32 index = 0; index < dispatchState->slots.size(); ++index)
        {
            const auto& slot = dispatchState->slots[index];
            if (slot.callback == nullptr || slot.activationEpoch == 0)
            {
                continue;
            }
            dispatchState->dispatchEntries.push_back({
                .activationEpoch = slot.activationEpoch,
                .slot = index,
                .generation = slot.generation,
            });
        }
        std::ranges::sort(dispatchState->dispatchEntries, {},
                          &Detail::PlatformEventDispatcherState::DispatchEntry::activationEpoch);

        for (const auto& dispatchEntry : dispatchState->dispatchEntries)
        {
            auto& slot = dispatchState->slots[dispatchEntry.slot];

            const std::shared_ptr<PlatformEventCallback> callback = slot.callback;
            if (callback == nullptr || !dispatchState->isActive(dispatchEntry.slot, dispatchEntry.generation))
            {
                continue;
            }
            try
            {
                std::invoke(*callback, notification);
            } catch (...)
            {
                auto status = dispatcherError(RuntimeErrorCode::PlatformEventCallbackThrewException,
                                              "A platform event callback threw an exception");
                try
                {
                    status.error().addContext("PlatformEventDispatcher::dispatch",
                                              "eventSequence=" + std::to_string(event.sequence));
                } catch (...)
                {
                    // Preserve the primary structured failure when optional
                    // diagnostic context cannot be allocated.
                }
                return status;
            }
        }
    }
    return Core::success();
}

void PlatformEventDispatcher::shutdown() noexcept
{
    if (m_state != nullptr)
    {
        m_state->close();
    }
}

} // namespace Tina
