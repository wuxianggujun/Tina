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

} // namespace
} // namespace Tina::Render::Bgfx
