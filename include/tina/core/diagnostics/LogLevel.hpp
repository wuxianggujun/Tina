#pragma once

#include <tina/core/base/Types.hpp>

#include <string_view>

namespace Tina::Core::Diagnostics {

// Ordered by severity. Values are append-only for stable captures.
enum class LogLevel : u8 {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Critical = 5,
    Off = 6,
};

[[nodiscard]] constexpr std::string_view logLevelName(LogLevel level) noexcept
{
    switch (level) {
    case LogLevel::Trace:
        return "Trace";
    case LogLevel::Debug:
        return "Debug";
    case LogLevel::Info:
        return "Info";
    case LogLevel::Warn:
        return "Warn";
    case LogLevel::Error:
        return "Error";
    case LogLevel::Critical:
        return "Critical";
    case LogLevel::Off:
        return "Off";
    }
    return "Unknown";
}

[[nodiscard]] constexpr bool isLogLevelEnabled(LogLevel minimum, LogLevel level) noexcept
{
    if (minimum == LogLevel::Off || level == LogLevel::Off) {
        return false;
    }
    return static_cast<u8>(level) >= static_cast<u8>(minimum);
}

} // namespace Tina::Core::Diagnostics
