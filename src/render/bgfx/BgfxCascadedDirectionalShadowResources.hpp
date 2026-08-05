#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <bgfx/bgfx.h>

namespace Tina::Render::Bgfx {

struct BgfxCascadedDirectionalShadowResources final {
    bgfx::TextureHandle depthAtlas = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;

    [[nodiscard]] bool valid() const noexcept
    {
        return bgfx::isValid(depthAtlas) && bgfx::isValid(frameBuffer);
    }
};

[[nodiscard]] Core::Result<BgfxCascadedDirectionalShadowResources>
createCascadedDirectionalShadowResources(u16 tileExtent);

void destroyCascadedDirectionalShadowResources(
    BgfxCascadedDirectionalShadowResources& resources) noexcept;

} // namespace Tina::Render::Bgfx
