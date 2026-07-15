#include "TestHarness.hpp"

#include "core/error/Result.hpp"

namespace Tina::Tests {
namespace {

Core::Result<int> requirePositive(int value)
{
    if (value <= 0) {
        return Core::failure(Core::ErrorCode::InvalidArgument, "value must be positive");
    }
    return value;
}

} // namespace

void runResultTests()
{
    const auto value = requirePositive(42);
    TINA_TEST_CHECK(value.has_value());
    TINA_TEST_CHECK(value.value() == 42);

    const auto error = requirePositive(0);
    TINA_TEST_CHECK(!error.has_value());
    TINA_TEST_CHECK(error.error().code == Core::ErrorCode::InvalidArgument);
    TINA_TEST_CHECK(error.error().message == "value must be positive");
    TINA_TEST_CHECK(error.error().location.line() > 0);

    const Core::Status status = Core::success();
    TINA_TEST_CHECK(status.has_value());
}

} // namespace Tina::Tests
