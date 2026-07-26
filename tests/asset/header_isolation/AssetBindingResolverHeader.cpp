#include <tina/asset/AssetBindingResolver.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::Asset::AssetBindingResolver>);
static_assert(sizeof(Tina::Asset::AssetBindingResolver) > 0);
