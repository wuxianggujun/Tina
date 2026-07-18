#include <tina/integration/UIRenderDisplayList.hpp>

#include <type_traits>

#if defined(BGFX_H_HEADER_GUARD) || defined(GLFW_VERSION_MAJOR) || \
    defined(SDL_MAJOR_VERSION) || defined(FT_FREETYPE_H) || \
    defined(EASTL_VECTOR_H)
#error "UIRenderDisplayList.hpp leaked a forbidden backend or compatibility dependency"
#endif

static_assert(std::is_trivially_copyable_v<
              Tina::Integration::UIRenderViewportMapping>);
static_assert(std::is_trivially_copyable_v<
              Tina::Integration::UIRenderDisplayListBuildStatistics>);
