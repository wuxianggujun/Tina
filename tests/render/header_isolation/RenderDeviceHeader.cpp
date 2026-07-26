#include <tina/render/RenderDevice.hpp>

#include <type_traits>

static_assert(std::has_virtual_destructor_v<Tina::Render::IRenderDevice>);
static_assert(std::is_trivially_copyable_v<Tina::Render::Mesh3DMaterialBindingDesc>);

constexpr Tina::Render::Mesh3DMaterialBindingDesc DefaultMesh3DMaterialBinding{};
static_assert(DefaultMesh3DMaterialBinding == Tina::Render::Mesh3DMaterialBindingDesc{});
static_assert(DefaultMesh3DMaterialBinding.metallicFactor == 0.0F);
static_assert(DefaultMesh3DMaterialBinding.roughnessFactor == 1.0F);
