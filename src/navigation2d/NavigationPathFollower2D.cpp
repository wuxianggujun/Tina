#include <tina/navigation2d/NavigationPathFollower2D.hpp>

#include <tina/navigation2d/NavigationErrors.hpp>
#include <tina/navigation2d/NavigationGrid2D.hpp>

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace Tina::Navigation2D {

struct NavigationPathFollower2D::Storage final {
    Storage(Core::usize capacity, std::pmr::memory_resource& memory)
        : resource(&memory), path(0U, &memory), scratch(0U, &memory)
    {
        path.reserve(capacity);
        scratch.reserve(capacity);
    }
    std::pmr::memory_resource* resource;
    std::pmr::vector<Math::Vec2> path;
    std::pmr::vector<Math::Vec2> scratch;
};

void NavigationPathFollower2D::destroyStorage(Storage* storage) noexcept
{
    std::pmr::polymorphic_allocator<Storage>{storage->resource}.delete_object(storage);
}

NavigationPathFollower2D::NavigationPathFollower2D(NavigationPathFollower2DConfig config, StorageOwner storage) noexcept
    : m_config(config), m_storage(std::move(storage))
{
}

NavigationPathFollower2D::NavigationPathFollower2D(NavigationPathFollower2D&& other) noexcept
    : m_config(std::exchange(other.m_config, {})), m_storage(std::move(other.m_storage)),
      m_waypoint(std::exchange(other.m_waypoint, 0)),
      m_state(std::exchange(other.m_state, NavigationPathFollowerState2D::Idle))
{
}

Core::Result<NavigationPathFollower2D> NavigationPathFollower2D::Create(
    NavigationPathFollower2DConfig config, std::pmr::memory_resource& resource)
{
    if (config.waypointCapacity == 0 || config.waypointCapacity > NavigationGrid2DContract::MaximumCellCount + 1U)
    {
        return Core::failure(Navigation2DErrorCode::CapacityExceeded, "navigation follower waypoint capacity is outside the supported range");
    }
    if (!std::isfinite(config.speedMetersPerSecond) || config.speedMetersPerSecond < 0.0F ||
        !std::isfinite(config.arrivalToleranceMeters) || config.arrivalToleranceMeters < 0.0F)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "navigation follower speed and arrival tolerance must be finite and nonnegative");
    }
    try
    {
        StorageOwner storage{std::pmr::polymorphic_allocator<Storage>{&resource}.new_object<Storage>(
                                 config.waypointCapacity, resource), &destroyStorage};
        return NavigationPathFollower2D(config, std::move(storage));
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Navigation2DErrorCode::AllocationFailed, "navigation follower fixed storage allocation failed");
    }
}

Core::Status NavigationPathFollower2D::setPath(std::span<const Math::Vec2> waypointsMeters)
{
    if (waypointsMeters.empty())
    {
        return Core::failure(Navigation2DErrorCode::InvalidPath, "navigation follower requires a nonempty path");
    }
    if (waypointsMeters.size() > m_config.waypointCapacity)
    {
        return Core::failure(Navigation2DErrorCode::CapacityExceeded, "navigation path exceeds follower waypoint capacity");
    }
    for (const auto waypoint : waypointsMeters)
    {
        if (!std::isfinite(waypoint.x) || !std::isfinite(waypoint.y))
        {
            return Core::failure(Navigation2DErrorCode::InvalidWorldPosition, "navigation follower waypoint is non-finite");
        }
    }
    m_storage->scratch.assign(waypointsMeters.begin(), waypointsMeters.end());
    m_storage->path.swap(m_storage->scratch);
    m_waypoint = 0;
    m_state = NavigationPathFollowerState2D::Following;
    return Core::success();
}

Core::Status NavigationPathFollower2D::setSpeed(float metersPerSecond)
{
    if (!std::isfinite(metersPerSecond) || metersPerSecond < 0.0F)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "navigation follower speed must be finite and nonnegative");
    }
    m_config.speedMetersPerSecond = metersPerSecond;
    return Core::success();
}

Core::Result<NavigationPathSteering2D> NavigationPathFollower2D::update(
    Math::Vec2 actualPositionMeters, float deltaSeconds)
{
    if (!std::isfinite(actualPositionMeters.x) || !std::isfinite(actualPositionMeters.y))
    {
        return Core::failure(Navigation2DErrorCode::InvalidWorldPosition, "navigation follower actual position is non-finite");
    }
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "navigation follower delta time must be finite and positive");
    }
    if (m_state == NavigationPathFollowerState2D::Idle)
    {
        return Core::failure(Navigation2DErrorCode::NoActiveGoal, "navigation follower has no path");
    }
    if (m_state != NavigationPathFollowerState2D::Following)
    {
        return NavigationPathSteering2D{.state = m_state, .targetPosition = actualPositionMeters, .waypointIndex = m_waypoint};
    }
    while (m_waypoint + 1U < m_storage->path.size() && actualPositionMeters == m_storage->path[m_waypoint]) { ++m_waypoint; }
    const Math::Vec2 target = m_storage->path[m_waypoint];
    const double dx = static_cast<double>(target.x) - actualPositionMeters.x;
    const double dy = static_cast<double>(target.y) - actualPositionMeters.y;
    const double distance = std::hypot(dx, dy);
    if (m_waypoint + 1U == m_storage->path.size() && distance <= m_config.arrivalToleranceMeters)
    {
        m_state = NavigationPathFollowerState2D::Arrived;
        return NavigationPathSteering2D{.state = m_state, .targetPosition = target, .waypointIndex = m_waypoint};
    }
    const double speed = (std::min)(static_cast<double>(m_config.speedMetersPerSecond), distance / deltaSeconds);
    const Math::Vec2 velocity = distance == 0.0 ? Math::Vec2{} :
        Math::Vec2{static_cast<float>(dx / distance * speed), static_cast<float>(dy / distance * speed)};
    return NavigationPathSteering2D{m_state, velocity, target, m_waypoint};
}

void NavigationPathFollower2D::cancel() noexcept
{
    reset();
    m_state = NavigationPathFollowerState2D::Cancelled;
}

void NavigationPathFollower2D::reset() noexcept
{
    if (m_storage)
    {
        m_storage->path.clear();
        m_storage->scratch.clear();
    }
    m_waypoint = 0;
    m_state = NavigationPathFollowerState2D::Idle;
}

std::span<const Math::Vec2> NavigationPathFollower2D::path() const noexcept
{
    return m_storage ? std::span<const Math::Vec2>{m_storage->path} : std::span<const Math::Vec2>{};
}

} // namespace Tina::Navigation2D
