#include <tina/ui/UIContext.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::UI::UIContext>);
static_assert(!std::is_move_constructible_v<Tina::UI::UIContext>);
static_assert(!std::is_copy_constructible_v<Tina::UI::UIRootOwner>);
static_assert(std::is_nothrow_move_constructible_v<Tina::UI::UIRootOwner>);
static_assert(!std::is_copy_constructible_v<Tina::UI::UITreeUpdater>);
static_assert(std::is_nothrow_move_constructible_v<Tina::UI::UITreeUpdater>);
static_assert(Tina::UI::UIContextCapacityConfig::DefaultNodeCapacity == 4096);
static_assert(Tina::UI::UIContextCapacityConfig::DefaultRootCapacity == 64);
