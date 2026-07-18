#include <tina/render/RenderFrame.hpp>

#include <type_traits>

static_assert(std::is_copy_constructible_v<Tina::Render::RenderFrame>);
static_assert(std::is_copy_constructible_v<Tina::Render::UIDisplayListView>);
