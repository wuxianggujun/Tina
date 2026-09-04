#include "AnimationTestSupport.hpp"

#include <tina/animation3d/Skeleton3D.hpp>
#include <tina/asset_format/SkinnedMeshPayload.hpp>

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

namespace Tina::Animation3D {
namespace {

TEST(Skeleton3DTests, BuildsFromPayloadAndResolvesNamesToIndices)
{
    const std::array<std::string, 3> names{"hips", "spine", "head"};
    const auto payload = Testing::makeChainSkeletonPayload(3, names);
    const auto view = AssetFormat::parseSkinnedMeshPayload(payload);
    ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);

    auto skeleton = Skeleton3D::Create(*view);
    ASSERT_TRUE(skeleton.has_value()) << (skeleton ? "" : skeleton.error().message);
    EXPECT_EQ(skeleton->jointCount(), 3U);
    EXPECT_EQ(skeleton->parent(0), JointIndexNone);
    EXPECT_EQ(skeleton->parent(1), 0U);
    EXPECT_EQ(skeleton->parent(2), 1U);

    EXPECT_EQ(skeleton->jointName(1), "spine");
    EXPECT_EQ(skeleton->findJoint("head"), 2U);
    EXPECT_FALSE(skeleton->findJoint("tail").has_value());
    // An unnamed joint is not addressable, so an empty query resolves to nothing.
    EXPECT_FALSE(skeleton->findJoint("").has_value());
}

// A default mask means every joint. "No joints" is expressed by weight 0 instead, because a
// layer whose caller never asked for masking must animate the whole skeleton -- the opposite
// default would leave a layer silently doing nothing.
TEST(Skeleton3DTests, DefaultMaskIncludesEveryJointAndExplicitMasksAreSelective)
{
    const std::array<std::string, 4> names{"hips", "spine", "chest", "head"};
    const auto payload = Testing::makeChainSkeletonPayload(4, names);
    const auto view = AssetFormat::parseSkinnedMeshPayload(payload);
    ASSERT_TRUE(view.has_value());
    auto skeleton = Skeleton3D::Create(*view);
    ASSERT_TRUE(skeleton.has_value());

    const JointMask everything{};
    EXPECT_TRUE(everything.includesEveryJoint());
    EXPECT_EQ(everything.includedCount(4), 4U);

    // Naming one joint without descendants includes only it.
    const std::array<std::string_view, 1> spineOnly{"spine"};
    auto exact = skeleton->resolveMask(spineOnly, false);
    ASSERT_TRUE(exact.has_value()) << (exact ? "" : exact.error().message);
    EXPECT_FALSE(exact->includes(0));
    EXPECT_TRUE(exact->includes(1));
    EXPECT_FALSE(exact->includes(2));
    EXPECT_EQ(exact->includedCount(4), 1U);

    // "The upper body" means a joint and everything under it, which is what content authors
    // mean; requiring them to enumerate a rig is how masks drift out of sync with it.
    auto subtree = skeleton->resolveMask(spineOnly, true);
    ASSERT_TRUE(subtree.has_value()) << (subtree ? "" : subtree.error().message);
    EXPECT_FALSE(subtree->includes(0));
    EXPECT_TRUE(subtree->includes(1));
    EXPECT_TRUE(subtree->includes(2));
    EXPECT_TRUE(subtree->includes(3));
    EXPECT_EQ(subtree->includedCount(4), 3U);
}

// A name this rig does not carry fails rather than being skipped: a mask that silently drops
// the bones it could not resolve produces an animation that is subtly wrong everywhere, and
// the usual cause is a mask authored against a different rig.
TEST(Skeleton3DTests, UnknownJointNameInAMaskFailsRatherThanSkipping)
{
    const std::array<std::string, 2> names{"hips", "spine"};
    const auto payload = Testing::makeChainSkeletonPayload(2, names);
    const auto view = AssetFormat::parseSkinnedMeshPayload(payload);
    ASSERT_TRUE(view.has_value());
    auto skeleton = Skeleton3D::Create(*view);
    ASSERT_TRUE(skeleton.has_value());

    const std::array<std::string_view, 2> withTypo{"hips", "spien"};
    const auto mask = skeleton->resolveMask(withTypo, false);
    ASSERT_FALSE(mask.has_value());
    EXPECT_EQ(mask.error().code, Animation3DErrorCode::UnknownJointName);
}

TEST(Skeleton3DTests, ComposesHierarchyIntoSkinningMatrices)
{
    const auto payload = Testing::makeChainSkeletonPayload(3);
    const auto view = AssetFormat::parseSkinnedMeshPayload(payload);
    ASSERT_TRUE(view.has_value());
    auto skeleton = Skeleton3D::Create(*view);
    ASSERT_TRUE(skeleton.has_value());

    auto pose = Pose3D::Create(3);
    ASSERT_TRUE(pose.has_value());
    ASSERT_TRUE(skeleton->writeBindPose(*pose).has_value());

    std::vector<float> matrices(3U * 16U, 0.0F);
    ASSERT_TRUE(skeleton->composeSkinningMatrices(*pose, matrices).has_value());

    // The chain offsets each joint one unit along X from its parent, and the inverse bind is
    // identity, so joint N's translation accumulates to N. Column-major: translation sits at
    // indices 12, 13, 14.
    EXPECT_FLOAT_EQ(matrices[12], 0.0F);
    EXPECT_FLOAT_EQ(matrices[16 + 12], 1.0F);
    EXPECT_FLOAT_EQ(matrices[32 + 12], 2.0F);

    // A wrongly sized span is rejected rather than partially filled.
    std::vector<float> tooSmall(16U, 0.0F);
    const auto failure = skeleton->composeSkinningMatrices(*pose, tooSmall);
    ASSERT_FALSE(failure.has_value());
    EXPECT_EQ(failure.error().code, Animation3DErrorCode::InvalidArgument);
}

} // namespace
} // namespace Tina::Animation3D
