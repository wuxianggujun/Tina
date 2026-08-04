#pragma once

#include <tina/render/RenderScene.hpp>

#include <cmath>
#include <numbers>
#include <optional>

namespace Tina::Scene {

struct SpotLightShadow3D final {
    float nearPlaneMeters = 0.05F;
    float depthBias = 0.0015F;
    float normalBiasMeters = 0.02F;

    friend constexpr bool operator==(const SpotLightShadow3D&,
                                     const SpotLightShadow3D&) noexcept = default;
};

[[nodiscard]] inline bool isValid(const SpotLightShadow3D& shadow) noexcept
{
    return std::isfinite(shadow.nearPlaneMeters) && shadow.nearPlaneMeters > 0.0F &&
           std::isfinite(shadow.depthBias) && shadow.depthBias >= 0.0F &&
           shadow.depthBias <= Render::Mesh3DSpotLightShadow::MaximumDepthBias &&
           std::isfinite(shadow.normalBiasMeters) && shadow.normalBiasMeters >= 0.0F &&
           shadow.normalBiasMeters <= Render::Mesh3DSpotLightShadow::MaximumNormalBiasMeters;
}

// Scene-owned spot light. WorldTransform position supplies the light center,
// while WorldTransform local -Z supplies the world-space emission direction.
// Influence radius and cone angles are independent of entity scale.
struct SpotLight3D final {
    Render::RenderLinearColor color{};
    float intensity = 1.0F;
    float influenceRadiusMeters = 4.0F;
    float innerConeHalfAngleDegrees = 20.0F;
    float outerConeHalfAngleDegrees = 30.0F;
    std::optional<SpotLightShadow3D> shadow{};
    bool active = true;

    friend constexpr bool operator==(const SpotLight3D&, const SpotLight3D&) noexcept = default;
};

[[nodiscard]] inline float spotLightConeCosine(float halfAngleDegrees) noexcept
{
    return static_cast<float>(std::cos(
        static_cast<double>(halfAngleDegrees) * std::numbers::pi / 180.0));
}

[[nodiscard]] inline bool isValid(const SpotLight3D& light) noexcept
{
    const float innerConeCosine = spotLightConeCosine(light.innerConeHalfAngleDegrees);
    const float outerConeCosine = spotLightConeCosine(light.outerConeHalfAngleDegrees);
    return std::isfinite(light.color.red) && light.color.red >= 0.0F &&
           std::isfinite(light.color.green) && light.color.green >= 0.0F &&
           std::isfinite(light.color.blue) && light.color.blue >= 0.0F &&
           std::isfinite(light.color.alpha) && light.color.alpha == 1.0F &&
           std::isfinite(light.intensity) && light.intensity >= 0.0F &&
           std::isfinite(light.influenceRadiusMeters) && light.influenceRadiusMeters > 0.0F &&
           std::isfinite(light.innerConeHalfAngleDegrees) &&
           std::isfinite(light.outerConeHalfAngleDegrees) &&
           light.innerConeHalfAngleDegrees >= 0.0F &&
           light.innerConeHalfAngleDegrees < light.outerConeHalfAngleDegrees &&
           light.outerConeHalfAngleDegrees < 90.0F && std::isfinite(innerConeCosine) &&
           std::isfinite(outerConeCosine) && innerConeCosine > outerConeCosine &&
           (!light.shadow.has_value() ||
            (isValid(*light.shadow) && light.shadow->nearPlaneMeters < light.influenceRadiusMeters));
}

} // namespace Tina::Scene

