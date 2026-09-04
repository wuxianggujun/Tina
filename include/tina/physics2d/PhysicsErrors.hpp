#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Physics2D::Physics2DErrorCode {

inline constexpr Core::ErrorCode InvalidConfiguration{Core::ErrorDomain::Physics2D, 1};
inline constexpr Core::ErrorCode WorldClosed{Core::ErrorDomain::Physics2D, 2};
inline constexpr Core::ErrorCode WrongOwnerThread{Core::ErrorDomain::Physics2D, 3};
inline constexpr Core::ErrorCode InvalidBody{Core::ErrorDomain::Physics2D, 4};
inline constexpr Core::ErrorCode WrongWorld{Core::ErrorDomain::Physics2D, 5};
inline constexpr Core::ErrorCode StaleBody{Core::ErrorDomain::Physics2D, 6};
inline constexpr Core::ErrorCode InvalidShape{Core::ErrorDomain::Physics2D, 7};
inline constexpr Core::ErrorCode StaleShape{Core::ErrorDomain::Physics2D, 8};
inline constexpr Core::ErrorCode CapacityExceeded{Core::ErrorDomain::Physics2D, 9};
inline constexpr Core::ErrorCode InvalidBodyDescription{Core::ErrorDomain::Physics2D, 10};
inline constexpr Core::ErrorCode InvalidShapeDescription{Core::ErrorDomain::Physics2D, 11};
inline constexpr Core::ErrorCode BackendFailure{Core::ErrorDomain::Physics2D, 12};
inline constexpr Core::ErrorCode ReentrantMutation{Core::ErrorDomain::Physics2D, 13};
inline constexpr Core::ErrorCode ConstructionFailed{Core::ErrorDomain::Physics2D, 14};
inline constexpr Core::ErrorCode InvalidQuery{Core::ErrorDomain::Physics2D, 15};
inline constexpr Core::ErrorCode InvalidJoint{Core::ErrorDomain::Physics2D, 16};
inline constexpr Core::ErrorCode StaleJoint{Core::ErrorDomain::Physics2D, 17};
inline constexpr Core::ErrorCode InvalidJointDescription{Core::ErrorDomain::Physics2D, 18};

} // namespace Tina::Physics2D::Physics2DErrorCode
