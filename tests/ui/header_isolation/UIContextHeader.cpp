#include <tina/ui/UIContext.hpp>

#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<Tina::UI::UIContext>);
static_assert(!std::is_move_constructible_v<Tina::UI::UIContext>);
static_assert(!std::is_copy_constructible_v<Tina::UI::UIRootOwner>);
static_assert(std::is_nothrow_move_constructible_v<Tina::UI::UIRootOwner>);
static_assert(!std::is_copy_constructible_v<Tina::UI::UITreeUpdater>);
static_assert(std::is_nothrow_move_constructible_v<Tina::UI::UITreeUpdater>);
static_assert(!std::is_copy_constructible_v<Tina::UI::UIRoutedPointerListenerToken>);
static_assert(!std::is_copy_assignable_v<Tina::UI::UIRoutedPointerListenerToken>);
static_assert(std::is_nothrow_move_constructible_v<Tina::UI::UIRoutedPointerListenerToken>);
static_assert(std::is_nothrow_move_assignable_v<Tina::UI::UIRoutedPointerListenerToken>);
static_assert(std::is_nothrow_destructible_v<Tina::UI::UIRoutedPointerListenerToken>);
using RootScopedListenerResult = decltype(std::declval<Tina::UI::UITreeUpdater&>().addRoutedPointerListener(
    std::declval<Tina::UI::UIRoutedPointerListenerDesc>(), std::declval<Tina::UI::UIRoutedPointerCallback>()));
static_assert(std::is_same_v<RootScopedListenerResult, Tina::Core::Result<Tina::UI::UIRoutedPointerListenerToken>>);
using SetEnabledResult = decltype(std::declval<Tina::UI::UITreeUpdater&>().setEnabled(
    std::declval<Tina::UI::UINodeId>(), true));
using IsEnabledResult = decltype(std::declval<const Tina::UI::UITreeUpdater&>().isEnabled(
    std::declval<Tina::UI::UINodeId>()));
static_assert(std::is_same_v<SetEnabledResult, Tina::Core::Status>);
static_assert(std::is_same_v<IsEnabledResult, Tina::Core::Result<bool>>);
static_assert(Tina::UI::UIContextCapacityConfig::DefaultNodeCapacity == 4096);
static_assert(Tina::UI::UIContextCapacityConfig::DefaultRootCapacity == 64);
