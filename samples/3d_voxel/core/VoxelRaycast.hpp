#pragma once

// Amanatides-Woo DDA traversal: the first solid block a ray enters, plus the face
// it entered through.

#include "VoxelWorld.hpp"

#include <tina/core/base/Types.hpp>

#include <cmath>
#include <optional>

namespace VoxelSample {

struct VoxelRaycastHit final {
    i32 blockX = 0;
    i32 blockY = 0;
    i32 blockZ = 0;
    // Outward normal of the entered face: exactly one axis is non-zero. All three
    // are zero when the ray began inside a solid block, where no face was crossed.
    // Breaking that block is still well defined; placing against it is not.
    i32 faceX = 0;
    i32 faceY = 0;
    i32 faceZ = 0;
    float distance = 0.0F;

    [[nodiscard]] bool hasFace() const noexcept
    {
        return faceX != 0 || faceY != 0 || faceZ != 0;
    }
};

// Direction must be unit length and non-degenerate; a camera forward built from
// yaw/pitch always is.
[[nodiscard]] inline std::optional<VoxelRaycastHit> raycastVoxel(const VoxelWorld& world,
                                                                 float originX, float originY,
                                                                 float originZ, float dirX,
                                                                 float dirY, float dirZ,
                                                                 float maxDistance) noexcept
{
    // An axis the ray does not travel along must never be chosen as the next
    // boundary crossing. Dividing by its zero component yields +/-inf or, when the
    // origin sits exactly on the boundary, 0/0 = NaN -- and every NaN comparison is
    // false, which silently corrupts the axis selection below. Yaw 0 gives dirX
    // exactly 0, so this is the default camera orientation, not a corner case.
    constexpr float NeverCrossed = 1.0e30F;
    constexpr float MinimumComponent = 1.0e-9F;

    i32 voxelX = static_cast<i32>(std::floor(originX));
    i32 voxelY = static_cast<i32>(std::floor(originY));
    i32 voxelZ = static_cast<i32>(std::floor(originZ));

    const i32 stepX = dirX > 0.0F ? 1 : -1;
    const i32 stepY = dirY > 0.0F ? 1 : -1;
    const i32 stepZ = dirZ > 0.0F ? 1 : -1;

    const auto axisSetup = [](float direction, float origin, i32 voxel, float& tDelta,
                              float& tMax) noexcept {
        if (std::abs(direction) < MinimumComponent)
        {
            tDelta = NeverCrossed;
            tMax = NeverCrossed;
            return;
        }
        tDelta = std::abs(1.0F / direction);
        const float boundary =
            direction > 0.0F ? static_cast<float>(voxel + 1) : static_cast<float>(voxel);
        tMax = (boundary - origin) / direction;
    };

    float tDeltaX = 0.0F;
    float tDeltaY = 0.0F;
    float tDeltaZ = 0.0F;
    float tMaxX = 0.0F;
    float tMaxY = 0.0F;
    float tMaxZ = 0.0F;
    axisSetup(dirX, originX, voxelX, tDeltaX, tMaxX);
    axisSetup(dirY, originY, voxelY, tDeltaY, tMaxY);
    axisSetup(dirZ, originZ, voxelZ, tDeltaZ, tMaxZ);

    float travelled = 0.0F;
    i32 enteredAxis = -1;

    while (travelled <= maxDistance)
    {
        if (world.solidAt(voxelX, voxelY, voxelZ))
        {
            VoxelRaycastHit hit{};
            hit.blockX = voxelX;
            hit.blockY = voxelY;
            hit.blockZ = voxelZ;
            hit.distance = travelled;
            if (enteredAxis == 0)
            {
                hit.faceX = -stepX;
            }
            else if (enteredAxis == 1)
            {
                hit.faceY = -stepY;
            }
            else if (enteredAxis == 2)
            {
                hit.faceZ = -stepZ;
            }
            return hit;
        }

        if (tMaxX < tMaxY && tMaxX < tMaxZ)
        {
            voxelX += stepX;
            travelled = tMaxX;
            tMaxX += tDeltaX;
            enteredAxis = 0;
        }
        else if (tMaxY < tMaxZ)
        {
            voxelY += stepY;
            travelled = tMaxY;
            tMaxY += tDeltaY;
            enteredAxis = 1;
        }
        else
        {
            voxelZ += stepZ;
            travelled = tMaxZ;
            tMaxZ += tDeltaZ;
            enteredAxis = 2;
        }
    }

    return std::nullopt;
}

} // namespace VoxelSample
