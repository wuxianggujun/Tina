#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/navigation2d/NavigationGrid2D.hpp>

#include <limits>
#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::Navigation2D {

enum class NavigationPathQueryState : Core::u8 {
    Idle = 0,
    Pending = 1,
    Reached = 2,
    Unreachable = 3,
    Cancelled = 4,
    Invalidated = 5,
};

struct NavigationPathQueryResult final {
    NavigationPathQueryState state = NavigationPathQueryState::Idle;
    Core::usize expandedNodes = 0;
    Core::usize pathCellCount = 0;
    Core::u64 gridRevision = 0;
};

struct NavigationPathfinder2DConfig final {
    // Maximum grid cells accepted by begin()/findPath(). Create performs all
    // persistent allocations for records, open set, and final path storage.
    Core::usize cellCapacity = 0;
};

// Reusable owner-thread A* query. Four-way movement has unit cost. Ties are
// deterministic by f-cost, then heuristic, then row-major cell index.
class NavigationPathfinder2D final {
public:
    [[nodiscard]] static Core::Result<NavigationPathfinder2D> Create(
        NavigationPathfinder2DConfig config,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~NavigationPathfinder2D() noexcept = default;

    NavigationPathfinder2D(const NavigationPathfinder2D&) = delete;
    NavigationPathfinder2D& operator=(const NavigationPathfinder2D&) = delete;
    NavigationPathfinder2D(NavigationPathfinder2D&& other) noexcept;
    NavigationPathfinder2D& operator=(NavigationPathfinder2D&&) = delete;

    // begin() publishes a fresh query only after all request validation succeeds.
    // A blocked start/goal is a deterministic Unreachable result, not an API error.
    [[nodiscard]] Core::Result<NavigationPathQueryResult>
    begin(const NavigationGrid2D& grid, NavigationCell2D start, NavigationCell2D goal);

    // Expands at most expansionBudget nodes. The same grid object must be supplied
    // for every step; address or revision changes terminate as Invalidated.
    [[nodiscard]] Core::Result<NavigationPathQueryResult>
    advance(const NavigationGrid2D& grid, Core::usize expansionBudget);

    [[nodiscard]] Core::Result<NavigationPathQueryResult>
    findPath(const NavigationGrid2D& grid, NavigationCell2D start, NavigationCell2D goal);

    // Pending -> Cancelled is absorbing until the next begin()/reset(). Calling
    // cancel() for an idle or already terminal query returns its current state.
    [[nodiscard]] NavigationPathQueryResult cancel() noexcept;
    void reset() noexcept;

    [[nodiscard]] NavigationPathQueryResult result() const noexcept;
    // Borrowed until the next begin()/reset() or pathfinder destruction.
    [[nodiscard]] std::span<const NavigationCell2D> path() const noexcept { return m_path; }
    [[nodiscard]] Core::usize cellCapacity() const noexcept { return m_cellCapacity; }

private:
    inline static constexpr Core::u32 InvalidIndex = (std::numeric_limits<Core::u32>::max)();
    inline static constexpr Core::u32 InfiniteCost = (std::numeric_limits<Core::u32>::max)();

    struct NodeRecord final {
        Core::u32 epoch = 0;
        Core::u32 parentIndex = InvalidIndex;
        Core::u32 gCost = InfiniteCost;
        Core::u32 heapIndex = InvalidIndex;
        bool closed = false;
    };

    NavigationPathfinder2D(Core::usize cellCapacity, std::pmr::vector<NodeRecord> records,
                           std::pmr::vector<Core::u32> openHeap,
                           std::pmr::vector<NavigationCell2D> path) noexcept;

    void startNewEpoch() noexcept;
    [[nodiscard]] Core::u32 cellIndex(NavigationCell2D cell) const noexcept;
    [[nodiscard]] NavigationCell2D cellForIndex(Core::u32 index) const noexcept;
    [[nodiscard]] Core::u32 heuristic(Core::u32 index) const noexcept;
    [[nodiscard]] bool higherPriority(Core::u32 left, Core::u32 right) const noexcept;
    void heapSwap(Core::u32 leftHeapIndex, Core::u32 rightHeapIndex) noexcept;
    void siftUp(Core::u32 heapIndex) noexcept;
    void siftDown(Core::u32 heapIndex) noexcept;
    void pushOpen(Core::u32 cellIndex) noexcept;
    [[nodiscard]] Core::u32 popOpen() noexcept;
    void updateOpenPriority(Core::u32 cellIndex) noexcept;
    [[nodiscard]] NodeRecord& recordFor(Core::u32 cellIndex) noexcept;
    [[nodiscard]] Core::Status reconstructPath(Core::u32 goalIndex);
    void setTerminal(NavigationPathQueryState state) noexcept;

    Core::usize m_cellCapacity = 0;
    std::pmr::vector<NodeRecord> m_records;
    std::pmr::vector<Core::u32> m_openHeap;
    std::pmr::vector<NavigationCell2D> m_path;
    const NavigationGrid2D* m_grid = nullptr;
    Core::u32 m_widthCells = 0;
    Core::u32 m_heightCells = 0;
    Core::u32 m_startIndex = InvalidIndex;
    Core::u32 m_goalIndex = InvalidIndex;
    Core::u32 m_epoch = 0;
    Core::u64 m_gridRevision = 0;
    Core::usize m_expandedNodes = 0;
    NavigationPathQueryState m_state = NavigationPathQueryState::Idle;
};

} // namespace Tina::Navigation2D
