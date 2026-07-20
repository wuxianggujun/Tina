#pragma once

#include <tina/asset/GridCollision.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/physics2d/PhysicsGridBodies.hpp>
#include <tina/physics2d/PhysicsWorld2D.hpp>

#include <span>

namespace Tina::Asset {

// Collects MaterialSolid cells from a grid provider into Physics2D solid-cell slots.
// Physics2D stays free of Asset types; this Game2D-side bridge owns the conversion.
// outCells receives up to capacity cells in row-major scan order; overflow is reported.
[[nodiscard]] Core::Result<Physics2D::PhysicsQueryWriteResult2D> collectSolidCellsForPhysics(
    const IGridCollisionProvider& grid,
    std::span<Physics2D::PhysicsGridSolidCell2D> outCells);

// Full-map solid AABB (entire grid extents). Same overflow semantics as collectSolidCellsForPhysics.
[[nodiscard]] Core::Result<Physics2D::PhysicsQueryWriteResult2D> collectAllSolidCellsForPhysics(
    const IGridCollisionProvider& grid,
    std::span<Physics2D::PhysicsGridSolidCell2D> outCells);

// Convenience: collect all solid cells then createStaticBodiesForSolidCells (all-or-nothing).
// outBodies receives PhysicsBodyId for created static boxes (optional capacity).
[[nodiscard]] Core::Result<Physics2D::PhysicsQueryWriteResult2D> syncTileMapSolidsToStaticBodies(
    const IGridCollisionProvider& grid,
    Physics2D::PhysicsWorld2D& world,
    const Physics2D::PhysicsGridBodySyncConfig2D& config,
    std::span<Physics2D::PhysicsBodyId> outBodies,
    std::span<Physics2D::PhysicsGridSolidCell2D> solidScratch);

} // namespace Tina::Asset
