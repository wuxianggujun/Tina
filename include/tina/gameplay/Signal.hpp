#pragma once

#include <tina/core/base/MoveOnlyFunction.hpp>
#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/gameplay/GameplayErrors.hpp>

#include <algorithm>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace Tina::Gameplay {

// Payload for a signal that carries nothing. Signals are always typed, so
// "something happened" is Signal<Unit> rather than a separate untyped class --
// one dispatch rule, one set of stats, one reentrancy contract.
struct Unit final {
    friend constexpr bool operator==(const Unit&, const Unit&) noexcept = default;
};

namespace Detail {

// Non-template base so SignalSubscription can unsubscribe without knowing the
// payload type. The vtable is the only indirection, and it is only touched when a
// token is reset -- never on the dispatch path.
class SignalControl {
  public:
    SignalControl() noexcept = default;
    virtual ~SignalControl();

    SignalControl(const SignalControl&) = delete;
    SignalControl& operator=(const SignalControl&) = delete;
    SignalControl(SignalControl&&) = delete;
    SignalControl& operator=(SignalControl&&) = delete;

    virtual void unsubscribeSlot(u32 slot, u32 generation) noexcept = 0;
    [[nodiscard]] virtual bool isSlotActive(u32 slot, u32 generation) const noexcept = 0;
};

} // namespace Detail

// Move-only ownership of one subscription. Destruction or reset() unsubscribes.
//
// It is the scoped part of the design: there is no unsubscribe-by-callback and no
// way to subscribe without receiving one, because the failure this prevents --
// a State that exited while its callback still points into freed members -- is a
// use-after-free rather than a leak, and callback identity cannot express it
// (two subscriptions of the same function are distinct registrations).
//
// Reset stays safe after the signal itself is destroyed: the token holds a weak
// reference, so an expired signal makes reset a no-op instead of a dangling
// write. That matters because a State's teardown order is not always the reverse
// of its construction order.
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

    SignalSubscription(std::weak_ptr<Detail::SignalControl> control, u32 slot,
                       u32 generation) noexcept;

    std::weak_ptr<Detail::SignalControl> m_control{};
    u32 m_slot = 0;
    u32 m_generation = 0;
};

struct SignalConfig final {
    // Concurrent subscribers. Fixed at Create; exceeding it is CapacityExceeded
    // rather than a reallocation.
    Core::usize subscriberCapacity = 32;
    // Payloads that may be queued by post() before a drain(). Zero means the
    // signal is immediate-only and post() returns DeferredDeliveryUnavailable --
    // stated rather than silently promoting post() to emit(), because the two
    // differ in exactly the ordering the caller chose post() to get.
    Core::usize deferredCapacity = 0;
    std::pmr::memory_resource* memoryResource = nullptr;
};

struct SignalStats final {
    Core::usize subscriberCapacity = 0;
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

// Fixed-capacity typed gameplay signal with explicit immediate and deferred
// delivery.
//
// It exists because the alternative in every State is a raw callback member or a
// direct method call between two owners that then cannot be separated. What this
// adds over that is only what a bus has to own to be safe: bounded storage,
// scoped registrations, a dispatch order that does not depend on nesting depth,
// and a queue for the case where the publisher must not run the subscribers
// inline.
//
// Typed per payload rather than one bus keyed by a type id: a central bus needs
// runtime type erasure and an any-like payload, and the mistake it admits --
// subscribing to the wrong payload type for an event name -- becomes a silent
// no-op instead of a compile error.
//
// emit() runs subscribers immediately, in subscription order. post() queues the
// payload for the next drain(), which is what a publisher inside a physics
// callback or a subscriber of another signal needs: dispatching there would run
// gameplay code in the middle of somebody else's iteration.
//
// Single-owner and not thread-safe. Subscribers may subscribe and unsubscribe
// freely, including unsubscribing themselves; a subscription added during a
// dispatch first receives the *next* delivery, and one removed during a dispatch
// receives nothing further, so delivery never depends on nesting depth. Emitting
// from inside a dispatch is ReentrantDispatch -- use post().
template <typename Payload>
class Signal final {
  public:
    using Callback = Core::MoveOnlyFunction<void(const Payload&)>;

    static_assert(std::is_nothrow_destructible_v<Payload>,
                  "Tina::Gameplay::Signal payloads must have noexcept destructors");

    Signal() noexcept = default;
    ~Signal() noexcept = default;

    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;
    Signal(Signal&&) noexcept = default;
    Signal& operator=(Signal&&) noexcept = default;

    [[nodiscard]] static Core::Result<Signal> Create(SignalConfig config = {})
    {
        if (config.subscriberCapacity == 0) {
            return Core::failure(GameplayErrorCode::InvalidConfiguration,
                                 "Signal subscriberCapacity must be greater than zero");
        }
        std::pmr::memory_resource& resource = config.memoryResource != nullptr
            ? *config.memoryResource
            : *std::pmr::get_default_resource();

        // One allocation for the shared state, plus one reserve for each bounded
        // vector. Nothing allocates afterwards: subscribe reuses slots and post
        // writes into reserved storage.
        try {
            std::pmr::polymorphic_allocator<State> allocator{&resource};
            auto state = std::allocate_shared<State>(allocator, config, resource);
            state->slots.reserve(config.subscriberCapacity);
            state->freeSlots.reserve(config.subscriberCapacity);
            if (config.deferredCapacity != 0) {
                state->queued.reserve(config.deferredCapacity);
            }
            return Signal(std::move(state));
        } catch (const std::bad_alloc&) {
            return Core::failure(GameplayErrorCode::AllocationFailed,
                                 "Signal storage allocation failed");
        }
    }

    [[nodiscard]] bool hasValue() const noexcept { return m_state != nullptr; }
    explicit operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] Core::Result<SignalSubscription> subscribe(Callback callback)
    {
        if (m_state == nullptr) {
            return Core::failure(GameplayErrorCode::InvalidConfiguration,
                                 "Signal was not created");
        }
        if (!callback) {
            return Core::failure(GameplayErrorCode::MissingCallback,
                                 "Signal subscriber callback is empty");
        }
        State& state = *m_state;
        if (state.subscriberCount >= state.config.subscriberCapacity) {
            return Core::failure(GameplayErrorCode::CapacityExceeded,
                                 "Signal subscriberCapacity is exhausted");
        }

        u32 slotIndex = 0;
        if (!state.freeSlots.empty()) {
            slotIndex = state.freeSlots.back();
            state.freeSlots.pop_back();
            Slot& slot = state.slots[slotIndex];
            slot.callback = std::move(callback);
            slot.active = true;
            slot.pendingRemoval = false;
            // A subscription made during a dispatch must not receive that same
            // dispatch, otherwise a subscriber that subscribes another is
            // delivered to or skipped depending on slot reuse order.
            slot.armedAtDispatch = state.dispatchSequence + (state.dispatching ? 1 : 0);
        } else {
            slotIndex = static_cast<u32>(state.slots.size());
            state.slots.push_back(Slot{
                .callback = std::move(callback),
                .generation = 1,
                .armedAtDispatch = state.dispatchSequence + (state.dispatching ? 1 : 0),
                .active = true,
                .pendingRemoval = false,
            });
        }
        ++state.subscriberCount;
        state.stats.subscriberHighWater =
            (std::max)(state.stats.subscriberHighWater, state.subscriberCount);

        return SignalSubscription(std::static_pointer_cast<Detail::SignalControl>(m_state),
                                  slotIndex, state.slots[slotIndex].generation);
    }

    // Dispatches to every subscriber armed before this call, in subscription
    // order. Returns the number of subscribers that ran.
    [[nodiscard]] Core::Result<Core::u32> emit(const Payload& payload)
    {
        if (m_state == nullptr) {
            return Core::failure(GameplayErrorCode::InvalidConfiguration,
                                 "Signal was not created");
        }
        State& state = *m_state;
        if (state.dispatching) {
            return Core::failure(GameplayErrorCode::ReentrantDispatch,
                                 "Signal::emit was re-entered from a subscriber; use post()");
        }
        ++state.stats.emitCount;
        return dispatch(state, payload);
    }

    // Queues a payload for the next drain(). Rejected when the signal was built
    // without a deferred queue, or when that queue is full.
    [[nodiscard]] Core::Status post(Payload payload)
    {
        static_assert(std::is_move_constructible_v<Payload> && std::is_move_assignable_v<Payload>,
                      "Tina::Gameplay::Signal::post requires a movable payload");
        if (m_state == nullptr) {
            return Core::failure(GameplayErrorCode::InvalidConfiguration,
                                 "Signal was not created");
        }
        State& state = *m_state;
        if (state.config.deferredCapacity == 0) {
            return Core::failure(GameplayErrorCode::DeferredDeliveryUnavailable,
                                 "Signal was created with deferredCapacity 0");
        }
        if (state.queued.size() >= state.config.deferredCapacity) {
            return Core::failure(GameplayErrorCode::CapacityExceeded,
                                 "Signal deferred queue is full");
        }
        state.queued.push_back(std::move(payload));
        ++state.stats.postCount;
        state.stats.queuedHighWater = (std::max)(state.stats.queuedHighWater, state.queued.size());
        return Core::success();
    }

    // Dispatches the payloads queued before this call and returns how many were
    // delivered. A payload posted by a subscriber during the drain stays queued
    // for the next one, which is what keeps a signal that re-posts itself from
    // running forever inside one frame.
    [[nodiscard]] Core::Result<Core::u32> drain()
    {
        if (m_state == nullptr) {
            return Core::failure(GameplayErrorCode::InvalidConfiguration,
                                 "Signal was not created");
        }
        State& state = *m_state;
        if (state.dispatching) {
            return Core::failure(GameplayErrorCode::ReentrantDispatch,
                                 "Signal::drain was re-entered from a subscriber");
        }
        ++state.stats.drainCount;

        // The batch is erased only after the whole loop, so a payload stays
        // addressable while it is being dispatched and a subscriber that posts
        // sees a queue that still contains the pending batch. The consequence is
        // deliberate: posting from a subscriber while the queue is at capacity is
        // CapacityExceeded even though those entries are about to be dropped.
        // Erasing as we go instead would move every remaining payload during
        // dispatch, which is a far worse trade for a rare full-queue case.
        //
        // Queue storage never reallocates: post() refuses past deferredCapacity
        // and Create reserved exactly that, so the reference handed to dispatch
        // survives a subscriber's own post.
        const Core::usize batch = state.queued.size();
        Core::u32 dispatched = 0;
        for (Core::usize index = 0; index < batch; ++index) {
            const Core::Result<Core::u32> delivered = dispatch(state, state.queued[index]);
            if (!delivered) {
                // Not reachable while dispatch's only refusal is reentrancy, which
                // the check above already excluded. Handled anyway so the consumed
                // prefix is dropped rather than redelivered by a later drain.
                state.queued.erase(state.queued.begin(),
                                   std::next(state.queued.begin(),
                                             static_cast<std::ptrdiff_t>(index)));
                return Core::failure(delivered.error());
            }
            ++dispatched;
        }
        state.queued.erase(state.queued.begin(),
                           std::next(state.queued.begin(),
                                     static_cast<std::ptrdiff_t>(batch)));
        return dispatched;
    }

    // Drops queued payloads without delivering them. For a State that is exiting
    // and must not run gameplay reactions to events it will never see.
    void clearQueued() noexcept
    {
        if (m_state != nullptr) {
            m_state->queued.clear();
        }
    }

    [[nodiscard]] Core::usize subscriberCount() const noexcept
    {
        return m_state != nullptr ? m_state->subscriberCount : 0;
    }

    [[nodiscard]] Core::usize queuedCount() const noexcept
    {
        return m_state != nullptr ? m_state->queued.size() : 0;
    }

    [[nodiscard]] SignalStats stats() const noexcept
    {
        if (m_state == nullptr) {
            return {};
        }
        SignalStats snapshot = m_state->stats;
        snapshot.subscriberCapacity = m_state->config.subscriberCapacity;
        snapshot.subscriberCount = m_state->subscriberCount;
        snapshot.deferredCapacity = m_state->config.deferredCapacity;
        snapshot.queuedCount = m_state->queued.size();
        return snapshot;
    }

  private:
    struct Slot final {
        Core::MoveOnlyFunction<void(const Payload&)> callback{};
        u32 generation = 1;
        // Dispatch sequence this slot becomes eligible at.
        Core::u64 armedAtDispatch = 0;
        bool active = false;
        // Unsubscribed while its own dispatch was running. The callback cannot be
        // destroyed there -- it may be the frame currently executing -- so the
        // slot is reclaimed after the loop.
        bool pendingRemoval = false;
    };

    struct State final : Detail::SignalControl {
        State(const SignalConfig& configuration, std::pmr::memory_resource& resource)
            : config(configuration),
              slots(std::pmr::polymorphic_allocator<Slot>{&resource}),
              freeSlots(std::pmr::polymorphic_allocator<u32>{&resource}),
              queued(std::pmr::polymorphic_allocator<Payload>{&resource})
        {
        }

        void unsubscribeSlot(u32 slot, u32 generation) noexcept override
        {
            if (slot >= slots.size()) {
                return;
            }
            Slot& target = slots[slot];
            if (!target.active || target.generation != generation || target.pendingRemoval) {
                return;
            }
            if (dispatching) {
                // Reclaimed by reclaimPendingRemovals once the loop unwinds. The
                // generation is bumped now so the token stops matching
                // immediately, which is what makes isActive() honest.
                target.pendingRemoval = true;
                ++target.generation;
                --subscriberCount;
                ++stats.unsubscribedCount;
                return;
            }
            releaseSlot(slot);
        }

        [[nodiscard]] bool isSlotActive(u32 slot, u32 generation) const noexcept override
        {
            if (slot >= slots.size()) {
                return false;
            }
            const Slot& target = slots[slot];
            return target.active && !target.pendingRemoval && target.generation == generation;
        }

        void releaseSlot(u32 slot) noexcept
        {
            Slot& target = slots[slot];
            target.callback = nullptr;
            target.active = false;
            target.pendingRemoval = false;
            ++target.generation;
            --subscriberCount;
            ++stats.unsubscribedCount;
            // Reserved at Create, so this never allocates.
            freeSlots.push_back(slot);
        }

        void reclaimPendingRemovals() noexcept
        {
            for (Core::usize index = 0; index < slots.size(); ++index) {
                Slot& target = slots[index];
                if (!target.pendingRemoval) {
                    continue;
                }
                // unsubscribeSlot already bumped the generation and the count.
                target.callback = nullptr;
                target.active = false;
                target.pendingRemoval = false;
                freeSlots.push_back(static_cast<u32>(index));
            }
        }

        SignalConfig config{};
        std::pmr::vector<Slot> slots;
        std::pmr::vector<u32> freeSlots;
        std::pmr::vector<Payload> queued;
        Core::usize subscriberCount = 0;
        Core::u64 dispatchSequence = 0;
        bool dispatching = false;
        SignalStats stats{};
    };

    [[nodiscard]] static Core::Result<Core::u32> dispatch(State& state, const Payload& payload)
    {
        const Core::u64 sequence = state.dispatchSequence;
        state.dispatching = true;
        // Restored through a scope guard rather than at the end of the function: a
        // subscriber is game code and may throw, and a signal left permanently
        // "dispatching" would answer ReentrantDispatch to every later emit for the
        // rest of the process.
        auto endDispatch = Core::makeScopeExit([&state]() noexcept {
            state.dispatching = false;
            ++state.dispatchSequence;
            state.reclaimPendingRemovals();
        });

        Core::u32 delivered = 0;
        // Indexed rather than iterated: a subscriber may subscribe another, and
        // even though slot storage cannot reallocate (see below) an iterator would
        // still be the wrong tool for a container being appended to mid-loop.
        //
        // Slot storage never reallocates because a slot is either active or on the
        // free list, subscribe() refuses past subscriberCapacity, and Create
        // reserved exactly that. So the reference below stays valid across the
        // callback even if that callback subscribes.
        for (Core::usize index = 0; index < state.slots.size(); ++index) {
            Slot& slot = state.slots[index];
            if (!slot.active || slot.pendingRemoval || slot.armedAtDispatch > sequence) {
                continue;
            }
            slot.callback(payload);
            ++delivered;
        }
        state.stats.deliveredCount += delivered;
        return delivered;
    }

    explicit Signal(std::shared_ptr<State> state) noexcept : m_state(std::move(state)) {}

    std::shared_ptr<State> m_state{};
};

} // namespace Tina::Gameplay
