#include <tina/ui/UIContext.hpp>

#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<Tina::UI::UIContext>);
static_assert(!std::is_move_constructible_v<Tina::UI::UIContext>);
using AuthoringCapability = decltype(std::declval<Tina::UI::UIContext&>().authoring());
using InputCapability = decltype(std::declval<Tina::UI::UIContext&>().input());
static_assert(std::is_same_v<AuthoringCapability, Tina::UI::UIAuthoring>);
static_assert(std::is_same_v<InputCapability, Tina::UI::UIInputRouter>);
static_assert(Tina::UI::UIContextCapacityConfig::DefaultNodeCapacity == 4096);
static_assert(Tina::UI::UIContextCapacityConfig::DefaultRootCapacity == 64);
