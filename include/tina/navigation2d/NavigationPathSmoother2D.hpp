#pragma once

#include <tina/navigation2d/NavigationPathfinder2D.hpp>

#include <memory_resource>
#include <memory>
#include <span>
#include <vector>

namespace Tina::Navigation2D {

struct NavigationPathSmoothingOptions final {
    // Independent of the search policy: a four-way A* path may be string-pulled
    // with strict diagonal visibility. Disabled retains axis-aligned segments.
    NavigationDiagonalMode2D diagonalMode = NavigationDiagonalMode2D::RequireClearAdjacentCells;
    // A shortcut may not increase the continuous, terrain-weighted length of
    // the replaced polyline. Disabling this explicitly ignores terrain weights.
    bool preserveTraversalCost = true;
};

[[nodiscard]] Core::Result<bool> hasNavigationLineOfSight2D(
    const NavigationGrid2D& grid, NavigationCell2D start, NavigationCell2D goal,
    NavigationDiagonalMode2D mode = NavigationDiagonalMode2D::RequireClearAdjacentCells);

[[nodiscard]] Core::Result<bool> hasNavigationWorldLineOfSight2D(
    const NavigationGrid2D& grid, Math::Vec2 startMeters, Math::Vec2 goalMeters,
    NavigationDiagonalMode2D mode = NavigationDiagonalMode2D::RequireClearAdjacentCells);

struct NavigationPathSmoother2DConfig final {
    Core::usize waypointCapacity = 0;
};

// Point-agent string pulling, not a clearance/radius or local-avoidance solver.
// Owns both publication and scratch storage; smooth() is allocation-free after
// Create and may safely consume this same owner's previously published path().
class NavigationPathSmoother2D final {
public:
    [[nodiscard]] static Core::Result<NavigationPathSmoother2D> Create(
        NavigationPathSmoother2DConfig config,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~NavigationPathSmoother2D() noexcept = default;
    NavigationPathSmoother2D(const NavigationPathSmoother2D&) = delete;
    NavigationPathSmoother2D& operator=(const NavigationPathSmoother2D&) = delete;
    NavigationPathSmoother2D(NavigationPathSmoother2D&& other) noexcept;
    NavigationPathSmoother2D& operator=(NavigationPathSmoother2D&&) = delete;

    // Validates every input segment before publishing. Errors preserve path().
    [[nodiscard]] Core::Status smooth(const NavigationGrid2D& grid,
                                      std::span<const NavigationCell2D> input,
                                      NavigationPathSmoothingOptions options = {});
    void reset() noexcept;
    // Borrowed until the next successful smooth(), reset(), move or destruction.
    [[nodiscard]] std::span<const NavigationCell2D> path() const noexcept;
    [[nodiscard]] Core::usize waypointCapacity() const noexcept { return m_capacity; }
    [[nodiscard]] Core::u64 gridRevision() const noexcept { return m_gridRevision; }
    [[nodiscard]] bool isCurrent(const NavigationGrid2D& grid) const noexcept;

private:
    struct Storage;
    static void destroyStorage(Storage* storage) noexcept;
    using StorageOwner = std::unique_ptr<Storage, decltype(&destroyStorage)>;
    NavigationPathSmoother2D(Core::usize capacity, StorageOwner storage) noexcept;

    Core::usize m_capacity = 0;
    StorageOwner m_storage{nullptr, &destroyStorage};
    const NavigationGrid2D* m_grid = nullptr;
    Core::u64 m_gridRevision = 0;
};

} // namespace Tina::Navigation2D
