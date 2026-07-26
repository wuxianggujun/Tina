#include <tina/scene/Trail2D.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::Scene::Trail2D>);
static_assert(!std::is_copy_assignable_v<Tina::Scene::Trail2D>);
static_assert(std::is_nothrow_move_constructible_v<Tina::Scene::Trail2D>);
static_assert(!std::is_move_assignable_v<Tina::Scene::Trail2D>);
static_assert(sizeof(Tina::Scene::Trail2DConfig) > 0);
