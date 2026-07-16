#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Task::TaskErrorCode {

inline constexpr Core::ErrorCode TaskSystemStopped{Core::ErrorDomain::Task, 1};

} // namespace Tina::Task::TaskErrorCode
