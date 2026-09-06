#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/math/Vec.hpp>

#include <memory_resource>
#include <memory>
#include <span>
#include <vector>

namespace Tina::Navigation2D {

enum class NavigationPathFollowerState2D : Core::u8 { Idle, Following, Arrived, Cancelled };

struct NavigationPathFollower2DConfig final {
    Core::usize waypointCapacity = 0;
    float speedMetersPerSecond = 1.0F;
    // Applied only to the final waypoint. Intermediate corners are not skipped
    // merely because the actor is near them; doing that could cut through walls.
    float arrivalToleranceMeters = 0.01F;
};

struct NavigationPathSteering2D final {
    NavigationPathFollowerState2D state = NavigationPathFollowerState2D::Idle;
    Math::Vec2 desiredVelocity{};
    Math::Vec2 targetPosition{};
    Core::usize waypointIndex = 0;
};

// Copies a world-space polyline and outputs velocity from the actor's ACTUAL
// position. Does not integrate position or own a Scene/Physics object. Movement
// is clamped to one waypoint per update, so a large dt never cuts across a turn.
class NavigationPathFollower2D final {
public:
    [[nodiscard]] static Core::Result<NavigationPathFollower2D> Create(
        NavigationPathFollower2DConfig config,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());
    ~NavigationPathFollower2D() noexcept = default;
    NavigationPathFollower2D(const NavigationPathFollower2D&) = delete;
    NavigationPathFollower2D& operator=(const NavigationPathFollower2D&) = delete;
    NavigationPathFollower2D(NavigationPathFollower2D&& other) noexcept;
    NavigationPathFollower2D& operator=(NavigationPathFollower2D&&) = delete;

    // Errors preserve the previous path/state; input may alias path().
    [[nodiscard]] Core::Status setPath(std::span<const Math::Vec2> waypointsMeters);
    [[nodiscard]] Core::Status setSpeed(float metersPerSecond);
    [[nodiscard]] Core::Result<NavigationPathSteering2D> update(Math::Vec2 actualPositionMeters,
                                                             float deltaSeconds);
    void cancel() noexcept;
    void reset() noexcept;
    [[nodiscard]] NavigationPathFollowerState2D state() const noexcept { return m_state; }
    // Borrowed until successful setPath(), reset(), cancel(), move or destruction.
    [[nodiscard]] std::span<const Math::Vec2> path() const noexcept;

private:
    struct Storage;
    static void destroyStorage(Storage* storage) noexcept;
    using StorageOwner = std::unique_ptr<Storage, decltype(&destroyStorage)>;
    NavigationPathFollower2D(NavigationPathFollower2DConfig config, StorageOwner storage) noexcept;
    NavigationPathFollower2DConfig m_config{};
    StorageOwner m_storage{nullptr, &destroyStorage};
    Core::usize m_waypoint = 0;
    NavigationPathFollowerState2D m_state = NavigationPathFollowerState2D::Idle;
};

} // namespace Tina::Navigation2D
