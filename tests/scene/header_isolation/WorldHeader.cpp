#include <tina/scene/World.hpp>

static_assert(Tina::Scene::WorldConfig{}.initialEntityReserve ==
              Tina::Scene::WorldConfig::DefaultInitialEntityReserve);
