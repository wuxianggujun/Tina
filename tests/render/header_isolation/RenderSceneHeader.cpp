#include <tina/render/RenderScene.hpp>

#include <type_traits>

static_assert(Tina::Render::RenderSceneCapacity{}.spriteCapacity != 0);
static_assert(std::is_copy_constructible_v<Tina::Render::RenderSceneCapacity>);
static_assert(!std::is_default_constructible_v<Tina::Render::RenderSceneBuilder>);
static_assert(!std::is_copy_constructible_v<Tina::Render::RenderSceneBuilder>);
static_assert(!std::is_copy_constructible_v<Tina::Render::RenderSceneWriter>);
