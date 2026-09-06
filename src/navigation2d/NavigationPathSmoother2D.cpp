#include <tina/navigation2d/NavigationPathSmoother2D.hpp>

#include <tina/navigation2d/NavigationErrors.hpp>

#include "NavigationTraversal2D.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace Tina::Navigation2D {
namespace Detail {

NavigationSegmentTrace2D traceNavigationSegment2D(
    const NavigationGrid2D& grid, double startX, double startY, double goalX, double goalY,
    NavigationDiagonalMode2D mode) noexcept
{
    if (!(startX >= 0.0 && startX < grid.widthCells() && startY >= 0.0 && startY < grid.heightCells() &&
          goalX >= 0.0 && goalX < grid.widthCells() && goalY >= 0.0 && goalY < grid.heightCells()))
    {
        return {};
    }
    Core::i32 x = static_cast<Core::i32>(startX);
    Core::i32 y = static_cast<Core::i32>(startY);
    const Core::i32 goalCellX = static_cast<Core::i32>(goalX);
    const Core::i32 goalCellY = static_cast<Core::i32>(goalY);
    const double dx = goalX - startX;
    const double dy = goalY - startY;
    if (mode == NavigationDiagonalMode2D::Disabled && dx != 0.0 && dy != 0.0)
    {
        return {};
    }
    const bool strict = mode != NavigationDiagonalMode2D::AllowCornerCutting;
    const bool alongVerticalBoundary = dx == 0.0 && startX == std::floor(startX);
    const bool alongHorizontalBoundary = dy == 0.0 && startY == std::floor(startY);
    const auto clearCell = [&grid](Core::i32 cellX, Core::i32 cellY) noexcept {
        return cellX >= 0 && cellY >= 0 &&
               !grid.isBlocked({static_cast<Core::u32>(cellX), static_cast<Core::u32>(cellY)});
    };
    if (!clearCell(goalCellX, goalCellY))
    {
        return {};
    }
    const Core::i32 stepX = dx > 0.0 ? 1 : (dx < 0.0 ? -1 : 0);
    const Core::i32 stepY = dy > 0.0 ? 1 : (dy < 0.0 ? -1 : 0);
    const double length = std::hypot(dx, dy);
    const double infinity = (std::numeric_limits<double>::infinity)();
    double previousT = 0.0;
    double weightedLength = 0.0;
    const Core::usize visitLimit = static_cast<Core::usize>(grid.widthCells()) + grid.heightCells() + 2U;
    for (Core::usize visited = 0; visited < visitLimit; ++visited)
    {
        if (!clearCell(x, y) ||
            (strict && alongVerticalBoundary && x > 0 && !clearCell(x - 1, y)) ||
            (strict && alongHorizontalBoundary && y > 0 && !clearCell(x, y - 1)))
        {
            return {};
        }
        if (x == goalCellX && y == goalCellY)
        {
            weightedLength += (1.0 - previousT) * length *
                              grid.traversalCostAt({static_cast<Core::u32>(x), static_cast<Core::u32>(y)});
            return {true, weightedLength};
        }
        // Recompute from the integer boundary instead of accumulating tDelta.
        // That keeps an exact center-to-center corner deterministic in both directions.
        const double nextX = stepX == 0 ? infinity :
            (static_cast<double>(x + (stepX > 0 ? 1 : 0)) - startX) / dx;
        const double nextY = stepY == 0 ? infinity :
            (static_cast<double>(y + (stepY > 0 ? 1 : 0)) - startY) / dy;
        const double nextT = (std::min)(1.0, (std::min)(nextX, nextY));
        weightedLength += (std::max)(0.0, nextT - previousT) * length *
                          grid.traversalCostAt({static_cast<Core::u32>(x), static_cast<Core::u32>(y)});
        const bool crossX = nextX <= nextY;
        const bool crossY = nextY <= nextX;
        if (crossX && crossY && strict &&
            (!clearCell(x + stepX, y) || !clearCell(x, y + stepY)))
        {
            return {};
        }
        if (crossX) { x += stepX; }
        if (crossY) { y += stepY; }
        previousT = nextT;
    }
    return {};
}

} // namespace Detail

Core::Result<bool> hasNavigationLineOfSight2D(
    const NavigationGrid2D& grid, NavigationCell2D start, NavigationCell2D goal,
    NavigationDiagonalMode2D mode)
{
    if (!grid || !Detail::validDiagonalMode(mode))
    {
        return Core::failure(Navigation2DErrorCode::InvalidData, "navigation visibility requires a valid grid and corner policy");
    }
    if (!grid.inBounds(start) || !grid.inBounds(goal))
    {
        return Core::failure(Navigation2DErrorCode::InvalidCell, "navigation visibility endpoint is outside the grid");
    }
    return Detail::traceNavigationCellSegment2D(grid, start, goal, mode).clear;
}

Core::Result<bool> hasNavigationWorldLineOfSight2D(
    const NavigationGrid2D& grid, Math::Vec2 startMeters, Math::Vec2 goalMeters,
    NavigationDiagonalMode2D mode)
{
    if (!grid || !Detail::validDiagonalMode(mode))
    {
        return Core::failure(Navigation2DErrorCode::InvalidData, "navigation visibility requires a valid grid and corner policy");
    }
    if (!grid.worldToCell(startMeters) || !grid.worldToCell(goalMeters))
    {
        return Core::failure(Navigation2DErrorCode::InvalidWorldPosition, "navigation visibility world endpoint is outside the grid or non-finite");
    }
    return Detail::traceNavigationSegment2D(
        grid, (static_cast<double>(startMeters.x) - grid.originXMeters()) / grid.cellSizeMeters(),
        (static_cast<double>(startMeters.y) - grid.originYMeters()) / grid.cellSizeMeters(),
        (static_cast<double>(goalMeters.x) - grid.originXMeters()) / grid.cellSizeMeters(),
        (static_cast<double>(goalMeters.y) - grid.originYMeters()) / grid.cellSizeMeters(), mode).clear;
}

struct NavigationPathSmoother2D::Storage final {
    Storage(Core::usize capacity, std::pmr::memory_resource& memory)
        : resource(&memory), path(0U, &memory), scratch(0U, &memory), prefixCosts(capacity, &memory)
    {
        path.reserve(capacity);
        scratch.reserve(capacity);
    }
    std::pmr::memory_resource* resource;
    std::pmr::vector<NavigationCell2D> path;
    std::pmr::vector<NavigationCell2D> scratch;
    std::pmr::vector<double> prefixCosts;
};

void NavigationPathSmoother2D::destroyStorage(Storage* storage) noexcept
{
    std::pmr::polymorphic_allocator<Storage>{storage->resource}.delete_object(storage);
}

NavigationPathSmoother2D::NavigationPathSmoother2D(Core::usize capacity, StorageOwner storage) noexcept
    : m_capacity(capacity), m_storage(std::move(storage))
{
}

NavigationPathSmoother2D::NavigationPathSmoother2D(NavigationPathSmoother2D&& other) noexcept
    : m_capacity(std::exchange(other.m_capacity, 0)), m_storage(std::move(other.m_storage)),
      m_grid(std::exchange(other.m_grid, nullptr)), m_gridRevision(std::exchange(other.m_gridRevision, 0))
{
}

Core::Result<NavigationPathSmoother2D> NavigationPathSmoother2D::Create(
    NavigationPathSmoother2DConfig config, std::pmr::memory_resource& resource)
{
    if (config.waypointCapacity == 0 || config.waypointCapacity > NavigationGrid2DContract::MaximumCellCount)
    {
        return Core::failure(Navigation2DErrorCode::CapacityExceeded, "navigation smoother waypoint capacity is outside the supported range");
    }
    try
    {
        StorageOwner storage{std::pmr::polymorphic_allocator<Storage>{&resource}.new_object<Storage>(
                                 config.waypointCapacity, resource), &destroyStorage};
        return NavigationPathSmoother2D(config.waypointCapacity, std::move(storage));
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Navigation2DErrorCode::AllocationFailed, "navigation smoother fixed storage allocation failed");
    }
}

Core::Status NavigationPathSmoother2D::smooth(
    const NavigationGrid2D& grid, std::span<const NavigationCell2D> input,
    NavigationPathSmoothingOptions options)
{
    if (!grid || !Detail::validDiagonalMode(options.diagonalMode))
    {
        return Core::failure(Navigation2DErrorCode::InvalidData, "navigation smoothing requires a valid grid and corner policy");
    }
    if (input.empty())
    {
        return Core::failure(Navigation2DErrorCode::InvalidPath, "navigation smoothing requires a nonempty path");
    }
    if (input.size() > m_capacity)
    {
        return Core::failure(Navigation2DErrorCode::CapacityExceeded, "navigation path exceeds smoother waypoint capacity");
    }
    m_storage->prefixCosts[0] = 0.0;
    for (Core::usize index = 0; index < input.size(); ++index)
    {
        if (grid.isBlocked(input[index]))
        {
            return Core::failure(Navigation2DErrorCode::InvalidPath, "navigation path contains a blocked or out-of-bounds cell");
        }
        if (index != 0)
        {
            const auto segment = Detail::traceNavigationCellSegment2D(grid, input[index - 1], input[index], options.diagonalMode);
            if (!segment.clear)
            {
                return Core::failure(Navigation2DErrorCode::InvalidPath, "navigation path contains a segment rejected by the corner policy");
            }
            m_storage->prefixCosts[index] = m_storage->prefixCosts[index - 1] + segment.weightedLength;
        }
    }

    m_storage->scratch.clear();
    m_storage->scratch.push_back(input.front());
    Core::usize anchor = 0;
    while (anchor + 1 < input.size())
    {
        Core::usize candidate = input.size() - 1;
        for (; candidate > anchor + 1; --candidate)
        {
            const auto segment = Detail::traceNavigationCellSegment2D(grid, input[anchor], input[candidate], options.diagonalMode);
            const double originalCost = m_storage->prefixCosts[candidate] - m_storage->prefixCosts[anchor];
            constexpr double relativeCostTolerance = 1.0e-10;
            if (segment.clear && (!options.preserveTraversalCost ||
                segment.weightedLength <= originalCost + relativeCostTolerance * (std::max)(1.0, originalCost)))
            {
                break;
            }
        }
        if (m_storage->scratch.back() != input[candidate])
        {
            m_storage->scratch.push_back(input[candidate]);
        }
        anchor = candidate;
    }
    m_storage->path.swap(m_storage->scratch);
    m_grid = &grid;
    m_gridRevision = grid.revision();
    return Core::success();
}

void NavigationPathSmoother2D::reset() noexcept
{
    if (m_storage)
    {
        m_storage->path.clear();
        m_storage->scratch.clear();
    }
    m_grid = nullptr;
    m_gridRevision = 0;
}

bool NavigationPathSmoother2D::isCurrent(const NavigationGrid2D& grid) const noexcept
{
    return m_grid == &grid && grid.revision() == m_gridRevision && !path().empty();
}

std::span<const NavigationCell2D> NavigationPathSmoother2D::path() const noexcept
{
    return m_storage ? std::span<const NavigationCell2D>{m_storage->path} : std::span<const NavigationCell2D>{};
}

} // namespace Tina::Navigation2D
