#pragma once

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/TileMapInstance.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/physics2d/PhysicsGridBodies.hpp>
#include <tina/physics2d/PhysicsWorld2D.hpp>

#include <memory_resource>
#include <vector>

namespace Tina::Asset {

struct TileMapPhysicsSync2DConfig final {
    AssetFormat::TileMapLayerId layerId = 0;
    Core::usize chunkCapacity = 64;
    // Zero derives the exact chunk-cell count from the bound TileMap. A smaller
    // value is useful only when the product deliberately limits rectangle count.
    Core::usize rectangleCapacityPerChunk = 0;
    Physics2D::PhysicsGridColliderMaterial2D material{};
    std::pmr::memory_resource* memoryResource = nullptr;
};

struct TileMapPhysicsSync2DStats final {
    Core::usize residentChunkCount = 0;
    Core::usize colliderBodyCount = 0;
    Core::usize colliderShapeCount = 0;
    Core::usize colliderSolidCellCount = 0;
    Core::usize lastAddedChunkCount = 0;
    Core::usize lastRebuiltChunkCount = 0;
    Core::usize lastRemovedChunkCount = 0;
    Core::usize lastUnchangedChunkCount = 0;
    Core::usize lastBakedRectangleCount = 0;
    Core::usize lastBakedSolidCellCount = 0;
    Core::u64 synchronizeCount = 0;
    Core::u64 totalAddedChunkCount = 0;
    Core::u64 totalRebuiltChunkCount = 0;
    Core::u64 totalRemovedChunkCount = 0;
    Core::u64 totalBakedRectangleCount = 0;
    Core::u64 totalBakedSolidCellCount = 0;
};

// Owner-thread bridge between one TileMap layer and one PhysicsWorld2D. The
// bridge stores only generation-aware runtime body handles; TileMap remains the
// source of truth for resident cells. Callers must invoke shutdown(world) before
// destroying the world. No UI, backend, or AssetLease ownership crosses this API.
class TileMapPhysicsSync2D final {
  public:
    TileMapPhysicsSync2D() noexcept = default;
    ~TileMapPhysicsSync2D() noexcept = default;

    TileMapPhysicsSync2D(const TileMapPhysicsSync2D&) = delete;
    TileMapPhysicsSync2D& operator=(const TileMapPhysicsSync2D&) = delete;
    TileMapPhysicsSync2D(TileMapPhysicsSync2D&&) noexcept = default;
    TileMapPhysicsSync2D& operator=(TileMapPhysicsSync2D&&) noexcept = delete;

    [[nodiscard]] static Core::Result<TileMapPhysicsSync2D>
    Create(const TileMapInstance& map, TileMapPhysicsSync2DConfig config = {});

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_layerId != 0 && m_chunkCapacity != 0 && !m_records.empty();
    }

    // Synchronizes only resident chunks. A changed or newly resident chunk is
    // staged as a complete one-body/many-shape collider before any old collider
    // is retired. The prior synchronized state remains published on bake/create
    // failure.
    [[nodiscard]] Core::Result<TileMapPhysicsSync2DStats>
    synchronize(const TileMapInstance& map, Physics2D::PhysicsWorld2D& world);

    // Retires all currently tracked bodies. On failure, the remaining records stay
    // live so the caller can retry after restoring the world boundary.
    [[nodiscard]] Core::Status shutdown(Physics2D::PhysicsWorld2D& world) noexcept;

    [[nodiscard]] TileMapPhysicsSync2DStats stats() const noexcept
    {
        return m_stats;
    }

  private:
    struct ChunkRecord final {
        TileMapChunkCoord coord{};
        Core::u64 residencyGeneration = 0;
        Core::u32 contentRevision = 0;
        Physics2D::PhysicsBodyId body{};
        Core::usize shapeCount = 0;
        Core::usize solidCellCount = 0;
        bool occupied = false;
    };

    TileMapPhysicsSync2D(
        AssetFormat::TileMapLayerId layerId,
        Core::AssetId tileMapAssetId,
        Core::AssetId tilesetAssetId,
        Core::u32 widthCells,
        Core::u32 heightCells,
        Core::u16 chunkSizeCells,
        float cellSizeMeters,
        Core::usize chunkCapacity,
        std::pmr::vector<ChunkRecord> records,
        std::pmr::vector<ChunkRecord> nextRecords,
        std::pmr::vector<Physics2D::PhysicsGridSolidRect2D> rectangles,
        std::pmr::vector<Core::u8> occupancy,
        std::pmr::vector<Physics2D::PhysicsBodyId> stagedBodies,
        std::pmr::vector<Physics2D::PhysicsBodyId> retiredBodies,
        std::pmr::vector<Core::u8> seenRecords,
        TileMapPhysicsSync2DConfig config) noexcept;

    [[nodiscard]] Core::Status validateMap(const TileMapInstance& map) const;
    [[nodiscard]] Core::Result<Core::usize> bakeChunk(
        const TileMapInstance& map,
        TileMapChunkCoord coord);
    [[nodiscard]] Core::usize findRecord(TileMapChunkCoord coord) const noexcept;
    [[nodiscard]] Core::Status clearStagedBodies(Physics2D::PhysicsWorld2D& world) noexcept;
    void clearScratch() noexcept;

    AssetFormat::TileMapLayerId m_layerId = 0;
    Core::AssetId m_tileMapAssetId{};
    Core::AssetId m_tilesetAssetId{};
    Core::u32 m_widthCells = 0;
    Core::u32 m_heightCells = 0;
    Core::u16 m_chunkSizeCells = 0;
    float m_cellSizeMeters = 0.0F;
    Core::usize m_chunkCapacity = 0;
    TileMapPhysicsSync2DConfig m_config{};
    std::pmr::vector<ChunkRecord> m_records{};
    std::pmr::vector<ChunkRecord> m_nextRecords{};
    std::pmr::vector<Physics2D::PhysicsGridSolidRect2D> m_rectangles{};
    std::pmr::vector<Core::u8> m_occupancy{};
    std::pmr::vector<Physics2D::PhysicsBodyId> m_stagedBodies{};
    std::pmr::vector<Physics2D::PhysicsBodyId> m_retiredBodies{};
    std::pmr::vector<Core::u8> m_seenRecords{};
    Core::usize m_activeRecordCount = 0;
    TileMapPhysicsSync2DStats m_stats{};
};

} // namespace Tina::Asset
