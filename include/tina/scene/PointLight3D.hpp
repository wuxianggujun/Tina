#pragma once

#include <tina/render/RenderScene.hpp>

#include <cmath>

namespace Tina::Scene {

// Scene-owned point light. WorldTransform position supplies the world-space
// light center; influence radius is independent of entity scale.
struct PointLight3D final {
    Render::RenderLinearColor color{};
    float intensity = 1.0F;
    float influenceRadiusMeters = 4.0F;
    bool active = true;

    friend constexpr bool operator==(const PointLight3D&, const PointLight3D&) noexcept = default;
};

[[nodiscard]] inline bool isValid(const PointLight3D& light) noexcept
{
    return std::isfinite(light.color.red) && light.color.red >= 0.0F &&
           std::isfinite(light.color.green) && light.color.green >= 0.0F &&
           std::isfinite(light.color.blue) && light.color.blue >= 0.0F &&
           std::isfinite(light.color.alpha) && light.color.alpha == 1.0F &&
           std::isfinite(light.intensity) && light.intensity >= 0.0F &&
           std::isfinite(light.influenceRadiusMeters) && light.influenceRadiusMeters > 0.0F;
}

} // namespace Tina::Scene
