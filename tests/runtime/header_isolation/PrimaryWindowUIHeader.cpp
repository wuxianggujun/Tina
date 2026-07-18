#include <tina/runtime/PrimaryWindowUI.hpp>

#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<Tina::PrimaryWindowUIRootBuilder>);
static_assert(std::is_move_constructible_v<Tina::PrimaryWindowUIRootBuilder>);
static_assert(!std::is_copy_constructible_v<Tina::PrimaryWindowUITreeUpdater>);
static_assert(std::is_move_constructible_v<Tina::PrimaryWindowUITreeUpdater>);

using PrimaryWindowListenerResult = decltype(std::declval<Tina::PrimaryWindowUITreeUpdater&>().addRoutedPointerListener(
    std::declval<Tina::UI::UIRoutedPointerListenerDesc>(), std::declval<Tina::UI::UIRoutedPointerCallback>()));
static_assert(std::is_same_v<PrimaryWindowListenerResult, Tina::Core::Result<Tina::UI::UIRoutedPointerListenerToken>>);
