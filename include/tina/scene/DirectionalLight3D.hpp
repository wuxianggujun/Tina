#pragma once

#include <tina/render/RenderScene.hpp>

#include <cmath>

namespace Tina::Scene {

// Scene-owned directional light. The entity's world-space local +Z axis points
// toward the light source (local -Z is the emitted-light direction).
struct DirectionalLight3D final {
    Render::RenderLinearColor color{};
    float intensity = 1.0F;
    float shadowDistanceMeters = 50.0F;
    float shadowDepthBias = 0.0015F;
    float shadowNormalBiasMeters = 0.02F;
    bool active = true;
    bool castsShadows = false;

    friend constexpr bool operator==(const DirectionalLight3D&, const DirectionalLight3D&) noexcept = default;
};

[[nodiscard]] inline bool isValid(const DirectionalLight3D& light) noexcept
{
    return std::isfinite(light.color.red) && light.color.red >= 0.0F && std::isfinite(light.color.green) &&
           light.color.green >= 0.0F && std::isfinite(light.color.blue) && light.color.blue >= 0.0F &&
           std::isfinite(light.color.alpha) && light.color.alpha == 1.0F && std::isfinite(light.intensity) &&
           light.intensity >= 0.0F && std::isfinite(light.shadowDistanceMeters) &&
           light.shadowDistanceMeters > 0.0F &&
           light.shadowDistanceMeters <= Render::Mesh3DLightingDesc::MaximumDirectionalShadowDistanceMeters &&
           std::isfinite(light.shadowDepthBias) && light.shadowDepthBias >= 0.0F &&
           light.shadowDepthBias <= Render::Mesh3DLightingDesc::MaximumDirectionalShadowDepthBias &&
           std::isfinite(light.shadowNormalBiasMeters) && light.shadowNormalBiasMeters >= 0.0F &&
           light.shadowNormalBiasMeters <=
               Render::Mesh3DLightingDesc::MaximumDirectionalShadowNormalBiasMeters;
}

} // namespace Tina::Scene
