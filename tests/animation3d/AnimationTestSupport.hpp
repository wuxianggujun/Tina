#pragma once

#include <tina/animation3d/ClipSampler3D.hpp>
#include <tina/animation3d/Skeleton3D.hpp>
#include <tina/asset_format/AnimationClip3DPayload.hpp>
#include <tina/asset_format/SkinnedMeshPayload.hpp>

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <string>
#include <vector>

namespace Tina::Animation3D::Testing {

// A named chain of `jointCount` joints, each the child of the previous one and offset one
// unit along X. Named so mask and IK tests can address bones the way content does.
[[nodiscard]] inline std::vector<std::byte> makeChainSkeletonPayload(
    Core::u16 jointCount, std::span<const std::string> names = {})
{
    std::vector<AssetFormat::SkinnedMeshJointDesc> joints;
    std::vector<float> inverseBind;
    joints.reserve(jointCount);
    inverseBind.reserve(static_cast<Core::usize>(jointCount) * 16U);

    for (Core::u16 index = 0; index < jointCount; ++index) {
        AssetFormat::SkinnedMeshJointDesc joint{};
        joint.parentJoint =
            index == 0U ? AssetFormat::SkinnedMeshWire::JointIndexNone
                        : static_cast<Core::u16>(index - 1U);
        // Root at the origin, every other joint one unit along X from its parent.
        joint.bindTranslation[0] = index == 0U ? 0.0F : 1.0F;
        if (index < names.size()) {
            joint.name = names[index];
        }
        joints.push_back(joint);

        // Identity inverse bind: the tests care about pose composition, not bind offsets.
        const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        inverseBind.insert(inverseBind.end(), identity.begin(), identity.end());
    }

    const std::array<AssetFormat::StaticMeshSubmeshDesc, 1> submeshes{
        AssetFormat::StaticMeshSubmeshDesc{.firstIndex = 0, .indexCount = 3}};
    const std::array<float, 3 * AssetFormat::SkinnedMeshWire::FloatsPerVertex> vertices{
        0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0,
        1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0,
        0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1};
    const std::array<Core::u16, 12> jointIndices{};
    const std::array<Core::u16, 12> jointWeights{65535, 0, 0, 0, 65535, 0, 0, 0, 65535, 0, 0, 0};
    const std::array<Core::u16, 3> indices{0, 1, 2};

    auto payload = AssetFormat::writeSkinnedMeshPayloadBytes(AssetFormat::SkinnedMeshPayloadDesc{
        .boundsRadius = 1.0F,
        .joints = joints,
        .inverseBindMatrices = inverseBind,
        .submeshes = submeshes,
        .vertices = vertices,
        .jointIndices = jointIndices,
        .jointWeights = jointWeights,
        .indices = indices,
    });
    EXPECT_TRUE(payload.has_value()) << (payload ? "" : payload.error().message);
    return payload ? std::move(*payload) : std::vector<std::byte>{};
}

// A clip translating one joint along X from `fromX` to `toX` over `duration`.
//
// The key arrays are locals rather than statics: the track descs below hold spans into them,
// and a static would be rewritten by the next call while an earlier desc still pointed at it.
// The spans only need to outlive writeAnimationClip3DPayloadBytes, which copies.
[[nodiscard]] inline std::vector<std::byte> makeTranslationClipPayload(
    Core::u16 jointCount, Core::u16 targetJoint, float fromX, float toX, float duration,
    AssetFormat::AnimationClip3DPlaybackMode mode = AssetFormat::AnimationClip3DPlaybackMode::Loop)
{
    const std::array<float, 2> times{0.0F, duration};
    const std::array<float, 6> values{fromX, 0.0F, 0.0F, toX, 0.0F, 0.0F};

    const std::array<AssetFormat::AnimationTrackDesc, 1> tracks{AssetFormat::AnimationTrackDesc{
        .jointIndex = targetJoint,
        .channel = AssetFormat::AnimationChannel::Translation,
        .interpolation = AssetFormat::AnimationInterpolation::Linear,
        .times = times,
        .values = values,
    }};

    auto payload = AssetFormat::writeAnimationClip3DPayloadBytes(
        AssetFormat::AnimationClip3DPayloadDesc{
            .playbackMode = mode,
            .jointCount = jointCount,
            .durationSeconds = duration,
            .tracks = tracks,
        });
    EXPECT_TRUE(payload.has_value()) << (payload ? "" : payload.error().message);
    return payload ? std::move(*payload) : std::vector<std::byte>{};
}

} // namespace Tina::Animation3D::Testing
