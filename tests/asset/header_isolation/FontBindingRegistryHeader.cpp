#include <tina/asset/FontBindingRegistry.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::Asset::FontBindingRegistry>);
