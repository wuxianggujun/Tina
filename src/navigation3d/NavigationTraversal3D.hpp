#pragma once

#include <tina/navigation3d/NavigationPathfinder3D.hpp>

#include <array>

namespace Tina::Navigation3D::Detail {

[[nodiscard]] constexpr bool validDiagonalMode(NavigationDiagonalMode3D mode) noexcept
{
    switch (mode)
    {
    case NavigationDiagonalMode3D::Disabled:
    case NavigationDiagonalMode3D::RequireClearAdjacentColumns:
    case NavigationDiagonalMode3D::AllowCornerCutting:
        return true;
    }
    return false;
}

// A move an agent can make from one standable cell to another. Horizontal offset plus
// the vertical delta the destination sits at, with the integer cost already resolved.
struct NavigationMove3D final {
    NavigationCell3D destination{};
    Core::u32 movementCost = 0;
};

// Connectivity is symmetric in the horizontal plane but NOT vertically: a fall the agent
// can survive downhill may exceed maxStepUpCells uphill. A reverse search would have to
// re-derive the move, so this visitor is forward-only by design.
//
// Vertical transitions are cardinal-only (ADR 0048 D6): an honest diagonal step-up needs
// clearance through a 2x2xk volume, and getting it wrong is an agent clipping a wall
// corner -- exactly what the 2D corner rule exists to prevent.
template <typename Visitor>
void visitNavigationNeighbors3D(const NavigationVolume3D& volume, NavigationCell3D current,
                                const NavigationAgentProfile3D& profile,
                                NavigationDiagonalMode3D mode, Visitor&& visitor)
{
    constexpr std::array<std::array<Core::i32, 2>, 8> offsets{{
        {0, 1}, {-1, 0}, {1, 0}, {0, -1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1},
    }};
    const Core::usize directionCount = mode == NavigationDiagonalMode3D::Disabled ? 4U : 8U;
    for (Core::usize direction = 0; direction < directionCount; ++direction)
    {
        const Core::i32 x = static_cast<Core::i32>(current.x) + offsets[direction][0];
        const Core::i32 z = static_cast<Core::i32>(current.z) + offsets[direction][1];
        if (x < 0 || z < 0 || x >= static_cast<Core::i32>(volume.widthCells()) ||
            z >= static_cast<Core::i32>(volume.depthCells()))
        {
            continue;
        }
        const Core::u32 nextX = static_cast<Core::u32>(x);
        const Core::u32 nextZ = static_cast<Core::u32>(z);
        const bool diagonal = direction >= 4U;
        const Core::u32 horizontalCost =
            diagonal ? NavigationPathCost3D::Diagonal : NavigationPathCost3D::Cardinal;

        if (diagonal && mode == NavigationDiagonalMode3D::RequireClearAdjacentColumns)
        {
            // Both orthogonal columns must clear the agent's FULL height, not just the
            // foot cell (ADR 0048 D7). Checking only the floor lets a tall agent's torso
            // slice through the corner block.
            if (!volume.hasClearance({nextX, current.y, current.z}, profile) ||
                !volume.hasClearance({current.x, current.y, nextZ}, profile))
            {
                continue;
            }
        }

        // Same level first: the cheapest and by far the most common move.
        if (volume.isStandable({nextX, current.y, nextZ}, profile))
        {
            visitor(NavigationMove3D{
                .destination = {nextX, current.y, nextZ},
                .movementCost = horizontalCost,
            });
            continue;
        }

        if (diagonal)
        {
            continue;
        }

        // Step up. Bounded by maxStepUpCells and stopped by the first ceiling: an agent
        // cannot climb through a slab even if a standable cell exists above it.
        bool climbed = false;
        for (Core::u32 rise = 1; rise <= profile.maxStepUpCells; ++rise)
        {
            const Core::u32 targetY = current.y + rise;
            if (targetY >= volume.heightCells())
            {
                break;
            }
            // The agent's own column has to be clear for the body to rise through it.
            if (volume.isSolid({current.x, current.y + profile.heightCells + rise - 1U, current.z}))
            {
                break;
            }
            if (volume.isStandable({nextX, targetY, nextZ}, profile))
            {
                visitor(NavigationMove3D{
                    .destination = {nextX, targetY, nextZ},
                    .movementCost = horizontalCost + NavigationPathCost3D::StepUpPerCell * rise,
                });
                climbed = true;
                break;
            }
        }
        if (climbed)
        {
            continue;
        }

        // Fall. The destination column must be clear all the way down, otherwise the
        // agent would be passing through a block on the way to the landing cell.
        for (Core::u32 drop = 1; drop <= profile.maxFallCells; ++drop)
        {
            if (drop > current.y)
            {
                break;
            }
            const Core::u32 targetY = current.y - drop;
            if (!volume.hasClearance({nextX, targetY + 1U, nextZ}, profile))
            {
                break;
            }
            if (volume.isStandable({nextX, targetY, nextZ}, profile))
            {
                visitor(NavigationMove3D{
                    .destination = {nextX, targetY, nextZ},
                    .movementCost = horizontalCost + NavigationPathCost3D::FallPerCell * drop,
                });
                break;
            }
        }
    }
}

} // namespace Tina::Navigation3D::Detail
