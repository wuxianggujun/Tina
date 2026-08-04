#include "BgfxOpaque3DShader.hpp"

#include <tina/render/RenderErrors.hpp>

#include <bgfx/embedded_shader.h>

#include "fs_tina_opaque3d_mr_glsl.bin.h"
#include "fs_tina_opaque3d_mr_spv.bin.h"
#include "fs_tina_opaque3d_csm_depth_glsl.bin.h"
#include "fs_tina_opaque3d_csm_depth_spv.bin.h"
#include "vs_tina_opaque3d_mr_glsl.bin.h"
#include "vs_tina_opaque3d_mr_spv.bin.h"
#include "vs_tina_opaque3d_csm_depth_glsl.bin.h"
#include "vs_tina_opaque3d_csm_depth_spv.bin.h"

#if BX_PLATFORM_WINDOWS
#include "fs_tina_opaque3d_mr_dxbc.bin.h"
#include "fs_tina_opaque3d_csm_depth_dxbc.bin.h"
#include "vs_tina_opaque3d_mr_dxbc.bin.h"
#include "vs_tina_opaque3d_csm_depth_dxbc.bin.h"
#endif

namespace Tina::Render::Bgfx::ShaderDetail {
namespace {

constexpr bgfx::EmbeddedShader EmbeddedShaders[] = {
    {
        "vs_tina_opaque3d_mr",
        {
#if BX_PLATFORM_WINDOWS
            {bgfx::RendererType::Direct3D11, vs_tina_opaque3d_mr_dxbc,
             sizeof(vs_tina_opaque3d_mr_dxbc)},
#endif
            {bgfx::RendererType::OpenGL, vs_tina_opaque3d_mr_glsl,
             sizeof(vs_tina_opaque3d_mr_glsl)},
            {bgfx::RendererType::Vulkan, vs_tina_opaque3d_mr_spv, sizeof(vs_tina_opaque3d_mr_spv)},
            {bgfx::RendererType::Count, nullptr, 0},
        },
    },
    {
        "fs_tina_opaque3d_mr",
        {
#if BX_PLATFORM_WINDOWS
            {bgfx::RendererType::Direct3D11, fs_tina_opaque3d_mr_dxbc,
             sizeof(fs_tina_opaque3d_mr_dxbc)},
#endif
            {bgfx::RendererType::OpenGL, fs_tina_opaque3d_mr_glsl,
             sizeof(fs_tina_opaque3d_mr_glsl)},
            {bgfx::RendererType::Vulkan, fs_tina_opaque3d_mr_spv, sizeof(fs_tina_opaque3d_mr_spv)},
            {bgfx::RendererType::Count, nullptr, 0},
        },
    },
    {
        "vs_tina_opaque3d_csm_depth",
        {
#if BX_PLATFORM_WINDOWS
            {bgfx::RendererType::Direct3D11, vs_tina_opaque3d_csm_depth_dxbc,
             sizeof(vs_tina_opaque3d_csm_depth_dxbc)},
#endif
            {bgfx::RendererType::OpenGL, vs_tina_opaque3d_csm_depth_glsl,
             sizeof(vs_tina_opaque3d_csm_depth_glsl)},
            {bgfx::RendererType::Vulkan, vs_tina_opaque3d_csm_depth_spv,
             sizeof(vs_tina_opaque3d_csm_depth_spv)},
            {bgfx::RendererType::Count, nullptr, 0},
        },
    },
    {
        "fs_tina_opaque3d_csm_depth",
        {
#if BX_PLATFORM_WINDOWS
            {bgfx::RendererType::Direct3D11, fs_tina_opaque3d_csm_depth_dxbc,
             sizeof(fs_tina_opaque3d_csm_depth_dxbc)},
#endif
            {bgfx::RendererType::OpenGL, fs_tina_opaque3d_csm_depth_glsl,
             sizeof(fs_tina_opaque3d_csm_depth_glsl)},
            {bgfx::RendererType::Vulkan, fs_tina_opaque3d_csm_depth_spv,
             sizeof(fs_tina_opaque3d_csm_depth_spv)},
            {bgfx::RendererType::Count, nullptr, 0},
        },
    },
    {nullptr, {{bgfx::RendererType::Count, nullptr, 0}}},
};

[[nodiscard]] Core::Error unsupportedShaderError(const char* context)
{
    Core::Error error{RenderErrorCode::DeviceInitializationFailed,
                      "The active bgfx renderer has no cooked Tina Opaque3D shader"};
    error.addContext(context);
    return error;
}

[[nodiscard]] Core::Result<bgfx::ProgramHandle> createEmbeddedProgram(const char* vertexName,
                                                                      const char* fragmentName,
                                                                      const char* context)
{
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();
    const bgfx::ShaderHandle vertexShader =
        bgfx::createEmbeddedShader(EmbeddedShaders, renderer, vertexName);
    if (!bgfx::isValid(vertexShader))
    {
        return Core::failure(unsupportedShaderError(context));
    }

    const bgfx::ShaderHandle fragmentShader =
        bgfx::createEmbeddedShader(EmbeddedShaders, renderer, fragmentName);
    if (!bgfx::isValid(fragmentShader))
    {
        bgfx::destroy(vertexShader);
        return Core::failure(unsupportedShaderError(context));
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

} // namespace

Core::Result<bgfx::ProgramHandle> createOpaque3DMrProgram()
{
    return createEmbeddedProgram("vs_tina_opaque3d_mr", "fs_tina_opaque3d_mr",
                                 "createOpaque3DMrProgram");
}

Core::Result<bgfx::ProgramHandle> createOpaque3DCascadedShadowDepthProgram()
{
    return createEmbeddedProgram("vs_tina_opaque3d_csm_depth",
                                 "fs_tina_opaque3d_csm_depth",
                                 "createOpaque3DCascadedShadowDepthProgram");
}

} // namespace Tina::Render::Bgfx::ShaderDetail
