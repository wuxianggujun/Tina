#pragma once

#include <bx/math.h>

#include <algorithm>

namespace Tina::Engine {

// bx::mtxProj takes a vertical field of view in degrees. Keeping this wrapper
// next to the camera contract prevents an accidental radians conversion from
// turning an ordinary 60-degree camera into a 1-degree full-screen close-up.
inline void buildRightHandedPerspective(float* output,
                                        float verticalFovDegrees,
                                        float aspect,
                                        float nearPlane,
                                        float farPlane,
                                        bool homogeneousDepth) noexcept
{
    const float safeFovDegrees = std::clamp(verticalFovDegrees, 1.0f, 179.0f);
    const float safeAspect = std::max(aspect, 0.001f);
    const float safeNear = std::max(nearPlane, 0.001f);
    const float safeFar = std::max(farPlane, safeNear + 0.001f);
    bx::mtxProj(output, safeFovDegrees, safeAspect, safeNear, safeFar,
                homogeneousDepth, bx::Handedness::Right);
}

} // namespace Tina::Engine
