#include <tina/asset/TileMapInstance.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
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
                                 Core::AssetId tilesetAssetId, std::pmr::vector<Core::u16> cells,
                                 std::pmr::vector<Core::u32> chunkRevisions, std::pmr::vector<TileDef> tileDefs) noexcept
    : m_width(width), m_height(height), m_cellSizeMeters(cellSize), m_chunkSize(chunkSize), m_chunkCountX(chunkCountX),
      m_chunkCountY(chunkCountY), m_tileMapAssetId(tileMapAssetId), m_tilesetAssetId(tilesetAssetId),
      m_cells(std::move(cells)), m_chunkRevisions(std::move(chunkRevisions)), m_tileDefs(std::move(tileDefs))
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
    if (tileMap.widthCells == 0 || tileMap.heightCells == 0 || tileMap.tileCount == 0)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map dimensions invalid");
    }
    if (!(tileMap.cellSizeMeters > 0.0f) || tileMap.cellSizeMeters != tileMap.cellSizeMeters)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cellSizeMeters invalid");
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

        const std::size_t cellCount = static_cast<std::size_t>(tileMap.widthCells) * tileMap.heightCells;
        if (cellCount != tileMap.tileCount)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map cell count mismatch");
        }
        std::pmr::vector<Core::u16> cells(config.memoryResource);
        cells.resize(cellCount);
        for (Core::u32 y = 0; y < tileMap.heightCells; ++y)
        {
            for (Core::u32 x = 0; x < tileMap.widthCells; ++x)
            {
                const auto id = tileMap.tileAt(x, y);
                if (!id)
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map cell missing");
                }
                if (*id != 0 && (*id >= defs.size() || !defs[*id].valid))
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map references unknown localId");
                }
                cells[static_cast<std::size_t>(y) * tileMap.widthCells + x] = *id;
            }
        }

        const Core::u32 chunkCountX = ceilDiv(tileMap.widthCells, config.chunkSizeCells);
        const Core::u32 chunkCountY = ceilDiv(tileMap.heightCells, config.chunkSizeCells);
        std::pmr::vector<Core::u32> revisions(static_cast<std::size_t>(chunkCountX) * chunkCountY, 1U,
                                              config.memoryResource);

        return TileMapInstance(tileMap.widthCells, tileMap.heightCells, tileMap.cellSizeMeters, config.chunkSizeCells,
                               chunkCountX, chunkCountY, tileMapAssetId, tilesetAssetId, std::move(cells),
                               std::move(revisions), std::move(defs));
    } catch (const std::bad_alloc&)
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

TileMapChunkCoord TileMapInstance::chunkCoordForCell(Core::u32 x, Core::u32 y) const noexcept
{
    return TileMapChunkCoord{.chunkX = x / m_chunkSize, .chunkY = y / m_chunkSize};
}

void TileMapInstance::bumpChunkForCell(Core::u32 x, Core::u32 y) noexcept
{
    const auto coord = chunkCoordForCell(x, y);
    const auto index = chunkIndex(coord.chunkX, coord.chunkY);
    if (index < m_chunkRevisions.size())
    {
        ++m_chunkRevisions[index];
        if (m_chunkRevisions[index] == 0U)
        {
            m_chunkRevisions[index] = 1U;
        }
    }
}

Core::u16 TileMapInstance::tileIdAt(Core::u32 x, Core::u32 y) const noexcept
{
    if (!inBounds(x, y))
    {
        return AssetFormat::TileMapWire::EmptyTileId;
    }
    return m_cells[cellIndex(x, y)];
}

std::optional<TileMapTileInfo> TileMapInstance::tileInfoAt(Core::u32 x, Core::u32 y) const noexcept
{
    if (!inBounds(x, y))
    {
        return std::nullopt;
    }
    const auto id = m_cells[cellIndex(x, y)];
    TileMapTileInfo info{.localTileId = id, .empty = id == 0};
    if (id != 0 && id < m_tileDefs.size() && m_tileDefs[id].valid)
    {
        const auto& def = m_tileDefs[id];
        info.materialFlags = def.materialFlags;
        info.u0 = def.u0;
        info.v0 = def.v0;
        info.u1 = def.u1;
        info.v1 = def.v1;
    }
    return info;
}

Core::Status TileMapInstance::setTile(Core::u32 x, Core::u32 y, Core::u16 localTileId) noexcept
{
    if (!*this)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map instance is empty");
    }
    if (!inBounds(x, y))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile coordinates out of bounds");
    }
    if (localTileId != 0 && (localTileId >= m_tileDefs.size() || !m_tileDefs[localTileId].valid))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "unknown tileset localId");
    }
    const auto index = cellIndex(x, y);
    if (m_cells[index] == localTileId)
    {
        return Core::success();
    }
    m_cells[index] = localTileId;
    bumpChunkForCell(x, y);
    return Core::success();
}

Core::u32 TileMapInstance::chunkRevision(Core::u32 chunkX, Core::u32 chunkY) const noexcept
{
    if (chunkX >= m_chunkCountX || chunkY >= m_chunkCountY)
    {
        return 0;
    }
    return m_chunkRevisions[chunkIndex(chunkX, chunkY)];
}

Core::Result<Core::u32> TileMapInstance::querySolidAabb(const TileMapSolidQuery& query,
                                                        std::pmr::vector<TileMapSolidHit>& out) const
{
    if (!*this)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map instance is empty");
    }
    if (!(query.maxX > query.minX) || !(query.maxY > query.minY) || query.minX != query.minX ||
        query.maxX != query.maxX || query.minY != query.minY || query.maxY != query.maxY)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "solid query AABB invalid");
    }

    out.clear();
    const float inv = 1.0f / m_cellSizeMeters;
    auto clampCell = [&](float world, Core::u32 limit) -> Core::u32 {
        if (world < 0.0f)
        {
            return 0;
        }
        const auto cell = static_cast<Core::u32>(world * inv);
        return cell >= limit ? limit - 1U : cell;
    };
    // Expand max edge exclusive-ish: use floor of max - epsilon by using ceil-1 pattern.
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
                // World cell coverage: [x*s, (x+1)*s) x [y*s, (y+1)*s)
                const float cellMinX = static_cast<float>(x) * m_cellSizeMeters;
                const float cellMinY = static_cast<float>(y) * m_cellSizeMeters;
                const float cellMaxX = cellMinX + m_cellSizeMeters;
                const float cellMaxY = cellMinY + m_cellSizeMeters;
                if (cellMaxX <= query.minX || cellMinX >= query.maxX || cellMaxY <= query.minY ||
                    cellMinY >= query.maxY)
                {
                    continue;
                }
                const auto id = m_cells[cellIndex(x, y)];
                if (id == 0 || id >= m_tileDefs.size() || !m_tileDefs[id].valid)
                {
                    continue;
                }
                if ((m_tileDefs[id].materialFlags & AssetFormat::TilesetWire::MaterialSolid) == 0)
                {
                    continue;
                }
                out.push_back(TileMapSolidHit{
                    .cellX = x,
                    .cellY = y,
                    .localTileId = id,
                    .materialFlags = m_tileDefs[id].materialFlags,
                });
            }
        }
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "solid query allocation failed");
    }
    return static_cast<Core::u32>(out.size());
}

} // namespace Tina::Asset
