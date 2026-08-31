#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Gameplay::GameplayErrorCode {

inline constexpr Core::ErrorCode InvalidConfiguration{Core::ErrorDomain::Gameplay, 1};
inline constexpr Core::ErrorCode InvalidArgument{Core::ErrorDomain::Gameplay, 2};
inline constexpr Core::ErrorCode CapacityExceeded{Core::ErrorDomain::Gameplay, 3};
inline constexpr Core::ErrorCode AllocationFailed{Core::ErrorDomain::Gameplay, 4};
// A handle that never existed, was cancelled, or belongs to another owner. Kept
// distinct from NotFound-style codes because a stale handle is nearly always a
// lifetime bug in the caller rather than a missing lookup.
inline constexpr Core::ErrorCode InvalidHandle{Core::ErrorDomain::Gameplay, 5};
// advance() re-entered from a callback it dispatched, or a signal published from
// one of its own subscribers. Both would make dispatch order depend on how deep
// the nesting happened to be.
inline constexpr Core::ErrorCode ReentrantDispatch{Core::ErrorDomain::Gameplay, 6};
inline constexpr Core::ErrorCode InvalidSequence{Core::ErrorDomain::Gameplay, 7};
inline constexpr Core::ErrorCode MissingCallback{Core::ErrorDomain::Gameplay, 8};
// post() on a signal built with no deferred queue.
inline constexpr Core::ErrorCode DeferredDeliveryUnavailable{Core::ErrorDomain::Gameplay, 9};

} // namespace Tina::Gameplay::GameplayErrorCode
