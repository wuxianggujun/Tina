#include "NavigationTestSupport3D.hpp"

#include <tina/navigation3d/NavigationErrors3D.hpp>
#include <tina/navigation3d/NavigationPathfinder3D.hpp>

#include <gtest/gtest.h>

#include <algorithm>

namespace Tina::Navigation3D {
namespace {

using TestSupport::VolumeBuilder;
using TestSupport::WalkerProfile;
using TestSupport::makeFlatVolume;

[[nodiscard]] NavigationPathfinder3D makePathfinder(Core::usize capacity)
{
    auto pathfinder = NavigationPathfinder3D::Create({.cellCapacity = capacity});
    EXPECT_TRUE(pathfinder.has_value());
    return std::move(*pathfinder);
}

[[nodiscard]] NavigationPathQueryOptions3D walkerOptions(
    NavigationDiagonalMode3D mode = NavigationDiagonalMode3D::Disabled)
{
    return NavigationPathQueryOptions3D{.diagonalMode = mode, .agent = WalkerProfile};
}

TEST(NavigationPathfinder3DTest, RejectsCapacityOutsideTheSupportedRange)
{
    const auto zero = NavigationPathfinder3D::Create({.cellCapacity = 0});
    ASSERT_FALSE(zero.has_value());
    EXPECT_EQ(zero.error().code, Navigation3DErrorCode::CapacityExceeded);

    const auto tooLarge = NavigationPathfinder3D::Create({
        .cellCapacity = NavigationVolume3DContract::MaximumCellCount + 1});
    ASSERT_FALSE(tooLarge.has_value());
    EXPECT_EQ(tooLarge.error().code, Navigation3DErrorCode::CapacityExceeded);
}

TEST(NavigationPathfinder3DTest, WalksAStraightLineAcrossFlatGround)
{
    const auto volume = makeFlatVolume(6, 4, 6);
    ASSERT_TRUE(volume.has_value());
    auto pathfinder = makePathfinder(volume->cellCount());

    const auto result = pathfinder.findPath(*volume, {1, 1, 1}, {4, 1, 1}, walkerOptions());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->state, NavigationPathQueryState3D::Reached);
    EXPECT_EQ(result->pathCellCount, 4U);
    EXPECT_EQ(result->pathCost, 3U * NavigationPathCost3D::Cardinal);
    EXPECT_EQ(result->verticalTransitions, 0U);
    ASSERT_EQ(pathfinder.path().size(), 4U);
    EXPECT_EQ(pathfinder.path().front(), (NavigationCell3D{1, 1, 1}));
    EXPECT_EQ(pathfinder.path().back(), (NavigationCell3D{4, 1, 1}));
}

TEST(NavigationPathfinder3DTest, AStartOrGoalNoAgentCanOccupyIsNotStandableRatherThanUnreachable)
{
    const auto volume = makeFlatVolume(6, 4, 6);
    ASSERT_TRUE(volume.has_value());
    auto pathfinder = makePathfinder(volume->cellCount());

    // Mid-air: inside the volume, but nothing supports it.
    const auto floatingStart = pathfinder.findPath(*volume, {1, 3, 1}, {4, 1, 1}, walkerOptions());
    ASSERT_FALSE(floatingStart.has_value());
    EXPECT_EQ(floatingStart.error().code, Navigation3DErrorCode::NotStandable);

    // Inside the floor itself.
    const auto buriedGoal = pathfinder.findPath(*volume, {1, 1, 1}, {4, 0, 1}, walkerOptions());
    ASSERT_FALSE(buriedGoal.has_value());
    EXPECT_EQ(buriedGoal.error().code, Navigation3DErrorCode::NotStandable);

    // Outside the volume is a different failure with a different cause.
    const auto outside = pathfinder.findPath(*volume, {1, 1, 1}, {9, 1, 1}, walkerOptions());
    ASSERT_FALSE(outside.has_value());
    EXPECT_EQ(outside.error().code, Navigation3DErrorCode::InvalidCell);
}

TEST(NavigationPathfinder3DTest, RejectsAnInvalidOrOversizedAgentProfile)
{
    const auto volume = makeFlatVolume(6, 4, 6);
    ASSERT_TRUE(volume.has_value());
    auto pathfinder = makePathfinder(volume->cellCount());

    const auto zeroHeight = pathfinder.findPath(*volume, {1, 1, 1}, {4, 1, 1},
        {.agent = {.heightCells = 0}});
    ASSERT_FALSE(zeroHeight.has_value());
    EXPECT_EQ(zeroHeight.error().code, Navigation3DErrorCode::InvalidAgentProfile);

    // Valid in isolation, but taller than this volume: reported against the profile, not
    // as a routing failure.
    const auto tooTallForVolume = pathfinder.findPath(*volume, {1, 1, 1}, {4, 1, 1},
        {.agent = {.heightCells = 8, .maxStepUpCells = 1, .maxFallCells = 1}});
    ASSERT_FALSE(tooTallForVolume.has_value());
    EXPECT_EQ(tooTallForVolume.error().code, Navigation3DErrorCode::InvalidAgentProfile);
}

// The staircase is the whole point of 3D navigation: a route that only exists if the
// solver can climb.
TEST(NavigationPathfinder3DTest, ClimbsAStaircaseWhenTheOnlyRouteGoesUp)
{
    // A wall across x=2 with a one-cell staircase, forcing the route over it.
    VolumeBuilder builder(6, 6, 3);
    builder.solidLayer(0);
    for (Core::u32 z = 0; z < 3; ++z)
    {
        builder.solidColumn(2, z, 1, 1);
    }
    const auto volume = builder.build();
    ASSERT_TRUE(volume.has_value());
    auto pathfinder = makePathfinder(volume->cellCount());

    const auto result = pathfinder.findPath(*volume, {1, 1, 1}, {4, 1, 1}, walkerOptions());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->state, NavigationPathQueryState3D::Reached);
    // Up onto the wall then back down: two height changes.
    EXPECT_EQ(result->verticalTransitions, 2U);
    const auto path = pathfinder.path();
    EXPECT_TRUE(std::any_of(path.begin(), path.end(),
                            [](NavigationCell3D cell) { return cell.y == 2; }));
}

TEST(NavigationPathfinder3DTest, AStepTallerThanTheAgentCanClimbIsNotARoute)
{
    // A two-cell wall. A maxStepUpCells = 1 walker cannot pass; a 2-step climber can.
    VolumeBuilder builder(6, 8, 3);
    builder.solidLayer(0);
    for (Core::u32 z = 0; z < 3; ++z)
    {
        builder.solidColumn(2, z, 1, 2);
    }
    const auto volume = builder.build();
    ASSERT_TRUE(volume.has_value());
    auto pathfinder = makePathfinder(volume->cellCount());

    const auto walker = pathfinder.findPath(*volume, {1, 1, 1}, {4, 1, 1}, walkerOptions());
    ASSERT_TRUE(walker.has_value());
    EXPECT_EQ(walker->state, NavigationPathQueryState3D::Unreachable);

    const auto climber = pathfinder.findPath(*volume, {1, 1, 1}, {4, 1, 1},
        {.agent = {.heightCells = 2, .maxStepUpCells = 2, .maxFallCells = 3}});
    ASSERT_TRUE(climber.has_value());
    EXPECT_EQ(climber->state, NavigationPathQueryState3D::Reached);
}

TEST(NavigationPathfinder3DTest, ADropDeeperThanTheAgentCanSurviveIsNotARoute)
{
    // A solid plateau filling y=1..4 over x=0..2, so its walkable surface is y=5. The pit
    // side keeps only the y=0 floor, so its surface is y=1: a drop of exactly 4 cells, with
    // no staircase for a gradual descent.
    VolumeBuilder builder(6, 8, 3);
    builder.solidLayer(0);
    for (Core::u32 z = 0; z < 3; ++z)
    {
        for (Core::u32 x = 0; x < 3; ++x)
        {
            builder.solidColumn(x, z, 1, 4);
        }
    }
    const auto volume = builder.build();
    ASSERT_TRUE(volume.has_value());
    ASSERT_TRUE(volume->isStandable({1, 5, 1}, WalkerProfile));
    ASSERT_TRUE(volume->isStandable({4, 1, 1}, WalkerProfile));
    auto pathfinder = makePathfinder(volume->cellCount());

    const auto shortFall = pathfinder.findPath(*volume, {1, 5, 1}, {4, 1, 1}, walkerOptions());
    ASSERT_TRUE(shortFall.has_value());
    EXPECT_EQ(shortFall->state, NavigationPathQueryState3D::Unreachable);

    // The same geometry with one more cell of fall allowance is a single-hop route, which
    // is what proves the refusal above came from the allowance and not from the shape.
    const auto longFall = pathfinder.findPath(*volume, {1, 5, 1}, {4, 1, 1},
        {.agent = {.heightCells = 2, .maxStepUpCells = 1, .maxFallCells = 4}});
    ASSERT_TRUE(longFall.has_value());
    EXPECT_EQ(longFall->state, NavigationPathQueryState3D::Reached);
    EXPECT_EQ(longFall->verticalTransitions, 1U);
}

// ADR 0048 D7: the corner rule must consider the agent's full height, not just its feet.
TEST(NavigationPathfinder3DTest, ADiagonalIsRefusedWhenOnlyTheUpperBodyWouldClipTheCorner)
{
    // One block in the HEAD layer only (y=2) of the {2,_,1} column. Its foot cell at y=1
    // stays open, so a corner check that looked at the floor alone would happily cut this
    // corner and drag the walker's torso through the block.
    VolumeBuilder builder(4, 5, 4);
    builder.solidLayer(0);
    builder.solid(2, 2, 1);
    const auto volume = builder.build();
    ASSERT_TRUE(volume.has_value());
    auto pathfinder = makePathfinder(volume->cellCount());

    // The distinction the rule turns on: feet fit, the body does not.
    ASSERT_FALSE(volume->isSolid({2, 1, 1}));
    ASSERT_FALSE(volume->hasClearance({2, 1, 1}, WalkerProfile));
    // The other orthogonal column is fully clear, so a legal detour exists.
    ASSERT_TRUE(volume->isStandable({1, 1, 2}, WalkerProfile));

    const auto strict = pathfinder.findPath(*volume, {1, 1, 1}, {2, 1, 2},
        walkerOptions(NavigationDiagonalMode3D::RequireClearAdjacentColumns));
    ASSERT_TRUE(strict.has_value());
    EXPECT_EQ(strict->state, NavigationPathQueryState3D::Reached);
    // Forced around the corner rather than through it.
    EXPECT_EQ(strict->pathCellCount, 3U);

    const auto cutting = pathfinder.findPath(*volume, {1, 1, 1}, {2, 1, 2},
        walkerOptions(NavigationDiagonalMode3D::AllowCornerCutting));
    ASSERT_TRUE(cutting.has_value());
    EXPECT_EQ(cutting->state, NavigationPathQueryState3D::Reached);
    EXPECT_EQ(cutting->pathCellCount, 2U);
    EXPECT_EQ(cutting->pathCost, NavigationPathCost3D::Diagonal);
}

TEST(NavigationPathfinder3DTest, DiagonalMovementIsRefusedEntirelyWhenDisabled)
{
    const auto volume = makeFlatVolume(4, 4, 4);
    ASSERT_TRUE(volume.has_value());
    auto pathfinder = makePathfinder(volume->cellCount());

    const auto result = pathfinder.findPath(*volume, {1, 1, 1}, {2, 1, 2}, walkerOptions());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->state, NavigationPathQueryState3D::Reached);
    EXPECT_EQ(result->pathCellCount, 3U);
    EXPECT_EQ(result->pathCost, 2U * NavigationPathCost3D::Cardinal);
}

TEST(NavigationPathfinder3DTest, TerrainCostSteersTheRouteAroundExpensiveGround)
{
    // A corridor where the direct line is expensive; the detour is longer but cheaper.
    VolumeBuilder builder(5, 4, 3);
    builder.solidLayer(0);
    for (Core::u32 x = 1; x <= 3; ++x)
    {
        builder.cost(x, 1, 1, 8);
    }
    const auto volume = builder.build();
    ASSERT_TRUE(volume.has_value());
    auto pathfinder = makePathfinder(volume->cellCount());

    const auto result = pathfinder.findPath(*volume, {0, 1, 1}, {4, 1, 1}, walkerOptions());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->state, NavigationPathQueryState3D::Reached);
    const auto path = pathfinder.path();
    EXPECT_TRUE(std::any_of(path.begin(), path.end(),
                            [](NavigationCell3D cell) { return cell.z != 1; }))
        << "route should leave the expensive middle row";
}

TEST(NavigationPathfinder3DTest, AGoalEqualToTheStartReachesWithASingleCell)
{
    const auto volume = makeFlatVolume(4, 4, 4);
    ASSERT_TRUE(volume.has_value());
    auto pathfinder = makePathfinder(volume->cellCount());

    const auto result = pathfinder.findPath(*volume, {1, 1, 1}, {1, 1, 1}, walkerOptions());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->state, NavigationPathQueryState3D::Reached);
    EXPECT_EQ(result->pathCellCount, 1U);
    EXPECT_EQ(result->pathCost, 0U);
    EXPECT_EQ(result->verticalTransitions, 0U);
}

TEST(NavigationPathfinder3DTest, SteppedExpansionReachesTheSameAnswerAsASingleCall)
{
    const auto volume = makeFlatVolume(8, 4, 8);
    ASSERT_TRUE(volume.has_value());
    auto stepped = makePathfinder(volume->cellCount());
    auto direct = makePathfinder(volume->cellCount());

    const auto begun = stepped.begin(*volume, {0, 1, 0}, {7, 1, 7}, walkerOptions());
    ASSERT_TRUE(begun.has_value());
    EXPECT_EQ(begun->state, NavigationPathQueryState3D::Pending);
    NavigationPathQueryResult3D result{};
    for (Core::usize guard = 0; guard < volume->cellCount(); ++guard)
    {
        const auto step = stepped.advance(*volume, 1);
        ASSERT_TRUE(step.has_value());
        result = *step;
        if (result.state != NavigationPathQueryState3D::Pending) { break; }
    }
    EXPECT_EQ(result.state, NavigationPathQueryState3D::Reached);

    const auto once = direct.findPath(*volume, {0, 1, 0}, {7, 1, 7}, walkerOptions());
    ASSERT_TRUE(once.has_value());
    EXPECT_EQ(once->pathCost, result.pathCost);
    EXPECT_EQ(once->pathCellCount, result.pathCellCount);
    EXPECT_TRUE(std::equal(stepped.path().begin(), stepped.path().end(), direct.path().begin(),
                           direct.path().end()));
}

TEST(NavigationPathfinder3DTest, AZeroExpansionBudgetIsRejectedWithoutTerminatingTheQuery)
{
    const auto volume = makeFlatVolume(6, 4, 6);
    ASSERT_TRUE(volume.has_value());
    auto pathfinder = makePathfinder(volume->cellCount());
    ASSERT_TRUE(pathfinder.begin(*volume, {0, 1, 0}, {5, 1, 5}, walkerOptions()).has_value());

    const auto rejected = pathfinder.advance(*volume, 0);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, Core::CoreErrorCode::InvalidArgument);
    EXPECT_EQ(pathfinder.result().state, NavigationPathQueryState3D::Pending);
}

TEST(NavigationPathfinder3DTest, AdvancingWithoutBeginningReportsQueryNotStarted)
{
    const auto volume = makeFlatVolume(4, 4, 4);
    ASSERT_TRUE(volume.has_value());
    auto pathfinder = makePathfinder(volume->cellCount());

    const auto advanced = pathfinder.advance(*volume, 4);
    ASSERT_FALSE(advanced.has_value());
    EXPECT_EQ(advanced.error().code, Navigation3DErrorCode::QueryNotStarted);
}

// This is the case a voxel game hits constantly: a block changes while a query is in
// flight. A stale answer would route an agent through a wall that now exists.
TEST(NavigationPathfinder3DTest, MutatingTheVolumeMidQueryInvalidatesItInsteadOfAnsweringStale)
{
    auto volume = makeFlatVolume(8, 4, 8);
    ASSERT_TRUE(volume.has_value());
    auto pathfinder = makePathfinder(volume->cellCount());
    ASSERT_TRUE(pathfinder.begin(*volume, {0, 1, 0}, {7, 1, 7}, walkerOptions()).has_value());
    const auto stepped = pathfinder.advance(*volume, 1);
    ASSERT_TRUE(stepped.has_value());
    ASSERT_EQ(stepped->state, NavigationPathQueryState3D::Pending);

    const auto blocker = volume->addBlocker({.x = 4, .y = 1, .z = 4,
                                             .width = 1, .height = 1, .depth = 1});
    ASSERT_TRUE(blocker.has_value());

    const auto invalidated = pathfinder.advance(*volume, 64);
    ASSERT_TRUE(invalidated.has_value());
    EXPECT_EQ(invalidated->state, NavigationPathQueryState3D::Invalidated);
    EXPECT_TRUE(pathfinder.path().empty());

    // Replanning against the mutated volume succeeds and now avoids the new block.
    const auto replanned = pathfinder.findPath(*volume, {0, 1, 0}, {7, 1, 7}, walkerOptions());
    ASSERT_TRUE(replanned.has_value());
    EXPECT_EQ(replanned->state, NavigationPathQueryState3D::Reached);
    const auto path = pathfinder.path();
    EXPECT_FALSE(std::any_of(path.begin(), path.end(), [](NavigationCell3D cell) {
        return cell == NavigationCell3D{4, 1, 4};
    }));
}

TEST(NavigationPathfinder3DTest, CancellingIsAbsorbingUntilTheNextBegin)
{
    const auto volume = makeFlatVolume(8, 4, 8);
    ASSERT_TRUE(volume.has_value());
    auto pathfinder = makePathfinder(volume->cellCount());
    ASSERT_TRUE(pathfinder.begin(*volume, {0, 1, 0}, {7, 1, 7}, walkerOptions()).has_value());

    EXPECT_EQ(pathfinder.cancel().state, NavigationPathQueryState3D::Cancelled);
    EXPECT_EQ(pathfinder.cancel().state, NavigationPathQueryState3D::Cancelled);
    const auto afterCancel = pathfinder.advance(*volume, 8);
    ASSERT_TRUE(afterCancel.has_value());
    EXPECT_EQ(afterCancel->state, NavigationPathQueryState3D::Cancelled);

    const auto restarted = pathfinder.findPath(*volume, {0, 1, 0}, {7, 1, 7}, walkerOptions());
    ASSERT_TRUE(restarted.has_value());
    EXPECT_EQ(restarted->state, NavigationPathQueryState3D::Reached);
}

TEST(NavigationPathfinder3DTest, AVolumeLargerThanTheFixedCapacityIsRejected)
{
    const auto volume = makeFlatVolume(8, 4, 8);
    ASSERT_TRUE(volume.has_value());
    auto pathfinder = makePathfinder(volume->cellCount() - 1);

    const auto result = pathfinder.begin(*volume, {0, 1, 0}, {7, 1, 7}, walkerOptions());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, Navigation3DErrorCode::CapacityExceeded);
}

TEST(NavigationPathfinder3DTest, PathfinderCreationReportsAllocationFailureRatherThanTerminating)
{
    for (Core::usize limit = 0; limit < 4; ++limit)
    {
        TestSupport::FailAfterMemoryResource memory{limit};
        const auto pathfinder = NavigationPathfinder3D::Create({.cellCapacity = 512}, memory);
        if (!pathfinder.has_value())
        {
            EXPECT_EQ(pathfinder.error().code, Navigation3DErrorCode::AllocationFailed);
            EXPECT_EQ(memory.liveBytes(), 0U) << "failed create leaked at limit " << limit;
        }
    }
}

TEST(NavigationPathfinder3DTest, RepeatedQueriesAfterCreateDoNotAllocate)
{
    TestSupport::SealedMemoryResource memory;
    const auto volume = makeFlatVolume(8, 4, 8, memory);
    ASSERT_TRUE(volume.has_value());
    auto pathfinder = NavigationPathfinder3D::Create({.cellCapacity = volume->cellCount()}, memory);
    ASSERT_TRUE(pathfinder.has_value());
    // One warm-up query so any lazily sized storage is already resident.
    ASSERT_TRUE(pathfinder->findPath(*volume, {0, 1, 0}, {7, 1, 7}, walkerOptions()).has_value());
    memory.seal();

    for (Core::u32 iteration = 0; iteration < 8; ++iteration)
    {
        const auto result = pathfinder->findPath(*volume, {0, 1, 0}, {7, 1, 7}, walkerOptions());
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->state, NavigationPathQueryState3D::Reached);
    }
}

TEST(NavigationPathfinder3DTest, AMovedPathfinderLeavesTheSourceInertAndKeepsWorking)
{
    const auto volume = makeFlatVolume(6, 4, 6);
    ASSERT_TRUE(volume.has_value());
    auto source = makePathfinder(volume->cellCount());
    ASSERT_TRUE(source.findPath(*volume, {0, 1, 0}, {5, 1, 5}, walkerOptions()).has_value());

    NavigationPathfinder3D moved{std::move(source)};
    EXPECT_TRUE(source.path().empty());
    EXPECT_EQ(source.cellCapacity(), 0U);
    EXPECT_FALSE(moved.path().empty());

    const auto result = moved.findPath(*volume, {0, 1, 0}, {5, 1, 5}, walkerOptions());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->state, NavigationPathQueryState3D::Reached);
}

} // namespace
} // namespace Tina::Navigation3D
