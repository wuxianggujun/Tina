#include <tina/asset/TileMapPhysicsSync.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/physics2d/PhysicsErrors.hpp>

#include <cmath>

namespace Tina::Asset {
namespace {

[[nodiscard]] bool isSolidMaterial(Core::u16 flags) noexcept
{
    return (flags & AssetFormat::TilesetWire::MaterialSolid) != 0;
}

} // namespace

Core::Result<Physics2D::PhysicsQueryWriteResult2D> collectSolidCellsForPhysics(
    const IGridCollisionProvider& grid,
    std::span<Physics2D::PhysicsGridSolidCell2D> outCells)
{
    const Core::u32 width = grid.widthCells();
    const Core::u32 height = grid.heightCells();
    if (width == 0 || height == 0 || !std::isfinite(grid.cellSizeMeters()) || grid.cellSizeMeters() <= 0.0F) {
        return Core::failure(
            AssetErrorCode::InvalidCatalogConfig,
            "grid collision map dimensions or cell size are invalid");
    }

    Physics2D::PhysicsQueryWriteResult2D result{};
    for (Core::u32 y = 0; y < height; ++y) {
        for (Core::u32 x = 0; x < width; ++x) {
            if (!isSolidMaterial(grid.materialFlagsAt(x, y))) {
                continue;
            }
            ++result.totalFound;
            if (result.written < outCells.size()) {
                outCells[result.written++] = Physics2D::PhysicsGridSolidCell2D{x, y};
            } else {
                result.overflow = true;
            }
        }
    }
    return result;
}

Core::Result<Physics2D::PhysicsQueryWriteResult2D> collectAllSolidCellsForPhysics(
    const IGridCollisionProvider& grid,
    std::span<Physics2D::PhysicsGridSolidCell2D> outCells)
{
    return collectSolidCellsForPhysics(grid, outCells);
}

Core::Result<Physics2D::PhysicsQueryWriteResult2D> syncTileMapSolidsToStaticBodies(
    const IGridCollisionProvider& grid,
    Physics2D::PhysicsWorld2D& world,
    const Physics2D::PhysicsGridBodySyncConfig2D& config,
    std::span<Physics2D::PhysicsBodyId> outBodies,
    std::span<Physics2D::PhysicsGridSolidCell2D> solidScratch)
{
    auto collected = collectSolidCellsForPhysics(grid, solidScratch);
    if (!collected) {
        return Core::failure(collected.error());
    }
    if (collected->overflow) {
        return Core::failure(
            Physics2D::Physics2DErrorCode::CapacityExceeded,
            "solid cell scratch buffer is smaller than the solid cell count");
    }

    Physics2D::PhysicsGridBodySyncConfig2D syncConfig = config;
    if (syncConfig.cellSizeMeters <= 0.0F || !std::isfinite(syncConfig.cellSizeMeters)) {
        syncConfig.cellSizeMeters = grid.cellSizeMeters();
    }

    const auto cells = solidScratch.subspan(0, collected->written);
    return Physics2D::createStaticBodiesForSolidCells(world, cells, syncConfig, outBodies);
}

} // namespace Tina::Asset
