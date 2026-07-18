#include <tina/ui/UIContextConfig.hpp>

#include <type_traits>

static_assert(std::is_aggregate_v<Tina::UI::UIContextCapacityConfig>);
static_assert(Tina::UI::UIContextCapacityConfig::DefaultNodeCapacity > 0);
static_assert(Tina::UI::UIContextCapacityConfig::DefaultRootCapacity > 0);
