#include <tina/navigation2d/NavigationErrors.hpp>
#include <tina/navigation2d/NavigationGrid2D.hpp>

#include "NavigationTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <memory_resource>
#include <utility>
#include <vector>

namespace Tina::Navigation2D {
namespace {

[[nodiscard]] NavigationGrid2DData makeData(
    Core::u32 width, Core::u32 height, std::span<const NavigationCell2D> blocked,
    std::pmr::memory_resource& memory)
{
    std::vector<Core::u8> flags(static_cast<Core::usize>(width) * height, 0U);
    std::vector<Core::u8> traversalCosts(
        static_cast<Core::usize>(width) * height,
        NavigationGrid2DContract::MinimumTraversalCost);
    for (const NavigationCell2D cell : blocked)
    {
        flags[static_cast<Core::usize>(cell.y) * width + cell.x] =
            NavigationGrid2DContract::CellBlocked;
    }
    auto data = NavigationGrid2DData::Create(
        NavigationGrid2DDataDesc{
            .widthCells = width,
            .heightCells = height,
            .cellSizeMeters = 0.5F,
            .cellFlags = flags,
            .traversalCosts = traversalCosts,
        },
        memory);
    EXPECT_TRUE(data.has_value()) << (data ? "" : data.error().message);
    return std::move(*data);
}

TEST(NavigationGrid2DDataTests, RejectsInvalidDimensionsFlagsAndTraversalCosts)
{
    std::pmr::unsynchronized_pool_resource memory;
    const std::array<Core::u8, 1> clear{0U};
    const std::array<Core::u8, 2> clearTwo{0U, 0U};
    const std::array<Core::u8, 1> unitCost{1U};
    const std::array<Core::u8, 2> unitCostTwo{1U, 1U};

    auto wrongFlagCount = NavigationGrid2DData::Create(
        NavigationGrid2DDataDesc{.widthCells = 2, .heightCells = 1, .cellFlags = clear,
                                 .traversalCosts = unitCostTwo}, memory);
    ASSERT_FALSE(wrongFlagCount.has_value());
    EXPECT_EQ(wrongFlagCount.error().code, Navigation2DErrorCode::InvalidData);

    auto wrongCostCount = NavigationGrid2DData::Create(
        NavigationGrid2DDataDesc{.widthCells = 2, .heightCells = 1, .cellFlags = clearTwo,
                                 .traversalCosts = unitCost}, memory);
    ASSERT_FALSE(wrongCostCount.has_value());
    EXPECT_EQ(wrongCostCount.error().code, Navigation2DErrorCode::InvalidData);

    const std::array<Core::u8, 1> reserved{2U};
    auto reservedFlags = NavigationGrid2DData::Create(
        NavigationGrid2DDataDesc{.widthCells = 1, .heightCells = 1, .cellFlags = reserved,
                                 .traversalCosts = unitCost}, memory);
    ASSERT_FALSE(reservedFlags.has_value());
    EXPECT_EQ(reservedFlags.error().code, Navigation2DErrorCode::InvalidData);

    const std::array<Core::u8, 1> zeroCost{0U};
    auto invalidCost = NavigationGrid2DData::Create(
        NavigationGrid2DDataDesc{.widthCells = 1, .heightCells = 1, .cellFlags = clear,
                                 .traversalCosts = zeroCost}, memory);
    ASSERT_FALSE(invalidCost.has_value());
    EXPECT_EQ(invalidCost.error().code, Navigation2DErrorCode::InvalidData);

    const std::array<Core::u8, 1> excessiveCost{
        static_cast<Core::u8>(NavigationGrid2DContract::MaximumTraversalCost + 1U)};
    auto excessiveTraversalCost = NavigationGrid2DData::Create(
        NavigationGrid2DDataDesc{.widthCells = 1, .heightCells = 1, .cellFlags = clear,
                                 .traversalCosts = excessiveCost}, memory);
    ASSERT_FALSE(excessiveTraversalCost.has_value());
    EXPECT_EQ(excessiveTraversalCost.error().code, Navigation2DErrorCode::InvalidData);
}

TEST(NavigationGrid2DDataTests, OwnsTraversalCostsAndPublishesMinimum)
{
    std::pmr::unsynchronized_pool_resource memory;
    std::array<Core::u8, 3> flags{};
    std::array<Core::u8, 3> costs{4U, 2U, NavigationGrid2DContract::MaximumTraversalCost};
    auto data = NavigationGrid2DData::Create(
        NavigationGrid2DDataDesc{.widthCells = 3, .heightCells = 1, .cellFlags = flags,
                                 .traversalCosts = costs}, memory);
    ASSERT_TRUE(data.has_value()) << data.error().message;

    flags[0] = NavigationGrid2DContract::CellBlocked;
    costs[0] = NavigationGrid2DContract::MinimumTraversalCost;
    EXPECT_EQ(data->minimumTraversalCost(), 2U);
    EXPECT_FALSE(data->blockedAt({0, 0}));
    EXPECT_EQ(data->traversalCostAt({0, 0}), 4U);
    EXPECT_EQ(data->traversalCostAt({2, 0}), NavigationGrid2DContract::MaximumTraversalCost);
    EXPECT_EQ(data->traversalCostAt({3, 0}), 0U);
}

TEST(NavigationGrid2DTests, DynamicBlockersAreGenerationSafeReferenceCountedAndRevisioned)
{
    std::pmr::unsynchronized_pool_resource memory;
    const std::array baseBlocked{NavigationCell2D{3, 1}};
    auto gridResult = NavigationGrid2D::Create(
        makeData(4, 3, baseBlocked, memory),
        NavigationGrid2DConfig{.initialBlockerReserve = 3}, memory);
    ASSERT_TRUE(gridResult.has_value()) << gridResult.error().message;
    NavigationGrid2D grid = std::move(*gridResult);
    EXPECT_EQ(grid.revision(), 1U);
    EXPECT_TRUE(grid.isBaseBlocked({3, 1}));
    EXPECT_TRUE(grid.isBlocked({99, 99}));

    auto first = grid.addBlocker({.x = 1, .y = 0, .width = 2, .height = 2});
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(grid.blockerRect(*first),
              (NavigationCellRect2D{.x = 1, .y = 0, .width = 2, .height = 2}));
    EXPECT_EQ(grid.revision(), 2U);
    EXPECT_EQ(grid.dynamicBlockerCountAt({1, 0}), 1U);
    EXPECT_EQ(grid.dynamicBlockerCountAt({2, 1}), 1U);

    auto second = grid.addBlocker({.x = 2, .y = 1, .width = 1, .height = 2});
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(grid.dynamicBlockerCountAt({2, 1}), 2U);
    EXPECT_EQ(grid.dynamicBlockerCount(), 2U);

    const Core::u64 beforeNoOp = grid.revision();
    ASSERT_TRUE(grid.updateBlocker(*second, {.x = 2, .y = 1, .width = 1, .height = 2}).has_value());
    EXPECT_EQ(grid.revision(), beforeNoOp);

    ASSERT_TRUE(grid.updateBlocker(*first, {.x = 0, .y = 2, .width = 2, .height = 1}).has_value());
    EXPECT_EQ(grid.blockerRect(*first),
              (NavigationCellRect2D{.x = 0, .y = 2, .width = 2, .height = 1}));
    EXPECT_EQ(grid.dynamicBlockerCountAt({1, 0}), 0U);
    EXPECT_EQ(grid.dynamicBlockerCountAt({2, 1}), 1U);
    EXPECT_EQ(grid.dynamicBlockerCountAt({0, 2}), 1U);

    ASSERT_TRUE(grid.removeBlocker(*second).has_value());
    EXPECT_FALSE(grid.containsBlocker(*second));
    EXPECT_FALSE(grid.blockerRect(*second).has_value());
    EXPECT_EQ(grid.dynamicBlockerCountAt({2, 1}), 0U);
    auto stale = grid.removeBlocker(*second);
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().code, Navigation2DErrorCode::InvalidBlocker);

    auto otherGridResult = NavigationGrid2D::Create(
        makeData(4, 3, {}, memory), NavigationGrid2DConfig{.initialBlockerReserve = 1}, memory);
    ASSERT_TRUE(otherGridResult.has_value());
    auto wrongOwner = otherGridResult->updateBlocker(*first, {.x = 0, .y = 0, .width = 1, .height = 1});
    ASSERT_FALSE(wrongOwner.has_value());
    EXPECT_EQ(wrongOwner.error().code, Navigation2DErrorCode::InvalidBlocker);
}

TEST(NavigationGrid2DTests, BlockersGrowBeyondTheReserveAndInvalidRectanglesAreTransactional)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto gridResult = NavigationGrid2D::Create(
        makeData(2, 2, {}, memory), NavigationGrid2DConfig{.initialBlockerReserve = 1}, memory);
    ASSERT_TRUE(gridResult.has_value());
    NavigationGrid2D grid = std::move(*gridResult);
    auto blocker = grid.addBlocker({.x = 0, .y = 0, .width = 1, .height = 1});
    ASSERT_TRUE(blocker.has_value());
    auto second = grid.addBlocker({.x = 1, .y = 1, .width = 1, .height = 1});
    ASSERT_TRUE(second.has_value());
    EXPECT_GE(grid.reservedBlockerSlots(), 2U);
    EXPECT_TRUE(grid.containsBlocker(*blocker));
    const Core::u64 revision = grid.revision();

    auto invalid = grid.updateBlocker(*blocker, {.x = 1, .y = 1, .width = 2, .height = 1});
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Navigation2DErrorCode::InvalidCell);
    EXPECT_TRUE(grid.isBlocked({0, 0}));
    EXPECT_TRUE(grid.isBlocked({1, 1}));
    EXPECT_EQ(grid.revision(), revision);
}

TEST(NavigationGrid2DTests, ZeroReserveGrowsAndOverlapCountsDoNotWrapAtTheOldLimit)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto grid = NavigationGrid2D::Create(
        makeData(1, 1, {}, memory), {.initialBlockerReserve = 0}, memory);
    ASSERT_TRUE(grid);
    EXPECT_EQ(grid->reservedBlockerSlots(), 0U);
    std::vector<NavigationBlockerId> blockers;
    constexpr Core::usize count = 65536;
    blockers.reserve(count);
    for (Core::usize index = 0; index < count; ++index) {
        auto blocker = grid->addBlocker({.width = 1, .height = 1});
        ASSERT_TRUE(blocker) << "blocker " << index;
        blockers.push_back(*blocker);
    }
    EXPECT_EQ(grid->dynamicBlockerCountAt({0, 0}), count);
    EXPECT_TRUE(grid->containsBlocker(blockers.front()));
    for (const auto blocker : blockers) {
        ASSERT_TRUE(grid->removeBlocker(blocker));
    }
    EXPECT_EQ(grid->dynamicBlockerCountAt({0, 0}), 0U);
    EXPECT_FALSE(grid->isBlocked({0, 0}));
}

TEST(NavigationGrid2DTests, GrowthFailurePreservesExistingHandlesOccupancyAndRevision)
{
    TestSupport::SealedMemoryResource memory;
    auto grid = NavigationGrid2D::Create(
        makeData(2, 1, {}, memory), {.initialBlockerReserve = 1}, memory);
    ASSERT_TRUE(grid);
    auto first = grid->addBlocker({.width = 1, .height = 1});
    ASSERT_TRUE(first);
    const auto revision = grid->revision();
    memory.seal();
    auto rejected = grid->addBlocker({.x = 1, .width = 1, .height = 1});
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, Core::CoreErrorCode::OutOfMemory);
    EXPECT_EQ(grid->revision(), revision);
    EXPECT_TRUE(grid->containsBlocker(*first));
    EXPECT_EQ(grid->dynamicBlockerCount(), 1U);
    EXPECT_FALSE(grid->isBlocked({1, 0}));
}

TEST(NavigationGrid2DTests, DataAndGridMovesDoNotAllocate)
{
    TestSupport::SealedMemoryResource dataMemory;
    auto data = makeData(2, 1, {}, dataMemory);
    const auto* flags = data.cellFlags().data();
    dataMemory.seal();
    NavigationGrid2DData movedData(std::move(data));
    EXPECT_FALSE(data);
    EXPECT_EQ(movedData.cellFlags().data(), flags);

    TestSupport::SealedMemoryResource gridMemory;
    auto grid = NavigationGrid2D::Create(std::move(movedData), {.initialBlockerReserve = 1}, gridMemory);
    ASSERT_TRUE(grid);
    auto blocker = grid->addBlocker({.width = 1, .height = 1});
    ASSERT_TRUE(blocker);
    gridMemory.seal();
    NavigationGrid2D movedGrid(std::move(*grid));
    EXPECT_FALSE(*grid);
    EXPECT_TRUE(movedGrid.containsBlocker(*blocker));
    EXPECT_EQ(movedGrid.dynamicBlockerCountAt({0, 0}), 1U);
}

TEST(NavigationGrid2DTests, FactoryAllocationFailuresReleaseEveryPartialOwner)
{
    const std::array<Core::u8, 1> flags{0};
    const std::array<Core::u8, 1> costs{1};
    bool reachedSuccess = false;
    for (Core::usize limit = 0; limit < 24 && !reachedSuccess; ++limit) {
        TestSupport::FailAfterMemoryResource memory(limit);
        {
            auto data = NavigationGrid2DData::Create(
                {.widthCells = 1, .heightCells = 1, .cellFlags = flags, .traversalCosts = costs}, memory);
            if (data) {
                auto grid = NavigationGrid2D::Create(std::move(*data), {.initialBlockerReserve = 1}, memory);
                reachedSuccess = grid.has_value();
            } else {
                EXPECT_EQ(data.error().code, Navigation2DErrorCode::AllocationFailed);
            }
        }
        EXPECT_EQ(memory.liveBytes(), 0U) << "allocation limit " << limit;
    }
    EXPECT_TRUE(reachedSuccess);
}

} // namespace
} // namespace Tina::Navigation2D
