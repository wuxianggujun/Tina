#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/physics2d/PhysicsTypes.hpp>
#include <tina/physics2d/PhysicsWorld2D.hpp>

#include <span>

namespace Tina::Physics2D {

// One solid grid cell in map-local meters with origin at cell (0,0) bottom-left.
// Center of cell (x,y) is ((x + 0.5) * cellSize, (y + 0.5) * cellSize).
struct PhysicsGridSolidCell2D final {
    Core::u32 cellX = 0;
    Core::u32 cellY = 0;

    friend constexpr bool operator==(
        const PhysicsGridSolidCell2D&,
        const PhysicsGridSolidCell2D&) noexcept = default;
};

struct PhysicsGridBodySyncConfig2D final {
    float cellSizeMeters = 1.0F;
    PhysicsCollisionFilter2D filter{};
    bool enableContactEvents = false;
    // When false, density is 0 (static massless fixtures). Static bodies ignore density
    // for dynamics but keep a finite non-negative value for Box2D validation.
    float density = 0.0F;
    float friction = 0.6F;
    float restitution = 0.0F;
};

// Creates one static box body per solid cell. All-or-nothing: any failure destroys
// bodies created in this call and does not leave half-synced geometry. Writes body
// IDs into outBodies when capacity allows; always reports total cells attempted.
// Does not depend on Asset/TileMap; callers collect solid cells from IGridCollisionProvider.
[[nodiscard]] Core::Result<PhysicsQueryWriteResult2D> createStaticBodiesForSolidCells(
    PhysicsWorld2D& world,
    std::span<const PhysicsGridSolidCell2D> solidCells,
    const PhysicsGridBodySyncConfig2D& config,
    std::span<PhysicsBodyId> outBodies);

// Destroys every live body id in the span. Stale/invalid ids are skipped and counted
// in totalFound while written tracks successful destroys. Continues after skips.
[[nodiscard]] Core::Result<PhysicsQueryWriteResult2D> destroyBodies(
    PhysicsWorld2D& world,
    std::span<const PhysicsBodyId> bodies);

} // namespace Tina::Physics2D
