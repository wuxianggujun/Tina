#include <tina/gameplay/Action.hpp>

#include <type_traits>

static_assert(!Tina::Gameplay::ActionId{}.hasValue());
// Move-only: an Action owns its node tree and is consumed by play().
static_assert(!std::is_copy_constructible_v<Tina::Gameplay::Action>);
static_assert(std::is_move_constructible_v<Tina::Gameplay::Action>);
static_assert(!std::is_copy_constructible_v<Tina::Gameplay::ActionRunner>);
