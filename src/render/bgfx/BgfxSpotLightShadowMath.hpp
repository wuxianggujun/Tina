#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderScene.hpp>

#include <array>

namespace Tina::Render::Bgfx {

struct BgfxSpotLightShadowInput final {
    Mesh3DSpotLight light{};
    float nearPlaneMeters = 0.1F;
};

struct BgfxSpotLightShadowProjection final {
    float fieldOfViewDegrees = 0.0F;
    float nearPlaneMeters = 0.0F;
    float farPlaneMeters = 0.0F;
    std::array<float, 16> lightView{};
    std::array<float, 16> lightProjection{};
    // World position -> full-map UV/depth, including backend crop rules.
    std::array<float, 16> samplingTransform{};
};

[[nodiscard]] Core::Result<BgfxSpotLightShadowProjection>
computeSpotLightShadowProjection(
    const BgfxSpotLightShadowInput& input,
    bool homogeneousDepth,
    bool originBottomLeft) noexcept;

} // namespace Tina::Render::Bgfx
