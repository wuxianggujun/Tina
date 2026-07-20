#pragma once

#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <memory_resource>
#include <optional>
#include <span>
#include <vector>

namespace Tina::Asset {

// Runtime mutable TileMap instance (gameplay feature; not Scene).
// First slice: single layer, fixed grid, power-of-two chunk size, solid AABB query.
// Built by copying tile ids from a parsed TileMap payload and material/UV tables from Tileset.

struct TileMapInstanceConfig final {
    // Chunk size in cells along each axis. Must be power-of-two in [1, 64]. Default 16.
    Core::u16 chunkSizeCells = 16;
    std::pmr::memory_resource* memoryResource = nullptr;
};

struct TileMapSolidQuery final {
    // Axis-aligned box in world meters (same origin as tile (0,0) bottom-left / map local).
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
};

struct TileMapSolidHit final {
    Core::u32 cellX = 0;
    Core::u32 cellY = 0;
    Core::u16 localTileId = 0;
    Core::u16 materialFlags = 0;
};

struct TileMapChunkCoord final {
    Core::u32 chunkX = 0;
    Core::u32 chunkY = 0;

    [[nodiscard]] friend constexpr bool operator==(const TileMapChunkCoord&, const TileMapChunkCoord&) = default;
};

struct TileMapTileInfo final {
    Core::u16 localTileId = 0;
    Core::u16 materialFlags = 0;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    bool empty = true;
};

class TileMapInstance final {
  public:
    TileMapInstance() noexcept = default;
    ~TileMapInstance() noexcept = default;

    TileMapInstance(const TileMapInstance&) = delete;
    TileMapInstance& operator=(const TileMapInstance&) = delete;
    TileMapInstance(TileMapInstance&&) noexcept = default;
    TileMapInstance& operator=(TileMapInstance&&) noexcept = default;

    // Copies cells from tileMap and material/UV table from tileset (by localId index).
    // tileset.localId values should be dense or sparse-safe via max localId table.
    [[nodiscard]] static Core::Result<TileMapInstance>
    Create(const AssetFormat::TileMapPayloadView& tileMap, const AssetFormat::TilesetPayloadView& tileset,
           Core::AssetId tileMapAssetId, Core::AssetId tilesetAssetId, TileMapInstanceConfig config = {});

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_width != 0 && m_height != 0;
    }

    [[nodiscard]] Core::u32 widthCells() const noexcept
    {
        return m_width;
    }
    [[nodiscard]] Core::u32 heightCells() const noexcept
    {
        return m_height;
    }
    [[nodiscard]] float cellSizeMeters() const noexcept
    {
        return m_cellSizeMeters;
    }
    [[nodiscard]] Core::u16 chunkSizeCells() const noexcept
    {
        return m_chunkSize;
    }
    [[nodiscard]] Core::u32 chunkCountX() const noexcept
    {
        return m_chunkCountX;
    }
    [[nodiscard]] Core::u32 chunkCountY() const noexcept
    {
        return m_chunkCountY;
    }
    [[nodiscard]] Core::AssetId tileMapAssetId() const noexcept
    {
        return m_tileMapAssetId;
    }
    [[nodiscard]] Core::AssetId tilesetAssetId() const noexcept
    {
        return m_tilesetAssetId;
    }

    [[nodiscard]] Core::u16 tileIdAt(Core::u32 x, Core::u32 y) const noexcept;
    [[nodiscard]] std::optional<TileMapTileInfo> tileInfoAt(Core::u32 x, Core::u32 y) const noexcept;

    // Sets cell; bumps affected chunk revision. localTileId 0 = empty.
    // Unknown non-zero local ids fail (must exist in tileset table used at Create).
    [[nodiscard]] Core::Status setTile(Core::u32 x, Core::u32 y, Core::u16 localTileId) noexcept;

    [[nodiscard]] Core::u32 chunkRevision(Core::u32 chunkX, Core::u32 chunkY) const noexcept;
    [[nodiscard]] TileMapChunkCoord chunkCoordForCell(Core::u32 x, Core::u32 y) const noexcept;

    // Collects solid (MaterialSolid) cells overlapping the AABB. Clears `out` first.
    // Capacity limited by out.capacity() growth via PMR vector; returns count written.
    [[nodiscard]] Core::Result<Core::u32> querySolidAabb(const TileMapSolidQuery& query,
                                                         std::pmr::vector<TileMapSolidHit>& out) const;

  private:
    struct TileDef final {
        Core::u16 materialFlags = 0;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 1.0f;
        float v1 = 1.0f;
        bool valid = false;
    };

    TileMapInstance(Core::u32 width, Core::u32 height, float cellSize, Core::u16 chunkSize, Core::u32 chunkCountX,
                    Core::u32 chunkCountY, Core::AssetId tileMapAssetId, Core::AssetId tilesetAssetId,
                    std::pmr::vector<Core::u16> cells, std::pmr::vector<Core::u32> chunkRevisions,
                    std::pmr::vector<TileDef> tileDefs) noexcept;

    [[nodiscard]] bool inBounds(Core::u32 x, Core::u32 y) const noexcept;
    [[nodiscard]] Core::u32 cellIndex(Core::u32 x, Core::u32 y) const noexcept;
    [[nodiscard]] Core::u32 chunkIndex(Core::u32 chunkX, Core::u32 chunkY) const noexcept;
    void bumpChunkForCell(Core::u32 x, Core::u32 y) noexcept;

    Core::u32 m_width = 0;
    Core::u32 m_height = 0;
    float m_cellSizeMeters = 1.0f;
    Core::u16 m_chunkSize = 16;
    Core::u32 m_chunkCountX = 0;
    Core::u32 m_chunkCountY = 0;
    Core::AssetId m_tileMapAssetId{};
    Core::AssetId m_tilesetAssetId{};
    std::pmr::vector<Core::u16> m_cells{};
    std::pmr::vector<Core::u32> m_chunkRevisions{};
    // Indexed by localTileId; entry.valid when present in tileset.
    std::pmr::vector<TileDef> m_tileDefs{};
};

} // namespace Tina::Asset
