#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <bgfx/bgfx.h>

namespace Tina::Render::Bgfx {

struct BgfxSpotLightShadowResources final {
    bgfx::TextureHandle depthMap = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;

    [[nodiscard]] bool valid() const noexcept
    {
        return bgfx::isValid(depthMap) && bgfx::isValid(frameBuffer);
    }
};

[[nodiscard]] Core::Result<BgfxSpotLightShadowResources>
createSpotLightShadowResources(u16 mapExtent);

void destroySpotLightShadowResources(
    BgfxSpotLightShadowResources& resources) noexcept;

} // namespace Tina::Render::Bgfx
