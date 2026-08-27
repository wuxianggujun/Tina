#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/physics2d/PhysicsWorld2D.hpp>
#include <tina/scene/Entity.hpp>
#include <tina/scene/World.hpp>

#include <vector>

namespace Tina::Gameplay2D {

struct Scene2DPhysicsBridgeConfig final {
    // Fixed at build time. Exceeding either is CapacityExceeded rather than a
    // reallocation, matching the physics world's own bounded contract.
    Core::usize bodyCapacity = 256;
    Core::usize shapeCapacity = 512;
};

struct Scene2DPhysicsBridgeStats final {
    Core::usize bodyCount = 0;
    Core::usize shapeCount = 0;
    // Shapes whose entity had no physics-body ancestor. They are reported rather
    // than silently dropped, because a shape that collides with nothing looks
    // identical to one the bridge never saw.
    Core::usize orphanShapeCount = 0;
    Core::u64 transformWriteBackCount = 0;
};

// Owner-thread bridge between one Scene::World's authored physics components and
// one PhysicsWorld2D.
//
// Physics is authoritative in one direction only: build() reads the authored
// components once, and applyTo() writes simulated position/angle back into
// LocalTransform. It never reads transforms back to drive the simulation, because
// a two-way sync has to define who wins and when, and teleport and CCD semantics
// then fight each other. A game that wants to move a body calls
// PhysicsWorld2D::enqueueSetTransform.
//
// The bridge stores only generation-aware runtime handles. It owns no AssetLease
// and touches no UI or backend. shutdown() must run before the physics world is
// destroyed, matching TileMapPhysicsSync2D's contract.
class Scene2DPhysicsBridge final {
  public:
    Scene2DPhysicsBridge() noexcept = default;
    ~Scene2DPhysicsBridge() noexcept = default;

    Scene2DPhysicsBridge(const Scene2DPhysicsBridge&) = delete;
    Scene2DPhysicsBridge& operator=(const Scene2DPhysicsBridge&) = delete;
    Scene2DPhysicsBridge(Scene2DPhysicsBridge&&) noexcept = default;
    Scene2DPhysicsBridge& operator=(Scene2DPhysicsBridge&&) = delete;

    // Creates one body per entity carrying PhysicsBody2D, then attaches every
    // PhysicsShape2D entity to the nearest physics-body ancestor. Parent/child
    // decides ownership, which is the same rule the Editor enforces when it
    // requires a CollisionShape2D to have a body parent.
    //
    // Body position and angle come from the entity's published WorldTransform, so
    // the caller must have run World::updateWorldTransforms() first. On failure
    // every object this call created is destroyed, so a partial bridge is never
    // left behind.
    [[nodiscard]] Core::Status build(const Scene::World& world,
                                     Physics2D::PhysicsWorld2D& physicsWorld,
                                     Scene2DPhysicsBridgeConfig config = {});

    // Copies simulated body transforms into LocalTransform. Call after
    // PhysicsWorld2D::step(); the caller then runs World::updateWorldTransforms()
    // to republish the hierarchy.
    //
    // Entities with a parent are skipped: their LocalTransform is relative to that
    // parent while the body is in world space, and writing one into the other
    // would compound the parent transform every frame.
    [[nodiscard]] Core::Status applyTo(Scene::World& world,
                                       const Physics2D::PhysicsWorld2D& physicsWorld);

    // Destroys every body and shape this bridge created. Safe to call twice.
    [[nodiscard]] Core::Status shutdown(Physics2D::PhysicsWorld2D& physicsWorld) noexcept;

    [[nodiscard]] const Scene2DPhysicsBridgeStats& stats() const noexcept { return m_stats; }
    [[nodiscard]] Physics2D::PhysicsBodyId bodyFor(Scene::EntityId entity) const noexcept;
    [[nodiscard]] Scene::EntityId entityFor(Physics2D::PhysicsBodyId body) const noexcept;

  private:
    struct BodyEntry final {
        Scene::EntityId entity{};
        Physics2D::PhysicsBodyId body{};
        // True when the entity has a parent, so applyTo must not write world-space
        // physics output into a parent-relative LocalTransform.
        bool parented = false;
    };

    std::vector<BodyEntry> m_bodies{};
    std::vector<Physics2D::PhysicsShapeId> m_shapes{};
    Scene2DPhysicsBridgeStats m_stats{};
};

} // namespace Tina::Gameplay2D
