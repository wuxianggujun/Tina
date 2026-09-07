#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::AI::AIErrorCode {
inline constexpr Core::ErrorCode InvalidConfig{Core::ErrorDomain::AI, 1};
inline constexpr Core::ErrorCode CapacityExceeded{Core::ErrorDomain::AI, 2};
inline constexpr Core::ErrorCode AllocationFailed{Core::ErrorDomain::AI, 3};
inline constexpr Core::ErrorCode InvalidKey{Core::ErrorDomain::AI, 4};
inline constexpr Core::ErrorCode TypeMismatch{Core::ErrorDomain::AI, 5};
inline constexpr Core::ErrorCode MissingValue{Core::ErrorDomain::AI, 6};
inline constexpr Core::ErrorCode InvalidTree{Core::ErrorDomain::AI, 7};
inline constexpr Core::ErrorCode ReentrantDispatch{Core::ErrorDomain::AI, 8};
inline constexpr Core::ErrorCode CallbackFailed{Core::ErrorDomain::AI, 9};
inline constexpr Core::ErrorCode InvalidState{Core::ErrorDomain::AI, 10};
inline constexpr Core::ErrorCode NotStarted{Core::ErrorDomain::AI, 11};
} // namespace Tina::AI::AIErrorCode
