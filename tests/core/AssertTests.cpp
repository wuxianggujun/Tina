#include <gtest/gtest.h>

#include <tina/core/diagnostics/Assert.hpp>

#include <cstdint>
#include <string>

namespace Tina::Tests {
namespace {

std::string CapturedExpression;
std::string CapturedMessage;
std::uint_least32_t CapturedLine = 0;

Core::Diagnostics::AssertAction captureAssertion(
    const Core::Diagnostics::AssertFailure& failure) noexcept
{
    CapturedExpression.assign(failure.expression);
    CapturedMessage.assign(failure.message);
    CapturedLine = failure.location.line();
    return Core::Diagnostics::AssertAction::Continue;
}

TEST(AssertTest, CustomHandlerReceivesFailureContext)
{
    using namespace Core::Diagnostics;

    CapturedExpression.clear();
    CapturedMessage.clear();
    CapturedLine = 0;

    const AssertHandler previousHandler = setAssertHandler(&captureAssertion);
    handleAssertion("value != nullptr", "test failure");
    setAssertHandler(previousHandler);

    EXPECT_EQ(CapturedExpression, "value != nullptr");
    EXPECT_EQ(CapturedMessage, "test failure");
    EXPECT_GT(CapturedLine, 0U);
}

} // namespace
} // namespace Tina::Tests
