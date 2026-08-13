#include <tina/scene/Fx2DFactory.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::Scene::Fx2DInstance>);
static_assert(std::is_move_constructible_v<Tina::Scene::Fx2DInstance>);
