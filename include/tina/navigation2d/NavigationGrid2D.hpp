#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/GenerationId.hpp>
#include <tina/core/id/GenerationPool.hpp>

#include <compare>
#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::Navigation2D {

namespace NavigationGrid2DSchema {

inline constexpr Core::u16 Version = 1;
inline constexpr Core::u8 CellBlocked = 1U << 0U;
inline constexpr Core::u8 ValidCellFlags = CellBlocked;
inline constexpr Core::u32 MaximumDimension = 4096;
inline constexpr Core::usize MaximumCellCount = Core::usize{16} * 1024U * 1024U;
inline constexpr Core::usize MaximumDynamicBlockers = 65535;

} // namespace NavigationGrid2DSchema

struct NavigationCell2D final {
    Core::u32 x = 0;
    Core::u32 y = 0;

    auto operator<=>(const NavigationCell2D&) const = default;
};

struct NavigationCellRect2D final {
    Core::u32 x = 0;
    Core::u32 y = 0;
    Core::u32 width = 0;
    Core::u32 height = 0;

    auto operator<=>(const NavigationCellRect2D&) const = default;
};

struct NavigationGrid2DDataDesc final {
    Core::u16 schemaVersion = NavigationGrid2DSchema::Version;
    Core::u32 widthCells = 0;
    Core::u32 heightCells = 0;
    float cellSizeMeters = 1.0F;
    // Row-major cell flags. Only NavigationGrid2DSchema::CellBlocked is valid in v1.
    std::span<const Core::u8> cellFlags{};
};

// Immutable owning schema-v1 navigation data. Old schemas are rejected rather
// than read through compatibility branches while the project remains pre-1.0.
class NavigationGrid2DData final {
public:
    [[nodiscard]] static Core::Result<NavigationGrid2DData> Create(
        const NavigationGrid2DDataDesc& desc,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~NavigationGrid2DData() noexcept = default;

    NavigationGrid2DData(const NavigationGrid2DData&) = delete;
    NavigationGrid2DData& operator=(const NavigationGrid2DData&) = delete;
    NavigationGrid2DData(NavigationGrid2DData&& other) noexcept;
    NavigationGrid2DData& operator=(NavigationGrid2DData&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] Core::u16 schemaVersion() const noexcept { return m_schemaVersion; }
    [[nodiscard]] Core::u32 widthCells() const noexcept { return m_widthCells; }
    [[nodiscard]] Core::u32 heightCells() const noexcept { return m_heightCells; }
    [[nodiscard]] Core::usize cellCount() const noexcept { return m_cellFlags.size(); }
    [[nodiscard]] float cellSizeMeters() const noexcept { return m_cellSizeMeters; }
    [[nodiscard]] std::span<const Core::u8> cellFlags() const noexcept { return m_cellFlags; }
    [[nodiscard]] bool inBounds(NavigationCell2D cell) const noexcept;
    [[nodiscard]] bool blockedAt(NavigationCell2D cell) const noexcept;

private:
    NavigationGrid2DData(Core::u16 schemaVersion, Core::u32 widthCells, Core::u32 heightCells,
                         float cellSizeMeters, std::pmr::vector<Core::u8> cellFlags) noexcept;

    Core::u16 m_schemaVersion = 0;
    Core::u32 m_widthCells = 0;
    Core::u32 m_heightCells = 0;
    float m_cellSizeMeters = 0.0F;
    std::pmr::vector<Core::u8> m_cellFlags;
};

namespace Detail {
struct NavigationBlockerRegistryTag final {
};
} // namespace Detail

using NavigationBlockerId = Core::GenerationId<Detail::NavigationBlockerRegistryTag>;

struct NavigationGrid2DConfig final {
    Core::usize dynamicBlockerCapacity = 64;
};

// Owner-thread mutable navigation grid. Base schema data remains immutable;
// fixed-capacity generation blockers are overlaid through per-cell reference counts.
class NavigationGrid2D final {
public:
    [[nodiscard]] static Core::Result<NavigationGrid2D> Create(
        NavigationGrid2DData data, NavigationGrid2DConfig config = {},
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~NavigationGrid2D() noexcept = default;

    NavigationGrid2D(const NavigationGrid2D&) = delete;
    NavigationGrid2D& operator=(const NavigationGrid2D&) = delete;
    NavigationGrid2D(NavigationGrid2D&& other) noexcept;
    NavigationGrid2D& operator=(NavigationGrid2D&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(m_data); }
    [[nodiscard]] const NavigationGrid2DData& data() const noexcept { return m_data; }
    [[nodiscard]] Core::u32 widthCells() const noexcept { return m_data.widthCells(); }
    [[nodiscard]] Core::u32 heightCells() const noexcept { return m_data.heightCells(); }
    [[nodiscard]] Core::usize cellCount() const noexcept { return m_data.cellCount(); }
    [[nodiscard]] float cellSizeMeters() const noexcept { return m_data.cellSizeMeters(); }
    [[nodiscard]] bool inBounds(NavigationCell2D cell) const noexcept { return m_data.inBounds(cell); }
    [[nodiscard]] bool isBaseBlocked(NavigationCell2D cell) const noexcept { return m_data.blockedAt(cell); }
    [[nodiscard]] bool isBlocked(NavigationCell2D cell) const noexcept;
    [[nodiscard]] Core::u16 dynamicBlockerCountAt(NavigationCell2D cell) const noexcept;
    [[nodiscard]] Core::u64 revision() const noexcept { return m_revision; }
    [[nodiscard]] Core::usize dynamicBlockerCapacity() const noexcept { return m_blockers.capacity(); }
    [[nodiscard]] Core::usize dynamicBlockerCount() const noexcept { return m_blockers.activeCount(); }

    [[nodiscard]] Core::Result<NavigationBlockerId> addBlocker(NavigationCellRect2D rect);
    [[nodiscard]] Core::Status updateBlocker(NavigationBlockerId blocker, NavigationCellRect2D rect);
    [[nodiscard]] Core::Status removeBlocker(NavigationBlockerId blocker);
    [[nodiscard]] bool containsBlocker(NavigationBlockerId blocker) const noexcept
    {
        return m_blockers.contains(blocker);
    }

private:
    struct DynamicBlocker final {
        NavigationCellRect2D rect{};
    };

    using BlockerPool = Core::GenerationPool<DynamicBlocker, Detail::NavigationBlockerRegistryTag>;

    NavigationGrid2D(NavigationGrid2DData data, BlockerPool blockers,
                     std::pmr::vector<Core::u16> blockerCounts) noexcept;

    [[nodiscard]] Core::Status validateRect(NavigationCellRect2D rect) const;
    void addRectCounts(NavigationCellRect2D rect) noexcept;
    void removeRectCounts(NavigationCellRect2D rect) noexcept;
    void advanceRevision() noexcept;
    [[nodiscard]] Core::usize cellIndex(NavigationCell2D cell) const noexcept;

    NavigationGrid2DData m_data;
    BlockerPool m_blockers;
    std::pmr::vector<Core::u16> m_blockerCounts;
    Core::u64 m_revision = 1;
};

} // namespace Tina::Navigation2D
