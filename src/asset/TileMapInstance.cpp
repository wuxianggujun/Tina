#include <tina/asset/TileMapInstance.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::u32 ceilDiv(Core::u32 value, Core::u32 divisor) noexcept
{
    return (value + divisor - 1U) / divisor;
}

} // namespace

TileMapInstance::TileMapInstance(Core::u32 width, Core::u32 height, float cellSize, Core::u16 chunkSize,
                                 Core::AssetId tileMapAssetId, Core::AssetId tilesetAssetId,
                                 std::pmr::vector<std::byte> payloadBytes,
                                 std::pmr::vector<AssetFormat::TileMapLayerPayloadView> layers,
                                 std::pmr::vector<TileDef> tileDefs,
                                 std::pmr::vector<ResidentChunk> residentChunks,
                                 Core::usize residentCapacity) noexcept
    : m_width(width), m_height(height), m_cellSizeMeters(cellSize), m_chunkSize(chunkSize),
      m_chunkCountX(ceilDiv(width, chunkSize)), m_chunkCountY(ceilDiv(height, chunkSize)),
      m_tileMapAssetId(tileMapAssetId), m_tilesetAssetId(tilesetAssetId),
      m_payloadBytes(std::move(payloadBytes)), m_layers(std::move(layers)),
      m_tileDefs(std::move(tileDefs)), m_residentChunks(std::move(residentChunks)),
      m_residentCapacity(residentCapacity)
{
}

Core::Result<TileMapInstance> TileMapInstance::Create(const AssetFormat::TileMapPayloadView& tileMap,
                                                      const AssetFormat::TilesetPayloadView& tileset,
                                                      Core::AssetId tileMapAssetId, Core::AssetId tilesetAssetId,
                                                      TileMapInstanceConfig config)
{
    if (!tileMapAssetId || !tilesetAssetId || tileMapAssetId == tilesetAssetId)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map instance requires distinct asset ids");
    }
    if (tileMap.payloadBytes.empty())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map payload view has no backing bytes");
    }
    auto parsedMap = AssetFormat::parseTileMapPayload(tileMap.payloadBytes);
    if (!parsedMap)
    {
        return Core::failure(std::move(parsedMap.error()));
    }
    if (tileset.tileCount == 0U)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tileset is empty");
    }
    if (config.residentChunkCapacity == 0U ||
        config.residentChunkCapacity > AssetFormat::TileMapWire::MaxChunkRefsPerMap)
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "resident chunk capacity must be in the TileMap root dependency range");
    }
    if (config.memoryResource == nullptr)
    {
        config.memoryResource = std::pmr::get_default_resource();
    }

    Core::u16 maxLocalId = 0;
    for (Core::u32 index = 0; index < tileset.tileCount; ++index)
    {
        const auto tile = tileset.tile(index);
        if (!tile)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tileset entry missing");
        }
        maxLocalId = (std::max)(maxLocalId, tile->localId);
    }

    try
    {
        std::pmr::vector<TileDef> defs(static_cast<std::size_t>(maxLocalId) + 1U, config.memoryResource);
        for (Core::u32 index = 0; index < tileset.tileCount; ++index)
        {
            const auto tile = *tileset.tile(index);
            if (tile.localId == AssetFormat::TileMapWire::EmptyTileId)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tileset localId 0 is reserved for empty");
            }
            TileDef& def = defs[tile.localId];
            if (def.valid)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "duplicate tileset localId");
            }
            def.materialFlags = tile.materialFlags;
            def.u0 = tile.u0;
            def.v0 = tile.v0;
            def.u1 = tile.u1;
            def.v1 = tile.v1;
            def.valid = true;
        }

        std::pmr::vector<std::byte> payloadBytes{config.memoryResource};
        payloadBytes.assign(parsedMap->payloadBytes.begin(), parsedMap->payloadBytes.end());

        // Re-parse the owned copy so every cached layer view borrows the bytes the
        // instance keeps, never the caller's. Parsing once here is what lets
        // layer() -- and therefore every per-cell lookup -- avoid re-validating the
        // whole payload, which is quadratic in chunk refs and objects.
        auto ownedMap = AssetFormat::parseTileMapPayload(
            std::span<const std::byte>{payloadBytes.data(), payloadBytes.size()});
        if (!ownedMap)
        {
            return Core::failure(std::move(ownedMap.error()));
        }
        std::pmr::vector<AssetFormat::TileMapLayerPayloadView> layers{config.memoryResource};
        layers.reserve(ownedMap->layerCount);
        for (Core::u16 index = 0; index < ownedMap->layerCount; ++index)
        {
            const auto view = ownedMap->layerAt(index);
            if (!view.has_value())
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "tilemap layer disappeared after validation");
            }
            layers.push_back(*view);
        }

        std::pmr::vector<ResidentChunk> residentChunks{config.memoryResource};
        residentChunks.reserve(config.residentChunkCapacity);
        return TileMapInstance(parsedMap->widthCells, parsedMap->heightCells, parsedMap->cellSizeMeters,
                               parsedMap->chunkSizeCells, tileMapAssetId, tilesetAssetId,
                               std::move(payloadBytes), std::move(layers), std::move(defs),
                               std::move(residentChunks), config.residentChunkCapacity);
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "tile map instance allocation failed");
    }
}

bool TileMapInstance::inBounds(Core::u32 x, Core::u32 y) const noexcept
{
    return x < m_width && y < m_height;
}

Core::Result<AssetFormat::TileMapLayerPayloadView>
TileMapInstance::layer(AssetFormat::TileMapLayerId layerId) const
{
    if (!*this || layerId == 0U)
    {
        return Core::failure(AssetErrorCode::TileMapLayerNotFound, "tilemap layer id is invalid");
    }
    // Layer ids are unique (the parser rejects duplicates) and a map authors at most
    // MaxLayers of them, so a linear scan of the cached table replaces a full
    // payload re-validation.
    for (const AssetFormat::TileMapLayerPayloadView& candidate : m_layers)
    {
        if (candidate.stableLayerId == layerId)
        {
            return candidate;
        }
    }
    return Core::failure(AssetErrorCode::TileMapLayerNotFound, "tilemap layer id was not found");
}

Core::Result<AssetFormat::TileMapLayerPayloadView>
TileMapInstance::tileLayerMetadata(AssetFormat::TileMapLayerId layerId) const
{
    auto selected = layer(layerId);
    if (!selected)
    {
        return Core::failure(std::move(selected.error()));
    }
    if (selected->kind != AssetFormat::TileMapLayerKind::Tile)
    {
        return Core::failure(AssetErrorCode::TileMapLayerTypeMismatch,
                             "selected tilemap layer is not a tile layer");
    }
    return *selected;
}

TileMapInstance::ResidentChunk* TileMapInstance::residentChunk(AssetFormat::TileMapLayerId layerId,
                                                               TileMapChunkCoord coord) noexcept
{
    for (ResidentChunk& candidate : m_residentChunks)
    {
        if (candidate.layerId == layerId && candidate.coord == coord)
        {
            return &candidate;
        }
    }
    return nullptr;
}

const TileMapInstance::ResidentChunk* TileMapInstance::residentChunk(AssetFormat::TileMapLayerId layerId,
                                                                     TileMapChunkCoord coord) const noexcept
{
    for (const ResidentChunk& candidate : m_residentChunks)
    {
        if (candidate.layerId == layerId && candidate.coord == coord)
        {
            return &candidate;
        }
    }
    return nullptr;
}

TileMapChunkCoord TileMapInstance::chunkCoordForCell(Core::u32 x, Core::u32 y) const noexcept
{
    return TileMapChunkCoord{.chunkX = x / m_chunkSize, .chunkY = y / m_chunkSize};
}

Core::Status TileMapInstance::attachChunk(Core::AssetId chunkAssetId,
                                          const AssetFormat::TileMapChunkPayloadView& chunk,
                                          Core::u64 residencyGeneration)
{
    if (!*this || !chunkAssetId || residencyGeneration == 0U ||
        chunk.schemaVersion != AssetFormat::TileMapChunkWire::SchemaVersion)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tilemap chunk attach identity invalid");
    }
    if (chunk.parentTileMapId != m_tileMapAssetId)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "tilemap chunk parent id mismatch");
    }
    auto metadata = tileLayerMetadata(chunk.layerId);
    if (!metadata)
    {
        return Core::failure(std::move(metadata.error()));
    }
    const auto reference = metadata->findChunkRef(chunk.chunkX, chunk.chunkY);
    if (!reference || reference->chunkAssetId != chunkAssetId || reference->widthCells != chunk.widthCells ||
        reference->heightCells != chunk.heightCells || reference->nonEmptyCount != chunk.nonEmptyCount)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "tilemap chunk payload does not match its root reference");
    }
    const TileMapChunkCoord coord{.chunkX = chunk.chunkX, .chunkY = chunk.chunkY};
    if (residentChunk(chunk.layerId, coord) != nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tilemap chunk coordinate is already resident");
    }
    if (m_residentChunks.size() >= m_residentCapacity)
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded, "tilemap resident chunk capacity exhausted");
    }

    try
    {
        ResidentChunk candidate{
            .layerId = chunk.layerId,
            .coord = coord,
            .widthCells = chunk.widthCells,
            .heightCells = chunk.heightCells,
            .nonEmptyCount = chunk.nonEmptyCount,
            .residencyGeneration = residencyGeneration,
            .contentRevision = 1U,
            .cells = std::pmr::vector<Core::u16>{m_residentChunks.get_allocator().resource()},
        };
        candidate.cells.reserve(chunk.cellCount);
        for (Core::u16 y = 0; y < chunk.heightCells; ++y)
        {
            for (Core::u16 x = 0; x < chunk.widthCells; ++x)
            {
                const auto localId = chunk.cellAt(x, y);
                if (!localId)
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "tilemap chunk cell missing after validation");
                }
                if (*localId != AssetFormat::TileMapWire::EmptyTileId &&
                    (*localId >= m_tileDefs.size() || !m_tileDefs[*localId].valid))
                {
                    return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                         "tilemap chunk references unknown tileset localId");
                }
                candidate.cells.push_back(*localId);
            }
        }
        m_residentChunks.push_back(std::move(candidate));
        return Core::success();
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "tilemap resident chunk allocation failed");
    }
}

Core::Status TileMapInstance::detachChunk(AssetFormat::TileMapLayerId layerId,
                                          TileMapChunkCoord coord) noexcept
{
    for (Core::usize index = 0; index < m_residentChunks.size(); ++index)
    {
        if (m_residentChunks[index].layerId == layerId && m_residentChunks[index].coord == coord)
        {
            if (index + 1U != m_residentChunks.size())
            {
                m_residentChunks[index] = std::move(m_residentChunks.back());
            }
            m_residentChunks.pop_back();
            return Core::success();
        }
    }
    // Reported rather than silently succeeding: a caller that believes a chunk is
    // resident when the instance does not is a desync, and the whole point of
    // returning Status here is to surface it. Callers who legitimately do not know
    // should ask isChunkResident() first.
    return Core::failure(AssetErrorCode::TileMapChunkNotResident,
                         "tilemap chunk to detach is not resident");
}

bool TileMapInstance::isChunkResident(AssetFormat::TileMapLayerId layerId,
                                      TileMapChunkCoord coord) const noexcept
{
    return residentChunk(layerId, coord) != nullptr;
}

Core::Result<TileMapChunkState> TileMapInstance::chunkState(AssetFormat::TileMapLayerId layerId,
                                                            Core::u32 chunkX, Core::u32 chunkY) const
{
    auto metadata = tileLayerMetadata(layerId);
    if (!metadata)
    {
        return Core::failure(std::move(metadata.error()));
    }
    if (chunkX >= m_chunkCountX || chunkY >= m_chunkCountY)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "tilemap chunk coordinates are out of bounds");
    }
    const TileMapChunkCoord coord{.chunkX = chunkX, .chunkY = chunkY};
    if (!metadata->findChunkRef(chunkX, chunkY))
    {
        return TileMapChunkState{};
    }
    const ResidentChunk* resident = residentChunk(layerId, coord);
    if (resident == nullptr)
    {
        return Core::failure(AssetErrorCode::TileMapChunkNotResident,
                             "tilemap chunk is referenced but not resident");
    }
    return TileMapChunkState{
        .contentRevision = resident->contentRevision,
        .residencyGeneration = resident->residencyGeneration,
        .nonEmptyCount = resident->nonEmptyCount,
    };
}

Core::Result<TileMapChunkCellsView> TileMapInstance::chunkCells(AssetFormat::TileMapLayerId layerId,
                                                                TileMapChunkCoord coord) const
{
    auto metadata = tileLayerMetadata(layerId);
    if (!metadata)
    {
        return Core::failure(std::move(metadata.error()));
    }
    if (!metadata->findChunkRef(coord.chunkX, coord.chunkY))
    {
        return Core::failure(AssetErrorCode::TileMapLayerNotFound,
                             "tilemap layer does not reference the requested chunk");
    }
    const ResidentChunk* resident = residentChunk(layerId, coord);
    if (resident == nullptr)
    {
        return Core::failure(AssetErrorCode::TileMapChunkNotResident,
                             "tilemap chunk is referenced but not resident");
    }
    return TileMapChunkCellsView{
        .layerId = layerId,
        .coord = coord,
        .originCellX = coord.chunkX * m_chunkSize,
        .originCellY = coord.chunkY * m_chunkSize,
        .widthCells = resident->widthCells,
        .heightCells = resident->heightCells,
        .nonEmptyCount = resident->nonEmptyCount,
        .cells = std::span<const Core::u16>{resident->cells.data(), resident->cells.size()},
    };
}

std::optional<TileMapTileInfo> TileMapInstance::tileInfoForLocalId(Core::u16 localTileId) const noexcept
{
    if (localTileId == AssetFormat::TileMapWire::EmptyTileId || localTileId >= m_tileDefs.size() ||
        !m_tileDefs[localTileId].valid)
    {
        return std::nullopt;
    }
    const TileDef& def = m_tileDefs[localTileId];
    return TileMapTileInfo{
        .localTileId = localTileId,
        .materialFlags = def.materialFlags,
        .u0 = def.u0,
        .v0 = def.v0,
        .u1 = def.u1,
        .v1 = def.v1,
        .empty = false,
    };
}

Core::Result<Core::u16> TileMapInstance::tileIdAt(AssetFormat::TileMapLayerId layerId, Core::u32 x,
                                                  Core::u32 y) const
{
    auto metadata = tileLayerMetadata(layerId);
    if (!metadata)
    {
        return Core::failure(std::move(metadata.error()));
    }
    if (!inBounds(x, y))
    {
        return AssetFormat::TileMapWire::EmptyTileId;
    }
    const TileMapChunkCoord coord = chunkCoordForCell(x, y);
    if (!metadata->findChunkRef(coord.chunkX, coord.chunkY))
    {
        return AssetFormat::TileMapWire::EmptyTileId;
    }
    const ResidentChunk* resident = residentChunk(layerId, coord);
    if (resident == nullptr)
    {
        return Core::failure(AssetErrorCode::TileMapChunkNotResident,
                             "tilemap cell belongs to a non-resident chunk");
    }
    const Core::u32 localX = x - coord.chunkX * m_chunkSize;
    const Core::u32 localY = y - coord.chunkY * m_chunkSize;
    if (localX >= resident->widthCells || localY >= resident->heightCells)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "tilemap resident chunk extent does not cover requested cell");
    }
    return resident->cells[static_cast<Core::usize>(localY) * resident->widthCells + localX];
}

Core::Result<std::optional<TileMapTileInfo>>
TileMapInstance::tileInfoAt(AssetFormat::TileMapLayerId layerId, Core::u32 x, Core::u32 y) const
{
    if (!inBounds(x, y))
    {
        auto metadata = tileLayerMetadata(layerId);
        if (!metadata)
        {
            return Core::failure(std::move(metadata.error()));
        }
        return std::optional<TileMapTileInfo>{};
    }
    auto localId = tileIdAt(layerId, x, y);
    if (!localId)
    {
        return Core::failure(std::move(localId.error()));
    }
    TileMapTileInfo info{
        .localTileId = *localId,
        .empty = *localId == AssetFormat::TileMapWire::EmptyTileId,
    };
    if (*localId != AssetFormat::TileMapWire::EmptyTileId && *localId < m_tileDefs.size() &&
        m_tileDefs[*localId].valid)
    {
        const TileDef& def = m_tileDefs[*localId];
        info.materialFlags = def.materialFlags;
        info.u0 = def.u0;
        info.v0 = def.v0;
        info.u1 = def.u1;
        info.v1 = def.v1;
    }
    return std::optional<TileMapTileInfo>{info};
}

Core::Status TileMapInstance::setTile(AssetFormat::TileMapLayerId layerId, Core::u32 x, Core::u32 y,
                                      Core::u16 localTileId)
{
    if (!inBounds(x, y))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile coordinates out of bounds");
    }
    auto metadata = tileLayerMetadata(layerId);
    if (!metadata)
    {
        return Core::failure(std::move(metadata.error()));
    }
    if (localTileId != AssetFormat::TileMapWire::EmptyTileId &&
        (localTileId >= m_tileDefs.size() || !m_tileDefs[localTileId].valid))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "unknown tileset localId");
    }
    const TileMapChunkCoord coord = chunkCoordForCell(x, y);
    if (!metadata->findChunkRef(coord.chunkX, coord.chunkY))
    {
        if (localTileId == AssetFormat::TileMapWire::EmptyTileId)
        {
            return Core::success();
        }
        return Core::failure(AssetErrorCode::TileMapChunkNotResident,
                             "known-empty chunk has no mutable resident asset");
    }
    ResidentChunk* resident = residentChunk(layerId, coord);
    if (resident == nullptr)
    {
        return Core::failure(AssetErrorCode::TileMapChunkNotResident,
                             "cannot edit a non-resident tilemap chunk");
    }
    const Core::u32 localX = x - coord.chunkX * m_chunkSize;
    const Core::u32 localY = y - coord.chunkY * m_chunkSize;
    const Core::usize index = static_cast<Core::usize>(localY) * resident->widthCells + localX;
    const Core::u16 previous = resident->cells[index];
    if (previous == localTileId)
    {
        return Core::success();
    }
    if (previous == AssetFormat::TileMapWire::EmptyTileId)
    {
        ++resident->nonEmptyCount;
    }
    else if (localTileId == AssetFormat::TileMapWire::EmptyTileId)
    {
        --resident->nonEmptyCount;
    }
    resident->cells[index] = localTileId;
    ++resident->contentRevision;
    if (resident->contentRevision == 0U)
    {
        resident->contentRevision = 1U;
    }
    return Core::success();
}

Core::Result<Core::u32> TileMapInstance::chunkRevision(AssetFormat::TileMapLayerId layerId, Core::u32 chunkX,
                                                        Core::u32 chunkY) const
{
    auto state = chunkState(layerId, chunkX, chunkY);
    if (!state)
    {
        return Core::failure(std::move(state.error()));
    }
    return state->contentRevision;
}

Core::Result<Core::u32> TileMapInstance::querySolidAabb(AssetFormat::TileMapLayerId layerId,
                                                        const TileMapSolidQuery& query,
                                                        std::pmr::vector<TileMapSolidHit>& out) const
{
    auto metadata = tileLayerMetadata(layerId);
    if (!metadata)
    {
        return Core::failure(std::move(metadata.error()));
    }
    if (!(query.maxX > query.minX) || !(query.maxY > query.minY) || !std::isfinite(query.minX) ||
        !std::isfinite(query.maxX) || !std::isfinite(query.minY) || !std::isfinite(query.maxY))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "solid query AABB invalid");
    }

    out.clear();
    const float mapMaxX = static_cast<float>(m_width) * m_cellSizeMeters;
    const float mapMaxY = static_cast<float>(m_height) * m_cellSizeMeters;
    if (query.maxX <= 0.0f || query.maxY <= 0.0f || query.minX >= mapMaxX || query.minY >= mapMaxY)
    {
        return 0U;
    }
    const float inverseCellSize = 1.0f / m_cellSizeMeters;
    const auto clampCell = [&](float world, Core::u32 limit) -> Core::u32 {
        if (world < 0.0f)
        {
            return 0U;
        }
        const float lastCellBoundary = static_cast<float>(limit) * m_cellSizeMeters;
        if (world >= lastCellBoundary)
        {
            return limit - 1U;
        }
        const auto cell = static_cast<Core::u32>(world * inverseCellSize);
        return cell >= limit ? limit - 1U : cell;
    };
    const Core::u32 minX = clampCell(query.minX, m_width);
    const Core::u32 minY = clampCell(query.minY, m_height);
    const Core::u32 maxX = clampCell(std::nextafter(query.maxX, 0.0f), m_width);
    const Core::u32 maxY = clampCell(std::nextafter(query.maxY, 0.0f), m_height);

    try
    {
        // Resolved lazily and reused while the scan stays inside one chunk. Without
        // this, every cell redid the layer scan, the chunk-ref binary search and the
        // resident-chunk scan, all of which are constant across a chunk.
        TileMapChunkCoord cachedCoord{};
        const ResidentChunk* cached = nullptr;
        for (Core::u32 y = minY; y <= maxY; ++y)
        {
            for (Core::u32 x = minX; x <= maxX; ++x)
            {
                const float cellMinX = static_cast<float>(x) * m_cellSizeMeters;
                const float cellMinY = static_cast<float>(y) * m_cellSizeMeters;
                const float cellMaxX = cellMinX + m_cellSizeMeters;
                const float cellMaxY = cellMinY + m_cellSizeMeters;
                if (cellMaxX <= query.minX || cellMinX >= query.maxX || cellMaxY <= query.minY ||
                    cellMinY >= query.maxY)
                {
                    continue;
                }
                const TileMapChunkCoord coord = chunkCoordForCell(x, y);
                if (cached == nullptr || !(cachedCoord == coord))
                {
                    if (!metadata->findChunkRef(coord.chunkX, coord.chunkY))
                    {
                        // Unreferenced chunks hold no cells, so nothing here is solid.
                        continue;
                    }
                    cached = residentChunk(layerId, coord);
                    if (cached == nullptr)
                    {
                        out.clear();
                        return Core::failure(AssetErrorCode::TileMapChunkNotResident,
                                             "tilemap cell belongs to a non-resident chunk");
                    }
                    cachedCoord = coord;
                }
                const Core::u32 localX = x - coord.chunkX * m_chunkSize;
                const Core::u32 localY = y - coord.chunkY * m_chunkSize;
                if (localX >= cached->widthCells || localY >= cached->heightCells)
                {
                    out.clear();
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "tilemap resident chunk extent does not cover requested cell");
                }
                const Core::u16 localId =
                    cached->cells[static_cast<Core::usize>(localY) * cached->widthCells + localX];
                if (localId == AssetFormat::TileMapWire::EmptyTileId || localId >= m_tileDefs.size() ||
                    !m_tileDefs[localId].valid ||
                    (m_tileDefs[localId].materialFlags & AssetFormat::TilesetWire::MaterialSolid) == 0U)
                {
                    continue;
                }
                out.push_back(TileMapSolidHit{
                    .layerId = layerId,
                    .cellX = x,
                    .cellY = y,
                    .localTileId = localId,
                    .materialFlags = m_tileDefs[localId].materialFlags,
                });
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        out.clear();
        return Core::failure(AssetErrorCode::AllocationFailed, "solid query allocation failed");
    }
    return static_cast<Core::u32>(out.size());
}

} // namespace Tina::Asset
