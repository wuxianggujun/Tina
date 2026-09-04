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
#include "vs_tina_opaque3d_skinned_glsl.bin.h"
#include "vs_tina_opaque3d_skinned_spv.bin.h"

#if BX_PLATFORM_WINDOWS
#include "fs_tina_opaque3d_mr_dxbc.bin.h"
#include "fs_tina_opaque3d_csm_depth_dxbc.bin.h"
#include "vs_tina_opaque3d_mr_dxbc.bin.h"
#include "vs_tina_opaque3d_csm_depth_dxbc.bin.h"
#include "vs_tina_opaque3d_skinned_dxbc.bin.h"
#endif

#if defined(TINA_RENDER_BGFX_MOBILE_SHADERS)
#include "fs_tina_opaque3d_mr_essl.bin.h"
#include "fs_tina_opaque3d_mr_mtl.bin.h"
#include "fs_tina_opaque3d_csm_depth_essl.bin.h"
#include "fs_tina_opaque3d_csm_depth_mtl.bin.h"
#include "vs_tina_opaque3d_mr_essl.bin.h"
#include "vs_tina_opaque3d_mr_mtl.bin.h"
#include "vs_tina_opaque3d_csm_depth_essl.bin.h"
#include "vs_tina_opaque3d_csm_depth_mtl.bin.h"
#include "vs_tina_opaque3d_skinned_essl.bin.h"
#include "vs_tina_opaque3d_skinned_mtl.bin.h"
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
#if defined(TINA_RENDER_BGFX_MOBILE_SHADERS)
            {bgfx::RendererType::OpenGLES, vs_tina_opaque3d_mr_essl,
             sizeof(vs_tina_opaque3d_mr_essl)},
            // Metal, not the GLES entry above: Apple deprecated OpenGL ES, so bgfx selects
            // Metal on every modern iOS device and a missing entry fails program creation.
            {bgfx::RendererType::Metal, vs_tina_opaque3d_mr_mtl,
             sizeof(vs_tina_opaque3d_mr_mtl)},
#endif
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
#if defined(TINA_RENDER_BGFX_MOBILE_SHADERS)
            {bgfx::RendererType::OpenGLES, fs_tina_opaque3d_mr_essl,
             sizeof(fs_tina_opaque3d_mr_essl)},
            {bgfx::RendererType::Metal, fs_tina_opaque3d_mr_mtl,
             sizeof(fs_tina_opaque3d_mr_mtl)},
#endif
            {bgfx::RendererType::Vulkan, fs_tina_opaque3d_mr_spv, sizeof(fs_tina_opaque3d_mr_spv)},
            {bgfx::RendererType::Count, nullptr, 0},
        },
    },
    {
        "vs_tina_opaque3d_skinned",
        {
#if BX_PLATFORM_WINDOWS
            {bgfx::RendererType::Direct3D11, vs_tina_opaque3d_skinned_dxbc,
             sizeof(vs_tina_opaque3d_skinned_dxbc)},
#endif
            {bgfx::RendererType::OpenGL, vs_tina_opaque3d_skinned_glsl,
             sizeof(vs_tina_opaque3d_skinned_glsl)},
#if defined(TINA_RENDER_BGFX_MOBILE_SHADERS)
            {bgfx::RendererType::OpenGLES, vs_tina_opaque3d_skinned_essl,
             sizeof(vs_tina_opaque3d_skinned_essl)},
            {bgfx::RendererType::Metal, vs_tina_opaque3d_skinned_mtl,
             sizeof(vs_tina_opaque3d_skinned_mtl)},
#endif
            {bgfx::RendererType::Vulkan, vs_tina_opaque3d_skinned_spv,
             sizeof(vs_tina_opaque3d_skinned_spv)},
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
#if defined(TINA_RENDER_BGFX_MOBILE_SHADERS)
            {bgfx::RendererType::OpenGLES, vs_tina_opaque3d_csm_depth_essl,
             sizeof(vs_tina_opaque3d_csm_depth_essl)},
            {bgfx::RendererType::Metal, vs_tina_opaque3d_csm_depth_mtl,
             sizeof(vs_tina_opaque3d_csm_depth_mtl)},
#endif
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
#if defined(TINA_RENDER_BGFX_MOBILE_SHADERS)
            {bgfx::RendererType::OpenGLES, fs_tina_opaque3d_csm_depth_essl,
             sizeof(fs_tina_opaque3d_csm_depth_essl)},
            {bgfx::RendererType::Metal, fs_tina_opaque3d_csm_depth_mtl,
             sizeof(fs_tina_opaque3d_csm_depth_mtl)},
#endif
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

[[nodiscard]] Core::Result<bgfx::ShaderHandle> createEmbeddedVertexShader(const char* vertexName,
                                                                          const char* context)
{
    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();
    const bgfx::ShaderHandle vertexShader =
        bgfx::createEmbeddedShader(EmbeddedShaders, renderer, vertexName);
    if (!bgfx::isValid(vertexShader))
    {
        return Core::failure(unsupportedShaderError(context));
    }
    return vertexShader;
}

} // namespace

Core::Result<bgfx::ShaderHandle> createOpaque3DMrVertexShader()
{
    return createEmbeddedVertexShader("vs_tina_opaque3d_mr", "createOpaque3DMrVertexShader");
}

Core::Result<bgfx::ShaderHandle> createOpaque3DSkinnedVertexShader()
{
    return createEmbeddedVertexShader("vs_tina_opaque3d_skinned",
                                      "createOpaque3DSkinnedVertexShader");
}

Core::Result<bgfx::ProgramHandle> createOpaque3DMrProgram()
{
    return createEmbeddedProgram("vs_tina_opaque3d_mr", "fs_tina_opaque3d_mr",
                                 "createOpaque3DMrProgram");
}

Core::Result<bgfx::ProgramHandle> createOpaque3DSkinnedMrProgram()
{
    // Shares the fs_tina_opaque3d_mr fragment stage: skinned meshes receive the
    // full Cook-Torrance GGX + IBL + shadow shading path.
    return createEmbeddedProgram("vs_tina_opaque3d_skinned", "fs_tina_opaque3d_mr",
                                 "createOpaque3DSkinnedMrProgram");
}

Core::Result<bgfx::ProgramHandle> createOpaque3DCascadedShadowDepthProgram()
{
    return createEmbeddedProgram("vs_tina_opaque3d_csm_depth",
                                 "fs_tina_opaque3d_csm_depth",
                                 "createOpaque3DCascadedShadowDepthProgram");
}

} // namespace Tina::Render::Bgfx::ShaderDetail
