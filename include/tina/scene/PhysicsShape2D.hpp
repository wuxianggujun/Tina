#pragma once

#include <tina/core/base/Types.hpp>

#include <cmath>

namespace Tina::Scene {

// Only the kinds that have an authored wire representation. Physics2D also
// supports ConvexPolygon and Chain; a game needing those builds them directly
// against the physics API instead of authoring them (ADR 0030).
enum class PhysicsShapeKind2D : u8 {
    Box = 0,
    Circle = 1,
    Capsule = 2,
};

// Scene-owned 2D collision shape description, attached to the entity that owns
// the shape. Ownership by a body follows the node hierarchy: the Editor requires
// a CollisionShape2D to have a physics-body parent, and the bridge resolves the
// body from that parent rather than from a handle stored here.
struct PhysicsShape2D final {
    PhysicsShapeKind2D kind = PhysicsShapeKind2D::Box;
    float halfExtentX = 0.5F;
    float halfExtentY = 0.5F;
    float radius = 0.5F;
    float localCenterX = 0.0F;
    float localCenterY = 0.0F;
    float localAngleRadians = 0.0F;
    float localPointAX = -0.5F;
    float localPointAY = 0.0F;
    float localPointBX = 0.5F;
    float localPointBY = 0.0F;
    float density = 1.0F;
    float friction = 0.6F;
    float restitution = 0.0F;
    bool enabled = true;
    bool sensor = false;
    bool sensorEvents = false;
    bool contactEvents = true;
    bool hitEvents = false;

    friend constexpr bool operator==(const PhysicsShape2D&, const PhysicsShape2D&) noexcept = default;
};

[[nodiscard]] constexpr bool isValidPhysicsShapeKind2D(PhysicsShapeKind2D kind) noexcept
{
    return kind == PhysicsShapeKind2D::Box || kind == PhysicsShapeKind2D::Circle ||
           kind == PhysicsShapeKind2D::Capsule;
}

// Dimensions are checked per kind, matching the Editor-side rule: a Box needs
// positive half extents, a Circle or Capsule needs a positive radius.
[[nodiscard]] inline bool isValid(const PhysicsShape2D& shape) noexcept
{
    if (!isValidPhysicsShapeKind2D(shape.kind)) {
        return false;
    }
    if (!std::isfinite(shape.halfExtentX) || !std::isfinite(shape.halfExtentY) ||
        !std::isfinite(shape.radius) || !std::isfinite(shape.localCenterX) ||
        !std::isfinite(shape.localCenterY) || !std::isfinite(shape.localAngleRadians) ||
        !std::isfinite(shape.localPointAX) || !std::isfinite(shape.localPointAY) ||
        !std::isfinite(shape.localPointBX) || !std::isfinite(shape.localPointBY)) {
        return false;
    }
    if (!std::isfinite(shape.density) || shape.density < 0.0F ||
        !std::isfinite(shape.friction) || shape.friction < 0.0F ||
        !std::isfinite(shape.restitution) || shape.restitution < 0.0F) {
        return false;
    }
    if (shape.kind == PhysicsShapeKind2D::Box) {
        return shape.halfExtentX > 0.0F && shape.halfExtentY > 0.0F;
    }
    return shape.radius > 0.0F;
}

} // namespace Tina::Scene
