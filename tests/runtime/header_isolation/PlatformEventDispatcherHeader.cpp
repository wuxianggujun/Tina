#include <tina/runtime/spi/PlatformEventDispatcher.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::PlatformEventDispatcher>);
static_assert(std::is_move_constructible_v<Tina::PlatformEventDispatcher>);
