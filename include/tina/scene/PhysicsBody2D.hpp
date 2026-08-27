#pragma once

#include <tina/core/base/Types.hpp>

#include <cmath>

namespace Tina::Scene {

// Which body the authored node asked for. The wire format encodes this in the
// node kind rather than the payload, so the component has to carry it or capture
// could not reproduce the authored node kind.
enum class PhysicsBodyKind2D : u8 {
    Static = 0,
    Rigid = 1,
    Character = 2,
    Area = 3,
};

// Scene-owned 2D physics body description. This is authored data only: it holds
// no PhysicsBodyId and no backend handle, so Scene stays independent of any
// physics backend (ADR 0010) while still round-tripping what the Editor wrote.
// Turning this into a live body is the bridge's job (ADR 0030).
struct PhysicsBody2D final {
    PhysicsBodyKind2D kind = PhysicsBodyKind2D::Static;
    float linearVelocityX = 0.0F;
    float linearVelocityY = 0.0F;
    float angularVelocityRadiansPerSecond = 0.0F;
    float linearDamping = 0.0F;
    float angularDamping = 0.0F;
    float gravityScale = 1.0F;
    bool enabled = true;
    bool enableSleep = true;
    bool initiallyAwake = true;
    bool fixedRotation = false;
    bool continuousCollision = false;

    friend constexpr bool operator==(const PhysicsBody2D&, const PhysicsBody2D&) noexcept = default;
};

[[nodiscard]] constexpr bool isValidPhysicsBodyKind2D(PhysicsBodyKind2D kind) noexcept
{
    return kind == PhysicsBodyKind2D::Static || kind == PhysicsBodyKind2D::Rigid ||
           kind == PhysicsBodyKind2D::Character || kind == PhysicsBodyKind2D::Area;
}

// Only what Scene can judge: finite values and non-negative damping. Whether the
// values are physically sensible is Physics2D's call.
[[nodiscard]] inline bool isValid(const PhysicsBody2D& body) noexcept
{
    if (!isValidPhysicsBodyKind2D(body.kind)) {
        return false;
    }
    if (!std::isfinite(body.linearVelocityX) || !std::isfinite(body.linearVelocityY) ||
        !std::isfinite(body.angularVelocityRadiansPerSecond) ||
        !std::isfinite(body.gravityScale)) {
        return false;
    }
    return std::isfinite(body.linearDamping) && body.linearDamping >= 0.0F &&
           std::isfinite(body.angularDamping) && body.angularDamping >= 0.0F;
}

} // namespace Tina::Scene
