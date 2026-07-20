#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Task::TaskErrorCode {

inline constexpr Core::ErrorCode TaskSystemStopped{Core::ErrorDomain::Task, 1};
inline constexpr Core::ErrorCode QueueFull{Core::ErrorDomain::Task, 2};
inline constexpr Core::ErrorCode InvalidArgument{Core::ErrorDomain::Task, 3};
inline constexpr Core::ErrorCode NotSupported{Core::ErrorDomain::Task, 4};

} // namespace Tina::Task::TaskErrorCode
