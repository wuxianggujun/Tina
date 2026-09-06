#include <tina/navigation2d/NavigationAgent2D.hpp>

#include <tina/navigation2d/NavigationErrors.hpp>

#include "NavigationTraversal2D.hpp"

#include <cmath>
#include <new>
#include <utility>

namespace Tina::Navigation2D {

struct NavigationAgent2D::Storage final {
    Storage(Core::usize capacity, std::pmr::memory_resource& memory)
        : resource(&memory), waypoints(0U, &memory)
    {
        waypoints.reserve(capacity);
    }
    std::pmr::memory_resource* resource;
    std::pmr::vector<Math::Vec2> waypoints;
};

void NavigationAgent2D::destroyStorage(Storage* storage) noexcept
{
    std::pmr::polymorphic_allocator<Storage>{storage->resource}.delete_object(storage);
}

NavigationAgent2D::NavigationAgent2D(
    NavigationAgent2DConfig config, NavigationPathfinder2D pathfinder, NavigationPathSmoother2D smoother,
    NavigationPathFollower2D follower, StorageOwner storage) noexcept
    : m_config(config), m_pathfinder(std::move(pathfinder)), m_smoother(std::move(smoother)),
      m_follower(std::move(follower)), m_storage(std::move(storage))
{
}

NavigationAgent2D::NavigationAgent2D(NavigationAgent2D&& other) noexcept
    : m_config(std::exchange(other.m_config, {})), m_pathfinder(std::move(other.m_pathfinder)),
      m_smoother(std::move(other.m_smoother)), m_follower(std::move(other.m_follower)),
      m_storage(std::move(other.m_storage)), m_grid(std::exchange(other.m_grid, nullptr)),
      m_gridRevision(std::exchange(other.m_gridRevision, 0)), m_searchStart(other.m_searchStart),
      m_goal(other.m_goal), m_options(other.m_options),
      m_state(std::exchange(other.m_state, NavigationAgentState2D::Idle))
{
}

Core::Result<NavigationAgent2D> NavigationAgent2D::Create(
    NavigationAgent2DConfig config, std::pmr::memory_resource& resource)
{
    auto pathfinder = NavigationPathfinder2D::Create({.cellCapacity = config.cellCapacity}, resource);
    if (!pathfinder) { return Core::failure(std::move(pathfinder.error())); }
    auto smoother = NavigationPathSmoother2D::Create({.waypointCapacity = config.cellCapacity}, resource);
    if (!smoother) { return Core::failure(std::move(smoother.error())); }
    auto follower = NavigationPathFollower2D::Create(
        {.waypointCapacity = config.cellCapacity + 1U, .speedMetersPerSecond = config.speedMetersPerSecond,
         .arrivalToleranceMeters = config.arrivalToleranceMeters}, resource);
    if (!follower) { return Core::failure(std::move(follower.error())); }
    try
    {
        StorageOwner storage{std::pmr::polymorphic_allocator<Storage>{&resource}.new_object<Storage>(
                                 config.cellCapacity + 1U, resource), &destroyStorage};
        return NavigationAgent2D(config, std::move(*pathfinder), std::move(*smoother), std::move(*follower), std::move(storage));
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Navigation2DErrorCode::AllocationFailed, "navigation agent fixed waypoint storage allocation failed");
    }
}

Core::Status NavigationAgent2D::setGoal(
    const NavigationGrid2D& grid, Math::Vec2 actualPositionMeters, Math::Vec2 goalMeters,
    NavigationPathQueryOptions options)
{
    if (!grid || !Detail::validDiagonalMode(options.diagonalMode))
    {
        return Core::failure(Navigation2DErrorCode::InvalidData, "navigation agent requires a valid grid and corner policy");
    }
    const auto start = grid.worldToCell(actualPositionMeters);
    const auto goal = grid.worldToCell(goalMeters);
    if (!start || !goal || !grid.cellCenter(*start) || !grid.cellCenter(*goal))
    {
        return Core::failure(Navigation2DErrorCode::InvalidWorldPosition, "navigation agent start/goal is outside the representable grid");
    }
    auto query = m_pathfinder.begin(grid, *start, *goal, options);
    if (!query) { return Core::failure(std::move(query.error())); }
    m_grid = &grid;
    m_gridRevision = grid.revision();
    m_searchStart = *start;
    m_goal = goalMeters;
    m_options = options;
    m_smoother.reset();
    m_follower.reset();
    m_storage->waypoints.clear();
    m_state = query->state == NavigationPathQueryState::Unreachable ?
        NavigationAgentState2D::Unreachable : NavigationAgentState2D::Planning;
    return Core::success();
}

Core::Status NavigationAgent2D::restart(const NavigationGrid2D& grid, NavigationCell2D start)
{
    const auto goal = grid.worldToCell(m_goal);
    if (!goal)
    {
        return Core::failure(Navigation2DErrorCode::InvalidWorldPosition, "navigation agent goal no longer maps to the grid");
    }
    auto query = m_pathfinder.begin(grid, start, *goal, m_options);
    if (!query) { return Core::failure(std::move(query.error())); }
    m_gridRevision = grid.revision();
    m_searchStart = start;
    m_follower.reset();
    m_smoother.reset();
    m_storage->waypoints.clear();
    m_state = query->state == NavigationPathQueryState::Unreachable ?
        NavigationAgentState2D::Unreachable : NavigationAgentState2D::Planning;
    return Core::success();
}

NavigationDiagonalMode2D NavigationAgent2D::visibilityMode() const noexcept
{
    return m_options.diagonalMode == NavigationDiagonalMode2D::AllowCornerCutting ?
        NavigationDiagonalMode2D::AllowCornerCutting : NavigationDiagonalMode2D::RequireClearAdjacentCells;
}

Core::Status NavigationAgent2D::publishPath(const NavigationGrid2D& grid)
{
    std::span<const NavigationCell2D> cells = m_pathfinder.path();
    if (m_config.smoothPath)
    {
        if (auto status = m_smoother.smooth(grid, cells, {.diagonalMode = visibilityMode()}); !status)
        {
            return status;
        }
        cells = m_smoother.path();
    }
    m_storage->waypoints.clear();
    // Inside one free cell the direct segment is safe; do not first walk away
    // from the requested goal to visit that cell's center.
    if (cells.size() > 1U)
    {
        for (const auto cell : cells)
        {
            const auto center = grid.cellCenter(cell);
            if (!center)
            {
                return Core::failure(Navigation2DErrorCode::InvalidWorldPosition, "navigation path cell center cannot be represented in world coordinates");
            }
            m_storage->waypoints.push_back(*center);
        }
    }
    if (m_storage->waypoints.empty() || m_storage->waypoints.back() != m_goal) { m_storage->waypoints.push_back(m_goal); }
    if (auto status = m_follower.setPath(m_storage->waypoints); !status) { return status; }
    m_state = NavigationAgentState2D::Following;
    return Core::success();
}

NavigationAgentSteering2D NavigationAgent2D::stopped(Math::Vec2 position) const noexcept
{
    return {.state = m_state, .targetPosition = position, .gridRevision = m_gridRevision};
}

Core::Result<NavigationAgentSteering2D> NavigationAgent2D::update(
    const NavigationGrid2D& grid, Math::Vec2 actualPositionMeters, float deltaSeconds,
    Core::usize expansionBudget)
{
    const auto actualCell = grid.worldToCell(actualPositionMeters);
    if (!actualCell)
    {
        return Core::failure(Navigation2DErrorCode::InvalidWorldPosition, "navigation agent actual position is outside the grid or non-finite");
    }
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F || expansionBudget == 0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "navigation agent delta time and expansion budget must be positive");
    }
    if (m_state == NavigationAgentState2D::Idle)
    {
        return Core::failure(Navigation2DErrorCode::NoActiveGoal, "navigation agent has no goal");
    }
    if (m_state == NavigationAgentState2D::Cancelled || m_state == NavigationAgentState2D::Invalidated)
    {
        return stopped(actualPositionMeters);
    }
    if (&grid != m_grid || (!m_config.replanOnGridChange && grid.revision() != m_gridRevision))
    {
        m_state = NavigationAgentState2D::Invalidated;
        m_follower.reset();
        return stopped(actualPositionMeters);
    }
    if (grid.revision() != m_gridRevision ||
        (m_state == NavigationAgentState2D::Planning && *actualCell != m_searchStart))
    {
        if (auto status = restart(grid, *actualCell); !status) { return Core::failure(std::move(status.error())); }
    }
    if (m_state == NavigationAgentState2D::Arrived || m_state == NavigationAgentState2D::Unreachable)
    {
        return stopped(actualPositionMeters);
    }
    if (m_state == NavigationAgentState2D::Planning)
    {
        auto query = m_pathfinder.advance(grid, expansionBudget);
        if (!query) { return Core::failure(std::move(query.error())); }
        if (query->state == NavigationPathQueryState::Pending) { return stopped(actualPositionMeters); }
        if (query->state == NavigationPathQueryState::Unreachable)
        {
            m_state = NavigationAgentState2D::Unreachable;
            return stopped(actualPositionMeters);
        }
        if (query->state != NavigationPathQueryState::Reached)
        {
            m_state = NavigationAgentState2D::Invalidated;
            return stopped(actualPositionMeters);
        }
        if (auto status = publishPath(grid); !status)
        {
            m_state = NavigationAgentState2D::Invalidated;
            return Core::failure(std::move(status.error()));
        }
    }
    auto steering = m_follower.update(actualPositionMeters, deltaSeconds);
    if (!steering) { return Core::failure(std::move(steering.error())); }
    auto visible = hasNavigationWorldLineOfSight2D(grid, actualPositionMeters, steering->targetPosition, visibilityMode());
    if (!visible) { return Core::failure(std::move(visible.error())); }
    if (!*visible)
    {
        // Physics displacement/teleport can leave the original polyline even
        // without a grid revision. Do not steer blindly back through a wall.
        if (auto status = restart(grid, *actualCell); !status) { return Core::failure(std::move(status.error())); }
        return stopped(actualPositionMeters);
    }
    if (steering->state == NavigationPathFollowerState2D::Arrived) { m_state = NavigationAgentState2D::Arrived; }
    return NavigationAgentSteering2D{m_state, steering->desiredVelocity, steering->targetPosition,
                                      steering->waypointIndex, m_gridRevision};
}

void NavigationAgent2D::cancel() noexcept
{
    reset();
    m_state = NavigationAgentState2D::Cancelled;
}

void NavigationAgent2D::reset() noexcept
{
    m_pathfinder.reset();
    m_smoother.reset();
    m_follower.reset();
    if (m_storage) { m_storage->waypoints.clear(); }
    m_grid = nullptr;
    m_gridRevision = 0;
    m_searchStart = {};
    m_goal = {};
    m_options = {};
    m_state = NavigationAgentState2D::Idle;
}

} // namespace Tina::Navigation2D
