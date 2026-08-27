#include <tina/gameplay2d/Scene2DPhysicsBridge.hpp>

#include <tina/physics2d/PhysicsTypes.hpp>
#include <tina/scene/SceneErrors.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <utility>

namespace Tina::Gameplay2D {
namespace {

[[nodiscard]] Physics2D::PhysicsWorld2DConfig physicsConfig()
{
    return Physics2D::PhysicsWorld2DConfig{
        .gravityMetersPerSecondSquared = {0.0F, -10.0F},
        .fixedDeltaSeconds = 1.0 / 60.0,
    };
}

[[nodiscard]] Scene::World makeWorld(Core::usize capacity = 32)
{
    auto world = Scene::World::Create(Scene::WorldConfig{.entityCapacity = capacity});
    EXPECT_TRUE(world) << (world ? "" : world.error().message);
    return std::move(*world);
}

[[nodiscard]] Physics2D::PhysicsWorld2D makePhysicsWorld()
{
    auto world = Physics2D::PhysicsWorld2D::Create(physicsConfig());
    EXPECT_TRUE(world) << (world ? "" : world.error().message);
    return std::move(*world);
}

// A CollisionShape2D belongs to the nearest physics-body ancestor, which is the
// same rule the Editor enforces when it refuses a shape without a body parent.
TEST(Scene2DPhysicsBridgeTest, AttachesShapesToTheNearestBodyAncestor)
{
    Scene::World world = makeWorld();
    Scene::LocalTransform bodyLocal{};
    bodyLocal.position = {2.0F, 5.0F, 0.0F};
    const Scene::EntityId body = world.createEntity(bodyLocal).value();
    ASSERT_TRUE(world.setPhysicsBody2D(body, Scene::PhysicsBody2D{
                                                 .kind = Scene::PhysicsBodyKind2D::Rigid,
                                             }));
    const Scene::EntityId shape = world.createEntity().value();
    ASSERT_TRUE(world.setParent(shape, body, Scene::ReparentMode::KeepLocal));
    ASSERT_TRUE(world.setPhysicsShape2D(shape, Scene::PhysicsShape2D{}));
    // A grandchild shape must still resolve to the same body.
    const Scene::EntityId nested = world.createEntity().value();
    ASSERT_TRUE(world.setParent(nested, shape, Scene::ReparentMode::KeepLocal));
    ASSERT_TRUE(world.setPhysicsShape2D(nested, Scene::PhysicsShape2D{
                                                    .kind = Scene::PhysicsShapeKind2D::Circle,
                                                }));
    // A shape with no body ancestor is reported rather than silently dropped.
    const Scene::EntityId orphan = world.createEntity().value();
    ASSERT_TRUE(world.setPhysicsShape2D(orphan, Scene::PhysicsShape2D{}));
    ASSERT_TRUE(world.updateWorldTransforms());

    Physics2D::PhysicsWorld2D physicsWorld = makePhysicsWorld();
    Scene2DPhysicsBridge bridge;
    ASSERT_TRUE(bridge.build(world, physicsWorld));
    EXPECT_EQ(bridge.stats().bodyCount, 1U);
    EXPECT_EQ(bridge.stats().shapeCount, 2U);
    EXPECT_EQ(bridge.stats().orphanShapeCount, 1U);

    const Physics2D::PhysicsBodyId bodyId = bridge.bodyFor(body);
    ASSERT_TRUE(bodyId.hasValue());
    EXPECT_EQ(bridge.entityFor(bodyId), body);
    // Body position comes from the published world transform.
    auto state = physicsWorld.bodyState(bodyId);
    ASSERT_TRUE(state) << state.error().message;
    EXPECT_FLOAT_EQ(state->positionMeters.x, 2.0F);
    EXPECT_FLOAT_EQ(state->positionMeters.y, 5.0F);

    ASSERT_TRUE(bridge.shutdown(physicsWorld));
    EXPECT_EQ(bridge.stats().bodyCount, 0U);
    EXPECT_FALSE(physicsWorld.contains(bodyId));
}

// Physics is authoritative one way: simulation drives the transform, and the
// bridge never reads the transform back to move the body.
TEST(Scene2DPhysicsBridgeTest, SimulationDrivesLocalTransformOneWay)
{
    Scene::World world = makeWorld();
    Scene::LocalTransform local{};
    local.position = {0.0F, 10.0F, 0.0F};
    const Scene::EntityId body = world.createEntity(local).value();
    ASSERT_TRUE(world.setPhysicsBody2D(body, Scene::PhysicsBody2D{
                                                 .kind = Scene::PhysicsBodyKind2D::Rigid,
                                             }));
    const Scene::EntityId shape = world.createEntity().value();
    ASSERT_TRUE(world.setParent(shape, body, Scene::ReparentMode::KeepLocal));
    ASSERT_TRUE(world.setPhysicsShape2D(shape, Scene::PhysicsShape2D{}));
    ASSERT_TRUE(world.updateWorldTransforms());

    Physics2D::PhysicsWorld2D physicsWorld = makePhysicsWorld();
    Scene2DPhysicsBridge bridge;
    ASSERT_TRUE(bridge.build(world, physicsWorld));

    for (int step = 0; step < 30; ++step)
    {
        ASSERT_TRUE(physicsWorld.step());
    }
    ASSERT_TRUE(bridge.applyTo(world, physicsWorld));
    ASSERT_TRUE(world.updateWorldTransforms());

    const Scene::LocalTransform* updated = world.localTransform(body);
    ASSERT_NE(updated, nullptr);
    // Gravity is negative Y, so a dynamic body must have fallen.
    EXPECT_LT(updated->position.y, 10.0F);
    EXPECT_GT(bridge.stats().transformWriteBackCount, 0U);

    // The child shape entity has a parent, so its own transform is untouched:
    // writing world-space physics output into a parent-relative transform would
    // compound the parent every frame.
    const Scene::LocalTransform* childLocal = world.localTransform(shape);
    ASSERT_NE(childLocal, nullptr);
    EXPECT_FLOAT_EQ(childLocal->position.y, 0.0F);

    ASSERT_TRUE(bridge.shutdown(physicsWorld));
}

TEST(Scene2DPhysicsBridgeTest, CapacityAndRebuildFailClosed)
{
    Scene::World world = makeWorld();
    for (int index = 0; index < 3; ++index)
    {
        const Scene::EntityId body = world.createEntity().value();
        ASSERT_TRUE(world.setPhysicsBody2D(body, Scene::PhysicsBody2D{}));
    }
    ASSERT_TRUE(world.updateWorldTransforms());

    Physics2D::PhysicsWorld2D physicsWorld = makePhysicsWorld();
    Scene2DPhysicsBridge bridge;
    auto overCapacity = bridge.build(world, physicsWorld, {.bodyCapacity = 2, .shapeCapacity = 4});
    ASSERT_FALSE(overCapacity);
    EXPECT_EQ(overCapacity.error().code, Scene::SceneErrorCode::CapacityExceeded);
    // A failed build must leave nothing behind, not a partial simulation.
    EXPECT_EQ(bridge.stats().bodyCount, 0U);
    EXPECT_EQ(physicsWorld.stats().bodyCount, 0U);

    ASSERT_TRUE(bridge.build(world, physicsWorld));
    EXPECT_EQ(bridge.stats().bodyCount, 3U);
    // Building twice would leak the first set of handles.
    auto rebuilt = bridge.build(world, physicsWorld);
    ASSERT_FALSE(rebuilt);
    EXPECT_EQ(rebuilt.error().code, Core::CoreErrorCode::AlreadyExists);

    ASSERT_TRUE(bridge.shutdown(physicsWorld));
    // shutdown is idempotent so a product can call it on an unwound path.
    ASSERT_TRUE(bridge.shutdown(physicsWorld));
}

} // namespace
} // namespace Tina::Gameplay2D
