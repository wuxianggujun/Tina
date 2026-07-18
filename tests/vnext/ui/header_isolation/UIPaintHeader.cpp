#include <tina/ui/UIPaint.hpp>

static_assert(Tina::UI::premultiply({.red = 255, .alpha = 128}).red == 128);
