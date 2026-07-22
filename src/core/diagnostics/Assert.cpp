#include "Assert.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace Tina::Core::Diagnostics {
namespace {

void writeAssertLog(const AssertFailure& failure) noexcept
{
    char path[512]{};
    const char* temp = std::getenv("TEMP");
    if (temp == nullptr || temp[0] == '\0')
    {
        temp = std::getenv("TMP");
    }
    if (temp == nullptr || temp[0] == '\0')
    {
        temp = ".";
    }
    std::snprintf(path, sizeof(path), "%s\\tina_assert_fatal.txt", temp);
    if (std::FILE* file = std::fopen(path, "wb"))
    {
        std::fprintf(
            file,
            "Tina assertion failed: %.*s\n  message: %.*s\n  location: %s(%u) in %s\n",
            static_cast<int>(failure.expression.size()),
            failure.expression.data(),
            static_cast<int>(failure.message.size()),
            failure.message.data(),
            failure.location.file_name(),
            failure.location.line(),
            failure.location.function_name());
        std::fflush(file);
        std::fclose(file);
    }
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
}

AssertAction defaultAssertHandler(const AssertFailure& failure) noexcept
{
    writeAssertLog(failure);
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
        // Avoid MSVC Debug CRT modal abort dialog (blocks automation).
        std::_Exit(3);
    }

    std::_Exit(3);
}

} // namespace Tina::Core::Diagnostics
