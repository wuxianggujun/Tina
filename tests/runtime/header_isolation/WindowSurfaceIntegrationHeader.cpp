#include <tina/integration/WindowSurface.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::Integration::NativeWindowSurfaceLease>);
static_assert(std::is_nothrow_move_constructible_v<Tina::Integration::NativeWindowSurfaceLease>);
static_assert(std::is_nothrow_destructible_v<Tina::Integration::NativeWindowSurfaceLease>);
