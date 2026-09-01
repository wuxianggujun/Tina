#include "AnimationTestSupport.hpp"

#include <tina/animation3d/ClipSampler3D.hpp>
#include <tina/animation3d/Skeleton3D.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

namespace Tina::Animation3D {
namespace {

struct Fixture final {
    std::vector<std::byte> meshPayload;
    std::vector<std::byte> clipPayload;
    AssetFormat::SkinnedMeshPayloadView meshView{};
    AssetFormat::AnimationClip3DPayloadView clipView{};
};

[[nodiscard]] Fixture makeFixture(
    float duration = 1.0F,
    AssetFormat::AnimationClip3DPlaybackMode mode = AssetFormat::AnimationClip3DPlaybackMode::Loop)
{
    Fixture fixture{};
    fixture.meshPayload = Testing::makeChainSkeletonPayload(2);
    fixture.clipPayload = Testing::makeTranslationClipPayload(2, 1, 0.0F, 10.0F, duration, mode);
    auto mesh = AssetFormat::parseSkinnedMeshPayload(fixture.meshPayload);
    auto clip = AssetFormat::parseAnimationClip3DPayload(fixture.clipPayload);
    EXPECT_TRUE(mesh.has_value());
    EXPECT_TRUE(clip.has_value());
    if (mesh) {
        fixture.meshView = *mesh;
    }
    if (clip) {
        fixture.clipView = *clip;
    }
    return fixture;
}

// Sampling is const, which is what lets a crossfade read one clip at two times in one frame.
// Animator3D could not: its evaluate step was private and always wrote its own buffers.
TEST(ClipSampler3DTests, SamplesTheSameClipAtTwoTimesWithoutAdvancingState)
{
    const Fixture fixture = makeFixture();
    auto skeleton = Skeleton3D::Create(fixture.meshView);
    ASSERT_TRUE(skeleton.has_value());
    auto sampler = ClipSampler3D::Create(fixture.clipView, skeleton->jointCount());
    ASSERT_TRUE(sampler.has_value()) << (sampler ? "" : sampler.error().message);

    auto early = Pose3D::Create(2);
    auto late = Pose3D::Create(2);
    ASSERT_TRUE(early.has_value());
    ASSERT_TRUE(late.has_value());

    ASSERT_TRUE(sampler->sample(0.25F, *skeleton, *early).has_value());
    ASSERT_TRUE(sampler->sample(0.75F, *skeleton, *late).has_value());
    EXPECT_FLOAT_EQ(early->at(1).position.x, 2.5F);
    EXPECT_FLOAT_EQ(late->at(1).position.x, 7.5F);

    // Re-sampling the earlier time still gives the earlier value: nothing advanced.
    ASSERT_TRUE(sampler->sample(0.25F, *skeleton, *early).has_value());
    EXPECT_FLOAT_EQ(early->at(1).position.x, 2.5F);
}

// A joint no track drives receives the bind pose, not whatever the buffer held. Otherwise the
// same clip would produce different results depending on which pose it was handed, which makes
// sampling order observable.
TEST(ClipSampler3DTests, UntrackedJointsReceiveTheBindPose)
{
    const Fixture fixture = makeFixture();
    auto skeleton = Skeleton3D::Create(fixture.meshView);
    ASSERT_TRUE(skeleton.has_value());
    auto sampler = ClipSampler3D::Create(fixture.clipView, skeleton->jointCount());
    ASSERT_TRUE(sampler.has_value());

    auto pose = Pose3D::Create(2);
    ASSERT_TRUE(pose.has_value());
    // Poison the untracked joint so a failure to reset it is visible.
    pose->at(0).position = Math::Vec3{99.0F, 99.0F, 99.0F};
    ASSERT_TRUE(sampler->sample(0.5F, *skeleton, *pose).has_value());

    EXPECT_FLOAT_EQ(pose->at(0).position.x, skeleton->bindPose(0).position.x);
    EXPECT_FLOAT_EQ(pose->at(0).position.y, 0.0F);
    EXPECT_TRUE(sampler->animatesJoint(1));
    EXPECT_FALSE(sampler->animatesJoint(0));
}

// jointCount equality is the only compatibility signal the wire format carries: a clip has no
// skeleton identity or hash. Binding a mismatched pair would drive joint N of one rig with
// joint N of another.
TEST(ClipSampler3DTests, RejectsAClipWhoseJointCountDiffersFromTheSkeleton)
{
    const Fixture fixture = makeFixture();
    const auto sampler = ClipSampler3D::Create(fixture.clipView, 3);
    ASSERT_FALSE(sampler.has_value());
    EXPECT_EQ(sampler.error().code, AnimationErrorCode::SkeletonMismatch);
}

// The remainder has to survive the loop point. A delta computed only from (previous, current)
// loses a whole cycle whenever one advance spans the wrap -- which is what happens at low
// frame rates, and it shows up as a character that travels short.
TEST(ClipSampler3DTests, LoopAdvanceReportsWrapAndCycleCount)
{
    const Fixture fixture = makeFixture(1.0F, AssetFormat::AnimationClip3DPlaybackMode::Loop);
    auto skeleton = Skeleton3D::Create(fixture.meshView);
    ASSERT_TRUE(skeleton.has_value());
    auto sampler = ClipSampler3D::Create(fixture.clipView, skeleton->jointCount());
    ASSERT_TRUE(sampler.has_value());

    ClipPlayhead3D playhead = sampler->startPlayhead();
    auto stepped = sampler->advance(playhead, Core::Duration{0.4}, 1.0F);
    ASSERT_TRUE(stepped.has_value());
    EXPECT_FLOAT_EQ(stepped->timeSeconds, 0.4F);
    EXPECT_FALSE(stepped->wrappedThisAdvance);

    // Crossing the loop point once.
    stepped = sampler->advance(*stepped, Core::Duration{0.8}, 1.0F);
    ASSERT_TRUE(stepped.has_value());
    EXPECT_TRUE(stepped->wrappedThisAdvance);
    EXPECT_EQ(stepped->cyclesCompleted, 1U);
    EXPECT_NEAR(stepped->timeSeconds, 0.2F, 1.0e-5F);

    // A single advance spanning several cycles reports all of them rather than iterating.
    stepped = sampler->advance(sampler->startPlayhead(), Core::Duration{3.5}, 1.0F);
    ASSERT_TRUE(stepped.has_value());
    EXPECT_TRUE(stepped->wrappedThisAdvance);
    EXPECT_EQ(stepped->cyclesCompleted, 3U);
    EXPECT_NEAR(stepped->timeSeconds, 0.5F, 1.0e-5F);
}

TEST(ClipSampler3DTests, OnceClampsAndCompletesWhilePingPongReverses)
{
    const Fixture once = makeFixture(1.0F, AssetFormat::AnimationClip3DPlaybackMode::Once);
    auto skeleton = Skeleton3D::Create(once.meshView);
    ASSERT_TRUE(skeleton.has_value());
    auto onceSampler = ClipSampler3D::Create(once.clipView, skeleton->jointCount());
    ASSERT_TRUE(onceSampler.has_value());

    auto stepped = onceSampler->advance(onceSampler->startPlayhead(), Core::Duration{2.0}, 1.0F);
    ASSERT_TRUE(stepped.has_value());
    EXPECT_FLOAT_EQ(stepped->timeSeconds, 1.0F);
    EXPECT_TRUE(stepped->completed);

    const Fixture pingPong =
        makeFixture(1.0F, AssetFormat::AnimationClip3DPlaybackMode::PingPong);
    auto pingPongSampler = ClipSampler3D::Create(pingPong.clipView, skeleton->jointCount());
    ASSERT_TRUE(pingPongSampler.has_value());

    // Past the end, so it must be running backwards now.
    auto bounced =
        pingPongSampler->advance(pingPongSampler->startPlayhead(), Core::Duration{1.25}, 1.0F);
    ASSERT_TRUE(bounced.has_value());
    EXPECT_TRUE(bounced->playingBackward);
    EXPECT_NEAR(bounced->timeSeconds, 0.75F, 1.0e-5F);
}

TEST(ClipSampler3DTests, ZeroMovementDoesNotSynthesizeBoundaryEvents)
{
    const Fixture once = makeFixture(1.0F, AssetFormat::AnimationClip3DPlaybackMode::Once);
    auto skeleton = Skeleton3D::Create(once.meshView);
    ASSERT_TRUE(skeleton.has_value());
    auto onceSampler = ClipSampler3D::Create(once.clipView, skeleton->jointCount());
    ASSERT_TRUE(onceSampler.has_value());

    auto unchanged = onceSampler->advance(onceSampler->startPlayhead(), Core::Duration{0.0}, 1.0F);
    ASSERT_TRUE(unchanged.has_value());
    EXPECT_FALSE(unchanged->completed);
    EXPECT_FALSE(unchanged->wrappedThisAdvance);
    EXPECT_EQ(unchanged->cyclesCompleted, 0U);

    const Fixture loop = makeFixture(1.0F, AssetFormat::AnimationClip3DPlaybackMode::Loop);
    auto loopSampler = ClipSampler3D::Create(loop.clipView, skeleton->jointCount());
    ASSERT_TRUE(loopSampler.has_value());
    const ClipPlayhead3D backwardAtEnd = loopSampler->startPlayhead(true);
    unchanged = loopSampler->advance(backwardAtEnd, Core::Duration{1.0}, 0.0F);
    ASSERT_TRUE(unchanged.has_value());
    EXPECT_FLOAT_EQ(unchanged->timeSeconds, 1.0F);
    EXPECT_TRUE(unchanged->playingBackward);
    EXPECT_FALSE(unchanged->advancedBackwardThisAdvance);
    EXPECT_FALSE(unchanged->wrappedThisAdvance);
    EXPECT_EQ(unchanged->cyclesCompleted, 0U);
}

TEST(ClipSampler3DTests, PingPongKeepsItsBackwardLegAcrossAdvances)
{
    const Fixture fixture =
        makeFixture(1.0F, AssetFormat::AnimationClip3DPlaybackMode::PingPong);
    auto skeleton = Skeleton3D::Create(fixture.meshView);
    ASSERT_TRUE(skeleton.has_value());
    auto sampler = ClipSampler3D::Create(fixture.clipView, skeleton->jointCount());
    ASSERT_TRUE(sampler.has_value());

    auto bounced = sampler->advance(sampler->startPlayhead(), Core::Duration{1.25}, 1.0F);
    ASSERT_TRUE(bounced.has_value());
    EXPECT_TRUE(bounced->wrappedThisAdvance);
    EXPECT_EQ(bounced->cyclesCompleted, 1U);
    EXPECT_TRUE(bounced->playingBackward);
    EXPECT_FALSE(bounced->advancedBackwardThisAdvance);
    EXPECT_NEAR(bounced->timeSeconds, 0.75F, 1.0e-5F);

    const auto continued = sampler->advance(*bounced, Core::Duration{0.25}, 1.0F);
    ASSERT_TRUE(continued.has_value());
    EXPECT_FALSE(continued->wrappedThisAdvance);
    EXPECT_TRUE(continued->playingBackward);
    EXPECT_TRUE(continued->advancedBackwardThisAdvance);
    EXPECT_NEAR(continued->timeSeconds, 0.5F, 1.0e-5F);
}

TEST(ClipSampler3DTests, PingPongCountsEveryBoundaryRelativeToTheStartingPhase)
{
    const Fixture fixture =
        makeFixture(1.0F, AssetFormat::AnimationClip3DPlaybackMode::PingPong);
    auto skeleton = Skeleton3D::Create(fixture.meshView);
    ASSERT_TRUE(skeleton.has_value());
    auto sampler = ClipSampler3D::Create(fixture.clipView, skeleton->jointCount());
    ASSERT_TRUE(sampler.has_value());

    const ClipPlayhead3D offsetStart{.timeSeconds = 0.75F, .previousTimeSeconds = 0.75F};
    const auto advanced = sampler->advance(offsetStart, Core::Duration{2.5}, 1.0F);
    ASSERT_TRUE(advanced.has_value());
    EXPECT_TRUE(advanced->wrappedThisAdvance);
    EXPECT_EQ(advanced->cyclesCompleted, 3U);
    EXPECT_TRUE(advanced->playingBackward);
    EXPECT_NEAR(advanced->timeSeconds, 0.75F, 1.0e-5F);
}

TEST(ClipSampler3DTests, ExtremeFiniteLoopDeltaProducesAFiniteRemainder)
{
    const Fixture fixture = makeFixture();
    auto skeleton = Skeleton3D::Create(fixture.meshView);
    ASSERT_TRUE(skeleton.has_value());
    auto sampler = ClipSampler3D::Create(fixture.clipView, skeleton->jointCount());
    ASSERT_TRUE(sampler.has_value());

    const auto advanced = sampler->advance(sampler->startPlayhead(),
                                           Core::Duration{std::numeric_limits<double>::max()},
                                           1.0F);
    ASSERT_TRUE(advanced.has_value());
    EXPECT_TRUE(std::isfinite(advanced->timeSeconds));
    EXPECT_GE(advanced->timeSeconds, 0.0F);
    EXPECT_LT(advanced->timeSeconds, sampler->durationSeconds());
    EXPECT_EQ(advanced->cyclesCompleted, (std::numeric_limits<Core::u32>::max)());
}

// Negative speed is accepted here, unlike Animator3D::setPlaybackSpeed which required > 0.
// A state machine that plays a clip backwards is ordinary; refusing it forces callers to cook
// a reversed copy of every clip.
TEST(ClipSampler3DTests, AcceptsNegativeSpeedAndRejectsNonFiniteInput)
{
    const Fixture fixture = makeFixture();
    auto skeleton = Skeleton3D::Create(fixture.meshView);
    ASSERT_TRUE(skeleton.has_value());
    auto sampler = ClipSampler3D::Create(fixture.clipView, skeleton->jointCount());
    ASSERT_TRUE(sampler.has_value());

    ClipPlayhead3D playhead = sampler->startPlayhead();
    auto forward = sampler->advance(playhead, Core::Duration{0.5}, 1.0F);
    ASSERT_TRUE(forward.has_value());
    auto backward = sampler->advance(*forward, Core::Duration{0.25}, -1.0F);
    ASSERT_TRUE(backward.has_value());
    EXPECT_NEAR(backward->timeSeconds, 0.25F, 1.0e-5F);

    EXPECT_FALSE(sampler->advance(playhead, Core::Duration{-0.1}, 1.0F).has_value());
    EXPECT_FALSE(sampler
                     ->advance(playhead, Core::Duration{0.1},
                               std::numeric_limits<float>::quiet_NaN())
                     .has_value());
}

} // namespace
} // namespace Tina::Animation3D
