// First consumer of the custom Mesh3D fragment shader path.
//
// Shape of the thing:
//   - CMake cooks fs_tinted.sc into a Mesh3D Shader payload and stages it beside the executable.
//   - onEnter reads that payload, wraps it in a cooked Shader asset in memory, and uploads it
//     through Asset::uploadShaderFromCooked. One cooked fragment is linked against both the rigid
//     and the skinned engine vertex stages because those two stages declare the same varyings.
//   - The shader gets one device binding key; the author uniform values get a second, independent
//     one, because one program drawn with two value sets is what a material is.
//   - Extraction draws three objects in one frame:
//       left   - fixture cube (mesh key 1, unbound geometry) with the custom fragment
//       centre - the same fixture cube with the engine fragment (control: must not move)
//       right  - a two-bone skinned chain with the same custom fragment
//   - Two captures pin u_tint.x to 0 then 1. The custom regions must change; the engine region
//     must not. That is what separates "the shader ran on both links" from "the whole frame
//     changed for some other reason" and from "only the rigid program was wired".
//
// The payload is wrapped rather than cooked into a catalog on purpose: a catalog would add a
// package, a manifest and a load plan to a sample whose subject is the shader path. The cooked
// header still has to exist, since uploadShaderFromCooked consumes a CookedAssetFile.
//
// Uniform values are re-published from updateFrame rather than written once, because bgfx uniforms
// are draw-local: the device re-issues every author uniform before each draw, and a value the
// caller never updated would simply stay at its last published number.

#include "Sample.hpp"

#include <tina/asset/AssetGpuMesh.hpp>
#include <tina/asset/AssetGpuShader.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/ShaderPayload.hpp>
#include <tina/asset_format/SkinnedMeshPayload.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/io/ApplicationPaths.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/text/ArgParser.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/render/FramePin.hpp>
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
#include <cstddef>
#include <cstdint>
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

constexpr std::string_view SampleName = "tina_sample_3d_custom_shader";
constexpr std::string_view PayloadFileName = "fs_tinted.shaderpayload";
constexpr std::string_view TintUniformName = "u_tint";

constexpr u64 DefaultFrameCount = 90;

// Shader program and author uniforms live in independent binding-key namespaces, so both may be 1.
// Mesh key 1 is reserved for the built-in procedural cube fixture; the skinned chain therefore
// occupies key 2. Material key 1 is shared: every draw in this sample wants the same scalars.
constexpr u32 ShaderBindingKey = 1;
constexpr u32 ShaderUniformBindingKey = 1;
constexpr u32 FixtureMeshBindingKey = 1;
constexpr u32 SkinnedMeshBindingKey = 2;
constexpr u32 MaterialBindingKey = 1;

constexpr u16 JointCount = 2;
constexpr u16 VerticesPerBone = 4;
constexpr u16 IndicesPerBone = 6;
constexpr u16 BoneCount = 2;
constexpr u16 VertexCount = VerticesPerBone * BoneCount;
constexpr u16 IndexCount = IndicesPerBone * BoneCount;
constexpr float BoneLengthMeters = 1.0F;
constexpr float ChainReachMeters = BoneLengthMeters * static_cast<float>(BoneCount);
constexpr float QuadHalfHeightMeters = 0.25F;

constexpr float TintStripeFrequency = 2.0F;
constexpr float EvidencePhaseA = 0.0F;
constexpr float EvidencePhaseB = 1.0F;

// Both captures must be armed after the first frames have settled, so the pair differs by the
// uniform alone rather than by startup state.
constexpr u64 EvidenceFirstFrame = 8;
// Frames the arm/collect sequence needs after EvidenceFirstFrame, with slack. A run shorter than
// this cannot produce evidence, so --frames is rejected rather than exiting 0 having proven nothing.
constexpr u64 EvidenceRequiredFrames = EvidenceFirstFrame + 8;
constexpr u64 MinimumFrameCount = EvidenceRequiredFrames;

// Predicted mean-absolute RGB delta between the two tint phases, argued from the fragment, not
// from a measured run.
//
// The fragment mixes a red band (1.00, 0.12, 0.08) into a green band (0.08, 1.00, 0.16) and then
// multiplies by the sampled colour (unbound s_texColor is 1x1 white) and a 0.70..1.00 stripe.
// Channel-wise mean |A-B| over a region that covers both stripe phases is therefore at least
// 0.70 * mean(|1.00-0.08|, |0.12-1.00|, |0.08-0.16|) = 0.70 * (0.92+0.88+0.08)/3 = 0.438 of the
// 0-1 range, which is ~112 of 255.
//
// 16 is two orders of magnitude below that guaranteed floor and still above a rounding wobble.
// It fails the defects this exists to catch:
//   - fragment never bound (engine PBR, both phases identical) scores 0
//   - only the rigid program linked (skinned region is engine PBR) scores 0 on that region
//   - u_tint never reaches the GPU (both phases identical) scores 0
//   - a leak of the previous draw's value would still pass the custom-vs-custom check, which is
//     why the engine control region must stay at 0: a whole-frame lighting wobble would move it
constexpr u32 EvidenceMinimumChannelDelta = 16;

constexpr float CameraDistanceMeters = 4.6F;
constexpr float CameraHeightMeters = 0.6F;
constexpr float CameraFovYDegrees = 55.0F;

constexpr float LeftCubeX = -2.5F;
constexpr float CentreCubeX = 0.0F;
constexpr float SkinnedOriginX = 1.6F;
constexpr float CubeInsetMeters = 0.45F;
constexpr float SkinnedInsetMinX = 0.40F;
constexpr float SkinnedInsetMaxX = 1.60F;
constexpr float SkinnedInsetHalfY = 0.10F;

// Any non-zero AssetId works: nothing here consults a catalog, and uploadShaderFromCooked only
// reads the payload behind the header.
constexpr std::string_view ShaderAssetIdText = "a3c81e5f7b204d6e9f1a2c4d8e0b3657";

struct SampleOptions final {
    u64 targetFrameCount = DefaultFrameCount;
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 renderExtractions = 0;
    u64 uniformPublishes = 0;
    u64 stateEnters = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
    u32 shaderBlobCount = 0;
    u32 shaderPayloadBytes = 0;
    bool shaderRetired = false;
    bool skinnedMeshRetired = false;
    bool materialRetired = false;
    bool engineHostDestroyed = false;
    // Pixel evidence. staticCustomDelta / skinnedCustomDelta are mean absolute RGB differences
    // over the custom-shader regions between the two pinned phases; engineControlDelta is the
    // same measure over the cube that names no shader. A working custom fragment stage moves the
    // first two and leaves the third at zero.
    u32 staticCustomDelta = 0;
    u32 skinnedCustomDelta = 0;
    u32 engineControlDelta = 0;
    bool evidenceCollected = false;
    std::optional<Tina::Core::Error> evidenceError{};
};

struct ScreenBox final {
    u32 minimumX = 0;
    u32 minimumY = 0;
    u32 maximumX = 0;
    u32 maximumY = 0;
};

[[nodiscard]] ScreenBox worldRectScreenBox(float minX, float maxX, float minY, float maxY, u32 width,
                                           u32 height) noexcept
{
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const float halfV = std::tan(CameraFovYDegrees * 0.5F * 3.14159265F / 180.0F) * CameraDistanceMeters;
    const float halfH = halfV * aspect;
    const auto toPixelX = [width, halfH](float x) noexcept {
        return static_cast<u32>(
            std::clamp((x / halfH * 0.5F + 0.5F) * static_cast<float>(width), 0.0F,
                       static_cast<float>(width)));
    };
    const auto toPixelY = [height](float y) noexcept {
        const float halfVLocal =
            std::tan(CameraFovYDegrees * 0.5F * 3.14159265F / 180.0F) * CameraDistanceMeters;
        const float ndcY = (y - CameraHeightMeters) / halfVLocal;
        return static_cast<u32>(
            std::clamp((0.5F - ndcY * 0.5F) * static_cast<float>(height), 0.0F,
                       static_cast<float>(height)));
    };
    u32 x0 = toPixelX(minX);
    u32 x1 = toPixelX(maxX);
    u32 y0 = toPixelY(maxY);
    u32 y1 = toPixelY(minY);
    if (x1 < x0)
    {
        std::swap(x0, x1);
    }
    if (y1 < y0)
    {
        std::swap(y0, y1);
    }
    return ScreenBox{.minimumX = x0, .minimumY = y0, .maximumX = x1, .maximumY = y1};
}

// Mean absolute RGB delta between two captures over one axis-aligned region, in 0-255 units.
// Alpha is skipped: the fragment writes opaque colour, so a colour-only change is exactly what
// this must detect.
[[nodiscard]] u32 meanRegionDelta(const Tina::Render::Rgba8FrameCapture& left,
                                  const Tina::Render::Rgba8FrameCapture& right, u32 minimumX,
                                  u32 minimumY, u32 maximumX, u32 maximumY) noexcept
{
    if (left.width != right.width || left.height != right.height || left.empty() ||
        maximumX > left.width || maximumY > left.height || minimumX >= maximumX ||
        minimumY >= maximumY)
    {
        return 0;
    }

    u64 total = 0;
    u64 samples = 0;
    for (u32 y = minimumY; y < maximumY; ++y)
    {
        for (u32 x = minimumX; x < maximumX; ++x)
        {
            const usize base = (static_cast<usize>(y) * left.width + x) * 4U;
            for (u32 channel = 0; channel < 3U; ++channel)
            {
                const auto a = std::to_integer<int>(left.rgba8Pixels[base + channel]);
                const auto b = std::to_integer<int>(right.rgba8Pixels[base + channel]);
                total += static_cast<u64>(a > b ? a - b : b - a);
                ++samples;
            }
        }
    }
    return samples == 0 ? 0U : static_cast<u32>(total / samples);
}

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
        joint.name = index == 0U ? "root" : "middle";

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

[[nodiscard]] Tina::Core::Result<Tina::Asset::CookedAssetFile> buildCookedSkinnedMesh(
    std::span<const std::byte> meshBytes)
{
    Tina::Core::AssetId::Bytes idBytes{};
    idBytes[0] = std::byte{3};
    auto cookedBytes =
        Tina::AssetFormat::writeCookedAssetBytes(Tina::AssetFormat::CookedAssetWriteDesc{
            .assetKind = Tina::AssetFormat::AssetKind::SkinnedMesh,
            .assetTypeVersion = Tina::AssetFormat::SkinnedMeshWire::SchemaVersion,
            .assetId = *Tina::Core::AssetId::fromBytes(idBytes),
            .payload = meshBytes,
        });
    if (!cookedBytes)
    {
        return Tina::Core::failure(
            std::move(cookedBytes.error()).withContext("buildCookedSkinnedMesh",
                                                       "writeCookedAssetBytes"));
    }

    std::pmr::vector<std::byte> owned{std::pmr::get_default_resource()};
    owned.assign(cookedBytes->begin(), cookedBytes->end());
    return Tina::Asset::makeCookedAssetFileFromBytes(
        std::move(owned), Tina::Asset::CookedAssetFileLoadConfig{
                              .memoryResource = std::pmr::get_default_resource()});
}

[[nodiscard]] std::string errorCodeName(Tina::Core::ErrorCode code)
{
    return "tina." + std::to_string(static_cast<std::uint16_t>(code.domain)) + "." +
           std::to_string(code.value);
}

void writeError(const Tina::Core::Error& error)
{
    Tina::Core::JsonWriter writer(std::cerr);
    writer.beginObject();
    writer.member("status", "error");
    writer.member("sample", SampleName);
    writer.member("code", errorCodeName(error.code));
    writer.member("message", error.message);
    writer.endObject();
    std::cerr << '\n';
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
                options.targetFrameCount < MinimumFrameCount)
            {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "--frames must appear once and be at least large enough to collect the "
                    "evidence sequence");
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

[[nodiscard]] Tina::Core::Result<Tina::Asset::CookedAssetFile> loadShaderAsset(u32& payloadBytes)
{
    auto payloadPath = Tina::Core::applicationFilePath(PayloadFileName);
    if (!payloadPath)
    {
        return Tina::Core::failure(std::move(payloadPath.error())
                                       .withContext("loadShaderAsset", "applicationFilePath"));
    }

    auto payload = Tina::Core::readFile(
        *payloadPath, Tina::Core::ReadFileConfig{.memoryResource = std::pmr::get_default_resource()});
    if (!payload)
    {
        return Tina::Core::failure(
            std::move(payload.error()).withContext("loadShaderAsset", *payloadPath));
    }
    payloadBytes = static_cast<u32>(payload->size());

    auto payloadView = Tina::AssetFormat::parseShaderPayload(*payload);
    if (!payloadView)
    {
        return Tina::Core::failure(
            std::move(payloadView.error()).withContext("loadShaderAsset", "parseShaderPayload"));
    }
    if (payloadView->shaderKind != Tina::AssetFormat::ShaderKind::Mesh3D)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "cooked shader payload is not a Mesh3D shader");
    }

    const auto assetId = Tina::Core::AssetId::parseCanonical(ShaderAssetIdText);
    if (!assetId)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "sample shader AssetId literal is not canonical");
    }

    auto cooked = Tina::AssetFormat::writeCookedAssetBytes(Tina::AssetFormat::CookedAssetWriteDesc{
        .assetKind = Tina::AssetFormat::AssetKind::Shader,
        .assetTypeVersion = Tina::AssetFormat::ShaderWire::SchemaVersion,
        .targetPlatform = Tina::AssetFormat::TargetPlatform::WindowsX64,
        .assetId = *assetId,
        .payload = *payload,
        .payloadAlignment = 16,
        .computeContentHash = true,
    });
    if (!cooked)
    {
        return Tina::Core::failure(
            std::move(cooked.error()).withContext("loadShaderAsset", "writeCookedAssetBytes"));
    }

    std::pmr::vector<std::byte> cookedBytes{std::pmr::get_default_resource()};
    cookedBytes.assign(cooked->begin(), cooked->end());
    return Tina::Asset::makeCookedAssetFileFromBytes(std::move(cookedBytes),
                                                     Tina::Asset::CookedAssetFileLoadConfig{});
}

[[nodiscard]] Tina::Render::GpuShaderUniformValue tintUniform(float mix) noexcept
{
    Tina::Render::GpuShaderUniformValue entry{};
    const usize length =
        (std::min)(TintUniformName.size(),
                   static_cast<usize>(Tina::Render::GpuShaderUniformValue::MaximumNameBytes));
    std::copy_n(TintUniformName.begin(), length, entry.name.begin());
    entry.value = {mix, TintStripeFrequency, 0.0F, 0.0F};
    return entry;
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

class CustomShader3dState final : public Tina::IGameState {
  public:
    CustomShader3dState(SampleOptions options, LifecycleCounters& counters) noexcept
        : options_(options), counters_(&counters)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        ++counters_->stateEnters;
        Tina::Render::IRenderDevice& device = context.renderDevice();

        u32 payloadBytes = 0;
        auto shaderAsset = loadShaderAsset(payloadBytes);
        if (!shaderAsset)
        {
            return Tina::Core::failure(std::move(shaderAsset.error()));
        }
        counters_->shaderPayloadBytes = payloadBytes;

        auto payloadView = Tina::Asset::parseShaderFromCooked(*shaderAsset);
        if (!payloadView)
        {
            return Tina::Core::failure(std::move(payloadView.error()));
        }
        counters_->shaderBlobCount = payloadView->blobCount;

        auto shader = Tina::Asset::uploadShaderFromCooked(device, *shaderAsset);
        if (!shader)
        {
            return Tina::Core::failure(std::move(shader.error()));
        }
        shader_ = *shader;
        if (auto status = device.setShaderBinding(ShaderBindingKey, shader_); !status)
        {
            return status;
        }
        shaderBound_ = true;

        auto meshBytes = buildSkinnedMeshPayload();
        if (meshBytes.empty())
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "failed to build the skinned mesh payload");
        }
        auto cookedMesh = buildCookedSkinnedMesh(meshBytes);
        if (!cookedMesh)
        {
            return Tina::Core::failure(std::move(cookedMesh.error()));
        }
        auto mesh = Tina::Asset::uploadSkinnedMeshFromCooked(device, *cookedMesh);
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

        for (u16 joint = 0; joint < JointCount; ++joint)
        {
            const usize base = static_cast<usize>(joint) * 16U;
            identityPalette_[base + 0U] = 1.0F;
            identityPalette_[base + 5U] = 1.0F;
            identityPalette_[base + 10U] = 1.0F;
            identityPalette_[base + 15U] = 1.0F;
        }

        return publishUniforms(device, EvidencePhaseA);
    }

    void onExit(Tina::GameStateExitContext& context) noexcept override
    {
        ++counters_->stateExits;
        Tina::Render::IRenderDevice& device = context.renderDevice();

        static_cast<void>(device.setShaderUniformBinding(ShaderUniformBindingKey,
                                                         Tina::Render::GpuShaderUniformBindingDesc{}));
        if (shaderBound_)
        {
            static_cast<void>(device.setShaderBinding(ShaderBindingKey, Tina::Render::GpuShaderId{}));
            shaderBound_ = false;
        }
        if (shader_)
        {
            Tina::Render::FramePin completion{};
            if (device.retireShader(shader_, completion))
            {
                counters_->shaderRetired = true;
            }
            shader_ = Tina::Render::GpuShaderId{};
        }
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
            static_cast<void>(
                device.setMesh3DBinding(SkinnedMeshBindingKey, Tina::Render::GpuMeshId{}));
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
        const float publishedMix = evidencePhase_.has_value() ? *evidencePhase_ : EvidencePhaseA;
        if (auto status = publishUniforms(*device, publishedMix); !status)
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
        auto& writer = context.renderSceneWriter();
        if (auto status = writer.setPerspectiveCamera(Tina::Render::RenderPerspectiveCameraInput{
                .stableCameraKey = 1,
                .worldPose = {.positionX = 0.0F,
                              .positionY = CameraHeightMeters,
                              .positionZ = CameraDistanceMeters},
                .verticalFovDegrees = CameraFovYDegrees,
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
        auto fixtureMesh =
            fixtureMeshResource_.intern(sink, Tina::Render::FrameResourceKind::Mesh3DGeometry,
                                        FixtureMeshBindingKey);
        if (!fixtureMesh)
        {
            return Tina::Core::failure(std::move(fixtureMesh.error()));
        }
        auto skinnedMesh =
            skinnedMeshResource_.intern(sink, Tina::Render::FrameResourceKind::SkinnedMesh3DGeometry,
                                        SkinnedMeshBindingKey);
        if (!skinnedMesh)
        {
            return Tina::Core::failure(std::move(skinnedMesh.error()));
        }
        auto material = materialResource_.intern(sink, Tina::Render::FrameResourceKind::Mesh3DMaterial,
                                                 MaterialBindingKey);
        if (!material)
        {
            return Tina::Core::failure(std::move(material.error()));
        }
        auto shader =
            shaderResource_.intern(sink, Tina::Render::FrameResourceKind::Shader, ShaderBindingKey);
        if (!shader)
        {
            return Tina::Core::failure(std::move(shader.error()));
        }
        auto uniforms = uniformResource_.intern(sink, Tina::Render::FrameResourceKind::ShaderUniforms,
                                                ShaderUniformBindingKey);
        if (!uniforms)
        {
            return Tina::Core::failure(std::move(uniforms.error()));
        }

        if (auto status = writer.addMesh3D(Tina::Render::RenderMesh3DInput{
                .mesh = *fixtureMesh,
                .material = *material,
                .shader = *shader,
                .shaderUniforms = *uniforms,
                .submeshIndex = 0,
                .stableEntityKey = 1,
                .worldTransform = {.pose = {.positionX = LeftCubeX}},
                .localBounds = {.radius = 1.8F},
                .baseColorFactor = {.red = 1.0F, .green = 1.0F, .blue = 1.0F, .alpha = 1.0F},
                .alphaMode = Tina::Render::Mesh3DAlphaMode::Opaque,
                .doubleSided = false,
                .visible = true,
            });
            !status)
        {
            return status;
        }
        if (auto status = writer.addMesh3D(Tina::Render::RenderMesh3DInput{
                .mesh = *fixtureMesh,
                .material = *material,
                .submeshIndex = 0,
                .stableEntityKey = 2,
                .worldTransform = {.pose = {.positionX = CentreCubeX}},
                .localBounds = {.radius = 1.8F},
                .baseColorFactor = {.red = 0.72F, .green = 0.74F, .blue = 0.80F, .alpha = 1.0F},
                .alphaMode = Tina::Render::Mesh3DAlphaMode::Opaque,
                .doubleSided = false,
                .visible = true,
            });
            !status)
        {
            return status;
        }
        if (auto status = writer.addSkinnedMesh3D(Tina::Render::RenderSkinnedMesh3DInput{
                .mesh = *skinnedMesh,
                .material = *material,
                .shader = *shader,
                .shaderUniforms = *uniforms,
                .submeshIndex = 0,
                .stableEntityKey = 3,
                .worldTransform = {.pose = {.positionX = SkinnedOriginX}},
                .localBounds = {.radius = ChainReachMeters + QuadHalfHeightMeters},
                .baseColorFactor = {.red = 1.0F, .green = 1.0F, .blue = 1.0F, .alpha = 1.0F},
                .paletteColumnMajorJointMatrices = identityPalette_,
                .alphaMode = Tina::Render::Mesh3DAlphaMode::Opaque,
                .doubleSided = true,
                .visible = true,
            });
            !status)
        {
            return status;
        }

        ++counters_->renderExtractions;
        return Tina::Core::success();
    }

  private:
    void advanceEvidence(Tina::Render::IRenderDevice& device) noexcept
    {
        const auto abandon = [this](Tina::Core::Error error) noexcept {
            if (!counters_->evidenceError.has_value())
            {
                counters_->evidenceError = std::move(error);
            }
            stage_ = EvidenceStage::Done;
            evidencePhase_.reset();
        };

        switch (stage_)
        {
        case EvidenceStage::Idle:
            if (counters_->frameUpdates < EvidenceFirstFrame)
            {
                return;
            }
            if (auto armed = device.requestPrimaryFrameCaptureOnNextPresent(); !armed)
            {
                abandon(std::move(armed.error()));
                return;
            }
            stage_ = EvidenceStage::WarmupAwait;
            return;

        case EvidenceStage::WarmupAwait:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandon(std::move(captured.error()));
                return;
            }
            evidencePhase_ = EvidencePhaseA;
            stage_ = EvidenceStage::PinnedA;
            return;
        }

        case EvidenceStage::PinnedA:
            if (auto armed = device.requestPrimaryFrameCaptureOnNextPresent(); !armed)
            {
                abandon(std::move(armed.error()));
                return;
            }
            stage_ = EvidenceStage::AwaitA;
            return;

        case EvidenceStage::AwaitA:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandon(std::move(captured.error()));
                return;
            }
            captureA_ = std::move(*captured);
            evidencePhase_ = EvidencePhaseB;
            stage_ = EvidenceStage::PinnedB;
            return;
        }

        case EvidenceStage::PinnedB:
            if (auto armed = device.requestPrimaryFrameCaptureOnNextPresent(); !armed)
            {
                abandon(std::move(armed.error()));
                return;
            }
            stage_ = EvidenceStage::AwaitB;
            return;

        case EvidenceStage::AwaitB:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandon(std::move(captured.error()));
                return;
            }
            const Tina::Render::Rgba8FrameCapture& captureB = *captured;
            const ScreenBox staticBox =
                worldRectScreenBox(LeftCubeX - CubeInsetMeters, LeftCubeX + CubeInsetMeters,
                                   -CubeInsetMeters, CubeInsetMeters, captureB.width, captureB.height);
            const ScreenBox engineBox =
                worldRectScreenBox(CentreCubeX - CubeInsetMeters, CentreCubeX + CubeInsetMeters,
                                   -CubeInsetMeters, CubeInsetMeters, captureB.width, captureB.height);
            const ScreenBox skinnedBox = worldRectScreenBox(
                SkinnedOriginX + SkinnedInsetMinX, SkinnedOriginX + SkinnedInsetMaxX,
                -SkinnedInsetHalfY, SkinnedInsetHalfY, captureB.width, captureB.height);
            counters_->staticCustomDelta =
                meanRegionDelta(captureA_, captureB, staticBox.minimumX, staticBox.minimumY,
                                staticBox.maximumX, staticBox.maximumY);
            counters_->skinnedCustomDelta =
                meanRegionDelta(captureA_, captureB, skinnedBox.minimumX, skinnedBox.minimumY,
                                skinnedBox.maximumX, skinnedBox.maximumY);
            counters_->engineControlDelta =
                meanRegionDelta(captureA_, captureB, engineBox.minimumX, engineBox.minimumY,
                                engineBox.maximumX, engineBox.maximumY);
            counters_->evidenceCollected = true;
            captureA_ = Tina::Render::Rgba8FrameCapture{};
            evidencePhase_.reset();
            stage_ = EvidenceStage::Done;
            return;
        }

        case EvidenceStage::Done:
            return;
        }
    }

    Tina::Core::Status publishUniforms(Tina::Render::IRenderDevice& device, float mix) noexcept
    {
        const std::array values{tintUniform(mix)};
        if (auto status = device.setShaderUniformBinding(
                ShaderUniformBindingKey, Tina::Render::GpuShaderUniformBindingDesc{.values = values});
            !status)
        {
            return status;
        }
        ++counters_->uniformPublishes;
        return Tina::Core::success();
    }

    enum class EvidenceStage : u8 {
        Idle,
        WarmupAwait,
        PinnedA,
        AwaitA,
        PinnedB,
        AwaitB,
        Done,
    };

    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    Tina::Render::GpuShaderId shader_{};
    Tina::Render::GpuMeshId skinnedMesh_{};
    std::array<float, static_cast<usize>(JointCount) * 16U> identityPalette_{};
    bool shaderBound_ = false;
    bool meshBound_ = false;
    bool materialBound_ = false;
    EvidenceStage stage_ = EvidenceStage::Idle;
    std::optional<float> evidencePhase_{};
    Tina::Render::Rgba8FrameCapture captureA_{};
    mutable KindedFrameResource fixtureMeshResource_{};
    mutable KindedFrameResource skinnedMeshResource_{};
    mutable KindedFrameResource materialResource_{};
    mutable KindedFrameResource shaderResource_{};
    mutable KindedFrameResource uniformResource_{};
};

class CustomShader3dApplication final : public Tina::IGameApplication {
  public:
    CustomShader3dApplication(SampleOptions options, LifecycleCounters& counters) noexcept
        : options_(options), counters_(&counters)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>>
    createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{
            std::make_unique<CustomShader3dState>(options_, *counters_)};
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
    config.applicationName = "Tina vNext 3D Custom Shader";
    config.primaryWindow.title = "Tina vNext - Mesh3D custom fragment shader";
    config.primaryWindow.initialLogicalExtent = {1280, 720};
    config.primaryWindow.initiallyVisible = true;
    return config;
}

[[nodiscard]] bool evidenceAccepted(const LifecycleCounters& counters) noexcept
{
    if (counters.evidenceCollected)
    {
        return counters.staticCustomDelta >= EvidenceMinimumChannelDelta &&
               counters.skinnedCustomDelta >= EvidenceMinimumChannelDelta &&
               counters.engineControlDelta == 0;
    }
    if (counters.evidenceError.has_value())
    {
        return counters.evidenceError->code.domain == Tina::Core::ErrorDomain::Render;
    }
    return false;
}

void writeCounters(Tina::Core::JsonWriter& writer, const LifecycleCounters& counters)
{
    writer.member("frames", counters.frameUpdates);
    writer.member("renderExtractions", counters.renderExtractions);
    writer.member("uniformPublishes", counters.uniformPublishes);
    writer.member("shaderPayloadBytes", counters.shaderPayloadBytes);
    writer.member("shaderBlobCount", counters.shaderBlobCount);
    writer.member("shaderRetired", counters.shaderRetired);
    writer.member("skinnedMeshRetired", counters.skinnedMeshRetired);
    writer.member("materialRetired", counters.materialRetired);
    writer.member("evidenceCollected", counters.evidenceCollected);
    writer.member("staticCustomDelta", counters.staticCustomDelta);
    writer.member("skinnedCustomDelta", counters.skinnedCustomDelta);
    writer.member("engineControlDelta", counters.engineControlDelta);
    writer.member("evidenceError", counters.evidenceError.has_value()
                                       ? errorCodeName(counters.evidenceError->code)
                                       : std::string{});
    writer.member("applicationShutdowns", counters.applicationShutdowns);
    writer.member("engineHostDestroyed", counters.engineHostDestroyed);
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
    CustomShader3dApplication application{options, counters};
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
                             counters.uniformPublishes == options.targetFrameCount + 1U &&
                             counters.stateEnters == 1 && counters.stateExits == 1 &&
                             counters.applicationShutdowns == 1 && counters.shaderBlobCount != 0 &&
                             counters.shaderRetired && counters.skinnedMeshRetired &&
                             counters.materialRetired && counters.engineHostDestroyed;
    if (!lifecycleOk || !evidenceAccepted(counters))
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

int runCustomShader3dSample(int argumentCount, char** arguments)
{
    try
    {
        return runSample(argumentCount, arguments);
    } catch (const std::bad_alloc&)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::OutOfMemory,
                                "The 3D custom shader sample ran out of memory"};
        writeError(error);
        return 1;
    } catch (const std::exception& exception)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "An exception crossed the 3D custom shader sample boundary"};
        error.addContext("main", exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    } catch (...)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "A non-standard exception crossed the 3D custom shader sample boundary"};
        writeError(error);
        return 1;
    }
}
