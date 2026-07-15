#pragma once

#include "../base/SourceLocation.hpp"
#include "../base/Types.hpp"

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
    SourceLocation location = SourceLocation::current();

    Error() = default;

    explicit Error(
        ErrorCode errorCode,
        std::string_view errorMessage = {},
        SourceLocation source = SourceLocation::current())
        : code(errorCode), message(errorMessage), location(source)
    {
    }
};

} // namespace Tina::Core
