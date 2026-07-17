#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Platform::PlatformErrorCode {

inline constexpr Core::ErrorCode BackendStopped{Core::ErrorDomain::Platform, 1};
inline constexpr Core::ErrorCode InvalidFrameCapacity{Core::ErrorDomain::Platform, 2};
inline constexpr Core::ErrorCode FrameBuilderState{Core::ErrorDomain::Platform, 3};
inline constexpr Core::ErrorCode FrameSequenceExhausted{Core::ErrorDomain::Platform, 4};
inline constexpr Core::ErrorCode PlatformSequenceExhausted{Core::ErrorDomain::Platform, 5};
inline constexpr Core::ErrorCode InvalidInputPayload{Core::ErrorDomain::Platform, 6};
inline constexpr Core::ErrorCode InvalidFrameSnapshot{Core::ErrorDomain::Platform, 7};
inline constexpr Core::ErrorCode InvalidPlatformEventPayload{Core::ErrorDomain::Platform, 8};

} // namespace Tina::Platform::PlatformErrorCode
