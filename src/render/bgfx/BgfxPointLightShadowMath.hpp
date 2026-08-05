#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderScene.hpp>

#include <array>

namespace Tina::Render::Bgfx {

inline constexpr usize BgfxPointLightShadowFaceCount = 6U;
static_assert(BgfxPointLightShadowFaceCount == Mesh3DPointLightShadow::FaceCount);

struct BgfxPointLightShadowInput final {
    Mesh3DPointLight light{};
    float nearPlaneMeters = 0.1F;
};

struct BgfxPointLightShadowFace final {
    std::array<float, 16> lightView{};
    std::array<float, 16> lightProjection{};
    // World position -> face-local UV/depth, including backend crop rules.
    std::array<float, 16> samplingTransform{};
};

struct BgfxPointLightShadowProjection final {
    float nearPlaneMeters = 0.0F;
    float farPlaneMeters = 0.0F;
    std::array<BgfxPointLightShadowFace, BgfxPointLightShadowFaceCount> faces{};
};

[[nodiscard]] Core::Result<BgfxPointLightShadowProjection>
computePointLightShadowProjection(
    const BgfxPointLightShadowInput& input,
    bool homogeneousDepth,
    bool originBottomLeft) noexcept;

} // namespace Tina::Render::Bgfx
