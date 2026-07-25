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

    try
    {
        for (Core::u32 cy = 0; cy < map.chunkCountY(); ++cy)
        {
            for (Core::u32 cx = 0; cx < map.chunkCountX(); ++cx)
            {
                const Core::u32 originX = cx * chunkSize;
                const Core::u32 originY = cy * chunkSize;
                const Core::u32 width =
                    std::min<Core::u32>(chunkSize, map.widthCells() > originX ? map.widthCells() - originX : 0);
                const Core::u32 height =
                    std::min<Core::u32>(chunkSize, map.heightCells() > originY ? map.heightCells() - originY : 0);
                if (width == 0 || height == 0)
                {
                    continue;
                }
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

                auto revision = map.chunkRevision(layerId, cx, cy);
                if (!revision)
                {
                    return Core::failure(std::move(revision.error()));
                }
                out.push_back(TileChunkView{
                    .layerId = layerId,
                    .coord = TileMapChunkCoord{.chunkX = cx, .chunkY = cy},
                    .revision = *revision,
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
    const Core::u16 chunkSize = map.chunkSizeCells();
    const Core::u32 originX = coord.chunkX * chunkSize;
    const Core::u32 originY = coord.chunkY * chunkSize;
    const Core::u32 width =
        std::min<Core::u32>(chunkSize, map.widthCells() > originX ? map.widthCells() - originX : 0);
    const Core::u32 height =
        std::min<Core::u32>(chunkSize, map.heightCells() > originY ? map.heightCells() - originY : 0);
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
        return Core::failure(AssetErrorCode::AllocationFailed, "chunk cell collection allocation failed");
    }
    return static_cast<Core::u32>(out.size());
}

} // namespace Tina::Asset
