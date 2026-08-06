#include <tina/navigation2d/NavigationErrors.hpp>
#include <tina/navigation2d/NavigationPathfinder2D.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <memory_resource>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace Tina::Navigation2D {
namespace {

class TrackingMemoryResource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] Core::usize allocationCount() const noexcept { return m_allocations; }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        void* value = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_allocations;
        return value;
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    Core::usize m_allocations = 0;
};

[[nodiscard]] NavigationGrid2D makeGrid(
    Core::u32 width, Core::u32 height, std::span<const NavigationCell2D> blocked,
    Core::usize blockerCapacity, std::pmr::memory_resource& memory)
{
    std::vector<Core::u8> flags(static_cast<Core::usize>(width) * height, 0U);
    for (const NavigationCell2D cell : blocked)
    {
        flags[static_cast<Core::usize>(cell.y) * width + cell.x] =
            NavigationGrid2DSchema::CellBlocked;
    }
    auto data = NavigationGrid2DData::Create(
        NavigationGrid2DDataDesc{.widthCells = width, .heightCells = height, .cellFlags = flags}, memory);
    EXPECT_TRUE(data.has_value()) << (data ? "" : data.error().message);
    auto grid = NavigationGrid2D::Create(
        std::move(*data), NavigationGrid2DConfig{.dynamicBlockerCapacity = blockerCapacity}, memory);
    EXPECT_TRUE(grid.has_value()) << (grid ? "" : grid.error().message);
    return std::move(*grid);
}

TEST(NavigationPathfinder2DTests, FindsStableShortestPathAroundBlockedCells)
{
    std::pmr::unsynchronized_pool_resource memory;
    const std::array blocked{NavigationCell2D{2, 0}, NavigationCell2D{2, 1}};
    NavigationGrid2D grid = makeGrid(5, 3, blocked, 2, memory);
    auto pathfinderResult = NavigationPathfinder2D::Create({.cellCapacity = 15}, memory);
    ASSERT_TRUE(pathfinderResult.has_value());
    NavigationPathfinder2D pathfinder = std::move(*pathfinderResult);

    auto result = pathfinder.findPath(grid, {0, 1}, {4, 1});
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->state, NavigationPathQueryState::Reached);
    const std::array expected{
        NavigationCell2D{0, 1}, NavigationCell2D{1, 1}, NavigationCell2D{1, 2},
        NavigationCell2D{2, 2}, NavigationCell2D{3, 2}, NavigationCell2D{3, 1},
        NavigationCell2D{4, 1},
    };
    ASSERT_EQ(pathfinder.path().size(), expected.size());
    for (Core::usize index = 0; index < expected.size(); ++index)
    {
        EXPECT_EQ(pathfinder.path()[index], expected[index]) << "path index " << index;
    }

    auto repeated = pathfinder.findPath(grid, {0, 1}, {4, 1});
    ASSERT_TRUE(repeated.has_value());
    EXPECT_EQ(repeated->state, NavigationPathQueryState::Reached);
    for (Core::usize index = 0; index < expected.size(); ++index)
    {
        EXPECT_EQ(pathfinder.path()[index], expected[index]) << "repeated path index " << index;
    }
}

TEST(NavigationPathfinder2DTests, ChoosesRowMajorPathWhenShortestAlternativesTie)
{
    std::pmr::unsynchronized_pool_resource memory;
    const std::array blocked{NavigationCell2D{1, 1}};
    NavigationGrid2D grid = makeGrid(3, 3, blocked, 1, memory);
    auto pathfinderResult = NavigationPathfinder2D::Create({.cellCapacity = 9}, memory);
    ASSERT_TRUE(pathfinderResult.has_value());
    NavigationPathfinder2D pathfinder = std::move(*pathfinderResult);

    auto result = pathfinder.findPath(grid, {0, 1}, {2, 1});
    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->state, NavigationPathQueryState::Reached);
    const std::array expected{
        NavigationCell2D{0, 1},
        NavigationCell2D{0, 0},
        NavigationCell2D{1, 0},
        NavigationCell2D{2, 0},
        NavigationCell2D{2, 1},
    };
    ASSERT_EQ(pathfinder.path().size(), expected.size());
    EXPECT_TRUE(std::equal(pathfinder.path().begin(), pathfinder.path().end(), expected.begin()));
}

TEST(NavigationPathfinder2DTests, UnreachableAndBlockedEndpointsAreTerminalResults)
{
    std::pmr::unsynchronized_pool_resource memory;
    const std::array wall{NavigationCell2D{1, 0}, NavigationCell2D{1, 1}, NavigationCell2D{1, 2}};
    NavigationGrid2D grid = makeGrid(3, 3, wall, 1, memory);
    auto pathfinderResult = NavigationPathfinder2D::Create({.cellCapacity = 9}, memory);
    ASSERT_TRUE(pathfinderResult.has_value());
    NavigationPathfinder2D pathfinder = std::move(*pathfinderResult);

    auto unreachable = pathfinder.findPath(grid, {0, 1}, {2, 1});
    ASSERT_TRUE(unreachable.has_value());
    EXPECT_EQ(unreachable->state, NavigationPathQueryState::Unreachable);
    EXPECT_TRUE(pathfinder.path().empty());

    auto blockedGoal = pathfinder.begin(grid, {0, 0}, {1, 0});
    ASSERT_TRUE(blockedGoal.has_value());
    EXPECT_EQ(blockedGoal->state, NavigationPathQueryState::Unreachable);
    EXPECT_EQ(blockedGoal->expandedNodes, 0U);
}

TEST(NavigationPathfinder2DTests, IncrementalCancellationAndGridMutationHaveDeterministicTerminalStates)
{
    std::pmr::unsynchronized_pool_resource memory;
    NavigationGrid2D grid = makeGrid(8, 8, {}, 2, memory);
    auto pathfinderResult = NavigationPathfinder2D::Create({.cellCapacity = 64}, memory);
    ASSERT_TRUE(pathfinderResult.has_value());
    NavigationPathfinder2D pathfinder = std::move(*pathfinderResult);

    auto started = pathfinder.begin(grid, {0, 0}, {7, 7});
    ASSERT_TRUE(started.has_value());
    ASSERT_EQ(started->state, NavigationPathQueryState::Pending);
    auto zeroBudget = pathfinder.advance(grid, 0);
    ASSERT_FALSE(zeroBudget.has_value());
    EXPECT_EQ(zeroBudget.error().code, Core::CoreErrorCode::InvalidArgument);
    EXPECT_EQ(pathfinder.result().state, NavigationPathQueryState::Pending);
    EXPECT_EQ(pathfinder.result().expandedNodes, 0U);
    auto partial = pathfinder.advance(grid, 1);
    ASSERT_TRUE(partial.has_value());
    EXPECT_EQ(partial->state, NavigationPathQueryState::Pending);
    EXPECT_EQ(partial->expandedNodes, 1U);
    EXPECT_EQ(pathfinder.cancel().state, NavigationPathQueryState::Cancelled);
    EXPECT_EQ(pathfinder.cancel().state, NavigationPathQueryState::Cancelled);
    auto afterCancel = pathfinder.advance(grid, 10);
    ASSERT_TRUE(afterCancel.has_value());
    EXPECT_EQ(afterCancel->state, NavigationPathQueryState::Cancelled);

    ASSERT_TRUE(pathfinder.begin(grid, {0, 0}, {7, 7}).has_value());
    auto blocker = grid.addBlocker({.x = 3, .y = 3, .width = 1, .height = 1});
    ASSERT_TRUE(blocker.has_value());
    auto invalidated = pathfinder.advance(grid, 1);
    ASSERT_TRUE(invalidated.has_value());
    EXPECT_EQ(invalidated->state, NavigationPathQueryState::Invalidated);
    EXPECT_TRUE(pathfinder.path().empty());

    NavigationGrid2D otherGrid = makeGrid(8, 8, {}, 1, memory);
    auto otherBlocker = otherGrid.addBlocker({.x = 3, .y = 3, .width = 1, .height = 1});
    ASSERT_TRUE(otherBlocker.has_value());
    ASSERT_EQ(otherGrid.revision(), grid.revision());
    ASSERT_TRUE(pathfinder.begin(grid, {0, 0}, {7, 7}).has_value());
    auto wrongGridAddress = pathfinder.advance(otherGrid, 1);
    ASSERT_TRUE(wrongGridAddress.has_value());
    EXPECT_EQ(wrongGridAddress->state, NavigationPathQueryState::Invalidated);
}

TEST(NavigationPathfinder2DTests, CapacityFailurePreservesPreviousCompletedQuery)
{
    std::pmr::unsynchronized_pool_resource memory;
    NavigationGrid2D small = makeGrid(2, 2, {}, 1, memory);
    NavigationGrid2D large = makeGrid(3, 3, {}, 1, memory);
    auto pathfinderResult = NavigationPathfinder2D::Create({.cellCapacity = 4}, memory);
    ASSERT_TRUE(pathfinderResult.has_value());
    NavigationPathfinder2D pathfinder = std::move(*pathfinderResult);
    auto reached = pathfinder.findPath(small, {0, 0}, {1, 0});
    ASSERT_TRUE(reached.has_value());
    ASSERT_EQ(reached->state, NavigationPathQueryState::Reached);
    const std::vector previous(pathfinder.path().begin(), pathfinder.path().end());

    auto tooLarge = pathfinder.begin(large, {0, 0}, {2, 2});
    ASSERT_FALSE(tooLarge.has_value());
    EXPECT_EQ(tooLarge.error().code, NavigationErrorCode::CapacityExceeded);
    EXPECT_EQ(pathfinder.result().state, NavigationPathQueryState::Reached);
    EXPECT_TRUE(std::equal(pathfinder.path().begin(), pathfinder.path().end(), previous.begin()));
}

TEST(NavigationPathfinder2DTests, SuccessfulQueriesAndBlockerMutationsDoNotAllocateAfterCreate)
{
    TrackingMemoryResource memory;
    NavigationGrid2D grid = makeGrid(6, 6, {}, 2, memory);
    auto pathfinderResult = NavigationPathfinder2D::Create({.cellCapacity = 36}, memory);
    ASSERT_TRUE(pathfinderResult.has_value());
    NavigationPathfinder2D pathfinder = std::move(*pathfinderResult);
    const Core::usize allocationsAfterCreate = memory.allocationCount();

    auto blocker = grid.addBlocker({.x = 2, .y = 2, .width = 1, .height = 2});
    ASSERT_TRUE(blocker.has_value());
    ASSERT_TRUE(grid.updateBlocker(*blocker, {.x = 3, .y = 2, .width = 1, .height = 2}).has_value());
    auto result = pathfinder.findPath(grid, {0, 0}, {5, 5});
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->state, NavigationPathQueryState::Reached);
    ASSERT_TRUE(grid.removeBlocker(*blocker).has_value());
    EXPECT_EQ(memory.allocationCount(), allocationsAfterCreate);
}

} // namespace
} // namespace Tina::Navigation2D
