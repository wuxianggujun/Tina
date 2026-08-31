#include <tina/math/Constants.hpp>

static_assert(Tina::Math::TwoPi == 2.0F * Tina::Math::Pi);
static_assert(Tina::Math::HalfPi == Tina::Math::Pi / 2.0F);
static_assert(Tina::Math::radians(180.0F) == Tina::Math::Pi);
static_assert(Tina::Math::degrees(Tina::Math::Pi) == 180.0F);
static_assert(Tina::Math::DefaultRelativeEpsilon == 1.0e-5F);
