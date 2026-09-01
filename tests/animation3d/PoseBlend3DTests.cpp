#include <tina/animation3d/PoseBlend3D.hpp>
#include <tina/animation3d/Skeleton3D.hpp>
#include <tina/math/Constants.hpp>
#include <tina/math/Quaternion.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace Tina::Animation3D {
namespace {

[[nodiscard]] Pose3D makePose(Core::u16 jointCount)
{
    auto pose = Pose3D::Create(jointCount);
    EXPECT_TRUE(pose.has_value());
    return pose ? std::move(*pose) : Pose3D{};
}

TEST(PoseBlend3DTests, BlendPairInterpolatesAndHitsEndpointsExactly)
{
    Pose3D from = makePose(1);
    Pose3D to = makePose(1);
    Pose3D output = makePose(1);
    from.at(0).position = Math::Vec3{0.0F, 0.0F, 0.0F};
    to.at(0).position = Math::Vec3{10.0F, 0.0F, 0.0F};

    blendPair(output, from, to, 0.25F);
    EXPECT_FLOAT_EQ(output.at(0).position.x, 2.5F);

    // Endpoints are exact rather than interpolated, so a finished blend lands on its target
    // instead of one float epsilon short of it.
    blendPair(output, from, to, 0.0F);
    EXPECT_FLOAT_EQ(output.at(0).position.x, 0.0F);
    blendPair(output, from, to, 1.0F);
    EXPECT_FLOAT_EQ(output.at(0).position.x, 10.0F);

    // Out of range clamps rather than extrapolating: a weight arriving mid-transition must
    // never be able to produce a pose beyond either input.
    blendPair(output, from, to, 2.0F);
    EXPECT_FLOAT_EQ(output.at(0).position.x, 10.0F);
    blendPair(output, from, to, -1.0F);
    EXPECT_FLOAT_EQ(output.at(0).position.x, 0.0F);
}

// A NaN weight holds the destination rather than snapping to the source. A caller's weight
// computation breaking should not also destroy the pose it was blending into.
TEST(PoseBlend3DTests, NonFiniteAlphaHoldsTheDestination)
{
    Pose3D destination = makePose(1);
    Pose3D source = makePose(1);
    destination.at(0).position = Math::Vec3{1.0F, 2.0F, 3.0F};
    source.at(0).position = Math::Vec3{9.0F, 9.0F, 9.0F};

    blendOverwrite(destination, source, std::numeric_limits<float>::quiet_NaN(), JointMask{});
    EXPECT_FLOAT_EQ(destination.at(0).position.x, 1.0F);
    EXPECT_FLOAT_EQ(destination.at(0).position.y, 2.0F);
}

// Rotation goes through slerp, never component-wise lerp. Near 180 degrees apart a
// component-wise blend collapses toward zero length and the joint snaps; the giveaway is that
// the interpolated quaternion is no longer unit length.
TEST(PoseBlend3DTests, RotationBlendStaysUnitLengthNearOppositeOrientations)
{
    Pose3D from = makePose(1);
    Pose3D to = makePose(1);
    Pose3D output = makePose(1);
    from.at(0).rotation = Math::Quaternion{0.0F, 0.0F, 0.0F, 1.0F};
    // 170 degrees about Z: far enough that a component-wise lerp visibly shortens.
    to.at(0).rotation = Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F},
                                            Math::radians(170.0F));

    blendPair(output, from, to, 0.5F);
    EXPECT_NEAR(Math::lengthSquared(output.at(0).rotation), 1.0F, 1.0e-4F);

    // Halfway between identity and 170 degrees is 85 degrees.
    const float halfAngle = Math::radians(85.0F) * 0.5F;
    EXPECT_NEAR(output.at(0).rotation.z, std::sin(halfAngle), 1.0e-4F);
    EXPECT_NEAR(output.at(0).rotation.w, std::cos(halfAngle), 1.0e-4F);
}

// The mask decides which joints a layer may write. A masked-out joint keeps the value the
// layers below produced -- that is the whole point of an upper-body layer.
TEST(PoseBlend3DTests, OverwriteRespectsTheMask)
{
    Pose3D destination = makePose(2);
    Pose3D source = makePose(2);
    destination.at(0).position = Math::Vec3{1.0F, 0.0F, 0.0F};
    destination.at(1).position = Math::Vec3{1.0F, 0.0F, 0.0F};
    source.at(0).position = Math::Vec3{5.0F, 0.0F, 0.0F};
    source.at(1).position = Math::Vec3{5.0F, 0.0F, 0.0F};

    JointMask mask{};
    mask.excludeAll();
    mask.include(1);

    blendOverwrite(destination, source, 1.0F, mask);
    EXPECT_FLOAT_EQ(destination.at(0).position.x, 1.0F);
    EXPECT_FLOAT_EQ(destination.at(1).position.x, 5.0F);
}

// Additive subtracts an explicit reference. Getting the reference wrong is the most common
// additive defect: subtracting the bind pose when the clip was authored against its own first
// frame doubles every offset, which is why the reference has no default.
TEST(PoseBlend3DTests, AdditiveIsRelativeToItsReferenceAndIsANoOpAtZeroWeight)
{
    Pose3D destination = makePose(1);
    Pose3D source = makePose(1);
    Pose3D reference = makePose(1);
    destination.at(0).position = Math::Vec3{1.0F, 0.0F, 0.0F};
    // The source sits 3 units from its own reference, so it adds 3 -- not its absolute 5.
    reference.at(0).position = Math::Vec3{2.0F, 0.0F, 0.0F};
    source.at(0).position = Math::Vec3{5.0F, 0.0F, 0.0F};

    Pose3D atFullWeight = makePose(1);
    atFullWeight.copyFrom(destination);
    blendAdditive(atFullWeight, source, reference, 1.0F, JointMask{});
    EXPECT_FLOAT_EQ(atFullWeight.at(0).position.x, 4.0F);

    Pose3D atHalfWeight = makePose(1);
    atHalfWeight.copyFrom(destination);
    blendAdditive(atHalfWeight, source, reference, 0.5F, JointMask{});
    EXPECT_FLOAT_EQ(atHalfWeight.at(0).position.x, 2.5F);

    Pose3D atZeroWeight = makePose(1);
    atZeroWeight.copyFrom(destination);
    blendAdditive(atZeroWeight, source, reference, 0.0F, JointMask{});
    EXPECT_FLOAT_EQ(atZeroWeight.at(0).position.x, 1.0F);
}

// An additive source equal to its reference must change nothing, at any weight. If it did,
// every additive layer would drift whenever its clip sat at the neutral pose.
TEST(PoseBlend3DTests, AdditiveWithSourceEqualToReferenceChangesNothing)
{
    Pose3D destination = makePose(1);
    destination.at(0).position = Math::Vec3{1.0F, 2.0F, 3.0F};
    destination.at(0).rotation =
        Math::fromAxisAngle(Math::Vec3{0.0F, 1.0F, 0.0F}, Math::radians(30.0F));
    destination.at(0).scale = Math::Vec3{2.0F, 2.0F, 2.0F};

    Pose3D neutral = makePose(1);
    neutral.at(0).position = Math::Vec3{7.0F, 8.0F, 9.0F};
    neutral.at(0).rotation =
        Math::fromAxisAngle(Math::Vec3{1.0F, 0.0F, 0.0F}, Math::radians(45.0F));
    neutral.at(0).scale = Math::Vec3{3.0F, 3.0F, 3.0F};

    Pose3D result = makePose(1);
    result.copyFrom(destination);
    blendAdditive(result, neutral, neutral, 1.0F, JointMask{});

    EXPECT_NEAR(result.at(0).position.x, 1.0F, 1.0e-5F);
    EXPECT_NEAR(result.at(0).scale.x, 2.0F, 1.0e-5F);
    EXPECT_NEAR(result.at(0).rotation.y, destination.at(0).rotation.y, 1.0e-5F);
    EXPECT_NEAR(result.at(0).rotation.w, destination.at(0).rotation.w, 1.0e-5F);
}

TEST(PoseBlend3DTests, DetectsNonFinitePosesAndRenormalizes)
{
    Pose3D pose = makePose(1);
    EXPECT_TRUE(isPoseFinite(pose));

    pose.at(0).position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(isPoseFinite(pose));

    // A zero-length rotation is not a rotation, so it must also read as invalid rather than
    // being silently normalized into an arbitrary orientation downstream.
    Pose3D degenerate = makePose(1);
    degenerate.at(0).rotation = Math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F};
    EXPECT_FALSE(isPoseFinite(degenerate));

    Pose3D drifted = makePose(1);
    drifted.at(0).rotation = Math::Quaternion{0.0F, 0.0F, 0.0F, 2.0F};
    normalizeRotations(drifted);
    EXPECT_NEAR(Math::lengthSquared(drifted.at(0).rotation), 1.0F, 1.0e-5F);
}

} // namespace
} // namespace Tina::Animation3D
