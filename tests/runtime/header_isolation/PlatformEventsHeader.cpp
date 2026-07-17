#include <tina/runtime/PlatformEvents.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::PlatformEventSubscription>);
static_assert(std::is_nothrow_move_constructible_v<Tina::PlatformEventSubscription>);
static_assert(!std::is_copy_constructible_v<Tina::PlatformEventSubscriptions>);
static_assert(!std::is_move_constructible_v<Tina::PlatformEventSubscriptions>);
static_assert(!std::is_copy_constructible_v<Tina::PlatformEventNotification>);
static_assert(!std::is_move_constructible_v<Tina::PlatformEventNotification>);
static_assert(Tina::PlatformEventSubscriptionConfig::DefaultSubscriberCapacity == 64);
