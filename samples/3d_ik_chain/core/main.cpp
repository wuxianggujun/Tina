// Two-bone IK driving a skinned mesh: the first sample to exercise Tina::Animation3D at all.
//
// Until this sample the whole module -- Skeleton3D, ClipSampler3D, AnimationGraph3D, BlendTree3D,
// IkSolver3D -- was reachable only from tests/animation3d. A unit test can prove solveTwoBoneIk
// puts a tip where it was asked to; it cannot prove the resulting pose survives
// composeSkinningMatrices, the palette upload, the extraction copy and the skinned vertex program
// as a picture. Those are four separate places a correct pose can be lost, and none of them is
// covered by the solver's own tests.
//
// So the evidence here is deliberately two-sided:
//   - numeric, from jointModelPosition: does the solved tip actually reach the goal
//   - pixel, from a frame capture: does the drawn silhouette move when the goal moves
// Either one alone is a false positive waiting to happen. A numeric pass with a frozen picture
// means the palette never reached the GPU; a moving picture with a numeric miss means something
// downstream is animating for an unrelated reason.

#include "Sample.hpp"

#include <tina/animation3d/IkSolver3D.hpp>
#include <tina/animation3d/Skeleton3D.hpp>
#include <tina/asset/AssetGpuMesh.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/SkinnedMeshPayload.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/text/ArgParser.hpp>
#include <tina/core/text/JsonWriter.hpp>
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
#include <utility>
#include <vector>

namespace {

using Tina::Core::u16;
using Tina::Core::u32;
using Tina::Core::u64;
using Tina::Core::u8;
using Tina::Core::usize;

constexpr std::string_view SampleName = "tina_sample_3d_ik_chain";
constexpr u64 DefaultFrameCount = 90;

// Root, middle, tip: the minimum a two-bone solver accepts, and what a leg or an arm is.
constexpr u16 JointCount = 3;
constexpr u16 RootJoint = 0;
constexpr u16 MiddleJoint = 1;
constexpr u16 TipJoint = 2;

// Each joint sits one unit along +X from its parent, so the chain's full reach is 2 and the
// bind pose is a straight line. A straight bind pose is the honest starting point: any bend the
// capture shows has to have come from the solver rather than from the skeleton.
constexpr float BoneLengthMeters = 1.0F;
constexpr float ChainReachMeters = BoneLengthMeters * 2.0F;

constexpr u32 SkinnedMeshBindingKey = 1;
constexpr u32 MaterialBindingKey = 1;

// A quad per bone, each fully weighted to that bone, spanning the bone's length along X and
// half a unit either side in Y. Full weight rather than a blend because this sample is about
// whether a pose reaches the screen, not about weight interpolation quality: with one influence
// per vertex a wrong joint matrix shows as a misplaced quad instead of a subtle smear.
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
        joints[index].name = index == RootJoint    ? "root"
                            : index == MiddleJoint ? "middle"
                                                   : "tip";

        // Inverse of the bind global transform, which for this chain is a pure translation of
        // index*BoneLength along X. Column-major, so the translation lives in elements 12..14.
        // Getting this wrong is the classic skinning failure: an identity inverse bind would
        // double-apply the bind pose and fling the mesh away from the skeleton.
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
    std::vector<u16> indices;
    indices.reserve(IndexCount);

    for (u16 bone = 0; bone < BoneCount; ++bone)
    {
        // Vertices are authored in model space, which for a straight bind pose puts bone `bone`
        // between x=bone and x=bone+1.
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
            // P3, N3 (+Z so the quad faces the camera), T4, UV2.
            const std::array<float, Tina::AssetFormat::SkinnedMeshWire::FloatsPerVertex> vertex{
                corner[0], corner[1], 0.0F, 0.0F, 0.0F, 1.0F,
                1.0F,      0.0F,      0.0F, 1.0F, corner[0] * 0.5F, corner[1] + 0.5F};
            vertices.insert(vertices.end(), vertex.begin(), vertex.end());

            // Bone 0 rides the root joint, bone 1 the middle joint. The tip joint carries no
            // geometry: it is the chain's end effector, which is what the solver aims.
            jointIndices.insert(jointIndices.end(), {bone, 0, 0, 0});
            jointWeights.insert(
                jointWeights.end(),
                {Tina::AssetFormat::SkinnedMeshWire::WeightScale, 0, 0, 0});
        }

        const u16 base = static_cast<u16>(bone * VerticesPerBone);
        indices.insert(indices.end(), {base, static_cast<u16>(base + 1U),
                                       static_cast<u16>(base + 2U), base,
                                       static_cast<u16>(base + 2U), static_cast<u16>(base + 3U)});
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

// The goal sweeps along an arc in the XY plane. Chosen so the chain has to bend rather than
// merely rotate: a target at constant distance from the root would be reachable by rotating the
// root alone, and a rotation-only solution would look animated while proving nothing about the
// middle joint.
constexpr float GoalNearDistanceMeters = 1.0F;
constexpr float GoalFarDistanceMeters = 1.9F;
constexpr float GoalSweepSeconds = 3.0F;

// Pulls the middle joint toward +Y so the chain bends one way rather than picking either of the
// two mirror solutions. Without this the elbow can flip between frames, which is the defect the
// solver's own header calls out as the most recognisable one.
constexpr float PoleTargetY = 4.0F;

constexpr u64 EvidenceFirstFrame = 8;
constexpr u64 EvidenceRequiredFrames = EvidenceFirstFrame + 6;

// The solver promises to reach the goal only when it is within the chain's actual reach. The
// sweep extends to 1.9m while the chain can only reach 2m, so the solver cannot miss by more
// than the solver's own numeric epsilon. 1mm is wide enough for rounding and conservative enough
// that a dramatically wrong composition would blow straight through it.
constexpr u32 EvidenceMaximumReachErrorMillimetres = 1;

// Captures two frames: one early in the sweep (near, small angle) and one later (far, wide).
// If the silhouette is frozen the two frames will match; if the palette is animating the
// difference between a near and a far goal must show on screen.
constexpr u32 EvidenceMinimumPixelDelta = 64;

struct SampleOptions final {
    u64 targetFrameCount = DefaultFrameCount;
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 renderExtractions = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
    bool skinnedMeshRetired = false;
    bool materialRetired = false;
    bool engineHostDestroyed = false;
    bool evidenceCollected = false;
    u32 reachErrorMillimetres = 0;
    u32 pixelDeltaNearToFar = 0;
    std::optional<Tina::Core::Error> evidenceError{};
};

struct GoalSample final {
    Tina::Math::Vec3 position{};
    float distanceMeters = 0.0F;
};

// Where the end effector should be at `seconds`. Distance oscillates between near and far, and
// the direction tilts with it, so both the reach and the bend change over a sweep.
[[nodiscard]] GoalSample goalAt(float seconds) noexcept
{
    const float phase = seconds / GoalSweepSeconds;
    const float wave = 0.5F - 0.5F * std::cos(phase * 6.2831853F);
    const float distance =
        GoalNearDistanceMeters + (GoalFarDistanceMeters - GoalNearDistanceMeters) * wave;
    // Sweep through a modest angle: enough that the silhouette moves, small enough that the
    // chain stays in front of the camera.
    const float angleRadians = -0.5F + wave * 1.0F;
    GoalSample sample{};
    sample.position = Tina::Math::Vec3{distance * std::cos(angleRadians),
                                       distance * std::sin(angleRadians), 0.0F};
    sample.distanceMeters = distance;
    return sample;
}

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
                options.targetFrameCount == 0)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frames must appear once and be greater than zero");
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
    writer.member("evidenceCollected", counters.evidenceCollected);
    writer.member("reachErrorMillimetres", counters.reachErrorMillimetres);
    writer.member("pixelDeltaNearToFar", counters.pixelDeltaNearToFar);
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

[[nodiscard]] bool targetReachedAcrossSweep(const LifecycleCounters& counters) noexcept
{
    if (!counters.evidenceCollected)
    {
        return false;
    }
    if (counters.evidenceError.has_value())
    {
        return counters.evidenceError->code.domain == Tina::Core::ErrorDomain::Render;
    }
    return counters.reachErrorMillimetres <= EvidenceMaximumReachErrorMillimetres;
}

[[nodiscard]] bool silhouetteAnimated(const LifecycleCounters& counters) noexcept
{
    if (!counters.evidenceCollected)
    {
        return false;
    }
    if (counters.evidenceError.has_value())
    {
        return counters.evidenceError->code.domain == Tina::Core::ErrorDomain::Render;
    }
    return counters.pixelDeltaNearToFar >= EvidenceMinimumPixelDelta;
}

class DeviceCapture final {
  public:
    void set(Tina::Render::IRenderDevice* device) noexcept { device_ = device; }
    [[nodiscard]] Tina::Render::IRenderDevice* get() const noexcept { return device_; }

  private:
    Tina::Render::IRenderDevice* device_ = nullptr;
};

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

// Owns the skeleton, the working pose and the skinning palette. Separate from the game state so
// the numeric side of the evidence can be exercised without a device: solve() and tipError() do
// not touch Render at all.
class IkChain final {
  public:
    [[nodiscard]] static Tina::Core::Result<IkChain> Create()
    {
        auto payloadBytes = buildSkinnedMeshPayload();
        if (payloadBytes.empty())
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "failed to build the skinned mesh payload");
        }

        auto payloadView = Tina::AssetFormat::parseSkinnedMeshPayload(payloadBytes);
        if (!payloadView)
        {
            return Tina::Core::failure(
                std::move(payloadView.error()).withContext("IkChain::Create", "parse"));
        }

        auto skeleton = Tina::Animation3D::Skeleton3D::Create(*payloadView);
        if (!skeleton)
        {
            return Tina::Core::failure(
                std::move(skeleton.error()).withContext("IkChain::Create", "Skeleton3D"));
        }

        auto pose = Tina::Animation3D::Pose3D::Create(skeleton->jointCount());
        if (!pose)
        {
            return Tina::Core::failure(
                std::move(pose.error()).withContext("IkChain::Create", "Pose3D"));
        }

        IkChain chain{std::move(*skeleton), std::move(*pose), std::move(payloadBytes)};
        if (auto status = chain.skeleton_.writeBindPose(chain.pose_); !status)
        {
            return Tina::Core::failure(
                std::move(status.error()).withContext("IkChain::Create", "writeBindPose"));
        }
        chain.palette_.assign(static_cast<usize>(chain.skeleton_.jointCount()) * 16U, 0.0F);
        return chain;
    }

    // Re-solves from the bind pose every frame rather than from the previous solution. IK is a
    // function of the goal, not of history: accumulating into last frame's pose would make the
    // result depend on the frame rate and hide a solver that drifts.
    [[nodiscard]] Tina::Core::Status solve(const Tina::Math::Vec3& goal) noexcept
    {
        if (auto status = skeleton_.writeBindPose(pose_); !status)
        {
            return status;
        }

        Tina::Animation3D::TwoBoneIkDesc desc{};
        desc.rootJoint = RootJoint;
        desc.middleJoint = MiddleJoint;
        desc.tipJoint = TipJoint;
        desc.targetPosition = goal;
        desc.poleTargetPosition = Tina::Math::Vec3{goal.x, PoleTargetY, 0.0F};
        desc.usePoleTarget = true;

        if (auto status = Tina::Animation3D::solveTwoBoneIk(skeleton_, desc, pose_); !status)
        {
            return status;
        }
        return skeleton_.composeSkinningMatrices(pose_, palette_);
    }

    // Distance from the solved tip to the goal. The solver only promises to reach a goal inside
    // the chain's reach, so a caller comparing this against a tolerance must clamp the goal
    // first -- see EvidenceMaximumReachErrorMillimetres.
    [[nodiscard]] Tina::Core::Result<float> tipError(const Tina::Math::Vec3& goal) const noexcept
    {
        auto tip = Tina::Animation3D::jointModelPosition(skeleton_, pose_, TipJoint);
        if (!tip)
        {
            return Tina::Core::failure(std::move(tip.error()));
        }
        const float dx = tip->x - goal.x;
        const float dy = tip->y - goal.y;
        const float dz = tip->z - goal.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    [[nodiscard]] std::span<const float> palette() const noexcept { return palette_; }
    [[nodiscard]] const Tina::Asset::CookedAssetFile* cookedMesh() const noexcept
    {
        return cookedMesh_ ? &*cookedMesh_ : nullptr;
    }
    // Wraps the raw payload in a cooked-asset container so uploadSkinnedMeshFromCooked accepts
    // it. No catalog and no file on disk: the bytes were built in memory above. assetId is
    // arbitrary (the device never resolves it), but a zero ID is rejected by writeCookedAssetBytes.
    [[nodiscard]] Tina::Core::Status buildCookedMesh()
    {
        Tina::Core::AssetId::Bytes idBytes{};
        idBytes[0] = std::byte{1};
        // assetTypeVersion is NOT defaulted here on purpose. CookedAssetWriteDesc defaults it to
        // 1 while parseSkinnedMeshFromCooked requires SkinnedMeshWire::SchemaVersion, so relying
        // on the default cooks an asset that every loader refuses.
        auto cookedBytes =
            Tina::AssetFormat::writeCookedAssetBytes(Tina::AssetFormat::CookedAssetWriteDesc{
                .assetKind = Tina::AssetFormat::AssetKind::SkinnedMesh,
                .assetTypeVersion = Tina::AssetFormat::SkinnedMeshWire::SchemaVersion,
                .assetId = *Tina::Core::AssetId::fromBytes(idBytes),
                .payload = payloadBytes_,
            });
        if (!cookedBytes)
        {
            return Tina::Core::failure(
                std::move(cookedBytes.error()).withContext("buildCookedMesh", "writeCookedAssetBytes"));
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

  private:
    IkChain(Tina::Animation3D::Skeleton3D skeleton, Tina::Animation3D::Pose3D pose,
            std::vector<std::byte> payloadBytes) noexcept
        : skeleton_(std::move(skeleton)), pose_(std::move(pose)),
          payloadBytes_(std::move(payloadBytes))
    {
    }

    Tina::Animation3D::Skeleton3D skeleton_;
    Tina::Animation3D::Pose3D pose_;
    // The raw skinned-mesh payload, kept because buildCookedMesh wraps it after construction.
    std::vector<std::byte> payloadBytes_;
    std::optional<Tina::Asset::CookedAssetFile> cookedMesh_{};
    std::vector<float> palette_{};
};

// The two sweep times the pixel evidence is pinned to. Both are phases of goalAt: a quarter
// through the sweep the goal is near and low, three quarters through it is far and high. Pinning
// rather than sampling whatever frame the capture lands on is what makes the delta reproducible.
constexpr float EvidenceNearSeconds = GoalSweepSeconds * 0.0F;
constexpr float EvidenceFarSeconds = GoalSweepSeconds * 0.5F;

constexpr float CameraDistanceMeters = 4.6F;
constexpr float CameraHeightMeters = 0.6F;

class IkChainState final : public Tina::IGameState {
  public:
    IkChainState(SampleOptions options, LifecycleCounters& counters,
                 const DeviceCapture& capture) noexcept
        : options_(options), counters_(&counters), capture_(&capture)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext&) override
    {
        Tina::Render::IRenderDevice* device = capture_->get();
        if (device == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "sample did not capture a render device");
        }

        auto chain = IkChain::Create();
        if (!chain)
        {
            return Tina::Core::failure(std::move(chain.error()));
        }
        chain_.emplace(std::move(*chain));
        if (auto status = chain_->buildCookedMesh(); !status)
        {
            return status;
        }

        auto mesh = Tina::Asset::uploadSkinnedMeshFromCooked(*device, *chain_->cookedMesh());
        if (!mesh)
        {
            return Tina::Core::failure(
                std::move(mesh.error()).withContext("onEnter", "uploadSkinnedMeshFromCooked"));
        }
        skinnedMesh_ = *mesh;
        if (auto status = device->setMesh3DBinding(SkinnedMeshBindingKey, skinnedMesh_); !status)
        {
            return status;
        }
        meshBound_ = true;

        // An untextured material: the chain's colour comes from baseColorFactor on the draw. A
        // texture would add a second thing that could explain a moving picture.
        if (auto status = device->setMesh3DMaterialBinding(
                MaterialBindingKey, Tina::Render::Mesh3DMaterialBindingDesc{
                                        .metallicFactor = 0.0F,
                                        .roughnessFactor = 0.6F,
                                    });
            !status)
        {
            return status;
        }
        materialBound_ = true;
        return solveForSeconds(0.0F);
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        Tina::Render::IRenderDevice* device = capture_->get();
        if (device == nullptr)
        {
            return;
        }
        if (materialBound_)
        {
            if (device->clearMesh3DMaterialBinding(MaterialBindingKey))
            {
                counters_->materialRetired = true;
            }
            materialBound_ = false;
        }
        if (meshBound_)
        {
            static_cast<void>(device->setMesh3DBinding(SkinnedMeshBindingKey,
                                                      Tina::Render::GpuMeshId{}));
            meshBound_ = false;
        }
        if (skinnedMesh_)
        {
            Tina::Render::FramePin completion{};
            if (device->retireGpuMesh(skinnedMesh_, completion))
            {
                counters_->skinnedMeshRetired = true;
            }
            skinnedMesh_ = Tina::Render::GpuMeshId{};
        }
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_->frameUpdates;
        Tina::Render::IRenderDevice* device = capture_->get();
        if (device == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "render device disappeared mid-run");
        }

        // A pinned evidence phase overrides the animated clock, so the two captured frames differ
        // by the goal alone.
        elapsedSeconds_ += FixedTimestepSeconds;
        const float seconds = pinnedSeconds_.has_value() ? *pinnedSeconds_ : elapsedSeconds_;
        if (auto status = solveForSeconds(seconds); !status)
        {
            return status;
        }
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

        // Looking down -Z at the chain, which lives in the XY plane around the origin.
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

        // localBounds must cover the chain in every pose it can reach, because commit culls with
        // it as authored and does not expand it for deformation. Full reach plus the quad's own
        // half height is that conservative sphere.
        return writer.addSkinnedMesh3D(Tina::Render::RenderSkinnedMesh3DInput{
            .mesh = *mesh,
            .material = *material,
            .submeshIndex = 0,
            .stableEntityKey = 1,
            .localBounds = {.radius = ChainReachMeters + QuadHalfHeightMeters},
            .baseColorFactor = {.red = 0.95F, .green = 0.62F, .blue = 0.16F, .alpha = 1.0F},
            .paletteColumnMajorJointMatrices = chain_->palette(),
            .alphaMode = Tina::Render::Mesh3DAlphaMode::Opaque,
            // The quads are single-sided +Z facing; a bend that swings one past the camera plane
            // would otherwise vanish and read as a rendering failure rather than a pose.
            .doubleSided = true,
            .visible = true,
        });
    }

  private:
    static constexpr float FixedTimestepSeconds = 1.0F / 60.0F;

    enum class EvidenceStage : u8 {
        Idle,
        WarmupAwait,
        PinnedNear,
        AwaitNear,
        PinnedFar,
        AwaitFar,
        Done,
    };

    [[nodiscard]] Tina::Core::Status solveForSeconds(float seconds) noexcept
    {
        const GoalSample goal = goalAt(seconds);
        if (auto status = chain_->solve(goal.position); !status)
        {
            return status;
        }
        auto error = chain_->tipError(goal.position);
        if (!error)
        {
            return Tina::Core::failure(std::move(error.error()));
        }
        // Worst error across the whole run, not the latest: a solver that reaches the goal on
        // most frames and misses on one is still broken, and a last-frame reading would hide it.
        const u32 millimetres = static_cast<u32>(std::lround(*error * 1000.0F));
        counters_->reachErrorMillimetres = std::max(counters_->reachErrorMillimetres, millimetres);
        return Tina::Core::success();
    }

    void advanceEvidence(Tina::Render::IRenderDevice& device) noexcept
    {
        const auto abandon = [this](Tina::Core::Error error) noexcept {
            if (!counters_->evidenceError.has_value())
            {
                counters_->evidenceError = std::move(error);
            }
            stage_ = EvidenceStage::Done;
        };
        const auto arm = [&](EvidenceStage next) noexcept {
            if (auto armed = device.requestPrimaryFrameCaptureOnNextPresent(); !armed)
            {
                abandon(std::move(armed.error()));
                return;
            }
            stage_ = next;
        };

        switch (stage_)
        {
        case EvidenceStage::Idle:
            if (counters_->frameUpdates < EvidenceFirstFrame)
            {
                return;
            }
            // The first capture of a run comes back blank on this host, so one is armed and
            // discarded before any measurement.
            arm(EvidenceStage::WarmupAwait);
            return;

        case EvidenceStage::WarmupAwait:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandon(std::move(captured.error()));
                return;
            }
            pinnedSeconds_ = EvidenceNearSeconds;
            arm(EvidenceStage::AwaitNear);
            return;
        }

        case EvidenceStage::PinnedNear:
            arm(EvidenceStage::AwaitNear);
            return;

        case EvidenceStage::AwaitNear:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandon(std::move(captured.error()));
                return;
            }
            nearCapture_ = std::move(*captured);
            pinnedSeconds_ = EvidenceFarSeconds;
            arm(EvidenceStage::AwaitFar);
            return;
        }

        case EvidenceStage::PinnedFar:
            arm(EvidenceStage::AwaitFar);
            return;

        case EvidenceStage::AwaitFar:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandon(std::move(captured.error()));
                return;
            }
            measure(nearCapture_, *captured);
            pinnedSeconds_.reset();
            stage_ = EvidenceStage::Done;
            return;
        }

        case EvidenceStage::Done:
            return;
        }
    }

    // Deliberately not named `near`/`far`: those are macros in the Windows SDK headers, and a
    // parameter with either name expands into a syntax error somewhere far from here.
    void measure(const Tina::Render::Rgba8FrameCapture& nearFrame,
                 const Tina::Render::Rgba8FrameCapture& farFrame) noexcept
    {
        if (nearFrame.empty() || farFrame.empty() || nearFrame.width != farFrame.width ||
            nearFrame.height != farFrame.height)
        {
            counters_->evidenceError =
                Tina::Core::Error{Tina::Core::CoreErrorCode::Internal,
                                  "the two evidence captures do not describe the same surface"};
            return;
        }

        // Counts pixels that changed rather than summing channel differences. A count is the
        // question actually being asked -- did the silhouette move -- and it does not let a few
        // strongly-changed pixels stand in for a moved limb.
        constexpr u32 ChannelThreshold = 24;
        u32 changed = 0;
        const usize pixelCount = static_cast<usize>(nearFrame.width) * nearFrame.height;
        for (usize pixel = 0; pixel < pixelCount; ++pixel)
        {
            const usize offset = pixel * 4U;
            const auto channelDelta = [&](usize channel) noexcept -> u32 {
                const auto a =
                    static_cast<int>(std::to_integer<u8>(nearFrame.rgba8Pixels[offset + channel]));
                const auto b =
                    static_cast<int>(std::to_integer<u8>(farFrame.rgba8Pixels[offset + channel]));
                return static_cast<u32>(std::abs(a - b));
            };
            if (channelDelta(0) >= ChannelThreshold || channelDelta(1) >= ChannelThreshold ||
                channelDelta(2) >= ChannelThreshold)
            {
                ++changed;
            }
        }
        counters_->pixelDeltaNearToFar = changed;
        counters_->evidenceCollected = true;
    }

    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    const DeviceCapture* capture_ = nullptr;
    std::optional<IkChain> chain_{};
    Tina::Render::GpuMeshId skinnedMesh_{};
    bool meshBound_ = false;
    bool materialBound_ = false;
    float elapsedSeconds_ = 0.0F;
    // Set means "hold the goal at this sweep time instead of the animated one".
    std::optional<float> pinnedSeconds_{};
    EvidenceStage stage_ = EvidenceStage::Idle;
    Tina::Render::Rgba8FrameCapture nearCapture_{};
    mutable KindedFrameResource meshResource_{};
    mutable KindedFrameResource materialResource_{};
};

class IkChainApplication final : public Tina::IGameApplication {
  public:
    IkChainApplication(SampleOptions options, LifecycleCounters& counters,
                       const DeviceCapture& capture) noexcept
        : options_(options), counters_(&counters), capture_(&capture)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>>
    createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>(
            std::make_unique<IkChainState>(options_, *counters_, *capture_));
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
        ++counters_->applicationShutdowns;
    }

  private:
    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    const DeviceCapture* capture_ = nullptr;
};

[[nodiscard]] Tina::EngineConfig createEngineConfig()
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina vNext 3D IK Chain";
    config.primaryWindow.title = "Tina vNext - two-bone IK drives a skinned chain";
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

    DeviceCapture capture;
    Tina::Desktop::CreateEngineOptions desktopOptions{};
    desktopOptions.wrapWindowSurfaceRenderDevice =
        [&capture](std::unique_ptr<Tina::Render::IRenderDevice> device)
            -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
        if (!device)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                       "Desktop bootstrap produced no render device");
        }
        capture.set(device.get());
        return device;
    };

    auto hostResult = Tina::Desktop::CreateEngine(createEngineConfig(), std::move(desktopOptions));
    if (!hostResult)
    {
        writeError(hostResult.error());
        return 1;
    }

    LifecycleCounters counters;
    IkChainApplication application{options, counters, capture};
    auto runResult = (*hostResult)->run(application);
    hostResult->reset();
    counters.engineHostDestroyed = true;
    if (!runResult)
    {
        writeError(runResult.error());
        return 1;
    }

    // A run too short to complete the capture sequence is a configuration mistake, not a pass:
    // without this the sample would exit 0 having proven nothing.
    if (*runResult != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame ||
        counters.frameUpdates != options.targetFrameCount ||
        counters.renderExtractions != options.targetFrameCount ||
        counters.applicationShutdowns != 1 || !counters.skinnedMeshRetired ||
        !counters.materialRetired || !counters.evidenceCollected ||
        !targetReachedAcrossSweep(counters) || !silhouetteAnimated(counters))
    {
        {
            Tina::Core::JsonWriter writer(std::cerr);
            writer.beginObject();
            writer.member("status", "error");
            writer.member("sample", SampleName);
            writer.member("message", "lifecycle counters did not match");
            writeCounters(writer, counters);
            writer.endObject();
        }
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

int runIkChain3dSample(int argumentCount, char** arguments)
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
