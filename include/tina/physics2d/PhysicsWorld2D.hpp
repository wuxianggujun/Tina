#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/math/Vec.hpp>
#include <tina/physics2d/PhysicsErrors.hpp>
#include <tina/physics2d/PhysicsTypes.hpp>

#include <memory_resource>
#include <span>

namespace Tina::Physics2D {

[[nodiscard]] Core::Status validatePhysicsWorld2DConfig(
    const PhysicsWorld2DConfig& config) noexcept;
[[nodiscard]] Core::Status validatePhysicsBody2DDesc(
    const PhysicsBody2DDesc& desc) noexcept;
[[nodiscard]] Core::Status validatePhysicsShape2DDesc(
    const PhysicsShape2DDesc& desc) noexcept;
[[nodiscard]] Core::Status validatePhysicsJoint2DDesc(
    const PhysicsJoint2DDesc& desc) noexcept;
[[nodiscard]] Core::Status validatePhysicsQueryFilter2D(
    const PhysicsQueryFilter2D& filter) noexcept;
[[nodiscard]] Core::Status validatePhysicsAabb2D(const PhysicsAabb2D& aabb) noexcept;
[[nodiscard]] Core::Status validatePhysicsRayCast2D(const PhysicsRayCast2D& ray) noexcept;

// Single-owner, fixed-step 2D physics world. Bodies, shapes, and joints have
// independent generation handles. Shapes are backend-neutral Box/Circle/Capsule/
// ConvexPolygon/Chain descriptors; every body may own multiple shapes. Chain is a
// static-body-only, non-sensor shape whose backend segments share one public handle.
// Joints are backend-neutral Distance/Revolute/Prismatic descriptors.
class PhysicsWorld2D final {
public:
    [[nodiscard]] static Core::Result<PhysicsWorld2D> Create(
        PhysicsWorld2DConfig config = {},
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~PhysicsWorld2D() noexcept;

    PhysicsWorld2D(const PhysicsWorld2D&) = delete;
    PhysicsWorld2D& operator=(const PhysicsWorld2D&) = delete;
    PhysicsWorld2D(PhysicsWorld2D&& other) noexcept;
    PhysicsWorld2D& operator=(PhysicsWorld2D&&) = delete;

    [[nodiscard]] Core::Result<PhysicsBodyId> createBody(const PhysicsBody2DDesc& body);
    [[nodiscard]] Core::Result<PhysicsShapeId> createShape(
        PhysicsBodyId body,
        const PhysicsShape2DDesc& shape);
    [[nodiscard]] Core::Status destroyShape(PhysicsShapeId shape) noexcept;
    [[nodiscard]] Core::Status destroyBody(PhysicsBodyId body) noexcept;
    [[nodiscard]] Core::Result<PhysicsJointId> createJoint(const PhysicsJoint2DDesc& joint);
    [[nodiscard]] Core::Status destroyJoint(PhysicsJointId joint) noexcept;

    // FIFO deferred mutations. Capacity is fixed at Create; full queue returns
    // CapacityExceeded without dropping earlier commands. Stale body targets are
    // skipped at apply time and counted in stats.
    [[nodiscard]] Core::Status enqueueDestroyBody(PhysicsBodyId body) noexcept;
    [[nodiscard]] Core::Status enqueueSetTransform(
        PhysicsBodyId body,
        Math::Vec2 positionMeters,
        float angleRadians) noexcept;
    [[nodiscard]] Core::Status enqueueSetLinearVelocity(
        PhysicsBodyId body,
        Math::Vec2 linearVelocityMetersPerSecond) noexcept;
    [[nodiscard]] Core::Status enqueueSetAngularVelocity(
        PhysicsBodyId body,
        float angularVelocityRadiansPerSecond) noexcept;
    [[nodiscard]] Core::Status enqueueApplyForceToCenter(
        PhysicsBodyId body,
        Math::Vec2 forceNewtons,
        bool wake = true) noexcept;
    [[nodiscard]] Core::Status enqueueApplyLinearImpulseToCenter(
        PhysicsBodyId body,
        Math::Vec2 impulseNewtonSeconds,
        bool wake = true) noexcept;
    [[nodiscard]] Core::Status enqueueSetEnabled(PhysicsBodyId body, bool enabled) noexcept;
    [[nodiscard]] Core::Status enqueueSetAwake(PhysicsBodyId body, bool awake) noexcept;
    [[nodiscard]] Core::Status clearCommands() noexcept;
    [[nodiscard]] Core::usize pendingCommandCount() const noexcept;

    // Advances exactly config.fixedDeltaSeconds. Runtime owns accumulator and
    // catch-up policy; this world never accepts a variable frame delta.
    // Flushes deferred commands first, then steps, then copies contact events.
    [[nodiscard]] Core::Status step() noexcept;

    // Borrowed until the next successful step(), shutdown(), move, or destroy.
    [[nodiscard]] Core::Result<PhysicsContactEvents2DView> contactEvents() const noexcept;

    // Precise box-proxy overlap. Hits are sorted by body index then shape index.
    // out may be empty; overflow is reported without heap growth.
    [[nodiscard]] Core::Result<PhysicsQueryWriteResult2D> overlapAabb(
        const PhysicsAabb2D& aabb,
        const PhysicsQueryFilter2D& filter,
        std::span<PhysicsOverlapHit2D> out) const noexcept;

    // Multi-hit ray cast sorted by fraction, then body/shape index. Closest-only
    // convenience returns the first hit after the same ordering rules.
    [[nodiscard]] Core::Result<PhysicsQueryWriteResult2D> castRay(
        const PhysicsRayCast2D& ray,
        const PhysicsQueryFilter2D& filter,
        std::span<PhysicsCastHit2D> out) const noexcept;
    [[nodiscard]] Core::Result<PhysicsCastHit2D> castRayClosest(
        const PhysicsRayCast2D& ray,
        const PhysicsQueryFilter2D& filter) const noexcept;

    [[nodiscard]] Core::Result<PhysicsBodyState2D> bodyState(
        PhysicsBodyId body) const noexcept;
    [[nodiscard]] Core::Result<PhysicsBodyId> shapeBody(
        PhysicsShapeId shape) const noexcept;
    [[nodiscard]] Core::Result<PhysicsShapeState2D> shapeState(
        PhysicsShapeId shape) const noexcept;
    [[nodiscard]] Core::Result<PhysicsJointState2D> jointState(
        PhysicsJointId joint) const noexcept;

    [[nodiscard]] bool contains(PhysicsBodyId body) const noexcept;
    [[nodiscard]] bool contains(PhysicsShapeId shape) const noexcept;
    [[nodiscard]] bool contains(PhysicsJointId joint) const noexcept;
    [[nodiscard]] PhysicsWorld2DStats stats() const noexcept;
    [[nodiscard]] bool isOpen() const noexcept;

    // Idempotent on the owner thread. Published handles become stale and no
    // further mutation or query is accepted after a successful shutdown.
    [[nodiscard]] Core::Status shutdown() noexcept;

private:
    struct Impl;

    explicit PhysicsWorld2D(Impl* impl) noexcept;

    [[nodiscard]] Core::Status ensureUsable() const noexcept;
    [[nodiscard]] Core::Status validateBody(PhysicsBodyId body) const noexcept;
    [[nodiscard]] Core::Status validateShape(PhysicsShapeId shape) const noexcept;
    [[nodiscard]] Core::Status validateJoint(PhysicsJointId joint) const noexcept;

    Impl* m_impl = nullptr;
};

} // namespace Tina::Physics2D
