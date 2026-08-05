#pragma once

#include "BgfxPointLightShadowMath.hpp"

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <bgfx/bgfx.h>

#include <array>

namespace Tina::Render::Bgfx {

inline constexpr u16 BgfxPointLightShadowMapExtent = 512;

struct BgfxPointLightShadowResources final {
    BgfxPointLightShadowResources() noexcept
    {
        for (bgfx::TextureHandle& depthMap : depthMaps)
        {
            depthMap = BGFX_INVALID_HANDLE;
        }
        for (bgfx::FrameBufferHandle& frameBuffer : frameBuffers)
        {
            frameBuffer = BGFX_INVALID_HANDLE;
        }
    }

    std::array<bgfx::TextureHandle, BgfxPointLightShadowFaceCount> depthMaps{};
    std::array<bgfx::FrameBufferHandle, BgfxPointLightShadowFaceCount> frameBuffers{};

    [[nodiscard]] bool valid() const noexcept;
};

[[nodiscard]] Core::Result<BgfxPointLightShadowResources>
createPointLightShadowResources();

void destroyPointLightShadowResources(BgfxPointLightShadowResources& resources) noexcept;

} // namespace Tina::Render::Bgfx
