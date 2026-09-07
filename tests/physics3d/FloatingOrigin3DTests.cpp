#include <tina/physics3d/PhysicsWorld3D.hpp>

#include <gtest/gtest.h>

#include <array>
#include <limits>

namespace Tina::Physics3D {
namespace {

TEST(FloatingOrigin3DTests, ConvertsGlobalCoordinatesBeforeNarrowingToFloat)
{
    auto world =
        PhysicsWorld3D::Create({.bodyCapacity = 1, .initialOriginMeters = {1000000000.0, -1000000000.0, 1000000000.0}});
    ASSERT_TRUE(world);
    const PhysicsGlobalPosition3D global{1000000000.125, -999999999.75, 999999999.5};
    auto local = world->toLocalPosition(global);
    ASSERT_TRUE(local);
    EXPECT_EQ(*local, (Math::Vec3{0.125F, 0.25F, -0.5F}));
    auto restored = world->toGlobalPosition(*local);
    ASSERT_TRUE(restored);
    EXPECT_EQ(*restored, global);
    EXPECT_FALSE(world->toLocalPosition({0.0, 0.0, 0.0}));
}

TEST(FloatingOrigin3DTests, ShiftsStaticDynamicKinematicAndSleepingBodiesWithoutReplacingThem)
{
    auto world = PhysicsWorld3D::Create({.bodyCapacity = 4, .initialOriginMeters = {1000000000.0, 0.0, 0.0}});
    ASSERT_TRUE(world);
    std::array<PhysicsBodyState3D, 4> before{};
    for (Core::usize index = 0; index < before.size(); ++index)
    {
        PhysicsBody3DDesc desc;
        desc.type = index == 0   ? PhysicsBodyType3D::Static
                    : index == 1 ? PhysicsBodyType3D::Kinematic
                                 : PhysicsBodyType3D::Dynamic;
        desc.positionMeters = {8192.0F + static_cast<float>(index), 16.0F, -64.0F};
        desc.rotation = Math::fromAxisAngle({0.0F, 1.0F, 0.0F}, 0.5F);
        desc.startAwake = index != 3;
        if (index == 1 || index == 2)
        {
            desc.linearVelocityMetersPerSecond = {1.0F, 2.0F, 3.0F};
            desc.angularVelocityRadiansPerSecond = {0.0F, 0.25F, 0.0F};
        }
        auto body = world->createBody(desc);
        ASSERT_TRUE(body);
        auto state = world->bodyState(*body);
        ASSERT_TRUE(state);
        before[index] = *state;
    }
    const Math::Vec3 offset{8192.0F, 16.0F, -64.0F};
    auto shifted = world->shiftOrigin(offset);
    ASSERT_TRUE(shifted);
    EXPECT_EQ(shifted->shiftedBodyCount, 4U);
    EXPECT_EQ(shifted->before.revision, 0U);
    EXPECT_EQ(shifted->after.revision, 1U);
    EXPECT_EQ(shifted->after.positionMeters, (PhysicsGlobalPosition3D{1000008192.0, 16.0, -64.0}));
    for (const auto& original : before)
    {
        auto current = world->bodyState(original.body);
        ASSERT_TRUE(current);
        EXPECT_EQ(current->positionMeters, original.positionMeters - offset);
        EXPECT_EQ(current->rotation, original.rotation);
        EXPECT_EQ(current->linearVelocityMetersPerSecond, original.linearVelocityMetersPerSecond);
        EXPECT_EQ(current->angularVelocityRadiansPerSecond, original.angularVelocityRadiansPerSecond);
        EXPECT_EQ(current->awake, original.awake);
        EXPECT_EQ(current->originRevision, 1U);
        auto global = world->toGlobalPosition(current->positionMeters);
        ASSERT_TRUE(global);
        EXPECT_DOUBLE_EQ(global->x, 1000000000.0 + original.positionMeters.x);
    }
    EXPECT_EQ(world->stats()->stepCount, 0U);
}

TEST(FloatingOrigin3DTests, RayAndBroadphaseQueriesImmediatelyUseTheShiftedFrame)
{
    auto world = PhysicsWorld3D::Create({.bodyCapacity = 1});
    ASSERT_TRUE(world);
    auto body = world->createBody({.positionMeters = {8192.0F, 0.0F, 0.0F}});
    ASSERT_TRUE(body);
    auto before = world->castRayClosest({{8192.0F, 5.0F, 0.0F}, {0.0F, -10.0F, 0.0F}});
    ASSERT_TRUE(before && before->has_value());
    ASSERT_TRUE(world->shiftOrigin({8192.0F, 0.0F, 0.0F}));
    auto after = world->castRayClosest({{0.0F, 5.0F, 0.0F}, {0.0F, -10.0F, 0.0F}});
    ASSERT_TRUE(after && after->has_value());
    EXPECT_EQ(after->value().body, *body);
    EXPECT_FLOAT_EQ(after->value().fraction, before->value().fraction);
    EXPECT_EQ(after->value().normal, before->value().normal);
    EXPECT_EQ(after->value().originRevision, 1U);
    std::array<PhysicsBodyId, 1> output{};
    auto query = world->queryAabb({{-2.0F, -2.0F, -2.0F}, {2.0F, 2.0F, 2.0F}}, {}, output);
    ASSERT_TRUE(query);
    EXPECT_EQ(query->totalCount, 1U);
    EXPECT_EQ(output.front(), *body);
    auto oldFrame = world->castRayClosest({{8192.0F, 5.0F, 0.0F}, {0.0F, -10.0F, 0.0F}});
    ASSERT_TRUE(oldFrame);
    EXPECT_FALSE(oldFrame->has_value());
}

TEST(FloatingOrigin3DTests, OutOfRangeBodyRejectsTheWholeShiftBeforeMovingAnyBody)
{
    auto world = PhysicsWorld3D::Create({.bodyCapacity = 2});
    ASSERT_TRUE(world);
    auto near = world->createBody({.positionMeters = {1.0F, 2.0F, 3.0F}});
    auto far = world->createBody({.positionMeters = {-Physics3DLimits::MaximumLocalCoordinateMeters, 0.0F, 0.0F}});
    ASSERT_TRUE(near && far);
    auto rejected = world->shiftOrigin({16.0F, 0.0F, 0.0F});
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, Physics3DErrorCode::InvalidOriginShift);
    EXPECT_EQ(world->bodyState(*near)->positionMeters, (Math::Vec3{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(world->bodyState(*far)->positionMeters.x, -Physics3DLimits::MaximumLocalCoordinateMeters);
    EXPECT_EQ(world->origin()->positionMeters, PhysicsGlobalPosition3D{});
    EXPECT_EQ(world->origin()->revision, 0U);
}

TEST(FloatingOrigin3DTests, RejectsNonFiniteAndGlobalPrecisionLossWithoutPublishing)
{
    auto world = PhysicsWorld3D::Create(
        {.bodyCapacity = 1, .initialOriginMeters = {Physics3DLimits::MaximumGlobalCoordinateMeters, 0.0, 0.0}});
    ASSERT_TRUE(world);
    EXPECT_FALSE(world->shiftOrigin({std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}));
    EXPECT_FALSE(world->shiftOrigin({std::numeric_limits<float>::infinity(), 0.0F, 0.0F}));
    EXPECT_FALSE(world->shiftOrigin({1.0F, 0.0F, 0.0F}));
    EXPECT_FALSE(world->shiftOrigin({-0.000000001F, 0.0F, 0.0F}));
    EXPECT_EQ(world->origin()->revision, 0U);
    auto zero = world->shiftOrigin({});
    ASSERT_TRUE(zero);
    EXPECT_EQ(zero->after.revision, 0U);
    EXPECT_EQ(zero->shiftedBodyCount, 0U);
}

TEST(FloatingOrigin3DTests, RepeatedReverseShiftsPreserveGlobalPositionAndAdvanceOnceEach)
{
    auto world = PhysicsWorld3D::Create({.bodyCapacity = 1});
    ASSERT_TRUE(world);
    auto body = world->createBody({.positionMeters = {0.125F, 0.25F, -0.5F}});
    ASSERT_TRUE(body);
    for (int iteration = 0; iteration < 16; ++iteration)
    {
        ASSERT_TRUE(world->shiftOrigin({1024.0F, -512.0F, 256.0F}));
        ASSERT_TRUE(world->shiftOrigin({-1024.0F, 512.0F, -256.0F}));
    }
    EXPECT_EQ(world->origin()->revision, 32U);
    EXPECT_EQ(world->origin()->positionMeters, PhysicsGlobalPosition3D{});
    EXPECT_EQ(world->bodyState(*body)->positionMeters, (Math::Vec3{0.125F, 0.25F, -0.5F}));
}

TEST(FloatingOrigin3DTests, SolverContactsStillHoldAfterRebasingTheFloorAndFallingBody)
{
    auto world = PhysicsWorld3D::Create({.bodyCapacity = 2});
    ASSERT_TRUE(world);
    PhysicsBody3DDesc floor;
    floor.positionMeters = {8192.0F, -0.5F, 0.0F};
    floor.shape.halfExtentsMeters = {20.0F, 0.5F, 20.0F};
    ASSERT_TRUE(world->createBody(floor));
    auto body = world->createBody({.type = PhysicsBodyType3D::Dynamic, .positionMeters = {8192.0F, 4.0F, 0.0F}});
    ASSERT_TRUE(body);
    for (int tick = 0; tick < 30; ++tick)
    {
        ASSERT_TRUE(world->step());
    }
    ASSERT_TRUE(world->shiftOrigin({8192.0F, 0.0F, 0.0F}));
    for (int tick = 0; tick < 210; ++tick)
    {
        ASSERT_TRUE(world->step());
    }
    auto state = world->bodyState(*body);
    ASSERT_TRUE(state);
    EXPECT_NEAR(state->positionMeters.y, 0.5F, 0.05F);
    EXPECT_NEAR(state->positionMeters.x, 0.0F, 0.01F);
    EXPECT_EQ(state->originRevision, 1U);
}

} // namespace
} // namespace Tina::Physics3D
