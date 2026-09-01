#pragma once

#include <tina/core/base/Compiler.hpp>
#include <tina/core/diagnostics/DiagnosticChannel.hpp>
#include <tina/core/diagnostics/LogFormat.hpp>
#include <tina/core/diagnostics/LogRecord.hpp>

#include <string_view>

// Compile-time level floor. Levels below it produce no code at all: the whole
// statement disappears and its arguments are never evaluated, the same way
// TINA_TRACE_ZONE vanishes under the None backend.
//
// The build graph selects exactly one value (see src/core/CMakeLists.txt).
// Defaulting here rather than #error-ing keeps a bare consumer compiling.
#if !defined(TINA_LOG_LEVEL_COMPILED)
#define TINA_LOG_LEVEL_COMPILED 0
#endif

namespace Tina::Core::Diagnostics {

class Diagnostics;

// Process-wide default channel, so a call site needs no plumbing to log.
//
// This is an explicitly assigned pointer, not a lazily constructed singleton:
// EngineHost sets it after creating Diagnostics and clears it before
// destroying it, so there is no static initialisation order to reason about and
// no owner ambiguity. Mirrors the g_assertHandler pattern in Assert.cpp.
//
// Safe to call from any thread. Before the first set and after the clear, the
// returned channel is closed and every write is a no-op.
TINA_CORE_API void setDefaultDiagnostics(Diagnostics* diagnostics) noexcept;
[[nodiscard]] TINA_CORE_API DiagnosticChannel defaultChannel() noexcept;

namespace Detail {

// Packs call-site values and writes one record. Kept out of the macro so the
// substitution loop and the sink path stay non-template.
//
// Formats directly into the record's buffer -- no scratch buffer, no copy.
inline void writeFormatted(
    const DiagnosticChannel& channel,
    const LogLevel level,
    const std::string_view category,
    const std::string_view pattern,
    const Format::Argument* const arguments,
    const usize argumentCount,
    const SourceLocation location) noexcept
{
    LogRecord record;
    record.setLevel(level);
    record.setCategory(category);
    record.setLocation(location);

    // One byte short of capacity: setMessageLength writes a NUL terminator at
    // the returned length, which must land inside the buffer.
    const Format::Result result = Format::format(
        record.messageBuffer(), LogRecord::MessageCapacity - 1, pattern, arguments, argumentCount);
    record.setMessageLength(result.length, result.truncated);
    channel.write(record);
}

// sizeof...(Values) == 0 is why this is a template rather than a macro trick:
// a zero-length array is ill-formed.
//
// location precedes category/pattern so the macros can supply it as a fixed
// argument and forward everything after it verbatim -- see TINA_DETAIL_LOG_AT.
template <typename... Values>
inline void log(
    const DiagnosticChannel& channel,
    const LogLevel level,
    const SourceLocation location,
    const std::string_view category,
    const std::string_view pattern,
    // By const reference, not by value: a std::string argument would otherwise be
    // copy-constructed, costing the heap allocation LogFormat.hpp rules out.
    const Values&... values) noexcept
{
    if constexpr (sizeof...(Values) == 0) {
        writeFormatted(channel, level, category, pattern, nullptr, 0, location);
    } else {
        const Format::Argument arguments[]{Format::Argument{values}...};
        writeFormatted(channel, level, category, pattern, arguments, sizeof...(Values), location);
    }
}

} // namespace Detail
} // namespace Tina::Core::Diagnostics

// Two gates before any work happens:
//   1. TINA_LOG_LEVEL_COMPILED strips the statement entirely at compile time.
//   2. isEnabled() short-circuits before formatting, so a filtered-out line
//      costs one atomic load and a comparison -- arguments are still evaluated
//      (they are ordinary function arguments), but nothing is formatted.
// __VA_ARGS__ carries category, pattern and any values, so it is never empty and
// no __VA_OPT__ is needed. MSVC's traditional preprocessor only supports
// __VA_OPT__ under /Zc:preprocessor, which a consumer of this header cannot be
// required to pass.
#define TINA_DETAIL_LOG_AT(channelExpr, levelEnum, ...)                       \
    do {                                                                      \
        const auto tinaLogChannel = (channelExpr);                             \
        if (tinaLogChannel.isEnabled(levelEnum)) {                             \
            ::Tina::Core::Diagnostics::Detail::log(                            \
                tinaLogChannel,                                               \
                levelEnum,                                                    \
                ::Tina::Core::SourceLocation::current(),                      \
                __VA_ARGS__);                                                 \
        }                                                                     \
    } while (false)

#define TINA_DETAIL_LOG_ENABLED(minimum) (TINA_LOG_LEVEL_COMPILED <= (minimum))

#define TINA_DETAIL_LOG_DISCARD() \
    do {                          \
    } while (false)

// Each takes (category, pattern) and optional values. They are spelled `...`
// rather than naming the two leading parameters so the whole tail can be
// forwarded as one non-empty __VA_ARGS__; the callee's signature does the
// arity and type checking.
#if TINA_DETAIL_LOG_ENABLED(0)
#define TINA_LOG_TRACE(...)                          \
    TINA_DETAIL_LOG_AT(                              \
        ::Tina::Core::Diagnostics::defaultChannel(),  \
        ::Tina::Core::Diagnostics::LogLevel::Trace,   \
        __VA_ARGS__)
#else
#define TINA_LOG_TRACE(...) TINA_DETAIL_LOG_DISCARD()
#endif

#if TINA_DETAIL_LOG_ENABLED(1)
#define TINA_LOG_DEBUG(...)                          \
    TINA_DETAIL_LOG_AT(                              \
        ::Tina::Core::Diagnostics::defaultChannel(),  \
        ::Tina::Core::Diagnostics::LogLevel::Debug,   \
        __VA_ARGS__)
#else
#define TINA_LOG_DEBUG(...) TINA_DETAIL_LOG_DISCARD()
#endif

#if TINA_DETAIL_LOG_ENABLED(2)
#define TINA_LOG_INFO(...)                           \
    TINA_DETAIL_LOG_AT(                              \
        ::Tina::Core::Diagnostics::defaultChannel(),  \
        ::Tina::Core::Diagnostics::LogLevel::Info,    \
        __VA_ARGS__)
#else
#define TINA_LOG_INFO(...) TINA_DETAIL_LOG_DISCARD()
#endif

#if TINA_DETAIL_LOG_ENABLED(3)
#define TINA_LOG_WARN(...)                           \
    TINA_DETAIL_LOG_AT(                              \
        ::Tina::Core::Diagnostics::defaultChannel(),  \
        ::Tina::Core::Diagnostics::LogLevel::Warn,    \
        __VA_ARGS__)
#else
#define TINA_LOG_WARN(...) TINA_DETAIL_LOG_DISCARD()
#endif

#if TINA_DETAIL_LOG_ENABLED(4)
#define TINA_LOG_ERROR(...)                          \
    TINA_DETAIL_LOG_AT(                              \
        ::Tina::Core::Diagnostics::defaultChannel(),  \
        ::Tina::Core::Diagnostics::LogLevel::Error,   \
        __VA_ARGS__)
#else
#define TINA_LOG_ERROR(...) TINA_DETAIL_LOG_DISCARD()
#endif

// Critical is never compiled out: a build that cannot report its own fatal
// conditions is worse than a slow one.
#define TINA_LOG_CRITICAL(...)                        \
    TINA_DETAIL_LOG_AT(                               \
        ::Tina::Core::Diagnostics::defaultChannel(),   \
        ::Tina::Core::Diagnostics::LogLevel::Critical, \
        __VA_ARGS__)

// Explicit-channel form, for tests that must isolate a Diagnostics instance and
// for code holding a channel directly. Not level-stripped: an explicit channel
// implies a caller that wants this specific write.
#define TINA_LOG_TO(channelExpr, levelEnum, ...) \
    TINA_DETAIL_LOG_AT(channelExpr, levelEnum, __VA_ARGS__)
