#include <tina/scene/Marker2D.hpp>

#include <type_traits>

static_assert(std::is_empty_v<Tina::Scene::Marker2D>);
static_assert(std::is_trivially_copyable_v<Tina::Scene::Marker2D>);
