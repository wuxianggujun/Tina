#include <tina/ui/UIAuthoring.hpp>

#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<Tina::UI::UIRootOwner>);
static_assert(std::is_nothrow_move_constructible_v<Tina::UI::UIRootOwner>);
static_assert(!std::is_copy_constructible_v<Tina::UI::UITreeUpdater>);
static_assert(std::is_nothrow_move_constructible_v<Tina::UI::UITreeUpdater>);
using SetEnabledResult = decltype(std::declval<Tina::UI::UITreeUpdater&>().setEnabled(
    std::declval<Tina::UI::UINodeId>(), true));
static_assert(std::is_same_v<SetEnabledResult, Tina::Core::Status>);
