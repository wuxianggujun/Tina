#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina {

namespace ConfigurationErrorCode {

inline constexpr Core::ErrorCode InvalidEngineConfig{Core::ErrorDomain::Configuration, 1};
inline constexpr Core::ErrorCode IncompleteEngineFactoryBundle{Core::ErrorDomain::Configuration, 2};

} // namespace ConfigurationErrorCode

namespace RuntimeErrorCode {

inline constexpr Core::ErrorCode EngineRunAlreadyStarted{Core::ErrorDomain::Runtime, 1};
inline constexpr Core::ErrorCode EngineFactoryReturnedNull{Core::ErrorDomain::Runtime, 2};
inline constexpr Core::ErrorCode EngineFactoryThrewException{Core::ErrorDomain::Runtime, 3};
inline constexpr Core::ErrorCode InitialGameStateWasNull{Core::ErrorDomain::Runtime, 4};
inline constexpr Core::ErrorCode GameCallbackThrewException{Core::ErrorDomain::Runtime, 5};
inline constexpr Core::ErrorCode MonotonicClockMovedBackward{Core::ErrorDomain::Runtime, 6};
inline constexpr Core::ErrorCode LifecycleInvariantViolation{Core::ErrorDomain::Runtime, 7};
inline constexpr Core::ErrorCode ShutdownDeadlineExceeded{Core::ErrorDomain::Runtime, 8};
inline constexpr Core::ErrorCode PlatformEventDispatcherStopped{Core::ErrorDomain::Runtime, 9};
inline constexpr Core::ErrorCode RecursivePlatformEventDispatch{Core::ErrorDomain::Runtime, 10};
inline constexpr Core::ErrorCode PlatformEventCallbackThrewException{Core::ErrorDomain::Runtime, 11};
inline constexpr Core::ErrorCode WrongOwnerThread{Core::ErrorDomain::Runtime, 12};
inline constexpr Core::ErrorCode PrimaryWindowUIUnavailable{Core::ErrorDomain::Runtime, 13};
inline constexpr Core::ErrorCode UIPhaseCapabilityExpired{Core::ErrorDomain::Runtime, 14};
inline constexpr Core::ErrorCode GameStateCommandAlreadyQueued{Core::ErrorDomain::Runtime, 15};
inline constexpr Core::ErrorCode GameStateStackCapacityExceeded{Core::ErrorDomain::Runtime, 16};
inline constexpr Core::ErrorCode GameStateTransitionFailed{Core::ErrorDomain::Runtime, 17};
inline constexpr Core::ErrorCode GameStateCommandRejected{Core::ErrorDomain::Runtime, 18};

} // namespace RuntimeErrorCode

} // namespace Tina
