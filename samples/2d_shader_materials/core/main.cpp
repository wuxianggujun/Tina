// One cooked fragment program, three materials, one real texture.
//
// samples/2d_custom_shader proved the shader path exists: cook, upload, bind, publish one uniform
// set, draw. This sample covers the parts that one left unproven, which are the parts a real game
// actually depends on:
//
//   - Three independent uniform binding keys against a *single* uploaded program. That is what a
//     material is: same code, different numbers. If the device leaked values between keys, every
//     sprite would come out the same colour.
//   - A real uploaded texture rather than the device's 1x1 white fallback. An unbound Texture2D key
//     silently resolves to that fallback, so a shader that never sampled correctly still looks
//     plausible. The texture here is a 4x4 with one distinct colour per quadrant, which makes a
//     wrong UV visible as a wrong quadrant colour.
//   - UV transform inside the fragment stage, so the sampler and the varying are both exercised
//     rather than just the uniform.
//   - Batch splitting: sprites are emitted interleaved by material, and each material forms its own
//     batch because shaderUniforms is part of the batch key.
//
//   - Name-based matching of a value table to the program's uniforms. One material publishes its two
//     values in reverse order, so a device that took them positionally would draw it wrong. With
//     every table in the same order that defect is invisible: it is the one this sample could not
//     see before, since separation and flatness both survive it.
//
// Pixel evidence compares the three material regions against each other in a single frame, which is
// a stronger criterion than the two-phase comparison the first sample used: it cannot be satisfied
// by anything that changes the whole frame uniformly.

#include "Sample.hpp"

#include "../../common/SampleSpriteFrameResource.hpp"

#include <tina/asset/AssetGpuShader.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/ShaderPayload.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/io/ApplicationPaths.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/text/ArgParser.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/PhaseContexts.hpp>
#include <tina/runtime/RunExitReason.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace {

using Tina::Core::u16;
using Tina::Core::u32;
using Tina::Core::u64;
using Tina::Core::u8;
using Tina::Core::usize;

constexpr std::string_view SampleName = "tina_sample_2d_shader_materials";
constexpr std::string_view PayloadFileName = "fs_tint.shaderpayload";
constexpr std::string_view TintUniformName = "u_tint";
constexpr std::string_view UvAdjustUniformName = "u_uvAdjust";

constexpr u64 DefaultFrameCount = 300;

// Any non-zero AssetId works: nothing here consults a catalog, and uploadShaderFromCooked only
// reads the payload behind the header.
constexpr std::string_view ShaderAssetIdText = "3f1b7c92a4d5e6081b2c3d4e5f607182";

// The shader program gets one binding key; each material gets its own key in the independent
// uniform namespace. Both namespaces start at 1, so the numbers may overlap.
constexpr u32 ShaderBindingKey = 1;
constexpr u64 TextureBindingKeyUnused = 0;

constexpr u32 MaterialCount = 3;
// Two sprites per material, emitted interleaved, so a batch-key defect shows up as a wrong colour
// rather than as a missing sprite.
constexpr u32 SpritesPerMaterial = 2;
constexpr u32 SpriteCount = MaterialCount * SpritesPerMaterial;

constexpr float CameraWorldWidthMeters = 16.0F;
constexpr float CameraWorldHeightMeters = 9.0F;
constexpr float SpriteExtentMeters = 2.2F;

// A 4x4 RGBA8 texture with one saturated colour per 2x2 quadrant. Generated in code rather than
// cooked because the subject here is the shader path, and a file would add an asset pipeline to the
// evidence chain. Colour space is Linear so the bytes reach the sampler unchanged: an Srgb texture
// would be gamma-decoded on read and the expected pixel values would no longer be the authored
// ones.
constexpr u16 TextureExtent = 4;

struct TextureQuadrantColor final {
    u8 red = 0;
    u8 green = 0;
    u8 blue = 0;
};

// Top-left, top-right, bottom-left, bottom-right in texture space (V grows downward in the bytes).
constexpr std::array<TextureQuadrantColor, 4> QuadrantColors{{
    {.red = 255, .green = 40, .blue = 40},
    {.red = 40, .green = 255, .blue = 40},
    {.red = 40, .green = 40, .blue = 255},
    {.red = 240, .green = 240, .blue = 40},
}};

using TexturePixels = std::array<std::byte, static_cast<usize>(TextureExtent) * TextureExtent * 4U>;

[[nodiscard]] TexturePixels makeQuadrantTexture() noexcept
{
    TexturePixels pixels{};
    for (u16 y = 0; y < TextureExtent; ++y)
    {
        for (u16 x = 0; x < TextureExtent; ++x)
        {
            const usize quadrant = (y < TextureExtent / 2U ? 0U : 2U) + (x < TextureExtent / 2U ? 0U : 1U);
            const TextureQuadrantColor& color = QuadrantColors[quadrant];
            const usize base = (static_cast<usize>(y) * TextureExtent + x) * 4U;
            pixels[base + 0U] = static_cast<std::byte>(color.red);
            pixels[base + 1U] = static_cast<std::byte>(color.green);
            pixels[base + 2U] = static_cast<std::byte>(color.blue);
            pixels[base + 3U] = static_cast<std::byte>(255);
        }
    }
    return pixels;
}

// One material is a uniform binding key plus the numbers published under it. Nothing else about a
// material exists: the program, the vertex stage and the texture are shared by all three.
struct MaterialSpec final {
    u32 uniformBindingKey = 0;
    // u_tint.rgb multiplier and u_tint.a swizzle selector.
    float tintRed = 1.0F;
    float tintGreen = 1.0F;
    float tintBlue = 1.0F;
    float swizzle = 0.0F;
    // u_uvAdjust: xy offset, z scale about the quad centre.
    float uvOffsetX = 0.0F;
    float uvOffsetY = 0.0F;
    float uvScale = 1.0F;
    // Publishes u_uvAdjust before u_tint instead of after. Nothing about the material changes; it
    // exists so the pixel criterion covers name matching rather than only value separation.
    bool reverseUniformOrder = false;
};

// Chosen so no two materials can produce the same pixels from the same texels:
//   0 -- identity: the texture's own four quadrant colours, unmodified.
//   1 -- swizzled and dimmed: red and blue quadrants trade places, everything darker.
//   2 -- UV zoom onto a single texel, so the whole quad is one flat colour.
// The third is the strongest of the three as evidence, because a flat quad cannot be produced by
// any tint of a four-quadrant sample.
constexpr std::array<MaterialSpec, MaterialCount> Materials{{
    {.uniformBindingKey = 1, .tintRed = 1.0F, .tintGreen = 1.0F, .tintBlue = 1.0F, .swizzle = 0.0F,
     .uvScale = 1.0F},
    {.uniformBindingKey = 2, .tintRed = 0.55F, .tintGreen = 0.55F, .tintBlue = 0.55F,
     .swizzle = 1.0F, .uvScale = 1.0F},
    // Reversed on purpose, and on this material rather than another: it is the one whose evidence is a
    // *shape* (a flat quad) rather than a colour offset. Take its two values positionally instead of
    // by name and u_uvAdjust.z becomes the 1.0 tint red, so the zoom vanishes and the quad comes back
    // as a four-quadrant sample -- which flatMaterialSpread catches outright.
    {.uniformBindingKey = 3, .tintRed = 1.0F, .tintGreen = 1.0F, .tintBlue = 1.0F, .swizzle = 0.0F,
     .uvOffsetX = -0.25F, .uvOffsetY = -0.25F, .uvScale = 0.08F, .reverseUniformOrder = true},
}};

struct SpriteSpec final {
    float centerX = 0.0F;
    u32 materialIndex = 0;
};

// Interleaved by material on purpose: consecutive sprites never share a uniform binding, so the
// batcher has to split on it. Emitting them grouped would let a batch-key defect pass unnoticed.
constexpr std::array<SpriteSpec, SpriteCount> Sprites{{
    {.centerX = -5.5F, .materialIndex = 0},
    {.centerX = -3.3F, .materialIndex = 1},
    {.centerX = -1.1F, .materialIndex = 2},
    {.centerX = 1.1F, .materialIndex = 0},
    {.centerX = 3.3F, .materialIndex = 1},
    {.centerX = 5.5F, .materialIndex = 2},
}};

[[nodiscard]] Tina::Render::GpuShaderUniformValue
makeUniformValue(std::string_view name, float x, float y, float z, float w) noexcept
{
    Tina::Render::GpuShaderUniformValue entry{};
    const usize length =
        (std::min)(name.size(),
                   static_cast<usize>(Tina::Render::GpuShaderUniformValue::MaximumNameBytes));
    std::copy_n(name.begin(), length, entry.name.begin());
    entry.value = {x, y, z, w};
    return entry;
}

// The frame the evidence capture is armed on. Late enough that startup state has settled; the very
// first capture of a run comes back blank on this host, so one is discarded before this.
constexpr u64 EvidenceFirstFrame = 8;
constexpr u64 EvidenceRequiredFrames = EvidenceFirstFrame + 6;
// Predicted separations are far larger than this: material 0 vs 1 differs by the 0.45 dim plus the
// R/B swap, and material 2 is a flat quad against a four-quadrant one. This is low enough to be
// insensitive to filtering and dithering, high enough that identical materials cannot pass.
constexpr u32 EvidenceMinimumMaterialSeparation = 12;
// Two sprites sharing a material must be pixel-identical. Not zero, because they occupy different
// screen columns and a filtering seam can land differently.
constexpr u32 EvidenceMaximumSameMaterialDelta = 6;
// Material 2 zooms onto one texel, so its region must be nearly flat. A four-quadrant sample has a
// spread in the hundreds, so this separates the two cases by a wide margin.
constexpr u32 EvidenceMaximumFlatSpread = 24;
// Zero, unlike every other threshold here, because this one is exact by construction rather than
// approximate: the material zooms far inside a single texel, so every pixel in the region samples
// that one texel, tint is identity and the blend is against an opaque quad. Measured 0. The failures
// it excludes are the other three quadrant colours and the clamped edges an off-texture UV produces,
// each a whole saturated channel away, so there is no near-miss case a tolerance would need to admit.
constexpr u32 EvidenceMaximumFlatTexelDistance = 0;

struct SampleOptions final {
    u64 targetFrameCount = DefaultFrameCount;
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 renderExtractions = 0;
    // One publish per material per frame, plus one set at onEnter.
    u64 uniformPublishes = 0;
    u64 stateEnters = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
    u32 shaderBlobCount = 0;
    u32 shaderPayloadBytes = 0;
    bool shaderRetired = false;
    bool textureRetired = false;
    // Smallest separation between any two distinct materials, and largest delta between two sprites
    // that share one. A working per-material uniform binding makes the first large and the second
    // near zero; a device that leaked values across keys would collapse the first to zero.
    u32 minimumMaterialSeparation = 0;
    u32 maximumSameMaterialDelta = 0;
    // Spread inside the single-texel-zoom material's region. Near zero proves the fragment stage's
    // UV transform reached the sampler, which no tint alone could do.
    u32 flatMaterialSpread = 0;
    // Distance from that flat region's colour to the texel the material's UV actually names. Flatness
    // alone is too weak: a wrong u_uvAdjust drives the UV off the texture and clamps to a single edge
    // colour, which is just as flat while sampling a different texel. Only this pins down *which*.
    u32 flatMaterialTexelDistance = 0;
    bool evidenceCollected = false;
    std::optional<Tina::Core::Error> evidenceError{};
};

struct ScreenBox final {
    u32 minimumX = 0;
    u32 minimumY = 0;
    u32 maximumX = 0;
    u32 maximumY = 0;
};

// Inset to the middle half so a pixel-snap or filtering difference at the quad edge cannot
// contribute to a measurement.
[[nodiscard]] ScreenBox spriteScreenBox(const SpriteSpec& spec, u32 width, u32 height) noexcept
{
    const float halfExtent = SpriteExtentMeters * 0.25F;
    const float pixelsPerMeterX = static_cast<float>(width) / CameraWorldWidthMeters;
    const float pixelsPerMeterY = static_cast<float>(height) / CameraWorldHeightMeters;
    const float centerPixelX = (spec.centerX + CameraWorldWidthMeters * 0.5F) * pixelsPerMeterX;
    const float centerPixelY = CameraWorldHeightMeters * 0.5F * pixelsPerMeterY;
    const float halfPixelsX = halfExtent * pixelsPerMeterX;
    const float halfPixelsY = halfExtent * pixelsPerMeterY;

    const auto clampToWidth = [width](float value) noexcept {
        return static_cast<u32>(std::clamp(value, 0.0F, static_cast<float>(width)));
    };
    const auto clampToHeight = [height](float value) noexcept {
        return static_cast<u32>(std::clamp(value, 0.0F, static_cast<float>(height)));
    };
    return ScreenBox{
        .minimumX = clampToWidth(centerPixelX - halfPixelsX),
        .minimumY = clampToHeight(centerPixelY - halfPixelsY),
        .maximumX = clampToWidth(centerPixelX + halfPixelsX),
        .maximumY = clampToHeight(centerPixelY + halfPixelsY),
    };
}

[[nodiscard]] bool boxIsUsable(const Tina::Render::Rgba8FrameCapture& capture,
                               const ScreenBox& box) noexcept
{
    return !capture.empty() && box.maximumX <= capture.width && box.maximumY <= capture.height &&
           box.minimumX < box.maximumX && box.minimumY < box.maximumY;
}

// Mean RGB per region, in 0-255 units. Alpha is skipped: the fragment shader writes premultiplied
// colour and leaves alpha at the sampled value, so colour is what distinguishes the materials.
struct RegionMean final {
    u32 red = 0;
    u32 green = 0;
    u32 blue = 0;
};

[[nodiscard]] RegionMean regionMean(const Tina::Render::Rgba8FrameCapture& capture,
                                    const ScreenBox& box) noexcept
{
    if (!boxIsUsable(capture, box))
    {
        return RegionMean{};
    }
    std::array<u64, 3> totals{};
    u64 samples = 0;
    for (u32 y = box.minimumY; y < box.maximumY; ++y)
    {
        for (u32 x = box.minimumX; x < box.maximumX; ++x)
        {
            const usize base = (static_cast<usize>(y) * capture.width + x) * 4U;
            for (u32 channel = 0; channel < 3U; ++channel)
            {
                totals[channel] += static_cast<u64>(std::to_integer<int>(capture.rgba8Pixels[base + channel]));
            }
            ++samples;
        }
    }
    if (samples == 0)
    {
        return RegionMean{};
    }
    return RegionMean{
        .red = static_cast<u32>(totals[0] / samples),
        .green = static_cast<u32>(totals[1] / samples),
        .blue = static_cast<u32>(totals[2] / samples),
    };
}

[[nodiscard]] u32 meanSeparation(const RegionMean& left, const RegionMean& right) noexcept
{
    const auto channelDelta = [](u32 a, u32 b) noexcept { return a > b ? a - b : b - a; };
    return (channelDelta(left.red, right.red) + channelDelta(left.green, right.green) +
            channelDelta(left.blue, right.blue)) /
           3U;
}

// Largest minus smallest channel value seen anywhere in the region, maximised over the three
// channels. A four-quadrant sample spreads across most of the range; a single-texel zoom does not.
[[nodiscard]] u32 regionSpread(const Tina::Render::Rgba8FrameCapture& capture,
                               const ScreenBox& box) noexcept
{
    if (!boxIsUsable(capture, box))
    {
        return 0;
    }
    std::array<int, 3> minimums{255, 255, 255};
    std::array<int, 3> maximums{0, 0, 0};
    for (u32 y = box.minimumY; y < box.maximumY; ++y)
    {
        for (u32 x = box.minimumX; x < box.maximumX; ++x)
        {
            const usize base = (static_cast<usize>(y) * capture.width + x) * 4U;
            for (u32 channel = 0; channel < 3U; ++channel)
            {
                const int value = std::to_integer<int>(capture.rgba8Pixels[base + channel]);
                minimums[channel] = (std::min)(minimums[channel], value);
                maximums[channel] = (std::max)(maximums[channel], value);
            }
        }
    }
    u32 widest = 0;
    for (u32 channel = 0; channel < 3U; ++channel)
    {
        widest = (std::max)(widest, static_cast<u32>(maximums[channel] - minimums[channel]));
    }
    return widest;
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

// Turns the cooked payload file into the CookedAssetFile that uploadShaderFromCooked consumes. The
// payload is wrapped in memory rather than cooked into a catalog because the subject here is the
// shader path, and a catalog would add a package, manifest and load plan to the evidence chain.
[[nodiscard]] Tina::Core::Result<Tina::Asset::CookedAssetFile> loadShaderAsset(u32& payloadBytes)
{
    auto payloadPath = Tina::Core::applicationFilePath(PayloadFileName);
    if (!payloadPath)
    {
        return Tina::Core::failure(std::move(payloadPath.error())
                                       .withContext("loadShaderAsset", "applicationFilePath"));
    }

    // memoryResource is required, not optional: ReadFileConfig's default leaves it null and readFile
    // rejects that as an invalid config rather than falling back to the default resource.
    auto payload = Tina::Core::readFile(
        *payloadPath, Tina::Core::ReadFileConfig{.memoryResource = std::pmr::get_default_resource()});
    if (!payload)
    {
        return Tina::Core::failure(std::move(payload.error()).withContext("loadShaderAsset", *payloadPath));
    }
    payloadBytes = static_cast<u32>(payload->size());

    // Parsed before wrapping so a stale or truncated payload is reported against the file rather
    // than as an upload failure.
    auto payloadView = Tina::AssetFormat::parseShaderPayload(*payload);
    if (!payloadView)
    {
        return Tina::Core::failure(std::move(payloadView.error())
                                       .withContext("loadShaderAsset", "parseShaderPayload"));
    }
    if (payloadView->shaderKind != Tina::AssetFormat::ShaderKind::Sprite2D)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "cooked shader payload is not a Sprite2D shader");
    }

    const auto assetId = Tina::Core::AssetId::parseCanonical(ShaderAssetIdText);
    if (!assetId)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "sample shader AssetId literal is not canonical");
    }

    // assetTypeVersion must be the Shader schema version, not the default 1-by-omission: a producer
    // that omits it cooks an asset the loader rejects after any schema bump.
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
        return Tina::Core::failure(std::move(cooked.error())
                                       .withContext("loadShaderAsset", "writeCookedAssetBytes"));
    }

    std::pmr::vector<std::byte> cookedBytes{std::pmr::get_default_resource()};
    cookedBytes.assign(cooked->begin(), cooked->end());
    return Tina::Asset::makeCookedAssetFileFromBytes(std::move(cookedBytes),
                                                     Tina::Asset::CookedAssetFileLoadConfig{});
}

// Frame-resource owner parameterised by kind. SampleSpriteFrameResource is hard-wired to
// FrameResourceKind::Texture2D, and a draw that swapped Shader for ShaderUniforms must fail closed
// at resolve rather than bind a program as a material.
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

class ShaderMaterialsState final : public Tina::IGameState {
  public:
    ShaderMaterialsState(SampleOptions options, LifecycleCounters& counters) noexcept
        : options_(options), counters_(&counters)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        ++counters_->stateEnters;

        Tina::Render::IRenderDevice& device = context.renderDevice();

        if (auto status = uploadTexture(device); !status)
        {
            return status;
        }
        if (auto status = uploadShader(device); !status)
        {
            return status;
        }
        // Published here as well as per frame so the first extraction names bindings that already
        // carry values: a draw resolving an empty uniform binding fails closed.
        return publishMaterials(device);
    }

    void onExit(Tina::GameStateExitContext& context) noexcept override
    {
        ++counters_->stateExits;
        Tina::Render::IRenderDevice& device = context.renderDevice();

        // Uniform bindings first: retireShader clears bindings that reference the program, but a
        // uniform binding is keyed in an independent namespace and is not one of them.
        for (const MaterialSpec& material : Materials)
        {
            static_cast<void>(device.setShaderUniformBinding(
                material.uniformBindingKey, Tina::Render::GpuShaderUniformBindingDesc{}));
        }
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
        if (texture_)
        {
            Tina::Render::FramePin completion{};
            if (device.retireTexture2D(texture_, completion))
            {
                counters_->textureRetired = true;
            }
            texture_ = Tina::Render::GpuTextureId{};
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
        // Re-published every frame because bgfx uniforms are draw-local: the device re-issues each
        // author uniform before every batch, so a value the caller stops publishing would freeze at
        // its last number.
        if (auto status = publishMaterials(*device); !status)
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
        const Tina::Render::RenderCamera2DInput camera{
            .stableCameraKey = 1,
            .centerX = 0.0F,
            .centerY = 0.0F,
            .rotationRadians = 0.0F,
            .worldWidth = CameraWorldWidthMeters,
            .worldHeight = CameraWorldHeightMeters,
            .actualPixelsPerMeter = 64.0F,
            .pixelSnap = Tina::Render::RenderPixelSnapPolicy::Disabled,
        };
        if (auto status = writer.setCamera2D(camera); !status)
        {
            return status;
        }

        auto& sink = context.frameResourceSink();
        auto texture = textureResource_.intern(sink, textureBindingKey_);
        if (!texture)
        {
            return Tina::Core::failure(std::move(texture.error()));
        }
        auto shader =
            shaderResource_.intern(sink, Tina::Render::FrameResourceKind::Shader, ShaderBindingKey);
        if (!shader)
        {
            return Tina::Core::failure(std::move(shader.error()));
        }

        std::array<Tina::Render::FrameResourceRef, MaterialCount> materialRefs{};
        for (u32 index = 0; index < MaterialCount; ++index)
        {
            auto uniforms = uniformResources_[index].intern(
                sink, Tina::Render::FrameResourceKind::ShaderUniforms,
                Materials[index].uniformBindingKey);
            if (!uniforms)
            {
                return Tina::Core::failure(std::move(uniforms.error()));
            }
            materialRefs[index] = *uniforms;
        }

        for (u32 index = 0; index < SpriteCount; ++index)
        {
            const SpriteSpec& spec = Sprites[index];
            const Tina::Render::RenderSprite2DInput sprite{
                .texture = *texture,
                .shader = *shader,
                .shaderUniforms = materialRefs[spec.materialIndex],
                .stableEntityKey = static_cast<u64>(index) + 1U,
                .centerX = spec.centerX,
                .centerY = 0.0F,
                .rotationRadians = 0.0F,
                .widthMeters = SpriteExtentMeters,
                .heightMeters = SpriteExtentMeters,
                .orderInLayer = static_cast<Tina::Core::i32>(index),
                .red = 255,
                .green = 255,
                .blue = 255,
                .alpha = 255,
                .visible = true,
            };
            if (auto status = writer.addSprite2D(sprite); !status)
            {
                return status;
            }
        }
        ++counters_->renderExtractions;
        return Tina::Core::success();
    }

  private:
    // Sampler is Point/Clamp deliberately. Linear filtering across a 4x4 would blur the quadrant
    // boundaries into each other, and the single-texel zoom would sample a gradient rather than one
    // flat colour -- which would weaken the very thing the flatness criterion measures.
    Tina::Core::Status uploadTexture(Tina::Render::IRenderDevice& device) noexcept
    {
        pixels_ = makeQuadrantTexture();
        const std::array<Tina::Render::Texture2DUploadLevel, 1> levels{{
            {.width = TextureExtent, .height = TextureExtent, .bytes = std::span{pixels_}},
        }};
        auto uploaded = device.createTexture2D(Tina::Render::Texture2DUploadDesc{
            .format = Tina::Render::GpuTextureFormat::Rgba8Unorm,
            // Linear, not Srgb: an Srgb texture is gamma-decoded on read, so the sampled values
            // would no longer be the authored bytes and the expected pixels would shift.
            .colorSpace = Tina::Render::GpuTextureColorSpace::Linear,
            .sampler =
                {
                    .wrapU = Tina::Render::GpuTextureWrapMode::Clamp,
                    .wrapV = Tina::Render::GpuTextureWrapMode::Clamp,
                    .minFilter = Tina::Render::GpuTextureFilterMode::Point,
                    .magFilter = Tina::Render::GpuTextureFilterMode::Point,
                    .mipFilter = Tina::Render::GpuTextureMipFilterMode::None,
                },
            .levels = std::span{levels},
        });
        if (!uploaded)
        {
            return Tina::Core::failure(std::move(uploaded.error()));
        }
        texture_ = *uploaded;

        // Allocated rather than hard-coded: createTexture2DBinding takes the next key from the
        // device's own namespace, so this cannot collide with a key the engine already handed out.
        auto bindingKey = device.createTexture2DBinding(texture_);
        if (!bindingKey)
        {
            return Tina::Core::failure(std::move(bindingKey.error()));
        }
        textureBindingKey_ = *bindingKey;
        return Tina::Core::success();
    }

    Tina::Core::Status uploadShader(Tina::Render::IRenderDevice& device) noexcept
    {
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
        return Tina::Core::success();
    }

    // One publish per material. All three name the same program; only the numbers differ, which is
    // the whole claim this sample exists to test.
    Tina::Core::Status publishMaterials(Tina::Render::IRenderDevice& device) noexcept
    {
        for (const MaterialSpec& material : Materials)
        {
            const auto tint = makeUniformValue(TintUniformName, material.tintRed, material.tintGreen,
                                               material.tintBlue, material.swizzle);
            const auto uvAdjust = makeUniformValue(UvAdjustUniformName, material.uvOffsetX,
                                                   material.uvOffsetY, material.uvScale, 0.0F);
            // A value table is matched to the program's uniforms by name, so the order a material
            // publishes them in must not matter. One material declares them reversed to hold the
            // device to that: with every table in the same order, name matching and "take the values
            // positionally" produce identical pixels, so the criterion below could not tell a correct
            // device from one that ignored the names entirely.
            const std::array values = material.reverseUniformOrder
                                          ? std::array{uvAdjust, tint}
                                          : std::array{tint, uvAdjust};
            if (auto status = device.setShaderUniformBinding(
                    material.uniformBindingKey,
                    Tina::Render::GpuShaderUniformBindingDesc{.values = values});
                !status)
            {
                return status;
            }
            ++counters_->uniformPublishes;
        }
        return Tina::Core::success();
    }

    // Arm one capture to discard, then arm the measured one. Unlike the first sample this needs only
    // a single measured frame: the criterion compares regions *within* one frame, so there is no
    // second phase to pin. That also makes it immune to anything that changes the whole frame.
    void advanceEvidence(Tina::Render::IRenderDevice& device) noexcept
    {
        const auto abandon = [this](Tina::Core::Error error) noexcept {
            if (!counters_->evidenceError.has_value())
            {
                counters_->evidenceError = std::move(error);
            }
            stage_ = EvidenceStage::Done;
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

        // The first capture of a run comes back blank on this host, which would read as three
        // identical materials -- a false failure. One discarded capture removes that.
        case EvidenceStage::WarmupAwait:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandon(std::move(captured.error()));
                return;
            }
            if (auto armed = device.requestPrimaryFrameCaptureOnNextPresent(); !armed)
            {
                abandon(std::move(armed.error()));
                return;
            }
            stage_ = EvidenceStage::MeasureAwait;
            return;
        }

        case EvidenceStage::MeasureAwait:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandon(std::move(captured.error()));
                return;
            }
            measure(*captured);
            stage_ = EvidenceStage::Done;
            return;
        }

        case EvidenceStage::Done:
            return;
        }
    }

    void measure(const Tina::Render::Rgba8FrameCapture& capture) noexcept
    {
        // First sprite of each material, plus the second one for the same-material check.
        std::array<RegionMean, MaterialCount> firstMeans{};
        std::array<RegionMean, MaterialCount> secondMeans{};
        std::array<ScreenBox, MaterialCount> firstBoxes{};
        std::array<bool, MaterialCount> haveFirst{};
        std::array<bool, MaterialCount> haveSecond{};

        for (const SpriteSpec& spec : Sprites)
        {
            const ScreenBox box = spriteScreenBox(spec, capture.width, capture.height);
            if (!boxIsUsable(capture, box))
            {
                counters_->evidenceError = Tina::Core::Error{
                    Tina::Core::CoreErrorCode::Internal,
                    "a sprite's screen region fell outside the captured frame"};
                return;
            }
            const RegionMean mean = regionMean(capture, box);
            if (!haveFirst[spec.materialIndex])
            {
                firstMeans[spec.materialIndex] = mean;
                firstBoxes[spec.materialIndex] = box;
                haveFirst[spec.materialIndex] = true;
                continue;
            }
            if (!haveSecond[spec.materialIndex])
            {
                secondMeans[spec.materialIndex] = mean;
                haveSecond[spec.materialIndex] = true;
            }
        }

        u32 minimumSeparation = (std::numeric_limits<u32>::max)();
        for (u32 left = 0; left < MaterialCount; ++left)
        {
            for (u32 right = left + 1U; right < MaterialCount; ++right)
            {
                minimumSeparation =
                    (std::min)(minimumSeparation, meanSeparation(firstMeans[left], firstMeans[right]));
            }
        }
        u32 maximumSameDelta = 0;
        for (u32 index = 0; index < MaterialCount; ++index)
        {
            if (haveSecond[index])
            {
                maximumSameDelta =
                    (std::max)(maximumSameDelta, meanSeparation(firstMeans[index], secondMeans[index]));
            }
        }

        counters_->minimumMaterialSeparation =
            minimumSeparation == (std::numeric_limits<u32>::max)() ? 0U : minimumSeparation;
        counters_->maximumSameMaterialDelta = maximumSameDelta;
        // Material 2 is the single-texel zoom; its region must be nearly flat.
        counters_->flatMaterialSpread = regionSpread(capture, firstBoxes[MaterialCount - 1U]);
        // ...and flat at the colour its own UV names. Its offset lands at (0.25, 0.25), the texture's
        // top-left quadrant, whose colour is QuadrantColors[0]. Comparing against that rather than
        // just measuring flatness is what makes the reversed publish order load-bearing: swap that
        // material's two values and the zoom is replaced by an off-texture UV that clamps to some
        // other edge texel -- equally flat, different colour.
        const TextureQuadrantColor& expected = QuadrantColors[0];
        counters_->flatMaterialTexelDistance =
            meanSeparation(firstMeans[MaterialCount - 1U],
                           RegionMean{.red = expected.red, .green = expected.green,
                                      .blue = expected.blue});
        counters_->evidenceCollected = true;
    }

    enum class EvidenceStage : u8 {
        Idle,
        WarmupAwait,
        MeasureAwait,
        Done,
    };

    EvidenceStage stage_ = EvidenceStage::Idle;
    TexturePixels pixels_{};

    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    Tina::Render::GpuShaderId shader_{};
    Tina::Render::GpuTextureId texture_{};
    u64 textureBindingKey_ = TextureBindingKeyUnused;
    bool shaderBound_ = false;
    mutable Tina::Samples::SampleSpriteFrameResource textureResource_{};
    mutable KindedFrameResource shaderResource_{};
    mutable std::array<KindedFrameResource, MaterialCount> uniformResources_{};
};

class ShaderMaterialsApplication final : public Tina::IGameApplication {
  public:
    ShaderMaterialsApplication(SampleOptions options, LifecycleCounters& counters) noexcept
        : options_(options), counters_(&counters)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>>
    createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{
            std::make_unique<ShaderMaterialsState>(options_, *counters_)};
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
    config.applicationName = "Tina vNext 2D Shader Materials";
    config.primaryWindow.title = "Tina vNext - one program, three materials";
    config.primaryWindow.initialLogicalExtent = {1280, 720};
    config.primaryWindow.initiallyVisible = true;
    config.renderSceneCapacities.spriteCapacity = 16;
    return config;
}

// Pixel evidence is required when it was obtainable and judged strictly when it was obtained, but a
// backend that declines frame read-back is a host capability rather than a statement about the
// shader path -- so a capture error is surfaced in the output instead of failing the run.
[[nodiscard]] bool evidenceAccepted(const LifecycleCounters& counters, u64 targetFrameCount) noexcept
{
    if (counters.evidenceCollected)
    {
        return counters.minimumMaterialSeparation >= EvidenceMinimumMaterialSeparation &&
               counters.maximumSameMaterialDelta <= EvidenceMaximumSameMaterialDelta &&
               counters.flatMaterialSpread <= EvidenceMaximumFlatSpread &&
               counters.flatMaterialTexelDistance <= EvidenceMaximumFlatTexelDistance;
    }
    if (counters.evidenceError.has_value())
    {
        return counters.evidenceError->code.domain == Tina::Core::ErrorDomain::Render;
    }
    // No error and no result: only legitimate if the run was too short to finish the sequence.
    return targetFrameCount < EvidenceRequiredFrames;
}

void writeCounters(Tina::Core::JsonWriter& writer, const LifecycleCounters& counters)
{
    writer.member("frames", counters.frameUpdates);
    writer.member("renderExtractions", counters.renderExtractions);
    writer.member("materials", MaterialCount);
    writer.member("spritesPerFrame", SpriteCount);
    writer.member("shaderPayloadBytes", counters.shaderPayloadBytes);
    writer.member("shaderBlobCount", counters.shaderBlobCount);
    writer.member("uniformPublishes", counters.uniformPublishes);
    writer.member("shaderRetired", counters.shaderRetired);
    writer.member("textureRetired", counters.textureRetired);
    writer.member("evidenceCollected", counters.evidenceCollected);
    writer.member("minimumMaterialSeparation", counters.minimumMaterialSeparation);
    writer.member("maximumSameMaterialDelta", counters.maximumSameMaterialDelta);
    writer.member("flatMaterialSpread", counters.flatMaterialSpread);
    writer.member("flatMaterialTexelDistance", counters.flatMaterialTexelDistance);
    writer.member("evidenceError", counters.evidenceError.has_value()
                                       ? errorCodeName(counters.evidenceError->code)
                                       : std::string{});
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
    ShaderMaterialsApplication application{options, counters};
    auto runResult = (*hostResult)->run(application);
    hostResult->reset();
    if (!runResult)
    {
        writeError(runResult.error());
        return 1;
    }

    // One publish per material at onEnter plus one per material per frame: a device that accepted
    // the upload but stopped re-issuing values would show a smaller count.
    const u64 expectedPublishes = static_cast<u64>(MaterialCount) * (options.targetFrameCount + 1U);
    if (*runResult != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame ||
        counters.frameUpdates != options.targetFrameCount ||
        counters.renderExtractions != options.targetFrameCount ||
        counters.uniformPublishes != expectedPublishes || counters.stateEnters != 1 ||
        counters.stateExits != 1 || counters.applicationShutdowns != 1 ||
        counters.shaderBlobCount == 0 || !counters.shaderRetired || !counters.textureRetired ||
        !evidenceAccepted(counters, options.targetFrameCount))
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

    {
        Tina::Core::JsonWriter writer(std::cout);
        writer.beginObject();
        writer.member("status", "ok");
        writer.member("sample", SampleName);
        writeCounters(writer, counters);
        writer.member("applicationShutdowns", counters.applicationShutdowns);
        writer.member("engineHostDestroyed", true);
        writer.endObject();
    }
    std::cout << '\n';
    return 0;
}

} // namespace

int runShaderMaterials2dSample(int argumentCount, char** arguments)
{
    try
    {
        return runSample(argumentCount, arguments);
    } catch (const std::bad_alloc&)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::OutOfMemory,
                                "The 2D shader materials sample ran out of memory"};
        writeError(error);
        return 1;
    } catch (const std::exception& exception)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "An exception crossed the 2D shader materials sample boundary"};
        error.addContext("main", exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    } catch (...)
    {
        Tina::Core::Error error{
            Tina::Core::CoreErrorCode::Internal,
            "A non-standard exception crossed the 2D shader materials sample boundary"};
        writeError(error);
        return 1;
    }
}
