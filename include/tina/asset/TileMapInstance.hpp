#pragma once

#include <tina/asset_format/TileMapChunkPayload.hpp>
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

// Runtime mutable TileMap stream instance (gameplay feature; not Scene).
// The v3 root metadata is owned, while only explicitly attached chunks hold cells.

struct TileMapInstanceConfig final {
    Core::usize residentChunkCapacity = 64;
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

struct TileMapChunkState final {
    Core::u32 contentRevision = 0;
    Core::u64 residencyGeneration = 0;
    // Maintained incrementally by attachChunk/setTile, so a caller never has to
    // recount cells to learn whether a chunk is worth visiting.
    Core::u32 nonEmptyCount = 0;
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

// Borrowed cells of one resident chunk. Resolved once so a per-cell loop does not
// repeat the layer scan, chunk-ref binary search and resident-chunk scan that
// tileIdAt() must redo on every single call.
//
// Valid until the owning instance detaches this chunk or is destroyed.
struct TileMapChunkCellsView final {
    AssetFormat::TileMapLayerId layerId = 0;
    TileMapChunkCoord coord{};
    Core::u32 originCellX = 0;
    Core::u32 originCellY = 0;
    Core::u16 widthCells = 0;
    Core::u16 heightCells = 0;
    Core::u32 nonEmptyCount = 0;
    // Row-major, widthCells * heightCells entries. 0 is the empty tile.
    std::span<const Core::u16> cells{};

    // Chunk-local coordinates. Out of range reads as empty rather than failing,
    // because the caller already bounds its loop by widthCells/heightCells.
    [[nodiscard]] Core::u16 localTileIdAt(Core::u32 localX, Core::u32 localY) const noexcept
    {
        if (localX >= widthCells || localY >= heightCells)
        {
            return 0U;
        }
        return cells[static_cast<Core::usize>(localY) * widthCells + localX];
    }
};

class TileMapInstance final {
  public:
    TileMapInstance() noexcept = default;
    ~TileMapInstance() noexcept = default;

    TileMapInstance(const TileMapInstance&) = delete;
    TileMapInstance& operator=(const TileMapInstance&) = delete;
    // Move construction transfers the payload buffer, so the cached layer views keep
    // pointing at bytes this instance owns.
    TileMapInstance(TileMapInstance&&) noexcept = default;
    // Move assignment is deleted: it would have to destroy the existing payload
    // while the incoming layer views still borrowed the source buffer, and a pmr
    // vector may reallocate rather than steal when allocators differ. TileMapStream
    // has the same restriction for the same reason, and every caller constructs
    // in place.
    TileMapInstance& operator=(TileMapInstance&&) = delete;

    // Copies only root metadata/object layers and the Tileset lookup table.
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

    // Returns a borrowed v3 root layer view. It remains valid until this instance
    // is moved or destroyed. Root metadata/chunk refs stay immutable; resident
    // mutable cells are observed through tileIdAt()/tileInfoAt().
    //
    // Resolved from a table built once at Create. The payload is immutable for the
    // life of the instance, so re-validating it per lookup was pure cost: this is
    // called once per cell by tileIdAt()/tileInfoAt(), and full payload validation
    // is quadratic in chunk refs and objects.
    [[nodiscard]] Core::Result<AssetFormat::TileMapLayerPayloadView>
    layer(AssetFormat::TileMapLayerId layerId) const;

    [[nodiscard]] Core::Result<Core::u16> tileIdAt(AssetFormat::TileMapLayerId layerId, Core::u32 x,
                                                   Core::u32 y) const;
    [[nodiscard]] Core::Result<std::optional<TileMapTileInfo>>
    tileInfoAt(AssetFormat::TileMapLayerId layerId, Core::u32 x, Core::u32 y) const;

    // Resolves one resident chunk's cells once, for callers that then walk every
    // cell in it. tileIdAt() has to redo the layer scan, the chunk-ref binary search
    // and the resident-chunk scan per call, and all three are invariant across a
    // chunk -- so a 64x64 chunk paid for them 4096 times.
    [[nodiscard]] Core::Result<TileMapChunkCellsView>
    chunkCells(AssetFormat::TileMapLayerId layerId, TileMapChunkCoord coord) const;

    // Material and UVs for a local tile id, or nullopt for empty/unknown. Pairs with
    // chunkCells() so a loop can go from cell id to tile info without another lookup.
    [[nodiscard]] std::optional<TileMapTileInfo> tileInfoForLocalId(Core::u16 localTileId) const noexcept;

    // Sets a cell in the selected tile layer; bumps that layer's affected chunk revision.
    // localTileId 0 = empty. Unknown non-zero local ids fail.
    [[nodiscard]] Core::Status setTile(AssetFormat::TileMapLayerId layerId, Core::u32 x, Core::u32 y,
                                       Core::u16 localTileId);

    // Attaches one validated chunk transactionally. The payload is copied into bounded
    // instance-owned mutable storage; duplicate coordinates or mismatched root refs fail.
    [[nodiscard]] Core::Status attachChunk(Core::AssetId chunkAssetId,
                                           const AssetFormat::TileMapChunkPayloadView& chunk,
                                           Core::u64 residencyGeneration);
    [[nodiscard]] Core::Status detachChunk(AssetFormat::TileMapLayerId layerId, TileMapChunkCoord coord) noexcept;
    [[nodiscard]] bool isChunkResident(AssetFormat::TileMapLayerId layerId, TileMapChunkCoord coord) const noexcept;
    [[nodiscard]] Core::Result<TileMapChunkState> chunkState(AssetFormat::TileMapLayerId layerId,
                                                            Core::u32 chunkX, Core::u32 chunkY) const;

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

    struct ResidentChunk final {
        AssetFormat::TileMapLayerId layerId = 0;
        TileMapChunkCoord coord{};
        Core::u16 widthCells = 0;
        Core::u16 heightCells = 0;
        Core::u32 nonEmptyCount = 0;
        Core::u64 residencyGeneration = 0;
        Core::u32 contentRevision = 1;
        std::pmr::vector<Core::u16> cells{};
    };

    // `layers` must already borrow `payloadBytes`, which this constructor takes
    // ownership of. Create builds both together so the views cannot outlive them.
    TileMapInstance(Core::u32 width, Core::u32 height, float cellSize, Core::u16 chunkSize,
                    Core::AssetId tileMapAssetId, Core::AssetId tilesetAssetId,
                    std::pmr::vector<std::byte> payloadBytes,
                    std::pmr::vector<AssetFormat::TileMapLayerPayloadView> layers,
                    std::pmr::vector<TileDef> tileDefs,
                    std::pmr::vector<ResidentChunk> residentChunks, Core::usize residentCapacity) noexcept;

    [[nodiscard]] bool inBounds(Core::u32 x, Core::u32 y) const noexcept;
    [[nodiscard]] Core::Result<AssetFormat::TileMapLayerPayloadView>
    tileLayerMetadata(AssetFormat::TileMapLayerId layerId) const;
    [[nodiscard]] ResidentChunk* residentChunk(AssetFormat::TileMapLayerId layerId,
                                               TileMapChunkCoord coord) noexcept;
    [[nodiscard]] const ResidentChunk* residentChunk(AssetFormat::TileMapLayerId layerId,
                                                     TileMapChunkCoord coord) const noexcept;

    Core::u32 m_width = 0;
    Core::u32 m_height = 0;
    float m_cellSizeMeters = 1.0f;
    Core::u16 m_chunkSize = 16;
    Core::u32 m_chunkCountX = 0;
    Core::u32 m_chunkCountY = 0;
    Core::AssetId m_tileMapAssetId{};
    Core::AssetId m_tilesetAssetId{};
    std::pmr::vector<std::byte> m_payloadBytes{};
    // Layer views borrowed from m_payloadBytes, in authored order. Built once from
    // the owned bytes, so they stay valid exactly as long as this instance does.
    std::pmr::vector<AssetFormat::TileMapLayerPayloadView> m_layers{};
    // Indexed by localTileId; entry.valid when present in tileset.
    std::pmr::vector<TileDef> m_tileDefs{};
    std::pmr::vector<ResidentChunk> m_residentChunks{};
    Core::usize m_residentCapacity = 0;
};

} // namespace Tina::Asset
