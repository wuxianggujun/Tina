#pragma once

#include <cmath>

namespace Tina::Scene {

// One local-space line segment that blocks authored PointLight2D illumination.
// World extraction applies the entity's published XY scale, rotation, and position.
struct ShadowOccluder2D final {
    float localStartX = -0.5F;
    float localStartY = 0.0F;
    float localEndX = 0.5F;
    float localEndY = 0.0F;
    bool active = true;

    friend constexpr bool operator==(const ShadowOccluder2D&,
                                     const ShadowOccluder2D&) noexcept = default;
};

[[nodiscard]] inline bool isValid(const ShadowOccluder2D& occluder) noexcept
{
    if (!std::isfinite(occluder.localStartX) || !std::isfinite(occluder.localStartY) ||
        !std::isfinite(occluder.localEndX) || !std::isfinite(occluder.localEndY)) {
        return false;
    }
    const float deltaX = occluder.localEndX - occluder.localStartX;
    const float deltaY = occluder.localEndY - occluder.localStartY;
    return deltaX != 0.0F || deltaY != 0.0F;
}

} // namespace Tina::Scene
