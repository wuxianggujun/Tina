#pragma once

#include <tina/navigation2d/NavigationPathfinder2D.hpp>

#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <vector>

namespace Tina::Navigation2D {

enum class NavigationFlowFieldState2D : Core::u8 {
    Idle, Pending, Ready, Cancelled, Invalidated,
};

struct NavigationFlowField2DConfig final {
    Core::usize cellCapacity = 0;
};

struct NavigationFlowFieldResult2D final {
    NavigationFlowFieldState2D state = NavigationFlowFieldState2D::Idle;
    Core::usize expandedNodes = 0;
    Core::usize reachableCells = 0;
    Core::u64 gridRevision = 0;
};

struct NavigationFlowFieldCell2D final {
    bool reachable = false;
    // Valid only when reachable. Exact forward A* 10/14 destination-cell cost.
    Core::u32 cost = 0;
    // No next cell at the goal or at an unreachable cell; use reachable to distinguish.
    std::optional<NavigationCell2D> nextCell{};
    // Unit direction to nextCell's center, or zero at the goal/unreachable cells.
    // This is not collision avoidance or a steering vector from an arbitrary position.
    Math::Vec2 direction{};
};

// One reverse Dijkstra build serves many agents pursuing the same goal. Fixed
// storage, owner-thread, no worker; cells publish only after the entire field is Ready.
class NavigationFlowField2D final {
public:
    [[nodiscard]] static Core::Result<NavigationFlowField2D> Create(
        NavigationFlowField2DConfig config,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~NavigationFlowField2D() noexcept = default;
    NavigationFlowField2D(const NavigationFlowField2D&) = delete;
    NavigationFlowField2D& operator=(const NavigationFlowField2D&) = delete;
    NavigationFlowField2D(NavigationFlowField2D&& other) noexcept;
    NavigationFlowField2D& operator=(NavigationFlowField2D&&) = delete;

    // Invalid requests preserve the previous field. A blocked goal yields a Ready
    // field with zero reachable cells, rather than a partial or invented route.
    [[nodiscard]] Core::Result<NavigationFlowFieldResult2D> begin(
        const NavigationGrid2D& grid, NavigationCell2D goal, NavigationPathQueryOptions options = {});
    [[nodiscard]] Core::Result<NavigationFlowFieldResult2D> advance(
        const NavigationGrid2D& grid, Core::usize expansionBudget);
    [[nodiscard]] Core::Result<NavigationFlowFieldResult2D> build(
        const NavigationGrid2D& grid, NavigationCell2D goal, NavigationPathQueryOptions options = {});
    [[nodiscard]] NavigationFlowFieldResult2D cancel() noexcept;
    void reset() noexcept;
    [[nodiscard]] NavigationFlowFieldResult2D result() const noexcept;
    // A ready field must still match the supplied grid address/revision. No stale
    // direction can be sampled after a dynamic blocker mutation or grid move.
    [[nodiscard]] Core::Result<NavigationFlowFieldCell2D> sample(
        const NavigationGrid2D& grid, NavigationCell2D cell) const;
    [[nodiscard]] bool isCurrent(const NavigationGrid2D& grid) const noexcept;
    [[nodiscard]] Core::usize cellCapacity() const noexcept { return m_capacity; }

private:
    inline static constexpr Core::u32 InvalidIndex = (std::numeric_limits<Core::u32>::max)();
    struct CellRecord final {
        Core::u32 epoch = 0;
        Core::u32 cost = InvalidIndex;
        Core::u32 nextIndex = InvalidIndex;
        Core::u32 heapIndex = InvalidIndex;
        bool closed = false;
    };
    struct Storage;
    static void destroyStorage(Storage* storage) noexcept;
    using StorageOwner = std::unique_ptr<Storage, decltype(&destroyStorage)>;
    NavigationFlowField2D(Core::usize capacity, StorageOwner storage) noexcept;
    void startNewEpoch() noexcept;
    [[nodiscard]] CellRecord& recordFor(Core::u32 index) noexcept;
    [[nodiscard]] bool higherPriority(Core::u32 left, Core::u32 right) const noexcept;
    void setTerminal(NavigationFlowFieldState2D state) noexcept;

    Core::usize m_capacity = 0;
    StorageOwner m_storage{nullptr, &destroyStorage};
    const NavigationGrid2D* m_grid = nullptr;
    Core::u64 m_gridRevision = 0;
    Core::u32 m_epoch = 0;
    Core::u32 m_width = 0;
    Core::usize m_expanded = 0;
    NavigationPathQueryOptions m_options{};
    NavigationFlowFieldState2D m_state = NavigationFlowFieldState2D::Idle;
};

} // namespace Tina::Navigation2D
