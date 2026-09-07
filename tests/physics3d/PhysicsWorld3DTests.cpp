#include <tina/physics3d/PhysicsWorld3D.hpp>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <thread>
#include <utility>

namespace Tina::Physics3D {
namespace {

TEST(PhysicsWorld3DTests, ValidatesConfigurationBeforeAllocating)
{
    PhysicsWorld3DConfig config;
    config.bodyCapacity = 0;
    auto invalid = PhysicsWorld3D::Create(config);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, Physics3DErrorCode::InvalidConfiguration);
    config.bodyCapacity = 8;
    config.fixedDeltaSeconds = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(PhysicsWorld3D::Create(config));
    config.fixedDeltaSeconds = 1.0F / 60.0F;
    config.initialOriginMeters.x = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(PhysicsWorld3D::Create(config));
}

TEST(PhysicsWorld3DTests, BodyCapacityGenerationAndWorldOwnershipAreIndependent)
{
    auto first = PhysicsWorld3D::Create({.bodyCapacity = 1});
    auto second = PhysicsWorld3D::Create({.bodyCapacity = 1});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    auto body = first->createBody({});
    ASSERT_TRUE(body);
    auto full = first->createBody({});
    ASSERT_FALSE(full);
    EXPECT_EQ(full.error().code, Physics3DErrorCode::CapacityExceeded);
    auto foreign = second->bodyState(*body);
    ASSERT_FALSE(foreign);
    EXPECT_EQ(foreign.error().code, Physics3DErrorCode::WrongWorld);
    ASSERT_TRUE(first->destroyBody(*body));
    auto replacement = first->createBody({});
    ASSERT_TRUE(replacement);
    EXPECT_EQ(replacement->index(), body->index());
    EXPECT_NE(replacement->generation(), body->generation());
    auto stale = first->bodyState(*body);
    ASSERT_FALSE(stale);
    EXPECT_EQ(stale.error().code, Physics3DErrorCode::StaleBody);
}

TEST(PhysicsWorld3DTests, InvalidShapeAndPoseDoNotConsumeCapacity)
{
    auto world = PhysicsWorld3D::Create({.bodyCapacity = 1});
    ASSERT_TRUE(world);
    PhysicsBody3DDesc desc;
    desc.shape.radiusMeters = -1.0F;
    desc.shape.kind = PhysicsShapeKind3D::Sphere;
    EXPECT_FALSE(world->createBody(desc));
    desc.shape.kind = static_cast<PhysicsShapeKind3D>(255);
    EXPECT_FALSE(world->createBody(desc));
    desc.shape.kind = PhysicsShapeKind3D::Box;
    desc.rotation = {0.0F, 0.0F, 0.0F, 0.0F};
    EXPECT_FALSE(world->createBody(desc));
    EXPECT_EQ(world->stats()->bodyCount, 0U);
    EXPECT_TRUE(world->createBody({}));
}

TEST(PhysicsWorld3DTests, BoxSphereAndCapsuleUseTheRealSolver)
{
    auto world = PhysicsWorld3D::Create({.bodyCapacity = 8});
    ASSERT_TRUE(world);
    PhysicsBody3DDesc floor;
    floor.positionMeters = {0.0F, -0.5F, 0.0F};
    floor.shape.halfExtentsMeters = {20.0F, 0.5F, 20.0F};
    ASSERT_TRUE(world->createBody(floor));
    std::array<PhysicsBodyId, 3> bodies{};
    for (Core::usize index = 0; index < bodies.size(); ++index)
    {
        PhysicsBody3DDesc desc;
        desc.type = PhysicsBodyType3D::Dynamic;
        desc.positionMeters = {static_cast<float>(index) * 3.0F, 4.0F, 0.0F};
        desc.shape.kind = static_cast<PhysicsShapeKind3D>(index);
        auto body = world->createBody(desc);
        ASSERT_TRUE(body);
        bodies[index] = *body;
    }
    for (int tick = 0; tick < 240; ++tick)
    {
        ASSERT_TRUE(world->step());
    }
    for (Core::usize index = 0; index < bodies.size(); ++index)
    {
        auto state = world->bodyState(bodies[index]);
        ASSERT_TRUE(state);
        EXPECT_NEAR(state->positionMeters.y, index == 2 ? 1.0F : 0.5F, 0.05F);
    }
}

TEST(PhysicsWorld3DTests, QueriesFilterSensorsAndReturnBoundedStableOutput)
{
    auto world = PhysicsWorld3D::Create({.bodyCapacity = 4});
    ASSERT_TRUE(world);
    auto solid = world->createBody({.positionMeters = {0.0F, 0.0F, 0.0F}});
    auto sensor = world->createBody({.positionMeters = {0.0F, 2.0F, 0.0F}, .sensor = true});
    ASSERT_TRUE(solid);
    ASSERT_TRUE(sensor);
    const PhysicsRayCast3D ray{{0.0F, 5.0F, 0.0F}, {0.0F, -10.0F, 0.0F}};
    auto hit = world->castRayClosest(ray);
    ASSERT_TRUE(hit && hit->has_value());
    EXPECT_EQ(hit->value().body, *solid);
    hit = world->castRayClosest(ray, {.includeSensors = true});
    ASSERT_TRUE(hit && hit->has_value());
    EXPECT_EQ(hit->value().body, *sensor);
    const Math::Aabb3 bounds{{-2.0F, -2.0F, -2.0F}, {2.0F, 4.0F, 2.0F}};
    std::array<PhysicsBodyId, 1> output{};
    auto query = world->queryAabb(bounds, {.includeSensors = true}, output);
    ASSERT_TRUE(query);
    EXPECT_EQ(query->totalCount, 2U);
    EXPECT_EQ(query->writtenCount, 1U);
    EXPECT_TRUE(query->overflowed());
    EXPECT_EQ(output.front(), *solid);
    query = world->queryAabb(bounds, {.includeSensors = true}, {});
    ASSERT_TRUE(query);
    EXPECT_EQ(query->totalCount, 2U);
    EXPECT_EQ(query->writtenCount, 0U);
    auto miss = world->castRayClosest(ray, {.ignoredBody = *solid});
    ASSERT_TRUE(miss);
    EXPECT_FALSE(miss->has_value());
}

TEST(PhysicsWorld3DTests, ClosingOneWorldDoesNotUnregisterAnotherWorld)
{
    auto first = PhysicsWorld3D::Create({.bodyCapacity = 1});
    auto second = PhysicsWorld3D::Create({.bodyCapacity = 1});
    ASSERT_TRUE(first && second);
    ASSERT_TRUE(first->shutdown());
    ASSERT_TRUE(first->shutdown());
    EXPECT_FALSE(first->isOpen());
    EXPECT_FALSE(first->step());
    EXPECT_TRUE(second->createBody({}));
    EXPECT_TRUE(second->step());
    ASSERT_TRUE(second->shutdown());
    auto reopened = PhysicsWorld3D::Create({.bodyCapacity = 1});
    ASSERT_TRUE(reopened);
    EXPECT_TRUE(reopened->createBody({}));
}

TEST(PhysicsWorld3DTests, WrongThreadRejectsMutationQueryAndShutdown)
{
    auto world = PhysicsWorld3D::Create({.bodyCapacity = 1});
    ASSERT_TRUE(world);
    auto body = world->createBody({});
    ASSERT_TRUE(body);
    std::array<Core::ErrorCode, 4> errors{};
    std::thread worker([&] {
        if (auto result = world->step(); !result)
        {
            errors[0] = result.error().code;
        }
        if (auto result = world->shiftOrigin({1.0F, 0.0F, 0.0F}); !result)
        {
            errors[1] = result.error().code;
        }
        if (auto result = world->bodyState(*body); !result)
        {
            errors[2] = result.error().code;
        }
        if (auto result = world->shutdown(); !result)
        {
            errors[3] = result.error().code;
        }
    });
    worker.join();
    for (const auto error : errors)
    {
        EXPECT_EQ(error, Physics3DErrorCode::WrongOwnerThread);
    }
    EXPECT_TRUE(world->isOpen());
    EXPECT_EQ(world->origin()->revision, 0U);
}

TEST(PhysicsWorld3DTests, MoveTransfersWorldAndPreservesBodyIdentities)
{
    auto created = PhysicsWorld3D::Create({.bodyCapacity = 1});
    ASSERT_TRUE(created);
    auto body = created->createBody({});
    ASSERT_TRUE(body);
    PhysicsWorld3D moved(std::move(*created));
    EXPECT_FALSE(created->isOpen());
    EXPECT_TRUE(moved.bodyState(*body));
    EXPECT_TRUE(moved.shiftOrigin({32.0F, 0.0F, 0.0F}));
}

} // namespace
} // namespace Tina::Physics3D
