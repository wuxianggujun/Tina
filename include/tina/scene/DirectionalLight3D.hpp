#pragma once

#include <tina/render/RenderScene.hpp>

#include <cmath>
#include <optional>

namespace Tina::Scene {

// Scene-owned directional light. The entity's world-space local +Z axis points
// toward the light source (local -Z is the emitted-light direction).
struct CascadedDirectionalShadow3D final {
    float maximumDistanceMeters = 50.0F;
    float depthBias = 0.0015F;
    float normalBiasMeters = 0.02F;

    friend constexpr bool operator==(const CascadedDirectionalShadow3D&,
                                     const CascadedDirectionalShadow3D&) noexcept = default;
};

[[nodiscard]] inline bool isValid(const CascadedDirectionalShadow3D& shadow) noexcept
{
    return std::isfinite(shadow.maximumDistanceMeters) && shadow.maximumDistanceMeters > 0.0F &&
           shadow.maximumDistanceMeters <= Render::Mesh3DCascadedDirectionalShadow::MaximumDistanceMeters &&
           std::isfinite(shadow.depthBias) && shadow.depthBias >= 0.0F &&
           shadow.depthBias <= Render::Mesh3DCascadedDirectionalShadow::MaximumDepthBias &&
           std::isfinite(shadow.normalBiasMeters) && shadow.normalBiasMeters >= 0.0F &&
           shadow.normalBiasMeters <= Render::Mesh3DCascadedDirectionalShadow::MaximumNormalBiasMeters;
}

struct DirectionalLight3D final {
    Render::RenderLinearColor color{};
    float intensity = 1.0F;
    std::optional<CascadedDirectionalShadow3D> cascadedShadow{};
    bool active = true;

    friend constexpr bool operator==(const DirectionalLight3D&, const DirectionalLight3D&) noexcept = default;
};

[[nodiscard]] inline bool isValid(const DirectionalLight3D& light) noexcept
{
    return std::isfinite(light.color.red) && light.color.red >= 0.0F && std::isfinite(light.color.green) &&
           light.color.green >= 0.0F && std::isfinite(light.color.blue) && light.color.blue >= 0.0F &&
           std::isfinite(light.color.alpha) && light.color.alpha == 1.0F && std::isfinite(light.intensity) &&
           light.intensity >= 0.0F &&
           (!light.cascadedShadow.has_value() || isValid(*light.cascadedShadow));
}

} // namespace Tina::Scene
