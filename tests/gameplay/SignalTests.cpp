#include <tina/gameplay/Signal.hpp>

#include <tina/gameplay/GameplayErrors.hpp>
#include "GameplayMemoryTestSupport.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <memory_resource>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Tina::Gameplay {
namespace {

class CountingMemoryResource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] Core::usize allocationCount() const noexcept { return m_allocationCount; }
    void resetCount() noexcept { m_allocationCount = 0; }

private:
    void* do_allocate(Core::usize bytes, Core::usize alignment) override
    {
        ++m_allocationCount;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, Core::usize bytes, Core::usize alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    Core::usize m_allocationCount = 0;
};

struct DamageEvent final {
    int amount = 0;

    friend constexpr bool operator==(const DamageEvent&, const DamageEvent&) noexcept = default;
};

template <typename Payload>
[[nodiscard]] Signal<Payload> makeSignal(SignalConfig config = {})
{
    Core::Result<Signal<Payload>> signal = Signal<Payload>::Create(config);
    EXPECT_TRUE(signal.has_value());
    return std::move(*signal);
}

} // namespace

TEST(SignalTests, CreateAcceptsZeroSubscriberReserve)
{
    Core::Result<Signal<Unit>> signal =
        Signal<Unit>::Create(SignalConfig{.initialSubscriberReserve = 0});
    ASSERT_TRUE(signal.has_value());
    EXPECT_EQ(signal->stats().reservedSubscriberSlots, 0U);
    EXPECT_TRUE(signal->subscribe([](const Unit&) {}));
}

// A default-constructed Signal is not a usable one. Every entry point reports
// InvalidConfiguration rather than dereferencing null shared state.
TEST(SignalTests, ADefaultConstructedSignalIsInertRatherThanUndefined)
{
    Signal<Unit> signal;
    EXPECT_FALSE(signal.hasValue());
    EXPECT_EQ(signal.subscribe([](const Unit&) {}).error().code,
              GameplayErrorCode::InvalidConfiguration);
    EXPECT_EQ(signal.emit(Unit{}).error().code, GameplayErrorCode::InvalidConfiguration);
    EXPECT_EQ(signal.post(Unit{}).error().code, GameplayErrorCode::InvalidConfiguration);
    EXPECT_EQ(signal.drain().error().code, GameplayErrorCode::InvalidConfiguration);
    EXPECT_EQ(signal.subscriberCount(), 0U);
    EXPECT_EQ(signal.queuedCount(), 0U);
    EXPECT_EQ(signal.stats().reservedSubscriberSlots, 0U);
    signal.clearQueued();
}

TEST(SignalTests, SubscribeRejectsAnEmptyCallback)
{
    Signal<Unit> signal = makeSignal<Unit>();
    Core::Result<SignalSubscription> subscription = signal.subscribe(Signal<Unit>::Callback{});
    ASSERT_FALSE(subscription.has_value());
    EXPECT_EQ(subscription.error().code, GameplayErrorCode::MissingCallback);
    EXPECT_EQ(signal.subscriberCount(), 0U);
}

TEST(SignalTests, SubscribeGrowsPastInitialReserve)
{
    Signal<Unit> signal = makeSignal<Unit>(SignalConfig{.initialSubscriberReserve = 2});
    std::vector<SignalSubscription> tokens;
    for (int index = 0; index < 2; ++index) {
        Core::Result<SignalSubscription> subscription = signal.subscribe([](const Unit&) {});
        ASSERT_TRUE(subscription.has_value());
        tokens.push_back(std::move(*subscription));
    }

    Core::Result<SignalSubscription> overflow = signal.subscribe([](const Unit&) {});
    ASSERT_TRUE(overflow.has_value());
    EXPECT_EQ(signal.subscriberCount(), 3U);
    EXPECT_GE(signal.stats().reservedSubscriberSlots, 3U);
}

// emit() runs subscribers immediately, in subscription order, and reports how many ran.
TEST(SignalTests, EmitDeliversInSubscriptionOrderAndCountsSubscribers)
{
    Signal<DamageEvent> signal = makeSignal<DamageEvent>();
    std::vector<int> order;
    std::vector<SignalSubscription> tokens;
    for (int index = 0; index < 3; ++index) {
        Core::Result<SignalSubscription> subscription =
            signal.subscribe([&order, index](const DamageEvent&) { order.push_back(index); });
        ASSERT_TRUE(subscription.has_value());
        tokens.push_back(std::move(*subscription));
    }

    Core::Result<Core::usize> delivered = signal.emit(DamageEvent{.amount = 5});
    ASSERT_TRUE(delivered.has_value());
    EXPECT_EQ(*delivered, 3U);
    EXPECT_EQ(order, std::vector<int>({0, 1, 2}));
    EXPECT_EQ(signal.stats().emitCount, 1U);
    EXPECT_EQ(signal.stats().deliveredCount, 3U);
}

TEST(SignalTests, EmitCarriesThePayloadUnchanged)
{
    Signal<DamageEvent> signal = makeSignal<DamageEvent>();
    DamageEvent seen{};
    Core::Result<SignalSubscription> subscription =
        signal.subscribe([&seen](const DamageEvent& event) { seen = event; });
    ASSERT_TRUE(subscription.has_value());

    ASSERT_TRUE(signal.emit(DamageEvent{.amount = 42}).has_value());
    EXPECT_EQ(seen, DamageEvent{.amount = 42});
}

// Destroying the token unsubscribes. There is no unsubscribe-by-callback, because the
// failure this prevents -- a State that exited while its callback still points into
// freed members -- cannot be expressed by callback identity.
TEST(SignalTests, DestroyingTheTokenUnsubscribes)
{
    Signal<Unit> signal = makeSignal<Unit>();
    int deliveries = 0;
    {
        Core::Result<SignalSubscription> subscription =
            signal.subscribe([&deliveries](const Unit&) { ++deliveries; });
        ASSERT_TRUE(subscription.has_value());
        EXPECT_TRUE(subscription->isActive());
        ASSERT_TRUE(signal.emit(Unit{}).has_value());
        EXPECT_EQ(deliveries, 1);
    }

    Core::Result<Core::usize> delivered = signal.emit(Unit{});
    ASSERT_TRUE(delivered.has_value());
    EXPECT_EQ(*delivered, 0U);
    EXPECT_EQ(deliveries, 1);
    EXPECT_EQ(signal.subscriberCount(), 0U);
    EXPECT_EQ(signal.stats().unsubscribedCount, 1U);
}

// A State's teardown order is not always the reverse of its construction order, so a
// token outliving its signal must be safe. The token holds a weak reference, making
// reset a no-op rather than a dangling write.
TEST(SignalTests, ATokenOutlivingItsSignalResetsSafely)
{
    SignalSubscription token;
    {
        Signal<Unit> signal = makeSignal<Unit>();
        Core::Result<SignalSubscription> subscription = signal.subscribe([](const Unit&) {});
        ASSERT_TRUE(subscription.has_value());
        token = std::move(*subscription);
        EXPECT_TRUE(token.isActive());
    }

    EXPECT_FALSE(token.isActive());
    token.reset();
    EXPECT_FALSE(token.isActive());
}

TEST(SignalTests, AMovedFromTokenNoLongerOwnsTheSubscription)
{
    Signal<Unit> signal = makeSignal<Unit>();
    int deliveries = 0;
    Core::Result<SignalSubscription> subscription =
        signal.subscribe([&deliveries](const Unit&) { ++deliveries; });
    ASSERT_TRUE(subscription.has_value());

    SignalSubscription moved = std::move(*subscription);
    EXPECT_FALSE(subscription->isActive());
    EXPECT_TRUE(moved.isActive());

    // The source token going out of scope must not unsubscribe what it handed over.
    subscription.value().reset();
    ASSERT_TRUE(signal.emit(Unit{}).has_value());
    EXPECT_EQ(deliveries, 1);

    moved.reset();
    ASSERT_TRUE(signal.emit(Unit{}).has_value());
    EXPECT_EQ(deliveries, 1);
}

// Re-entering emit() from a subscriber is refused. Allowing it would make delivery
// order depend on how deeply the subscribers nested; post() is the supported way.
TEST(SignalTests, EmitRefusesToBeReenteredFromASubscriber)
{
    Signal<Unit> signal = makeSignal<Unit>();
    Core::ErrorCode innerCode{};
    bool innerAttempted = false;
    Core::Result<SignalSubscription> subscription =
        signal.subscribe([&](const Unit&) {
            if (innerAttempted) {
                return;
            }
            innerAttempted = true;
            Core::Result<Core::usize> inner = signal.emit(Unit{});
            ASSERT_FALSE(inner.has_value());
            innerCode = inner.error().code;
        });
    ASSERT_TRUE(subscription.has_value());

    ASSERT_TRUE(signal.emit(Unit{}).has_value());
    EXPECT_TRUE(innerAttempted);
    EXPECT_EQ(innerCode, GameplayErrorCode::ReentrantDispatch);

    // Dispatch was left clean, so a later emit still works.
    ASSERT_TRUE(signal.emit(Unit{}).has_value());
}

// A subscriber is game code and may throw. The dispatch flag is restored by a scope
// guard, so a throwing subscriber does not leave the signal answering
// ReentrantDispatch for the rest of the process.
TEST(SignalTests, AThrowingSubscriberDoesNotLeaveTheSignalWedged)
{
    Signal<Unit> signal = makeSignal<Unit>();
    int deliveries = 0;
    Core::Result<SignalSubscription> subscription =
        signal.subscribe([&deliveries](const Unit&) {
            ++deliveries;
            if (deliveries == 1) {
                throw std::runtime_error("subscriber failed");
            }
        });
    ASSERT_TRUE(subscription.has_value());

    EXPECT_THROW(static_cast<void>(signal.emit(Unit{})), std::runtime_error);
    EXPECT_EQ(deliveries, 1);

    Core::Result<Core::usize> afterThrow = signal.emit(Unit{});
    ASSERT_TRUE(afterThrow.has_value());
    EXPECT_EQ(deliveries, 2);
}

// A subscription made during a dispatch first receives the *next* one. Otherwise a
// subscriber that subscribes another would be delivered to or skipped depending on
// slot reuse order, which is not something a game can reason about.
TEST(SignalTests, ASubscriptionMadeDuringDispatchFirstReceivesTheNextOne)
{
    Signal<Unit> signal = makeSignal<Unit>();
    int nested = 0;
    SignalSubscription nestedToken;
    bool subscribedOnce = false;
    Core::Result<SignalSubscription> outer = signal.subscribe([&](const Unit&) {
        if (subscribedOnce) {
            return;
        }
        subscribedOnce = true;
        Core::Result<SignalSubscription> inner =
            signal.subscribe([&nested](const Unit&) { ++nested; });
        ASSERT_TRUE(inner.has_value());
        nestedToken = std::move(*inner);
    });
    ASSERT_TRUE(outer.has_value());

    Core::Result<Core::usize> first = signal.emit(Unit{});
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, 1U);
    EXPECT_EQ(nested, 0);

    Core::Result<Core::usize> second = signal.emit(Unit{});
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, 2U);
    EXPECT_EQ(nested, 1);
}

// A subscriber may unsubscribe itself. The slot cannot be released inside the loop --
// that would destroy the callback currently executing -- so it is marked and reclaimed
// after the dispatch unwinds, while isActive() reports the truth immediately.
TEST(SignalTests, ASubscriberMayUnsubscribeItselfDuringDispatch)
{
    Signal<Unit> signal = makeSignal<Unit>();
    int deliveries = 0;
    SignalSubscription token;
    Core::Result<SignalSubscription> subscription = signal.subscribe([&](const Unit&) {
        ++deliveries;
        token.reset();
        EXPECT_FALSE(token.isActive());
    });
    ASSERT_TRUE(subscription.has_value());
    token = std::move(*subscription);

    ASSERT_TRUE(signal.emit(Unit{}).has_value());
    EXPECT_EQ(deliveries, 1);
    EXPECT_EQ(signal.subscriberCount(), 0U);

    Core::Result<Core::usize> afterRemoval = signal.emit(Unit{});
    ASSERT_TRUE(afterRemoval.has_value());
    EXPECT_EQ(*afterRemoval, 0U);
    EXPECT_EQ(deliveries, 1);
}

// A subscriber removed during a dispatch receives nothing further in that same
// dispatch, even if its slot has not been visited yet.
TEST(SignalTests, ASubscriberRemovedDuringDispatchIsSkipped)
{
    Signal<Unit> signal = makeSignal<Unit>();
    int firstDeliveries = 0;
    int secondDeliveries = 0;
    SignalSubscription secondToken;

    Core::Result<SignalSubscription> first = signal.subscribe([&](const Unit&) {
        ++firstDeliveries;
        secondToken.reset();
    });
    ASSERT_TRUE(first.has_value());

    Core::Result<SignalSubscription> second =
        signal.subscribe([&secondDeliveries](const Unit&) { ++secondDeliveries; });
    ASSERT_TRUE(second.has_value());
    secondToken = std::move(*second);

    Core::Result<Core::usize> delivered = signal.emit(Unit{});
    ASSERT_TRUE(delivered.has_value());
    EXPECT_EQ(*delivered, 1U);
    EXPECT_EQ(firstDeliveries, 1);
    EXPECT_EQ(secondDeliveries, 0);
}

// A slot freed during dispatch is reusable afterwards, so unsubscribing and
// resubscribing across frames does not leak capacity.
TEST(SignalTests, ASlotFreedDuringDispatchIsReusable)
{
    Signal<Unit> signal = makeSignal<Unit>(SignalConfig{.initialSubscriberReserve = 1});
    SignalSubscription token;
    Core::Result<SignalSubscription> first = signal.subscribe([&](const Unit&) { token.reset(); });
    ASSERT_TRUE(first.has_value());
    token = std::move(*first);

    ASSERT_TRUE(signal.emit(Unit{}).has_value());
    EXPECT_EQ(signal.subscriberCount(), 0U);

    // Reusing the retired slot should not grow storage.
    int replacement = 0;
    Core::Result<SignalSubscription> second =
        signal.subscribe([&replacement](const Unit&) { ++replacement; });
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(signal.stats().reservedSubscriberSlots, 1U);
    ASSERT_TRUE(signal.emit(Unit{}).has_value());
    EXPECT_EQ(replacement, 1);
}

// Pending removals retain executing callbacks; growth uses a different stable block.
TEST(SignalTests, SubscribingWhileARemovalIsPendingGrowsStableStorage)
{
    Signal<Unit> signal = makeSignal<Unit>(SignalConfig{.initialSubscriberReserve = 2});
    SignalSubscription victimToken;
    Core::ErrorCode replacementCode{};
    bool attempted = false;
    int replacementDeliveries = 0;

    Core::Result<SignalSubscription> canceller = signal.subscribe([&](const Unit&) {
        if (attempted) {
            return;
        }
        attempted = true;
        // Frees one logical subscriber slot, but the slot itself stays occupied until
        // this dispatch ends.
        victimToken.reset();
        EXPECT_EQ(signal.subscriberCount(), 1U);

        Core::Result<SignalSubscription> replacement =
            signal.subscribe([&replacementDeliveries](const Unit&) { ++replacementDeliveries; });
        if (replacement.has_value()) {
            replacementCode = Core::ErrorCode{};
            victimToken = std::move(*replacement);
        } else {
            replacementCode = replacement.error().code;
        }
    });
    ASSERT_TRUE(canceller.has_value());

    Core::Result<SignalSubscription> victim = signal.subscribe([](const Unit&) {});
    ASSERT_TRUE(victim.has_value());
    victimToken = std::move(*victim);

    ASSERT_TRUE(signal.emit(Unit{}).has_value());
    EXPECT_TRUE(attempted);
    EXPECT_EQ(replacementCode, Core::ErrorCode{});
    EXPECT_GE(signal.stats().reservedSubscriberSlots, 3U);
    EXPECT_EQ(replacementDeliveries, 0);

    // Once the dispatch has unwound the slot is reclaimed, so the same subscribe now
    // succeeds by reusing it rather than by growing storage.
    Core::Result<SignalSubscription> afterDispatch =
        signal.subscribe([&replacementDeliveries](const Unit&) { ++replacementDeliveries; });
    ASSERT_TRUE(afterDispatch.has_value());
    ASSERT_TRUE(signal.emit(Unit{}).has_value());
    EXPECT_EQ(replacementDeliveries, 2);
}

// post() on an immediate-only signal is refused rather than silently promoted to
// emit(): the two differ in exactly the ordering the caller chose post() to get.
TEST(SignalTests, PostIsRefusedWhenNoDeferredQueueWasRequested)
{
    Signal<Unit> signal = makeSignal<Unit>(SignalConfig{.deferredCapacity = 0});
    int deliveries = 0;
    Core::Result<SignalSubscription> subscription =
        signal.subscribe([&deliveries](const Unit&) { ++deliveries; });
    ASSERT_TRUE(subscription.has_value());

    Core::Status posted = signal.post(Unit{});
    ASSERT_FALSE(posted.has_value());
    EXPECT_EQ(posted.error().code, GameplayErrorCode::DeferredDeliveryUnavailable);
    EXPECT_EQ(deliveries, 0);
    EXPECT_EQ(signal.queuedCount(), 0U);
}

TEST(SignalTests, PostQueuesUntilDrainAndDrainReportsPayloadCount)
{
    Signal<DamageEvent> signal =
        makeSignal<DamageEvent>(SignalConfig{.deferredCapacity = 4});
    std::vector<int> seen;
    Core::Result<SignalSubscription> subscription = signal.subscribe(
        [&seen](const DamageEvent& event) { seen.push_back(event.amount); });
    ASSERT_TRUE(subscription.has_value());

    ASSERT_TRUE(signal.post(DamageEvent{.amount = 1}).has_value());
    ASSERT_TRUE(signal.post(DamageEvent{.amount = 2}).has_value());
    EXPECT_TRUE(seen.empty());
    EXPECT_EQ(signal.queuedCount(), 2U);

    // The count is payloads dispatched, not subscribers run.
    Core::Result<Core::usize> drained = signal.drain();
    ASSERT_TRUE(drained.has_value());
    EXPECT_EQ(*drained, 2U);
    EXPECT_EQ(seen, std::vector<int>({1, 2}));
    EXPECT_EQ(signal.queuedCount(), 0U);
    EXPECT_EQ(signal.stats().postCount, 2U);
    EXPECT_EQ(signal.stats().drainCount, 1U);
}

TEST(SignalTests, PostFailsClosedWhenTheDeferredQueueIsFull)
{
    Signal<Unit> signal = makeSignal<Unit>(SignalConfig{.deferredCapacity = 2});
    ASSERT_TRUE(signal.post(Unit{}).has_value());
    ASSERT_TRUE(signal.post(Unit{}).has_value());

    Core::Status overflow = signal.post(Unit{});
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, GameplayErrorCode::CapacityExceeded);
    EXPECT_EQ(signal.queuedCount(), 2U);
    EXPECT_EQ(signal.stats().queuedHighWater, 2U);
}

// A payload posted by a subscriber during a drain stays queued for the next one. That
// is what keeps a signal which re-posts itself from running forever inside one frame.
TEST(SignalTests, APayloadPostedDuringDrainWaitsForTheNextDrain)
{
    Signal<DamageEvent> signal =
        makeSignal<DamageEvent>(SignalConfig{.deferredCapacity = 4});
    std::vector<int> seen;
    Core::Result<SignalSubscription> subscription =
        signal.subscribe([&](const DamageEvent& event) {
            seen.push_back(event.amount);
            if (event.amount == 1) {
                EXPECT_TRUE(signal.post(DamageEvent{.amount = 2}).has_value());
            }
        });
    ASSERT_TRUE(subscription.has_value());

    ASSERT_TRUE(signal.post(DamageEvent{.amount = 1}).has_value());
    Core::Result<Core::usize> firstDrain = signal.drain();
    ASSERT_TRUE(firstDrain.has_value());
    EXPECT_EQ(*firstDrain, 1U);
    EXPECT_EQ(seen, std::vector<int>({1}));
    EXPECT_EQ(signal.queuedCount(), 1U);

    Core::Result<Core::usize> secondDrain = signal.drain();
    ASSERT_TRUE(secondDrain.has_value());
    EXPECT_EQ(*secondDrain, 1U);
    EXPECT_EQ(seen, std::vector<int>({1, 2}));
    EXPECT_EQ(signal.queuedCount(), 0U);
}

TEST(SignalTests, DrainIsRefusedFromInsideADispatch)
{
    Signal<Unit> signal = makeSignal<Unit>(SignalConfig{.deferredCapacity = 2});
    Core::ErrorCode innerCode{};
    bool attempted = false;
    Core::Result<SignalSubscription> subscription = signal.subscribe([&](const Unit&) {
        if (attempted) {
            return;
        }
        attempted = true;
        Core::Result<Core::usize> inner = signal.drain();
        ASSERT_FALSE(inner.has_value());
        innerCode = inner.error().code;
    });
    ASSERT_TRUE(subscription.has_value());

    ASSERT_TRUE(signal.emit(Unit{}).has_value());
    EXPECT_TRUE(attempted);
    EXPECT_EQ(innerCode, GameplayErrorCode::ReentrantDispatch);
}

// For a State that is exiting and must not run gameplay reactions to events it will
// never see.
TEST(SignalTests, ClearQueuedDropsPayloadsWithoutDelivering)
{
    Signal<Unit> signal = makeSignal<Unit>(SignalConfig{.deferredCapacity = 4});
    int deliveries = 0;
    Core::Result<SignalSubscription> subscription =
        signal.subscribe([&deliveries](const Unit&) { ++deliveries; });
    ASSERT_TRUE(subscription.has_value());

    ASSERT_TRUE(signal.post(Unit{}).has_value());
    ASSERT_TRUE(signal.post(Unit{}).has_value());
    signal.clearQueued();
    EXPECT_EQ(signal.queuedCount(), 0U);

    Core::Result<Core::usize> drained = signal.drain();
    ASSERT_TRUE(drained.has_value());
    EXPECT_EQ(*drained, 0U);
    EXPECT_EQ(deliveries, 0);
}

// Subscribers within the initial reserve and the bounded payload ring reuse storage.
TEST(SignalTests, ReservedSubscribersAndDeferredRingReuseStorage)
{
    CountingMemoryResource resource;
    Signal<DamageEvent> signal = makeSignal<DamageEvent>(SignalConfig{
        .initialSubscriberReserve = 4,
        .deferredCapacity = 4,
        .memoryResource = &resource,
    });
    EXPECT_GT(resource.allocationCount(), 0U);
    resource.resetCount();

    std::vector<SignalSubscription> tokens;
    for (int index = 0; index < 4; ++index) {
        Core::Result<SignalSubscription> subscription =
            signal.subscribe([](const DamageEvent&) {});
        ASSERT_TRUE(subscription.has_value());
        tokens.push_back(std::move(*subscription));
    }
    EXPECT_EQ(resource.allocationCount(), 0U);

    ASSERT_TRUE(signal.emit(DamageEvent{.amount = 1}).has_value());
    ASSERT_TRUE(signal.post(DamageEvent{.amount = 2}).has_value());
    ASSERT_TRUE(signal.drain().has_value());
    EXPECT_EQ(resource.allocationCount(), 0U);

    tokens.clear();
    EXPECT_EQ(resource.allocationCount(), 0U);

    // Slot reuse after the tokens were dropped must also stay allocation-free.
    Core::Result<SignalSubscription> reused = signal.subscribe([](const DamageEvent&) {});
    ASSERT_TRUE(reused.has_value());
    EXPECT_EQ(resource.allocationCount(), 0U);
}

TEST(SignalTests, StatsSeparateTheLiveCountFromThePeak)
{
    Signal<Unit> signal = makeSignal<Unit>(SignalConfig{.initialSubscriberReserve = 8});
    {
        std::vector<SignalSubscription> tokens;
        for (int index = 0; index < 3; ++index) {
            Core::Result<SignalSubscription> subscription = signal.subscribe([](const Unit&) {});
            ASSERT_TRUE(subscription.has_value());
            tokens.push_back(std::move(*subscription));
        }
        EXPECT_EQ(signal.stats().subscriberCount, 3U);
        EXPECT_EQ(signal.stats().subscriberHighWater, 3U);
    }

    EXPECT_EQ(signal.stats().subscriberCount, 0U);
    EXPECT_EQ(signal.stats().subscriberHighWater, 3U);
    EXPECT_EQ(signal.stats().unsubscribedCount, 3U);
    EXPECT_EQ(signal.stats().reservedSubscriberSlots, 8U);
}

// Unit exists so "something happened" is Signal<Unit> rather than a separate untyped
// class: one dispatch rule, one set of stats, one reentrancy contract.
TEST(SignalTests, AUnitSignalCarriesNoPayloadButFollowsTheSameRules)
{
    Signal<Unit> signal = makeSignal<Unit>(SignalConfig{.deferredCapacity = 1});
    int deliveries = 0;
    Core::Result<SignalSubscription> subscription =
        signal.subscribe([&deliveries](const Unit&) { ++deliveries; });
    ASSERT_TRUE(subscription.has_value());

    ASSERT_TRUE(signal.emit(Unit{}).has_value());
    ASSERT_TRUE(signal.post(Unit{}).has_value());
    ASSERT_TRUE(signal.drain().has_value());
    EXPECT_EQ(deliveries, 2);
}

TEST(SignalTests, ReusingAnEarlierSlotDoesNotReorderSubscribers)
{
    auto signal = makeSignal<Unit>({.initialSubscriberReserve = 2});
    std::vector<int> order;
    auto first = signal.subscribe([&](const Unit&) { order.push_back(1); });
    auto second = signal.subscribe([&](const Unit&) { order.push_back(2); });
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    first->reset();
    auto third = signal.subscribe([&](const Unit&) { order.push_back(3); });
    ASSERT_TRUE(third);
    ASSERT_TRUE(signal.emit({}));
    EXPECT_EQ(order, std::vector<int>({2, 3}));
}

TEST(SignalTests, ClearDuringDrainPreservesTheInFlightPayloadAndDefersReposts)
{
    auto signal = makeSignal<std::unique_ptr<int>>({.deferredCapacity = 3});
    std::vector<int> observed;
    auto first = signal.subscribe([&](const std::unique_ptr<int>& payload) {
        ASSERT_NE(payload, nullptr);
        observed.push_back(*payload);
        if (*payload == 1) {
            signal.clearQueued();
            ASSERT_TRUE(signal.post(std::make_unique<int>(3)));
            EXPECT_EQ(*payload, 1);
        }
    });
    auto second = signal.subscribe([&](const std::unique_ptr<int>& payload) {
        ASSERT_NE(payload, nullptr);
        observed.push_back(*payload);
    });
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(signal.post(std::make_unique<int>(1)));
    ASSERT_TRUE(signal.post(std::make_unique<int>(2)));
    auto drained = signal.drain();
    ASSERT_TRUE(drained);
    EXPECT_EQ(*drained, 1U);
    EXPECT_EQ(observed, std::vector<int>({1, 1}));
    EXPECT_EQ(signal.queuedCount(), 1U);
    ASSERT_TRUE(signal.drain());
    EXPECT_EQ(observed, std::vector<int>({1, 1, 3, 3}));
}

TEST(SignalTests, ThrowingDrainConsumesItsCurrentPayloadOnceAndKeepsLaterMessages)
{
    auto signal = makeSignal<std::unique_ptr<int>>({.deferredCapacity = 3});
    std::vector<int> observed;
    auto token = signal.subscribe([&](const std::unique_ptr<int>& payload) {
        observed.push_back(*payload);
        if (*payload == 1) { throw std::runtime_error("delivery"); }
        if (*payload == 2) {
            signal.clearQueued();
            EXPECT_TRUE(signal.post(std::make_unique<int>(4)));
            throw std::runtime_error("clear and repost");
        }
    });
    ASSERT_TRUE(token);
    for (int value : {1, 2, 3}) { ASSERT_TRUE(signal.post(std::make_unique<int>(value))); }
    EXPECT_THROW((void)signal.drain(), std::runtime_error);
    EXPECT_EQ(signal.queuedCount(), 2U);
    EXPECT_THROW((void)signal.drain(), std::runtime_error);
    EXPECT_EQ(signal.queuedCount(), 1U);
    ASSERT_TRUE(signal.drain());
    EXPECT_EQ(observed, std::vector<int>({1, 2, 4}));
    EXPECT_EQ(signal.queuedCount(), 0U);
}

TEST(SignalTests, FacadeResetDuringDeliveryKeepsTheCurrentCallbacksAlive)
{
    auto signal = makeSignal<Unit>();
    int delivered = 0;
    auto first = signal.subscribe([&](const Unit&) { ++delivered; signal = Signal<Unit>{}; });
    auto second = signal.subscribe([&](const Unit&) { ++delivered; });
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    auto emitted = signal.emit({});
    ASSERT_TRUE(emitted);
    EXPECT_EQ(*emitted, 2U);
    EXPECT_EQ(delivered, 2);
    EXPECT_FALSE(signal);
    EXPECT_FALSE(first->isActive());
    EXPECT_FALSE(second->isActive());
}

TEST(SignalTests, FailedGrowthLeavesSubscriptionsUsable)
{
    TestSupport::FailingMemoryResource resource;
    auto signal = makeSignal<Unit>({.initialSubscriberReserve = 1, .memoryResource = &resource});
    int delivered = 0;
    auto first = signal.subscribe([&](const Unit&) { ++delivered; });
    ASSERT_TRUE(first);
    resource.failAt = resource.allocations;
    EXPECT_FALSE(signal.subscribe([](const Unit&) {}));
    EXPECT_TRUE(first->isActive());
    ASSERT_TRUE(signal.emit({}));
    EXPECT_EQ(delivered, 1);
    EXPECT_EQ(signal.subscriberCount(), 1U);
}

namespace {
struct DestructionPayload final {
    Core::MoveOnlyFunction<void()> onDestroy{};
    DestructionPayload() = default;
    explicit DestructionPayload(Core::MoveOnlyFunction<void()> callback) : onDestroy(std::move(callback)) {}
    DestructionPayload(DestructionPayload&&) noexcept = default;
    DestructionPayload& operator=(DestructionPayload&&) noexcept = default;
    ~DestructionPayload() noexcept { if (onDestroy) { onDestroy(); } }
};
}

TEST(SignalTests, PayloadDestructionCannotReenterRingMutation)
{
    auto signal = makeSignal<DestructionPayload>({.deferredCapacity = 2});
    int destructed = 0;
    ASSERT_TRUE(signal.post(DestructionPayload{[&] {
        ++destructed;
        signal.clearQueued();
        auto posted = signal.post(DestructionPayload{});
        EXPECT_FALSE(posted);
        if (!posted) { EXPECT_EQ(posted.error().code, GameplayErrorCode::ReentrantDispatch); }
    }}));
    ASSERT_TRUE(signal.post(DestructionPayload{}));
    auto drained = signal.drain();
    ASSERT_TRUE(drained);
    EXPECT_EQ(*drained, 1U);
    EXPECT_EQ(destructed, 1);
    EXPECT_EQ(signal.queuedCount(), 0U);
}

TEST(SignalTests, TokenAssignmentDoesNotOverwriteAReentrantCaptureReplacement)
{
    auto signal = makeSignal<Unit>({.initialSubscriberReserve = 1});
    SignalSubscription token;
    std::vector<int> delivered;
    auto first = signal.subscribe([owner = DestructionPayload{[&] {
        auto replacement = signal.subscribe([&](const Unit&) { delivered.push_back(3); });
        EXPECT_TRUE(replacement);
        if (replacement) { token = std::move(*replacement); }
    }}](const Unit&) { (void)owner; });
    ASSERT_TRUE(first);
    token = std::move(*first);
    auto incoming = signal.subscribe([&](const Unit&) { delivered.push_back(2); });
    ASSERT_TRUE(incoming);
    token = std::move(*incoming);
    EXPECT_TRUE(token.isActive());
    EXPECT_EQ(signal.subscriberCount(), 1U);
    ASSERT_TRUE(signal.emit({}));
    EXPECT_EQ(delivered, std::vector<int>({3}));
}

} // namespace Tina::Gameplay
