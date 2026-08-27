#include <tina/gameplay2d/Scene2DPhysicsBridge.hpp>

#include <tina/scene/SceneErrors.hpp>

#include <algorithm>
#include <cmath>
#include <new>

namespace Tina::Gameplay2D {
namespace {

[[nodiscard]] Physics2D::PhysicsBodyType2D physicsTypeFor(Scene::PhysicsBodyKind2D kind) noexcept
{
    switch (kind)
    {
    case Scene::PhysicsBodyKind2D::Rigid:
        return Physics2D::PhysicsBodyType2D::Dynamic;
    // A character body is driven by gameplay movement rather than forces, and an
    // area is a trigger volume, so both map to Kinematic: they move without
    // reacting to collisions.
    case Scene::PhysicsBodyKind2D::Character:
    case Scene::PhysicsBodyKind2D::Area:
        return Physics2D::PhysicsBodyType2D::Kinematic;
    case Scene::PhysicsBodyKind2D::Static:
    default:
        return Physics2D::PhysicsBodyType2D::Static;
    }
}

[[nodiscard]] Physics2D::PhysicsShapeKind2D physicsShapeKindFor(Scene::PhysicsShapeKind2D kind) noexcept
{
    switch (kind)
    {
    case Scene::PhysicsShapeKind2D::Circle:
        return Physics2D::PhysicsShapeKind2D::Circle;
    case Scene::PhysicsShapeKind2D::Capsule:
        return Physics2D::PhysicsShapeKind2D::Capsule;
    case Scene::PhysicsShapeKind2D::Box:
    default:
        return Physics2D::PhysicsShapeKind2D::Box;
    }
}

// 2D bodies rotate about Z only, so the planar angle is recovered from the
// quaternion's Z/W terms rather than a full Euler decomposition.
[[nodiscard]] float planarAngleRadians(const Scene::Quaternion& rotation) noexcept
{
    return 2.0F * std::atan2(rotation.z, rotation.w);
}

[[nodiscard]] Scene::Quaternion planarRotation(float angleRadians) noexcept
{
    const float half = angleRadians * 0.5F;
    return Scene::Quaternion{.x = 0.0F, .y = 0.0F, .z = std::sin(half), .w = std::cos(half)};
}

} // namespace

Core::Status Scene2DPhysicsBridge::build(const Scene::World& world, Physics2D::PhysicsWorld2D& physicsWorld,
                                         Scene2DPhysicsBridgeConfig config)
try
{
    if (config.bodyCapacity == 0 || config.shapeCapacity == 0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Scene2DPhysicsBridge capacities must be non-zero");
    }
    if (!m_bodies.empty() || !m_shapes.empty())
    {
        return Core::failure(Core::CoreErrorCode::AlreadyExists,
                             "Scene2DPhysicsBridge is already built; shutdown first");
    }
    m_stats = {};

    // Anything created before a failure is destroyed, so a failed build never
    // leaves half a simulation wired up.
    const auto rollback = [this, &physicsWorld]() noexcept {
        for (const Physics2D::PhysicsShapeId shape : m_shapes)
        {
            static_cast<void>(physicsWorld.destroyShape(shape));
        }
        for (const BodyEntry& entry : m_bodies)
        {
            static_cast<void>(physicsWorld.destroyBody(entry.body));
        }
        m_shapes.clear();
        m_bodies.clear();
        m_stats = {};
    };

    for (const Scene::EntityId entity : world.liveEntities())
    {
        const Scene::PhysicsBody2D* body = world.physicsBody2D(entity);
        if (body == nullptr)
        {
            continue;
        }
        const Scene::WorldTransform* transform = world.worldTransform(entity);
        if (transform == nullptr)
        {
            rollback();
            return Core::failure(Scene::SceneErrorCode::InvalidTransform,
                                 "Scene2DPhysicsBridge requires a published world transform per body");
        }
        if (m_bodies.size() == config.bodyCapacity)
        {
            rollback();
            return Core::failure(Scene::SceneErrorCode::CapacityExceeded,
                                 "Scene2DPhysicsBridge body capacity is exhausted");
        }
        auto created = physicsWorld.createBody(Physics2D::PhysicsBody2DDesc{
            .type = physicsTypeFor(body->kind),
            .positionMeters = {transform->position.x, transform->position.y},
            .angleRadians = planarAngleRadians(transform->rotation),
            .linearVelocityMetersPerSecond = {body->linearVelocityX, body->linearVelocityY},
            .angularVelocityRadiansPerSecond = body->angularVelocityRadiansPerSecond,
            .linearDamping = body->linearDamping,
            .angularDamping = body->angularDamping,
            .gravityScale = body->gravityScale,
            .enableSleep = body->enableSleep,
            .initiallyAwake = body->initiallyAwake,
            .fixedRotation = body->fixedRotation,
            .continuousCollision = body->continuousCollision,
            .enabled = body->enabled,
        });
        if (!created)
        {
            rollback();
            return Core::failure(std::move(created.error()));
        }
        m_bodies.push_back(BodyEntry{
            .entity = entity,
            .body = *created,
            .parented = world.parent(entity).hasValue(),
        });
    }

    for (const Scene::EntityId entity : world.liveEntities())
    {
        const Scene::PhysicsShape2D* shape = world.physicsShape2D(entity);
        if (shape == nullptr)
        {
            continue;
        }
        // Walk up to the nearest ancestor that owns a body. The shape's own entity
        // is checked first so a body and its shape may share one entity.
        Physics2D::PhysicsBodyId owner{};
        for (Scene::EntityId candidate = entity; candidate.hasValue();
             candidate = world.parent(candidate))
        {
            owner = bodyFor(candidate);
            if (owner.hasValue())
            {
                break;
            }
        }
        if (!owner.hasValue())
        {
            ++m_stats.orphanShapeCount;
            continue;
        }
        if (m_shapes.size() == config.shapeCapacity)
        {
            rollback();
            return Core::failure(Scene::SceneErrorCode::CapacityExceeded,
                                 "Scene2DPhysicsBridge shape capacity is exhausted");
        }
        auto created = physicsWorld.createShape(
            owner, Physics2D::PhysicsShape2DDesc{
                       .kind = physicsShapeKindFor(shape->kind),
                       .halfExtentsMeters = {shape->halfExtentX, shape->halfExtentY},
                       .radiusMeters = shape->radius,
                       .localCenterMeters = {shape->localCenterX, shape->localCenterY},
                       .localAngleRadians = shape->localAngleRadians,
                       .localPointAMeters = {shape->localPointAX, shape->localPointAY},
                       .localPointBMeters = {shape->localPointBX, shape->localPointBY},
                       .density = shape->density,
                       .friction = shape->friction,
                       .restitution = shape->restitution,
                       .isSensor = shape->sensor,
                       .enableSensorEvents = shape->sensorEvents,
                       .enableContactEvents = shape->contactEvents,
                       .enableHitEvents = shape->hitEvents,
                   });
        if (!created)
        {
            rollback();
            return Core::failure(std::move(created.error()));
        }
        m_shapes.push_back(*created);
    }

    m_stats.bodyCount = m_bodies.size();
    m_stats.shapeCount = m_shapes.size();
    return Core::success();
}
catch (const std::bad_alloc&)
{
    static_cast<void>(shutdown(physicsWorld));
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "Scene2DPhysicsBridge build allocation failed");
}

Core::Status Scene2DPhysicsBridge::applyTo(Scene::World& world,
                                           const Physics2D::PhysicsWorld2D& physicsWorld)
{
    for (const BodyEntry& entry : m_bodies)
    {
        // A parented entity's LocalTransform is relative to its parent while the
        // body is in world space; writing one into the other would compound the
        // parent transform every frame.
        if (entry.parented)
        {
            continue;
        }
        const Scene::LocalTransform* current = world.localTransform(entry.entity);
        if (current == nullptr)
        {
            // The game destroyed the entity while the bridge still tracks its body.
            // That is legal, so skip rather than fail the frame.
            continue;
        }
        auto state = physicsWorld.bodyState(entry.body);
        if (!state)
        {
            return Core::failure(std::move(state.error()));
        }
        Scene::LocalTransform updated = *current;
        updated.position.x = state->positionMeters.x;
        updated.position.y = state->positionMeters.y;
        updated.rotation = planarRotation(state->angleRadians);
        if (Core::Status status = world.setLocalTransform(entry.entity, updated); !status)
        {
            return status;
        }
        ++m_stats.transformWriteBackCount;
    }
    return Core::success();
}

Core::Status Scene2DPhysicsBridge::shutdown(Physics2D::PhysicsWorld2D& physicsWorld) noexcept
{
    Core::Status result = Core::success();
    for (const Physics2D::PhysicsShapeId shape : m_shapes)
    {
        if (Core::Status status = physicsWorld.destroyShape(shape); !status && result)
        {
            result = status;
        }
    }
    for (const BodyEntry& entry : m_bodies)
    {
        if (Core::Status status = physicsWorld.destroyBody(entry.body); !status && result)
        {
            result = status;
        }
    }
    m_shapes.clear();
    m_bodies.clear();
    m_stats = {};
    return result;
}

Physics2D::PhysicsBodyId Scene2DPhysicsBridge::bodyFor(Scene::EntityId entity) const noexcept
{
    const auto found = std::ranges::find(m_bodies, entity, &BodyEntry::entity);
    return found == m_bodies.end() ? Physics2D::PhysicsBodyId{} : found->body;
}

Scene::EntityId Scene2DPhysicsBridge::entityFor(Physics2D::PhysicsBodyId body) const noexcept
{
    const auto found = std::ranges::find(m_bodies, body, &BodyEntry::body);
    return found == m_bodies.end() ? Scene::EntityId{} : found->entity;
}

} // namespace Tina::Gameplay2D
