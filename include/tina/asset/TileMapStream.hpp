#pragma once

#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/TileChunkView.hpp>
#include <tina/asset/TileMapInstance.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::Asset {

enum class TileMapChunkResidencyState : Core::u8 {
    Requested = 1,
    Resident = 2,
    Failed = 3,
};

struct TileMapChunkDemand final {
    AssetFormat::TileMapLayerId layerId = 0;
    Core::u32 priority = 0;
    TileChunkCameraQuery camera{};
};

struct TileMapStreamConfig final {
    Core::usize residentCapacity = 64;
    Core::u32 requestBudgetPerUpdate = 4;
    Core::u16 loadMarginChunks = 0;
    Core::u16 retainMarginChunks = 1;
    std::pmr::memory_resource* memoryResource = nullptr;
};

struct TileMapStreamStats final {
    Core::usize activeSlots = 0;
    Core::usize requestedSlots = 0;
    Core::usize residentSlots = 0;
    Core::usize failedSlots = 0;
    Core::usize peakResidentSlots = 0;
    Core::u64 totalRequests = 0;
    Core::u64 totalCommitted = 0;
    Core::u64 totalCancelled = 0;
    Core::u64 totalUnloaded = 0;
    Core::u64 totalFailed = 0;
};

// Fixed-capacity owner for lazy TileMapChunk AssetHandle/AssetLease residency.
// Call order per frame: updateDemand -> AssetSystem::pump -> commitReady -> extraction.
class TileMapStream final {
  public:
    TileMapStream() noexcept = default;
    ~TileMapStream() noexcept;

    TileMapStream(const TileMapStream&) = delete;
    TileMapStream& operator=(const TileMapStream&) = delete;
    TileMapStream(TileMapStream&& other) noexcept;
    TileMapStream& operator=(TileMapStream&&) = delete;

    [[nodiscard]] static Core::Result<TileMapStream>
    Create(AssetSystem& assets, AssetLease rootLease, AssetLease tilesetLease,
           TileMapStreamConfig config = {});

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_assets != nullptr && static_cast<bool>(m_map);
    }

    // Capacity failure is transactional: the previous active set remains unchanged.
    [[nodiscard]] Core::Status updateDemand(std::span<const TileMapChunkDemand> demands);
    [[nodiscard]] Core::Result<TileMapStreamStats> commitReady();
    [[nodiscard]] Core::Status shutdown() noexcept;

    [[nodiscard]] TileMapInstance& map() noexcept
    {
        return m_map;
    }
    [[nodiscard]] const TileMapInstance& map() const noexcept
    {
        return m_map;
    }
    [[nodiscard]] TileMapStreamStats stats() const noexcept;

  private:
    struct ChunkKey final {
        AssetFormat::TileMapLayerId layerId = 0;
        TileMapChunkCoord coord{};
        Core::AssetId assetId{};

        [[nodiscard]] friend constexpr bool operator==(const ChunkKey&, const ChunkKey&) = default;
    };

    struct DesiredChunk final {
        ChunkKey key{};
        Core::u32 priority = 0;
    };

    struct Slot final {
        ChunkKey key{};
        AssetHandle handle{};
        TileMapChunkResidencyState state = TileMapChunkResidencyState::Requested;
        AssetLease lease{};
        Core::u64 lastDesiredDemand = 0;
    };

    struct RetainCandidate final {
        ChunkKey key{};
        Core::u64 lastDesiredDemand = 0;
    };

    TileMapStream(AssetSystem& assets, AssetLease rootLease, AssetLease tilesetLease,
                  TileMapInstance map, TileMapStreamConfig config,
                  std::pmr::vector<Slot> slots, std::pmr::vector<DesiredChunk> desired,
                  std::pmr::vector<RetainCandidate> retain) noexcept;

    [[nodiscard]] Core::Status buildDemandSet(std::span<const TileMapChunkDemand> demands,
                                              Core::u16 margin, std::pmr::vector<DesiredChunk>& out) const;
    [[nodiscard]] bool retainedByDemand(std::span<const TileMapChunkDemand> demands,
                                        const ChunkKey& key, Core::u16 margin) const noexcept;
    [[nodiscard]] Slot* findSlot(const ChunkKey& key) noexcept;
    [[nodiscard]] const Slot* findSlot(const ChunkKey& key) const noexcept;
    [[nodiscard]] static bool contains(std::span<const ChunkKey> keys, const ChunkKey& key) noexcept;
    [[nodiscard]] static bool contains(std::span<const DesiredChunk> chunks,
                                       const ChunkKey& key) noexcept;
    [[nodiscard]] static bool contains(std::span<const RetainCandidate> candidates,
                                       const ChunkKey& key) noexcept;
    [[nodiscard]] static bool keyLess(const ChunkKey& left, const ChunkKey& right) noexcept;
    [[nodiscard]] static bool desiredLess(const DesiredChunk& left, const DesiredChunk& right) noexcept;
    [[nodiscard]] Core::u64 nextResidencyGeneration() noexcept;
    [[nodiscard]] Core::u64 nextDemandGeneration() noexcept;
    void removeSlot(Core::usize index) noexcept;

    AssetSystem* m_assets = nullptr;
    AssetLease m_rootLease{};
    AssetLease m_tilesetLease{};
    TileMapInstance m_map{};
    TileMapStreamConfig m_config{};
    std::pmr::vector<Slot> m_slots{};
    std::pmr::vector<DesiredChunk> m_desired{};
    std::pmr::vector<RetainCandidate> m_retain{};
    Core::u64 m_nextResidencyGeneration = 1;
    Core::u64 m_nextDemandGeneration = 1;
    Core::usize m_peakResidentSlots = 0;
    Core::u64 m_totalRequests = 0;
    Core::u64 m_totalCommitted = 0;
    Core::u64 m_totalCancelled = 0;
    Core::u64 m_totalUnloaded = 0;
    Core::u64 m_totalFailed = 0;
};

} // namespace Tina::Asset
