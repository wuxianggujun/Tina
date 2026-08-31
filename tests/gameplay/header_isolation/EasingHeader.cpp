#include <tina/gameplay/Easing.hpp>

// Endpoints are exact rather than run through the curve, so a tween lands on its
// authored target instead of on whatever the formula rounds to.
static_assert(Tina::Gameplay::isValidEasing(Tina::Gameplay::Easing::BounceInOut));
static_assert(!Tina::Gameplay::isValidEasing(Tina::Gameplay::Easing::Count));
