#include <tina/scene/ParticleSystem2D.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::Scene::ParticleSystem2D>);
static_assert(std::is_nothrow_move_constructible_v<Tina::Scene::ParticleSystem2D>);
static_assert(sizeof(Tina::Scene::ParticleBurst2D) > 0);
