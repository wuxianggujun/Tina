#pragma once

#include <tina/render/RenderScene.hpp>

#include <cmath>

namespace Tina::Scene {

// Scene-owned directional light. The entity's world-space local +Z axis points
// toward the light source (local -Z is the emitted-light direction).
struct DirectionalLight3D final {
    Render::RenderLinearColor color{};
    float intensity = 1.0F;
    bool active = true;

    friend constexpr bool operator==(const DirectionalLight3D&, const DirectionalLight3D&) noexcept = default;
};

[[nodiscard]] inline bool isValid(const DirectionalLight3D& light) noexcept
{
    return std::isfinite(light.color.red) && light.color.red >= 0.0F && std::isfinite(light.color.green) &&
           light.color.green >= 0.0F && std::isfinite(light.color.blue) && light.color.blue >= 0.0F &&
           std::isfinite(light.color.alpha) && light.color.alpha == 1.0F && std::isfinite(light.intensity) &&
           light.intensity >= 0.0F;
}

} // namespace Tina::Scene
