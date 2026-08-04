#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>

#include <bgfx/bgfx.h>

namespace Tina::Render::Bgfx {

inline constexpr u64 BgfxEnvironmentMapNativeTextureCount = 3U;

struct BgfxEnvironmentMapResources final {
    bgfx::TextureHandle diffuseIrradiance = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle prefilteredSpecular = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle brdfLut = BGFX_INVALID_HANDLE;

    [[nodiscard]] bool valid() const noexcept
    {
        return bgfx::isValid(diffuseIrradiance) &&
               bgfx::isValid(prefilteredSpecular) && bgfx::isValid(brdfLut);
    }
};

[[nodiscard]] Core::Result<BgfxEnvironmentMapResources>
createEnvironmentMapResources(const EnvironmentMapUploadDesc& desc);

void destroyEnvironmentMapResources(BgfxEnvironmentMapResources& resources) noexcept;

} // namespace Tina::Render::Bgfx
