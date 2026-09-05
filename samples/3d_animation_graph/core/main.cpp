// A state machine crossfading between three locomotion clips, driving a skinned mesh.
//
// 3d_ik_chain proved a *computed* pose reaches the screen. It links Tina::Animation3D for
// solveTwoBoneIk and nothing else, so the module's other half -- ClipSampler3D, PoseBlend3D,
// BlendTree3D, AnimationGraph3D -- still had no sample. Those four are where the silent failures
// live, because every one of them produces a plausible-looking pose when it is wrong:
//
//   - sampling before advancing renders one frame behind, which looks like input lag
//   - advancing a state you are fading *out* of runs its clips at double rate
//   - a Blend1D built as a chain of Blend2 lets middle clips contribute where they should be silent
//   - a component-wise quaternion blend collapses near 180 degrees apart and the joint snaps
//
// None of those is an error return. So the evidence here is structural rather than a status check:
//
//   numeric  - the graph's own stats (transitions started, refused, evaluation failures) plus a
//              measured pose divergence between the two locomotion extremes
//   pixel    - two captures pinned to opposite ends of the speed parameter, which must differ
//
// The mesh is the same three-joint chain 3d_ik_chain uses, for the same reason: with one influence
// per vertex a wrong joint matrix shows as a misplaced quad rather than a subtle smear.

#include "Sample.hpp"

#include <tina/animation3d/AnimationGraph3D.hpp>
#include <tina/animation3d/BlendTree3D.hpp>
#include <tina/animation3d/ClipSampler3D.hpp>
#include <tina/animation3d/PoseBlend3D.hpp>
#include <tina/animation3d/Skeleton3D.hpp>
#include <tina/asset/AssetGpuMesh.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/AnimationClip3DPayload.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/SkinnedMeshPayload.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/text/ArgParser.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/math/Vec.hpp>
#include <tina/render/FrameResource.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/RunExitReason.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Tina::Core::u16;
using Tina::Core::u32;
using Tina::Core::u64;
using Tina::Core::u8;
using Tina::Core::usize;

constexpr std::string_view SampleName = "tina_sample_3d_animation_graph";
constexpr u64 DefaultFrameCount = 240;

// Root, middle, tip. The same chain 3d_ik_chain uses: three joints is the minimum that has a
// parent-of-a-parent, which is what makes a wrong composition order visible.
constexpr u16 JointCount = 3;
constexpr u16 RootJoint = 0;
constexpr u16 MiddleJoint = 1;
constexpr u16 TipJoint = 2;

constexpr float BoneLengthMeters = 1.0F;
constexpr float ChainReachMeters = BoneLengthMeters * 2.0F;

constexpr u32 SkinnedMeshBindingKey = 1;
constexpr u32 MaterialBindingKey = 1;

constexpr u16 VerticesPerBone = 4;
constexpr u16 IndicesPerBone = 6;
constexpr u16 BoneCount = 2;
constexpr u16 VertexCount = VerticesPerBone * BoneCount;
constexpr u16 IndexCount = IndicesPerBone * BoneCount;
constexpr float QuadHalfHeightMeters = 0.25F;

[[nodiscard]] std::vector<std::byte> buildSkinnedMeshPayload()
{
    std::array<Tina::AssetFormat::SkinnedMeshJointDesc, JointCount> joints{};
    std::vector<float> inverseBind;
    inverseBind.reserve(static_cast<usize>(JointCount) * 16U);

    for (u16 index = 0; index < JointCount; ++index)
    {
        Tina::AssetFormat::SkinnedMeshJointDesc& joint = joints[index];
        joint.parentJoint = index == 0U ? Tina::AssetFormat::SkinnedMeshWire::JointIndexNone
                                        : static_cast<u16>(index - 1U);
        joint.bindTranslation[0] = index == 0U ? 0.0F : BoneLengthMeters;
        // Named, unlike 3d_ik_chain's chain. The cooked joint order is a permutation the loader is
        // free to choose, so a name is the only stable way to say "the middle joint"; the rig
        // resolves these back through Skeleton3D::findJoint before authoring any clip.
        joint.name = index == RootJoint ? "root" : index == MiddleJoint ? "middle" : "tip";

        // Inverse of the bind global transform: a pure translation of index*BoneLength along X.
        // Column-major, so the translation is in elements 12..14.
        std::array<float, 16> matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        matrix[12] = -BoneLengthMeters * static_cast<float>(index);
        inverseBind.insert(inverseBind.end(), matrix.begin(), matrix.end());
    }

    std::vector<float> vertices;
    vertices.reserve(static_cast<usize>(VertexCount) *
                     Tina::AssetFormat::SkinnedMeshWire::FloatsPerVertex);
    std::vector<u16> jointIndices;
    jointIndices.reserve(static_cast<usize>(VertexCount) * 4U);
    std::vector<u16> jointWeights;
    jointWeights.reserve(static_cast<usize>(VertexCount) * 4U);
    std::vector<u32> indices;
    indices.reserve(IndexCount);

    for (u16 bone = 0; bone < BoneCount; ++bone)
    {
        const float nearX = BoneLengthMeters * static_cast<float>(bone);
        const float farX = nearX + BoneLengthMeters;
        const std::array<std::array<float, 2>, VerticesPerBone> corners{{
            {nearX, -QuadHalfHeightMeters},
            {farX, -QuadHalfHeightMeters},
            {farX, QuadHalfHeightMeters},
            {nearX, QuadHalfHeightMeters},
        }};

        for (const std::array<float, 2>& corner : corners)
        {
            // P3, N3 (+Z, facing the camera), T4, UV2.
            const std::array<float, Tina::AssetFormat::SkinnedMeshWire::FloatsPerVertex> vertex{
                corner[0], corner[1], 0.0F, 0.0F, 0.0F, 1.0F,
                1.0F,      0.0F,     0.0F, 1.0F, corner[0] * 0.5F, corner[1] + 0.5F};
            vertices.insert(vertices.end(), vertex.begin(), vertex.end());

            jointIndices.insert(jointIndices.end(), {bone, 0, 0, 0});
            jointWeights.insert(jointWeights.end(),
                                {Tina::AssetFormat::SkinnedMeshWire::WeightScale, 0, 0, 0});
        }

        const u32 base = static_cast<u32>(bone) * VerticesPerBone;
        indices.insert(indices.end(), {base, base + 1U, base + 2U,
                                       base, base + 2U, base + 3U});
    }

    const std::array<Tina::AssetFormat::StaticMeshSubmeshDesc, 1> submeshes{
        Tina::AssetFormat::StaticMeshSubmeshDesc{.firstIndex = 0, .indexCount = IndexCount}};

    auto payload =
        Tina::AssetFormat::writeSkinnedMeshPayloadBytes(Tina::AssetFormat::SkinnedMeshPayloadDesc{
            .boundsRadius = ChainReachMeters,
            .joints = joints,
            .inverseBindMatrices = inverseBind,
            .submeshes = submeshes,
            .vertices = vertices,
            .jointIndices = jointIndices,
            .jointWeights = jointWeights,
            .indices = indices,
        });
    if (!payload)
    {
        return {};
    }
    return std::move(*payload);
}

// The three locomotion clips. Each rotates the two driven joints about Z by a different
// amplitude, so a wrong blend weight shows as a limb at the wrong angle rather than as an
// absence of motion. Rotation rather than translation because translating a joint of a skinned
// chain detaches it from its parent's bone, which reads as a rendering bug rather than a pose.
//
// Z rotation keeps the chain in the XY plane the camera looks at head on, so every pose
// difference is a difference in the silhouette rather than in foreshortening.
// Each clip is a constant bend plus an oscillation about it: angle = base + swing*sin(phase).
//
// The constant term is what makes the evidence phase-independent, and it is not a trick -- a real
// run cycle is crouched throughout, not a neutral stance that happens to wobble. With a pure
// oscillation the two extremes would coincide whenever both sines crossed zero, and a capture
// landing there would report "the blend did nothing" for a perfectly correct blend.
struct ClipShape final {
    std::string_view name;
    // Middle joint, radians. The root gets a third of both, which is what makes the three clips
    // distinguishable at the tip: angles compound down a chain.
    float middleBaseRadians;
    float middleSwingRadians;
    float cycleSeconds;
};

// Idle stands nearly straight and barely breathes; walk is a moderate bend; run is deep and fast.
// The bands they occupy do not overlap:
//
//   idle total joint angle (middle + root) in [0.000, 0.133] rad
//   walk                                   in [0.333, 0.867] rad
//   run                                    in [0.933, 1.733] rad
//
// Disjoint bands are what let a single-frame measurement decide whether the blend parameter
// reached the pose, regardless of where each leaf's playhead happens to be.
constexpr ClipShape IdleShape{"idle", 0.05F, 0.05F, 2.4F};
constexpr ClipShape WalkShape{"walk", 0.45F, 0.20F, 1.2F};
constexpr ClipShape RunShape{"run", 1.00F, 0.30F, 0.6F};

constexpr u16 ClipKeyCount = 9;

// A full cycle of Z rotation for one joint, sampled at ClipKeyCount keys.
//
// Linear interpolation between quaternion keys means SLERP at runtime (see
// AnimationClip3DPayload.hpp), so nine keys over a cycle is plenty: the error against a
// continuous sine is bounded by the chord of a 45-degree arc, well under what the pixel
// comparison resolves.
void appendRotationTrackKeys(float baseRadians, float swingRadians, float cycleSeconds,
                             std::vector<float>& times, std::vector<float>& values)
{
    for (u16 key = 0; key < ClipKeyCount; ++key)
    {
        const float phase = static_cast<float>(key) / static_cast<float>(ClipKeyCount - 1U);
        times.push_back(phase * cycleSeconds);

        const float angle = baseRadians + swingRadians * std::sin(phase * 6.2831853F);
        // Z-axis quaternion: (0, 0, sin(a/2), cos(a/2)).
        values.push_back(0.0F);
        values.push_back(0.0F);
        values.push_back(std::sin(angle * 0.5F));
        values.push_back(std::cos(angle * 0.5F));
    }
}

[[nodiscard]] std::vector<std::byte> buildClipPayload(const ClipShape& shape)
{
    // Two tracks: root and middle. The tip carries no geometry, so animating it would change the
    // numbers without changing the picture -- and this sample's whole claim is that the two agree.
    std::vector<float> rootTimes;
    std::vector<float> rootValues;
    appendRotationTrackKeys(shape.middleBaseRadians / 3.0F, shape.middleSwingRadians / 3.0F,
                            shape.cycleSeconds, rootTimes, rootValues);

    std::vector<float> middleTimes;
    std::vector<float> middleValues;
    appendRotationTrackKeys(shape.middleBaseRadians, shape.middleSwingRadians, shape.cycleSeconds,
                            middleTimes, middleValues);

    // Strictly increasing by (jointIndex, channel), which the writer enforces.
    const std::array<Tina::AssetFormat::AnimationTrackDesc, 2> tracks{
        Tina::AssetFormat::AnimationTrackDesc{
            .jointIndex = RootJoint,
            .channel = Tina::AssetFormat::AnimationChannel::Rotation,
            .interpolation = Tina::AssetFormat::AnimationInterpolation::Linear,
            .times = rootTimes,
            .values = rootValues,
        },
        Tina::AssetFormat::AnimationTrackDesc{
            .jointIndex = MiddleJoint,
            .channel = Tina::AssetFormat::AnimationChannel::Rotation,
            .interpolation = Tina::AssetFormat::AnimationInterpolation::Linear,
            .times = middleTimes,
            .values = middleValues,
        },
    };

    // durationSeconds must equal the maximum last-key time bit-exactly, so it is the same
    // expression that produced the last key rather than the nominal cycleSeconds.
    const float duration = middleTimes.back();
    auto payload = Tina::AssetFormat::writeAnimationClip3DPayloadBytes(
        Tina::AssetFormat::AnimationClip3DPayloadDesc{
            .playbackMode = Tina::AssetFormat::AnimationClip3DPlaybackMode::Loop,
            .jointCount = JointCount,
            .durationSeconds = duration,
            .tracks = tracks,
        });
    if (!payload)
    {
        return {};
    }
    return std::move(*payload);
}

// The speed parameter's two extremes, which every piece of evidence is measured between.
// 0 is pure idle, 1 is pure run: the Blend1D thresholds below put walk at the midpoint.
constexpr float SpeedParameterIdle = 0.0F;
constexpr float SpeedParameterRun = 1.0F;

// How long the graph holds each locomotion state before requesting the next transition. Longer
// than the transition duration, so a state reaches a settled pose rather than being measured
// perpetually mid-blend.
constexpr float StateHoldSeconds = 0.6F;
constexpr double TransitionSeconds = 0.35;

// The evidence phase pins the graph out of band, so it must not start until the schedule has
// finished proving the state machine. At 1/60s per frame StateHoldSeconds is 36 frames, so slot 1
// (walk) fires around frame 36 and slot 2 (run) around frame 72; the float accumulation of 1/60
// drifts by a frame either way, which is why this is 96 and not 73. By then all three states have
// been entered and two crossfades have run.
constexpr u64 EvidenceFirstFrame = 96;

// Frames to hold a pinned parameter before capturing. One is the true minimum -- the pin is applied
// after this frame's tick, so the pose it selects first reaches the palette on the next frame --
// and eight leaves room for a capture path that presents a frame behind.
constexpr u64 EvidenceSettleFrames = 8;

// Frames the whole sequence needs: EvidenceFirstFrame, then per capture one frame to arm, one to
// collect, and EvidenceSettleFrames to settle, three times over. Below this the run cannot produce
// its evidence, so --frames is rejected rather than exiting 0 having proven nothing.
constexpr u64 MinimumFrameCount = EvidenceFirstFrame + 3U * (EvidenceSettleFrames + 2U) + 2U;

// Pose divergence between the idle and run extremes, in milliradians of total joint rotation
// (`poseRotationMilliradians` sums |angle| over every joint, so it is the sum of the middle and
// root angles; the tip carries no track).
//
// Derived from the authored clip bands, not from a measured run:
//
//   idle: middle in [0.00, 0.10], root in [0.000, 0.033]  -> total in [0.000, 0.133] rad
//   run:  middle in [0.70, 1.30], root in [0.233, 0.433]  -> total in [0.933, 1.733] rad
//
// The worst case is idle at its high end against run at its low end, so a correct blend cannot
// report less than 933 - 133 = 800 mrad whatever phase the captures land on. The threshold is half
// of that guaranteed floor: it leaves a 2x margin for a correct run while still failing the defects
// it exists to catch -- a pin that never reached the parameter, or a graph frozen on one state,
// both of which report 0.
constexpr u32 EvidenceMinimumPoseDivergenceMilliradians = 400;

// Two captures pinned to opposite parameter extremes. A frozen picture makes them identical; a
// blend that reaches the GPU cannot.
//
// The floor is argued from geometry, not from an observed number. At 1280x720 with a 55-degree
// vertical FOV from 4.6m, one metre at the chain's depth spans roughly 720 / (2 * 4.6 * tan(27.5))
// ~= 150 pixels. The middle joint swings at least 0.7 rad between the two extremes, sweeping the
// outer bone's 1m length x 0.5m quad width through an area far larger than 64 pixels. 64 is
// deliberately two orders of magnitude below that: it has to survive a resized window or a host
// that renders at a lower backbuffer scale, while still failing the defect it exists to catch --
// two identical pictures, which score exactly 0.
constexpr u32 EvidenceMinimumPixelDelta = 64;

// The chain lies in the XY plane around the origin; the camera looks down -Z at it head on so a
// joint angle change is a silhouette change rather than foreshortening.
constexpr float CameraDistanceMeters = 4.6F;
constexpr float CameraHeightMeters = 0.6F;

struct SampleOptions final {
    u64 targetFrameCount = DefaultFrameCount;
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 renderExtractions = 0;
    u64 applicationShutdowns = 0;
    bool skinnedMeshRetired = false;
    bool materialRetired = false;
    bool engineHostDestroyed = false;
    bool evidenceCollected = false;

    // Graph structure, read back from AnimationGraph3D::stats rather than from what was authored:
    // a state the graph refused to add would otherwise go unnoticed until a transition failed.
    u16 stateCount = 0;
    u16 transitionCount = 0;
    u16 layerCount = 0;
    u64 graphAdvances = 0;
    u64 transitionsStarted = 0;
    u64 transitionsRefused = 0;
    u64 evaluationFailures = 0;
    bool observedTransitionInFlight = false;

    // How many distinct states the run actually entered. Authoring three and visiting one is the
    // failure a stats-only check would miss.
    u32 statesVisited = 0;

    u32 poseDivergenceMilliradians = 0;
    u32 pixelDeltaIdleToRun = 0;
    std::optional<Tina::Core::Error> evidenceError{};
};

[[nodiscard]] Tina::Core::Result<SampleOptions> parseOptions(int argumentCount, char** arguments)
{
    SampleOptions options;
    bool hasFrames = false;
    Tina::Core::ArgScanner scanner(argumentCount, arguments);
    while (scanner.next())
    {
        if (const auto value = scanner.value("--frames"))
        {
            if (hasFrames || !Tina::Core::parseArgUnsigned(*value, options.targetFrameCount) ||
                options.targetFrameCount < MinimumFrameCount)
            {
                Tina::Core::Error error{
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "--frames must appear once and be at least large enough to collect the "
                    "evidence sequence"};
                error.addContext("parseOptions", "--frames");
                return Tina::Core::failure(std::move(error));
            }
            hasFrames = true;
            continue;
        }
        if (scanner.failed())
        {
            Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                    "Command-line option is missing its value"};
            error.addContext("parseOptions", scanner.failedOption());
            return Tina::Core::failure(std::move(error));
        }
        Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                "Unsupported command-line argument"};
        error.addContext("parseOptions", scanner.token());
        return Tina::Core::failure(std::move(error));
    }
    return options;
}

void writeCounters(Tina::Core::JsonWriter& writer, const LifecycleCounters& counters)
{
    writer.member("frames", counters.frameUpdates);
    writer.member("renderExtractions", counters.renderExtractions);
    writer.member("skinnedMeshRetired", counters.skinnedMeshRetired);
    writer.member("materialRetired", counters.materialRetired);
    writer.member("stateCount", counters.stateCount);
    writer.member("transitionCount", counters.transitionCount);
    writer.member("layerCount", counters.layerCount);
    writer.member("graphAdvances", counters.graphAdvances);
    writer.member("transitionsStarted", counters.transitionsStarted);
    writer.member("transitionsRefused", counters.transitionsRefused);
    writer.member("evaluationFailures", counters.evaluationFailures);
    writer.member("observedTransitionInFlight", counters.observedTransitionInFlight);
    writer.member("statesVisited", counters.statesVisited);
    writer.member("evidenceCollected", counters.evidenceCollected);
    writer.member("poseDivergenceMilliradians", counters.poseDivergenceMilliradians);
    writer.member("pixelDeltaIdleToRun", counters.pixelDeltaIdleToRun);
    if (counters.evidenceError.has_value())
    {
        writer.member("evidenceError", counters.evidenceError->message);
    }
    else
    {
        writer.member("evidenceError", "");
    }
    writer.member("applicationShutdowns", counters.applicationShutdowns);
    writer.member("engineHostDestroyed", counters.engineHostDestroyed);
}

// A capture failure on a host without readback is not this sample's defect, so the pixel and
// pose criteria both surrender to a Render-domain error rather than failing the run. The numeric
// half of the evidence still has to hold.
[[nodiscard]] bool evidenceExcused(const LifecycleCounters& counters) noexcept
{
    return counters.evidenceError.has_value() &&
           counters.evidenceError->code.domain == Tina::Core::ErrorDomain::Render;
}

[[nodiscard]] bool blendProducedDistinctPoses(const LifecycleCounters& counters) noexcept
{
    if (!counters.evidenceCollected)
    {
        return evidenceExcused(counters);
    }
    return counters.poseDivergenceMilliradians >= EvidenceMinimumPoseDivergenceMilliradians;
}

[[nodiscard]] bool silhouetteChangedWithSpeed(const LifecycleCounters& counters) noexcept
{
    if (!counters.evidenceCollected)
    {
        return evidenceExcused(counters);
    }
    return counters.pixelDeltaIdleToRun >= EvidenceMinimumPixelDelta;
}

// Three states authored, three visited, and at least two crossfades started with none refused
// and nothing failing to evaluate. `transitionsRefused` is not merely a tuning signal here: every
// transition this sample requests is authored and interruptible, so a refusal means the state
// machine dropped a request it accepted the shape of.
[[nodiscard]] bool graphDroveEveryState(const LifecycleCounters& counters) noexcept
{
    return counters.stateCount == 3U && counters.transitionCount >= 2U &&
           counters.layerCount == 1U && counters.statesVisited == 3U &&
           counters.transitionsStarted >= 2U && counters.transitionsRefused == 0U &&
           counters.evaluationFailures == 0U && counters.observedTransitionInFlight &&
           counters.graphAdvances == counters.frameUpdates;
}

class KindedFrameResource final {
  public:
    KindedFrameResource() noexcept = default;
    KindedFrameResource(const KindedFrameResource&) = delete;
    KindedFrameResource& operator=(const KindedFrameResource&) = delete;

    ~KindedFrameResource() noexcept
    {
        if (borrowCount_ != 0)
        {
            std::terminate();
        }
    }

    [[nodiscard]] Tina::Core::Result<Tina::Render::FrameResourceRef>
    intern(Tina::Render::FrameResourceSink& sink, Tina::Render::FrameResourceKind kind,
           u64 deviceBindingKey) const noexcept
    {
        ++borrowCount_;
        Tina::Render::FramePin pin{
            Tina::Render::FramePinKind::Custom,
            deviceBindingKey,
            const_cast<KindedFrameResource*>(this),
            &KindedFrameResource::releaseBorrow,
        };
        return sink.intern(
            Tina::Render::FrameResourceDescriptor{
                .kind = kind,
                .deviceBindingKey = deviceBindingKey,
            },
            std::move(pin));
    }

  private:
    static void releaseBorrow(void* userData) noexcept
    {
        auto* owner = static_cast<KindedFrameResource*>(userData);
        if (owner == nullptr || owner->borrowCount_ == 0)
        {
            std::terminate();
        }
        --owner->borrowCount_;
    }

    mutable u32 borrowCount_ = 0;
};

// Owns the skeleton, the three clip samplers, the blend tree and the graph. Separate from the
// game state for the same reason 3d_ik_chain's IkChain is: nothing here touches Render, so the
// numeric half of the evidence is exercisable without a device.
//
// Member order is load-bearing. The graph borrows the samplers and the tree, and the tree borrows
// the samplers, so both must be declared before the graph and outlive it. Declaring the graph
// first would leave it holding dangling pointers at destruction.
class LocomotionRig final {
  public:
    [[nodiscard]] static Tina::Core::Result<std::unique_ptr<LocomotionRig>> Create()
    {
        auto meshBytes = buildSkinnedMeshPayload();
        if (meshBytes.empty())
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "failed to build the skinned mesh payload");
        }

        auto meshView = Tina::AssetFormat::parseSkinnedMeshPayload(meshBytes);
        if (!meshView)
        {
            return Tina::Core::failure(
                std::move(meshView.error()).withContext("LocomotionRig::Create", "parseMesh"));
        }

        auto skeleton = Tina::Animation3D::Skeleton3D::Create(*meshView);
        if (!skeleton)
        {
            return Tina::Core::failure(
                std::move(skeleton.error()).withContext("LocomotionRig::Create", "Skeleton3D"));
        }

        auto pose = Tina::Animation3D::Pose3D::Create(skeleton->jointCount());
        if (!pose)
        {
            return Tina::Core::failure(
                std::move(pose.error()).withContext("LocomotionRig::Create", "Pose3D"));
        }

        // Heap-allocated because the graph and tree hold pointers into this object's members;
        // a moved-from stack value would leave them dangling.
        auto rig = std::unique_ptr<LocomotionRig>(
            new LocomotionRig(std::move(*skeleton), std::move(*pose), std::move(meshBytes)));
        if (auto status = rig->buildClips(); !status)
        {
            return Tina::Core::failure(std::move(status.error()));
        }
        if (auto status = rig->buildBlendTree(); !status)
        {
            return Tina::Core::failure(std::move(status.error()));
        }
        if (auto status = rig->buildGraph(); !status)
        {
            return Tina::Core::failure(std::move(status.error()));
        }
        if (auto status = rig->buildCookedMesh(); !status)
        {
            return Tina::Core::failure(std::move(status.error()));
        }
        return rig;
    }

    LocomotionRig(const LocomotionRig&) = delete;
    LocomotionRig& operator=(const LocomotionRig&) = delete;

    [[nodiscard]] std::span<const float> palette() const noexcept { return palette_; }
    [[nodiscard]] const Tina::Asset::CookedAssetFile* cookedMesh() const noexcept
    {
        return cookedMesh_ ? &*cookedMesh_ : nullptr;
    }
    [[nodiscard]] Tina::Animation3D::AnimationGraph3DStats stats() const noexcept
    {
        return graph_ ? graph_->stats() : Tina::Animation3D::AnimationGraph3DStats{};
    }
    [[nodiscard]] u32 statesVisited() const noexcept { return statesVisited_; }

    // Advance then evaluate, in that order and both every frame. Evaluating first renders a frame
    // behind; skipping the advance freezes the clips while the transition still progresses.
    [[nodiscard]] Tina::Core::Status tick(Tina::Core::Duration delta)
    {
        if (auto status = graph_->advance(delta); !status)
        {
            return status;
        }
        if (auto status = graph_->evaluate(pose_); !status)
        {
            return status;
        }
        return graph_->writeSkinningMatrices(palette_);
    }

    // Drives the state machine on a fixed schedule: idle -> walk -> run -> idle. Requested rather
    // than cut, so every change is a crossfade through the authored transition.
    [[nodiscard]] Tina::Core::Status advanceStateSchedule(float elapsedSeconds)
    {
        const auto slot = static_cast<u32>(elapsedSeconds / StateHoldSeconds);
        if (slot == scheduleSlot_)
        {
            return Tina::Core::success();
        }
        scheduleSlot_ = slot;

        const u32 next = slot % 3U;

        // A self-transition cannot be authored, so requestTransition finds no edge and fails
        // InvalidTransition rather than treating "already there" as a no-op. The schedule can ask
        // for the current state after the evidence phase cut to it out of band, so the check is
        // against the graph's own answer, never against a slot number this class remembers.
        auto current = graph_->currentState(layer_);
        if (!current)
        {
            return Tina::Core::failure(
                std::move(current.error()).withContext("advanceStateSchedule", "currentState"));
        }
        if (*current == states_[next])
        {
            markVisited(next);
            return Tina::Core::success();
        }

        if (auto status = graph_->requestTransition(layer_, states_[next]); !status)
        {
            return status;
        }
        markVisited(next);
        return Tina::Core::success();
    }

    // Cuts to the blend-tree state and pins its parameter, for the evidence captures. A cut rather
    // than a crossfade because a pinned measurement must not be taken mid-transition.
    //
    // Deliberately the tree state and not the idle/run clip states: pinning those would compare two
    // clips playing directly and prove nothing about Blend1D. Driving the tree to each end of its
    // axis makes the same two poses come out of the blender, so the measurement is of the blend.
    [[nodiscard]] Tina::Core::Status pinToSpeed(float speed)
    {
        if (auto status = graph_->setState(layer_, states_[1]); !status)
        {
            return status;
        }
        markVisited(1U);
        return blendTree_->setParameter(SpeedParameter, speed);
    }

    // Total absolute joint rotation angle of the current pose, in milliradians. A scalar summary
    // of "where the limbs are": comparing this between the two extremes asks whether the blend
    // moved the skeleton, without depending on which joint moved.
    [[nodiscard]] u32 poseRotationMilliradians() const noexcept
    {
        float total = 0.0F;
        for (u16 joint = 0; joint < pose_.jointCount(); ++joint)
        {
            const Tina::Math::Quaternion& rotation = pose_.at(joint).rotation;
            // Angle of a unit quaternion. |w| may drift a hair past 1 through repeated slerp, and
            // acos of that is NaN, so it is clamped rather than trusted.
            const float w = std::clamp(std::abs(rotation.w), 0.0F, 1.0F);
            total += 2.0F * std::acos(w);
        }
        return static_cast<u32>(std::lround(total * 1000.0F));
    }

    [[nodiscard]] Tina::Core::Status setSpeedParameter(float speed)
    {
        return blendTree_->setParameter(SpeedParameter, speed);
    }

  private:
    static constexpr Tina::Animation3D::BlendParameterIndex SpeedParameter = 0;
    static constexpr u16 IdleClip = 0;
    static constexpr u16 WalkClip = 1;
    static constexpr u16 RunClip = 2;

    // Counted here rather than from stats, because stats reports how many states exist and how
    // many transitions ran, never *which* states the run actually entered.
    void markVisited(u32 index) noexcept
    {
        if ((visitedMask_ & (1U << index)) == 0U)
        {
            visitedMask_ |= 1U << index;
            ++statesVisited_;
        }
    }

    LocomotionRig(Tina::Animation3D::Skeleton3D skeleton, Tina::Animation3D::Pose3D pose,
                  std::vector<std::byte> meshBytes) noexcept
        : skeleton_(std::move(skeleton)), pose_(std::move(pose)), meshBytes_(std::move(meshBytes))
    {
        palette_.assign(static_cast<usize>(skeleton_.jointCount()) * 16U, 0.0F);
    }

    [[nodiscard]] Tina::Core::Status buildClips()
    {
        const std::array<ClipShape, 3> shapes{IdleShape, WalkShape, RunShape};
        for (const ClipShape& shape : shapes)
        {
            auto bytes = buildClipPayload(shape);
            if (bytes.empty())
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "failed to build an animation clip payload");
            }
            clipBytes_.push_back(std::move(bytes));
        }

        // Parsed in a second pass: clipBytes_ reallocates as it grows, and a view taken during the
        // first pass would alias a freed buffer. ClipSampler3D copies the wire data, but the view
        // it is handed must still be valid at that moment.
        for (const std::vector<std::byte>& bytes : clipBytes_)
        {
            auto view = Tina::AssetFormat::parseAnimationClip3DPayload(bytes);
            if (!view)
            {
                return Tina::Core::failure(
                    std::move(view.error()).withContext("buildClips", "parseClip"));
            }
            auto sampler = Tina::Animation3D::ClipSampler3D::Create(*view, skeleton_.jointCount());
            if (!sampler)
            {
                return Tina::Core::failure(
                    std::move(sampler.error()).withContext("buildClips", "ClipSampler3D"));
            }
            samplers_.push_back(std::move(*sampler));
        }

        samplerPointers_.reserve(samplers_.size());
        for (const Tina::Animation3D::ClipSampler3D& sampler : samplers_)
        {
            samplerPointers_.push_back(&sampler);
        }
        return Tina::Core::success();
    }

    // idle / walk / run on one Blend1D axis. Blend1D rather than two chained Blend2 nodes on
    // purpose: in a chain, idle keeps contributing at parameter values where it should be silent,
    // which is the defect BlendTree3D's header calls out.
    [[nodiscard]] Tina::Core::Status buildBlendTree()
    {
        const std::array<u16, 3> blendInputs{0, 1, 2};
        // Walk sits at the midpoint, so parameter 0.5 is pure walk and the halves either side
        // interpolate toward idle and run.
        const std::array<float, 3> thresholds{0.0F, 0.5F, 1.0F};
        const std::array<Tina::Animation3D::BlendTreeNodeDesc, 4> nodes{
            Tina::Animation3D::BlendTreeNodeDesc{
                .kind = Tina::Animation3D::BlendTreeNodeKind::Clip,
                .clipIndex = IdleClip,
            },
            Tina::Animation3D::BlendTreeNodeDesc{
                .kind = Tina::Animation3D::BlendTreeNodeKind::Clip,
                .clipIndex = WalkClip,
            },
            Tina::Animation3D::BlendTreeNodeDesc{
                .kind = Tina::Animation3D::BlendTreeNodeKind::Clip,
                .clipIndex = RunClip,
            },
            Tina::Animation3D::BlendTreeNodeDesc{
                .kind = Tina::Animation3D::BlendTreeNodeKind::Blend1D,
                .inputs = blendInputs,
                .thresholds = thresholds,
                .parameter = SpeedParameter,
            },
        };

        auto tree = Tina::Animation3D::BlendTree3D::Create(
            Tina::Animation3D::BlendTree3DDesc{
                .nodes = nodes,
                .rootNode = 3,
                .parameterCount = 1,
            },
            skeleton_, samplerPointers_);
        if (!tree)
        {
            return Tina::Core::failure(
                std::move(tree.error()).withContext("buildBlendTree", "BlendTree3D"));
        }
        blendTree_ = std::make_unique<Tina::Animation3D::BlendTree3D>(std::move(*tree));
        treePointers_.push_back(blendTree_.get());
        return Tina::Core::success();
    }

    // Three states on the base layer: idle and run play their clips directly, walk drives the
    // blend tree. Mixing both source kinds is the point -- a graph that only ever ran clip states
    // would leave BlendTree3D's integration with the state machine unproven.
    [[nodiscard]] Tina::Core::Status buildGraph()
    {
        auto graph = Tina::Animation3D::AnimationGraph3D::Create(skeleton_, samplerPointers_,
                                                                treePointers_);
        if (!graph)
        {
            return Tina::Core::failure(
                std::move(graph.error()).withContext("buildGraph", "AnimationGraph3D"));
        }
        graph_ = std::make_unique<Tina::Animation3D::AnimationGraph3D>(std::move(*graph));
        layer_ = graph_->baseLayer();

        const std::array<Tina::Animation3D::StateDesc, 3> stateDescs{
            Tina::Animation3D::StateDesc{
                .kind = Tina::Animation3D::StateSourceKind::Clip,
                .clipIndex = IdleClip,
            },
            Tina::Animation3D::StateDesc{
                .kind = Tina::Animation3D::StateSourceKind::BlendTree,
                .blendTreeIndex = 0,
                // Locomotion resumes mid-stride rather than snapping to the clip start, which is
                // what makes a walk->run transition read as a change of gait.
                .restartOnEnter = false,
            },
            Tina::Animation3D::StateDesc{
                .kind = Tina::Animation3D::StateSourceKind::Clip,
                .clipIndex = RunClip,
                .restartOnEnter = false,
            },
        };

        for (usize index = 0; index < stateDescs.size(); ++index)
        {
            auto state = graph_->addState(layer_, stateDescs[index]);
            if (!state)
            {
                return Tina::Core::failure(
                    std::move(state.error()).withContext("buildGraph", "addState"));
            }
            states_[index] = *state;
        }

        // A transition per adjacent pair in both directions, plus the wrap. requestTransition
        // fails InvalidTransition when no authored edge connects the pair, so the schedule's
        // every hop needs one.
        const std::array<std::array<usize, 2>, 6> edges{
            {{0, 1}, {1, 2}, {2, 0}, {1, 0}, {2, 1}, {0, 2}}};
        for (const std::array<usize, 2>& edge : edges)
        {
            if (auto status = graph_->addTransition(
                    layer_, Tina::Animation3D::TransitionDesc{
                                .from = states_[edge[0]],
                                .to = states_[edge[1]],
                                .duration = Tina::Core::Duration{TransitionSeconds},
                                .easing = Tina::Gameplay::Easing::QuadraticInOut,
                            });
                !status)
            {
                return Tina::Core::failure(
                    std::move(status.error()).withContext("buildGraph", "addTransition"));
            }
        }

        if (auto status = graph_->setState(layer_, states_[0]); !status)
        {
            return Tina::Core::failure(
                std::move(status.error()).withContext("buildGraph", "setState"));
        }
        visitedMask_ = 1U;
        statesVisited_ = 1U;
        return blendTree_->setParameter(SpeedParameter, 0.5F);
    }

    // Wraps the mesh payload in a cooked-asset container. No catalog and no file on disk; assetId
    // is arbitrary because the device never resolves it, but a zero ID is rejected outright.
    [[nodiscard]] Tina::Core::Status buildCookedMesh()
    {
        Tina::Core::AssetId::Bytes idBytes{};
        idBytes[0] = std::byte{2};
        // assetTypeVersion is set explicitly: CookedAssetWriteDesc defaults it to 1 while the
        // loader requires SkinnedMeshWire::SchemaVersion, so the default cooks an asset nothing
        // will load.
        auto cookedBytes =
            Tina::AssetFormat::writeCookedAssetBytes(Tina::AssetFormat::CookedAssetWriteDesc{
                .assetKind = Tina::AssetFormat::AssetKind::SkinnedMesh,
                .assetTypeVersion = Tina::AssetFormat::SkinnedMeshWire::SchemaVersion,
                .assetId = *Tina::Core::AssetId::fromBytes(idBytes),
                .payload = meshBytes_,
            });
        if (!cookedBytes)
        {
            return Tina::Core::failure(std::move(cookedBytes.error())
                                           .withContext("buildCookedMesh", "writeCookedAssetBytes"));
        }

        std::pmr::vector<std::byte> owned{std::pmr::get_default_resource()};
        owned.assign(cookedBytes->begin(), cookedBytes->end());
        auto cooked = Tina::Asset::makeCookedAssetFileFromBytes(
            std::move(owned), Tina::Asset::CookedAssetFileLoadConfig{
                                  .memoryResource = std::pmr::get_default_resource()});
        if (!cooked)
        {
            return Tina::Core::failure(
                std::move(cooked.error()).withContext("buildCookedMesh", "makeCookedAssetFile"));
        }
        cookedMesh_.emplace(std::move(*cooked));
        return Tina::Core::success();
    }

    Tina::Animation3D::Skeleton3D skeleton_;
    Tina::Animation3D::Pose3D pose_;
    std::vector<std::byte> meshBytes_;
    std::vector<std::vector<std::byte>> clipBytes_{};
    // Borrowed by both the tree and the graph, so declared before them.
    std::vector<Tina::Animation3D::ClipSampler3D> samplers_{};
    std::vector<const Tina::Animation3D::ClipSampler3D*> samplerPointers_{};
    std::unique_ptr<Tina::Animation3D::BlendTree3D> blendTree_{};
    std::vector<Tina::Animation3D::BlendTree3D*> treePointers_{};
    std::unique_ptr<Tina::Animation3D::AnimationGraph3D> graph_{};
    Tina::Animation3D::LayerId layer_{};
    std::array<Tina::Animation3D::StateId, 3> states_{};
    std::optional<Tina::Asset::CookedAssetFile> cookedMesh_{};
    std::vector<float> palette_{};
    u32 scheduleSlot_ = 0;
    u32 visitedMask_ = 0;
    u32 statesVisited_ = 0;
};

class AnimationGraphState final : public Tina::IGameState {
  public:
    AnimationGraphState(SampleOptions options, LifecycleCounters& counters) noexcept
        : options_(options), counters_(&counters)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        Tina::Render::IRenderDevice& device = context.renderDevice();

        auto rig = LocomotionRig::Create();
        if (!rig)
        {
            return Tina::Core::failure(std::move(rig.error()));
        }
        rig_ = std::move(*rig);

        auto mesh = Tina::Asset::uploadSkinnedMeshFromCooked(device, *rig_->cookedMesh());
        if (!mesh)
        {
            return Tina::Core::failure(
                std::move(mesh.error()).withContext("onEnter", "uploadSkinnedMeshFromCooked"));
        }
        skinnedMesh_ = *mesh;
        if (auto status = device.setMesh3DBinding(SkinnedMeshBindingKey, skinnedMesh_); !status)
        {
            return status;
        }
        meshBound_ = true;

        if (auto status = device.setMesh3DMaterialBinding(
                MaterialBindingKey, Tina::Render::Mesh3DMaterialBindingDesc{
                                        .metallicFactor = 0.0F,
                                        .roughnessFactor = 0.62F,
                                    });
            !status)
        {
            return status;
        }
        materialBound_ = true;
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext& context) noexcept override
    {
        Tina::Render::IRenderDevice& device = context.renderDevice();
        if (materialBound_)
        {
            if (device.clearMesh3DMaterialBinding(MaterialBindingKey))
            {
                counters_->materialRetired = true;
            }
            materialBound_ = false;
        }
        if (meshBound_)
        {
            static_cast<void>(device.setMesh3DBinding(SkinnedMeshBindingKey,
                                                        Tina::Render::GpuMeshId{}));
            meshBound_ = false;
        }
        if (skinnedMesh_)
        {
            Tina::Render::FramePin completion{};
            if (device.retireGpuMesh(skinnedMesh_, completion))
            {
                counters_->skinnedMeshRetired = true;
            }
            skinnedMesh_ = Tina::Render::GpuMeshId{};
        }
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_->frameUpdates;
        Tina::Render::IRenderDevice* device = context.renderDevice();
        if (device == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "render device disappeared mid-run");
        }

        elapsedSeconds_ += FixedTimestepSeconds;
        if (!pinnedSpeed_.has_value())
        {
            if (auto status = rig_->advanceStateSchedule(elapsedSeconds_); !status)
            {
                return status;
            }
        }

        // Exactly one graph tick per update: advance first, then evaluate and write the palette.
        if (auto status = rig_->tick(Tina::Core::Duration{FixedTimestepSeconds}); !status)
        {
            return status;
        }
        const auto stats = rig_->stats();
        counters_->stateCount = stats.stateCount;
        counters_->transitionCount = stats.transitionCount;
        counters_->layerCount = stats.layerCount;
        counters_->graphAdvances = stats.advanceCount;
        counters_->transitionsStarted = stats.transitionsStarted;
        counters_->transitionsRefused = stats.transitionsRefused;
        counters_->evaluationFailures = stats.evaluationFailures;
        counters_->observedTransitionInFlight =
            counters_->observedTransitionInFlight || stats.transitionInFlight;
        counters_->statesVisited = rig_->statesVisited();

        advanceEvidence(*device);
        if (counters_->frameUpdates >= options_.targetFrameCount)
        {
            context.requestExitAfterFrame();
        }
        return Tina::Core::success();
    }

    Tina::Core::Status extractRenderScene(Tina::RenderSceneExtractionContext& context) const override
    {
        ++counters_->renderExtractions;
        auto& writer = context.renderSceneWriter();
        if (auto status = writer.setPerspectiveCamera(Tina::Render::RenderPerspectiveCameraInput{
                .stableCameraKey = 1,
                .worldPose = {.positionX = 0.9F,
                              .positionY = CameraHeightMeters,
                              .positionZ = CameraDistanceMeters},
                .verticalFovDegrees = 55.0F,
                .nearPlaneMeters = 0.1F,
                .farPlaneMeters = 100.0F,
            });
            !status)
        {
            return status;
        }

        const std::array<Tina::Render::Mesh3DDirectionalLight, 1> lights{{
            {.directionTowardLightX = 0.2F,
             .directionTowardLightY = 0.7F,
             .directionTowardLightZ = 0.68F,
             .colorR = 1.6F,
             .colorG = 1.6F,
             .colorB = 1.6F},
        }};
        if (auto status = writer.setMesh3DLighting(
                Tina::Render::Mesh3DLightingDesc{.directionalLights = lights, .ambientScale = 0.25F});
            !status)
        {
            return status;
        }

        auto& sink = context.frameResourceSink();
        auto mesh = meshResource_.intern(sink, Tina::Render::FrameResourceKind::SkinnedMesh3DGeometry,
                                         SkinnedMeshBindingKey);
        if (!mesh)
        {
            return Tina::Core::failure(std::move(mesh.error()));
        }
        auto material = materialResource_.intern(sink, Tina::Render::FrameResourceKind::Mesh3DMaterial,
                                                 MaterialBindingKey);
        if (!material)
        {
            return Tina::Core::failure(std::move(material.error()));
        }

        return writer.addSkinnedMesh3D(Tina::Render::RenderSkinnedMesh3DInput{
            .mesh = *mesh,
            .material = *material,
            .submeshIndex = 0,
            .stableEntityKey = 1,
            .localBounds = {.radius = ChainReachMeters + QuadHalfHeightMeters},
            .baseColorFactor = {.red = 0.18F, .green = 0.62F, .blue = 0.95F, .alpha = 1.0F},
            .paletteColumnMajorJointMatrices = rig_->palette(),
            .alphaMode = Tina::Render::Mesh3DAlphaMode::Opaque,
            .doubleSided = true,
            .visible = true,
        });
    }

  private:
    static constexpr float FixedTimestepSeconds = 1.0F / 60.0F;

    enum class EvidenceStage : u8 {
        Idle,
        WarmupAwait,
        SettleIdle,
        AwaitIdle,
        SettleRun,
        AwaitRun,
        Done,
    };

    void abandonEvidence(Tina::Core::Error error) noexcept
    {
        if (!counters_->evidenceError.has_value())
        {
            counters_->evidenceError = std::move(error);
        }
        stage_ = EvidenceStage::Done;
    }

    void armEvidence(Tina::Render::IRenderDevice& device, EvidenceStage next) noexcept
    {
        if (auto armed = device.requestPrimaryFrameCaptureOnNextPresent(); !armed)
        {
            abandonEvidence(std::move(armed.error()));
            return;
        }
        stage_ = next;
    }

    // Runs after tick() and before this frame's extract, so the pose read when a capture is armed
    // is the pose that capture will contain. Reading it a frame later would compare a picture
    // against a pose the clips had already moved past.
    void advanceEvidence(Tina::Render::IRenderDevice& device) noexcept
    {
        switch (stage_)
        {
        case EvidenceStage::Idle:
            if (counters_->frameUpdates >= EvidenceFirstFrame)
            {
                // The first capture of a run comes back blank on this host, so one is armed and
                // discarded before any measurement.
                armEvidence(device, EvidenceStage::WarmupAwait);
            }
            return;

        case EvidenceStage::WarmupAwait:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandonEvidence(std::move(captured.error()));
                return;
            }
            if (auto status = rig_->pinToSpeed(SpeedParameterIdle); !status)
            {
                abandonEvidence(std::move(status.error()));
                return;
            }
            pinnedSpeed_ = SpeedParameterIdle;
            settleFramesRemaining_ = EvidenceSettleFrames;
            stage_ = EvidenceStage::SettleIdle;
            return;
        }

        case EvidenceStage::SettleIdle:
            if (settleFramesRemaining_ != 0)
            {
                --settleFramesRemaining_;
                return;
            }
            idlePoseRotationMilliradians_ = rig_->poseRotationMilliradians();
            armEvidence(device, EvidenceStage::AwaitIdle);
            return;

        case EvidenceStage::AwaitIdle:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandonEvidence(std::move(captured.error()));
                return;
            }
            idleCapture_ = std::move(*captured);
            if (auto status = rig_->pinToSpeed(SpeedParameterRun); !status)
            {
                abandonEvidence(std::move(status.error()));
                return;
            }
            pinnedSpeed_ = SpeedParameterRun;
            settleFramesRemaining_ = EvidenceSettleFrames;
            stage_ = EvidenceStage::SettleRun;
            return;
        }

        case EvidenceStage::SettleRun:
            if (settleFramesRemaining_ != 0)
            {
                --settleFramesRemaining_;
                return;
            }
            runPoseRotationMilliradians_ = rig_->poseRotationMilliradians();
            armEvidence(device, EvidenceStage::AwaitRun);
            return;

        case EvidenceStage::AwaitRun:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandonEvidence(std::move(captured.error()));
                return;
            }
            measure(idleCapture_, *captured);
            // Unpinned so the remaining frames resume the schedule; a run that ends pinned would
            // leave the last visible seconds frozen on one state.
            pinnedSpeed_.reset();
            stage_ = EvidenceStage::Done;
            return;
        }

        case EvidenceStage::Done:
            return;
        }
    }

    void measure(const Tina::Render::Rgba8FrameCapture& idleFrame,
                 const Tina::Render::Rgba8FrameCapture& runFrame) noexcept
    {
        counters_->poseDivergenceMilliradians =
            std::max(idlePoseRotationMilliradians_, runPoseRotationMilliradians_) -
            std::min(idlePoseRotationMilliradians_, runPoseRotationMilliradians_);

        if (idleFrame.empty() || runFrame.empty() || idleFrame.width != runFrame.width ||
            idleFrame.height != runFrame.height)
        {
            counters_->evidenceError = Tina::Core::Error{
                Tina::Core::CoreErrorCode::Internal,
                "the two evidence captures do not describe the same surface"};
            return;
        }

        // Counts pixels that changed rather than summing channel differences: the question is
        // whether the silhouette moved, and a count does not let a few strongly-changed pixels
        // stand in for a moved limb.
        constexpr u32 ChannelThreshold = 24;
        u32 changed = 0;
        const usize pixelCount = static_cast<usize>(idleFrame.width) * idleFrame.height;
        for (usize pixel = 0; pixel < pixelCount; ++pixel)
        {
            const usize offset = pixel * 4U;
            const auto channelDelta = [&](usize channel) noexcept -> u32 {
                const auto a =
                    static_cast<int>(std::to_integer<u8>(idleFrame.rgba8Pixels[offset + channel]));
                const auto b =
                    static_cast<int>(std::to_integer<u8>(runFrame.rgba8Pixels[offset + channel]));
                return static_cast<u32>(std::abs(a - b));
            };
            if (channelDelta(0) >= ChannelThreshold || channelDelta(1) >= ChannelThreshold ||
                channelDelta(2) >= ChannelThreshold)
            {
                ++changed;
            }
        }
        counters_->pixelDeltaIdleToRun = changed;
        counters_->evidenceCollected = true;
    }

    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    // A unique_ptr rather than an optional: the graph and the blend tree hold pointers into the
    // rig's own members, so it must never be moved after construction.
    std::unique_ptr<LocomotionRig> rig_{};
    Tina::Render::GpuMeshId skinnedMesh_{};
    bool meshBound_ = false;
    bool materialBound_ = false;
    float elapsedSeconds_ = 0.0F;
    // Set means "hold this blend parameter and this state instead of running the schedule".
    std::optional<float> pinnedSpeed_{};
    u64 settleFramesRemaining_ = 0;
    EvidenceStage stage_ = EvidenceStage::Idle;
    Tina::Render::Rgba8FrameCapture idleCapture_{};
    u32 idlePoseRotationMilliradians_ = 0;
    u32 runPoseRotationMilliradians_ = 0;
    mutable KindedFrameResource meshResource_{};
    mutable KindedFrameResource materialResource_{};
};

class AnimationGraphApplication final : public Tina::IGameApplication {
  public:
    AnimationGraphApplication(SampleOptions options, LifecycleCounters& counters) noexcept
        : options_(options), counters_(&counters)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>>
    createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>(
            std::make_unique<AnimationGraphState>(options_, *counters_));
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
        ++counters_->applicationShutdowns;
    }

  private:
    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
};

[[nodiscard]] Tina::EngineConfig createEngineConfig()
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina vNext 3D Animation Graph";
    config.primaryWindow.title = "Tina vNext - AnimationGraph3D locomotion";
    config.primaryWindow.initialLogicalExtent = {1280, 720};
    config.primaryWindow.initiallyVisible = true;
    return config;
}

void writeError(const Tina::Core::Error& error)
{
    Tina::Core::JsonWriter writer(std::cerr);
    writer.beginObject();
    writer.member("status", "error");
    writer.member("sample", SampleName);
    writer.member("message", error.message);
    writer.endObject();
    std::cerr << '\n';
}

[[nodiscard]] int runSample(int argumentCount, char** arguments)
{
    auto optionsResult = parseOptions(argumentCount, arguments);
    if (!optionsResult)
    {
        writeError(optionsResult.error());
        return 2;
    }
    const SampleOptions options = *optionsResult;

    auto hostResult = Tina::Desktop::CreateEngine(createEngineConfig());
    if (!hostResult)
    {
        writeError(hostResult.error());
        return 1;
    }

    LifecycleCounters counters;
    AnimationGraphApplication application{options, counters};
    auto runResult = (*hostResult)->run(application);
    hostResult->reset();
    counters.engineHostDestroyed = true;
    if (!runResult)
    {
        writeError(runResult.error());
        return 1;
    }

    const bool lifecycleOk = *runResult == Tina::RunExitReason::GameRequestedExitAfterCurrentFrame &&
                             counters.frameUpdates == options.targetFrameCount &&
                             counters.renderExtractions == options.targetFrameCount &&
                             counters.applicationShutdowns == 1 && counters.skinnedMeshRetired &&
                             counters.materialRetired && counters.engineHostDestroyed;
    const bool evidenceOk = blendProducedDistinctPoses(counters) &&
                            silhouetteChangedWithSpeed(counters);
    if (!lifecycleOk || !graphDroveEveryState(counters) || !evidenceOk)
    {
        Tina::Core::JsonWriter writer(std::cerr);
        writer.beginObject();
        writer.member("status", "error");
        writer.member("sample", SampleName);
        writer.member("message", "lifecycle counters did not match");
        writeCounters(writer, counters);
        writer.endObject();
        std::cerr << '\n';
        return 1;
    }

    Tina::Core::JsonWriter writer(std::cout);
    writer.beginObject();
    writer.member("status", "ok");
    writer.member("sample", SampleName);
    writeCounters(writer, counters);
    writer.endObject();
    std::cout << '\n';
    return 0;
}

} // namespace

int runAnimationGraph3dSample(int argumentCount, char** arguments)
{
    try
    {
        return runSample(argumentCount, arguments);
    }
    catch (const std::exception& error)
    {
        std::cerr << "unhandled exception: " << error.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "unhandled non-standard exception\n";
        return 1;
    }
}
