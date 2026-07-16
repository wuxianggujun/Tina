#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Platform::PlatformErrorCode {

inline constexpr Core::ErrorCode BackendStopped{Core::ErrorDomain::Platform, 1};

} // namespace Tina::Platform::PlatformErrorCode
