#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/render/RenderScene.hpp>

#include <cmath>

namespace Tina::Scene {

// Scene-owned perspective camera. Pose comes from WorldTransform on the same
// entity (local -Z forward, local +Y up). Aspect is resolved by RenderScene from
// frame parameters, not stored on this component.
struct PerspectiveCamera3D final {
    float verticalFovDegrees = 60.0F;
    float nearPlaneMeters = 0.1F;
    float farPlaneMeters = 1000.0F;
    Render::RenderNormalizedViewport normalizedViewport{};
    bool active = true;

    friend constexpr bool operator==(const PerspectiveCamera3D&,
                                     const PerspectiveCamera3D&) noexcept = default;
};

[[nodiscard]] inline bool isFiniteViewport(const Render::RenderNormalizedViewport& viewport) noexcept
{
    return std::isfinite(viewport.x) && std::isfinite(viewport.y) && std::isfinite(viewport.width)
        && std::isfinite(viewport.height);
}

[[nodiscard]] inline bool isValid(const PerspectiveCamera3D& camera) noexcept
{
    if (!std::isfinite(camera.verticalFovDegrees) || camera.verticalFovDegrees <= 0.0F
        || camera.verticalFovDegrees >= 180.0F)
    {
        return false;
    }
    if (!std::isfinite(camera.nearPlaneMeters) || !std::isfinite(camera.farPlaneMeters)
        || camera.nearPlaneMeters <= 0.0F || camera.farPlaneMeters <= camera.nearPlaneMeters)
    {
        return false;
    }
    if (!isFiniteViewport(camera.normalizedViewport))
    {
        return false;
    }
    if (camera.normalizedViewport.x < 0.0F || camera.normalizedViewport.y < 0.0F
        || camera.normalizedViewport.width <= 0.0F || camera.normalizedViewport.height <= 0.0F)
    {
        return false;
    }
    if (camera.normalizedViewport.x + camera.normalizedViewport.width > 1.0F + 1.0e-6F
        || camera.normalizedViewport.y + camera.normalizedViewport.height > 1.0F + 1.0e-6F)
    {
        return false;
    }
    return true;
}

} // namespace Tina::Scene
