#include "BgfxPointLightShadowResources.hpp"

#include <tina/render/RenderErrors.hpp>

#include <algorithm>

namespace Tina::Render::Bgfx {
namespace {

constexpr u64 ShadowMapTextureFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL |
                                      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

} // namespace

bool BgfxPointLightShadowResources::valid() const noexcept
{
    return std::ranges::all_of(depthMaps, [](bgfx::TextureHandle handle) noexcept {
               return bgfx::isValid(handle);
           }) &&
           std::ranges::all_of(frameBuffers, [](bgfx::FrameBufferHandle handle) noexcept {
               return bgfx::isValid(handle);
           });
}

Core::Result<BgfxPointLightShadowResources> createPointLightShadowResources(u16 faceExtent)
{
    if (!bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D16,
                              ShadowMapTextureFlags))
    {
        return Core::failure(
            RenderErrorCode::DeviceInitializationFailed,
            "The active bgfx renderer cannot create sampled D16 point-light shadow maps");
    }

    BgfxPointLightShadowResources resources{};
    for (usize faceIndex = 0; faceIndex < resources.depthMaps.size(); ++faceIndex)
    {
        resources.depthMaps[faceIndex] = bgfx::createTexture2D(
            faceExtent, faceExtent, false, 1,
            bgfx::TextureFormat::D16, ShadowMapTextureFlags, nullptr);
        if (!bgfx::isValid(resources.depthMaps[faceIndex]))
        {
            destroyPointLightShadowResources(resources);
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected a point-light shadow depth map");
        }

        resources.frameBuffers[faceIndex] =
            bgfx::createFrameBuffer(1, &resources.depthMaps[faceIndex], false);
        if (!bgfx::isValid(resources.frameBuffers[faceIndex]))
        {
            destroyPointLightShadowResources(resources);
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected a point-light shadow framebuffer");
        }
    }
    return resources;
}

void destroyPointLightShadowResources(BgfxPointLightShadowResources& resources) noexcept
{
    for (bgfx::FrameBufferHandle& frameBuffer : resources.frameBuffers)
    {
        if (bgfx::isValid(frameBuffer))
        {
            bgfx::destroy(frameBuffer);
            frameBuffer = BGFX_INVALID_HANDLE;
        }
    }
    for (bgfx::TextureHandle& depthMap : resources.depthMaps)
    {
        if (bgfx::isValid(depthMap))
        {
            bgfx::destroy(depthMap);
            depthMap = BGFX_INVALID_HANDLE;
        }
    }
}

} // namespace Tina::Render::Bgfx
