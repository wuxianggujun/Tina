#pragma once

#include <tina/asset/AssetErrors.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/navigation2d/NavigationGrid2D.hpp>
#include <tina/physics2d/PhysicsWorld2D.hpp>

#include <memory_resource>
#include <optional>
#include <vector>

namespace Tina::Asset {

struct PhysicsNavigationSync2DTestAccess;

struct PhysicsNavigationSync2DConfig final {
    Core::usize registrationCapacity = 64;
    std::pmr::memory_resource* memoryResource = nullptr;
};

struct PhysicsNavigationBody2DDesc final {
    Physics2D::PhysicsBodyId body{};
    // Conservative body-local bounds for navigation projection. Gameplay owns
    // this authoring contract; Physics2D does not expose backend shape geometry.
    Physics2D::PhysicsAabb2D localBoundsMeters{
        .lowerMeters = {-0.5F, -0.5F},
        .upperMeters = {0.5F, 0.5F}};
};

struct PhysicsNavigationSync2DStats final {
    Core::usize registeredBodyCount = 0;
    Core::usize publishedBlockerCount = 0;
    Core::usize lastAddedBlockerCount = 0;
    Core::usize lastUpdatedBlockerCount = 0;
    Core::usize lastRemovedBlockerCount = 0;
    Core::usize lastUnchangedBlockerCount = 0;
    Core::usize lastOutsideGridCount = 0;
    Core::usize lastRetiredBodyCount = 0;
    Core::u64 synchronizeCount = 0;
    Core::u64 totalAddedBlockerCount = 0;
    Core::u64 totalUpdatedBlockerCount = 0;
    Core::u64 totalRemovedBlockerCount = 0;
    Core::u64 totalRetiredBodyCount = 0;
};

// Owner-thread bridge that projects explicitly registered Physics2D bodies into
// Navigation2D dynamic blockers. Physics body transforms remain authoritative;
// local bounds are gameplay-authored and conservatively rasterized after rotation.
// The bridge never scans PhysicsWorld2D and never creates physics colliders.
class PhysicsNavigationSync2D final {
public:
    PhysicsNavigationSync2D() noexcept = default;
    ~PhysicsNavigationSync2D() noexcept = default;

    PhysicsNavigationSync2D(const PhysicsNavigationSync2D&) = delete;
    PhysicsNavigationSync2D& operator=(const PhysicsNavigationSync2D&) = delete;
    PhysicsNavigationSync2D(PhysicsNavigationSync2D&&) noexcept = default;
    PhysicsNavigationSync2D& operator=(PhysicsNavigationSync2D&&) noexcept = delete;

    [[nodiscard]] static Core::Result<PhysicsNavigationSync2D> Create(
        PhysicsNavigationSync2DConfig config = {});

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_registrationCapacity != 0 && m_records.size() == m_registrationCapacity;
    }

    // A body may be registered once. The PhysicsWorld2D object must remain at
    // the same address until shutdown(); stale bodies are retired by synchronize().
    [[nodiscard]] Core::Status registerBody(
        const Physics2D::PhysicsWorld2D& world,
        const PhysicsNavigationBody2DDesc& body);
    [[nodiscard]] Core::Status setLocalBounds(
        Physics2D::PhysicsBodyId body,
        const Physics2D::PhysicsAabb2D& localBoundsMeters);

    // Explicit unregister immediately removes the published blocker. The grid
    // must be the same object first passed to synchronize().
    [[nodiscard]] Core::Status unregisterBody(
        Physics2D::PhysicsBodyId body,
        Navigation2D::NavigationGrid2D& grid);

    // Preflights every registration and grid capacity before applying mutations.
    // Disabled/out-of-grid bodies temporarily publish no blocker. Destroyed bodies
    // remove their blocker and registration. No successful steady-state call grows
    // the supplied PMR storage.
    [[nodiscard]] Core::Result<PhysicsNavigationSync2DStats> synchronize(
        const Physics2D::PhysicsWorld2D& world,
        Navigation2D::NavigationGrid2D& grid);

    // Removes all blockers and registrations. Idempotent on the bound owner thread.
    // Must be called before destroying or moving the bound world/grid.
    [[nodiscard]] Core::Status shutdown(Navigation2D::NavigationGrid2D& grid) noexcept;

    [[nodiscard]] bool contains(Physics2D::PhysicsBodyId body) const noexcept;
    [[nodiscard]] Core::usize capacity() const noexcept { return m_registrationCapacity; }
    [[nodiscard]] PhysicsNavigationSync2DStats stats() const noexcept { return m_stats; }

private:
    friend struct PhysicsNavigationSync2DTestAccess;

    enum class PlannedAction : Core::u8 {
        None,
        Add,
        Update,
        Remove,
        Retire,
        RetireAndRemove,
    };

    struct Record final {
        Physics2D::PhysicsBodyId body{};
        Physics2D::PhysicsAabb2D localBoundsMeters{};
        Navigation2D::NavigationBlockerId blocker{};
        Navigation2D::NavigationCellRect2D publishedRect{};
        bool occupied = false;
    };

    PhysicsNavigationSync2D(
        Core::usize registrationCapacity,
        std::pmr::vector<Record> records,
        std::pmr::vector<Navigation2D::NavigationCellRect2D> plannedRects,
        std::pmr::vector<PlannedAction> plannedActions) noexcept;

    [[nodiscard]] Core::usize findRecord(Physics2D::PhysicsBodyId body) const noexcept;
    [[nodiscard]] Core::usize findFreeRecord() const noexcept;
    [[nodiscard]] Core::Result<std::optional<Navigation2D::NavigationCellRect2D>>
    projectBody(
        const Physics2D::PhysicsBodyState2D& bodyState,
        const Physics2D::PhysicsAabb2D& localBoundsMeters,
        const Navigation2D::NavigationGrid2D& grid) const;
    void refreshLiveStats() noexcept;
    void clearPlan() noexcept;

    Core::usize m_registrationCapacity = 0;
    std::pmr::vector<Record> m_records;
    std::pmr::vector<Navigation2D::NavigationCellRect2D> m_plannedRects;
    std::pmr::vector<PlannedAction> m_plannedActions;
    const Physics2D::PhysicsWorld2D* m_world = nullptr;
    const Navigation2D::NavigationGrid2D* m_grid = nullptr;
    PhysicsNavigationSync2DStats m_stats{};
};

} // namespace Tina::Asset
