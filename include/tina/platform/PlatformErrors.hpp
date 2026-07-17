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
inline constexpr Core::ErrorCode BackendAlreadyActive{Core::ErrorDomain::Platform, 9};
inline constexpr Core::ErrorCode BackendInitializationFailed{Core::ErrorDomain::Platform, 10};
inline constexpr Core::ErrorCode WindowCreationFailed{Core::ErrorDomain::Platform, 11};
inline constexpr Core::ErrorCode BackendOperationFailed{Core::ErrorDomain::Platform, 12};
inline constexpr Core::ErrorCode CallbackFrameAssemblyFailed{Core::ErrorDomain::Platform, 13};
inline constexpr Core::ErrorCode WrongOwnerThread{Core::ErrorDomain::Platform, 14};
inline constexpr Core::ErrorCode WindowSurfaceUnavailable{Core::ErrorDomain::Platform, 15};
inline constexpr Core::ErrorCode WindowSurfaceLeaseAlreadyAcquired{Core::ErrorDomain::Platform, 16};
inline constexpr Core::ErrorCode WindowSurfaceRevisionExhausted{Core::ErrorDomain::Platform, 17};
inline constexpr Core::ErrorCode WindowPublicationFailed{Core::ErrorDomain::Platform, 18};

} // namespace Tina::Platform::PlatformErrorCode
