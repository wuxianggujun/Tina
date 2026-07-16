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

} // namespace RuntimeErrorCode

} // namespace Tina
