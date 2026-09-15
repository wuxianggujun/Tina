#include <tina/scene/ParticleSystem3D.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::Scene::ParticleSystem3D>);
static_assert(std::is_nothrow_move_constructible_v<Tina::Scene::ParticleSystem3D>);
static_assert(!std::is_move_assignable_v<Tina::Scene::ParticleSystem3D>);
static_assert(sizeof(Tina::Scene::ParticleBurst3D) > 0);
static_assert(std::is_same_v<
              decltype(Tina::Scene::ParticleBurst3D::sprite),
              Tina::Asset::AssetHandle>);
// The 2D and 3D systems share one ParticleLifetimeRange; two definitions of that
// name in Tina::Scene would be an ODR violation, so this header must reach it
// through the shared include rather than declaring its own.
static_assert(std::is_same_v<
              decltype(Tina::Scene::ParticleBurst3D::lifetime),
              Tina::Scene::ParticleLifetimeRange>);
