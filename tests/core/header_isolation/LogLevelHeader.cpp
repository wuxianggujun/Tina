#include <tina/core/diagnostics/LogLevel.hpp>

static_assert(Tina::Core::Diagnostics::isLogLevelEnabled(
    Tina::Core::Diagnostics::LogLevel::Info,
    Tina::Core::Diagnostics::LogLevel::Error));
