#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/GenerationId.hpp>
#include <tina/core/id/GenerationPool.hpp>
#include <tina/math/Vec.hpp>

#include <compare>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <vector>

namespace Tina::Navigation3D {

namespace NavigationVolume3DContract {

inline constexpr Core::u8 CellSolid = 1U << 0U;
inline constexpr Core::u8 ValidCellFlags = CellSolid;
inline constexpr Core::u8 MinimumTraversalCost = 1;
inline constexpr Core::u8 MaximumTraversalCost = 16;
inline constexpr Core::u32 MaximumDimension = 1024;
// Two orders of magnitude below the 2D ceiling: a volume of this size already costs
// 16 MiB of flags plus 16 MiB of costs, and a pathfinder over it needs one record per
// cell. A caller who wants more should stream sub-volumes.
inline constexpr Core::usize MaximumCellCount = Core::usize{16} * 1024U * 1024U;
inline constexpr Core::usize MaximumDynamicBlockers = 65535;

// Agent height is expressed in cells because standability is a voxel predicate. A
// 1-cell agent is legal (a crawling or flying-low creature); 0 is not, because an
// agent occupying no cell cannot be blocked by anything.
inline constexpr Core::u32 MinimumAgentHeightCells = 1;
inline constexpr Core::u32 MaximumAgentHeightCells = 64;
inline constexpr Core::u32 MaximumAgentStepUpCells = 8;
inline constexpr Core::u32 MaximumAgentFallCells = 64;

} // namespace NavigationVolume3DContract

// Y is up, matching Math's right-handed convention and the voxel worlds this module
// serves. Component order is deliberately x/y/z rather than x/z/y so that reading a
// cell aloud matches reading a world position aloud.
struct NavigationCell3D final {
    Core::u32 x = 0;
    Core::u32 y = 0;
    Core::u32 z = 0;

    auto operator<=>(const NavigationCell3D&) const = default;
};

struct NavigationCellBox3D final {
    Core::u32 x = 0;
    Core::u32 y = 0;
    Core::u32 z = 0;
    Core::u32 width = 0;
    Core::u32 height = 0;
    Core::u32 depth = 0;

    auto operator<=>(const NavigationCellBox3D&) const = default;
};

// What an agent's body needs in order to stand somewhere and to move between two
// places. Deliberately not baked into the volume: one world serves several body
// sizes, and a volume that encoded a single profile would need rebuilding per
// creature type.
struct NavigationAgentProfile3D final {
    // Cells of head clearance including the cell the feet occupy. A 1.8 m creature in
    // a 1 m voxel world takes 2.
    Core::u32 heightCells = 2;
    // Cells this agent can climb in one horizontal step. 0 forbids stepping up, which
    // is a legal profile for something that cannot leave a flat floor.
    Core::u32 maxStepUpCells = 1;
    // Cells this agent can drop in one horizontal step. Falling further than this is
    // not a slower route, it is no route: the agent would take damage or land
    // somewhere it cannot leave.
    Core::u32 maxFallCells = 3;

    friend constexpr bool operator==(
        const NavigationAgentProfile3D&,
        const NavigationAgentProfile3D&) noexcept = default;
};

struct NavigationVolume3DDataDesc final {
    Core::u32 widthCells = 0;
    Core::u32 heightCells = 0;
    Core::u32 depthCells = 0;
    float originXMeters = 0.0F;
    float originYMeters = 0.0F;
    float originZMeters = 0.0F;
    float cellSizeMeters = 1.0F;
    // Indexed by x + z * width + y * width * depth. That layout puts a column's cells
    // one width*depth stride apart, and the standability predicate reads a column.
    std::span<const Core::u8> cellFlags{};
    // Same indexing. Multipliers in [1, 16], including solid cells.
    std::span<const Core::u8> traversalCosts{};
};

// Immutable owning occupancy volume using the only supported layout.
class NavigationVolume3DData final {
public:
    [[nodiscard]] static Core::Result<NavigationVolume3DData> Create(
        const NavigationVolume3DDataDesc& desc,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~NavigationVolume3DData() noexcept = default;

    NavigationVolume3DData(const NavigationVolume3DData&) = delete;
    NavigationVolume3DData& operator=(const NavigationVolume3DData&) = delete;
    NavigationVolume3DData(NavigationVolume3DData&& other) noexcept;
    NavigationVolume3DData& operator=(NavigationVolume3DData&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] Core::u32 widthCells() const noexcept { return m_widthCells; }
    [[nodiscard]] Core::u32 heightCells() const noexcept { return m_heightCells; }
    [[nodiscard]] Core::u32 depthCells() const noexcept { return m_depthCells; }
    [[nodiscard]] Core::usize cellCount() const noexcept { return m_cellCount; }
    [[nodiscard]] float originXMeters() const noexcept { return m_originXMeters; }
    [[nodiscard]] float originYMeters() const noexcept { return m_originYMeters; }
    [[nodiscard]] float originZMeters() const noexcept { return m_originZMeters; }
    [[nodiscard]] float cellSizeMeters() const noexcept { return m_cellSizeMeters; }
    [[nodiscard]] std::span<const Core::u8> cellFlags() const noexcept { return flagsSpan(); }
    [[nodiscard]] std::span<const Core::u8> traversalCosts() const noexcept { return costsSpan(); }
    [[nodiscard]] Core::u8 minimumTraversalCost() const noexcept { return m_minimumTraversalCost; }
    [[nodiscard]] bool inBounds(NavigationCell3D cell) const noexcept;
    [[nodiscard]] bool solidAt(NavigationCell3D cell) const noexcept;
    // Returns zero for an out-of-bounds cell.
    [[nodiscard]] Core::u8 traversalCostAt(NavigationCell3D cell) const noexcept;
    // Half-open world box: lower faces included, upper faces excluded. Non-finite or
    // outside positions return nullopt.
    [[nodiscard]] std::optional<NavigationCell3D> worldToCell(Math::Vec3 positionMeters) const noexcept;
    // Returns nullopt if the cell is outside the volume or its center cannot be
    // represented by a finite Vec3 that maps back to that same cell.
    [[nodiscard]] std::optional<Math::Vec3> cellCenter(NavigationCell3D cell) const noexcept;

private:
    // The vectors live in a PMR-allocated owner rather than directly in this object.
    // In MSVC Debug the allocator-only and move constructors of pmr::vector are noexcept
    // yet still allocate an iterator proxy, so a bad_alloc raised there aborts instead of
    // unwinding -- a factory that must report AllocationFailed cannot use them. Moving the
    // owner only transfers a pointer, so the move stays genuinely noexcept.
    struct Storage;
    static void destroyStorage(Storage* storage) noexcept;
    using StorageOwner = std::unique_ptr<Storage, decltype(&destroyStorage)>;

    NavigationVolume3DData(Core::u32 widthCells, Core::u32 heightCells, Core::u32 depthCells,
                           float originXMeters, float originYMeters, float originZMeters,
                           float cellSizeMeters, StorageOwner storage,
                           Core::u8 minimumTraversalCost) noexcept;

    [[nodiscard]] std::span<const Core::u8> flagsSpan() const noexcept;
    [[nodiscard]] std::span<const Core::u8> costsSpan() const noexcept;

    Core::u32 m_widthCells = 0;
    Core::u32 m_heightCells = 0;
    Core::u32 m_depthCells = 0;
    float m_originXMeters = 0.0F;
    float m_originYMeters = 0.0F;
    float m_originZMeters = 0.0F;
    float m_cellSizeMeters = 0.0F;
    StorageOwner m_storage{nullptr, &destroyStorage};
    Core::usize m_cellCount = 0;
    Core::u8 m_minimumTraversalCost = 0;
};

namespace Detail {
struct NavigationBlocker3DRegistryTag final {
};
} // namespace Detail

using NavigationBlocker3DId = Core::GenerationId<Detail::NavigationBlocker3DRegistryTag>;

struct NavigationVolume3DConfig final {
    Core::usize dynamicBlockerCapacity = 64;
};

// Owner-thread mutable occupancy volume. The base data stays immutable; fixed-capacity
// generation blockers overlay it through per-cell reference counts.
//
// A blocker makes cells SOLID rather than merely impassable. In a voxel world the thing
// that appears and disappears at runtime is a block, and a block both obstructs the cell
// it occupies and supports the cell above it. Splitting those into two concepts would
// hold two truths about one voxel; as a consequence, placing a blocker can *add*
// standable cells on top of it, which is why every mutation advances revision().
class NavigationVolume3D final {
public:
    [[nodiscard]] static Core::Result<NavigationVolume3D> Create(
        NavigationVolume3DData data, NavigationVolume3DConfig config = {},
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~NavigationVolume3D() noexcept = default;

    NavigationVolume3D(const NavigationVolume3D&) = delete;
    NavigationVolume3D& operator=(const NavigationVolume3D&) = delete;
    NavigationVolume3D(NavigationVolume3D&& other) noexcept;
    NavigationVolume3D& operator=(NavigationVolume3D&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(m_data); }
    [[nodiscard]] const NavigationVolume3DData& data() const noexcept { return m_data; }
    [[nodiscard]] Core::u32 widthCells() const noexcept { return m_data.widthCells(); }
    [[nodiscard]] Core::u32 heightCells() const noexcept { return m_data.heightCells(); }
    [[nodiscard]] Core::u32 depthCells() const noexcept { return m_data.depthCells(); }
    [[nodiscard]] Core::usize cellCount() const noexcept { return m_data.cellCount(); }
    [[nodiscard]] float cellSizeMeters() const noexcept { return m_data.cellSizeMeters(); }
    [[nodiscard]] std::optional<NavigationCell3D> worldToCell(Math::Vec3 positionMeters) const noexcept
    {
        return m_data.worldToCell(positionMeters);
    }
    [[nodiscard]] std::optional<Math::Vec3> cellCenter(NavigationCell3D cell) const noexcept
    {
        return m_data.cellCenter(cell);
    }
    [[nodiscard]] bool inBounds(NavigationCell3D cell) const noexcept { return m_data.inBounds(cell); }
    [[nodiscard]] bool isBaseSolid(NavigationCell3D cell) const noexcept { return m_data.solidAt(cell); }
    // Out-of-bounds cells are NOT solid. Above the volume that means open sky, so the
    // top layer stays reachable; below y = 0 it means no support, so an empty volume is
    // entirely unstandable rather than presenting an implicit floor.
    [[nodiscard]] bool isSolid(NavigationCell3D cell) const noexcept;
    [[nodiscard]] Core::u8 traversalCostAt(NavigationCell3D cell) const noexcept
    {
        return m_data.traversalCostAt(cell);
    }
    [[nodiscard]] Core::u8 minimumTraversalCost() const noexcept { return m_data.minimumTraversalCost(); }

    // An agent of this profile can occupy cell: the cell and the heightCells-1 cells
    // above it are clear, and the cell directly below is solid.
    [[nodiscard]] bool isStandable(NavigationCell3D cell,
                                  const NavigationAgentProfile3D& profile) const noexcept;
    // Vertical clearance only, without the support requirement. This is what a step-up
    // or fall destination needs before its own support is checked.
    [[nodiscard]] bool hasClearance(NavigationCell3D cell,
                                    const NavigationAgentProfile3D& profile) const noexcept;

    [[nodiscard]] Core::u16 dynamicBlockerCountAt(NavigationCell3D cell) const noexcept;
    [[nodiscard]] Core::u64 revision() const noexcept { return m_revision; }
    [[nodiscard]] Core::usize dynamicBlockerCapacity() const noexcept { return m_blockers.capacity(); }
    [[nodiscard]] Core::usize dynamicBlockerCount() const noexcept { return m_blockers.activeCount(); }

    [[nodiscard]] Core::Result<NavigationBlocker3DId> addBlocker(NavigationCellBox3D box);
    [[nodiscard]] Core::Status updateBlocker(NavigationBlocker3DId blocker, NavigationCellBox3D box);
    [[nodiscard]] Core::Status removeBlocker(NavigationBlocker3DId blocker);
    [[nodiscard]] std::optional<NavigationCellBox3D> blockerBox(
        NavigationBlocker3DId blocker) const noexcept;
    [[nodiscard]] bool containsBlocker(NavigationBlocker3DId blocker) const noexcept
    {
        return m_blockers.contains(blocker);
    }

private:
    struct DynamicBlocker final {
        NavigationCellBox3D box{};
    };

    using BlockerPool = Core::GenerationPool<DynamicBlocker, Detail::NavigationBlocker3DRegistryTag>;

    // Same MSVC Debug iterator-proxy reason as NavigationVolume3DData: the count vector
    // stays inside a PMR owner so Create() can report AllocationFailed instead of aborting.
    struct CountStorage;
    static void destroyCountStorage(CountStorage* storage) noexcept;
    using CountStorageOwner = std::unique_ptr<CountStorage, decltype(&destroyCountStorage)>;

    NavigationVolume3D(NavigationVolume3DData data, BlockerPool blockers,
                       CountStorageOwner counts) noexcept;

    [[nodiscard]] Core::Status validateBox(NavigationCellBox3D box) const;
    void addBoxCounts(NavigationCellBox3D box) noexcept;
    void removeBoxCounts(NavigationCellBox3D box) noexcept;
    void advanceRevision() noexcept;
    [[nodiscard]] Core::usize cellIndex(NavigationCell3D cell) const noexcept;

    [[nodiscard]] Core::u16& countAt(Core::usize index) noexcept;
    [[nodiscard]] Core::u16 countAt(Core::usize index) const noexcept;

    NavigationVolume3DData m_data;
    BlockerPool m_blockers;
    CountStorageOwner m_counts{nullptr, &destroyCountStorage};
    Core::u64 m_revision = 1;
};

[[nodiscard]] bool isValidNavigationAgentProfile3D(const NavigationAgentProfile3D& profile) noexcept;

} // namespace Tina::Navigation3D
