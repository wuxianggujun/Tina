#include <tina/core/id/GenerationPool.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/runtime/spi/PlatformEventDispatcher.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Tina {

struct PlatformEventDispatcherTestAccess final {
    static Core::Result<PlatformEventSubscription> subscribe(PlatformEventDispatcher& dispatcher,
                                                             PlatformEventCallback callback)
    {
        return dispatcher.subscribe(std::move(callback));
    }

    static Core::Status dispatch(PlatformEventDispatcher& dispatcher, std::span<const Platform::PlatformEvent> events,
                                 std::span<const Platform::WindowFrameSnapshot> windows = {},
                                 std::span<const Platform::GamepadSnapshot> gamepads = {}) noexcept
    {
        return dispatcher.dispatch(events, windows, gamepads);
    }
};

namespace Tests {
namespace {

[[nodiscard]] Platform::PlatformEvent metricsEvent(u64 sequence = 1)
{
    return Platform::PlatformEvent{
        .sequence = sequence,
        .payload = Platform::WindowMetricsChangedEvent{.metricsRevision = sequence},
    };
}

[[nodiscard]] PlatformEventSubscriptionConfig eventSubscriptionConfig(u32 subscriberCapacity) noexcept
{
    return PlatformEventSubscriptionConfig{
        .subscriberCapacity = subscriberCapacity,
    };
}

} // namespace

TEST(PlatformEventDispatcherTest, RejectsInvalidCapacityAndEmptyCallback)
{
    EXPECT_FALSE(PlatformEventDispatcher::Create(eventSubscriptionConfig(0)).has_value());
    EXPECT_FALSE(PlatformEventDispatcher::Create(
                     eventSubscriptionConfig(PlatformEventSubscriptionConfig::MaximumSubscriberCapacity + 1))
                     .has_value());

    auto dispatcher = PlatformEventDispatcher::Create(eventSubscriptionConfig(1));
    ASSERT_TRUE(dispatcher.has_value());
    EXPECT_FALSE(PlatformEventDispatcherTestAccess::subscribe(*dispatcher, {}).has_value());
}

TEST(PlatformEventDispatcherTest, SubscriptionIsMoveOnlyAndUnsubscribesOnDestruction)
{
    auto dispatcher = PlatformEventDispatcher::Create(eventSubscriptionConfig(2));
    ASSERT_TRUE(dispatcher.has_value());
    u32 calls = 0;
    {
        auto token = PlatformEventDispatcherTestAccess::subscribe(
            *dispatcher, [&calls](const PlatformEventNotification&) { ++calls; });
        ASSERT_TRUE(token.has_value());
        EXPECT_TRUE(token->isActive());
        EXPECT_EQ(dispatcher->subscriberCount(), 1U);
        const auto event = metricsEvent();
        EXPECT_TRUE(PlatformEventDispatcherTestAccess::dispatch(*dispatcher, {&event, 1}).has_value());
        EXPECT_EQ(calls, 1U);
    }
    EXPECT_EQ(dispatcher->subscriberCount(), 0U);
}

TEST(PlatformEventDispatcherTest, CapacityCanBeReusedWithoutReactivatingAStaleToken)
{
    auto dispatcher = PlatformEventDispatcher::Create(eventSubscriptionConfig(1));
    ASSERT_TRUE(dispatcher.has_value());
    u32 replacementCalls = 0;
    auto first = PlatformEventDispatcherTestAccess::subscribe(*dispatcher, [](const PlatformEventNotification&) {});
    ASSERT_TRUE(first.has_value());

    auto exhausted = PlatformEventDispatcherTestAccess::subscribe(*dispatcher, [](const PlatformEventNotification&) {});
    ASSERT_FALSE(exhausted.has_value());
    EXPECT_EQ(exhausted.error().code, Core::CoreErrorCode::CapacityExceeded);

    first->reset();
    EXPECT_FALSE(first->isActive());
    auto replacement = PlatformEventDispatcherTestAccess::subscribe(
        *dispatcher, [&replacementCalls](const PlatformEventNotification&) { ++replacementCalls; });
    ASSERT_TRUE(replacement.has_value());
    EXPECT_TRUE(replacement->isActive());

    first->reset();
    const auto event = metricsEvent();
    ASSERT_TRUE(PlatformEventDispatcherTestAccess::dispatch(*dispatcher, {&event, 1}).has_value());
    EXPECT_EQ(replacementCalls, 1U);
}

TEST(PlatformEventDispatcherTest, TokenRemainsSafeWhenDispatcherDiesFirst)
{
    auto dispatcher = PlatformEventDispatcher::Create(eventSubscriptionConfig(1));
    ASSERT_TRUE(dispatcher.has_value());
    auto token = PlatformEventDispatcherTestAccess::subscribe(*dispatcher, [](const PlatformEventNotification&) {});
    ASSERT_TRUE(token.has_value());

    dispatcher = PlatformEventDispatcher::Create(eventSubscriptionConfig(1));

    EXPECT_FALSE(token->isActive());
    token->reset();
}

TEST(PlatformEventDispatcherTest, CallbackCanDestroyItsOwnToken)
{
    auto dispatcher = PlatformEventDispatcher::Create(eventSubscriptionConfig(1));
    ASSERT_TRUE(dispatcher.has_value());
    std::unique_ptr<PlatformEventSubscription> token;
    u32 calls = 0;
    auto subscription =
        PlatformEventDispatcherTestAccess::subscribe(*dispatcher, [&](const PlatformEventNotification&) {
            ++calls;
            token.reset();
        });
    ASSERT_TRUE(subscription.has_value());
    token = std::make_unique<PlatformEventSubscription>(std::move(*subscription));

    const Platform::PlatformEvent events[] = {metricsEvent(1), metricsEvent(2)};
    EXPECT_TRUE(PlatformEventDispatcherTestAccess::dispatch(*dispatcher, events).has_value());
    EXPECT_EQ(calls, 1U);
    EXPECT_EQ(dispatcher->subscriberCount(), 0U);
}

TEST(PlatformEventDispatcherTest, CallbackCanReplaceTheDispatcherWithoutCrossingState)
{
    auto dispatcher = PlatformEventDispatcher::Create(eventSubscriptionConfig(2));
    ASSERT_TRUE(dispatcher.has_value());
    u32 replacingCalls = 0;
    u32 removedCalls = 0;
    auto replacingToken =
        PlatformEventDispatcherTestAccess::subscribe(*dispatcher, [&](const PlatformEventNotification&) {
            ++replacingCalls;
            dispatcher = PlatformEventDispatcher::Create(eventSubscriptionConfig(1));
        });
    ASSERT_TRUE(replacingToken.has_value());
    auto removedToken = PlatformEventDispatcherTestAccess::subscribe(
        *dispatcher, [&](const PlatformEventNotification&) { ++removedCalls; });
    ASSERT_TRUE(removedToken.has_value());

    const Platform::PlatformEvent events[] = {metricsEvent(1), metricsEvent(2)};
    EXPECT_TRUE(PlatformEventDispatcherTestAccess::dispatch(*dispatcher, events).has_value());
    EXPECT_EQ(replacingCalls, 1U);
    EXPECT_EQ(removedCalls, 0U);
    EXPECT_FALSE(replacingToken->isActive());
    EXPECT_FALSE(removedToken->isActive());
    EXPECT_EQ(dispatcher->capacity(), 1U);
}

TEST(PlatformEventDispatcherTest, SubscriptionAddedDuringCallbackStartsWithNextEvent)
{
    auto dispatcher = PlatformEventDispatcher::Create(eventSubscriptionConfig(2));
    ASSERT_TRUE(dispatcher.has_value());
    u32 firstCalls = 0;
    u32 secondCalls = 0;
    std::unique_ptr<PlatformEventSubscription> secondToken;
    auto firstToken = PlatformEventDispatcherTestAccess::subscribe(*dispatcher, [&](const PlatformEventNotification&) {
        ++firstCalls;
        if (secondToken == nullptr)
        {
            auto added = PlatformEventDispatcherTestAccess::subscribe(
                *dispatcher, [&secondCalls](const PlatformEventNotification&) { ++secondCalls; });
            ASSERT_TRUE(added.has_value());
            secondToken = std::make_unique<PlatformEventSubscription>(std::move(*added));
        }
    });
    ASSERT_TRUE(firstToken.has_value());

    const Platform::PlatformEvent events[] = {metricsEvent(1), metricsEvent(2)};
    ASSERT_TRUE(PlatformEventDispatcherTestAccess::dispatch(*dispatcher, events).has_value());
    EXPECT_EQ(firstCalls, 2U);
    EXPECT_EQ(secondCalls, 1U);
}

TEST(PlatformEventDispatcherTest, DispatchesInActivationOrderAfterSlotReuse)
{
    auto dispatcher = PlatformEventDispatcher::Create(eventSubscriptionConfig(2));
    ASSERT_TRUE(dispatcher.has_value());
    std::vector<char> order;
    auto first = PlatformEventDispatcherTestAccess::subscribe(
        *dispatcher, [&](const PlatformEventNotification&) { order.push_back('A'); });
    auto second = PlatformEventDispatcherTestAccess::subscribe(
        *dispatcher, [&](const PlatformEventNotification&) { order.push_back('B'); });
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    first->reset();
    auto third = PlatformEventDispatcherTestAccess::subscribe(
        *dispatcher, [&](const PlatformEventNotification&) { order.push_back('C'); });
    ASSERT_TRUE(third.has_value());
    const auto event = metricsEvent();

    ASSERT_TRUE(PlatformEventDispatcherTestAccess::dispatch(*dispatcher, {&event, 1}).has_value());

    EXPECT_EQ(order, std::vector<char>({'B', 'C'}));
}

TEST(PlatformEventDispatcherTest, CancellationImmediatelySkipsALaterListener)
{
    auto dispatcher = PlatformEventDispatcher::Create(eventSubscriptionConfig(2));
    ASSERT_TRUE(dispatcher.has_value());
    u32 laterCalls = 0;
    std::optional<PlatformEventSubscription> later;
    auto first = PlatformEventDispatcherTestAccess::subscribe(*dispatcher,
                                                              [&](const PlatformEventNotification&) { later.reset(); });
    ASSERT_TRUE(first.has_value());
    auto laterResult = PlatformEventDispatcherTestAccess::subscribe(
        *dispatcher, [&](const PlatformEventNotification&) { ++laterCalls; });
    ASSERT_TRUE(laterResult.has_value());
    later.emplace(std::move(*laterResult));
    const auto event = metricsEvent();

    ASSERT_TRUE(PlatformEventDispatcherTestAccess::dispatch(*dispatcher, {&event, 1}).has_value());

    EXPECT_EQ(laterCalls, 0U);
}

TEST(PlatformEventDispatcherTest, CallbackExceptionBecomesStructuredFailure)
{
    auto dispatcher = PlatformEventDispatcher::Create(eventSubscriptionConfig(1));
    ASSERT_TRUE(dispatcher.has_value());
    auto token = PlatformEventDispatcherTestAccess::subscribe(
        *dispatcher, [](const PlatformEventNotification&) { throw std::runtime_error("boom"); });
    ASSERT_TRUE(token.has_value());
    const auto event = metricsEvent();

    auto status = PlatformEventDispatcherTestAccess::dispatch(*dispatcher, {&event, 1});

    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, RuntimeErrorCode::PlatformEventCallbackThrewException);
    ASSERT_FALSE(status.error().context.empty());
    EXPECT_EQ(status.error().context.back().operation, "PlatformEventDispatcher::dispatch");
    EXPECT_EQ(status.error().context.back().detail, "eventSequence=1");
}

TEST(PlatformEventDispatcherTest, RecursiveDispatchIsRejectedWithoutStoppingOuterDispatch)
{
    auto dispatcher = PlatformEventDispatcher::Create(eventSubscriptionConfig(1));
    ASSERT_TRUE(dispatcher.has_value());
    std::optional<Core::ErrorCode> recursiveFailure;
    auto token =
        PlatformEventDispatcherTestAccess::subscribe(*dispatcher, [&](const PlatformEventNotification& notification) {
            const Platform::PlatformEvent& event = notification.event();
            auto nested = PlatformEventDispatcherTestAccess::dispatch(dispatcher.value(), {&event, 1});
            ASSERT_FALSE(nested.has_value());
            recursiveFailure = nested.error().code;
        });
    ASSERT_TRUE(token.has_value());
    const auto event = metricsEvent();

    auto outer = PlatformEventDispatcherTestAccess::dispatch(*dispatcher, {&event, 1});

    EXPECT_TRUE(outer.has_value());
    EXPECT_EQ(recursiveFailure, RuntimeErrorCode::RecursivePlatformEventDispatch);
}

TEST(PlatformEventDispatcherTest, NotificationExposesFinalLifecycleSnapshotForResetRecovery)
{
    auto dispatcher = PlatformEventDispatcher::Create(eventSubscriptionConfig(1));
    ASSERT_TRUE(dispatcher.has_value());
    u32 observedWidth = 0;
    u64 observedRevision = 0;
    std::optional<Platform::GamepadId> observedGamepad;
    auto gamepadPool = Core::GenerationPool<int, Platform::GamepadRegistryTag>::Create(1);
    ASSERT_TRUE(gamepadPool.has_value());
    auto gamepad = gamepadPool->tryEmplace(1);
    ASSERT_TRUE(gamepad.has_value());
    auto token =
        PlatformEventDispatcherTestAccess::subscribe(*dispatcher, [&](const PlatformEventNotification& notification) {
            const auto* metrics = notification.primaryWindowMetrics();
            ASSERT_NE(metrics, nullptr);
            observedWidth = metrics->logicalExtent.width;
            observedRevision = metrics->revision;
            EXPECT_EQ(notification.connectedGamepadCount(), 1U);
            observedGamepad = notification.connectedGamepadId(0);
            EXPECT_FALSE(notification.connectedGamepadId(1).has_value());
        });
    ASSERT_TRUE(token.has_value());
    const Platform::PlatformEvent event{
        .sequence = 9,
        .payload =
            Platform::PlatformEventStreamReset{
                .reason = Platform::PlatformEventResetReason::CapacityExceeded,
            },
    };
    const Platform::WindowFrameSnapshot windows[] = {
        Platform::WindowFrameSnapshot{
            .metrics =
                {
                    .logicalExtent = {1600, 900},
                    .revision = 42,
                },
        },
    };
    const Platform::GamepadSnapshot gamepads[] = {
        Platform::GamepadSnapshot{.gamepad = *gamepad},
    };

    auto status = PlatformEventDispatcherTestAccess::dispatch(*dispatcher, {&event, 1}, windows, gamepads);

    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(observedWidth, 1600U);
    EXPECT_EQ(observedRevision, 42U);
    ASSERT_TRUE(observedGamepad.has_value());
    EXPECT_EQ(*observedGamepad, *gamepad);
}

} // namespace Tests
} // namespace Tina
