#include "BgfxPostProcess.hpp"

#include <tina/render/RenderErrors.hpp>
#include <bgfx/embedded_shader.h>

#include "vs_tina_postprocess_glsl.bin.h"
#include "vs_tina_postprocess_spv.bin.h"
#include "fs_tina_postprocess_glsl.bin.h"
#include "fs_tina_postprocess_spv.bin.h"
#if BX_PLATFORM_WINDOWS
#include "vs_tina_postprocess_dxbc.bin.h"
#include "fs_tina_postprocess_dxbc.bin.h"
#endif
#if defined(TINA_RENDER_BGFX_MOBILE_SHADERS)
#include "vs_tina_postprocess_essl.bin.h"
#include "fs_tina_postprocess_essl.bin.h"
#include "vs_tina_postprocess_mtl.bin.h"
#include "fs_tina_postprocess_mtl.bin.h"
#endif

namespace Tina::Render::Bgfx {
namespace {
constexpr bgfx::EmbeddedShader EmbeddedShaders[] = {
    {"vs_tina_postprocess", {
#if BX_PLATFORM_WINDOWS
        {bgfx::RendererType::Direct3D11, vs_tina_postprocess_dxbc, sizeof(vs_tina_postprocess_dxbc)},
        {bgfx::RendererType::Direct3D12, vs_tina_postprocess_dxbc, sizeof(vs_tina_postprocess_dxbc)},
#endif
        {bgfx::RendererType::OpenGL, vs_tina_postprocess_glsl, sizeof(vs_tina_postprocess_glsl)},
        {bgfx::RendererType::Vulkan, vs_tina_postprocess_spv, sizeof(vs_tina_postprocess_spv)},
#if defined(TINA_RENDER_BGFX_MOBILE_SHADERS)
        {bgfx::RendererType::OpenGLES, vs_tina_postprocess_essl, sizeof(vs_tina_postprocess_essl)},
        {bgfx::RendererType::Metal, vs_tina_postprocess_mtl, sizeof(vs_tina_postprocess_mtl)},
#endif
        {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_tina_postprocess", {
#if BX_PLATFORM_WINDOWS
        {bgfx::RendererType::Direct3D11, fs_tina_postprocess_dxbc, sizeof(fs_tina_postprocess_dxbc)},
        {bgfx::RendererType::Direct3D12, fs_tina_postprocess_dxbc, sizeof(fs_tina_postprocess_dxbc)},
#endif
        {bgfx::RendererType::OpenGL, fs_tina_postprocess_glsl, sizeof(fs_tina_postprocess_glsl)},
        {bgfx::RendererType::Vulkan, fs_tina_postprocess_spv, sizeof(fs_tina_postprocess_spv)},
#if defined(TINA_RENDER_BGFX_MOBILE_SHADERS)
        {bgfx::RendererType::OpenGLES, fs_tina_postprocess_essl, sizeof(fs_tina_postprocess_essl)},
        {bgfx::RendererType::Metal, fs_tina_postprocess_mtl, sizeof(fs_tina_postprocess_mtl)},
#endif
        {bgfx::RendererType::Count, nullptr, 0}}},
    BGFX_EMBEDDED_SHADER_END()
};
}

Core::Status BgfxPostProcess::initialize()
{
    if (bgfx::isValid(program_)) return Core::success();
    const auto renderer = bgfx::getRendererType();
    vertex_ = bgfx::createEmbeddedShader(EmbeddedShaders, renderer, "vs_tina_postprocess");
    const auto fragment = bgfx::createEmbeddedShader(EmbeddedShaders, renderer, "fs_tina_postprocess");
    if (bgfx::isValid(vertex_) && bgfx::isValid(fragment))
        program_ = bgfx::createProgram(vertex_, fragment, false);
    if (bgfx::isValid(fragment)) bgfx::destroy(fragment);
    if (!bgfx::isValid(program_))
    {
        shutdown();
        return Core::failure(RenderErrorCode::RenderTextureUnsupported, "bgfx could not link the fullscreen post-process program");
    }

    constexpr std::array names{"s_postSource", "s_postAuxiliary", "u_postSourceInfo", "u_postDestinationInfo",
        "u_postParams", "u_postBloom", "u_postFog", "u_postFogColor", "u_postCamera", "u_postViewport",
        "u_postInverseViewProjection", "u_postDecalFromWorld", "u_postDecalColor"};
    for (usize index = 0; index < Count; ++index)
    {
        const auto type = index <= Auxiliary ? bgfx::UniformType::Sampler :
            (index == InverseViewProjection || index == DecalFromWorld ? bgfx::UniformType::Mat4 : bgfx::UniformType::Vec4);
        uniforms_[index] = bgfx::createUniform(names[index], type);
        if (!bgfx::isValid(uniforms_[index]))
        {
            shutdown();
            return Core::failure(RenderErrorCode::RenderTextureUnsupported, "bgfx could not allocate a post-process uniform");
        }
    }
    bgfx::VertexLayout layout{};
    layout.begin().add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float).end();
    constexpr std::array<float, 6> triangle{-1, -1, 3, -1, -1, 3};
    const bgfx::Memory* triangleMemory = bgfx::copy(triangle.data(), sizeof(triangle));
    if (triangleMemory == nullptr)
    {
        shutdown();
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "bgfx could not copy the fullscreen triangle");
    }
    triangle_ = bgfx::createVertexBuffer(triangleMemory, layout);
    if (!bgfx::isValid(triangle_))
    {
        shutdown();
        return Core::failure(RenderErrorCode::RenderTextureUnsupported, "bgfx could not allocate the fullscreen triangle");
    }
    return Core::success();
}

u64 BgfxPostProcess::nativeCount() const noexcept
{
    u64 count = static_cast<u64>(bgfx::isValid(vertex_)) + static_cast<u64>(bgfx::isValid(program_)) +
                static_cast<u64>(bgfx::isValid(triangle_));
    for (auto uniform : uniforms_) count += static_cast<u64>(bgfx::isValid(uniform));
    return count;
}

void BgfxPostProcess::shutdown() noexcept
{
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    if (bgfx::isValid(vertex_)) bgfx::destroy(vertex_);
    if (bgfx::isValid(triangle_)) bgfx::destroy(triangle_);
    program_ = BGFX_INVALID_HANDLE;
    vertex_ = BGFX_INVALID_HANDLE;
    triangle_ = BGFX_INVALID_HANDLE;
    for (auto& uniform : uniforms_)
    {
        if (bgfx::isValid(uniform)) bgfx::destroy(uniform);
        uniform = BGFX_INVALID_HANDLE;
    }
}

void BgfxPostProcess::submit(const BgfxPostProcessDraw& draw) const noexcept
{
    const auto* caps = bgfx::getCaps();
    const std::array<float, 4> sourceInfo{
        1.0F / static_cast<float>(draw.sourceExtent.width), 1.0F / static_cast<float>(draw.sourceExtent.height),
        caps != nullptr && caps->originBottomLeft ? 1.0F : 0.0F, static_cast<float>(draw.pass.sourceMipLevel)};
    const std::array<float, 4> destinationInfo{static_cast<float>(draw.destinationExtent.width),
        static_cast<float>(draw.destinationExtent.height), static_cast<float>(draw.pass.destinationMipLevel), 0};
    const std::array<float, 4> params{static_cast<float>(draw.pass.kind), static_cast<float>(draw.toneMapping.operation),
        draw.toneMapping.exposure, 0};
    const std::array<float, 4> bloom{draw.bloom.threshold, draw.bloom.softKnee, draw.bloom.intensity, 0};
    const std::array<float, 4> fog{static_cast<float>(draw.fog.mode), draw.fog.density, draw.fog.linearStart, draw.fog.linearEnd};
    const std::array<float, 4> fogColor{draw.fog.colorR, draw.fog.colorG, draw.fog.colorB, 1};
    bgfx::setViewFrameBuffer(draw.view, draw.framebuffer);
    bgfx::setViewRect(draw.view, 0, 0, static_cast<u16>(draw.destinationExtent.width),
                      static_cast<u16>(draw.destinationExtent.height));
    bgfx::setViewClear(draw.view, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(draw.view, nullptr, nullptr);
    bgfx::setViewMode(draw.view, bgfx::ViewMode::Sequential);
    bgfx::setScissor();
    bgfx::setUniform(uniforms_[SourceInfo], sourceInfo.data());
    bgfx::setUniform(uniforms_[DestinationInfo], destinationInfo.data());
    bgfx::setUniform(uniforms_[Params], params.data());
    bgfx::setUniform(uniforms_[Bloom], bloom.data());
    bgfx::setUniform(uniforms_[Fog], fog.data());
    bgfx::setUniform(uniforms_[FogColor], fogColor.data());
    bgfx::setUniform(uniforms_[Camera], draw.camera.data());
    bgfx::setUniform(uniforms_[Viewport], draw.viewport.data());
    bgfx::setUniform(uniforms_[InverseViewProjection], draw.inverseViewProjection.data());
    bgfx::setUniform(uniforms_[DecalFromWorld], draw.decalFromWorld.data());
    bgfx::setUniform(uniforms_[DecalColor], draw.decalColor.data());
    bgfx::setTexture(0, uniforms_[Source], draw.source);
    bgfx::setTexture(1, uniforms_[Auxiliary], bgfx::isValid(draw.auxiliary) ? draw.auxiliary : draw.source);
    u64 state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
    if (draw.pass.kind == RenderPipelinePassKind::BloomComposite)
        state = BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);
    else if (draw.pass.kind == RenderPipelinePassKind::Fog || draw.pass.kind == RenderPipelinePassKind::Decal)
        state = BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    bgfx::setState(state);
    bgfx::setVertexBuffer(0, triangle_);
    bgfx::submit(draw.view, bgfx::isValid(draw.program) ? draw.program : program_);
}
} // namespace Tina::Render::Bgfx
