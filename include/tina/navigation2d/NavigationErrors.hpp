#pragma once

#include <tina/core/error/Error.hpp>

namespace Tina::Navigation2D::Navigation2DErrorCode {

inline constexpr Core::ErrorCode InvalidData{Core::ErrorDomain::Navigation2D, 1};
inline constexpr Core::ErrorCode InvalidCell{Core::ErrorDomain::Navigation2D, 2};
inline constexpr Core::ErrorCode InvalidBlocker{Core::ErrorDomain::Navigation2D, 3};
inline constexpr Core::ErrorCode CapacityExceeded{Core::ErrorDomain::Navigation2D, 4};
inline constexpr Core::ErrorCode AllocationFailed{Core::ErrorDomain::Navigation2D, 5};
inline constexpr Core::ErrorCode QueryNotStarted{Core::ErrorDomain::Navigation2D, 6};
// An input polyline is empty, leaves the grid, or contains a segment the corner
// rule refuses to verify. Distinct from InvalidCell because the offending value is
// the path as a whole rather than one coordinate.
inline constexpr Core::ErrorCode InvalidPath{Core::ErrorDomain::Navigation2D, 7};
// A flow field cost/direction query issued before the field reached Ready. The
// field publishes nothing while Pending, because a half-expanded Dijkstra front
// holds directions that point at cells whose own cost is not final yet.
inline constexpr Core::ErrorCode FieldNotReady{Core::ErrorDomain::Navigation2D, 8};
// An agent step or goal request carried a non-finite world position, or a position
// outside the grid the agent planned against.
inline constexpr Core::ErrorCode InvalidWorldPosition{Core::ErrorDomain::Navigation2D, 9};
// An agent step ran without a goal. Requesting velocity from an idle agent is a
// caller sequencing bug, not a "stand still" answer.
inline constexpr Core::ErrorCode NoActiveGoal{Core::ErrorDomain::Navigation2D, 10};

} // namespace Tina::Navigation2D::Navigation2DErrorCode
