#include <gtest/gtest.h>

#include "core/error/Result.hpp"

#include <memory>

namespace Tina::Tests {
namespace {

Core::Result<int> requirePositive(int value)
{
    if (value <= 0) {
        return Core::failure(Core::ErrorCode::InvalidArgument, "value must be positive");
    }
    return value;
}

TEST(ResultTest, CarriesValuesAndStructuredErrors)
{
    const auto value = requirePositive(42);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), 42);

    const auto error = requirePositive(0);
    ASSERT_FALSE(error.has_value());
    EXPECT_EQ(error.error().code, Core::ErrorCode::InvalidArgument);
    EXPECT_EQ(error.error().message, "value must be positive");
    EXPECT_GT(error.error().location.line(), 0U);

    const Core::Status status = Core::success();
    EXPECT_TRUE(status.has_value());
}

TEST(ResultTest, SupportsMoveOnlyValuesAndVoidErrors)
{
    Core::Result<std::unique_ptr<int>> value = std::make_unique<int>(7);
    ASSERT_TRUE(value);
    EXPECT_EQ(*value.value(), 7);

    const Core::Status status = Core::failure(Core::ErrorCode::Io, "read failed");
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, Core::ErrorCode::Io);
    EXPECT_EQ(status.error().message, "read failed");
}

} // namespace
} // namespace Tina::Tests
