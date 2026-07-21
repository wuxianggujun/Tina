#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Audio {

namespace AudioErrorCode {

inline constexpr Core::ErrorCode InvalidConfiguration{Core::ErrorDomain::Audio, 1};
inline constexpr Core::ErrorCode EngineClosed{Core::ErrorDomain::Audio, 2};
inline constexpr Core::ErrorCode WrongOwnerThread{Core::ErrorDomain::Audio, 3};
inline constexpr Core::ErrorCode InvalidVoice{Core::ErrorDomain::Audio, 4};
inline constexpr Core::ErrorCode StaleVoice{Core::ErrorDomain::Audio, 5};
inline constexpr Core::ErrorCode CapacityExceeded{Core::ErrorDomain::Audio, 6};
inline constexpr Core::ErrorCode NotSupported{Core::ErrorDomain::Audio, 7};
inline constexpr Core::ErrorCode BackendFailure{Core::ErrorDomain::Audio, 8};
inline constexpr Core::ErrorCode ConstructionFailed{Core::ErrorDomain::Audio, 9};

} // namespace AudioErrorCode

} // namespace Tina::Audio
