#include <tina/scene/SceneErrors.hpp>

static_assert(
    Tina::Scene::SceneErrorCode::InvalidEntity.domain == Tina::Core::ErrorDomain::Scene);
static_assert(
    Tina::Scene::SceneErrorCode::ConstructionFailed.domain == Tina::Core::ErrorDomain::Scene);
static_assert(Tina::Scene::SceneErrorCode::ConstructionFailed.value == 11U);
static_assert(Tina::Scene::SceneErrorCode::TooManyActiveSpotLightShadows.value == 23U);
