#pragma once

#include <tina/animation3d/Skeleton3D.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/math/Vec.hpp>

#include <span>

namespace Tina::Animation3D {

// Two-bone IK: the foot-on-ground and hand-on-handle solver.
//
// Analytic rather than iterative. A two-bone chain has a closed-form solution -- the law of
// cosines gives the joint angle directly -- so CCD or FABRIK would iterate toward an answer
// this computes exactly, and would introduce a per-frame iteration budget whose effect on
// the result is invisible until it runs out.
struct TwoBoneIkDesc final {
    // Root, middle and tip joint of the chain. The middle must be a child of the root and
    // the tip a child of the middle; the solver checks this rather than trusting it, because
    // a chain assembled from the wrong joint indices produces a plausible-looking pose.
    Core::u16 rootJoint = 0;
    Core::u16 middleJoint = 0;
    Core::u16 tipJoint = 0;

    // Where the tip should end up, in the same space as the pose's root -- model space for a
    // pose evaluated straight out of a graph.
    Math::Vec3 targetPosition{};

    // Which way the joint bends. Without it the chain has a full circle of valid solutions
    // and a knee can silently invert -- the single most recognisable IK defect. Expressed as
    // a position the middle joint is pulled toward rather than an axis, because that is what
    // an animator can place in a viewport.
    Math::Vec3 poleTargetPosition{};
    bool usePoleTarget = false;

    // Blends the solved chain against its input. Must be in [0,1]; 0 leaves the pose
    // untouched, so a caller can fade IK in as a foot approaches the ground rather than
    // snapping it.
    float weight = 1.0F;

    // Refuses to straighten the chain beyond this fraction of its full length. At exactly
    // 1 the law-of-cosines solution is a straight line whose direction is numerically
    // unstable, and the joint jitters when the target sits near maximum reach.
    float maximumReachFraction = 0.999F;
};

// Solves in place. The chain follows the target when it is within reach and points at it
// when it is not -- an unreachable target stretches nothing, because bone length is fixed by
// the skeleton and a "stretched" limb is worse than a not-quite-reaching one.
//
// `pose` must match the skeleton. Non-finite input, non-unit scale on the chain ancestry, or
// joints that are not in a root-middle-tip parent relationship fail without touching the pose.
[[nodiscard]] Core::Status solveTwoBoneIk(const Skeleton3D& skeleton, const TwoBoneIkDesc& desc,
                                         Pose3D& pose);

// Where a chain's tip currently sits in model space, for deciding whether IK is needed at
// all. Fails when the joint is out of range.
[[nodiscard]] Core::Result<Math::Vec3> jointModelPosition(const Skeleton3D& skeleton,
                                                          const Pose3D& pose, Core::u16 joint);

} // namespace Tina::Animation3D
