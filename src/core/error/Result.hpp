#pragma once

#include "Error.hpp"

#include <expected>
#include <utility>

namespace Tina::Core {

template <typename Value>
using Result = std::expected<Value, Error>;

using Status = Result<void>;

[[nodiscard]] inline Status success() noexcept
{
    return {};
}

[[nodiscard]] inline std::unexpected<Error> failure(
    ErrorCode code,
    std::string_view message = {},
    std::source_location location = std::source_location::current())
{
    return std::unexpected(Error{code, message, location});
}

} // namespace Tina::Core
