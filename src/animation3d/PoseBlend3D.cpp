#include <tina/animation3d/PoseBlend3D.hpp>

#include <tina/math/Quaternion.hpp>
#include <tina/math/Vec.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::Animation3D {

namespace {

[[nodiscard]] float clampAlpha(float alpha) noexcept
{
    // Non-finite becomes 0 rather than 1: an alpha that arrived as NaN means the caller's
    // weight computation broke, and holding the destination pose is the recoverable choice.
    if (!std::isfinite(alpha)) {
        return 0.0F;
    }
    return std::clamp(alpha, 0.0F, 1.0F);
}

[[nodiscard]] Core::u16 blendJointCount(const Pose3D& left, const Pose3D& right) noexcept
{
    return static_cast<Core::u16>((std::min)(left.jointCount(), right.jointCount()));
}

} // namespace

void blendOverwrite(Pose3D& destination, const Pose3D& source, float alpha,
                    const JointMask& mask) noexcept
{
    const float weight = clampAlpha(alpha);
    if (weight <= 0.0F) {
        return;
    }
    const Core::u16 count = blendJointCount(destination, source);
    for (Core::u16 joint = 0; joint < count; ++joint) {
        if (!mask.includes(joint)) {
            continue;
        }
        Scene::LocalTransform& target = destination.at(joint);
        const Scene::LocalTransform& from = source.at(joint);
        if (weight >= 1.0F) {
            target = from;
            continue;
        }
        target.position = Math::lerp(target.position, from.position, weight);
        target.scale = Math::lerp(target.scale, from.scale, weight);
        target.rotation = Math::slerp(target.rotation, from.rotation, weight);
    }
}

void blendAdditive(Pose3D& destination, const Pose3D& source, const Pose3D& reference, float alpha,
                   const JointMask& mask) noexcept
{
    const float weight = clampAlpha(alpha);
    if (weight <= 0.0F) {
        return;
    }
    Core::u16 count = blendJointCount(destination, source);
    count = static_cast<Core::u16>((std::min)(count, reference.jointCount()));
    for (Core::u16 joint = 0; joint < count; ++joint) {
        if (!mask.includes(joint)) {
            continue;
        }
        Scene::LocalTransform& target = destination.at(joint);
        const Scene::LocalTransform& from = source.at(joint);
        const Scene::LocalTransform& neutral = reference.at(joint);

        target.position = target.position + (from.position - neutral.position) * weight;
        // Scale is additive about 1, not about 0: a source equal to the reference must leave
        // the destination's scale unchanged, and (source - reference) is 0 there.
        target.scale = target.scale + (from.scale - neutral.scale) * weight;

        // The delta rotation is what the source adds relative to its own neutral, scaled by
        // slerping from identity. Composed on the right so the offset is applied in the
        // destination joint's local frame -- composing on the left would rotate the offset
        // by whatever the base layer happened to be doing.
        const Math::Quaternion delta = Math::conjugate(Math::normalized(neutral.rotation)) *
                                       Math::normalized(from.rotation);
        const Math::Quaternion scaledDelta =
            Math::slerp(Math::Quaternion{0.0F, 0.0F, 0.0F, 1.0F}, delta, weight);
        target.rotation = Math::normalized(target.rotation * scaledDelta);
    }
}

void blendPair(Pose3D& output, const Pose3D& from, const Pose3D& to, float alpha) noexcept
{
    const float weight = clampAlpha(alpha);
    Core::u16 count = blendJointCount(from, to);
    count = static_cast<Core::u16>((std::min)(count, output.jointCount()));
    for (Core::u16 joint = 0; joint < count; ++joint) {
        const Scene::LocalTransform& left = from.at(joint);
        const Scene::LocalTransform& right = to.at(joint);
        Scene::LocalTransform& target = output.at(joint);
        if (weight <= 0.0F) {
            target = left;
            continue;
        }
        if (weight >= 1.0F) {
            target = right;
            continue;
        }
        target.position = Math::lerp(left.position, right.position, weight);
        target.scale = Math::lerp(left.scale, right.scale, weight);
        target.rotation = Math::slerp(left.rotation, right.rotation, weight);
    }
}

bool isPoseFinite(const Pose3D& pose) noexcept
{
    for (const Scene::LocalTransform& transform : pose.transforms()) {
        if (!Scene::isValid(transform)) {
            return false;
        }
    }
    return true;
}

void normalizeRotations(Pose3D& pose) noexcept
{
    for (Scene::LocalTransform& transform : pose.transforms()) {
        transform.rotation = Math::normalized(transform.rotation);
    }
}

} // namespace Tina::Animation3D
