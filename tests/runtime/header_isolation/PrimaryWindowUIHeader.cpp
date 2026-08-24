#include <tina/runtime/PrimaryWindowUI.hpp>

#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<Tina::PrimaryWindowUIRootBuilder>);
static_assert(std::is_move_constructible_v<Tina::PrimaryWindowUIRootBuilder>);
static_assert(!std::is_copy_constructible_v<Tina::PrimaryWindowUITreeUpdater>);
static_assert(std::is_move_constructible_v<Tina::PrimaryWindowUITreeUpdater>);
static_assert(!std::is_copy_constructible_v<Tina::PrimaryWindowUIBuildTransaction>);
static_assert(std::is_move_constructible_v<Tina::PrimaryWindowUIBuildTransaction>);

using PrimaryWindowListenerResult = decltype(std::declval<Tina::PrimaryWindowUITreeUpdater&>().addRoutedPointerListener(
    std::declval<Tina::UI::UIRoutedPointerListenerDesc>(), std::declval<Tina::UI::UIRoutedPointerCallback>()));
static_assert(std::is_same_v<PrimaryWindowListenerResult, Tina::Core::Result<Tina::UI::UIRoutedPointerListenerToken>>);
using PrimaryWindowSetEnabledResult = decltype(
    std::declval<Tina::PrimaryWindowUITreeUpdater&>().setEnabled(
        std::declval<Tina::UI::UINodeId>(), true));
using PrimaryWindowIsEnabledResult = decltype(
    std::declval<const Tina::PrimaryWindowUITreeUpdater&>().isEnabled(
        std::declval<Tina::UI::UINodeId>()));
using PrimaryWindowSetStyleTransitionResult = decltype(
    std::declval<Tina::PrimaryWindowUITreeUpdater&>().motion().setStyleBackgroundColorTransition(
        std::declval<const Tina::UI::UITransitionSpec&>()));
using PrimaryWindowStyleTransitionResult = decltype(
    std::declval<const Tina::PrimaryWindowUITreeUpdater&>().motion().styleBackgroundColorTransition());
static_assert(std::is_same_v<PrimaryWindowSetEnabledResult, Tina::Core::Status>);
static_assert(std::is_same_v<PrimaryWindowIsEnabledResult, Tina::Core::Result<bool>>);
static_assert(std::is_same_v<PrimaryWindowSetStyleTransitionResult, Tina::Core::Status>);
static_assert(std::is_same_v<PrimaryWindowStyleTransitionResult,
                             Tina::Core::Result<Tina::UI::UITransitionSpec>>);
