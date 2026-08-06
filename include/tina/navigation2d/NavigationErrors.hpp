#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Navigation2D::NavigationErrorCode {

inline constexpr Core::ErrorCode InvalidData{Core::ErrorDomain::Navigation2D, 1};
inline constexpr Core::ErrorCode InvalidCell{Core::ErrorDomain::Navigation2D, 2};
inline constexpr Core::ErrorCode InvalidBlocker{Core::ErrorDomain::Navigation2D, 3};
inline constexpr Core::ErrorCode CapacityExceeded{Core::ErrorDomain::Navigation2D, 4};
inline constexpr Core::ErrorCode AllocationFailed{Core::ErrorDomain::Navigation2D, 5};
inline constexpr Core::ErrorCode QueryNotStarted{Core::ErrorDomain::Navigation2D, 6};

} // namespace Tina::Navigation2D::NavigationErrorCode
