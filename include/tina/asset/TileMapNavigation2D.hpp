#pragma once

#include <tina/asset/TileMapInstance.hpp>
#include <tina/navigation2d/NavigationGrid2D.hpp>

#include <memory_resource>
#include <string_view>

namespace Tina::Asset {

struct TileMapNavigation2DDataBuildConfig final {
    AssetFormat::TileMapLayerId solidTileLayerId = 0;
    // Optional object layer. Only visible Rectangle objects whose property exactly
    // matches blockerPropertyKey/value are rasterized into blocked cells.
    AssetFormat::TileMapLayerId blockerObjectLayerId = 0;
    std::string_view blockerPropertyKey = "navigation";
    std::string_view blockerPropertyValue = "blocked";
};

struct TileMapNavigation2DDataBuildStats final {
    Core::usize solidTileCells = 0;
    Core::usize blockerRectangles = 0;
    Core::usize blockedCells = 0;
};

struct TileMapNavigation2DDataBuildResult final {
    Navigation2D::NavigationGrid2DData data;
    TileMapNavigation2DDataBuildStats stats{};
};

// Builds immutable schema-v1 navigation data from the current resident TileMap
// snapshot. Referenced non-resident chunks fail atomically; no partial data is returned.
[[nodiscard]] Core::Result<TileMapNavigation2DDataBuildResult>
buildTileMapNavigation2DData(
    const TileMapInstance& map, const TileMapNavigation2DDataBuildConfig& config,
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

} // namespace Tina::Asset
