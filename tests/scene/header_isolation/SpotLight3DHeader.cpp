#include <tina/scene/SpotLight3D.hpp>

static_assert(sizeof(Tina::Scene::SpotLight3D) > 0);
static_assert(Tina::Scene::SpotLightShadow3D{}.nearPlaneMeters == 0.05F);
static_assert(Tina::Scene::SpotLightShadow3D{}.depthBias == 0.0015F);
static_assert(Tina::Scene::SpotLightShadow3D{}.normalBiasMeters == 0.02F);

