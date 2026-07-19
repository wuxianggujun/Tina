#include "BgfxOpaque3DShader.hpp"

#include <tina/render/RenderErrors.hpp>

#include <bgfx/embedded_shader.h>

#include "fs_tina_opaque3d_unlit_glsl.bin.h"
#include "fs_tina_opaque3d_unlit_spv.bin.h"
#include "vs_tina_opaque3d_unlit_glsl.bin.h"
#include "vs_tina_opaque3d_unlit_spv.bin.h"

#if BX_PLATFORM_WINDOWS
#include "fs_tina_opaque3d_unlit_dxbc.bin.h"
#include "vs_tina_opaque3d_unlit_dxbc.bin.h"
#endif

namespace Tina::Render::Bgfx::ShaderDetail {
namespace {

constexpr bgfx::EmbeddedShader EmbeddedShaders[] = {
    {
        "vs_tina_opaque3d_unlit",
        {
#if BX_PLATFORM_WINDOWS
            {bgfx::RendererType::Direct3D11, vs_tina_opaque3d_unlit_dxbc,
             sizeof(vs_tina_opaque3d_unlit_dxbc)},
#endif
            {bgfx::RendererType::OpenGL, vs_tina_opaque3d_unlit_glsl,
             sizeof(vs_tina_opaque3d_unlit_glsl)},
            {bgfx::RendererType::Vulkan, vs_tina_opaque3d_unlit_spv,
             sizeof(vs_tina_opaque3d_unlit_spv)},
            {bgfx::RendererType::Count, nullptr, 0},
        },
    },
    {
        "fs_tina_opaque3d_unlit",
        {
#if BX_PLATFORM_WINDOWS
            {bgfx::RendererType::Direct3D11, fs_tina_opaque3d_unlit_dxbc,
             sizeof(fs_tina_opaque3d_unlit_dxbc)},
#endif
            {bgfx::RendererType::OpenGL, fs_tina_opaque3d_unlit_glsl,
             sizeof(fs_tina_opaque3d_unlit_glsl)},
            {bgfx::RendererType::Vulkan, fs_tina_opaque3d_unlit_spv,
             sizeof(fs_tina_opaque3d_unlit_spv)},
            {bgfx::RendererType::Count, nullptr, 0},
        },
    },
    {nullptr, {{bgfx::RendererType::Count, nullptr, 0}}},
};

[[nodiscard]] Core::Error unsupportedShaderError()
{
    Core::Error error{RenderErrorCode::DeviceInitializationFailed,
                      "The active bgfx renderer has no cooked Tina Opaque3D shader"};
    error.addContext("createOpaque3DUnlitProgram");
    return error;
}

} // namespace

Core::Result<bgfx::ProgramHandle> createOpaque3DUnlitProgram()
{
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();
    const bgfx::ShaderHandle vertexShader =
        bgfx::createEmbeddedShader(EmbeddedShaders, renderer, "vs_tina_opaque3d_unlit");
    if (!bgfx::isValid(vertexShader))
    {
        return Core::failure(unsupportedShaderError());
    }

    const bgfx::ShaderHandle fragmentShader =
        bgfx::createEmbeddedShader(EmbeddedShaders, renderer, "fs_tina_opaque3d_unlit");
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
                             "bgfx rejected the Tina Opaque3D shader program");
    }
    return program;
}

} // namespace Tina::Render::Bgfx::ShaderDetail
