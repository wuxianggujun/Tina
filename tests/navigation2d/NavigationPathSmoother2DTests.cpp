#include <tina/navigation2d/NavigationPathSmoother2D.hpp>
#include <tina/navigation2d/NavigationErrors.hpp>

#include "NavigationTestSupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace Tina::Navigation2D {
namespace {

TEST(NavigationCoordinates2DTests, ConvertsCellCentersAndUsesHalfOpenWorldBounds)
{
    auto grid = TestSupport::makeGrid(4, 3, {}, {}, {-2.0F, 3.0F}, 0.5F);
    ASSERT_TRUE(grid);
    const auto center = grid->cellCenter({1, 2});
    ASSERT_TRUE(center);
    EXPECT_EQ(*center, (Math::Vec2{-1.25F, 4.25F}));
    EXPECT_EQ(grid->worldToCell(*center), (NavigationCell2D{1, 2}));
    EXPECT_EQ(grid->worldToCell({-2.0F, 3.0F}), (NavigationCell2D{0, 0}));
    EXPECT_FALSE(grid->worldToCell({0.0F, 3.0F}));
    EXPECT_FALSE(grid->worldToCell({-2.0F, 4.5F}));
    EXPECT_FALSE(grid->worldToCell({std::nextafter(-2.0F, -3.0F), 3.0F}));
    EXPECT_FALSE(grid->cellCenter({4, 0}));
    EXPECT_FALSE(grid->worldToCell({(std::numeric_limits<float>::infinity)(), 3.0F}));
    EXPECT_FALSE(grid->worldToCell({-2.0F, (std::numeric_limits<float>::quiet_NaN)()}));
    EXPECT_FALSE(grid->worldToCell({(std::numeric_limits<float>::max)(), 3.0F}));
    NavigationGrid2D moved = std::move(*grid);
    EXPECT_FALSE(grid->worldToCell({-2.0F, 3.0F}));
    EXPECT_FALSE(grid->cellCenter({0, 0}));
    EXPECT_TRUE(moved.worldToCell(*center));
}

TEST(NavigationCoordinates2DTests, RejectsCentersWhichCannotRoundTripThroughFloat)
{
    auto grid = TestSupport::makeGrid(2, 1, {}, {}, {(std::numeric_limits<float>::max)(), 0.0F});
    ASSERT_TRUE(grid);
    EXPECT_FALSE(grid->cellCenter({1, 0}));
}

TEST(NavigationPathSmoother2DTests, StringPullsOpenStaircaseAndAcceptsItsOwnOutput)
{
    auto grid = TestSupport::makeGrid(5, 5);
    auto search = NavigationPathfinder2D::Create({.cellCapacity = 25});
    auto smoother = NavigationPathSmoother2D::Create({.waypointCapacity = 25});
    ASSERT_TRUE(grid); ASSERT_TRUE(search); ASSERT_TRUE(smoother);
    ASSERT_TRUE(search->findPath(*grid, {0, 0}, {4, 4}));
    ASSERT_GT(search->path().size(), 2U);
    ASSERT_TRUE(smoother->smooth(*grid, search->path()));
    ASSERT_EQ(smoother->path().size(), 2U);
    EXPECT_EQ(smoother->path().front(), (NavigationCell2D{0, 0}));
    EXPECT_EQ(smoother->path().back(), (NavigationCell2D{4, 4}));
    ASSERT_TRUE(smoother->smooth(*grid, smoother->path()));
    EXPECT_EQ(smoother->path().size(), 2U);
    const std::array repeated{NavigationCell2D{1, 1}, NavigationCell2D{1, 1}, NavigationCell2D{1, 1}};
    ASSERT_TRUE(smoother->smooth(*grid, repeated));
    EXPECT_EQ(smoother->path().size(), 1U);
}

TEST(NavigationPathSmoother2DTests, EnforcesCornerPolicyInBothDirectionsAndWorldSpace)
{
    const std::array blocked{NavigationCell2D{1, 0}};
    auto grid = TestSupport::makeGrid(3, 3, blocked);
    ASSERT_TRUE(grid);
    for (const bool reverse : {false, true})
    {
        const NavigationCell2D start = reverse ? NavigationCell2D{1, 1} : NavigationCell2D{0, 0};
        const NavigationCell2D goal = reverse ? NavigationCell2D{0, 0} : NavigationCell2D{1, 1};
        auto strict = hasNavigationLineOfSight2D(*grid, start, goal);
        auto cutting = hasNavigationLineOfSight2D(*grid, start, goal, NavigationDiagonalMode2D::AllowCornerCutting);
        auto cardinal = hasNavigationLineOfSight2D(*grid, start, goal, NavigationDiagonalMode2D::Disabled);
        ASSERT_TRUE(strict); ASSERT_TRUE(cutting); ASSERT_TRUE(cardinal);
        EXPECT_FALSE(*strict); EXPECT_TRUE(*cutting); EXPECT_FALSE(*cardinal);
    }
    auto world = hasNavigationWorldLineOfSight2D(*grid, {0.25F, 0.25F}, {2.75F, 2.75F});
    ASSERT_TRUE(world);
    EXPECT_FALSE(*world);
    // A vertical segment exactly on a blocked cell's left edge is not strict LOS.
    auto edge = hasNavigationWorldLineOfSight2D(*grid, {2.0F, 0.1F}, {2.0F, 2.9F});
    ASSERT_TRUE(edge);
    EXPECT_FALSE(*edge);
    EXPECT_FALSE(hasNavigationWorldLineOfSight2D(*grid, {-0.1F, 0.0F}, {0.5F, 0.5F}));
}

TEST(NavigationPathSmoother2DTests, DoesNotShortcutThroughExpensiveTerrainByDefault)
{
    std::array<Core::u8, 15> costs{};
    costs.fill(1);
    costs[7] = 16;
    auto grid = TestSupport::makeGrid(5, 3, {}, costs);
    auto search = NavigationPathfinder2D::Create({.cellCapacity = 15});
    auto smoother = NavigationPathSmoother2D::Create({.waypointCapacity = 15});
    ASSERT_TRUE(grid); ASSERT_TRUE(search); ASSERT_TRUE(smoother);
    ASSERT_TRUE(search->findPath(*grid, {0, 1}, {4, 1}));
    ASSERT_TRUE(smoother->smooth(*grid, search->path()));
    EXPECT_GT(smoother->path().size(), 2U);
    ASSERT_TRUE(smoother->smooth(*grid, search->path(), {.preserveTraversalCost = false}));
    EXPECT_EQ(smoother->path().size(), 2U);
}

TEST(NavigationPathSmoother2DTests, FailurePreservesPublicationAndMutationInvalidatesIt)
{
    const std::array blocked{NavigationCell2D{1, 1}};
    auto grid = TestSupport::makeGrid(3, 3, blocked);
    auto smoother = NavigationPathSmoother2D::Create({.waypointCapacity = 4});
    ASSERT_TRUE(grid); ASSERT_TRUE(smoother);
    const std::array valid{NavigationCell2D{0, 0}, NavigationCell2D{2, 0}};
    ASSERT_TRUE(smoother->smooth(*grid, valid));
    const std::array blockedSegment{NavigationCell2D{0, 1}, NavigationCell2D{2, 1}};
    auto invalid = smoother->smooth(*grid, blockedSegment);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, Navigation2DErrorCode::InvalidPath);
    EXPECT_TRUE(std::equal(smoother->path().begin(), smoother->path().end(), valid.begin(), valid.end()));
    EXPECT_FALSE(smoother->smooth(*grid, {}));
    const std::array tooLong{NavigationCell2D{}, NavigationCell2D{}, NavigationCell2D{}, NavigationCell2D{}, NavigationCell2D{}};
    EXPECT_FALSE(smoother->smooth(*grid, tooLong));
    EXPECT_TRUE(smoother->isCurrent(*grid));
    ASSERT_TRUE(grid->addBlocker({0, 0, 1, 1}));
    EXPECT_FALSE(smoother->isCurrent(*grid));
    EXPECT_FALSE(smoother->smooth(*grid, valid));
}

TEST(NavigationPathSmoother2DTests, UsesOnlyFixedPmrStorageAfterCreate)
{
    TestSupport::SealedMemoryResource memory;
    auto grid = TestSupport::makeGrid(8, 8, {}, {}, {}, 1.0F, memory);
    auto search = NavigationPathfinder2D::Create({.cellCapacity = 64}, memory);
    auto smoother = NavigationPathSmoother2D::Create({.waypointCapacity = 64}, memory);
    ASSERT_TRUE(grid); ASSERT_TRUE(search); ASSERT_TRUE(smoother);
    const auto allocations = memory.allocations();
    memory.seal();
    for (Core::u32 goal = 1; goal < 8; ++goal)
    {
        ASSERT_TRUE(search->findPath(*grid, {0, 0}, {goal, goal}));
        ASSERT_TRUE(smoother->smooth(*grid, search->path()));
    }
    EXPECT_EQ(memory.allocations(), allocations);
    EXPECT_FALSE(NavigationPathSmoother2D::Create({.waypointCapacity = 2}, *std::pmr::null_memory_resource()));
}

} // namespace
} // namespace Tina::Navigation2D
