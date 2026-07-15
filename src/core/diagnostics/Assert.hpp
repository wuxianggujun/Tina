#pragma once

#include "../base/Compiler.hpp"
#include "../base/SourceLocation.hpp"

#include <string_view>

namespace Tina::Core::Diagnostics {

enum class AssertAction {
    Continue,
    Break,
    Abort,
};

struct AssertFailure {
    std::string_view expression;
    std::string_view message;
    SourceLocation location;
};

using AssertHandler = AssertAction (*)(const AssertFailure&) noexcept;

TINA_CORE_API AssertHandler setAssertHandler(AssertHandler handler) noexcept;
TINA_CORE_API AssertAction reportAssertion(const AssertFailure& failure) noexcept;
TINA_CORE_API void handleAssertion(
    std::string_view expression,
    std::string_view message = {},
    SourceLocation location = SourceLocation::current()) noexcept;

[[nodiscard]] constexpr std::string_view assertMessage() noexcept
{
    return {};
}

[[nodiscard]] constexpr std::string_view assertMessage(std::string_view message) noexcept
{
    return message;
}

} // namespace Tina::Core::Diagnostics

#if !defined(NDEBUG)
#define TINA_ASSERT(expression, ...)                                                               \
    do {                                                                                            \
        if (!(expression)) {                                                                        \
            ::Tina::Core::Diagnostics::handleAssertion(                                             \
                #expression,                                                                        \
                ::Tina::Core::Diagnostics::assertMessage(__VA_ARGS__),                              \
                ::Tina::Core::SourceLocation::current());                                           \
        }                                                                                           \
    } while (false)
#else
#define TINA_ASSERT(expression, ...) \
    do {                              \
    } while (false)
#endif

#ifndef ASSERT
#define ASSERT(expression) TINA_ASSERT(expression)
#endif
