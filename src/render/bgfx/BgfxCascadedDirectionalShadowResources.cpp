#include "BgfxCascadedDirectionalShadowResources.hpp"

#include <tina/render/RenderErrors.hpp>

namespace Tina::Render::Bgfx {
namespace {

constexpr u64 ShadowAtlasTextureFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL |
                                        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

} // namespace

Core::Result<BgfxCascadedDirectionalShadowResources>
createCascadedDirectionalShadowResources(u16 tileExtent)
{
    if (!bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D16,
                              ShadowAtlasTextureFlags))
    {
        return Core::failure(
            RenderErrorCode::DeviceInitializationFailed,
            "The active bgfx renderer cannot create a sampled D16 cascaded directional shadow atlas");
    }

    const u16 atlasExtent = static_cast<u16>(tileExtent * 2U);
    BgfxCascadedDirectionalShadowResources resources{};
    resources.depthAtlas = bgfx::createTexture2D(
        atlasExtent,
        atlasExtent,
        false,
        1,
        bgfx::TextureFormat::D16,
        ShadowAtlasTextureFlags,
        nullptr);
    if (!bgfx::isValid(resources.depthAtlas))
    {
        return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                             "bgfx rejected the cascaded directional shadow depth atlas");
    }

    resources.frameBuffer = bgfx::createFrameBuffer(1, &resources.depthAtlas, false);
    if (!bgfx::isValid(resources.frameBuffer))
    {
        bgfx::destroy(resources.depthAtlas);
        resources.depthAtlas = BGFX_INVALID_HANDLE;
        return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                             "bgfx rejected the cascaded directional shadow framebuffer");
    }
    return resources;
}

void destroyCascadedDirectionalShadowResources(
    BgfxCascadedDirectionalShadowResources& resources) noexcept
{
    if (bgfx::isValid(resources.frameBuffer))
    {
        bgfx::destroy(resources.frameBuffer);
        resources.frameBuffer = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(resources.depthAtlas))
    {
        bgfx::destroy(resources.depthAtlas);
        resources.depthAtlas = BGFX_INVALID_HANDLE;
    }
}

} // namespace Tina::Render::Bgfx
