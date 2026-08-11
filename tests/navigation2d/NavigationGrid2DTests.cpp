#include <tina/navigation2d/NavigationErrors.hpp>
#include <tina/navigation2d/NavigationGrid2D.hpp>

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
    EXPECT_EQ(wrongFlagCount.error().code, NavigationErrorCode::InvalidData);

    auto wrongCostCount = NavigationGrid2DData::Create(
        NavigationGrid2DDataDesc{.widthCells = 2, .heightCells = 1, .cellFlags = clearTwo,
                                 .traversalCosts = unitCost}, memory);
    ASSERT_FALSE(wrongCostCount.has_value());
    EXPECT_EQ(wrongCostCount.error().code, NavigationErrorCode::InvalidData);

    const std::array<Core::u8, 1> reserved{2U};
    auto reservedFlags = NavigationGrid2DData::Create(
        NavigationGrid2DDataDesc{.widthCells = 1, .heightCells = 1, .cellFlags = reserved,
                                 .traversalCosts = unitCost}, memory);
    ASSERT_FALSE(reservedFlags.has_value());
    EXPECT_EQ(reservedFlags.error().code, NavigationErrorCode::InvalidData);

    const std::array<Core::u8, 1> zeroCost{0U};
    auto invalidCost = NavigationGrid2DData::Create(
        NavigationGrid2DDataDesc{.widthCells = 1, .heightCells = 1, .cellFlags = clear,
                                 .traversalCosts = zeroCost}, memory);
    ASSERT_FALSE(invalidCost.has_value());
    EXPECT_EQ(invalidCost.error().code, NavigationErrorCode::InvalidData);

    const std::array<Core::u8, 1> excessiveCost{
        static_cast<Core::u8>(NavigationGrid2DContract::MaximumTraversalCost + 1U)};
    auto excessiveTraversalCost = NavigationGrid2DData::Create(
        NavigationGrid2DDataDesc{.widthCells = 1, .heightCells = 1, .cellFlags = clear,
                                 .traversalCosts = excessiveCost}, memory);
    ASSERT_FALSE(excessiveTraversalCost.has_value());
    EXPECT_EQ(excessiveTraversalCost.error().code, NavigationErrorCode::InvalidData);
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
        NavigationGrid2DConfig{.dynamicBlockerCapacity = 3}, memory);
    ASSERT_TRUE(gridResult.has_value()) << gridResult.error().message;
    NavigationGrid2D grid = std::move(*gridResult);
    EXPECT_EQ(grid.revision(), 1U);
    EXPECT_TRUE(grid.isBaseBlocked({3, 1}));
    EXPECT_TRUE(grid.isBlocked({99, 99}));

    auto first = grid.addBlocker({.x = 1, .y = 0, .width = 2, .height = 2});
    ASSERT_TRUE(first.has_value());
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
    EXPECT_EQ(grid.dynamicBlockerCountAt({1, 0}), 0U);
    EXPECT_EQ(grid.dynamicBlockerCountAt({2, 1}), 1U);
    EXPECT_EQ(grid.dynamicBlockerCountAt({0, 2}), 1U);

    ASSERT_TRUE(grid.removeBlocker(*second).has_value());
    EXPECT_FALSE(grid.containsBlocker(*second));
    EXPECT_EQ(grid.dynamicBlockerCountAt({2, 1}), 0U);
    auto stale = grid.removeBlocker(*second);
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().code, NavigationErrorCode::InvalidBlocker);

    auto otherGridResult = NavigationGrid2D::Create(
        makeData(4, 3, {}, memory), NavigationGrid2DConfig{.dynamicBlockerCapacity = 1}, memory);
    ASSERT_TRUE(otherGridResult.has_value());
    auto wrongOwner = otherGridResult->updateBlocker(*first, {.x = 0, .y = 0, .width = 1, .height = 1});
    ASSERT_FALSE(wrongOwner.has_value());
    EXPECT_EQ(wrongOwner.error().code, NavigationErrorCode::InvalidBlocker);
}

TEST(NavigationGrid2DTests, CapacityAndRectangleValidationAreTransactional)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto gridResult = NavigationGrid2D::Create(
        makeData(2, 2, {}, memory), NavigationGrid2DConfig{.dynamicBlockerCapacity = 1}, memory);
    ASSERT_TRUE(gridResult.has_value());
    NavigationGrid2D grid = std::move(*gridResult);
    auto blocker = grid.addBlocker({.x = 0, .y = 0, .width = 1, .height = 1});
    ASSERT_TRUE(blocker.has_value());
    const Core::u64 revision = grid.revision();

    auto full = grid.addBlocker({.x = 1, .y = 1, .width = 1, .height = 1});
    ASSERT_FALSE(full.has_value());
    EXPECT_EQ(full.error().code, NavigationErrorCode::CapacityExceeded);
    EXPECT_EQ(grid.revision(), revision);
    EXPECT_FALSE(grid.isBlocked({1, 1}));

    auto invalid = grid.updateBlocker(*blocker, {.x = 1, .y = 1, .width = 2, .height = 1});
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, NavigationErrorCode::InvalidCell);
    EXPECT_TRUE(grid.isBlocked({0, 0}));
    EXPECT_FALSE(grid.isBlocked({1, 1}));
    EXPECT_EQ(grid.revision(), revision);
}

} // namespace
} // namespace Tina::Navigation2D
