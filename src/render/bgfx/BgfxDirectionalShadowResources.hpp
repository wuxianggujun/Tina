#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <bgfx/bgfx.h>

namespace Tina::Render::Bgfx {

inline constexpr u16 BgfxDirectionalShadowMapExtent = 1024;

struct BgfxDirectionalShadowResources final {
    bgfx::TextureHandle depthTexture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;

    [[nodiscard]] bool valid() const noexcept
    {
        return bgfx::isValid(depthTexture) && bgfx::isValid(frameBuffer);
    }
};

[[nodiscard]] Core::Result<BgfxDirectionalShadowResources>
createDirectionalShadowResources();

void destroyDirectionalShadowResources(BgfxDirectionalShadowResources& resources) noexcept;

} // namespace Tina::Render::Bgfx
