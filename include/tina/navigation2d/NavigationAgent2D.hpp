#pragma once

#include <tina/navigation2d/NavigationPathFollower2D.hpp>
#include <tina/navigation2d/NavigationPathSmoother2D.hpp>

namespace Tina::Navigation2D {

enum class NavigationAgentState2D : Core::u8 {
    Idle, Planning, Following, Arrived, Unreachable, Cancelled, Invalidated,
};

struct NavigationAgent2DConfig final {
    Core::usize cellCapacity = 0;
    float speedMetersPerSecond = 1.0F;
    float arrivalToleranceMeters = 0.01F;
    bool smoothPath = true;
    bool replanOnGridChange = true;
};

struct NavigationAgentSteering2D final {
    NavigationAgentState2D state = NavigationAgentState2D::Idle;
    Math::Vec2 desiredVelocity{};
    Math::Vec2 targetPosition{};
    Core::usize waypointIndex = 0;
    Core::u64 gridRevision = 0;
};

// Product-owned point agent: cooperative A* -> terrain-aware string pulling ->
// world-space follower. The caller supplies actual position AFTER physics and
// applies desiredVelocity itself; the agent never moves Scene/Physics objects.
class NavigationAgent2D final {
public:
    [[nodiscard]] static Core::Result<NavigationAgent2D> Create(
        NavigationAgent2DConfig config,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());
    ~NavigationAgent2D() noexcept = default;
    NavigationAgent2D(const NavigationAgent2D&) = delete;
    NavigationAgent2D& operator=(const NavigationAgent2D&) = delete;
    NavigationAgent2D(NavigationAgent2D&& other) noexcept;
    NavigationAgent2D& operator=(NavigationAgent2D&&) = delete;

    // Invalid arguments preserve the active goal. Pending search is not a borrowed
    // path: the agent owns every path/waypoint and all storage is allocated at Create.
    [[nodiscard]] Core::Status setGoal(const NavigationGrid2D& grid, Math::Vec2 actualPositionMeters,
                                       Math::Vec2 goalMeters, NavigationPathQueryOptions options = {});
    [[nodiscard]] Core::Result<NavigationAgentSteering2D> update(
        const NavigationGrid2D& grid, Math::Vec2 actualPositionMeters, float deltaSeconds,
        Core::usize expansionBudget);
    [[nodiscard]] Core::Status setSpeed(float metersPerSecond) { return m_follower.setSpeed(metersPerSecond); }
    void cancel() noexcept;
    void reset() noexcept;
    [[nodiscard]] NavigationAgentState2D state() const noexcept { return m_state; }
    // Diagnostic polyline. Borrowed until replan, successful setGoal, reset/cancel,
    // move or destruction. A grid change invalidates it even before update is called.
    [[nodiscard]] std::span<const Math::Vec2> path() const noexcept { return m_follower.path(); }

private:
    struct Storage;
    static void destroyStorage(Storage* storage) noexcept;
    using StorageOwner = std::unique_ptr<Storage, decltype(&destroyStorage)>;
    NavigationAgent2D(NavigationAgent2DConfig config, NavigationPathfinder2D pathfinder,
                       NavigationPathSmoother2D smoother, NavigationPathFollower2D follower,
                       StorageOwner storage) noexcept;
    [[nodiscard]] Core::Status restart(const NavigationGrid2D& grid, NavigationCell2D start);
    [[nodiscard]] Core::Status publishPath(const NavigationGrid2D& grid);
    [[nodiscard]] NavigationAgentSteering2D stopped(Math::Vec2 position) const noexcept;
    [[nodiscard]] NavigationDiagonalMode2D visibilityMode() const noexcept;

    NavigationAgent2DConfig m_config{};
    NavigationPathfinder2D m_pathfinder;
    NavigationPathSmoother2D m_smoother;
    NavigationPathFollower2D m_follower;
    StorageOwner m_storage{nullptr, &destroyStorage};
    const NavigationGrid2D* m_grid = nullptr;
    Core::u64 m_gridRevision = 0;
    NavigationCell2D m_searchStart{};
    Math::Vec2 m_goal{};
    NavigationPathQueryOptions m_options{};
    NavigationAgentState2D m_state = NavigationAgentState2D::Idle;
};

} // namespace Tina::Navigation2D
