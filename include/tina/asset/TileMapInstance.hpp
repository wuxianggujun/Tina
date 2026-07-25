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
// Tile data is owned per explicit TileMapLayerId. Object layers and metadata are
// retained in the instance-owned validated payload and exposed through layer().

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
    AssetFormat::TileMapLayerId layerId = 0;
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

    // Copies tile cells, metadata, and object layers from tileMap. Tile values
    // are validated against the tileset's local-id table before the instance is published.
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

    // Returns a borrowed layer view. It remains valid until this instance is moved
    // or destroyed. Tile bytes stay synchronized with setTile(), while names,
    // properties, ordering, visibility, and object data remain immutable.
    [[nodiscard]] Core::Result<AssetFormat::TileMapLayerPayloadView>
    layer(AssetFormat::TileMapLayerId layerId) const;

    [[nodiscard]] Core::Result<Core::u16> tileIdAt(AssetFormat::TileMapLayerId layerId, Core::u32 x,
                                                   Core::u32 y) const;
    [[nodiscard]] Core::Result<std::optional<TileMapTileInfo>>
    tileInfoAt(AssetFormat::TileMapLayerId layerId, Core::u32 x, Core::u32 y) const;

    // Sets a cell in the selected tile layer; bumps that layer's affected chunk revision.
    // localTileId 0 = empty. Unknown non-zero local ids fail.
    [[nodiscard]] Core::Status setTile(AssetFormat::TileMapLayerId layerId, Core::u32 x, Core::u32 y,
                                       Core::u16 localTileId);

    [[nodiscard]] Core::Result<Core::u32> chunkRevision(AssetFormat::TileMapLayerId layerId, Core::u32 chunkX,
                                                         Core::u32 chunkY) const;
    [[nodiscard]] TileMapChunkCoord chunkCoordForCell(Core::u32 x, Core::u32 y) const noexcept;

    // Collects solid (MaterialSolid) cells from the selected tile layer that overlap the AABB.
    // Clears `out` first; returns count written.
    [[nodiscard]] Core::Result<Core::u32> querySolidAabb(AssetFormat::TileMapLayerId layerId,
                                                         const TileMapSolidQuery& query,
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

    struct TileLayerState final {
        AssetFormat::TileMapLayerId layerId = 0;
        Core::usize payloadTileByteOffset = 0;
        std::pmr::vector<Core::u16> cells{};
        std::pmr::vector<Core::u32> chunkRevisions{};
    };

    TileMapInstance(Core::u32 width, Core::u32 height, float cellSize, Core::u16 chunkSize, Core::u32 chunkCountX,
                    Core::u32 chunkCountY, Core::AssetId tileMapAssetId, Core::AssetId tilesetAssetId,
                    std::pmr::vector<std::byte> payloadBytes, std::pmr::vector<TileLayerState> tileLayers,
                    std::pmr::vector<TileDef> tileDefs) noexcept;

    [[nodiscard]] bool inBounds(Core::u32 x, Core::u32 y) const noexcept;
    [[nodiscard]] Core::u32 cellIndex(Core::u32 x, Core::u32 y) const noexcept;
    [[nodiscard]] Core::u32 chunkIndex(Core::u32 chunkX, Core::u32 chunkY) const noexcept;
    [[nodiscard]] Core::Result<TileLayerState*> tileLayer(AssetFormat::TileMapLayerId layerId);
    [[nodiscard]] Core::Result<const TileLayerState*> tileLayer(AssetFormat::TileMapLayerId layerId) const;
    void bumpChunkForCell(TileLayerState& layer, Core::u32 x, Core::u32 y) noexcept;

    Core::u32 m_width = 0;
    Core::u32 m_height = 0;
    float m_cellSizeMeters = 1.0f;
    Core::u16 m_chunkSize = 16;
    Core::u32 m_chunkCountX = 0;
    Core::u32 m_chunkCountY = 0;
    Core::AssetId m_tileMapAssetId{};
    Core::AssetId m_tilesetAssetId{};
    std::pmr::vector<std::byte> m_payloadBytes{};
    std::pmr::vector<TileLayerState> m_tileLayers{};
    // Indexed by localTileId; entry.valid when present in tileset.
    std::pmr::vector<TileDef> m_tileDefs{};
};

} // namespace Tina::Asset
