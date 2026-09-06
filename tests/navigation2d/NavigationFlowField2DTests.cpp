#include <tina/navigation2d/NavigationFlowField2D.hpp>
#include <tina/navigation2d/NavigationErrors.hpp>

#include "NavigationTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <utility>

namespace Tina::Navigation2D {
namespace {

TEST(NavigationFlowField2DTests, MatchesForwardAStarCostFromEveryCellForEveryCornerPolicy)
{
    const std::array blocked{NavigationCell2D{1, 1}, NavigationCell2D{3, 2}};
    const std::array<Core::u8, 20> costs{1, 3, 16, 1, 2, 2, 1, 5, 2, 3, 1, 4, 1, 1, 2, 6, 1, 3, 1, 7};
    auto grid = TestSupport::makeGrid(5, 4, blocked, costs);
    auto field = NavigationFlowField2D::Create({.cellCapacity = 20});
    auto search = NavigationPathfinder2D::Create({.cellCapacity = 20});
    ASSERT_TRUE(grid); ASSERT_TRUE(field); ASSERT_TRUE(search);
    const NavigationCell2D goal{4, 3};
    for (const auto mode : {NavigationDiagonalMode2D::Disabled,
                           NavigationDiagonalMode2D::RequireClearAdjacentCells,
                           NavigationDiagonalMode2D::AllowCornerCutting})
    {
        ASSERT_TRUE(field->build(*grid, goal, {.diagonalMode = mode}));
        ASSERT_EQ(field->result().state, NavigationFlowFieldState2D::Ready);
        for (Core::u32 y = 0; y < 4; ++y)
        {
            for (Core::u32 x = 0; x < 5; ++x)
            {
                const NavigationCell2D cell{x, y};
                auto path = search->findPath(*grid, cell, goal, {.diagonalMode = mode});
                auto sample = field->sample(*grid, cell);
                ASSERT_TRUE(path); ASSERT_TRUE(sample);
                EXPECT_EQ(sample->reachable, path->state == NavigationPathQueryState::Reached) << x << ',' << y;
                if (!sample->reachable) { continue; }
                EXPECT_EQ(sample->cost, path->pathCost) << x << ',' << y;
                if (cell == goal)
                {
                    EXPECT_FALSE(sample->nextCell);
                    EXPECT_EQ(sample->direction, Math::Vec2{});
                }
                else
                {
                    ASSERT_TRUE(sample->nextCell);
                    auto next = field->sample(*grid, *sample->nextCell);
                    ASSERT_TRUE(next);
                    EXPECT_LT(next->cost, sample->cost);
                    EXPECT_NEAR(Math::length(sample->direction), 1.0F, 0.00001F);
                }
            }
        }
    }
}

TEST(NavigationFlowField2DTests, ReverseRelaxationChargesForwardDestinationNotPredecessor)
{
    const std::array<Core::u8, 3> costs{16, 2, 7};
    auto grid = TestSupport::makeGrid(3, 1, {}, costs);
    auto field = NavigationFlowField2D::Create({.cellCapacity = 3});
    ASSERT_TRUE(grid); ASSERT_TRUE(field);
    ASSERT_TRUE(field->build(*grid, {2, 0}));
    auto first = field->sample(*grid, {0, 0});
    auto middle = field->sample(*grid, {1, 0});
    ASSERT_TRUE(first); ASSERT_TRUE(middle);
    EXPECT_EQ(first->cost, 90U);
    EXPECT_EQ(middle->cost, 70U);
}

TEST(NavigationFlowField2DTests, BudgetedBuildDoesNotPublishPartialResultsAndIsDeterministic)
{
    auto grid = TestSupport::makeGrid(5, 5);
    auto incremental = NavigationFlowField2D::Create({.cellCapacity = 25});
    auto synchronous = NavigationFlowField2D::Create({.cellCapacity = 25});
    ASSERT_TRUE(grid); ASSERT_TRUE(incremental); ASSERT_TRUE(synchronous);
    ASSERT_TRUE(incremental->begin(*grid, {4, 4}));
    EXPECT_FALSE(incremental->advance(*grid, 0));
    EXPECT_FALSE(incremental->sample(*grid, {4, 4}));
    for (Core::usize count = 1; count <= 25; ++count)
    {
        auto step = incremental->advance(*grid, 1);
        ASSERT_TRUE(step);
        EXPECT_EQ(step->expandedNodes, count);
        if (count < 25) { EXPECT_EQ(step->reachableCells, 0U); }
    }
    ASSERT_TRUE(synchronous->build(*grid, {4, 4}));
    for (Core::u32 y = 0; y < 5; ++y)
    {
        for (Core::u32 x = 0; x < 5; ++x)
        {
            auto left = incremental->sample(*grid, {x, y});
            auto right = synchronous->sample(*grid, {x, y});
            ASSERT_TRUE(left); ASSERT_TRUE(right);
            EXPECT_EQ(left->cost, right->cost);
            EXPECT_EQ(left->nextCell, right->nextCell);
        }
    }
    EXPECT_EQ(incremental->result().reachableCells, 25U);
}

TEST(NavigationFlowField2DTests, InvalidRequestsPreserveFieldAndBlockedGoalHasNoReachableCells)
{
    const std::array blocked{NavigationCell2D{1, 1}};
    auto grid = TestSupport::makeGrid(3, 3, blocked);
    auto field = NavigationFlowField2D::Create({.cellCapacity = 9});
    ASSERT_TRUE(grid); ASSERT_TRUE(field);
    ASSERT_TRUE(field->build(*grid, {2, 2}));
    EXPECT_FALSE(field->begin(*grid, {3, 0}));
    EXPECT_FALSE(field->begin(*grid, {0, 0}, {.diagonalMode = static_cast<NavigationDiagonalMode2D>(99)}));
    EXPECT_TRUE(field->isCurrent(*grid));
    ASSERT_TRUE(field->build(*grid, {1, 1}));
    EXPECT_EQ(field->result().state, NavigationFlowFieldState2D::Ready);
    EXPECT_EQ(field->result().reachableCells, 0U);
    auto value = field->sample(*grid, {0, 0});
    ASSERT_TRUE(value);
    EXPECT_FALSE(value->reachable);
}

TEST(NavigationFlowField2DTests, CancellationAndGridMutationNeverExposeStaleDirections)
{
    auto grid = TestSupport::makeGrid(3, 3);
    auto other = TestSupport::makeGrid(3, 3);
    auto field = NavigationFlowField2D::Create({.cellCapacity = 9});
    ASSERT_TRUE(grid); ASSERT_TRUE(other); ASSERT_TRUE(field);
    EXPECT_FALSE(field->advance(*grid, 1));
    ASSERT_TRUE(field->begin(*grid, {2, 2}));
    EXPECT_EQ(field->cancel().state, NavigationFlowFieldState2D::Cancelled);
    auto cancelled = field->advance(*grid, 9);
    ASSERT_TRUE(cancelled);
    EXPECT_EQ(cancelled->state, NavigationFlowFieldState2D::Cancelled);
    ASSERT_TRUE(field->build(*grid, {2, 2}));
    EXPECT_FALSE(field->sample(*other, {0, 0}));
    ASSERT_TRUE(grid->addBlocker({1, 0, 1, 1}));
    auto stale = field->sample(*grid, {0, 0});
    ASSERT_FALSE(stale);
    EXPECT_EQ(stale.error().code, Navigation2DErrorCode::GridInvalidated);
    auto invalidated = field->advance(*grid, 1);
    ASSERT_TRUE(invalidated);
    EXPECT_EQ(invalidated->state, NavigationFlowFieldState2D::Invalidated);
    ASSERT_TRUE(field->build(*grid, {2, 2}));
    NavigationGrid2D moved = std::move(*grid);
    EXPECT_FALSE(field->sample(moved, {0, 0}));
}

TEST(NavigationFlowField2DTests, RebuildAndSamplingUseOnlyPreallocatedPmrStorage)
{
    TestSupport::SealedMemoryResource memory;
    auto grid = TestSupport::makeGrid(4, 4, {}, {}, {}, 1.0F, memory);
    auto field = NavigationFlowField2D::Create({.cellCapacity = 16}, memory);
    ASSERT_TRUE(grid); ASSERT_TRUE(field);
    const auto allocations = memory.allocations();
    memory.seal();
    for (Core::u32 goal = 0; goal < 4; ++goal)
    {
        ASSERT_TRUE(field->build(*grid, {goal, goal}));
        ASSERT_TRUE(field->sample(*grid, {0, 0}));
    }
    EXPECT_EQ(memory.allocations(), allocations);
    EXPECT_FALSE(NavigationFlowField2D::Create({.cellCapacity = 2}, *std::pmr::null_memory_resource()));
}

} // namespace
} // namespace Tina::Navigation2D
