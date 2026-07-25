#include <tina/physics2d/PhysicsGridBodies.hpp>
#include <tina/physics2d/PhysicsWorld2D.hpp>

#include <gtest/gtest.h>

#include <array>
#include <utility>
#include <vector>

namespace Tina::Physics2D {
namespace {

[[nodiscard]] PhysicsWorld2DConfig smallWorld(Core::usize bodyCapacity = 16) noexcept
{
    PhysicsWorld2DConfig config;
    config.bodyCapacity = bodyCapacity;
    config.shapeCapacity = bodyCapacity;
    config.contactBeginCapacity = 8;
    config.contactEndCapacity = 8;
    config.contactHitCapacity = 4;
    config.commandCapacity = 8;
    config.solverSubStepCount = 1;
    config.gravityMetersPerSecondSquared = {0.0F, 0.0F};
    return config;
}

TEST(PhysicsGridBodiesTest, CreatesStaticBodiesAtCellCenters)
{
    auto worldResult = PhysicsWorld2D::Create(smallWorld(8));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    // Mimic TileMap solid floor cells (x,0) with cellSize=1 without linking Asset.
    const std::array<PhysicsGridSolidCell2D, 3> solids{{
        {1, 0},
        {2, 0},
        {3, 0},
    }};
    PhysicsBodyId bodies[3]{};
    PhysicsGridBodySyncConfig2D config;
    config.cellSizeMeters = 1.0F;
    config.enableContactEvents = true;

    auto sync = createStaticBodiesForSolidCells(world, solids, config, bodies);
    ASSERT_TRUE(sync) << sync.error().message;
    EXPECT_EQ(sync->totalFound, 3U);
    EXPECT_EQ(sync->written, 3U);
    EXPECT_FALSE(sync->overflow);
    EXPECT_EQ(world.stats().bodyCount, 3U);

    auto state0 = world.bodyState(bodies[0]);
    ASSERT_TRUE(state0) << state0.error().message;
    EXPECT_NEAR(state0->positionMeters.x, 1.5F, 1.0e-5F);
    EXPECT_NEAR(state0->positionMeters.y, 0.5F, 1.0e-5F);

    auto state2 = world.bodyState(bodies[2]);
    ASSERT_TRUE(state2) << state2.error().message;
    EXPECT_NEAR(state2->positionMeters.x, 3.5F, 1.0e-5F);
    EXPECT_NEAR(state2->positionMeters.y, 0.5F, 1.0e-5F);

    // Dynamic body falls onto solid row and begins contact.
    PhysicsBody2DDesc dynamicBody;
    dynamicBody.type = PhysicsBodyType2D::Dynamic;
    dynamicBody.positionMeters = {1.5F, 3.0F};
    dynamicBody.linearVelocityMetersPerSecond = {0.0F, -20.0F};
    PhysicsShape2DDesc box;
    box.kind = PhysicsShapeKind2D::Box;
    box.halfExtentsMeters = {0.4F, 0.4F};
    box.density = 1.0F;
    box.enableContactEvents = true;
    auto dynamic = world.createBody(dynamicBody);
    ASSERT_TRUE(dynamic) << dynamic.error().message;
    auto dynamicShape = world.createShape(*dynamic, box);
    ASSERT_TRUE(dynamicShape) << dynamicShape.error().message;

    bool sawBegin = false;
    for (int step = 0; step < 120; ++step) {
        ASSERT_TRUE(world.step());
        auto contacts = world.contactEvents();
        ASSERT_TRUE(contacts);
        if (!contacts->beginEvents.empty()) {
            sawBegin = true;
            break;
        }
    }
    EXPECT_TRUE(sawBegin);

    auto destroyed = destroyBodies(world, bodies);
    ASSERT_TRUE(destroyed) << destroyed.error().message;
    EXPECT_EQ(destroyed->written, 3U);
    EXPECT_FALSE(world.contains(bodies[0]));
    EXPECT_EQ(world.stats().bodyCount, 1U);
}

TEST(PhysicsGridBodiesTest, RollsBackOnCapacityFailureAndReportsOutOverflow)
{
    auto worldResult = PhysicsWorld2D::Create(smallWorld(2));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    const std::array<PhysicsGridSolidCell2D, 3> solids{{
        {0, 0},
        {1, 0},
        {2, 0},
    }};
    PhysicsBodyId ignored[1]{};
    auto failure = createStaticBodiesForSolidCells(world, solids, {}, ignored);
    EXPECT_FALSE(failure);
    EXPECT_EQ(world.stats().bodyCount, 0U);

    const std::array<PhysicsGridSolidCell2D, 2> two{{
        {0, 0},
        {1, 0},
    }};
    PhysicsBodyId oneSlot[1]{};
    auto overflow = createStaticBodiesForSolidCells(world, two, {}, oneSlot);
    ASSERT_TRUE(overflow) << overflow.error().message;
    EXPECT_EQ(overflow->totalFound, 2U);
    EXPECT_EQ(overflow->written, 1U);
    EXPECT_TRUE(overflow->overflow);
    EXPECT_EQ(world.stats().bodyCount, 2U);
    EXPECT_TRUE(world.contains(oneSlot[0]));
}

TEST(PhysicsGridBodiesTest, DestroyBodiesSkipsStaleAndEmpty)
{
    auto worldResult = PhysicsWorld2D::Create(smallWorld(4));
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    PhysicsWorld2D world = std::move(*worldResult);

    const std::array<PhysicsGridSolidCell2D, 1> solids{{{0, 0}}};
    PhysicsBodyId bodies[1]{};
    ASSERT_TRUE(createStaticBodiesForSolidCells(world, solids, {}, bodies));
    ASSERT_TRUE(world.destroyBody(bodies[0]));

    PhysicsBodyId staleBatch[2]{bodies[0], {}};
    auto result = destroyBodies(world, staleBatch);
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(result->totalFound, 2U);
    EXPECT_EQ(result->written, 0U);
}

} // namespace
} // namespace Tina::Physics2D
