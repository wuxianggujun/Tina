#include <tina/navigation2d/NavigationAgent2D.hpp>
#include <tina/navigation2d/NavigationErrors.hpp>
#include <tina/navigation2d/NavigationFlowField2D.hpp>

#include "NavigationTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <utility>

namespace Tina::Navigation2D {
namespace {

TEST(NavigationPathFollower2DTests, LargeDeltaStopsAtCornerAndDoesNotIntegrateUnobservedMovement)
{
    auto follower = NavigationPathFollower2D::Create({.waypointCapacity = 3, .speedMetersPerSecond = 100.0F});
    ASSERT_TRUE(follower);
    const std::array path{Math::Vec2{0, 0}, Math::Vec2{1, 0}, Math::Vec2{1, 1}};
    ASSERT_TRUE(follower->setPath(path));
    auto first = follower->update({0, 0}, 100.0F);
    ASSERT_TRUE(first);
    EXPECT_EQ(first->targetPosition, (Math::Vec2{1, 0}));
    EXPECT_FLOAT_EQ(first->desiredVelocity.x, 0.01F);
    EXPECT_FLOAT_EQ(first->desiredVelocity.y, 0.0F);
    auto notMoved = follower->update({0, 0}, 100.0F);
    ASSERT_TRUE(notMoved);
    EXPECT_EQ(notMoved->waypointIndex, first->waypointIndex);
    auto second = follower->update({1, 0}, 100.0F);
    ASSERT_TRUE(second);
    EXPECT_EQ(second->targetPosition, (Math::Vec2{1, 1}));
    EXPECT_FLOAT_EQ(second->desiredVelocity.x, 0.0F);
    EXPECT_FLOAT_EQ(second->desiredVelocity.y, 0.01F);
    auto arrived = follower->update({1, 1}, 1.0F);
    ASSERT_TRUE(arrived);
    EXPECT_EQ(arrived->state, NavigationPathFollowerState2D::Arrived);
    EXPECT_EQ(arrived->desiredVelocity, Math::Vec2{});
}

TEST(NavigationPathFollower2DTests, FailedReplacementPreservesPathAndSelfAssignmentIsSafe)
{
    auto follower = NavigationPathFollower2D::Create({.waypointCapacity = 2});
    ASSERT_TRUE(follower);
    const std::array valid{Math::Vec2{0, 0}, Math::Vec2{1, 1}};
    ASSERT_TRUE(follower->setPath(valid));
    ASSERT_TRUE(follower->setPath(follower->path()));
    const std::array invalid{Math::Vec2{(std::numeric_limits<float>::quiet_NaN)(), 0}};
    EXPECT_FALSE(follower->setPath(invalid));
    EXPECT_FALSE(follower->setPath({}));
    EXPECT_FALSE(follower->setSpeed(-1.0F));
    EXPECT_FALSE(follower->update({0, 0}, 0.0F));
    ASSERT_EQ(follower->path().size(), 2U);
    EXPECT_EQ(follower->path().back(), valid.back());
    EXPECT_EQ(follower->state(), NavigationPathFollowerState2D::Following);
    follower->cancel();
    EXPECT_EQ(follower->state(), NavigationPathFollowerState2D::Cancelled);
    follower->reset();
    EXPECT_FALSE(follower->update({0, 0}, 1.0F));
}

TEST(NavigationAgent2DTests, PlansSmoothsAndFollowsToExactWorldGoalWithoutCrossingWalls)
{
    const std::array blocked{NavigationCell2D{2, 0}, NavigationCell2D{2, 1}, NavigationCell2D{2, 2}};
    auto grid = TestSupport::makeGrid(5, 4, blocked);
    auto agent = NavigationAgent2D::Create({.cellCapacity = 20, .speedMetersPerSecond = 4.0F});
    ASSERT_TRUE(grid); ASSERT_TRUE(agent);
    Math::Vec2 position{0.25F, 0.25F};
    const Math::Vec2 goal{4.8F, 3.8F};
    ASSERT_TRUE(agent->setGoal(*grid, position, goal));
    constexpr float delta = 1.0F / 60.0F;
    bool sawPlanning = false;
    for (Core::usize frame = 0; frame < 2000; ++frame)
    {
        auto steering = agent->update(*grid, position, delta, 1);
        ASSERT_TRUE(steering) << steering.error().message;
        sawPlanning |= steering->state == NavigationAgentState2D::Planning;
        if (steering->state == NavigationAgentState2D::Arrived) { break; }
        const Math::Vec2 nextPosition = position + steering->desiredVelocity * delta;
        auto visible = hasNavigationWorldLineOfSight2D(*grid, position, nextPosition);
        ASSERT_TRUE(visible);
        EXPECT_TRUE(*visible) << "frame " << frame;
        position = nextPosition;
    }
    EXPECT_TRUE(sawPlanning);
    EXPECT_EQ(agent->state(), NavigationAgentState2D::Arrived);
    EXPECT_LE(Math::length(position - goal), 0.011F);
    ASSERT_FALSE(agent->path().empty());
    EXPECT_EQ(agent->path().back(), goal);
}

TEST(NavigationAgent2DTests, SameCellGoalMovesDirectlyInsteadOfDetouringThroughCenter)
{
    auto grid = TestSupport::makeGrid(1, 1);
    auto agent = NavigationAgent2D::Create({.cellCapacity = 1});
    ASSERT_TRUE(grid); ASSERT_TRUE(agent);
    ASSERT_TRUE(agent->setGoal(*grid, {0.1F, 0.1F}, {0.2F, 0.1F}));
    auto steering = agent->update(*grid, {0.1F, 0.1F}, 1.0F, 1);
    ASSERT_TRUE(steering);
    EXPECT_EQ(steering->targetPosition, (Math::Vec2{0.2F, 0.1F}));
    EXPECT_FLOAT_EQ(steering->desiredVelocity.y, 0.0F);
    EXPECT_NEAR(steering->desiredVelocity.x, 0.1F, 0.00001F);
}

TEST(NavigationAgent2DTests, ReplansOnRevisionAndRejectsOtherGridOwners)
{
    auto grid = TestSupport::makeGrid(5, 3);
    auto other = TestSupport::makeGrid(5, 3);
    auto agent = NavigationAgent2D::Create({.cellCapacity = 15});
    ASSERT_TRUE(grid); ASSERT_TRUE(other); ASSERT_TRUE(agent);
    const Math::Vec2 position{0.5F, 1.5F};
    ASSERT_TRUE(agent->setGoal(*grid, position, {4.5F, 1.5F}));
    ASSERT_TRUE(agent->update(*grid, position, 0.1F, 15));
    ASSERT_TRUE(grid->addBlocker({2, 1, 1, 1}));
    auto replanned = agent->update(*grid, position, 0.1F, 15);
    ASSERT_TRUE(replanned);
    EXPECT_EQ(replanned->state, NavigationAgentState2D::Following);
    EXPECT_EQ(replanned->gridRevision, grid->revision());
    auto visible = hasNavigationWorldLineOfSight2D(*grid, position, replanned->targetPosition);
    ASSERT_TRUE(visible);
    EXPECT_TRUE(*visible);
    auto wrongOwner = agent->update(*other, position, 0.1F, 15);
    ASSERT_TRUE(wrongOwner);
    EXPECT_EQ(wrongOwner->state, NavigationAgentState2D::Invalidated);
    EXPECT_EQ(wrongOwner->desiredVelocity, Math::Vec2{});
}

TEST(NavigationAgent2DTests, CanFailClosedOnRevisionAndPreservesGoalAfterInvalidRequest)
{
    auto grid = TestSupport::makeGrid(3, 1);
    auto agent = NavigationAgent2D::Create({.cellCapacity = 3, .replanOnGridChange = false});
    ASSERT_TRUE(grid); ASSERT_TRUE(agent);
    ASSERT_TRUE(agent->setGoal(*grid, {0.5F, 0.5F}, {2.5F, 0.5F}));
    EXPECT_FALSE(agent->setGoal(*grid, {0.5F, 0.5F}, {9.5F, 0.5F}));
    auto valid = agent->update(*grid, {0.5F, 0.5F}, 0.1F, 3);
    ASSERT_TRUE(valid);
    EXPECT_EQ(valid->state, NavigationAgentState2D::Following);
    ASSERT_TRUE(grid->addBlocker({1, 0, 1, 1}));
    auto changed = agent->update(*grid, {0.5F, 0.5F}, 0.1F, 3);
    ASSERT_TRUE(changed);
    EXPECT_EQ(changed->state, NavigationAgentState2D::Invalidated);
    EXPECT_EQ(changed->desiredVelocity, Math::Vec2{});
}

TEST(NavigationAgent2DTests, UnreachableCancellationAndMovementWhilePlanningAreExplicit)
{
    const std::array blocked{NavigationCell2D{1, 0}};
    auto grid = TestSupport::makeGrid(3, 1, blocked);
    auto agent = NavigationAgent2D::Create({.cellCapacity = 12});
    ASSERT_TRUE(grid); ASSERT_TRUE(agent);
    ASSERT_TRUE(agent->setGoal(*grid, {0.5F, 0.5F}, {2.5F, 0.5F}));
    auto unreachable = agent->update(*grid, {0.5F, 0.5F}, 1.0F, 1);
    ASSERT_TRUE(unreachable);
    EXPECT_EQ(unreachable->state, NavigationAgentState2D::Unreachable);
    agent->cancel();
    auto cancelled = agent->update(*grid, {0.5F, 0.5F}, 1.0F, 1);
    ASSERT_TRUE(cancelled);
    EXPECT_EQ(cancelled->state, NavigationAgentState2D::Cancelled);
    auto open = TestSupport::makeGrid(4, 3);
    ASSERT_TRUE(open);
    ASSERT_TRUE(agent->setGoal(*open, {0.5F, 0.5F}, {3.5F, 2.5F}));
    ASSERT_TRUE(agent->update(*open, {0.5F, 0.5F}, 1.0F, 1));
    auto displaced = agent->update(*open, {3.5F, 2.5F}, 1.0F, 1);
    ASSERT_TRUE(displaced);
    EXPECT_EQ(displaced->state, NavigationAgentState2D::Arrived);
}

TEST(NavigationAgent2DTests, CreateAllocatesAllStorageAndMoveRetainsActiveGoal)
{
    TestSupport::SealedMemoryResource memory;
    auto grid = TestSupport::makeGrid(4, 4, {}, {}, {}, 1.0F, memory);
    auto agent = NavigationAgent2D::Create({.cellCapacity = 16}, memory);
    ASSERT_TRUE(grid); ASSERT_TRUE(agent);
    const auto allocations = memory.allocations();
    memory.seal();
    ASSERT_TRUE(agent->setGoal(*grid, {0.5F, 0.5F}, {3.5F, 3.5F}));
    NavigationAgent2D moved = std::move(*agent);
    EXPECT_FALSE(agent->update(*grid, {0.5F, 0.5F}, 1.0F, 16));
    ASSERT_TRUE(moved.update(*grid, {0.5F, 0.5F}, 1.0F, 16));
    ASSERT_TRUE(grid->addBlocker({1, 1, 1, 1}));
    ASSERT_TRUE(moved.update(*grid, {0.5F, 0.5F}, 1.0F, 16));
    EXPECT_EQ(memory.allocations(), allocations);
    EXPECT_FALSE(NavigationAgent2D::Create({.cellCapacity = 2}, *std::pmr::null_memory_resource()));
}

TEST(NavigationStorage2DTests, EveryFactoryAllocationFailureReturnsAnErrorAndReleasesPartialStorage)
{
    const auto verify = [](auto factory) {
        bool succeeded = false;
        constexpr Core::usize maximumFactoryAllocations = 64;
        for (Core::usize allowed = 0; allowed < maximumFactoryAllocations; ++allowed)
        {
            TestSupport::FailAfterMemoryResource memory{allowed};
            {
                auto owner = factory(memory);
                if (owner) { succeeded = true; }
                else { EXPECT_EQ(owner.error().code, Navigation2DErrorCode::AllocationFailed) << "allocation " << allowed; }
            }
            EXPECT_EQ(memory.liveBytes(), 0U) << "allocation " << allowed;
            if (succeeded) { break; }
        }
        EXPECT_TRUE(succeeded);
    };
    verify([](auto& memory) { return NavigationPathfinder2D::Create({.cellCapacity = 4}, memory); });
    verify([](auto& memory) { return NavigationPathSmoother2D::Create({.waypointCapacity = 4}, memory); });
    verify([](auto& memory) { return NavigationFlowField2D::Create({.cellCapacity = 4}, memory); });
    verify([](auto& memory) { return NavigationPathFollower2D::Create({.waypointCapacity = 4}, memory); });
    verify([](auto& memory) { return NavigationAgent2D::Create({.cellCapacity = 4}, memory); });
}

TEST(NavigationStorage2DTests, OwnersMoveWithoutAllocatingAndMovedFromResetIsSafe)
{
    TestSupport::SealedMemoryResource memory;
    auto grid = TestSupport::makeGrid(3, 3);
    auto search = NavigationPathfinder2D::Create({.cellCapacity = 9}, memory);
    auto smoother = NavigationPathSmoother2D::Create({.waypointCapacity = 9}, memory);
    auto field = NavigationFlowField2D::Create({.cellCapacity = 9}, memory);
    auto follower = NavigationPathFollower2D::Create({.waypointCapacity = 2}, memory);
    ASSERT_TRUE(grid); ASSERT_TRUE(search); ASSERT_TRUE(smoother); ASSERT_TRUE(field); ASSERT_TRUE(follower);
    ASSERT_TRUE(search->findPath(*grid, {0, 0}, {2, 2}));
    ASSERT_TRUE(smoother->smooth(*grid, search->path()));
    ASSERT_TRUE(field->build(*grid, {2, 2}));
    const std::array points{Math::Vec2{0.5F, 0.5F}, Math::Vec2{2.5F, 2.5F}};
    ASSERT_TRUE(follower->setPath(points));
    memory.seal();
    NavigationPathfinder2D movedSearch = std::move(*search);
    NavigationPathSmoother2D movedSmoother = std::move(*smoother);
    NavigationFlowField2D movedField = std::move(*field);
    NavigationPathFollower2D movedFollower = std::move(*follower);
    search->reset(); smoother->reset(); field->reset(); follower->reset();
    EXPECT_TRUE(search->path().empty()); EXPECT_TRUE(smoother->path().empty()); EXPECT_TRUE(follower->path().empty());
    EXPECT_FALSE(movedSearch.path().empty());
    EXPECT_EQ(movedSmoother.path().size(), 2U);
    EXPECT_TRUE(movedField.sample(*grid, {0, 0}));
    EXPECT_TRUE(movedFollower.update({0.5F, 0.5F}, 1.0F));
}

} // namespace
} // namespace Tina::Navigation2D
