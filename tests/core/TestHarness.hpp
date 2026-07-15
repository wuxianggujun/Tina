#pragma once

#include <cstdio>
#include <source_location>
#include <string_view>

namespace Tina::Tests {

inline int FailureCount = 0;

inline void check(
    bool condition,
    std::string_view expression,
    std::source_location location = std::source_location::current())
{
    if (condition) {
        return;
    }

    ++FailureCount;
    std::fprintf(
        stderr,
        "%s(%u): check failed: %.*s\n",
        location.file_name(),
        location.line(),
        static_cast<int>(expression.size()),
        expression.data());
}

} // namespace Tina::Tests

#define TINA_TEST_CHECK(expression) \
    ::Tina::Tests::check(static_cast<bool>(expression), #expression, std::source_location::current())
