#include <tina/physics2d/PhysicsGridBodies.hpp>

#include <tina/physics2d/PhysicsErrors.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Tina::Physics2D {
namespace {

[[nodiscard]] bool isFinitePositive(float value) noexcept
{
    return std::isfinite(value) && value > 0.0F;
}

} // namespace

Core::Result<PhysicsQueryWriteResult2D> createStaticBodiesForSolidCells(
    PhysicsWorld2D& world,
    std::span<const PhysicsGridSolidCell2D> solidCells,
    const PhysicsGridBodySyncConfig2D& config,
    std::span<PhysicsBodyId> outBodies)
{
    if (!world.isOpen()) {
        return Core::failure(
            Physics2DErrorCode::WorldClosed,
            "Physics2D world is closed");
    }
    if (!isFinitePositive(config.cellSizeMeters)
        || !std::isfinite(config.density)
        || config.density < 0.0F
        || !std::isfinite(config.friction)
        || config.friction < 0.0F
        || config.friction > 1.0F
        || !std::isfinite(config.restitution)
        || config.restitution < 0.0F
        || config.restitution > 1.0F
        || config.filter.categoryBits == 0) {
        return Core::failure(
            Physics2DErrorCode::InvalidConfiguration,
            "Physics2D grid body sync config is invalid");
    }

    PhysicsQueryWriteResult2D result{};
    result.totalFound = solidCells.size();
    if (solidCells.empty()) {
        return result;
    }

    // Track created bodies for all-or-nothing rollback without heap growth when
    // outBodies already has space; otherwise use a temporary vector on the default
    // resource only for rollback bookkeeping of this call.
    std::vector<PhysicsBodyId> created;
    created.reserve(solidCells.size());

    const float half = 0.5F * config.cellSizeMeters;
    PhysicsBody2DDesc bodyDesc;
    bodyDesc.type = PhysicsBodyType2D::Static;
    bodyDesc.initiallyAwake = false;
    bodyDesc.enableSleep = true;

    PhysicsShape2DDesc shapeDesc;
    shapeDesc.kind = PhysicsShapeKind2D::Box;
    shapeDesc.halfExtentsMeters = {half, half};
    shapeDesc.density = config.density;
    shapeDesc.friction = config.friction;
    shapeDesc.restitution = config.restitution;
    shapeDesc.enableContactEvents = config.enableContactEvents;
    shapeDesc.enableHitEvents = false;
    shapeDesc.filter = config.filter;

    for (const PhysicsGridSolidCell2D& cell : solidCells) {
        bodyDesc.positionMeters = {
            (static_cast<float>(cell.cellX) + 0.5F) * config.cellSizeMeters,
            (static_cast<float>(cell.cellY) + 0.5F) * config.cellSizeMeters};
        auto createdBody = world.createBody(bodyDesc);
        if (!createdBody) {
            for (const PhysicsBodyId id : created) {
                (void)world.destroyBody(id);
            }
            return Core::failure(createdBody.error());
        }
        auto createdShape = world.createShape(*createdBody, shapeDesc);
        if (!createdShape) {
            (void)world.destroyBody(*createdBody);
            for (const PhysicsBodyId id : created) {
                (void)world.destroyBody(id);
            }
            return Core::failure(createdShape.error());
        }
        created.push_back(*createdBody);
    }

    const Core::usize writeCount =
        (std::min)(created.size(), outBodies.size());
    for (Core::usize index = 0; index < writeCount; ++index) {
        outBodies[index] = created[index];
    }
    result.written = writeCount;
    result.overflow = created.size() > outBodies.size();
    return result;
}

Core::Result<PhysicsQueryWriteResult2D> destroyBodies(
    PhysicsWorld2D& world,
    std::span<const PhysicsBodyId> bodies)
{
    if (!world.isOpen()) {
        return Core::failure(
            Physics2DErrorCode::WorldClosed,
            "Physics2D world is closed");
    }

    PhysicsQueryWriteResult2D result{};
    result.totalFound = bodies.size();
    for (const PhysicsBodyId body : bodies) {
        if (!body.hasValue() || !world.contains(body)) {
            continue;
        }
        if (world.destroyBody(body)) {
            ++result.written;
        }
    }
    return result;
}

} // namespace Tina::Physics2D
