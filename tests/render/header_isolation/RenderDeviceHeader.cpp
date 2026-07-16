#include <tina/render/RenderDevice.hpp>

#include <type_traits>

static_assert(std::has_virtual_destructor_v<Tina::Render::IRenderDevice>);
