#include <tina/animation3d/IkSolver3D.hpp>

#include <tina/math/Mat4.hpp>
#include <tina/math/Quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::Animation3D {

namespace {

constexpr float UnitScaleTolerance = 1.0e-5F;

[[nodiscard]] bool isUnitScale(Math::Vec3 scale) noexcept
{
    return Math::isFinite(scale) && std::abs(scale.x - 1.0F) <= UnitScaleTolerance &&
           std::abs(scale.y - 1.0F) <= UnitScaleTolerance &&
           std::abs(scale.z - 1.0F) <= UnitScaleTolerance;
}

// Model-space transform of one joint, by walking to the root. A chain solve touches three
// joints, so walking beats composing every joint's matrix into a scratch buffer.
struct JointModelTransform final {
    Math::Vec3 position{};
    Math::Quaternion rotation{0.0F, 0.0F, 0.0F, 1.0F};
};

[[nodiscard]] JointModelTransform modelTransformOf(const Skeleton3D& skeleton, const Pose3D& pose,
                                                  Core::u16 joint) noexcept
{
    // Accumulated from the joint upward, then applied outward: composing in the other
    // direction would need the chain reversed into scratch storage first.
    Math::Vec3 position = pose.at(joint).position;
    Math::Quaternion rotation = pose.at(joint).rotation;
    Core::u16 current = skeleton.parent(joint);
    while (current != JointIndexNone) {
        const Scene::LocalTransform& parentLocal = pose.at(current);
        // Scale is deliberately ignored: a scaled IK chain has no well-defined bone length,
        // and every rig this solver targets has unit-scaled joints. A non-unit scale would
        // silently make the reach computation wrong, so it is documented rather than
        // half-supported.
        position = parentLocal.position + Math::rotate(parentLocal.rotation, position);
        rotation = parentLocal.rotation * rotation;
        current = skeleton.parent(current);
    }
    return JointModelTransform{.position = position, .rotation = Math::normalized(rotation)};
}

// Rotation taking `from` onto `to`, both assumed non-degenerate unit vectors.
[[nodiscard]] Math::Quaternion rotationBetween(Math::Vec3 from, Math::Vec3 to) noexcept
{
    const Math::Vec3 a = Math::normalized(from);
    const Math::Vec3 b = Math::normalized(to);
    const float dot = Math::dot(a, b);
    if (dot >= 1.0F - 1.0e-6F) {
        return Math::Quaternion{0.0F, 0.0F, 0.0F, 1.0F};
    }
    if (dot <= -1.0F + 1.0e-6F) {
        // Antiparallel: any perpendicular axis is a valid 180-degree rotation. Picking the
        // one least aligned with `a` keeps the cross product well conditioned.
        const Math::Vec3 axisCandidate = std::abs(a.x) < 0.9F ? Math::Vec3{1.0F, 0.0F, 0.0F}
                                                             : Math::Vec3{0.0F, 1.0F, 0.0F};
        const Math::Vec3 axis = Math::normalized(Math::cross(a, axisCandidate));
        return Math::Quaternion{axis.x, axis.y, axis.z, 0.0F};
    }
    const Math::Vec3 axis = Math::cross(a, b);
    const Math::Quaternion result{axis.x, axis.y, axis.z, 1.0F + dot};
    return Math::normalized(result);
}

} // namespace

Core::Result<Math::Vec3> jointModelPosition(const Skeleton3D& skeleton, const Pose3D& pose,
                                            Core::u16 joint)
{
    if (joint >= skeleton.jointCount() || pose.jointCount() != skeleton.jointCount()) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "joint index or pose does not match the skeleton");
    }
    return modelTransformOf(skeleton, pose, joint).position;
}

Core::Status solveTwoBoneIk(const Skeleton3D& skeleton, const TwoBoneIkDesc& desc, Pose3D& pose)
{
    if (pose.jointCount() != skeleton.jointCount()) {
        return Core::failure(AnimationErrorCode::SkeletonMismatch,
                             "IK pose does not match the skeleton");
    }
    const Core::u16 jointCount = skeleton.jointCount();
    if (desc.rootJoint >= jointCount || desc.middleJoint >= jointCount ||
        desc.tipJoint >= jointCount) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "IK chain joint index is outside the skeleton");
    }
    // Checked rather than trusted: a chain built from the wrong indices still produces a
    // pose, just a wrong one, and nothing downstream can tell.
    if (skeleton.parent(desc.middleJoint) != desc.rootJoint ||
        skeleton.parent(desc.tipJoint) != desc.middleJoint) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "IK chain joints must form a root-middle-tip parent chain");
    }
    if (!Math::isFinite(desc.targetPosition) ||
        (desc.usePoleTarget && !Math::isFinite(desc.poleTargetPosition))) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "IK target positions must be finite");
    }
    if (!std::isfinite(desc.weight) || desc.weight < 0.0F || desc.weight > 1.0F ||
        !std::isfinite(desc.maximumReachFraction) ||
        desc.maximumReachFraction <= 0.0F || desc.maximumReachFraction > 1.0F) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "IK weight and reach fraction must be finite and in range");
    }

    // modelTransformOf deliberately ignores scale, so accepting a scaled joint would produce
    // plausible but wrong bone lengths. Validate the whole tip ancestry because a scaled
    // parent above root also changes the model-space chain.
    Core::u16 ancestor = desc.tipJoint;
    while (ancestor != JointIndexNone) {
        const Scene::LocalTransform& local = pose.at(ancestor);
        if (!Scene::isValid(local) || !isUnitScale(local.scale)) {
            return Core::failure(AnimationErrorCode::InvalidArgument,
                                 "IK chain ancestry must contain valid unit-scaled transforms");
        }
        ancestor = skeleton.parent(ancestor);
    }

    const float weight = desc.weight;
    if (weight <= 0.0F) {
        return Core::success();
    }

    const JointModelTransform rootModel = modelTransformOf(skeleton, pose, desc.rootJoint);
    const JointModelTransform middleModel = modelTransformOf(skeleton, pose, desc.middleJoint);
    const JointModelTransform tipModel = modelTransformOf(skeleton, pose, desc.tipJoint);

    const float upperLength = Math::length(middleModel.position - rootModel.position);
    const float lowerLength = Math::length(tipModel.position - middleModel.position);
    if (!std::isfinite(upperLength) || !std::isfinite(lowerLength) ||
        !(upperLength > 0.0F) || !(lowerLength > 0.0F)) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "IK chain bone lengths must be finite and non-zero");
    }

    const Math::Vec3 toTarget = desc.targetPosition - rootModel.position;
    const float targetDistance = Math::length(toTarget);
    if (!std::isfinite(targetDistance)) {
        return Core::failure(AnimationErrorCode::InvalidArgument,
                             "IK target distance must be finite");
    }
    if (!(targetDistance > 1.0e-6F)) {
        // The target sits on the root joint; there is no direction to point the chain along.
        return Core::success();
    }

    // Clamped below full extension: at exactly full reach the law-of-cosines solution is a
    // straight line whose direction is numerically unstable, and the joint visibly jitters
    // when a target hovers at maximum reach.
    const float maximumReach = (upperLength + lowerLength) * desc.maximumReachFraction;
    const float minimumReach = std::abs(upperLength - lowerLength) * 1.001F;
    if (!std::isfinite(maximumReach) || !std::isfinite(minimumReach) ||
        minimumReach > maximumReach) {
        return Core::failure(
            AnimationErrorCode::InvalidArgument,
            "IK reach fraction is below the chain's minimum reachable distance");
    }
    const float reach = std::clamp(targetDistance, minimumReach, maximumReach);

    // Law of cosines for the angle at the root between the upper bone and the root-to-target
    // direction, and the interior angle at the middle joint.
    const float rootCosine =
        std::clamp((upperLength * upperLength + reach * reach - lowerLength * lowerLength) /
                       (2.0F * upperLength * reach),
                   -1.0F, 1.0F);
    const float middleCosine =
        std::clamp((upperLength * upperLength + lowerLength * lowerLength - reach * reach) /
                       (2.0F * upperLength * lowerLength),
                   -1.0F, 1.0F);
    const float rootAngle = std::acos(rootCosine);
    const float middleAngle = std::acos(middleCosine);
    if (!std::isfinite(rootAngle) || !std::isfinite(middleAngle)) {
        return Core::failure(AnimationErrorCode::EvaluationFailed,
                             "IK angle solution is not finite");
    }

    const Math::Vec3 targetDirection = Math::normalized(toTarget);
    const Math::Vec3 currentDirection =
        Math::normalized(middleModel.position - rootModel.position);

    // The bend axis. A pole target names the plane the chain bends in; without one the axis
    // comes from the chain's current bend, which preserves whatever the animation authored --
    // that is why usePoleTarget is opt-in rather than defaulted to a world axis, which would
    // flip a knee the moment the leg passed through that axis.
    Math::Vec3 bendAxis{};
    if (desc.usePoleTarget) {
        const Math::Vec3 toPole = desc.poleTargetPosition - rootModel.position;
        bendAxis = Math::cross(targetDirection, toPole);
    } else {
        bendAxis = Math::cross(currentDirection, targetDirection);
        if (Math::lengthSquared(bendAxis) <= 1.0e-12F) {
            // The chain already points at the target, so its current bend plane is the one
            // to keep.
            bendAxis = Math::cross(middleModel.position - rootModel.position,
                                   tipModel.position - middleModel.position);
        }
    }
    if (Math::lengthSquared(bendAxis) <= 1.0e-12F) {
        // A fully straight chain with no pole target has no defined bend plane. Any choice
        // would be arbitrary and would differ between frames, so the solve is skipped rather
        // than guessing an axis that pops.
        return Core::success();
    }
    bendAxis = Math::normalized(bendAxis);

    // Where the middle and tip joints must end up in model space.
    const Math::Quaternion rootBend = Math::fromAxisAngle(bendAxis, rootAngle);
    const Math::Vec3 upperDirection = Math::rotate(rootBend, targetDirection);
    const Math::Vec3 solvedMiddle = rootModel.position + upperDirection * upperLength;
    // The interior angle at the middle joint is measured from the reversed upper bone, so
    // the turn from the upper direction is (pi - middleAngle).
    const Math::Quaternion middleBend =
        Math::fromAxisAngle(bendAxis, middleAngle - Math::Pi);
    const Math::Vec3 lowerDirection = Math::rotate(middleBend, upperDirection);
    const Math::Vec3 solvedTip = solvedMiddle + lowerDirection * lowerLength;

    // Converted back into local rotations. Each delta is computed in model space and then
    // expressed in the joint's parent frame, which is what the pose stores.
    const Math::Quaternion rootDelta =
        rotationBetween(middleModel.position - rootModel.position, solvedMiddle - rootModel.position);
    const Math::Quaternion middleDelta =
        rotationBetween(tipModel.position - middleModel.position, solvedTip - solvedMiddle);

    const JointModelTransform rootParent =
        skeleton.parent(desc.rootJoint) == JointIndexNone
            ? JointModelTransform{}
            : modelTransformOf(skeleton, pose, skeleton.parent(desc.rootJoint));

    Scene::LocalTransform& rootLocal = pose.at(desc.rootJoint);
    Scene::LocalTransform& middleLocal = pose.at(desc.middleJoint);
    const Math::Quaternion parentInverse = Math::conjugate(Math::normalized(rootParent.rotation));

    const Math::Quaternion solvedRootModelRotation =
        Math::normalized(rootDelta * rootModel.rotation);
    const Math::Quaternion solvedRootLocal =
        Math::normalized(parentInverse * solvedRootModelRotation);
    const Math::Quaternion solvedMiddleModelRotation =
        Math::normalized(middleDelta * middleModel.rotation);
    const Math::Quaternion solvedMiddleLocal =
        Math::normalized(Math::conjugate(solvedRootModelRotation) * solvedMiddleModelRotation);

    if (!Math::isFinite(solvedRootLocal) || !Math::isFinite(solvedMiddleLocal)) {
        return Core::failure(AnimationErrorCode::EvaluationFailed,
                             "IK solved rotation is not finite");
    }

    // Blended against the input so IK can fade in. slerp, not assignment, is what makes a
    // foot settle onto uneven ground instead of snapping to it.
    rootLocal.rotation = Math::slerp(rootLocal.rotation, solvedRootLocal, weight);
    middleLocal.rotation = Math::slerp(middleLocal.rotation, solvedMiddleLocal, weight);
    return Core::success();
}

} // namespace Tina::Animation3D
