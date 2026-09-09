#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderScene.hpp>

#include <array>

namespace Tina::Render::Shadow {

inline constexpr usize PointLightShadowFaceCount = 6U;
static_assert(PointLightShadowFaceCount == Mesh3DPointLightShadow::FaceCount);

struct PointLightShadowInput final {
    Mesh3DPointLight light{};
    float nearPlaneMeters = 0.1F;
};

struct PointLightShadowFace final {
    std::array<float, 16> lightView{};
    std::array<float, 16> lightProjection{};
    // World position -> face-local UV/depth, including backend crop rules.
    std::array<float, 16> samplingTransform{};
};

struct PointLightShadowProjection final {
    float nearPlaneMeters = 0.0F;
    float farPlaneMeters = 0.0F;
    std::array<PointLightShadowFace, PointLightShadowFaceCount> faces{};
};

[[nodiscard]] Core::Result<PointLightShadowProjection>
computePointLightShadowProjection(
    const PointLightShadowInput& input,
    bool homogeneousDepth,
    bool originBottomLeft) noexcept;

} // namespace Tina::Render::Shadow
