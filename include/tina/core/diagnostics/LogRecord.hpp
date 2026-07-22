#pragma once

#include <tina/core/base/SourceLocation.hpp>
#include <tina/core/diagnostics/LogLevel.hpp>

#include <string_view>

namespace Tina::Core::Diagnostics {

// Borrowed log payload for a single write. Category must be a stable low-cardinality
// string; message is UTF-8. Callers must not pass high-cardinality dynamic categories.
struct LogRecord final {
    LogLevel level = LogLevel::Info;
    std::string_view category{};
    std::string_view message{};
    SourceLocation location = SourceLocation::current();
};

} // namespace Tina::Core::Diagnostics
