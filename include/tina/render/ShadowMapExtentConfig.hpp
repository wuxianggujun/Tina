#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

namespace Tina::Render {

struct ShadowMapExtentConfig final {
    static constexpr u16 MinimumExtent = 128;
    static constexpr u16 MaximumExtent = 4096;
    static constexpr u16 DefaultDirectionalCascadeTileExtent = 1024;
    static constexpr u16 DefaultSpotLightMapExtent = 1024;
    static constexpr u16 DefaultPointLightFaceExtent = 512;

    u16 directionalCascadeTileExtent = DefaultDirectionalCascadeTileExtent;
    u16 spotLightMapExtent = DefaultSpotLightMapExtent;
    u16 pointLightFaceExtent = DefaultPointLightFaceExtent;

    [[nodiscard]] constexpr u16 directionalAtlasExtent() const noexcept
    {
        return static_cast<u16>(directionalCascadeTileExtent * 2U);
    }
};

[[nodiscard]] Core::Status
validateShadowMapExtentConfig(const ShadowMapExtentConfig& config);

} // namespace Tina::Render
