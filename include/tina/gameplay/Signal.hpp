#pragma once

#include <tina/core/base/MoveOnlyFunction.hpp>
#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/GenerationPool.hpp>
#include <tina/gameplay/GameplayErrors.hpp>

#include <algorithm>
#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace Tina::Gameplay {

struct Unit final {
    friend constexpr bool operator==(const Unit&, const Unit&) noexcept = default;
};

namespace Detail {

struct SignalSlotTag final {};
using SignalSlotId = Core::GenerationId<SignalSlotTag>;

// The token needs only this non-template lifetime/identity boundary.
class SignalControl {
  public:
    SignalControl() noexcept = default;
    virtual ~SignalControl();
    SignalControl(const SignalControl&) = delete;
    SignalControl& operator=(const SignalControl&) = delete;
    SignalControl(SignalControl&&) = delete;
    SignalControl& operator=(SignalControl&&) = delete;

    virtual void unsubscribeSlot(SignalSlotId slot) noexcept = 0;
    [[nodiscard]] virtual bool isSlotActive(SignalSlotId slot) const noexcept = 0;
};

} // namespace Detail

// Scoped registration. A weak owner plus generation identity makes reset safe
// after Signal destruction and prevents a recycled slot from matching a token.
class SignalSubscription final {
  public:
    SignalSubscription() noexcept = default;
    ~SignalSubscription() noexcept;
    SignalSubscription(const SignalSubscription&) = delete;
    SignalSubscription& operator=(const SignalSubscription&) = delete;
    SignalSubscription(SignalSubscription&& other) noexcept;
    SignalSubscription& operator=(SignalSubscription&& other) noexcept;

    void reset() noexcept;
    [[nodiscard]] bool isActive() const noexcept;
    explicit operator bool() const noexcept { return isActive(); }

  private:
    template <typename Payload>
    friend class Signal;
    SignalSubscription(std::weak_ptr<Detail::SignalControl> control,
                       Detail::SignalSlotId slot) noexcept;
    std::weak_ptr<Detail::SignalControl> m_control{};
    Detail::SignalSlotId m_slot{};
};

struct SignalConfig final {
    // Hint only. Subscribers grow in stable blocks, including during dispatch.
    Core::usize initialSubscriberReserve = 32;
    // Explicit producer backpressure, not a subscriber limit. Zero disables post.
    // Includes the payload currently being delivered by drain().
    Core::usize deferredCapacity = 0;
    std::pmr::memory_resource* memoryResource = nullptr;
};

struct SignalStats final {
    Core::usize reservedSubscriberSlots = 0;
    Core::usize subscriberCount = 0;
    Core::usize subscriberHighWater = 0;
    Core::usize deferredCapacity = 0;
    Core::usize queuedCount = 0;
    Core::usize queuedHighWater = 0;
    Core::u64 emitCount = 0;
    Core::u64 postCount = 0;
    Core::u64 drainCount = 0;
    Core::u64 deliveredCount = 0;
    Core::u64 unsubscribedCount = 0;
};

// Typed, single-owner signal. emit() preserves registration order independently
// of slot reuse. Registration during a delivery starts with the next payload;
// unsubscription takes effect immediately but destroys callbacks at a safe point.
// Recursive emit/drain is rejected; use post for deferred work.
template <typename Payload>
class Signal final {
  public:
    using Callback = Core::MoveOnlyFunction<void(const Payload&)>;
    static_assert(std::is_nothrow_destructible_v<Payload>,
                  "Tina::Gameplay::Signal payloads must have noexcept destructors");

    Signal() noexcept = default;
    ~Signal() noexcept { m_state.reset(); }
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;
    Signal(Signal&&) noexcept = default;
    Signal& operator=(Signal&&) noexcept = default;

    [[nodiscard]] static Core::Result<Signal> Create(SignalConfig config = {})
    {
        auto& resource = config.memoryResource ? *config.memoryResource : *std::pmr::get_default_resource();
        auto slots = SlotPool::Create(config.initialSubscriberReserve, resource);
        if (!slots) {
            return Core::failure(std::move(slots.error()));
        }
        try {
            auto state = std::allocate_shared<State>(std::pmr::polymorphic_allocator<State>{&resource},
                                                    config, std::move(*slots), resource);
            return Signal(std::move(state));
        } catch (const std::bad_alloc&) {
            return Core::failure(GameplayErrorCode::AllocationFailed, "Signal storage allocation failed");
        } catch (const std::length_error&) {
            return Core::failure(GameplayErrorCode::CapacityExceeded, "Signal deferred storage exceeds addressable size");
        }
    }

    [[nodiscard]] bool hasValue() const noexcept { return m_state != nullptr; }
    explicit operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] Core::Result<SignalSubscription> subscribe(Callback callback)
    {
        const auto state = m_state;
        if (!state) {
            return Core::failure(GameplayErrorCode::InvalidConfiguration, "Signal was not created");
        }
        if (!callback) {
            return Core::failure(GameplayErrorCode::MissingCallback, "Signal subscriber callback is empty");
        }
        if (state->slots.availableCount() == 0) {
            if (state->slots.capacity() == SlotId::InvalidIndex) {
                return Core::failure(GameplayErrorCode::CapacityExceeded, "Signal slot index space is exhausted");
            }
            if (auto status = state->slots.reserve(state->slots.capacity() + 1); !status) {
                return Core::failure(std::move(status.error()));
            }
        }
        auto slot = state->slots.tryEmplace(Slot{.callback = std::move(callback), .previous = state->tail});
        if (!slot) {
            return Core::failure(std::move(slot.error()));
        }
        if (auto* tail = state->slots.tryGet(state->tail)) {
            tail->next = *slot;
        } else {
            state->head = *slot;
        }
        state->tail = *slot;
        ++state->subscriberCount;
        state->stats.subscriberHighWater = (std::max)(state->stats.subscriberHighWater, state->subscriberCount);
        return SignalSubscription(state, *slot);
    }

    [[nodiscard]] Core::Result<Core::usize> emit(const Payload& payload)
    {
        // Keep State alive even if a callback moves/resets the Signal facade.
        const auto state = m_state;
        if (!state) {
            return Core::failure(GameplayErrorCode::InvalidConfiguration, "Signal was not created");
        }
        if (state->dispatching || state->draining || state->reclaiming || state->mutatingQueue) {
            return Core::failure(GameplayErrorCode::ReentrantDispatch, "Signal::emit cannot reenter delivery");
        }
        ++state->stats.emitCount;
        return dispatch(*state, payload);
    }

    // Container OOM is reported; other user Payload move exceptions propagate
    // without publishing a queue element. Queue capacity includes in-flight work.
    [[nodiscard]] Core::Status post(Payload payload)
    {
        static_assert(std::is_move_constructible_v<Payload>, "Signal::post requires a movable payload");
        const auto state = m_state;
        if (!state) {
            return Core::failure(GameplayErrorCode::InvalidConfiguration, "Signal was not created");
        }
        if (state->config.deferredCapacity == 0) {
            return Core::failure(GameplayErrorCode::DeferredDeliveryUnavailable, "Signal deferred delivery is disabled");
        }
        if (state->mutatingQueue) {
            return Core::failure(GameplayErrorCode::ReentrantDispatch,
                                 "Signal::post cannot reenter a payload constructor or destructor");
        }
        if (state->queuedCount == state->config.deferredCapacity) {
            return Core::failure(GameplayErrorCode::CapacityExceeded, "Signal deferred queue is full");
        }
        state->mutatingQueue = true;
        auto endMutation = Core::makeScopeExit([&state]() noexcept {
            state->mutatingQueue = false;
            state->applyPendingClear();
        });
        try {
            // A bounded ring preallocates only the intentional queue budget. No
            // element moves when posting, draining, or clearing other payloads.
            state->queued[state->queueIndex(state->queuedCount)].emplace(std::move(payload));
        } catch (const std::bad_alloc&) {
            return Core::failure(GameplayErrorCode::AllocationFailed, "Signal payload allocation failed");
        }
        ++state->queuedCount;
        ++state->stats.postCount;
        state->stats.queuedHighWater = (std::max)(state->stats.queuedHighWater, state->queuedCount);
        return Core::success();
    }

    // Only the entry batch is drained. Reposts wait for the next drain. A callback
    // exception consumes the current payload once, preserves the remainder, and
    // propagates after restoring the dispatch/queue guards.
    [[nodiscard]] Core::Result<Core::usize> drain()
    {
        const auto state = m_state;
        if (!state) {
            return Core::failure(GameplayErrorCode::InvalidConfiguration, "Signal was not created");
        }
        if (state->dispatching || state->draining || state->reclaiming || state->mutatingQueue) {
            return Core::failure(GameplayErrorCode::ReentrantDispatch, "Signal::drain cannot reenter delivery");
        }
        ++state->stats.drainCount;
        state->draining = true;
        state->remainingBatch = state->queuedCount;
        auto endDrain = Core::makeScopeExit([&state]() noexcept {
            state->draining = false;
            state->remainingBatch = 0;
        });
        Core::usize dispatched = 0;
        while (state->remainingBatch != 0) {
            --state->remainingBatch;
            state->inFlight = true;
            auto consumePayload = Core::makeScopeExit([&state]() noexcept {
                state->consumeFront();
            });
            (void)dispatch(*state, *state->queued[state->queueHead]);
            ++dispatched;
        }
        return dispatched;
    }

    // Clear pending messages, but keep the in-flight payload alive until all its
    // subscribers return. post() after clear belongs to the next drain.
    void clearQueued() noexcept
    {
        const auto state = m_state;
        if (!state) {
            return;
        }
        state->remainingBatch = 0;
        state->clearRequested = true;
        state->applyPendingClear();
    }

    [[nodiscard]] Core::usize subscriberCount() const noexcept { return m_state ? m_state->subscriberCount : 0; }
    [[nodiscard]] Core::usize queuedCount() const noexcept { return m_state ? m_state->queuedCount : 0; }
    [[nodiscard]] SignalStats stats() const noexcept
    {
        if (!m_state) {
            return {};
        }
        SignalStats snapshot = m_state->stats;
        snapshot.reservedSubscriberSlots = m_state->slots.capacity();
        snapshot.subscriberCount = m_state->subscriberCount;
        snapshot.deferredCapacity = m_state->config.deferredCapacity;
        snapshot.queuedCount = m_state->queuedCount;
        return snapshot;
    }

  private:
    using SlotId = Detail::SignalSlotId;
    struct Slot final {
        Callback callback{};
        SlotId previous{};
        SlotId next{};
        SlotId nextRemoval{};
        bool pendingRemoval = false;
    };
    using SlotPool = Core::GenerationPool<Slot, Detail::SignalSlotTag>;

    struct State final : Detail::SignalControl {
        State(SignalConfig configuration, SlotPool slotStorage, std::pmr::memory_resource& resource)
            : config(configuration), slots(std::move(slotStorage)), queued(config.deferredCapacity, &resource) {}

        [[nodiscard]] Core::usize queueIndex(Core::usize offset) const noexcept
        {
            const Core::usize toEnd = queued.size() - queueHead;
            return offset < toEnd ? queueHead + offset : offset - toEnd;
        }

        void applyPendingClear() noexcept
        {
            if (!clearRequested || mutatingQueue) {
                return;
            }
            mutatingQueue = true;
            const Core::usize keep = inFlight ? 1 : 0;
            while (queuedCount > keep) {
                const Core::usize index = queueIndex(--queuedCount);
                // Publish removal first; destructor reentry can request another
                // clear, but cannot mutate the ring while reset() is executing.
                queued[index].reset();
            }
            clearRequested = false;
            mutatingQueue = false;
        }

        void consumeFront() noexcept
        {
            mutatingQueue = true;
            const Core::usize index = queueHead;
            queueHead = queueIndex(1);
            --queuedCount;
            inFlight = false;
            queued[index].reset();
            mutatingQueue = false;
            applyPendingClear();
        }

        void unsubscribeSlot(SlotId id) noexcept override
        {
            Slot* slot = slots.tryGet(id);
            if (slot == nullptr || slot->pendingRemoval) {
                return;
            }
            slot->pendingRemoval = true;
            slot->nextRemoval = removals;
            removals = id;
            --subscriberCount;
            ++stats.unsubscribedCount;
            if (!dispatching && !reclaiming) {
                reclaimPendingRemovals();
            }
        }

        [[nodiscard]] bool isSlotActive(SlotId id) const noexcept override
        {
            const Slot* slot = slots.tryGet(id);
            return slot != nullptr && !slot->pendingRemoval;
        }

        void reclaimPendingRemovals() noexcept
        {
            reclaiming = true;
            while (removals) {
                const SlotId id = removals;
                Slot& slot = *slots.tryGet(id);
                removals = slot.nextRemoval;
                if (Slot* previous = slots.tryGet(slot.previous)) {
                    previous->next = slot.next;
                } else {
                    head = slot.next;
                }
                if (Slot* next = slots.tryGet(slot.next)) {
                    next->previous = slot.previous;
                } else {
                    tail = slot.previous;
                }
                // Capture destructors may reset other tokens. Those removals join
                // this no-allocation queue rather than recursively erasing.
                (void)slots.erase(id);
            }
            reclaiming = false;
        }

        SignalConfig config;
        SlotPool slots;
        std::pmr::vector<std::optional<Payload>> queued;
        SlotId head{};
        SlotId tail{};
        SlotId removals{};
        Core::usize subscriberCount = 0;
        Core::usize queueHead = 0;
        Core::usize queuedCount = 0;
        Core::usize remainingBatch = 0;
        bool dispatching = false;
        bool reclaiming = false;
        bool draining = false;
        bool inFlight = false;
        bool mutatingQueue = false;
        bool clearRequested = false;
        SignalStats stats{};
    };

    [[nodiscard]] static Core::usize dispatch(State& state, const Payload& payload)
    {
        state.dispatching = true;
        auto endDispatch = Core::makeScopeExit([&state]() noexcept {
            state.reclaimPendingRemovals();
            state.dispatching = false;
        });
        const SlotId last = state.tail;
        Core::usize delivered = 0;
        for (SlotId current = state.head; current;) {
            Slot& slot = *state.slots.tryGet(current);
            if (!slot.pendingRemoval) {
                slot.callback(payload);
                ++delivered;
                ++state.stats.deliveredCount;
            }
            if (current == last) {
                break;
            }
            current = slot.next;
        }
        return delivered;
    }

    explicit Signal(std::shared_ptr<State> state) noexcept : m_state(std::move(state)) {}
    std::shared_ptr<State> m_state{};
};

} // namespace Tina::Gameplay
