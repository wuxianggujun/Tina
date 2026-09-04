#include "AnimationTestSupport.hpp"

#include <tina/animation3d/IkSolver3D.hpp>
#include <tina/animation3d/Skeleton3D.hpp>
#include <tina/math/Constants.hpp>
#include <tina/math/Quaternion.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

namespace Tina::Animation3D {
namespace {

struct Chain final {
    std::vector<std::byte> payload;
    AssetFormat::SkinnedMeshPayloadView view{};
};

[[nodiscard]] Chain makeThreeJointChain()
{
    Chain chain{};
    chain.payload = Testing::makeChainSkeletonPayload(3);
    auto parsed = AssetFormat::parseSkinnedMeshPayload(chain.payload);
    EXPECT_TRUE(parsed.has_value());
    if (parsed) {
        chain.view = *parsed;
    }
    return chain;
}

// The chain is root at origin, middle at x=1, tip at x=2, so each bone is 1 unit long and
// full reach is 2.
TEST(IkSolver3DTests, ReachesATargetInsideItsReach)
{
    const Chain chain = makeThreeJointChain();
    auto skeleton = Skeleton3D::Create(chain.view);
    ASSERT_TRUE(skeleton.has_value());
    auto pose = Pose3D::Create(3);
    ASSERT_TRUE(pose.has_value());
    ASSERT_TRUE(skeleton->writeBindPose(*pose).has_value());

    // A target well inside reach, off the chain's axis so the solve has to bend.
    const Math::Vec3 target{1.0F, 1.0F, 0.0F};
    const TwoBoneIkDesc desc{
        .rootJoint = 0,
        .middleJoint = 1,
        .tipJoint = 2,
        .targetPosition = target,
        .poleTargetPosition = Math::Vec3{0.0F, 0.0F, 1.0F},
        .usePoleTarget = true,
    };
    ASSERT_TRUE(solveTwoBoneIk(*skeleton, desc, *pose).has_value());

    const auto tip = jointModelPosition(*skeleton, *pose, 2);
    ASSERT_TRUE(tip.has_value());
    EXPECT_NEAR(tip->x, target.x, 1.0e-3F);
    EXPECT_NEAR(tip->y, target.y, 1.0e-3F);
    EXPECT_NEAR(tip->z, target.z, 1.0e-3F);
}

// An out-of-reach target must point the chain at it, not stretch it. Bone length is fixed by
// the skeleton, and a stretched limb is a worse artefact than one that does not quite reach.
TEST(IkSolver3DTests, PointsAtAnUnreachableTargetWithoutStretchingBones)
{
    const Chain chain = makeThreeJointChain();
    auto skeleton = Skeleton3D::Create(chain.view);
    ASSERT_TRUE(skeleton.has_value());
    auto pose = Pose3D::Create(3);
    ASSERT_TRUE(pose.has_value());
    ASSERT_TRUE(skeleton->writeBindPose(*pose).has_value());

    const TwoBoneIkDesc desc{
        .rootJoint = 0,
        .middleJoint = 1,
        .tipJoint = 2,
        // 10 units away: five times the chain's full reach.
        .targetPosition = Math::Vec3{0.0F, 10.0F, 0.0F},
        .poleTargetPosition = Math::Vec3{0.0F, 0.0F, 1.0F},
        .usePoleTarget = true,
    };
    ASSERT_TRUE(solveTwoBoneIk(*skeleton, desc, *pose).has_value());

    const auto middle = jointModelPosition(*skeleton, *pose, 1);
    const auto tip = jointModelPosition(*skeleton, *pose, 2);
    ASSERT_TRUE(middle.has_value());
    ASSERT_TRUE(tip.has_value());

    // Bones keep their length.
    EXPECT_NEAR(Math::length(*middle), 1.0F, 1.0e-3F);
    EXPECT_NEAR(Math::length(*tip - *middle), 1.0F, 1.0e-3F);
    // And the chain is nearly straight, aimed at the target.
    EXPECT_NEAR(Math::length(*tip), 2.0F, 1.0e-2F);
    EXPECT_GT(tip->y, 1.9F);
}

// Weight 0 leaves the pose untouched, which is what lets a foot fade onto uneven ground
// rather than snapping to it.
TEST(IkSolver3DTests, ZeroWeightLeavesThePoseUntouchedAndPartialWeightBlends)
{
    const Chain chain = makeThreeJointChain();
    auto skeleton = Skeleton3D::Create(chain.view);
    ASSERT_TRUE(skeleton.has_value());

    auto untouched = Pose3D::Create(3);
    auto blended = Pose3D::Create(3);
    auto full = Pose3D::Create(3);
    ASSERT_TRUE(untouched.has_value());
    ASSERT_TRUE(blended.has_value());
    ASSERT_TRUE(full.has_value());
    ASSERT_TRUE(skeleton->writeBindPose(*untouched).has_value());
    ASSERT_TRUE(skeleton->writeBindPose(*blended).has_value());
    ASSERT_TRUE(skeleton->writeBindPose(*full).has_value());

    TwoBoneIkDesc desc{
        .rootJoint = 0,
        .middleJoint = 1,
        .tipJoint = 2,
        .targetPosition = Math::Vec3{1.0F, 1.0F, 0.0F},
        .poleTargetPosition = Math::Vec3{0.0F, 0.0F, 1.0F},
        .usePoleTarget = true,
        .weight = 0.0F,
    };
    ASSERT_TRUE(solveTwoBoneIk(*skeleton, desc, *untouched).has_value());
    EXPECT_FLOAT_EQ(untouched->at(0).rotation.w, skeleton->bindPose(0).rotation.w);
    EXPECT_FLOAT_EQ(untouched->at(1).rotation.w, skeleton->bindPose(1).rotation.w);

    desc.weight = 1.0F;
    ASSERT_TRUE(solveTwoBoneIk(*skeleton, desc, *full).has_value());
    desc.weight = 0.5F;
    ASSERT_TRUE(solveTwoBoneIk(*skeleton, desc, *blended).has_value());

    // The half-weight tip sits between the untouched and fully solved ones.
    const auto untouchedTip = jointModelPosition(*skeleton, *untouched, 2);
    const auto blendedTip = jointModelPosition(*skeleton, *blended, 2);
    const auto fullTip = jointModelPosition(*skeleton, *full, 2);
    ASSERT_TRUE(untouchedTip.has_value() && blendedTip.has_value() && fullTip.has_value());
    EXPECT_GT(blendedTip->y, untouchedTip->y);
    EXPECT_LT(blendedTip->y, fullTip->y);
}

// A chain assembled from the wrong joint indices still produces a pose -- just a wrong one --
// and nothing downstream can tell, so the parent relationship is checked rather than trusted.
TEST(IkSolver3DTests, RejectsAChainThatIsNotRootMiddleTip)
{
    const Chain chain = makeThreeJointChain();
    auto skeleton = Skeleton3D::Create(chain.view);
    ASSERT_TRUE(skeleton.has_value());
    auto pose = Pose3D::Create(3);
    ASSERT_TRUE(pose.has_value());
    ASSERT_TRUE(skeleton->writeBindPose(*pose).has_value());

    // Root and tip swapped: joint 0 is not the parent of joint 1 in this ordering.
    const TwoBoneIkDesc reversed{
        .rootJoint = 2,
        .middleJoint = 1,
        .tipJoint = 0,
        .targetPosition = Math::Vec3{1.0F, 1.0F, 0.0F},
    };
    const auto failure = solveTwoBoneIk(*skeleton, reversed, *pose);
    ASSERT_FALSE(failure.has_value());
    EXPECT_EQ(failure.error().code, Animation3DErrorCode::InvalidArgument);

    // Skipping the middle joint is also refused.
    const TwoBoneIkDesc skipping{
        .rootJoint = 0,
        .middleJoint = 2,
        .tipJoint = 1,
        .targetPosition = Math::Vec3{1.0F, 1.0F, 0.0F},
    };
    EXPECT_FALSE(solveTwoBoneIk(*skeleton, skipping, *pose).has_value());

    // A non-finite target is refused without touching the pose.
    const TwoBoneIkDesc nonFinite{
        .rootJoint = 0,
        .middleJoint = 1,
        .tipJoint = 2,
        .targetPosition = Math::Vec3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
    };
    EXPECT_FALSE(solveTwoBoneIk(*skeleton, nonFinite, *pose).has_value());
    EXPECT_TRUE(Scene::isValid(pose->at(0)));
}

TEST(IkSolver3DTests, RejectsWeightsOutsideTheDocumentedBlendRange)
{
    const Chain chain = makeThreeJointChain();
    auto skeleton = Skeleton3D::Create(chain.view);
    ASSERT_TRUE(skeleton.has_value());
    auto pose = Pose3D::Create(3);
    ASSERT_TRUE(pose.has_value());
    ASSERT_TRUE(skeleton->writeBindPose(*pose).has_value());

    TwoBoneIkDesc desc{
        .rootJoint = 0,
        .middleJoint = 1,
        .tipJoint = 2,
        .targetPosition = Math::Vec3{1.0F, 1.0F, 0.0F},
        .poleTargetPosition = Math::Vec3{0.0F, 0.0F, 1.0F},
        .usePoleTarget = true,
        .weight = -0.01F,
    };
    EXPECT_FALSE(solveTwoBoneIk(*skeleton, desc, *pose).has_value());

    desc.weight = 1.01F;
    EXPECT_FALSE(solveTwoBoneIk(*skeleton, desc, *pose).has_value());
    EXPECT_EQ(pose->at(0), skeleton->bindPose(0));
    EXPECT_EQ(pose->at(1), skeleton->bindPose(1));
}

TEST(IkSolver3DTests, RejectsAnImpossibleReachIntervalBeforeClamping)
{
    const Chain chain = makeThreeJointChain();
    auto skeleton = Skeleton3D::Create(chain.view);
    ASSERT_TRUE(skeleton.has_value());
    auto pose = Pose3D::Create(3);
    ASSERT_TRUE(pose.has_value());
    ASSERT_TRUE(skeleton->writeBindPose(*pose).has_value());

    // Make the lower bone much shorter than the upper. A 0.5 maximum-reach fraction is then
    // below the physical minimum reach, which would make std::clamp(low, high) invalid.
    pose->at(2).position = Math::Vec3{0.1F, 0.0F, 0.0F};
    const TwoBoneIkDesc desc{
        .rootJoint = 0,
        .middleJoint = 1,
        .tipJoint = 2,
        .targetPosition = Math::Vec3{1.0F, 1.0F, 0.0F},
        .poleTargetPosition = Math::Vec3{0.0F, 0.0F, 1.0F},
        .usePoleTarget = true,
        .maximumReachFraction = 0.5F,
    };
    const auto result = solveTwoBoneIk(*skeleton, desc, *pose);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, Animation3DErrorCode::InvalidArgument);
}

TEST(IkSolver3DTests, RejectsScaleThatTheAnalyticSolverDoesNotModel)
{
    const Chain chain = makeThreeJointChain();
    auto skeleton = Skeleton3D::Create(chain.view);
    ASSERT_TRUE(skeleton.has_value());
    auto pose = Pose3D::Create(3);
    ASSERT_TRUE(pose.has_value());
    ASSERT_TRUE(skeleton->writeBindPose(*pose).has_value());

    pose->at(1).scale = Math::Vec3{2.0F, 1.0F, 1.0F};
    const TwoBoneIkDesc desc{
        .rootJoint = 0,
        .middleJoint = 1,
        .tipJoint = 2,
        .targetPosition = Math::Vec3{1.0F, 1.0F, 0.0F},
        .poleTargetPosition = Math::Vec3{0.0F, 0.0F, 1.0F},
        .usePoleTarget = true,
    };
    const auto result = solveTwoBoneIk(*skeleton, desc, *pose);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, Animation3DErrorCode::InvalidArgument);
    EXPECT_FLOAT_EQ(pose->at(1).scale.x, 2.0F);
}

} // namespace
} // namespace Tina::Animation3D
