#include <tina/runtime/PrimaryWindowUI.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::PrimaryWindowUIRootBuilder>);
static_assert(std::is_move_constructible_v<Tina::PrimaryWindowUIRootBuilder>);
static_assert(!std::is_copy_constructible_v<Tina::PrimaryWindowUITreeUpdater>);
static_assert(std::is_move_constructible_v<Tina::PrimaryWindowUITreeUpdater>);
