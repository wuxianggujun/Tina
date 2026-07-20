#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/physics2d/PhysicsErrors.hpp>
#include <tina/physics2d/PhysicsTypes.hpp>

#include <memory_resource>

namespace Tina::Physics2D {

[[nodiscard]] Core::Status validatePhysicsWorld2DConfig(
    const PhysicsWorld2DConfig& config) noexcept;
[[nodiscard]] Core::Status validatePhysicsBody2DDesc(
    const PhysicsBody2DDesc& desc) noexcept;
[[nodiscard]] Core::Status validatePhysicsBoxShape2DDesc(
    const PhysicsBoxShape2DDesc& desc) noexcept;

// Single-owner, fixed-step 2D physics world. M11-A0 owns one Box shape per body
// and fixed-step lifecycle; M11-A1 copies contact begin/end/hit into fixed
// Tina storage before step() returns. Spatial queries and deferred commands
// remain separate follow-up contracts.
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

    [[nodiscard]] Core::Result<PhysicsBodyShape2D> createBoxBody(
        const PhysicsBody2DDesc& body,
        const PhysicsBoxShape2DDesc& shape);
    [[nodiscard]] Core::Status destroyBody(PhysicsBodyId body) noexcept;

    // Advances exactly config.fixedDeltaSeconds. Runtime owns accumulator and
    // catch-up policy; this world never accepts a variable frame delta.
    // Before returning, Box2D contact events are copied into fixed Tina storage.
    [[nodiscard]] Core::Status step() noexcept;

    // Borrowed until the next successful step(), shutdown(), move, or destroy.
    [[nodiscard]] Core::Result<PhysicsContactEvents2DView> contactEvents() const noexcept;

    [[nodiscard]] Core::Result<PhysicsBodyState2D> bodyState(
        PhysicsBodyId body) const noexcept;
    [[nodiscard]] Core::Result<PhysicsBodyId> shapeBody(
        PhysicsShapeId shape) const noexcept;

    [[nodiscard]] bool contains(PhysicsBodyId body) const noexcept;
    [[nodiscard]] bool contains(PhysicsShapeId shape) const noexcept;
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

    Impl* m_impl = nullptr;
};

} // namespace Tina::Physics2D
