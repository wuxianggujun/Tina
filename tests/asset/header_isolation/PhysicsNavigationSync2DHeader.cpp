#include <tina/asset/PhysicsNavigationSync2D.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::Asset::PhysicsNavigationSync2D>);
static_assert(std::is_move_constructible_v<Tina::Asset::PhysicsNavigationSync2D>);
