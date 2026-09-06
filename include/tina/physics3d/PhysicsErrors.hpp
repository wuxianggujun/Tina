#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Physics3D::Physics3DErrorCode {

inline constexpr Core::ErrorCode InvalidConfiguration{Core::ErrorDomain::Physics3D, 1};
inline constexpr Core::ErrorCode WorldClosed{Core::ErrorDomain::Physics3D, 2};
inline constexpr Core::ErrorCode WrongOwnerThread{Core::ErrorDomain::Physics3D, 3};
inline constexpr Core::ErrorCode InvalidBody{Core::ErrorDomain::Physics3D, 4};
inline constexpr Core::ErrorCode WrongWorld{Core::ErrorDomain::Physics3D, 5};
inline constexpr Core::ErrorCode StaleBody{Core::ErrorDomain::Physics3D, 6};
inline constexpr Core::ErrorCode CapacityExceeded{Core::ErrorDomain::Physics3D, 7};
inline constexpr Core::ErrorCode InvalidBodyDescription{Core::ErrorDomain::Physics3D, 8};
inline constexpr Core::ErrorCode InvalidQuery{Core::ErrorDomain::Physics3D, 9};
inline constexpr Core::ErrorCode InvalidOriginShift{Core::ErrorDomain::Physics3D, 10};
inline constexpr Core::ErrorCode BackendFailure{Core::ErrorDomain::Physics3D, 11};
inline constexpr Core::ErrorCode WorldFaulted{Core::ErrorDomain::Physics3D, 12};

} // namespace Tina::Physics3D::Physics3DErrorCode
