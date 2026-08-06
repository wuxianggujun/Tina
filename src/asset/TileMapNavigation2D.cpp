#include <tina/asset/TileMapNavigation2D.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/navigation2d/NavigationErrors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::Status rasterizeBlockerRectangle(
    const AssetFormat::TileMapObjectPayloadView& object, Core::u32 widthCells,
    Core::u32 heightCells, float cellSizeMeters, std::pmr::vector<Core::u8>& flags)
{
    if (object.kind != AssetFormat::TileMapObjectKind::Rectangle)
    {
        return Core::failure(Navigation2D::NavigationErrorCode::InvalidData,
                             "tilemap navigation blocker property requires a rectangle object");
    }
    if (!std::isfinite(object.x) || !std::isfinite(object.y) ||
        !std::isfinite(object.width) || !std::isfinite(object.height) ||
        !(object.width > 0.0F) || !(object.height > 0.0F))
    {
        return Core::failure(Navigation2D::NavigationErrorCode::InvalidData,
                             "tilemap navigation blocker rectangle geometry is invalid");
    }

    const double minX = static_cast<double>(object.x);
    const double minY = static_cast<double>(object.y);
    const double maxX = minX + static_cast<double>(object.width);
    const double maxY = minY + static_cast<double>(object.height);
    const double mapMaxX = static_cast<double>(widthCells) * cellSizeMeters;
    const double mapMaxY = static_cast<double>(heightCells) * cellSizeMeters;
    if (!std::isfinite(maxX) || !std::isfinite(maxY))
    {
        return Core::failure(Navigation2D::NavigationErrorCode::InvalidData,
                             "tilemap navigation blocker rectangle extent overflowed");
    }
    if (maxX <= 0.0 || maxY <= 0.0 || minX >= mapMaxX || minY >= mapMaxY)
    {
        return Core::success();
    }

    const double clippedMinX = (std::max)(0.0, minX);
    const double clippedMinY = (std::max)(0.0, minY);
    const double clippedMaxX = (std::min)(mapMaxX, maxX);
    const double clippedMaxY = (std::min)(mapMaxY, maxY);
    const double inverseCellSize = 1.0 / static_cast<double>(cellSizeMeters);
    const auto cellForMinimum = [inverseCellSize](double world) -> Core::u32 {
        return static_cast<Core::u32>(std::floor(world * inverseCellSize));
    };
    const auto cellForMaximum = [inverseCellSize](double world) -> Core::u32 {
        return static_cast<Core::u32>(
            std::floor(std::nextafter(world, -std::numeric_limits<double>::infinity()) * inverseCellSize));
    };

    const Core::u32 minCellX = (std::min)(cellForMinimum(clippedMinX), widthCells - 1U);
    const Core::u32 minCellY = (std::min)(cellForMinimum(clippedMinY), heightCells - 1U);
    const Core::u32 maxCellX = (std::min)(cellForMaximum(clippedMaxX), widthCells - 1U);
    const Core::u32 maxCellY = (std::min)(cellForMaximum(clippedMaxY), heightCells - 1U);
    for (Core::u32 y = minCellY; y <= maxCellY; ++y)
    {
        for (Core::u32 x = minCellX; x <= maxCellX; ++x)
        {
            flags[static_cast<Core::usize>(y) * widthCells + x] |=
                Navigation2D::NavigationGrid2DSchema::CellBlocked;
        }
    }
    return Core::success();
}

} // namespace

Core::Result<TileMapNavigation2DDataBuildResult> buildTileMapNavigation2DData(
    const TileMapInstance& map, const TileMapNavigation2DDataBuildConfig& config,
    std::pmr::memory_resource& resource)
{
    if (!map || config.solidTileLayerId == 0U)
    {
        return Core::failure(Navigation2D::NavigationErrorCode::InvalidData,
                             "tilemap navigation build requires a valid map and solid tile layer id");
    }
    auto tileLayer = map.layer(config.solidTileLayerId);
    if (!tileLayer)
    {
        return Core::failure(std::move(tileLayer.error()));
    }
    if (tileLayer->kind != AssetFormat::TileMapLayerKind::Tile)
    {
        return Core::failure(AssetErrorCode::TileMapLayerTypeMismatch,
                             "tilemap navigation solid layer is not a tile layer");
    }
    if (config.blockerObjectLayerId != 0U &&
        (config.blockerPropertyKey.empty() || config.blockerPropertyValue.empty()))
    {
        return Core::failure(Navigation2D::NavigationErrorCode::InvalidData,
                             "tilemap navigation object blocker property match must be non-empty");
    }

    try
    {
        const Core::usize cellCount =
            static_cast<Core::usize>(map.widthCells()) * map.heightCells();
        std::pmr::vector<Core::u8> flags{&resource};
        flags.resize(cellCount, Core::u8{0});
        TileMapNavigation2DDataBuildStats stats{};

        for (Core::u32 y = 0; y < map.heightCells(); ++y)
        {
            for (Core::u32 x = 0; x < map.widthCells(); ++x)
            {
                auto info = map.tileInfoAt(config.solidTileLayerId, x, y);
                if (!info)
                {
                    return Core::failure(std::move(info.error()).withContext(
                        "buildTileMapNavigation2DData", "solid tile layer"));
                }
                if (*info && !(*info)->empty &&
                    (((*info)->materialFlags & AssetFormat::TilesetWire::MaterialSolid) != 0U))
                {
                    flags[static_cast<Core::usize>(y) * map.widthCells() + x] |=
                        Navigation2D::NavigationGrid2DSchema::CellBlocked;
                    ++stats.solidTileCells;
                }
            }
        }

        if (config.blockerObjectLayerId != 0U)
        {
            auto objectLayer = map.layer(config.blockerObjectLayerId);
            if (!objectLayer)
            {
                return Core::failure(std::move(objectLayer.error()));
            }
            if (objectLayer->kind != AssetFormat::TileMapLayerKind::Object)
            {
                return Core::failure(AssetErrorCode::TileMapLayerTypeMismatch,
                                     "tilemap navigation blocker layer is not an object layer");
            }
            for (Core::u32 index = 0; index < objectLayer->objectCount; ++index)
            {
                const auto object = objectLayer->objectAt(index);
                if (!object)
                {
                    return Core::failure(Navigation2D::NavigationErrorCode::InvalidData,
                                         "tilemap navigation object view is missing after validation");
                }
                if (!object->visible)
                {
                    continue;
                }
                const auto property = object->findProperty(config.blockerPropertyKey);
                if (!property || property->value != config.blockerPropertyValue)
                {
                    continue;
                }
                if (Core::Status status = rasterizeBlockerRectangle(
                        *object, map.widthCells(), map.heightCells(), map.cellSizeMeters(), flags);
                    !status)
                {
                    return Core::failure(std::move(status.error()).withContext(
                        "buildTileMapNavigation2DData", "blocker object rectangle"));
                }
                ++stats.blockerRectangles;
            }
        }

        stats.blockedCells = static_cast<Core::usize>(std::count_if(
            flags.begin(), flags.end(), [](Core::u8 value) {
                return (value & Navigation2D::NavigationGrid2DSchema::CellBlocked) != 0U;
            }));
        auto data = Navigation2D::NavigationGrid2DData::Create(
            Navigation2D::NavigationGrid2DDataDesc{
                .widthCells = map.widthCells(),
                .heightCells = map.heightCells(),
                .cellSizeMeters = map.cellSizeMeters(),
                .cellFlags = flags,
            },
            resource);
        if (!data)
        {
            return Core::failure(std::move(data.error()).withContext(
                "buildTileMapNavigation2DData", "schema-v1 data publication"));
        }
        return TileMapNavigation2DDataBuildResult{.data = std::move(*data), .stats = stats};
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Navigation2D::NavigationErrorCode::AllocationFailed,
                             "tilemap navigation build allocation failed");
    }
}

} // namespace Tina::Asset
