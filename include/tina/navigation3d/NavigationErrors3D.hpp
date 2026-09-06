#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Navigation3D::Navigation3DErrorCode {

inline constexpr Core::ErrorCode InvalidData{Core::ErrorDomain::Navigation3D, 1};
inline constexpr Core::ErrorCode InvalidCell{Core::ErrorDomain::Navigation3D, 2};
inline constexpr Core::ErrorCode InvalidBlocker{Core::ErrorDomain::Navigation3D, 3};
inline constexpr Core::ErrorCode CapacityExceeded{Core::ErrorDomain::Navigation3D, 4};
inline constexpr Core::ErrorCode AllocationFailed{Core::ErrorDomain::Navigation3D, 5};
inline constexpr Core::ErrorCode QueryNotStarted{Core::ErrorDomain::Navigation3D, 6};
// A start or goal cell exists but no agent of this profile can stand in it: the cell
// is solid, its head clearance is occupied, or nothing supports it. Distinct from
// InvalidCell because the coordinate is inside the volume; it is the agent that does
// not fit. A caller that cannot tell these apart would report "out of bounds" for a
// goal the player is looking straight at.
inline constexpr Core::ErrorCode NotStandable{Core::ErrorDomain::Navigation3D, 7};
// An agent profile whose height, step-up or fall allowance is outside the supported
// range, or which cannot fit in the volume at all.
inline constexpr Core::ErrorCode InvalidAgentProfile{Core::ErrorDomain::Navigation3D, 8};
inline constexpr Core::ErrorCode InvalidWorldPosition{Core::ErrorDomain::Navigation3D, 9};

} // namespace Tina::Navigation3D::Navigation3DErrorCode
