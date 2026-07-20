#include <tina/asset/TileChunkView.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <cmath>
#include <new>

namespace Tina::Asset {
namespace {

[[nodiscard]] bool aabbOverlap(float aMinX, float aMinY, float aMaxX, float aMaxY, float bMinX, float bMinY,
                               float bMaxX, float bMaxY) noexcept
{
    return aMaxX > bMinX && aMinX < bMaxX && aMaxY > bMinY && aMinY < bMaxY;
}

} // namespace

Core::Result<Core::u32> extractVisibleTileChunks(const TileMapInstance& map, const TileChunkCameraQuery& camera,
                                                 std::pmr::vector<TileChunkView>& out)
{
    if (!map)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map instance is empty");
    }
    if (!(camera.halfWidth > 0.0f) || !(camera.halfHeight > 0.0f) || camera.halfWidth != camera.halfWidth ||
        camera.halfHeight != camera.halfHeight || camera.centerX != camera.centerX || camera.centerY != camera.centerY)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "camera query invalid");
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
                        if (map.tileIdAt(originX + x, originY + y) != 0)
                        {
                            ++nonEmpty;
                        }
                    }
                }
                if (nonEmpty == 0)
                {
                    continue;
                }

                out.push_back(TileChunkView{
                    .coord = TileMapChunkCoord{.chunkX = cx, .chunkY = cy},
                    .revision = map.chunkRevision(cx, cy),
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

Core::Result<Core::u32> collectChunkNonEmptyCells(const TileMapInstance& map, TileMapChunkCoord coord,
                                                  std::pmr::vector<TileChunkCell>& out)
{
    if (!map)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map instance is empty");
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
                auto info = map.tileInfoAt(cellX, cellY);
                if (!info || info->empty)
                {
                    continue;
                }
                out.push_back(TileChunkCell{.cellX = cellX, .cellY = cellY, .info = *info});
            }
        }
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "chunk cell collection allocation failed");
    }
    return static_cast<Core::u32>(out.size());
}

} // namespace Tina::Asset
