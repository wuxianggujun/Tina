#include <tina/asset/Sprite2DBindingRegistry.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::Asset::Sprite2DBindingRegistry>);
static_assert(std::is_move_constructible_v<Tina::Asset::Sprite2DBindingRegistry>);
static_assert(Tina::Asset::DefaultSprite2DBindingCapacity > 0U);
