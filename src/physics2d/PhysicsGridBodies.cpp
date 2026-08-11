#include <tina/physics2d/PhysicsGridBodies.hpp>

#include <tina/physics2d/PhysicsErrors.hpp>

#include <cmath>
#include <limits>

namespace Tina::Physics2D {
namespace {

[[nodiscard]] bool isFinitePositive(float value) noexcept
{
    return std::isfinite(value) && value > 0.0F;
}

[[nodiscard]] bool validMaterial(const PhysicsGridColliderMaterial2D& material) noexcept
{
    return material.filter.categoryBits != 0
        && std::isfinite(material.density) && material.density >= 0.0F
        && std::isfinite(material.friction) && material.friction >= 0.0F && material.friction <= 1.0F
        && std::isfinite(material.restitution) && material.restitution >= 0.0F
        && material.restitution <= 1.0F;
}

[[nodiscard]] bool validRectangle(
    const PhysicsGridSolidRect2D& rectangle,
    float cellSizeMeters) noexcept
{
    if (rectangle.widthCells == 0 || rectangle.heightCells == 0) {
        return false;
    }

    const Core::u64 right = static_cast<Core::u64>(rectangle.cellX) + rectangle.widthCells;
    const Core::u64 top = static_cast<Core::u64>(rectangle.cellY) + rectangle.heightCells;
    if (right > (std::numeric_limits<Core::u32>::max)()
        || top > (std::numeric_limits<Core::u32>::max)()) {
        return false;
    }

    const double halfWidth = 0.5 * static_cast<double>(rectangle.widthCells) * cellSizeMeters;
    const double halfHeight = 0.5 * static_cast<double>(rectangle.heightCells) * cellSizeMeters;
    const double centerX = (static_cast<double>(rectangle.cellX)
                            + 0.5 * static_cast<double>(rectangle.widthCells)) * cellSizeMeters;
    const double centerY = (static_cast<double>(rectangle.cellY)
                            + 0.5 * static_cast<double>(rectangle.heightCells)) * cellSizeMeters;
    return std::isfinite(halfWidth) && std::isfinite(halfHeight)
        && std::isfinite(centerX) && std::isfinite(centerY)
        && halfWidth > 0.0 && halfHeight > 0.0
        && halfWidth <= (std::numeric_limits<float>::max)()
        && halfHeight <= (std::numeric_limits<float>::max)()
        && centerX <= (std::numeric_limits<float>::max)()
        && centerY <= (std::numeric_limits<float>::max)();
}

} // namespace

Core::Result<PhysicsGridBodyCreateResult2D> createStaticBodyForSolidRectangles(
    PhysicsWorld2D& world,
    std::span<const PhysicsGridSolidRect2D> solidRectangles,
    float cellSizeMeters,
    const PhysicsGridColliderMaterial2D& material)
{
    if (!world.isOpen()) {
        return Core::failure(Physics2DErrorCode::WorldClosed, "Physics2D world is closed");
    }
    if (!isFinitePositive(cellSizeMeters) || !validMaterial(material)) {
        return Core::failure(
            Physics2DErrorCode::InvalidConfiguration,
            "Physics2D grid collider material or cell size is invalid");
    }
    for (const PhysicsGridSolidRect2D& rectangle : solidRectangles) {
        if (!validRectangle(rectangle, cellSizeMeters)) {
            return Core::failure(
                Physics2DErrorCode::InvalidShapeDescription,
                "Physics2D grid collider contains an invalid rectangle");
        }
    }

    PhysicsGridBodyCreateResult2D result{};
    if (solidRectangles.empty()) {
        return result;
    }

    PhysicsBody2DDesc bodyDesc;
    bodyDesc.type = PhysicsBodyType2D::Static;
    bodyDesc.initiallyAwake = false;
    bodyDesc.enableSleep = true;
    auto body = world.createBody(bodyDesc);
    if (!body) {
        return Core::failure(body.error());
    }

    for (const PhysicsGridSolidRect2D& rectangle : solidRectangles) {
        PhysicsShape2DDesc shape;
        shape.kind = PhysicsShapeKind2D::Box;
        shape.halfExtentsMeters = {
            static_cast<float>(0.5 * static_cast<double>(rectangle.widthCells) * cellSizeMeters),
            static_cast<float>(0.5 * static_cast<double>(rectangle.heightCells) * cellSizeMeters)};
        shape.localCenterMeters = {
            static_cast<float>((static_cast<double>(rectangle.cellX)
                                + 0.5 * static_cast<double>(rectangle.widthCells)) * cellSizeMeters),
            static_cast<float>((static_cast<double>(rectangle.cellY)
                                + 0.5 * static_cast<double>(rectangle.heightCells)) * cellSizeMeters)};
        shape.density = material.density;
        shape.friction = material.friction;
        shape.restitution = material.restitution;
        shape.enableContactEvents = material.enableContactEvents;
        shape.filter = material.filter;

        auto createdShape = world.createShape(*body, shape);
        if (!createdShape) {
            (void)world.destroyBody(*body);
            return Core::failure(createdShape.error());
        }
        ++result.shapeCount;
    }

    result.body = *body;
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
