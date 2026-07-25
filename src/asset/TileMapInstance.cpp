#include <tina/asset/TileMapInstance.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace Tina::Asset {
namespace {

[[nodiscard]] bool isPowerOfTwo(Core::u16 value) noexcept
{
    return value != 0 && (value & (value - 1U)) == 0;
}

[[nodiscard]] Core::u32 ceilDiv(Core::u32 value, Core::u32 divisor) noexcept
{
    return (value + divisor - 1U) / divisor;
}

} // namespace

TileMapInstance::TileMapInstance(Core::u32 width, Core::u32 height, float cellSize, Core::u16 chunkSize,
                                 Core::u32 chunkCountX, Core::u32 chunkCountY, Core::AssetId tileMapAssetId,
                                 Core::AssetId tilesetAssetId, std::pmr::vector<std::byte> payloadBytes,
                                 std::pmr::vector<TileLayerState> tileLayers,
                                 std::pmr::vector<TileDef> tileDefs) noexcept
    : m_width(width), m_height(height), m_cellSizeMeters(cellSize), m_chunkSize(chunkSize), m_chunkCountX(chunkCountX),
      m_chunkCountY(chunkCountY), m_tileMapAssetId(tileMapAssetId), m_tilesetAssetId(tilesetAssetId),
      m_payloadBytes(std::move(payloadBytes)), m_tileLayers(std::move(tileLayers)), m_tileDefs(std::move(tileDefs))
{
}

Core::Result<TileMapInstance> TileMapInstance::Create(const AssetFormat::TileMapPayloadView& tileMap,
                                                      const AssetFormat::TilesetPayloadView& tileset,
                                                      Core::AssetId tileMapAssetId, Core::AssetId tilesetAssetId,
                                                      TileMapInstanceConfig config)
{
    if (!tileMapAssetId || !tilesetAssetId)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map instance requires asset ids");
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
    if (config.chunkSizeCells == 0 || config.chunkSizeCells > 64 || !isPowerOfTwo(config.chunkSizeCells))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "chunkSizeCells must be power-of-two in [1,64]");
    }
    if (tileset.tileCount == 0)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tileset is empty");
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
        maxLocalId = std::max(maxLocalId, tile->localId);
    }

    try
    {
        std::pmr::vector<TileDef> defs(static_cast<std::size_t>(maxLocalId) + 1U, config.memoryResource);
        for (Core::u32 index = 0; index < tileset.tileCount; ++index)
        {
            const auto tile = *tileset.tile(index);
            if (tile.localId == 0)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tileset localId 0 is reserved for empty");
            }
            auto& def = defs[tile.localId];
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

        const Core::u32 chunkCountX = ceilDiv(parsedMap->widthCells, config.chunkSizeCells);
        const Core::u32 chunkCountY = ceilDiv(parsedMap->heightCells, config.chunkSizeCells);
        const std::size_t cellCount = static_cast<std::size_t>(parsedMap->widthCells) * parsedMap->heightCells;
        const std::size_t chunkCount = static_cast<std::size_t>(chunkCountX) * chunkCountY;

        std::pmr::vector<TileLayerState> tileLayers{config.memoryResource};
        for (Core::u16 layerIndex = 0; layerIndex < parsedMap->layerCount; ++layerIndex)
        {
            const auto layer = parsedMap->layerAt(layerIndex);
            if (!layer)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map layer missing after validation");
            }
            if (layer->kind != AssetFormat::TileMapLayerKind::Tile)
            {
                continue;
            }

            TileLayerState state{
                .layerId = layer->stableLayerId,
                .payloadTileByteOffset =
                    static_cast<Core::usize>(layer->tileBytes.data() - parsedMap->payloadBytes.data()),
                .cells = std::pmr::vector<Core::u16>{config.memoryResource},
                .chunkRevisions = std::pmr::vector<Core::u32>{config.memoryResource},
            };
            if (state.payloadTileByteOffset + cellCount * sizeof(Core::u16) > parsedMap->payloadBytes.size())
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "tile map layer storage exceeds validated payload");
            }
            state.cells.resize(cellCount);
            for (Core::u32 y = 0; y < parsedMap->heightCells; ++y)
            {
                for (Core::u32 x = 0; x < parsedMap->widthCells; ++x)
                {
                    const auto id = layer->tileAt(x, y);
                    if (!id)
                    {
                        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map cell missing");
                    }
                    if (*id != AssetFormat::TileMapWire::EmptyTileId &&
                        (*id >= defs.size() || !defs[*id].valid))
                    {
                        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                             "tile map references unknown localId");
                    }
                    state.cells[static_cast<std::size_t>(y) * parsedMap->widthCells + x] = *id;
                }
            }
            state.chunkRevisions.resize(chunkCount, 1U);
            tileLayers.push_back(std::move(state));
        }

        std::pmr::vector<std::byte> payloadBytes{config.memoryResource};
        payloadBytes.assign(parsedMap->payloadBytes.begin(), parsedMap->payloadBytes.end());
        return TileMapInstance(parsedMap->widthCells, parsedMap->heightCells, parsedMap->cellSizeMeters,
                               config.chunkSizeCells, chunkCountX, chunkCountY, tileMapAssetId, tilesetAssetId,
                               std::move(payloadBytes), std::move(tileLayers), std::move(defs));
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

Core::u32 TileMapInstance::cellIndex(Core::u32 x, Core::u32 y) const noexcept
{
    return y * m_width + x;
}

Core::u32 TileMapInstance::chunkIndex(Core::u32 chunkX, Core::u32 chunkY) const noexcept
{
    return chunkY * m_chunkCountX + chunkX;
}

Core::Result<AssetFormat::TileMapLayerPayloadView>
TileMapInstance::layer(AssetFormat::TileMapLayerId layerId) const
{
    if (!*this || layerId == 0)
    {
        return Core::failure(AssetErrorCode::TileMapLayerNotFound, "tilemap layer id is invalid");
    }
    auto parsed = AssetFormat::parseTileMapPayload(
        std::span<const std::byte>{m_payloadBytes.data(), m_payloadBytes.size()});
    if (!parsed)
    {
        return Core::failure(std::move(parsed.error()));
    }
    const auto selected = parsed->findLayer(layerId);
    if (!selected)
    {
        return Core::failure(AssetErrorCode::TileMapLayerNotFound, "tilemap layer id was not found");
    }
    return *selected;
}

Core::Result<TileMapInstance::TileLayerState*> TileMapInstance::tileLayer(AssetFormat::TileMapLayerId layerId)
{
    for (TileLayerState& candidate : m_tileLayers)
    {
        if (candidate.layerId == layerId)
        {
            return &candidate;
        }
    }
    auto metadata = layer(layerId);
    if (!metadata)
    {
        return Core::failure(std::move(metadata.error()));
    }
    return Core::failure(AssetErrorCode::TileMapLayerTypeMismatch, "selected tilemap layer is not a tile layer");
}

Core::Result<const TileMapInstance::TileLayerState*>
TileMapInstance::tileLayer(AssetFormat::TileMapLayerId layerId) const
{
    for (const TileLayerState& candidate : m_tileLayers)
    {
        if (candidate.layerId == layerId)
        {
            return &candidate;
        }
    }
    auto metadata = layer(layerId);
    if (!metadata)
    {
        return Core::failure(std::move(metadata.error()));
    }
    return Core::failure(AssetErrorCode::TileMapLayerTypeMismatch, "selected tilemap layer is not a tile layer");
}

TileMapChunkCoord TileMapInstance::chunkCoordForCell(Core::u32 x, Core::u32 y) const noexcept
{
    return TileMapChunkCoord{.chunkX = x / m_chunkSize, .chunkY = y / m_chunkSize};
}

void TileMapInstance::bumpChunkForCell(TileLayerState& layer, Core::u32 x, Core::u32 y) noexcept
{
    const auto coord = chunkCoordForCell(x, y);
    const auto index = chunkIndex(coord.chunkX, coord.chunkY);
    if (index < layer.chunkRevisions.size())
    {
        ++layer.chunkRevisions[index];
        if (layer.chunkRevisions[index] == 0U)
        {
            layer.chunkRevisions[index] = 1U;
        }
    }
}

Core::Result<Core::u16> TileMapInstance::tileIdAt(AssetFormat::TileMapLayerId layerId, Core::u32 x,
                                                  Core::u32 y) const
{
    auto selected = tileLayer(layerId);
    if (!selected)
    {
        return Core::failure(std::move(selected.error()));
    }
    if (!inBounds(x, y))
    {
        return AssetFormat::TileMapWire::EmptyTileId;
    }
    return (*selected)->cells[cellIndex(x, y)];
}

Core::Result<std::optional<TileMapTileInfo>>
TileMapInstance::tileInfoAt(AssetFormat::TileMapLayerId layerId, Core::u32 x, Core::u32 y) const
{
    auto selected = tileLayer(layerId);
    if (!selected)
    {
        return Core::failure(std::move(selected.error()));
    }
    if (!inBounds(x, y))
    {
        return std::optional<TileMapTileInfo>{};
    }
    const auto id = (*selected)->cells[cellIndex(x, y)];
    TileMapTileInfo info{.localTileId = id, .empty = id == AssetFormat::TileMapWire::EmptyTileId};
    if (id != AssetFormat::TileMapWire::EmptyTileId && id < m_tileDefs.size() && m_tileDefs[id].valid)
    {
        const TileDef& def = m_tileDefs[id];
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
    if (!*this)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map instance is empty");
    }
    auto selected = tileLayer(layerId);
    if (!selected)
    {
        return Core::failure(std::move(selected.error()));
    }
    if (!inBounds(x, y))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile coordinates out of bounds");
    }
    if (localTileId != AssetFormat::TileMapWire::EmptyTileId &&
        (localTileId >= m_tileDefs.size() || !m_tileDefs[localTileId].valid))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "unknown tileset localId");
    }
    const auto index = cellIndex(x, y);
    if ((*selected)->cells[index] == localTileId)
    {
        return Core::success();
    }

    const auto wireOffset =
        (*selected)->payloadTileByteOffset + static_cast<Core::usize>(index) * sizeof(Core::u16);
    if (wireOffset + sizeof(Core::u16) > m_payloadBytes.size())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "tilemap layer storage no longer matches validated payload");
    }

    m_payloadBytes[wireOffset] = static_cast<std::byte>(localTileId & 0xFFU);
    m_payloadBytes[wireOffset + 1U] = static_cast<std::byte>((localTileId >> 8U) & 0xFFU);
    (*selected)->cells[index] = localTileId;
    bumpChunkForCell(**selected, x, y);
    return Core::success();
}

Core::Result<Core::u32> TileMapInstance::chunkRevision(AssetFormat::TileMapLayerId layerId, Core::u32 chunkX,
                                                        Core::u32 chunkY) const
{
    auto selected = tileLayer(layerId);
    if (!selected)
    {
        return Core::failure(std::move(selected.error()));
    }
    if (chunkX >= m_chunkCountX || chunkY >= m_chunkCountY)
    {
        return 0U;
    }
    return (*selected)->chunkRevisions[chunkIndex(chunkX, chunkY)];
}

Core::Result<Core::u32> TileMapInstance::querySolidAabb(AssetFormat::TileMapLayerId layerId,
                                                        const TileMapSolidQuery& query,
                                                        std::pmr::vector<TileMapSolidHit>& out) const
{
    if (!*this)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map instance is empty");
    }
    auto selected = tileLayer(layerId);
    if (!selected)
    {
        return Core::failure(std::move(selected.error()));
    }
    if (!(query.maxX > query.minX) || !(query.maxY > query.minY) || !std::isfinite(query.minX) ||
        !std::isfinite(query.maxX) || !std::isfinite(query.minY) || !std::isfinite(query.maxY))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "solid query AABB invalid");
    }

    out.clear();
    const float inv = 1.0f / m_cellSizeMeters;
    const auto clampCell = [&](float world, Core::u32 limit) -> Core::u32 {
        if (world < 0.0f)
        {
            return 0;
        }
        const auto cell = static_cast<Core::u32>(world * inv);
        return cell >= limit ? limit - 1U : cell;
    };
    const Core::u32 minX = clampCell(query.minX, m_width);
    const Core::u32 minY = clampCell(query.minY, m_height);
    const Core::u32 maxX = clampCell(std::nextafter(query.maxX, 0.0f), m_width);
    const Core::u32 maxY = clampCell(std::nextafter(query.maxY, 0.0f), m_height);

    try
    {
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
                const auto id = (*selected)->cells[cellIndex(x, y)];
                if (id == AssetFormat::TileMapWire::EmptyTileId || id >= m_tileDefs.size() || !m_tileDefs[id].valid ||
                    (m_tileDefs[id].materialFlags & AssetFormat::TilesetWire::MaterialSolid) == 0)
                {
                    continue;
                }
                out.push_back(TileMapSolidHit{
                    .layerId = layerId,
                    .cellX = x,
                    .cellY = y,
                    .localTileId = id,
                    .materialFlags = m_tileDefs[id].materialFlags,
                });
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "solid query allocation failed");
    }
    return static_cast<Core::u32>(out.size());
}

} // namespace Tina::Asset
