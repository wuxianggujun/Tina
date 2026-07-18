#include <tina/runtime/EngineConfig.hpp>

#include <type_traits>

static_assert(std::is_copy_constructible_v<Tina::EngineConfig>);
static_assert(std::is_copy_constructible_v<Tina::UI::UIContextCapacityConfig>);
