#include "BgfxSpotLightShadowResources.hpp"

#include <tina/render/RenderErrors.hpp>

namespace Tina::Render::Bgfx {
namespace {

constexpr u64 ShadowMapTextureFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL |
                                      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

} // namespace

Core::Result<BgfxSpotLightShadowResources>
createSpotLightShadowResources(u16 mapExtent)
{
    if (!bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D16,
                              ShadowMapTextureFlags))
    {
        return Core::failure(
            RenderErrorCode::DeviceInitializationFailed,
            "The active bgfx renderer cannot create a sampled D16 spot-light shadow map");
    }

    BgfxSpotLightShadowResources resources{};
    resources.depthMap = bgfx::createTexture2D(
        mapExtent,
        mapExtent,
        false,
        1,
        bgfx::TextureFormat::D16,
        ShadowMapTextureFlags,
        nullptr);
    if (!bgfx::isValid(resources.depthMap))
    {
        return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                             "bgfx rejected the spot-light shadow depth map");
    }

    resources.frameBuffer = bgfx::createFrameBuffer(1, &resources.depthMap, false);
    if (!bgfx::isValid(resources.frameBuffer))
    {
        bgfx::destroy(resources.depthMap);
        resources.depthMap = BGFX_INVALID_HANDLE;
        return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                             "bgfx rejected the spot-light shadow framebuffer");
    }
    return resources;
}

void destroySpotLightShadowResources(
    BgfxSpotLightShadowResources& resources) noexcept
{
    if (bgfx::isValid(resources.frameBuffer))
    {
        bgfx::destroy(resources.frameBuffer);
        resources.frameBuffer = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(resources.depthMap))
    {
        bgfx::destroy(resources.depthMap);
        resources.depthMap = BGFX_INVALID_HANDLE;
    }
}

} // namespace Tina::Render::Bgfx
