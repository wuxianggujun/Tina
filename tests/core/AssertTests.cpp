#include "TestHarness.hpp"

#include "core/diagnostics/Assert.hpp"

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

} // namespace

void runAssertTests()
{
    using namespace Core::Diagnostics;

    const AssertHandler previousHandler = setAssertHandler(&captureAssertion);
    handleAssertion("value != nullptr", "test failure");
    setAssertHandler(previousHandler);

    TINA_TEST_CHECK(CapturedExpression == "value != nullptr");
    TINA_TEST_CHECK(CapturedMessage == "test failure");
    TINA_TEST_CHECK(CapturedLine > 0);
}

} // namespace Tina::Tests
