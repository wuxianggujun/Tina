#include <tina/asset/ShaderBindingRegistry.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Tina::Asset::ShaderBindingRegistry>);
static_assert(std::is_move_constructible_v<Tina::Asset::ShaderBindingRegistry>);
static_assert(Tina::Asset::ShaderBindingRegistryConfig{}.initialShaderReserve > 0U);
static_assert(std::is_nothrow_move_constructible_v<Tina::Asset::ShaderBindingRegistry>);
