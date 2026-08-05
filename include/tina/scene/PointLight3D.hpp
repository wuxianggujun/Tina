#pragma once

#include <tina/render/RenderScene.hpp>

#include <cmath>
#include <optional>

namespace Tina::Scene {

struct PointLightShadow3D final {
    float nearPlaneMeters = 0.05F;
    float depthBias = 0.0015F;
    float normalBiasMeters = 0.02F;

    friend constexpr bool operator==(const PointLightShadow3D&,
                                     const PointLightShadow3D&) noexcept = default;
};

[[nodiscard]] inline bool isValid(const PointLightShadow3D& shadow) noexcept
{
    return std::isfinite(shadow.nearPlaneMeters) && shadow.nearPlaneMeters > 0.0F &&
           std::isfinite(shadow.depthBias) && shadow.depthBias >= 0.0F &&
           shadow.depthBias <= Render::Mesh3DPointLightShadow::MaximumDepthBias &&
           std::isfinite(shadow.normalBiasMeters) && shadow.normalBiasMeters >= 0.0F &&
           shadow.normalBiasMeters <= Render::Mesh3DPointLightShadow::MaximumNormalBiasMeters;
}

// Scene-owned point light. WorldTransform position supplies the world-space
// light center; influence radius is independent of entity scale.
struct PointLight3D final {
    Render::RenderLinearColor color{};
    float intensity = 1.0F;
    float influenceRadiusMeters = 4.0F;
    std::optional<PointLightShadow3D> shadow{};
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
           std::isfinite(light.influenceRadiusMeters) && light.influenceRadiusMeters > 0.0F &&
           (!light.shadow.has_value() ||
            (isValid(*light.shadow) && light.shadow->nearPlaneMeters < light.influenceRadiusMeters));
}

} // namespace Tina::Scene
