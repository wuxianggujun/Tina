#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>

#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<Tina::EngineCompositionFactories>);
static_assert(std::is_move_constructible_v<Tina::EngineCompositionFactories>);
static_assert(std::is_nothrow_move_constructible_v<Tina::EngineCompositionFactories>);
static_assert(noexcept(Tina::EngineHost::Create(std::declval<const Tina::EngineConfig&>(),
                                                std::declval<Tina::EngineCompositionFactories>())));
