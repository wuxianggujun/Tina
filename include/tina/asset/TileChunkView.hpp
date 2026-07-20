#pragma once

#include <tina/asset/TileMapInstance.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <memory_resource>
#include <vector>

namespace Tina::Asset {

// Gameplay/extraction-boundary view of one map chunk (game-2d TileChunkView).
// Does not own tile storage; nonEmptyTileCount is computed at extraction time.
// Render packet conversion (FrameResourceRef geometry) remains a later slice.
struct TileChunkView final {
    TileMapChunkCoord coord{};
    Core::u32 revision = 0;
    Core::u32 originCellX = 0;
    Core::u32 originCellY = 0;
    Core::u32 widthCells = 0;
    Core::u32 heightCells = 0;
    float worldMinX = 0.0f;
    float worldMinY = 0.0f;
    float worldMaxX = 0.0f;
    float worldMaxY = 0.0f;
    Core::AssetId tileMapAssetId{};
    Core::AssetId tilesetAssetId{};
    Core::u32 nonEmptyTileCount = 0;
    bool empty = true;
};

struct TileChunkCameraQuery final {
    // Axis-aligned camera bounds in map-local meters (rotation not applied; conservative AABB).
    float centerX = 0.0f;
    float centerY = 0.0f;
    float halfWidth = 1.0f;
    float halfHeight = 1.0f;
};

// Appends visible non-empty chunks intersecting the camera AABB. Clears `out` first.
// Empty chunks are skipped. Returns number of views written.
[[nodiscard]] Core::Result<Core::u32> extractVisibleTileChunks(const TileMapInstance& map,
                                                               const TileChunkCameraQuery& camera,
                                                               std::pmr::vector<TileChunkView>& out);

// Enumerate non-empty cells inside a chunk into `out` (local tile info + cell coords).
struct TileChunkCell final {
    Core::u32 cellX = 0;
    Core::u32 cellY = 0;
    TileMapTileInfo info{};
};

[[nodiscard]] Core::Result<Core::u32> collectChunkNonEmptyCells(const TileMapInstance& map, TileMapChunkCoord coord,
                                                               std::pmr::vector<TileChunkCell>& out);

} // namespace Tina::Asset
