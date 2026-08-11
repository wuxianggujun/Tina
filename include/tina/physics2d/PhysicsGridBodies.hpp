#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/physics2d/PhysicsTypes.hpp>
#include <tina/physics2d/PhysicsWorld2D.hpp>

#include <span>

namespace Tina::Physics2D {

// A maximal axis-aligned solid rectangle in grid-cell coordinates. The rectangle
// origin is the bottom-left cell and its extents are strictly positive.
struct PhysicsGridSolidRect2D final {
    Core::u32 cellX = 0;
    Core::u32 cellY = 0;
    Core::u32 widthCells = 1;
    Core::u32 heightCells = 1;

    friend constexpr bool operator==(
        const PhysicsGridSolidRect2D&,
        const PhysicsGridSolidRect2D&) noexcept = default;
};

struct PhysicsGridColliderMaterial2D final {
    PhysicsCollisionFilter2D filter{};
    bool enableContactEvents = false;
    float density = 0.0F;
    float friction = 0.6F;
    float restitution = 0.0F;
};

struct PhysicsGridBodyCreateResult2D final {
    PhysicsBodyId body{};
    Core::usize shapeCount = 0;
};

// Creates one static body containing one box shape per solid rectangle. The
// operation is atomic: a failed shape creation destroys the body and every shape
// created by this call. No Asset or TileMap type crosses the Physics2D boundary.
// An empty rectangle span is a successful no-op with an empty body id.
[[nodiscard]] Core::Result<PhysicsGridBodyCreateResult2D>
createStaticBodyForSolidRectangles(
    PhysicsWorld2D& world,
    std::span<const PhysicsGridSolidRect2D> solidRectangles,
    float cellSizeMeters,
    const PhysicsGridColliderMaterial2D& material);

// Destroys every live body id in the span. Stale/invalid ids are skipped and
// counted in totalFound while written tracks successful destroys.
[[nodiscard]] Core::Result<PhysicsQueryWriteResult2D> destroyBodies(
    PhysicsWorld2D& world,
    std::span<const PhysicsBodyId> bodies);

} // namespace Tina::Physics2D
