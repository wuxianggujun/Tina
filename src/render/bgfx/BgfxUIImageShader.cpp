#include "BgfxUIImageShader.hpp"

#include <tina/render/RenderErrors.hpp>

#include <bgfx/embedded_shader.h>

#include "fs_tina_ui_image_quad_glsl.bin.h"
#include "fs_tina_ui_image_quad_spv.bin.h"
#include "vs_tina_ui_image_quad_glsl.bin.h"
#include "vs_tina_ui_image_quad_spv.bin.h"

#if BX_PLATFORM_WINDOWS
#include "fs_tina_ui_image_quad_dxbc.bin.h"
#include "vs_tina_ui_image_quad_dxbc.bin.h"
#endif

namespace Tina::Render::Bgfx::ShaderDetail {
namespace {

constexpr bgfx::EmbeddedShader EmbeddedShaders[] = {
    {
        "vs_tina_ui_image_quad",
        {
#if BX_PLATFORM_WINDOWS
            {bgfx::RendererType::Direct3D11, vs_tina_ui_image_quad_dxbc,
             sizeof(vs_tina_ui_image_quad_dxbc)},
#endif
            {bgfx::RendererType::OpenGL, vs_tina_ui_image_quad_glsl,
             sizeof(vs_tina_ui_image_quad_glsl)},
            {bgfx::RendererType::Vulkan, vs_tina_ui_image_quad_spv,
             sizeof(vs_tina_ui_image_quad_spv)},
            {bgfx::RendererType::Count, nullptr, 0},
        },
    },
    {
        "fs_tina_ui_image_quad",
        {
#if BX_PLATFORM_WINDOWS
            {bgfx::RendererType::Direct3D11, fs_tina_ui_image_quad_dxbc,
             sizeof(fs_tina_ui_image_quad_dxbc)},
#endif
            {bgfx::RendererType::OpenGL, fs_tina_ui_image_quad_glsl,
             sizeof(fs_tina_ui_image_quad_glsl)},
            {bgfx::RendererType::Vulkan, fs_tina_ui_image_quad_spv,
             sizeof(fs_tina_ui_image_quad_spv)},
            {bgfx::RendererType::Count, nullptr, 0},
        },
    },
    {nullptr, {{bgfx::RendererType::Count, nullptr, 0}}},
};

[[nodiscard]] Core::Error unsupportedShaderError()
{
    Core::Error error{RenderErrorCode::DeviceInitializationFailed,
                      "The active bgfx renderer has no cooked Tina RGBA UI image shader"};
    error.addContext("createUIImageQuadProgram");
    return error;
}

} // namespace

Core::Result<bgfx::ProgramHandle> createUIImageQuadProgram()
{
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();
    const bgfx::ShaderHandle vertexShader =
        bgfx::createEmbeddedShader(EmbeddedShaders, renderer, "vs_tina_ui_image_quad");
    if (!bgfx::isValid(vertexShader))
    {
        return Core::failure(unsupportedShaderError());
    }
    const bgfx::ShaderHandle fragmentShader =
        bgfx::createEmbeddedShader(EmbeddedShaders, renderer, "fs_tina_ui_image_quad");
    if (!bgfx::isValid(fragmentShader))
    {
        bgfx::destroy(vertexShader);
        return Core::failure(unsupportedShaderError());
    }
    const bgfx::ProgramHandle program = bgfx::createProgram(vertexShader, fragmentShader, false);
    bgfx::destroy(vertexShader);
    bgfx::destroy(fragmentShader);
    if (!bgfx::isValid(program))
    {
        return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                             "bgfx rejected the Tina RGBA UI image shader program");
    }
    return program;
}

} // namespace Tina::Render::Bgfx::ShaderDetail
