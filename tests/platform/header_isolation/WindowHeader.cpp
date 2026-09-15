#include <tina/platform/Window.hpp>

static_assert(!Tina::Platform::WindowId{}.hasValue());
static_assert(Tina::Platform::PrimaryWindowConfig{}.title == "Tina");
static_assert(Tina::Platform::WindowSafeInsets{}.left == 0.0F);
static_assert(Tina::Platform::WindowMetricsSnapshot{}.safeInsets.top == 0.0F);
