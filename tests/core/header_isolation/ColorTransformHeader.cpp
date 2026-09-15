#include <tina/core/color/ColorTransform.hpp>

static_assert(sizeof(Tina::Core::ColorRgba) == 16);
static_assert(sizeof(Tina::Core::ColorTransform) == 32);
static_assert(Tina::Core::ColorTransform{}.channels()[3] == 1.0F);
