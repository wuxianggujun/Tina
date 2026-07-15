#pragma once

#include "../base/Types.hpp"

#include <source_location>
#include <string>
#include <string_view>

namespace Tina::Core {

enum class ErrorCode : u16 {
    None = 0,
    InvalidArgument,
    NotFound,
    AlreadyExists,
    PermissionDenied,
    Io,
    Timeout,
    Unsupported,
    OutOfMemory,
    Internal,
};

struct Error final {
    ErrorCode code = ErrorCode::Internal;
    std::string message;
    std::source_location location = std::source_location::current();

    Error() = default;

    explicit Error(
        ErrorCode errorCode,
        std::string_view errorMessage = {},
        std::source_location source = std::source_location::current())
        : code(errorCode), message(errorMessage), location(source)
    {
    }
};

} // namespace Tina::Core
