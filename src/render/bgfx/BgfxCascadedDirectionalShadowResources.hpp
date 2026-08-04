#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <bgfx/bgfx.h>

namespace Tina::Render::Bgfx {

inline constexpr u16 BgfxCascadedDirectionalShadowAtlasExtent = 2048;
inline constexpr u16 BgfxCascadedDirectionalShadowTileExtent = 1024;
static_assert(BgfxCascadedDirectionalShadowAtlasExtent ==
              BgfxCascadedDirectionalShadowTileExtent * 2U);

struct BgfxCascadedDirectionalShadowResources final {
    bgfx::TextureHandle depthAtlas = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;

    [[nodiscard]] bool valid() const noexcept
    {
        return bgfx::isValid(depthAtlas) && bgfx::isValid(frameBuffer);
    }
};

[[nodiscard]] Core::Result<BgfxCascadedDirectionalShadowResources>
createCascadedDirectionalShadowResources();

void destroyCascadedDirectionalShadowResources(
    BgfxCascadedDirectionalShadowResources& resources) noexcept;

} // namespace Tina::Render::Bgfx
