#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Animation3D::AnimationErrorCode {

inline constexpr Core::ErrorCode InvalidConfiguration{Core::ErrorDomain::Animation3D, 1};
inline constexpr Core::ErrorCode InvalidArgument{Core::ErrorDomain::Animation3D, 2};
inline constexpr Core::ErrorCode CapacityExceeded{Core::ErrorDomain::Animation3D, 3};
inline constexpr Core::ErrorCode AllocationFailed{Core::ErrorDomain::Animation3D, 4};
// A clip, state, layer or joint handle that never existed or belongs to another owner.
inline constexpr Core::ErrorCode InvalidHandle{Core::ErrorDomain::Animation3D, 5};
// A clip whose jointCount does not match the skeleton the graph was built against.
// Distinct from InvalidArgument because it is the one mismatch that a content pipeline
// produces rather than a caller mistake.
inline constexpr Core::ErrorCode SkeletonMismatch{Core::ErrorDomain::Animation3D, 6};
// A joint name that no joint in this skeleton carries. Named separately from NotFound-style
// codes because the usual cause is a mask or retarget table authored against another rig,
// and reporting it as "not found" invites the caller to treat it as optional.
inline constexpr Core::ErrorCode UnknownJointName{Core::ErrorDomain::Animation3D, 7};
inline constexpr Core::ErrorCode InvalidTransition{Core::ErrorDomain::Animation3D, 8};
inline constexpr Core::ErrorCode InvalidBlendTree{Core::ErrorDomain::Animation3D, 9};
// Pose evaluation produced a non-finite transform. The previous pose is kept, so a caller
// that ignores this still renders the last good frame rather than a collapsed skeleton.
inline constexpr Core::ErrorCode EvaluationFailed{Core::ErrorDomain::Animation3D, 10};
// The graph has no state or clip bound yet, so there is nothing to evaluate.
inline constexpr Core::ErrorCode NotBound{Core::ErrorDomain::Animation3D, 11};

} // namespace Tina::Animation3D::AnimationErrorCode
