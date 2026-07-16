#pragma once

#include <tina/core/error/Error.hpp>

#include <expected>
#include <string_view>
#include <utility>

#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
#error "Tina Core requires a C++23 standard library with std::expected support"
#endif

namespace Tina::Core {

template <typename Value>
using Result = std::expected<Value, Error>;

using Status = Result<void>;

[[nodiscard]] inline Status success() noexcept
{
    return {};
}

[[nodiscard]] inline std::unexpected<Error> failure(Error error)
{
    return std::unexpected<Error>(std::move(error));
}

[[nodiscard]] inline std::unexpected<Error> failure(
    ErrorCode code,
    std::string_view message = {},
    SourceLocation location = SourceLocation::current())
{
    return failure(Error{code, message, location});
}

} // namespace Tina::Core
