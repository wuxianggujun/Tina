#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/navigation3d/NavigationVolume3D.hpp>

#include <limits>
#include <memory>
#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::Navigation3D {

enum class NavigationPathQueryState3D : Core::u8 {
    Idle = 0,
    Pending = 1,
    Reached = 2,
    Unreachable = 3,
    Cancelled = 4,
    Invalidated = 5,
};

enum class NavigationDiagonalMode3D : Core::u8 {
    Disabled = 0,
    // Both orthogonal columns must clear the agent's full height, not merely its feet.
    RequireClearAdjacentColumns = 1,
    AllowCornerCutting = 2,
};

struct NavigationPathQueryOptions3D final {
    NavigationDiagonalMode3D diagonalMode = NavigationDiagonalMode3D::Disabled;
    NavigationAgentProfile3D agent{};

    friend constexpr bool operator==(
        const NavigationPathQueryOptions3D&,
        const NavigationPathQueryOptions3D&) noexcept = default;
};

namespace NavigationPathCost3D {

inline constexpr Core::u32 Cardinal = 10;
inline constexpr Core::u32 Diagonal = 14;
// Climbing is charged per cell of rise; descending is cheaper but never free. The
// contract is the ORDER 0 < FallPerCell < StepUpPerCell -- in a world with gravity going
// down costs less than going up, and a free descent makes a planner drop for no reason
// among otherwise equal paths. The exact values are tunable.
inline constexpr Core::u32 StepUpPerCell = 10;
inline constexpr Core::u32 FallPerCell = 4;

} // namespace NavigationPathCost3D

struct NavigationPathQueryResult3D final {
    NavigationPathQueryState3D state = NavigationPathQueryState3D::Idle;
    Core::usize expandedNodes = 0;
    Core::usize pathCellCount = 0;
    // Deterministic integer cost: horizontal base cost plus vertical surcharge, times the
    // destination cell terrain multiplier. Zero unless Reached.
    Core::u32 pathCost = 0;
    Core::u64 volumeRevision = 0;
    // How many of the path's transitions changed height. Reported because a caller that
    // animates jumps or plays footstep audio needs it, and because a test can assert a
    // route really used the staircase instead of walking around.
    Core::usize verticalTransitions = 0;
};

struct NavigationPathfinder3DConfig final {
    // Maximum volume cells accepted by begin()/findPath(). Create performs all persistent
    // allocations for records, open set, and final path storage.
    Core::usize cellCapacity = 0;
};

// Reusable owner-thread A* over a voxel volume. Movement is four-way by default; optional
// horizontal diagonals use 10/14 integer base costs. Vertical transitions are cardinal
// only and priced per cell. Ties are deterministic by f-cost, then heuristic, then cell
// index.
class NavigationPathfinder3D final {
public:
    [[nodiscard]] static Core::Result<NavigationPathfinder3D> Create(
        NavigationPathfinder3DConfig config,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~NavigationPathfinder3D() noexcept = default;

    NavigationPathfinder3D(const NavigationPathfinder3D&) = delete;
    NavigationPathfinder3D& operator=(const NavigationPathfinder3D&) = delete;
    NavigationPathfinder3D(NavigationPathfinder3D&& other) noexcept;
    NavigationPathfinder3D& operator=(NavigationPathfinder3D&&) = delete;

    // begin() publishes a fresh query only after all request validation succeeds. A start
    // or goal no agent of this profile can occupy is NotStandable rather than a
    // deterministic Unreachable: unlike a blocked 2D cell, "cannot stand here" usually
    // means the caller sampled a position in mid-air, and silently answering
    // "no route" would hide that.
    [[nodiscard]] Core::Result<NavigationPathQueryResult3D>
    begin(const NavigationVolume3D& volume, NavigationCell3D start, NavigationCell3D goal,
          NavigationPathQueryOptions3D options = {});

    // Expands at most expansionBudget nodes. The same volume object must be supplied for
    // every step; address or revision changes terminate as Invalidated.
    [[nodiscard]] Core::Result<NavigationPathQueryResult3D>
    advance(const NavigationVolume3D& volume, Core::usize expansionBudget);

    [[nodiscard]] Core::Result<NavigationPathQueryResult3D>
    findPath(const NavigationVolume3D& volume, NavigationCell3D start, NavigationCell3D goal,
             NavigationPathQueryOptions3D options = {});

    // Pending -> Cancelled is absorbing until the next begin()/reset(). Calling cancel()
    // for an idle or already terminal query returns its current state.
    [[nodiscard]] NavigationPathQueryResult3D cancel() noexcept;
    void reset() noexcept;

    [[nodiscard]] NavigationPathQueryResult3D result() const noexcept;
    // Borrowed until the next begin()/reset() or pathfinder destruction.
    [[nodiscard]] std::span<const NavigationCell3D> path() const noexcept;
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

    struct Storage;
    static void destroyStorage(Storage* storage) noexcept;
    using StorageOwner = std::unique_ptr<Storage, decltype(&destroyStorage)>;
    NavigationPathfinder3D(Core::usize cellCapacity, StorageOwner storage) noexcept;

    void startNewEpoch() noexcept;
    [[nodiscard]] Core::u32 cellIndex(NavigationCell3D cell) const noexcept;
    [[nodiscard]] NavigationCell3D cellForIndex(Core::u32 index) const noexcept;
    [[nodiscard]] Core::u32 heuristic(Core::u32 index) const noexcept;
    [[nodiscard]] bool higherPriority(Core::u32 left, Core::u32 right) const noexcept;
    void pushOpen(Core::u32 cellIndex) noexcept;
    [[nodiscard]] Core::u32 popOpen() noexcept;
    void updateOpenPriority(Core::u32 cellIndex) noexcept;
    [[nodiscard]] NodeRecord& recordFor(Core::u32 cellIndex) noexcept;
    [[nodiscard]] Core::Status reconstructPath(Core::u32 goalIndex);
    void setTerminal(NavigationPathQueryState3D state) noexcept;

    Core::usize m_cellCapacity = 0;
    StorageOwner m_storage{nullptr, &destroyStorage};
    const NavigationVolume3D* m_volume = nullptr;
    Core::u32 m_widthCells = 0;
    Core::u32 m_heightCells = 0;
    Core::u32 m_depthCells = 0;
    Core::u32 m_startIndex = InvalidIndex;
    Core::u32 m_goalIndex = InvalidIndex;
    Core::u32 m_epoch = 0;
    Core::u64 m_volumeRevision = 0;
    Core::usize m_expandedNodes = 0;
    Core::usize m_verticalTransitions = 0;
    NavigationPathQueryOptions3D m_options{};
    Core::u32 m_pathCost = 0;
    NavigationPathQueryState3D m_state = NavigationPathQueryState3D::Idle;
};

} // namespace Tina::Navigation3D
