#include <tina/render/RenderSurface.hpp>

#include <type_traits>

static_assert(std::is_trivially_copyable_v<Tina::Render::RenderSurfaceState>);
static_assert(!Tina::Render::RenderSurfaceId{}.hasValue());
static_assert(Tina::Render::RenderSurfaceId{.owner = 1, .index = 0, .generation = 1}.hasValue());
static_assert(!Tina::Render::RenderSurfaceId{
    .owner = 1, .index = Tina::Render::RenderSurfaceId::InvalidIndex, .generation = 1}
                   .hasValue());
