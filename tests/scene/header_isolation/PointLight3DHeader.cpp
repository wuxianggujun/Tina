#include <tina/scene/PointLight3D.hpp>

static_assert(sizeof(Tina::Scene::PointLight3D) > 0);
static_assert(Tina::Scene::PointLightShadow3D{}.nearPlaneMeters == 0.05F);
static_assert(!Tina::Scene::PointLight3D{}.shadow.has_value());
