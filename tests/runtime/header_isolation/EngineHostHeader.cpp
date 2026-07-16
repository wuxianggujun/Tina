#include <tina/runtime/EngineHost.hpp>

#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<Tina::EngineHost>);
static_assert(!std::is_move_constructible_v<Tina::EngineHost>);
static_assert(std::is_nothrow_destructible_v<Tina::EngineHost>);
static_assert(std::is_copy_constructible_v<Tina::EngineConfig>);
static_assert(noexcept(std::declval<Tina::EngineHost&>().run(
    std::declval<Tina::IGameApplication&>())));
