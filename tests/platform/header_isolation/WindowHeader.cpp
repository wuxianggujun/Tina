#include <tina/platform/Window.hpp>

static_assert(!Tina::Platform::WindowId{}.hasValue());
static_assert(Tina::Platform::PrimaryWindowConfig{}.title == "Tina");
