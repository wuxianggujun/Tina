#include <tina/scene/ParticleSystem2D.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::Scene::ParticleSystem2D>);
static_assert(std::is_nothrow_move_constructible_v<Tina::Scene::ParticleSystem2D>);
static_assert(sizeof(Tina::Scene::ParticleBurst2D) > 0);
static_assert(std::is_same_v<
              decltype(Tina::Scene::ParticleBurst2D::sprite),
              Tina::Asset::AssetHandle>);
