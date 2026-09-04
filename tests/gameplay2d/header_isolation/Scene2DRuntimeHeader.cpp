#include <tina/gameplay2d/Scene2DRuntime.hpp>

#if defined(TINA_HAS_PHYSICS2D)
#error "gameplay2d header isolation must compile Scene2DRuntime.hpp without TINA_HAS_PHYSICS2D"
#endif

static_assert(sizeof(Tina::Gameplay2D::Scene2DRuntime) > 0);
static_assert(sizeof(Tina::Gameplay2D::Scene2DRuntimeConfig) > 0);
static_assert(sizeof(Tina::Gameplay2D::Scene2DRuntimeStats) > 0);
