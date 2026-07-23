#include "WindowVisibilityPacing.hpp"

#include <gtest/gtest.h>

namespace {

using Tina::Sample2D::MinimumProductUiVisibilityMilliseconds;
using Tina::Sample2D::productUiTargetElapsedMilliseconds;
using Tina::Sample2D::useProductUiVisibilityPacing;

TEST(Sample2DWindowVisibilityPacingTest, EnablesDeadlinePacingOnlyForZeroRequestedDelay)
{
    EXPECT_TRUE(useProductUiVisibilityPacing(0));
    EXPECT_FALSE(useProductUiVisibilityPacing(1));
    EXPECT_FALSE(useProductUiVisibilityPacing(16));
}

TEST(Sample2DWindowVisibilityPacingTest, SpreadsMinimumVisibilityAcrossTheFiniteFrameRun)
{
    EXPECT_EQ(productUiTargetElapsedMilliseconds(0, 300), 0U);
    EXPECT_EQ(productUiTargetElapsedMilliseconds(150, 300), 1100U);
    EXPECT_EQ(productUiTargetElapsedMilliseconds(300, 300), MinimumProductUiVisibilityMilliseconds);
    EXPECT_EQ(productUiTargetElapsedMilliseconds(301, 300), MinimumProductUiVisibilityMilliseconds);
    EXPECT_EQ(productUiTargetElapsedMilliseconds(1, 0), MinimumProductUiVisibilityMilliseconds);
}

} // namespace
