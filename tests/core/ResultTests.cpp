#include <gtest/gtest.h>

#include <tina/core/error/Result.hpp>

#include <memory>
#include <type_traits>

namespace Tina::Tests {
namespace {

Core::Result<int> requirePositive(int value)
{
    if (value <= 0) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "value must be positive");
    }
    return value;
}

TEST(ResultTest, UsesExpectedForValuesMoveOnlyValuesAndStatus)
{
    static_assert(std::is_same_v<Core::Result<int>, std::expected<int, Core::Error>>);

    const auto value = requirePositive(42);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), 42);

    Core::Result<std::unique_ptr<int>> moveOnlyValue = std::make_unique<int>(7);
    ASSERT_TRUE(moveOnlyValue);
    EXPECT_EQ(*moveOnlyValue.value(), 7);

    const Core::Status status = Core::success();
    EXPECT_TRUE(status.has_value());
}

TEST(ResultTest, CarriesStableCodeOriginNativeCodeAndContextChain)
{
    Core::Error error{
        Core::CoreErrorCode::Io,
        "unable to read cooked asset",
    };
    error.setNativeCode(5)
        .addContext("open manifest", "assets/manifest.bin")
        .addContext("加载启动场景", "主场景.scene");

    const Core::Status status = Core::failure(std::move(error));
    ASSERT_FALSE(status);

    const Core::Error& captured = status.error();
    EXPECT_EQ(captured.code, Core::CoreErrorCode::Io);
    EXPECT_EQ(captured.code.domain, Core::ErrorDomain::Core);
    EXPECT_EQ(captured.code.value, 5U);
    EXPECT_EQ(captured.message, "unable to read cooked asset");
    EXPECT_GT(captured.origin.line(), 0U);
    ASSERT_TRUE(captured.nativeCode.has_value());
    EXPECT_EQ(*captured.nativeCode, 5);
    ASSERT_EQ(captured.context.size(), 2U);
    EXPECT_EQ(captured.context[0].operation, "open manifest");
    EXPECT_EQ(captured.context[0].detail, "assets/manifest.bin");
    EXPECT_EQ(captured.context[1].operation, "加载启动场景");
    EXPECT_EQ(captured.context[1].detail, "主场景.scene");
    EXPECT_GT(captured.context[1].location.line(), 0U);
}

TEST(ResultTest, FailureHelperCapturesTheCallSite)
{
    const Core::Status status = Core::failure(
        Core::CoreErrorCode::InvalidArgument,
        "invalid config");

    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, Core::CoreErrorCode::InvalidArgument);
    EXPECT_EQ(status.error().message, "invalid config");
    EXPECT_GT(status.error().origin.line(), 0U);
    EXPECT_THROW(status.value(), std::bad_expected_access<Core::Error>);
}

} // namespace
} // namespace Tina::Tests
