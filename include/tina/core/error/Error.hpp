#pragma once

#include <tina/core/base/SourceLocation.hpp>
#include <tina/core/base/Types.hpp>

#include <compare>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Tina::Core {

enum class ErrorDomain : u16 {
    Core = 1,
    Configuration = 2,
    Runtime = 3,
    Platform = 4,
    Task = 5,
    Asset = 6,
    Scene = 7,
    Render = 8,
    UI = 9,
    Audio = 10,
    Physics2D = 11,
    Cooker = 12,
    Navigation2D = 13,
    Editor = 14,
    Network = 15,
    Save = 16,
    Gameplay = 17,
};

struct ErrorCode final {
    ErrorDomain domain = ErrorDomain::Core;
    u32 value = 0;

    auto operator<=>(const ErrorCode&) const = default;
};

namespace CoreErrorCode {

inline constexpr ErrorCode InvalidArgument{ErrorDomain::Core, 1};
inline constexpr ErrorCode NotFound{ErrorDomain::Core, 2};
inline constexpr ErrorCode AlreadyExists{ErrorDomain::Core, 3};
inline constexpr ErrorCode PermissionDenied{ErrorDomain::Core, 4};
inline constexpr ErrorCode Io{ErrorDomain::Core, 5};
inline constexpr ErrorCode Timeout{ErrorDomain::Core, 6};
inline constexpr ErrorCode Unsupported{ErrorDomain::Core, 7};
inline constexpr ErrorCode OutOfMemory{ErrorDomain::Core, 8};
inline constexpr ErrorCode CapacityExceeded{ErrorDomain::Core, 9};
inline constexpr ErrorCode Internal{ErrorDomain::Core, 10};

} // namespace CoreErrorCode

struct ErrorContext final {
    std::string operation;
    std::string detail;
    SourceLocation location = SourceLocation::current();
};

struct Error final {
    ErrorCode code = CoreErrorCode::Internal;
    std::string message;
    SourceLocation origin = SourceLocation::current();
    std::optional<i64> nativeCode;
    std::vector<ErrorContext> context;

    Error() = default;

    explicit Error(
        ErrorCode errorCode,
        std::string_view errorMessage = {},
        SourceLocation source = SourceLocation::current())
        : code(errorCode), message(errorMessage), origin(source)
    {
    }

    Error& setNativeCode(i64 value) noexcept
    {
        nativeCode = value;
        return *this;
    }

    Error& addContext(
        std::string_view operation,
        std::string_view detail = {},
        SourceLocation location = SourceLocation::current())
    {
        context.emplace_back(std::string(operation), std::string(detail), location);
        return *this;
    }

    [[nodiscard]] Error withContext(
        std::string_view operation,
        std::string_view detail = {},
        SourceLocation location = SourceLocation::current()) &&
    {
        addContext(operation, detail, location);
        return std::move(*this);
    }
};

} // namespace Tina::Core
