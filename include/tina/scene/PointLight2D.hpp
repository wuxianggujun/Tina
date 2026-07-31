#pragma once

#include <tina/render/RenderScene.hpp>

#include <cmath>

namespace Tina::Scene {

// Scene-owned unshadowed point light. WorldTransform position supplies the
// light center; radiusMeters is an explicit world-space radius and ignores scale.
struct PointLight2D final {
    Render::RenderLinearColor color{};
    float intensity = 1.0F;
    float radiusMeters = 4.0F;
    bool active = true;

    friend constexpr bool operator==(const PointLight2D&, const PointLight2D&) noexcept = default;
};

[[nodiscard]] inline bool isValid(const PointLight2D& light) noexcept
{
    return std::isfinite(light.color.red) && light.color.red >= 0.0F &&
           std::isfinite(light.color.green) && light.color.green >= 0.0F &&
           std::isfinite(light.color.blue) && light.color.blue >= 0.0F &&
           std::isfinite(light.color.alpha) && light.color.alpha == 1.0F &&
           std::isfinite(light.intensity) && light.intensity >= 0.0F &&
           std::isfinite(light.radiusMeters) && light.radiusMeters > 0.0F;
}

} // namespace Tina::Scene
