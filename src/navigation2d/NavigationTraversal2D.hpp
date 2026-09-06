#pragma once

#include <tina/navigation2d/NavigationPathfinder2D.hpp>

#include <array>

namespace Tina::Navigation2D::Detail {

[[nodiscard]] constexpr bool validDiagonalMode(NavigationDiagonalMode2D mode) noexcept
{
    switch (mode)
    {
    case NavigationDiagonalMode2D::Disabled:
    case NavigationDiagonalMode2D::RequireClearAdjacentCells:
    case NavigationDiagonalMode2D::AllowCornerCutting:
        return true;
    }
    return false;
}

// Connectivity is symmetric; traversal cost is not. A reverse search must charge
// the forward destination, not the predecessor returned by this visitor.
template <typename Visitor>
void visitNavigationNeighbors2D(const NavigationGrid2D& grid, NavigationCell2D current,
                                NavigationDiagonalMode2D mode, Visitor&& visitor)
{
    constexpr std::array<std::array<Core::i32, 2>, 8> offsets{{
        {0, 1}, {-1, 0}, {1, 0}, {0, -1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1},
    }};
    const Core::usize directionCount = mode == NavigationDiagonalMode2D::Disabled ? 4U : 8U;
    for (Core::usize direction = 0; direction < directionCount; ++direction)
    {
        const Core::i32 x = static_cast<Core::i32>(current.x) + offsets[direction][0];
        const Core::i32 y = static_cast<Core::i32>(current.y) + offsets[direction][1];
        if (x < 0 || y < 0 || x >= static_cast<Core::i32>(grid.widthCells()) ||
            y >= static_cast<Core::i32>(grid.heightCells()))
        {
            continue;
        }
        const NavigationCell2D neighbor{static_cast<Core::u32>(x), static_cast<Core::u32>(y)};
        if (grid.isBlocked(neighbor))
        {
            continue;
        }
        if (direction >= 4U && mode == NavigationDiagonalMode2D::RequireClearAdjacentCells &&
            (grid.isBlocked({neighbor.x, current.y}) || grid.isBlocked({current.x, neighbor.y})))
        {
            continue;
        }
        visitor(neighbor, direction < 4U ? NavigationPathCost2D::Cardinal : NavigationPathCost2D::Diagonal);
    }
}

struct NavigationSegmentTrace2D final {
    bool clear = false;
    // Continuous length in grid-cell units, integrated against cell multipliers.
    // This is deliberately not the A* 10/14 destination-cell pathCost.
    double weightedLength = 0.0;
};

[[nodiscard]] NavigationSegmentTrace2D traceNavigationSegment2D(
    const NavigationGrid2D& grid, double startX, double startY, double goalX, double goalY,
    NavigationDiagonalMode2D mode) noexcept;

[[nodiscard]] inline NavigationSegmentTrace2D traceNavigationCellSegment2D(
    const NavigationGrid2D& grid, NavigationCell2D start, NavigationCell2D goal,
    NavigationDiagonalMode2D mode) noexcept
{
    return traceNavigationSegment2D(grid, static_cast<double>(start.x) + 0.5,
                                    static_cast<double>(start.y) + 0.5,
                                    static_cast<double>(goal.x) + 0.5,
                                    static_cast<double>(goal.y) + 0.5, mode);
}

} // namespace Tina::Navigation2D::Detail
