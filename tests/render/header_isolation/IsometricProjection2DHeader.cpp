#include <tina/render/IsometricProjection2D.hpp>

static_assert(Tina::Render::IsometricProjection2D{}.project({1.0F, 1.0F}).y == 0.75F);
static_assert(Tina::Render::IsometricProjection2D{}.sortDepth({0.0F, 0.0F, 1.0F}) == 0.25);
