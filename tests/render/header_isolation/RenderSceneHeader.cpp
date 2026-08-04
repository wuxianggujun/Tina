#include <tina/render/RenderScene.hpp>

#include <type_traits>

static_assert(Tina::Render::RenderSceneCapacity{}.spriteCapacity != 0);
static_assert(Tina::Render::RenderSceneCapacity{}.mesh3DItemCapacity != 0);
static_assert(Tina::Render::RenderSceneCapacity{}.mesh3DBatchCapacity != 0);
static_assert(std::is_copy_constructible_v<Tina::Render::RenderSceneCapacity>);
static_assert(std::is_standard_layout_v<Tina::Render::RenderSceneFrameParameters>);
static_assert(std::is_standard_layout_v<Tina::Render::RenderPerspectiveCameraInput>);
static_assert(std::is_standard_layout_v<Tina::Render::RenderMesh3DInput>);
static_assert(std::is_standard_layout_v<Tina::Render::RenderPerspectiveCamera>);
static_assert(std::is_standard_layout_v<Tina::Render::RenderMesh3DItem>);
static_assert(std::is_standard_layout_v<Tina::Render::RenderMesh3DBatch>);
static_assert(std::is_standard_layout_v<Tina::Render::Mesh3DSpotLightShadow>);
static_assert(Tina::Render::Mesh3DSpotLightShadow{}.nearPlaneMeters == 0.05F);
static_assert(Tina::Render::Mesh3DSpotLightShadow::MaximumDepthBias == 0.05F);
static_assert(Tina::Render::Mesh3DSpotLightShadow::MaximumNormalBiasMeters == 1.0F);
static_assert(!std::is_default_constructible_v<Tina::Render::RenderSceneBuilder>);
static_assert(!std::is_copy_constructible_v<Tina::Render::RenderSceneBuilder>);
static_assert(!std::is_copy_constructible_v<Tina::Render::RenderSceneWriter>);
