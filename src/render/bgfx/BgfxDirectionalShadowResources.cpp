#include "BgfxDirectionalShadowResources.hpp"

#include <tina/render/RenderErrors.hpp>

namespace Tina::Render::Bgfx {
namespace {

constexpr u64 ShadowTextureFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL |
                                   BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

} // namespace

Core::Result<BgfxDirectionalShadowResources> createDirectionalShadowResources()
{
    if (!bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D16,
                              ShadowTextureFlags))
    {
        return Core::failure(
            RenderErrorCode::DeviceInitializationFailed,
            "The active bgfx renderer cannot create a sampled D16 directional shadow map");
    }

    BgfxDirectionalShadowResources resources{};
    resources.depthTexture = bgfx::createTexture2D(
        BgfxDirectionalShadowMapExtent,
        BgfxDirectionalShadowMapExtent,
        false,
        1,
        bgfx::TextureFormat::D16,
        ShadowTextureFlags,
        nullptr);
    if (!bgfx::isValid(resources.depthTexture))
    {
        return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                             "bgfx rejected the directional shadow depth texture");
    }

    resources.frameBuffer =
        bgfx::createFrameBuffer(1, &resources.depthTexture, false);
    if (!bgfx::isValid(resources.frameBuffer))
    {
        bgfx::destroy(resources.depthTexture);
        resources.depthTexture = BGFX_INVALID_HANDLE;
        return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                             "bgfx rejected the directional shadow framebuffer");
    }
    return resources;
}

void destroyDirectionalShadowResources(BgfxDirectionalShadowResources& resources) noexcept
{
    if (bgfx::isValid(resources.frameBuffer))
    {
        bgfx::destroy(resources.frameBuffer);
        resources.frameBuffer = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(resources.depthTexture))
    {
        bgfx::destroy(resources.depthTexture);
        resources.depthTexture = BGFX_INVALID_HANDLE;
    }
}

} // namespace Tina::Render::Bgfx
