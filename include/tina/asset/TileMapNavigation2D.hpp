#pragma once

#include <tina/asset/TileMapInstance.hpp>
#include <tina/navigation2d/NavigationGrid2D.hpp>

#include <memory_resource>
#include <span>
#include <string_view>

namespace Tina::Asset {

struct TileMapNavigation2DMaterialCostRule final {
    // Exact Tileset materialFlags match. Zero is reserved for the default cost.
    Core::u16 materialFlags = 0;
    Core::u8 traversalCost = Navigation2D::NavigationGrid2DContract::MinimumTraversalCost;
};

struct TileMapNavigation2DDataBuildConfig final {
    AssetFormat::TileMapLayerId solidTileLayerId = 0;
    // Optional object layer. Only visible Rectangle objects whose property exactly
    // matches blockerPropertyKey/value are rasterized into blocked cells.
    AssetFormat::TileMapLayerId blockerObjectLayerId = 0;
    std::string_view blockerPropertyKey = "navigation";
    std::string_view blockerPropertyValue = "blocked";
    // Optional exact material flag -> traversal multiplier rules. Rules must be
    // unique and costs must fit the Navigation2D grid contract.
    std::span<const TileMapNavigation2DMaterialCostRule> materialCostRules{};
};

struct TileMapNavigation2DDataBuildStats final {
    Core::usize solidTileCells = 0;
    Core::usize blockerRectangles = 0;
    Core::usize blockedCells = 0;
    // Cells whose published traversal multiplier is greater than one. Blocked
    // cells remain part of the grid data and are included in these cost stats.
    Core::usize weightedCells = 0;
    Core::u8 maximumTraversalCost = Navigation2D::NavigationGrid2DContract::MinimumTraversalCost;
};

struct TileMapNavigation2DDataBuildResult final {
    Navigation2D::NavigationGrid2DData data;
    TileMapNavigation2DDataBuildStats stats{};
};

// Builds immutable navigation grid data from the current resident TileMap
// snapshot. Referenced non-resident chunks fail atomically; no partial data is returned.
[[nodiscard]] Core::Result<TileMapNavigation2DDataBuildResult>
buildTileMapNavigation2DData(
    const TileMapInstance& map, const TileMapNavigation2DDataBuildConfig& config,
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

} // namespace Tina::Asset
