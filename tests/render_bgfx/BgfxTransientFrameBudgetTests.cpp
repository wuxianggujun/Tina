#include "BgfxTransientFrameBudget.hpp"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <span>

namespace Tina::Render::Bgfx {
namespace {

TEST(BgfxTransientFrameBudgetTest, EmptyAndZeroCountRequestsReturnZeroBudget)
{
    auto empty = checkedTransientVertexBudget(std::span<const BgfxTransientVertexRequest>{});
    ASSERT_TRUE(empty.has_value());
    EXPECT_EQ(*empty, 0U);

    constexpr std::array<BgfxTransientVertexRequest, 3> zeroCountRequests{{
        {.count = 0U, .stride = 0U},
        {.count = 0U, .stride = 12U},
        {.count = 0U, .stride = 80U},
    }};
    auto zeroCount = checkedTransientVertexBudget(zeroCountRequests);
    ASSERT_TRUE(zeroCount.has_value());
    EXPECT_EQ(*zeroCount, 0U);
}

TEST(BgfxTransientFrameBudgetTest, SingleRequestIncludesAlignmentSlack)
{
    constexpr std::array<BgfxTransientVertexRequest, 1> requests{{
        {.count = 1U, .stride = 80U},
    }};

    auto budget = checkedTransientVertexBudget(requests);
    ASSERT_TRUE(budget.has_value());
    EXPECT_EQ(*budget, 159U);
}

TEST(BgfxTransientFrameBudgetTest, Combined3DAndUIRequestsReturnExpectedBudget)
{
    constexpr std::array<BgfxTransientVertexRequest, 2> requests{{
        {.count = 3U, .stride = 80U},
        {.count = 16U, .stride = 12U},
    }};

    auto budget = checkedTransientVertexBudget(requests);
    ASSERT_TRUE(budget.has_value());
    EXPECT_EQ(*budget, 522U);
}

TEST(BgfxTransientFrameBudgetTest, Combined3DSpriteAndUIRequestsReturnExpectedBudget)
{
    constexpr std::array<BgfxTransientVertexRequest, 3> requests{{
        {.count = 3U, .stride = 80U},
        {.count = 8U, .stride = 20U},
        {.count = 16U, .stride = 12U},
    }};

    auto budget = checkedTransientVertexBudget(requests);
    ASSERT_TRUE(budget.has_value());
    EXPECT_EQ(*budget, 701U);
}

TEST(BgfxTransientFrameBudgetTest, NonZeroCountWithZeroStrideIsInvalidArgument)
{
    constexpr std::array<BgfxTransientVertexRequest, 1> requests{{
        {.count = 1U, .stride = 0U},
    }};

    auto budget = checkedTransientVertexBudget(requests);
    ASSERT_FALSE(budget.has_value());
    EXPECT_EQ(budget.error().code, Core::CoreErrorCode::InvalidArgument);
}

TEST(BgfxTransientFrameBudgetTest, OverflowingProductIsCapacityExceeded)
{
    constexpr std::array<BgfxTransientVertexRequest, 1> requests{{
        {.count = (std::numeric_limits<u32>::max)(), .stride = 2U},
    }};

    auto budget = checkedTransientVertexBudget(requests);
    ASSERT_FALSE(budget.has_value());
    EXPECT_EQ(budget.error().code, Core::CoreErrorCode::CapacityExceeded);
}

TEST(BgfxTransientFrameBudgetTest, SummedRequestsOverflowIsCapacityExceeded)
{
    constexpr u32 HalfMaxCount = ((std::numeric_limits<u32>::max)() - 1U) / 2U;
    constexpr std::array<BgfxTransientVertexRequest, 2> requests{{
        {.count = HalfMaxCount, .stride = 2U},
        {.count = HalfMaxCount, .stride = 2U},
    }};

    auto budget = checkedTransientVertexBudget(requests);
    ASSERT_FALSE(budget.has_value());
    EXPECT_EQ(budget.error().code, Core::CoreErrorCode::CapacityExceeded);
}

TEST(BgfxTransientFrameBudgetTest, EmptyAndZeroTransientIndexCountsReturnZeroBudget)
{
    auto empty = checkedTransientIndexBudget(std::span<const u32>{});
    ASSERT_TRUE(empty.has_value());
    EXPECT_EQ(*empty, 0U);

    constexpr std::array<u32, 3> zeroCounts{0U, 0U, 0U};
    auto zeroBudget = checkedTransientIndexBudget(zeroCounts);
    ASSERT_TRUE(zeroBudget.has_value());
    EXPECT_EQ(*zeroBudget, 0U);
}

TEST(BgfxTransientFrameBudgetTest, CombinedSpriteAndUITransientIndexCountsAreSummed)
{
    constexpr std::array<u32, 2> indexCounts{12U, 18U};

    auto budget = checkedTransientIndexBudget(indexCounts);
    ASSERT_TRUE(budget.has_value());
    EXPECT_EQ(*budget, 30U);
}

TEST(BgfxTransientFrameBudgetTest, SummedTransientIndexCountsOverflowIsCapacityExceeded)
{
    constexpr std::array<u32, 2> indexCounts{(std::numeric_limits<u32>::max)(), 1U};

    auto budget = checkedTransientIndexBudget(indexCounts);
    ASSERT_FALSE(budget.has_value());
    EXPECT_EQ(budget.error().code, Core::CoreErrorCode::CapacityExceeded);
}

} // namespace
} // namespace Tina::Render::Bgfx
