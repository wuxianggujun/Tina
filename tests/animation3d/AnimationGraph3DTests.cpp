#include "AnimationTestSupport.hpp"

#include <tina/animation3d/AnimationGraph3D.hpp>
#include <tina/animation3d/BlendTree3D.hpp>
#include <tina/animation3d/ClipSampler3D.hpp>
#include <tina/animation3d/Skeleton3D.hpp>
#include <tina/core/memory/CountingMemoryResource.hpp>
#include <tina/core/memory/MemoryTracker.hpp>

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace Tina::Animation3D {
namespace {

// Two joints; clip A moves joint 1 to x=10, clip B moves it to x=-10, both over 1 second.
struct GraphFixture final {
    std::vector<std::byte> meshPayload;
    std::vector<std::byte> clipAPayload;
    std::vector<std::byte> clipBPayload;
    AssetFormat::SkinnedMeshPayloadView meshView{};
    AssetFormat::AnimationClip3DPayloadView clipAView{};
    AssetFormat::AnimationClip3DPayloadView clipBView{};
};

[[nodiscard]] GraphFixture makeGraphFixture()
{
    GraphFixture fixture{};
    fixture.meshPayload = Testing::makeChainSkeletonPayload(2);
    fixture.clipAPayload = Testing::makeTranslationClipPayload(2, 1, 0.0F, 10.0F, 1.0F);
    fixture.clipBPayload = Testing::makeTranslationClipPayload(2, 1, 0.0F, -10.0F, 1.0F);
    auto mesh = AssetFormat::parseSkinnedMeshPayload(fixture.meshPayload);
    auto clipA = AssetFormat::parseAnimationClip3DPayload(fixture.clipAPayload);
    auto clipB = AssetFormat::parseAnimationClip3DPayload(fixture.clipBPayload);
    EXPECT_TRUE(mesh.has_value());
    EXPECT_TRUE(clipA.has_value());
    EXPECT_TRUE(clipB.has_value());
    if (mesh) {
        fixture.meshView = *mesh;
    }
    if (clipA) {
        fixture.clipAView = *clipA;
    }
    if (clipB) {
        fixture.clipBView = *clipB;
    }
    return fixture;
}

// A crossfade blends both states, and both sides keep advancing. A fading-out state that
// stopped advancing would freeze mid-stride and the blend would visibly stutter.
TEST(AnimationGraph3DTests, CrossfadeBlendsBothStatesAndCompletes)
{
    const GraphFixture fixture = makeGraphFixture();
    auto skeleton = Skeleton3D::Create(fixture.meshView);
    ASSERT_TRUE(skeleton.has_value());
    auto clipA = ClipSampler3D::Create(fixture.clipAView, 2);
    auto clipB = ClipSampler3D::Create(fixture.clipBView, 2);
    ASSERT_TRUE(clipA.has_value());
    ASSERT_TRUE(clipB.has_value());

    const std::array<const ClipSampler3D*, 2> clips{&*clipA, &*clipB};
    auto graph = AnimationGraph3D::Create(*skeleton, clips, {});
    ASSERT_TRUE(graph.has_value()) << (graph ? "" : graph.error().message);

    const LayerId base = graph->baseLayer();
    auto stateA = graph->addState(base, StateDesc{.clipIndex = 0});
    auto stateB = graph->addState(base, StateDesc{.clipIndex = 1});
    ASSERT_TRUE(stateA.has_value());
    ASSERT_TRUE(stateB.has_value());
    ASSERT_TRUE(graph->setState(base, *stateA).has_value());

    auto pose = Pose3D::Create(2);
    ASSERT_TRUE(pose.has_value());

    // Advance A halfway, so it sits at x=5.
    ASSERT_TRUE(graph->advance(Core::Duration{0.5}).has_value());
    ASSERT_TRUE(graph->evaluate(*pose).has_value());
    EXPECT_NEAR(pose->at(1).position.x, 5.0F, 1.0e-4F);

    // Crossfade to B over 1 second with a linear curve so the arithmetic is checkable.
    ASSERT_TRUE(graph->crossfadeTo(base, *stateB, Core::Duration{1.0},
                                   Gameplay::Easing::Linear)
                    .has_value());
    EXPECT_TRUE(*graph->isTransitioning(base));

    // A quarter second in, deliberately short of A's duration: a Loop clip advanced to
    // exactly its duration wraps to time 0, so stepping a full 0.5 here would sample A at
    // its first frame rather than its last and the blend arithmetic would say nothing about
    // the crossfade.
    //
    // A: 0.5 -> 0.75, x = 7.5. B restarted, 0 -> 0.25, x = -2.5. Transition alpha 0.25.
    ASSERT_TRUE(graph->advance(Core::Duration{0.25}).has_value());
    ASSERT_TRUE(graph->evaluate(*pose).has_value());
    EXPECT_NEAR(pose->at(1).position.x, 5.0F, 1.0e-3F);
    EXPECT_TRUE(*graph->isTransitioning(base));

    // Completing the transition leaves only B.
    ASSERT_TRUE(graph->advance(Core::Duration{0.75}).has_value());
    EXPECT_FALSE(*graph->isTransitioning(base));
    EXPECT_EQ(*graph->currentState(base), *stateB);
}

// A missing transition is refused rather than defaulted. A silent default duration hides the
// authoring gap for as long as the result merely looks a bit snappy.
TEST(AnimationGraph3DTests, RequestTransitionNeedsAnAuthoredTransition)
{
    const GraphFixture fixture = makeGraphFixture();
    auto skeleton = Skeleton3D::Create(fixture.meshView);
    ASSERT_TRUE(skeleton.has_value());
    auto clipA = ClipSampler3D::Create(fixture.clipAView, 2);
    auto clipB = ClipSampler3D::Create(fixture.clipBView, 2);
    ASSERT_TRUE(clipA.has_value() && clipB.has_value());
    const std::array<const ClipSampler3D*, 2> clips{&*clipA, &*clipB};
    auto graph = AnimationGraph3D::Create(*skeleton, clips, {});
    ASSERT_TRUE(graph.has_value());

    const LayerId base = graph->baseLayer();
    auto stateA = graph->addState(base, StateDesc{.clipIndex = 0});
    auto stateB = graph->addState(base, StateDesc{.clipIndex = 1});
    ASSERT_TRUE(stateA.has_value() && stateB.has_value());
    ASSERT_TRUE(graph->setState(base, *stateA).has_value());

    const auto refused = graph->requestTransition(base, *stateB);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, AnimationErrorCode::InvalidTransition);

    ASSERT_TRUE(graph->addTransition(base, TransitionDesc{.from = *stateA,
                                                          .to = *stateB,
                                                          .duration = Core::Duration{0.25}})
                    .has_value());
    EXPECT_TRUE(graph->requestTransition(base, *stateB).has_value());

    // A self-transition is meaningless and is refused at authoring time.
    EXPECT_FALSE(graph->addTransition(base, TransitionDesc{.from = *stateA, .to = *stateA})
                     .has_value());
}

// A masked layer writes only its own joints; everything else keeps what the base produced.
// The base layer itself cannot be masked, because the excluded joints would then hold
// whatever the pose buffer last contained.
TEST(AnimationGraph3DTests, MaskedLayerOverridesOnlyItsJointsAndBaseCannotBeMasked)
{
    const GraphFixture fixture = makeGraphFixture();
    auto skeleton = Skeleton3D::Create(fixture.meshView);
    ASSERT_TRUE(skeleton.has_value());
    auto clipA = ClipSampler3D::Create(fixture.clipAView, 2);
    auto clipB = ClipSampler3D::Create(fixture.clipBView, 2);
    ASSERT_TRUE(clipA.has_value() && clipB.has_value());
    const std::array<const ClipSampler3D*, 2> clips{&*clipA, &*clipB};
    auto graph = AnimationGraph3D::Create(*skeleton, clips, {});
    ASSERT_TRUE(graph.has_value());

    const LayerId base = graph->baseLayer();
    auto baseState = graph->addState(base, StateDesc{.clipIndex = 0});
    ASSERT_TRUE(baseState.has_value());
    ASSERT_TRUE(graph->setState(base, *baseState).has_value());

    // A mask covering nothing but joint 0, which no clip animates, so the overlay must not
    // disturb joint 1.
    JointMask mask{};
    mask.excludeAll();
    mask.include(0);
    auto overlay = graph->addLayer(LayerDesc{.mode = LayerBlendMode::Override,
                                             .weight = 1.0F,
                                             .mask = mask});
    ASSERT_TRUE(overlay.has_value());
    auto overlayState = graph->addState(*overlay, StateDesc{.clipIndex = 1});
    ASSERT_TRUE(overlayState.has_value());
    ASSERT_TRUE(graph->setState(*overlay, *overlayState).has_value());

    auto pose = Pose3D::Create(2);
    ASSERT_TRUE(pose.has_value());
    // Short of the clip duration on purpose: a Loop clip advanced to exactly its duration
    // wraps back to time 0, which would sample the clip's first frame and make this assert
    // about wrap behaviour rather than about masking.
    ASSERT_TRUE(graph->advance(Core::Duration{0.75}).has_value());
    ASSERT_TRUE(graph->evaluate(*pose).has_value());

    // Joint 1 is the base's, untouched by the masked overlay.
    EXPECT_NEAR(pose->at(1).position.x, 7.5F, 1.0e-4F);

    const auto masked = graph->setLayerMask(base, mask);
    ASSERT_FALSE(masked.has_value());
    EXPECT_EQ(masked.error().code, AnimationErrorCode::InvalidArgument);
}

// Root motion is removed from the pose because the caller applies it to the entity. Leaving
// it in and also reporting it moves the character twice, and the doubling reads as a tuning
// problem rather than a bug.
TEST(AnimationGraph3DTests, RootMotionIsReportedAndRemovedFromThePose)
{
    GraphFixture fixture{};
    fixture.meshPayload = Testing::makeChainSkeletonPayload(2);
    // The root joint itself moves, which is what root motion means.
    fixture.clipAPayload = Testing::makeTranslationClipPayload(2, 0, 0.0F, 4.0F, 1.0F);
    auto mesh = AssetFormat::parseSkinnedMeshPayload(fixture.meshPayload);
    auto clip = AssetFormat::parseAnimationClip3DPayload(fixture.clipAPayload);
    ASSERT_TRUE(mesh.has_value() && clip.has_value());

    auto skeleton = Skeleton3D::Create(*mesh);
    ASSERT_TRUE(skeleton.has_value());
    auto sampler = ClipSampler3D::Create(*clip, 2);
    ASSERT_TRUE(sampler.has_value());
    const std::array<const ClipSampler3D*, 1> clips{&*sampler};

    AnimationGraph3DConfig config{};
    config.rootMotion = RootMotionConfig{
        .enabled = true, .rootJoint = 0, .applyTranslationXZ = true, .applyRotation = false};
    auto graph = AnimationGraph3D::Create(*skeleton, clips, {}, config);
    ASSERT_TRUE(graph.has_value()) << (graph ? "" : graph.error().message);

    const LayerId base = graph->baseLayer();
    auto state = graph->addState(base, StateDesc{.clipIndex = 0});
    ASSERT_TRUE(state.has_value());
    ASSERT_TRUE(graph->setState(base, *state).has_value());

    auto pose = Pose3D::Create(2);
    ASSERT_TRUE(pose.has_value());
    ASSERT_TRUE(graph->advance(Core::Duration{0.25}).has_value());
    ASSERT_TRUE(graph->evaluate(*pose).has_value());

    // A quarter of a 4-unit clip is 1 unit of delta.
    EXPECT_NEAR(graph->rootMotion().translation.x, 1.0F, 1.0e-3F);
    // And the root joint is back at its bind position, so applying the delta moves the
    // character exactly once.
    EXPECT_NEAR(pose->at(0).position.x, skeleton->bindPose(0).position.x, 1.0e-4F);
}

TEST(AnimationGraph3DTests, RootMotionPreservesDirectionAcrossABackwardLoopWrap)
{
    const auto meshPayload = Testing::makeChainSkeletonPayload(2);
    const auto clipPayload = Testing::makeTranslationClipPayload(
        2, 0, 0.0F, 4.0F, 1.0F, AssetFormat::AnimationClip3DPlaybackMode::Loop);
    auto mesh = AssetFormat::parseSkinnedMeshPayload(meshPayload);
    auto clip = AssetFormat::parseAnimationClip3DPayload(clipPayload);
    ASSERT_TRUE(mesh.has_value() && clip.has_value());

    auto skeleton = Skeleton3D::Create(*mesh);
    auto sampler = ClipSampler3D::Create(*clip, 2);
    ASSERT_TRUE(skeleton.has_value() && sampler.has_value());
    const std::array<const ClipSampler3D*, 1> clips{&*sampler};

    AnimationGraph3DConfig config{};
    config.rootMotion = RootMotionConfig{
        .enabled = true, .rootJoint = 0, .applyTranslationXZ = true, .applyRotation = false};
    auto graph = AnimationGraph3D::Create(*skeleton, clips, {}, config);
    ASSERT_TRUE(graph.has_value());
    const LayerId base = graph->baseLayer();
    auto state = graph->addState(base, StateDesc{.clipIndex = 0, .speed = -1.0F});
    ASSERT_TRUE(state.has_value());
    ASSERT_TRUE(graph->setState(base, *state).has_value());

    ASSERT_TRUE(graph->advance(Core::Duration{0.25}).has_value());
    EXPECT_TRUE(graph->rootMotion().wrapped);
    EXPECT_NEAR(graph->rootMotion().translation.x, -1.0F, 1.0e-3F);
}

TEST(AnimationGraph3DTests, RootMotionReversesAtAPingPongEndpoint)
{
    const auto meshPayload = Testing::makeChainSkeletonPayload(2);
    const auto clipPayload = Testing::makeTranslationClipPayload(
        2, 0, 0.0F, 4.0F, 1.0F, AssetFormat::AnimationClip3DPlaybackMode::PingPong);
    auto mesh = AssetFormat::parseSkinnedMeshPayload(meshPayload);
    auto clip = AssetFormat::parseAnimationClip3DPayload(clipPayload);
    ASSERT_TRUE(mesh.has_value() && clip.has_value());

    auto skeleton = Skeleton3D::Create(*mesh);
    auto sampler = ClipSampler3D::Create(*clip, 2);
    ASSERT_TRUE(skeleton.has_value() && sampler.has_value());
    const std::array<const ClipSampler3D*, 1> clips{&*sampler};

    AnimationGraph3DConfig config{};
    config.rootMotion = RootMotionConfig{
        .enabled = true, .rootJoint = 0, .applyTranslationXZ = true, .applyRotation = false};
    auto graph = AnimationGraph3D::Create(*skeleton, clips, {}, config);
    ASSERT_TRUE(graph.has_value());
    const LayerId base = graph->baseLayer();
    auto state = graph->addState(base, StateDesc{.clipIndex = 0});
    ASSERT_TRUE(state.has_value());
    ASSERT_TRUE(graph->setState(base, *state).has_value());

    // 0 -> 4 reaches the far endpoint, then 4 -> 3 runs one quarter backward: net +3.
    ASSERT_TRUE(graph->advance(Core::Duration{1.25}).has_value());
    EXPECT_TRUE(graph->rootMotion().wrapped);
    EXPECT_NEAR(graph->rootMotion().translation.x, 3.0F, 1.0e-3F);
}

// Evaluation must not allocate: it runs once per character per frame, and a per-frame
// allocation in an animation system is the kind of cost that only shows up in a crowd.
TEST(AnimationGraph3DTests, AdvanceAndEvaluateAreAllocationFree)
{
    const GraphFixture fixture = makeGraphFixture();
    auto skeleton = Skeleton3D::Create(fixture.meshView);
    ASSERT_TRUE(skeleton.has_value());
    auto clipA = ClipSampler3D::Create(fixture.clipAView, 2);
    auto clipB = ClipSampler3D::Create(fixture.clipBView, 2);
    ASSERT_TRUE(clipA.has_value() && clipB.has_value());
    const std::array<const ClipSampler3D*, 2> clips{&*clipA, &*clipB};

    Core::MemoryTracker tracker{};
    Core::CountingMemoryResource resource{tracker, Core::MemoryTag::Animation3D,
                                          *std::pmr::get_default_resource()};
    AnimationGraph3DConfig config{};
    config.memoryResource = &resource;
    auto graph = AnimationGraph3D::Create(*skeleton, clips, {}, config);
    ASSERT_TRUE(graph.has_value());

    const LayerId base = graph->baseLayer();
    auto stateA = graph->addState(base, StateDesc{.clipIndex = 0});
    auto stateB = graph->addState(base, StateDesc{.clipIndex = 1});
    ASSERT_TRUE(stateA.has_value() && stateB.has_value());
    ASSERT_TRUE(graph->setState(base, *stateA).has_value());
    auto pose = Pose3D::Create(2, resource);
    ASSERT_TRUE(pose.has_value());
    // Warm up once so first-touch effects are not counted.
    ASSERT_TRUE(graph->advance(Core::Duration{0.1}).has_value());
    ASSERT_TRUE(graph->evaluate(*pose).has_value());

    const Core::MemoryStatistics before = tracker.snapshot(Core::MemoryTag::Animation3D);
    // A crossfade is the heaviest path: it samples two states and blends them.
    ASSERT_TRUE(graph->crossfadeTo(base, *stateB, Core::Duration{0.5}).has_value());
    for (int frame = 0; frame < 20; ++frame) {
        ASSERT_TRUE(graph->advance(Core::Duration{1.0 / 60.0}).has_value());
        ASSERT_TRUE(graph->evaluate(*pose).has_value());
    }
    const Core::MemoryStatistics after = tracker.snapshot(Core::MemoryTag::Animation3D);
    EXPECT_EQ(before.allocationCount, after.allocationCount);
}

// A handle from another graph must not address this graph's slots. States are never erased, so
// the index alone is stable -- the owner token is what makes a foreign handle detectable.
TEST(AnimationGraph3DTests, RejectsHandlesFromAnotherGraphAndDefaultHandles)
{
    const GraphFixture fixture = makeGraphFixture();
    auto skeleton = Skeleton3D::Create(fixture.meshView);
    ASSERT_TRUE(skeleton.has_value());
    auto clipA = ClipSampler3D::Create(fixture.clipAView, 2);
    ASSERT_TRUE(clipA.has_value());
    const std::array<const ClipSampler3D*, 1> clips{&*clipA};

    auto first = AnimationGraph3D::Create(*skeleton, clips, {});
    auto second = AnimationGraph3D::Create(*skeleton, clips, {});
    ASSERT_TRUE(first.has_value() && second.has_value());

    auto foreignState = second->addState(second->baseLayer(), StateDesc{.clipIndex = 0});
    ASSERT_TRUE(foreignState.has_value());

    const auto rejected = first->setState(first->baseLayer(), *foreignState);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, AnimationErrorCode::InvalidHandle);

    EXPECT_FALSE(first->setState(LayerId{}, *foreignState).has_value());
    EXPECT_FALSE(first->addState(LayerId{}, StateDesc{}).has_value());
}

// Evaluating before any state is bound fails rather than producing whatever the buffer held.
TEST(AnimationGraph3DTests, EvaluateBeforeBindingFailsAndSkinningNeedsAnEvaluatedPose)
{
    const GraphFixture fixture = makeGraphFixture();
    auto skeleton = Skeleton3D::Create(fixture.meshView);
    ASSERT_TRUE(skeleton.has_value());
    auto clipA = ClipSampler3D::Create(fixture.clipAView, 2);
    ASSERT_TRUE(clipA.has_value());
    const std::array<const ClipSampler3D*, 1> clips{&*clipA};
    auto graph = AnimationGraph3D::Create(*skeleton, clips, {});
    ASSERT_TRUE(graph.has_value());

    auto pose = Pose3D::Create(2);
    ASSERT_TRUE(pose.has_value());
    const auto unbound = graph->evaluate(*pose);
    ASSERT_FALSE(unbound.has_value());
    EXPECT_EQ(unbound.error().code, AnimationErrorCode::NotBound);

    std::vector<float> matrices(2U * 16U, 0.0F);
    const auto noPose = graph->writeSkinningMatrices(matrices);
    ASSERT_FALSE(noPose.has_value());
    EXPECT_EQ(noPose.error().code, AnimationErrorCode::NotBound);

    const LayerId base = graph->baseLayer();
    auto state = graph->addState(base, StateDesc{.clipIndex = 0});
    ASSERT_TRUE(state.has_value());
    ASSERT_TRUE(graph->setState(base, *state).has_value());
    ASSERT_TRUE(graph->advance(Core::Duration{0.1}).has_value());
    ASSERT_TRUE(graph->evaluate(*pose).has_value());
    EXPECT_TRUE(graph->writeSkinningMatrices(matrices).has_value());
}

TEST(AnimationGraph3DTests, RejectsUnknownEnumsAndStatesFromAnotherLayer)
{
    const GraphFixture fixture = makeGraphFixture();
    auto skeleton = Skeleton3D::Create(fixture.meshView);
    auto clipA = ClipSampler3D::Create(fixture.clipAView, 2);
    auto clipB = ClipSampler3D::Create(fixture.clipBView, 2);
    ASSERT_TRUE(skeleton.has_value() && clipA.has_value() && clipB.has_value());
    const std::array<const ClipSampler3D*, 2> clips{&*clipA, &*clipB};
    auto graph = AnimationGraph3D::Create(*skeleton, clips, {});
    ASSERT_TRUE(graph.has_value());

    const auto unknownLayer = graph->addLayer(
        LayerDesc{.mode = static_cast<LayerBlendMode>(0xFFU)});
    ASSERT_FALSE(unknownLayer.has_value());
    EXPECT_EQ(unknownLayer.error().code, AnimationErrorCode::InvalidArgument);
    EXPECT_FALSE(graph->addLayer(LayerDesc{.mode = LayerBlendMode::Override,
                                           .referenceClipIndex = 0})
                     .has_value());

    const LayerId base = graph->baseLayer();
    const auto unknownState = graph->addState(
        base, StateDesc{.kind = static_cast<StateSourceKind>(0xFFU)});
    ASSERT_FALSE(unknownState.has_value());
    EXPECT_EQ(unknownState.error().code, AnimationErrorCode::InvalidArgument);

    auto overlay = graph->addLayer(LayerDesc{});
    ASSERT_TRUE(overlay.has_value());
    auto baseState = graph->addState(base, StateDesc{.clipIndex = 0});
    auto overlayState = graph->addState(*overlay, StateDesc{.clipIndex = 1});
    ASSERT_TRUE(baseState.has_value() && overlayState.has_value());
    ASSERT_TRUE(graph->setState(base, *baseState).has_value());

    const auto wrongSet = graph->setState(base, *overlayState);
    ASSERT_FALSE(wrongSet.has_value());
    EXPECT_EQ(wrongSet.error().code, AnimationErrorCode::InvalidHandle);
    EXPECT_FALSE(graph->crossfadeTo(base, *overlayState, Core::Duration{0.25}).has_value());
    EXPECT_FALSE(graph->requestTransition(base, *overlayState).has_value());
    EXPECT_FALSE(graph->addTransition(
                          base, TransitionDesc{.from = *baseState, .to = *overlayState})
                     .has_value());
}

TEST(AnimationGraph3DTests, MutableBlendTreeInstanceCanBackOnlyOneState)
{
    const GraphFixture fixture = makeGraphFixture();
    auto skeleton = Skeleton3D::Create(fixture.meshView);
    auto clip = ClipSampler3D::Create(fixture.clipAView, 2);
    ASSERT_TRUE(skeleton.has_value() && clip.has_value());
    const std::array<const ClipSampler3D*, 1> clips{&*clip};
    const std::array<BlendTreeNodeDesc, 1> nodes{
        BlendTreeNodeDesc{.kind = BlendTreeNodeKind::Clip, .clipIndex = 0}};
    auto tree = BlendTree3D::Create(
        BlendTree3DDesc{.nodes = nodes, .rootNode = 0}, *skeleton, clips);
    ASSERT_TRUE(tree.has_value());

    const std::array<BlendTree3D*, 2> duplicateTrees{&*tree, &*tree};
    const auto duplicateGraph = AnimationGraph3D::Create(*skeleton, clips, duplicateTrees);
    ASSERT_FALSE(duplicateGraph.has_value());
    EXPECT_EQ(duplicateGraph.error().code, AnimationErrorCode::InvalidArgument);

    const std::array<BlendTree3D*, 1> trees{&*tree};
    auto graph = AnimationGraph3D::Create(*skeleton, clips, trees);
    ASSERT_TRUE(graph.has_value());
    const StateDesc treeState{.kind = StateSourceKind::BlendTree, .blendTreeIndex = 0};
    ASSERT_TRUE(graph->addState(graph->baseLayer(), treeState).has_value());
    const auto duplicateState = graph->addState(graph->baseLayer(), treeState);
    ASSERT_FALSE(duplicateState.has_value());
    EXPECT_EQ(duplicateState.error().code, AnimationErrorCode::InvalidArgument);
}

TEST(BlendTree3DTests, RejectsUnknownNodeKind)
{
    const GraphFixture fixture = makeGraphFixture();
    auto skeleton = Skeleton3D::Create(fixture.meshView);
    auto clip = ClipSampler3D::Create(fixture.clipAView, 2);
    ASSERT_TRUE(skeleton.has_value() && clip.has_value());
    const std::array<const ClipSampler3D*, 1> clips{&*clip};
    const std::array<BlendTreeNodeDesc, 1> nodes{
        BlendTreeNodeDesc{.kind = static_cast<BlendTreeNodeKind>(0xFFU)}};

    const auto tree = BlendTree3D::Create(
        BlendTree3DDesc{.nodes = nodes, .rootNode = 0}, *skeleton, clips);
    ASSERT_FALSE(tree.has_value());
    EXPECT_EQ(tree.error().code, AnimationErrorCode::InvalidBlendTree);
}

TEST(BlendTree3DTests, RejectsOversizedBlend1DAndInputsOnClipNodes)
{
    const GraphFixture fixture = makeGraphFixture();
    auto skeleton = Skeleton3D::Create(fixture.meshView);
    auto clip = ClipSampler3D::Create(fixture.clipAView, 2);
    ASSERT_TRUE(skeleton.has_value() && clip.has_value());
    const std::array<const ClipSampler3D*, 1> clips{&*clip};

    std::vector<Core::u16> oversizedInputs(MaximumBlendTreeNodeCount + 1U, 0U);
    std::vector<float> oversizedThresholds(MaximumBlendTreeNodeCount + 1U, 0.0F);
    for (Core::usize index = 0; index < oversizedThresholds.size(); ++index) {
        oversizedThresholds[index] = static_cast<float>(index);
    }
    const std::array<BlendTreeNodeDesc, 2> oversizedNodes{
        BlendTreeNodeDesc{.kind = BlendTreeNodeKind::Clip, .clipIndex = 0},
        BlendTreeNodeDesc{.kind = BlendTreeNodeKind::Blend1D,
                          .inputs = oversizedInputs,
                          .thresholds = oversizedThresholds},
    };
    const auto oversized = BlendTree3D::Create(
        BlendTree3DDesc{.nodes = oversizedNodes, .rootNode = 1}, *skeleton, clips);
    ASSERT_FALSE(oversized.has_value());
    EXPECT_EQ(oversized.error().code, AnimationErrorCode::InvalidBlendTree);

    const std::array<Core::u16, 1> unexpectedInput{0};
    const std::array<BlendTreeNodeDesc, 2> clipWithInput{
        BlendTreeNodeDesc{.kind = BlendTreeNodeKind::Clip, .clipIndex = 0},
        BlendTreeNodeDesc{.kind = BlendTreeNodeKind::Clip,
                          .clipIndex = 0,
                          .inputs = unexpectedInput},
    };
    const auto malformedClip = BlendTree3D::Create(
        BlendTree3DDesc{.nodes = clipWithInput, .rootNode = 1}, *skeleton, clips);
    ASSERT_FALSE(malformedClip.has_value());
    EXPECT_EQ(malformedClip.error().code, AnimationErrorCode::InvalidBlendTree);
}

} // namespace
} // namespace Tina::Animation3D
