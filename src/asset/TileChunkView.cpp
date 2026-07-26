#include <tina/asset/TileChunkView.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace Tina::Asset {
namespace {

[[nodiscard]] bool aabbOverlap(float aMinX, float aMinY, float aMaxX, float aMaxY, float bMinX, float bMinY,
                               float bMaxX, float bMaxY) noexcept
{
    return aMaxX > bMinX && aMinX < bMaxX && aMaxY > bMinY && aMinY < bMaxY;
}

} // namespace

Core::Result<Core::u32> extractVisibleTileChunks(const TileMapInstance& map, AssetFormat::TileMapLayerId layerId,
                                                 const TileChunkCameraQuery& camera, std::pmr::vector<TileChunkView>& out)
{
    if (!map)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map instance is empty");
    }
    auto layer = map.layer(layerId);
    if (!layer)
    {
        return Core::failure(std::move(layer.error()));
    }
    if (layer->kind != AssetFormat::TileMapLayerKind::Tile)
    {
        return Core::failure(AssetErrorCode::TileMapLayerTypeMismatch, "visible chunk extraction requires tile layer");
    }
    if (!(camera.halfWidth > 0.0f) || !(camera.halfHeight > 0.0f) || !std::isfinite(camera.halfWidth) ||
        !std::isfinite(camera.halfHeight) || !std::isfinite(camera.centerX) || !std::isfinite(camera.centerY))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "camera query invalid");
    }
    if (!layer->visible)
    {
        out.clear();
        return Core::u32{0};
    }

    out.clear();
    const float camMinX = camera.centerX - camera.halfWidth;
    const float camMinY = camera.centerY - camera.halfHeight;
    const float camMaxX = camera.centerX + camera.halfWidth;
    const float camMaxY = camera.centerY + camera.halfHeight;
    const Core::u16 chunkSize = map.chunkSizeCells();
    const float cell = map.cellSizeMeters();
    const float mapMaxX = static_cast<float>(map.widthCells()) * cell;
    const float mapMaxY = static_cast<float>(map.heightCells()) * cell;
    if (!aabbOverlap(0.0f, 0.0f, mapMaxX, mapMaxY, camMinX, camMinY, camMaxX, camMaxY))
    {
        return Core::u32{0};
    }
    const auto chunkForWorld = [&](float world, Core::u32 chunkCount) -> Core::u32 {
        if (world <= 0.0f)
        {
            return 0U;
        }
        const auto cellIndex = static_cast<Core::u32>(world / cell);
        return (std::min)(cellIndex / chunkSize, chunkCount - 1U);
    };
    const Core::u32 minChunkX = chunkForWorld((std::max)(camMinX, 0.0f), map.chunkCountX());
    const Core::u32 minChunkY = chunkForWorld((std::max)(camMinY, 0.0f), map.chunkCountY());
    const Core::u32 maxChunkX =
        chunkForWorld(std::nextafter((std::min)(camMaxX, mapMaxX), 0.0f), map.chunkCountX());
    const Core::u32 maxChunkY =
        chunkForWorld(std::nextafter((std::min)(camMaxY, mapMaxY), 0.0f), map.chunkCountY());

    try
    {
        for (Core::u32 cy = minChunkY; cy <= maxChunkY; ++cy)
        {
            for (Core::u32 cx = minChunkX; cx <= maxChunkX; ++cx)
            {
                const auto rootRef = layer->findChunkRef(cx, cy);
                if (!rootRef)
                {
                    continue;
                }
                const TileMapChunkCoord coord{.chunkX = cx, .chunkY = cy};
                if (!map.isChunkResident(layerId, coord))
                {
                    continue;
                }
                const Core::u32 originX = cx * chunkSize;
                const Core::u32 originY = cy * chunkSize;
                const Core::u32 width = rootRef->widthCells;
                const Core::u32 height = rootRef->heightCells;
                const float worldMinX = static_cast<float>(originX) * cell;
                const float worldMinY = static_cast<float>(originY) * cell;
                const float worldMaxX = static_cast<float>(originX + width) * cell;
                const float worldMaxY = static_cast<float>(originY + height) * cell;
                if (!aabbOverlap(worldMinX, worldMinY, worldMaxX, worldMaxY, camMinX, camMinY, camMaxX, camMaxY))
                {
                    continue;
                }

                Core::u32 nonEmpty = 0;
                for (Core::u32 y = 0; y < height; ++y)
                {
                    for (Core::u32 x = 0; x < width; ++x)
                    {
                        auto tileId = map.tileIdAt(layerId, originX + x, originY + y);
                        if (!tileId)
                        {
                            out.clear();
                            return Core::failure(std::move(tileId.error()));
                        }
                        if (*tileId != AssetFormat::TileMapWire::EmptyTileId)
                        {
                            ++nonEmpty;
                        }
                    }
                }
                if (nonEmpty == 0)
                {
                    continue;
                }

                auto state = map.chunkState(layerId, cx, cy);
                if (!state)
                {
                    out.clear();
                    return Core::failure(std::move(state.error()));
                }
                out.push_back(TileChunkView{
                    .layerId = layerId,
                    .coord = coord,
                    .revision = state->contentRevision,
                    .residencyGeneration = state->residencyGeneration,
                    .originCellX = originX,
                    .originCellY = originY,
                    .widthCells = width,
                    .heightCells = height,
                    .worldMinX = worldMinX,
                    .worldMinY = worldMinY,
                    .worldMaxX = worldMaxX,
                    .worldMaxY = worldMaxY,
                    .tileMapAssetId = map.tileMapAssetId(),
                    .tilesetAssetId = map.tilesetAssetId(),
                    .nonEmptyTileCount = nonEmpty,
                    .empty = false,
                });
            }
        }
    } catch (const std::bad_alloc&)
    {
        out.clear();
        return Core::failure(AssetErrorCode::AllocationFailed, "visible tile chunk extraction allocation failed");
    }
    return static_cast<Core::u32>(out.size());
}

Core::Result<Core::u32> collectChunkNonEmptyCells(const TileMapInstance& map, AssetFormat::TileMapLayerId layerId,
                                                  TileMapChunkCoord coord, std::pmr::vector<TileChunkCell>& out)
{
    if (!map)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map instance is empty");
    }
    auto layer = map.layer(layerId);
    if (!layer)
    {
        return Core::failure(std::move(layer.error()));
    }
    if (layer->kind != AssetFormat::TileMapLayerKind::Tile)
    {
        return Core::failure(AssetErrorCode::TileMapLayerTypeMismatch, "chunk cell collection requires tile layer");
    }
    if (coord.chunkX >= map.chunkCountX() || coord.chunkY >= map.chunkCountY())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "chunk coord out of range");
    }
    out.clear();
    const auto rootRef = layer->findChunkRef(coord.chunkX, coord.chunkY);
    if (!rootRef)
    {
        return Core::u32{0};
    }
    if (!map.isChunkResident(layerId, coord))
    {
        return Core::failure(AssetErrorCode::TileMapChunkNotResident,
                             "cannot collect cells from a non-resident tilemap chunk");
    }
    const Core::u16 chunkSize = map.chunkSizeCells();
    const Core::u32 originX = coord.chunkX * chunkSize;
    const Core::u32 originY = coord.chunkY * chunkSize;
    const Core::u32 width = rootRef->widthCells;
    const Core::u32 height = rootRef->heightCells;
    try
    {
        for (Core::u32 y = 0; y < height; ++y)
        {
            for (Core::u32 x = 0; x < width; ++x)
            {
                const Core::u32 cellX = originX + x;
                const Core::u32 cellY = originY + y;
                auto info = map.tileInfoAt(layerId, cellX, cellY);
                if (!info)
                {
                    out.clear();
                    return Core::failure(std::move(info.error()));
                }
                if (!*info || (*info)->empty)
                {
                    continue;
                }
                out.push_back(TileChunkCell{.cellX = cellX, .cellY = cellY, .info = **info});
            }
        }
    } catch (const std::bad_alloc&)
    {
        out.clear();
        return Core::failure(AssetErrorCode::AllocationFailed, "chunk cell collection allocation failed");
    }
    return static_cast<Core::u32>(out.size());
}

} // namespace Tina::Asset
