#include <tina/render/RenderDevice.hpp>

#include <type_traits>

static_assert(std::has_virtual_destructor_v<Tina::Render::IRenderDevice>);
static_assert(std::is_trivially_copyable_v<Tina::Render::Mesh3DMaterialBindingDesc>);

constexpr Tina::Render::Mesh3DMaterialBindingDesc DefaultMesh3DMaterialBinding{};
static_assert(DefaultMesh3DMaterialBinding == Tina::Render::Mesh3DMaterialBindingDesc{});
static_assert(DefaultMesh3DMaterialBinding.metallicFactor == 0.0F);
static_assert(DefaultMesh3DMaterialBinding.roughnessFactor == 1.0F);

// A default-constructed handle must never read as live, or an unbound shader slot would resolve.
static_assert(!Tina::Render::GpuShaderId{}.hasValue());
// The unscoped owner is reserved for test doubles; a real device issues its own owner id.
static_assert(Tina::Render::GpuShaderId{0U, 1U}.hasValue());
static_assert(Tina::Render::GpuShaderId{0U, 1U}.owner == Tina::Render::GpuShaderId::UnscopedOwner);
// Zero generation is the "never issued" state, so it stays invalid even with a valid index.
static_assert(!Tina::Render::GpuShaderId{7U, 3U, 0U}.hasValue());
