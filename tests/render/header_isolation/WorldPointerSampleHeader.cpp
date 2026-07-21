#include <tina/render/WorldPointerSample.hpp>

#include <type_traits>

static_assert(std::is_standard_layout_v<Tina::Render::WorldPointerSample>);
static_assert(std::is_copy_constructible_v<Tina::Render::WorldPointerSample>);
