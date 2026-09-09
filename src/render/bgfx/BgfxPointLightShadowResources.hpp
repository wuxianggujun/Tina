#pragma once

#include "../shadow/PointLightShadowMath.hpp"

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <bgfx/bgfx.h>

#include <array>

namespace Tina::Render::Bgfx {

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

    std::array<bgfx::TextureHandle, Shadow::PointLightShadowFaceCount> depthMaps{};
    std::array<bgfx::FrameBufferHandle, Shadow::PointLightShadowFaceCount> frameBuffers{};

    [[nodiscard]] bool valid() const noexcept;
};

[[nodiscard]] Core::Result<BgfxPointLightShadowResources>
createPointLightShadowResources(u16 faceExtent);

void destroyPointLightShadowResources(BgfxPointLightShadowResources& resources) noexcept;

} // namespace Tina::Render::Bgfx
