#include <gtest/gtest.h>

#include "engine/EventSystem.hpp"

namespace Tina::Engine {
namespace {

struct QueuedTestEvent : Event<QueuedTestEvent, EventTypeId::GamePaused> {
    int value = 0;
};

TEST(EventSystemTest, KeepsPerInstanceInlineStorageWithinStackBudget)
{
    constexpr std::size_t maxInlineBytes = 64 * 1024;
    EXPECT_LT(sizeof(EventSystem), maxInlineBytes)
        << "EventSystem instances must remain safe for the default Windows thread stack";
}

TEST(EventSystemTest, DispatchesQueuedEventsInPriorityOrder)
{
    EventSystem events;
    ASSERT_TRUE(events.initialize());

    Container::Vector<int> received;
    auto subscription = events.subscribe<QueuedTestEvent>([&received](const QueuedTestEvent& event) {
        received.push_back(event.value);
    });

    QueuedTestEvent low{};
    low.value = 3;
    QueuedTestEvent high{};
    high.value = 1;
    QueuedTestEvent medium{};
    medium.value = 2;

    ASSERT_TRUE(events.enqueue(low, EventPriority::Low));
    ASSERT_TRUE(events.enqueue(high, EventPriority::High));
    ASSERT_TRUE(events.enqueue(medium, EventPriority::Medium));
    EXPECT_TRUE(received.empty());

    events.update();

    ASSERT_EQ(received.size(), 3u);
    EXPECT_EQ(received[0], 1);
    EXPECT_EQ(received[1], 2);
    EXPECT_EQ(received[2], 3);
}

TEST(EventSystemTest, SubscriptionTokenCanOutliveDispatcherSafely)
{
    SubscriptionToken orphanedToken;
    {
        EventSystem events;
        ASSERT_TRUE(events.initialize());
        orphanedToken = events.subscribe<QueuedTestEvent>([](const QueuedTestEvent&) {});
        ASSERT_TRUE(orphanedToken);
    }

    EXPECT_FALSE(orphanedToken);
    EXPECT_NO_THROW(orphanedToken.reset());
}

TEST(EventSystemTest, ResetImmediatelyStopsFurtherDelivery)
{
    EventSystem events;
    ASSERT_TRUE(events.initialize());

    int deliveries = 0;
    auto subscription = events.subscribe<QueuedTestEvent>([&deliveries](const QueuedTestEvent&) {
        ++deliveries;
    });

    events.trigger(QueuedTestEvent{});
    subscription.reset();
    events.trigger(QueuedTestEvent{});

    EXPECT_EQ(deliveries, 1);
}

} // namespace
} // namespace Tina::Engine
