#include "Assert.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace Tina::Core::Diagnostics {
namespace {

AssertAction defaultAssertHandler(const AssertFailure& failure) noexcept
{
    std::fprintf(
        stderr,
        "Tina assertion failed: %.*s\n  message: %.*s\n  location: %s(%u) in %s\n",
        static_cast<int>(failure.expression.size()),
        failure.expression.data(),
        static_cast<int>(failure.message.size()),
        failure.message.data(),
        failure.location.file_name(),
        failure.location.line(),
        failure.location.function_name());
    std::fflush(stderr);
    return AssertAction::Abort;
}

std::atomic<AssertHandler> g_assertHandler{&defaultAssertHandler};

} // namespace

AssertHandler setAssertHandler(AssertHandler handler) noexcept
{
    if (handler == nullptr) {
        handler = &defaultAssertHandler;
    }
    return g_assertHandler.exchange(handler, std::memory_order_acq_rel);
}

AssertAction reportAssertion(const AssertFailure& failure) noexcept
{
    return g_assertHandler.load(std::memory_order_acquire)(failure);
}

void handleAssertion(
    std::string_view expression,
    std::string_view message,
    SourceLocation location) noexcept
{
    switch (reportAssertion(AssertFailure{expression, message, location})) {
    case AssertAction::Continue:
        return;
    case AssertAction::Break:
        debugBreak();
        return;
    case AssertAction::Abort:
        std::abort();
    }

    std::abort();
}

} // namespace Tina::Core::Diagnostics
