// First consumer of the custom Sprite2D fragment shader path.
//
// Shape of the thing:
//   - CMake cooks fs_pulse.sc into a Shader payload and stages it beside the executable.
//   - onEnter reads that payload, wraps it in a cooked Shader asset in memory, and uploads it
//     through Asset::uploadShaderFromCooked, which is the mapping from the AssetFormat shader
//     vocabulary to the Render one.
//   - The shader gets one device binding key; the author uniform values get a second, independent
//     one, because one program drawn with two value sets is what a material is.
//   - onEnter also uploads a 2x2 Linear Point/Clamp texture and interns the device-allocated
//     binding key. An unbound Texture2D key resolves to a 1x1 white, which cannot prove sampling.
//   - Extraction interns texture, shader and uniforms as frame resources. A sprite naming a
//     shader batches separately from one that does not. The last sprite names no shader so the
//     same frame carries both fragment stages; its four quadrant colours are the sampling/UV
//     evidence, because the custom fragment's pulse would let a wrong-UV draw still produce a
//     large RGB delta.
//
// The payload is wrapped rather than cooked into a catalog on purpose: a catalog would add a
// package, a manifest and a load plan to a sample whose subject is the shader path. The cooked
// header still has to exist, since uploadShaderFromCooked consumes a CookedAssetFile.
//
// Uniform values are re-published from updateFrame rather than written once, because bgfx uniforms
// are draw-local: the device re-issues every author uniform before each batch, and a value the
// caller never updated would simply stay at its last published number.

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
#include <cstring>
#include <exception>
#include <iostream>
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

constexpr std::string_view SampleName = "tina_sample_2d_custom_shader";
constexpr std::string_view PayloadFileName = "fs_pulse.shaderpayload";
constexpr std::string_view PulseUniformName = "u_pulse";

constexpr u64 DefaultFrameCount = 300;
constexpr u32 SpriteCount = 4;
// Shader program and author uniforms live in independent binding-key namespaces, so both may be 1.
// The texture key is allocated by the device rather than hard-coded: createTexture2DBinding takes
// the next key from that namespace, so this cannot collide with a key the engine already handed out.
constexpr u32 ShaderBindingKey = 1;
constexpr u32 ShaderUniformBindingKey = 1;

// 2x2 RGBA8, one saturated colour per texel. Generated in code rather than cooked because the
// subject here is the shader path, and a file would add an asset pipeline to the evidence chain.
// Row 0 is the top half of a Sprite2D quad (V grows downward in the bytes). Channels are 0 or 255
// so a point-sampled quadrant centre is an unambiguous primary colour -- an unbound Texture2D key
// resolves to the device's 1x1 white, which would make every quadrant the same and fail closed.
constexpr u16 CheckerTextureExtent = 2;
constexpr std::array<u8, 16> CheckerTextureTexels{
    255, 0,   0,   255, /**/ 0,   255, 0,   255, // row 0: red, green
    0,   0,   255, 255, /**/ 255, 255, 255, 255, // row 1: blue, white
};
constexpr u32 CheckerChannelTolerance = 24;

constexpr float PulseFrequencyHz = 0.45F;
constexpr float PulseRingWidthMeters = 0.55F;

// The two phases the pixel evidence pins u_pulse.x to. Chosen so the breathe term differs by a
// large margin: sin(0) = 0 against sin(pi/2) = 1 at the same frequency, which is 0.65 vs 1.00 on
// the sampled colour. A pair one frame apart would differ by well under a quantisation step.
constexpr float EvidencePhaseA = 0.0F;
constexpr float EvidencePhaseB = 1.0F / (PulseFrequencyHz * 4.0F);
// Both captures must be armed after the first frames have settled, so the pair differs by the
// uniform alone rather than by startup state.
constexpr u64 EvidenceFirstFrame = 8;
// Below which a channel delta is indistinguishable from backend dithering. The predicted delta on
// the breathe term alone is 0.35 of the sampled colour, so this is two orders of magnitude below
// the smallest real effect and still above a rounding wobble.
constexpr u32 EvidenceMinimumChannelDelta = 8;
// Frames the arm/collect sequence needs after EvidenceFirstFrame, with slack. A run shorter than
// this cannot produce evidence, so not producing it is not treated as a failure.
constexpr u64 EvidenceRequiredFrames = EvidenceFirstFrame + 8;

// Any non-zero AssetId works: nothing here consults a catalog, and uploadShaderFromCooked only
// reads the payload behind the header.
constexpr std::string_view ShaderAssetIdText = "7a9c1d3e5b8f4062a1c7e93d20b64f18";

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
    bool textureRetired = false;
    // Pixel evidence. customSpriteDelta is the mean absolute RGB difference over the custom-shader
    // sprite's screen region between the two pinned phases; engineSpriteDelta is the same measure
    // over the sprite that names no shader. A working custom fragment stage moves the first and
    // leaves the second at zero -- which is what separates "the shader ran" from "the whole frame
    // changed for some other reason".
    u32 customSpriteDelta = 0;
    u32 engineSpriteDelta = 0;
    // Sampling/UV evidence, read from the engine control sprite so the custom fragment's pulse
    // cannot satisfy it. Each mean is 0-255 over an inset quadrant; matched requires each primary
    // near 255 and the others near 0 (white bottom-right near 255 on all three).
    u32 checkerTopLeftR = 0;
    u32 checkerTopLeftG = 0;
    u32 checkerTopLeftB = 0;
    u32 checkerTopRightR = 0;
    u32 checkerTopRightG = 0;
    u32 checkerTopRightB = 0;
    u32 checkerBottomLeftR = 0;
    u32 checkerBottomLeftG = 0;
    u32 checkerBottomLeftB = 0;
    u32 checkerBottomRightR = 0;
    u32 checkerBottomRightG = 0;
    u32 checkerBottomRightB = 0;
    bool checkerSamplingMatched = false;
    bool evidenceCollected = false;
    std::optional<Tina::Core::Error> evidenceError{};
};

struct SpriteSpec final {
    float centerX = 0.0F;
    float centerY = 0.0F;
    u8 red = 255;
    u8 green = 255;
    u8 blue = 255;
    bool custom = true;
};

constexpr float SpriteExtentMeters = 2.4F;
constexpr float CameraWorldWidthMeters = 16.0F;
constexpr float CameraWorldHeightMeters = 9.0F;

// The last sprite deliberately names no shader, so one frame carries both the engine fragment stage
// and the custom one -- and so the pixel evidence has a control region that must NOT change.
constexpr std::array<SpriteSpec, SpriteCount> Sprites{{
    {.centerX = -4.2F, .centerY = 0.0F, .red = 255, .green = 96, .blue = 96},
    {.centerX = -1.4F, .centerY = 0.0F, .red = 120, .green = 220, .blue = 140},
    {.centerX = 1.4F, .centerY = 0.0F, .red = 140, .green = 170, .blue = 255},
    {.centerX = 4.2F, .centerY = 0.0F, .red = 255, .green = 255, .blue = 255, .custom = false},
}};

// Screen-space box for one sprite, inset to the middle half so a pixel-snap or filtering difference
// at the quad edge cannot contribute to the measured delta.
struct ScreenBox final {
    u32 minimumX = 0;
    u32 minimumY = 0;
    u32 maximumX = 0;
    u32 maximumY = 0;
};

[[nodiscard]] ScreenBox spriteScreenBox(const SpriteSpec& spec, u32 width, u32 height) noexcept
{
    const float halfExtent = SpriteExtentMeters * 0.25F;
    const float pixelsPerMeterX = static_cast<float>(width) / CameraWorldWidthMeters;
    const float pixelsPerMeterY = static_cast<float>(height) / CameraWorldHeightMeters;
    const float centerPixelX = (spec.centerX + CameraWorldWidthMeters * 0.5F) * pixelsPerMeterX;
    // Screen Y grows downward while world Y grows upward, hence the subtraction.
    const float centerPixelY = (CameraWorldHeightMeters * 0.5F - spec.centerY) * pixelsPerMeterY;
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

// Mean absolute RGB delta between two captures over one axis-aligned region, in 0-255 units.
// Alpha is skipped: the fragment shader writes premultiplied colour and leaves alpha at the
// sampled value, so a colour-only change is exactly what this must detect.
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

struct MeanRgb final {
    u32 red = 0;
    u32 green = 0;
    u32 blue = 0;
    bool valid = false;
};

// Mean RGB over an axis-aligned region, in 0-255 units. Alpha is skipped: both fragment stages
// write premultiplied colour, and the sampling proof is the authored texel colour, not coverage.
[[nodiscard]] MeanRgb meanRegionRgb(const Tina::Render::Rgba8FrameCapture& capture, u32 minimumX,
                                    u32 minimumY, u32 maximumX, u32 maximumY) noexcept
{
    if (capture.empty() || maximumX > capture.width || maximumY > capture.height ||
        minimumX >= maximumX || minimumY >= maximumY)
    {
        return {};
    }

    u64 red = 0;
    u64 green = 0;
    u64 blue = 0;
    u64 samples = 0;
    for (u32 y = minimumY; y < maximumY; ++y)
    {
        for (u32 x = minimumX; x < maximumX; ++x)
        {
            const usize base = (static_cast<usize>(y) * capture.width + x) * 4U;
            red += static_cast<u64>(std::to_integer<int>(capture.rgba8Pixels[base + 0U]));
            green += static_cast<u64>(std::to_integer<int>(capture.rgba8Pixels[base + 1U]));
            blue += static_cast<u64>(std::to_integer<int>(capture.rgba8Pixels[base + 2U]));
            ++samples;
        }
    }
    if (samples == 0)
    {
        return {};
    }
    return MeanRgb{
        .red = static_cast<u32>(red / samples),
        .green = static_cast<u32>(green / samples),
        .blue = static_cast<u32>(blue / samples),
        .valid = true,
    };
}

[[nodiscard]] bool channelNear(u32 actual, u32 expected, u32 tolerance) noexcept
{
    const u32 delta = actual > expected ? actual - expected : expected - actual;
    return delta <= tolerance;
}

[[nodiscard]] bool primaryQuadrantMatched(const MeanRgb& rgb, u32 primaryChannel) noexcept
{
    if (!rgb.valid)
    {
        return false;
    }
    const u32 channels[3]{rgb.red, rgb.green, rgb.blue};
    for (u32 channel = 0; channel < 3U; ++channel)
    {
        const u32 expected = channel == primaryChannel ? 255U : 0U;
        if (!channelNear(channels[channel], expected, CheckerChannelTolerance))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool whiteQuadrantMatched(const MeanRgb& rgb) noexcept
{
    return rgb.valid && channelNear(rgb.red, 255U, CheckerChannelTolerance) &&
           channelNear(rgb.green, 255U, CheckerChannelTolerance) &&
           channelNear(rgb.blue, 255U, CheckerChannelTolerance);
}

// Four inset quadrant centres of one sprite. Inset by a quarter of each half so a pixel-snap or
// bilinear bleed at the texel boundary cannot satisfy or refute the primary-colour criterion.
[[nodiscard]] std::array<ScreenBox, 4> spriteQuadrantBoxes(const ScreenBox& box) noexcept
{
    const u32 width = box.maximumX - box.minimumX;
    const u32 height = box.maximumY - box.minimumY;
    const u32 halfX = width / 2U;
    const u32 halfY = height / 2U;
    const u32 insetX = (std::max)(halfX / 4U, 1U);
    const u32 insetY = (std::max)(halfY / 4U, 1U);
    const auto make = [&](u32 originX, u32 originY, u32 extentX, u32 extentY) noexcept {
        const u32 innerMinimumX = originX + insetX;
        const u32 innerMinimumY = originY + insetY;
        const u32 innerMaximumX = originX + extentX - insetX;
        const u32 innerMaximumY = originY + extentY - insetY;
        return ScreenBox{
            .minimumX = innerMinimumX,
            .minimumY = innerMinimumY,
            .maximumX = innerMaximumX > innerMinimumX ? innerMaximumX : innerMinimumX,
            .maximumY = innerMaximumY > innerMinimumY ? innerMaximumY : innerMinimumY,
        };
    };
    return {
        make(box.minimumX, box.minimumY, halfX, halfY),
        make(box.minimumX + halfX, box.minimumY, width - halfX, halfY),
        make(box.minimumX, box.minimumY + halfY, halfX, height - halfY),
        make(box.minimumX + halfX, box.minimumY + halfY, width - halfX, height - halfY),
    };
}

// Reaching the live device: CreateEngineOptions::wrapWindowSurfaceRenderDevice is the only hook
// that sees it. Identity capture rather than a forwarding wrapper, because every optional
// IRenderDevice entry point has a fail-closed base implementation, so a wrapper that forgot to
// forward one would turn into a runtime upload failure instead of a compile error.
class DeviceCapture final {
  public:
    void set(Tina::Render::IRenderDevice* device) noexcept { device_ = device; }
    [[nodiscard]] Tina::Render::IRenderDevice* get() const noexcept { return device_; }

  private:
    Tina::Render::IRenderDevice* device_ = nullptr;
};

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

// Turns the cooked payload file into the CookedAssetFile that uploadShaderFromCooked consumes.
[[nodiscard]] Tina::Core::Result<Tina::Asset::CookedAssetFile> loadShaderAsset(u32& payloadBytes)
{
    auto payloadPath = Tina::Core::applicationFilePath(PayloadFileName);
    if (!payloadPath)
    {
        return Tina::Core::failure(std::move(payloadPath.error())
                                       .withContext("loadShaderAsset", "applicationFilePath"));
    }

    // memoryResource is required, not optional: ReadFileConfig's default leaves it null and
    // readFile rejects that as an invalid config rather than falling back to the default resource.
    auto payload = Tina::Core::readFile(
        *payloadPath, Tina::Core::ReadFileConfig{.memoryResource = std::pmr::get_default_resource()});
    if (!payload)
    {
        return Tina::Core::failure(std::move(payload.error())
                                       .withContext("loadShaderAsset", *payloadPath));
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

    // assetTypeVersion must be the Shader schema version, not the default 1-by-omission: a
    // producer that omits it cooks an asset the loader rejects after any schema bump.
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

[[nodiscard]] Tina::Render::GpuShaderUniformValue pulseUniform(float seconds) noexcept
{
    Tina::Render::GpuShaderUniformValue entry{};
    const usize length =
        (std::min)(PulseUniformName.size(),
                   static_cast<usize>(Tina::Render::GpuShaderUniformValue::MaximumNameBytes));
    std::copy_n(PulseUniformName.begin(), length, entry.name.begin());
    entry.value = {seconds, PulseFrequencyHz, PulseRingWidthMeters, 0.0F};
    return entry;
}

// Frame-resource owner for the two custom-shader bindings. Separate from
// SampleSpriteFrameResource because that one is hard-wired to FrameResourceKind::Texture2D, and a
// draw that swapped Shader for ShaderUniforms must fail closed at resolve rather than bind a
// program as a material.
class ShaderFrameResource final {
  public:
    ShaderFrameResource() noexcept = default;
    ShaderFrameResource(const ShaderFrameResource&) = delete;
    ShaderFrameResource& operator=(const ShaderFrameResource&) = delete;

    ~ShaderFrameResource() noexcept
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
            const_cast<ShaderFrameResource*>(this),
            &ShaderFrameResource::releaseBorrow,
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
        auto* owner = static_cast<ShaderFrameResource*>(userData);
        if (owner == nullptr || owner->borrowCount_ == 0)
        {
            std::terminate();
        }
        --owner->borrowCount_;
    }

    mutable u32 borrowCount_ = 0;
};

class CustomShaderState final : public Tina::IGameState {
  public:
    CustomShaderState(SampleOptions options, LifecycleCounters& counters,
                      const DeviceCapture& capture) noexcept
        : options_(options), counters_(&counters), capture_(&capture)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext&) override
    {
        ++counters_->stateEnters;

        Tina::Render::IRenderDevice* device = capture_->get();
        if (device == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "sample did not capture a render device");
        }

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

        auto shader = Tina::Asset::uploadShaderFromCooked(*device, *shaderAsset);
        if (!shader)
        {
            return Tina::Core::failure(std::move(shader.error()));
        }
        shader_ = *shader;

        if (auto status = device->setShaderBinding(ShaderBindingKey, shader_); !status)
        {
            return status;
        }
        shaderBound_ = true;

        if (auto status = uploadTexture(*device); !status)
        {
            return status;
        }

        // Published here as well as per frame so the first extraction names a binding that already
        // carries values: a draw resolving an empty uniform binding fails closed.
        return publishUniforms(*device, 0.0F);
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        ++counters_->stateExits;
        Tina::Render::IRenderDevice* device = capture_->get();
        if (device == nullptr)
        {
            return;
        }

        // Clearing the value binding first: retireShader clears bindings that reference the
        // program, but the uniform binding is keyed independently and is not one of them.
        static_cast<void>(device->setShaderUniformBinding(ShaderUniformBindingKey,
                                                         Tina::Render::GpuShaderUniformBindingDesc{}));
        if (shaderBound_)
        {
            static_cast<void>(device->setShaderBinding(ShaderBindingKey, Tina::Render::GpuShaderId{}));
            shaderBound_ = false;
        }
        if (shader_)
        {
            Tina::Render::FramePin completion{};
            if (device->retireShader(shader_, completion))
            {
                counters_->shaderRetired = true;
            }
            shader_ = Tina::Render::GpuShaderId{};
        }
        if (textureBound_)
        {
            static_cast<void>(device->setTexture2DBinding(textureBindingKey_,
                                                          Tina::Render::GpuTextureId{}));
            textureBound_ = false;
        }
        if (texture_)
        {
            Tina::Render::FramePin completion{};
            if (device->retireTexture2D(texture_, completion))
            {
                counters_->textureRetired = true;
            }
            texture_ = Tina::Render::GpuTextureId{};
        }
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_->frameUpdates;
        elapsedSeconds_ += static_cast<float>(context.frameTiming().updateDelta.count());

        Tina::Render::IRenderDevice* device = capture_->get();
        if (device == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "render device disappeared mid-run");
        }
        // The evidence phase overrides the animated value, so the two captured frames differ by
        // u_pulse.x alone. Everything else about the scene is frame-independent already.
        const float publishedSeconds =
            evidencePhase_.has_value() ? *evidencePhase_ : elapsedSeconds_;
        if (auto status = publishUniforms(*device, publishedSeconds); !status)
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

        // Ambient 1 so the engine control sprite's sampled texels reach the backbuffer as authored
        // bytes. The default 0.2 would scale 255 to ~51 and make the quadrant criterion fail even
        // when sampling is correct. Custom sprites do not consume this lighting -- fs_pulse.sc
        // multiplies the sampled texel by vertex colour and the pulse, not by u_spriteLightParams.
        if (auto status = writer.setSprite2DLighting({.ambientScale = 1.0F}); !status)
        {
            return status;
        }

        auto& sink = context.frameResourceSink();
        auto texture = textureResource_.intern(sink, textureBindingKey_);
        if (!texture)
        {
            return Tina::Core::failure(std::move(texture.error()));
        }
        auto shader = shaderResource_.intern(sink, Tina::Render::FrameResourceKind::Shader,
                                             ShaderBindingKey);
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

        for (u32 index = 0; index < SpriteCount; ++index)
        {
            const SpriteSpec& spec = Sprites[index];
            const Tina::Render::RenderSprite2DInput sprite{
                .texture = *texture,
                .shader = spec.custom ? *shader : Tina::Render::FrameResourceRef{},
                .shaderUniforms = spec.custom ? *uniforms : Tina::Render::FrameResourceRef{},
                .stableEntityKey = static_cast<u64>(index) + 1U,
                .centerX = spec.centerX,
                .centerY = spec.centerY,
                .rotationRadians = 0.0F,
                .widthMeters = SpriteExtentMeters,
                .heightMeters = SpriteExtentMeters,
                .orderInLayer = static_cast<Tina::Core::i32>(index),
                .red = spec.red,
                .green = spec.green,
                .blue = spec.blue,
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
    // Arm/collect over four frames: pin phase A and arm, collect A and pin B and arm, collect B and
    // measure. The pin has to land one frame before the arm because a capture belongs to the *next*
    // presented frame, so arming in the same call that changed the uniform would read the previous
    // frame's pixels -- which looks exactly like a shader that ignored the uniform.
    //
    // A capture failure abandons the sequence rather than failing the phase: this is diagnostic
    // instrumentation, and a backend that declines read-back is not a reason to stop drawing.
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

        // The first capture of a run comes back blank on some hosts, which would make the control
        // region differ too and turn a working shader into a failure. One discarded capture removes
        // that from the measurement entirely.
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
            const ScreenBox customBox =
                spriteScreenBox(Sprites[0], captureB.width, captureB.height);
            const ScreenBox engineBox =
                spriteScreenBox(Sprites[SpriteCount - 1U], captureB.width, captureB.height);
            counters_->customSpriteDelta =
                meanRegionDelta(captureA_, captureB, customBox.minimumX, customBox.minimumY,
                                customBox.maximumX, customBox.maximumY);
            counters_->engineSpriteDelta =
                meanRegionDelta(captureA_, captureB, engineBox.minimumX, engineBox.minimumY,
                                engineBox.maximumX, engineBox.maximumY);

            // Sampling/UV proof is on the engine control sprite: pulse/breathe would let a
            // wrong-UV custom fragment still produce a large RGB delta. Capture A is the source
            // because both phases must agree on the control region (engineSpriteDelta == 0).
            const std::array<ScreenBox, 4> quadrants = spriteQuadrantBoxes(engineBox);
            const MeanRgb topLeft =
                meanRegionRgb(captureA_, quadrants[0].minimumX, quadrants[0].minimumY,
                              quadrants[0].maximumX, quadrants[0].maximumY);
            const MeanRgb topRight =
                meanRegionRgb(captureA_, quadrants[1].minimumX, quadrants[1].minimumY,
                              quadrants[1].maximumX, quadrants[1].maximumY);
            const MeanRgb bottomLeft =
                meanRegionRgb(captureA_, quadrants[2].minimumX, quadrants[2].minimumY,
                              quadrants[2].maximumX, quadrants[2].maximumY);
            const MeanRgb bottomRight =
                meanRegionRgb(captureA_, quadrants[3].minimumX, quadrants[3].minimumY,
                              quadrants[3].maximumX, quadrants[3].maximumY);
            counters_->checkerTopLeftR = topLeft.red;
            counters_->checkerTopLeftG = topLeft.green;
            counters_->checkerTopLeftB = topLeft.blue;
            counters_->checkerTopRightR = topRight.red;
            counters_->checkerTopRightG = topRight.green;
            counters_->checkerTopRightB = topRight.blue;
            counters_->checkerBottomLeftR = bottomLeft.red;
            counters_->checkerBottomLeftG = bottomLeft.green;
            counters_->checkerBottomLeftB = bottomLeft.blue;
            counters_->checkerBottomRightR = bottomRight.red;
            counters_->checkerBottomRightG = bottomRight.green;
            counters_->checkerBottomRightB = bottomRight.blue;
            counters_->checkerSamplingMatched =
                primaryQuadrantMatched(topLeft, 0U) && primaryQuadrantMatched(topRight, 1U) &&
                primaryQuadrantMatched(bottomLeft, 2U) && whiteQuadrantMatched(bottomRight);

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

    Tina::Core::Status publishUniforms(Tina::Render::IRenderDevice& device, float seconds) noexcept
    {
        const std::array values{pulseUniform(seconds)};
        if (auto status = device.setShaderUniformBinding(
                ShaderUniformBindingKey,
                Tina::Render::GpuShaderUniformBindingDesc{.values = values});
            !status)
        {
            return status;
        }
        ++counters_->uniformPublishes;
        return Tina::Core::success();
    }

    // Sampler is Point/Clamp: Linear filtering across a 2x2 would blur the four texels into each
    // other, and the inset-quadrant centres would no longer be a saturated primary. Linear colour
    // space, not Srgb: an Srgb texture is gamma-decoded on read, so the sampled values would no
    // longer be the authored bytes.
    Tina::Core::Status uploadTexture(Tina::Render::IRenderDevice& device) noexcept
    {
        std::memcpy(texturePixels_.data(), CheckerTextureTexels.data(), texturePixels_.size());
        const std::array<Tina::Render::Texture2DUploadLevel, 1> levels{{
            {.width = CheckerTextureExtent,
             .height = CheckerTextureExtent,
             .bytes = std::span{texturePixels_}},
        }};
        auto uploaded = device.createTexture2D(Tina::Render::Texture2DUploadDesc{
            .format = Tina::Render::GpuTextureFormat::Rgba8Unorm,
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

        auto bindingKey = device.createTexture2DBinding(texture_);
        if (!bindingKey)
        {
            return Tina::Core::failure(std::move(bindingKey.error()));
        }
        textureBindingKey_ = *bindingKey;
        textureBound_ = true;
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
    const DeviceCapture* capture_ = nullptr;
    Tina::Render::GpuShaderId shader_{};
    Tina::Render::GpuTextureId texture_{};
    std::array<std::byte, CheckerTextureTexels.size()> texturePixels_{};
    u32 textureBindingKey_ = 0;
    bool shaderBound_ = false;
    bool textureBound_ = false;
    float elapsedSeconds_ = 0.0F;
    EvidenceStage stage_ = EvidenceStage::Idle;
    // Set means "hold u_pulse.x at this value instead of the animated one".
    std::optional<float> evidencePhase_{};
    Tina::Render::Rgba8FrameCapture captureA_{};
    mutable Tina::Samples::SampleSpriteFrameResource textureResource_{};
    mutable ShaderFrameResource shaderResource_{};
    mutable ShaderFrameResource uniformResource_{};
};

class CustomShaderApplication final : public Tina::IGameApplication {
  public:
    CustomShaderApplication(SampleOptions options, LifecycleCounters& counters,
                            const DeviceCapture& capture) noexcept
        : options_(options), counters_(&counters), capture_(&capture)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>>
    createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{
            std::make_unique<CustomShaderState>(options_, *counters_, *capture_)};
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
    config.applicationName = "Tina vNext 2D Custom Shader";
    config.primaryWindow.title = "Tina vNext - Sprite2D custom fragment shader";
    config.primaryWindow.initialLogicalExtent = {1280, 720};
    config.primaryWindow.initiallyVisible = true;
    config.renderSceneCapacities.spriteCapacity = 16;
    return config;
}

// Pixel evidence is required when it was obtainable and judged strictly when it was obtained, but a
// backend that declines frame read-back is a host capability rather than a statement about the
// fragment stage -- so a capture error is surfaced in the output instead of failing the run.
[[nodiscard]] bool evidenceAccepted(const LifecycleCounters& counters, u64 targetFrameCount) noexcept
{
    if (counters.evidenceCollected)
    {
        return counters.customSpriteDelta >= EvidenceMinimumChannelDelta &&
               counters.engineSpriteDelta == 0 && counters.checkerSamplingMatched;
    }
    if (counters.evidenceError.has_value())
    {
        return counters.evidenceError->code.domain == Tina::Core::ErrorDomain::Render;
    }
    // No error and no result: only legitimate if the run was too short to finish the sequence.
    return targetFrameCount < EvidenceRequiredFrames;
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
    CustomShaderApplication application{options, counters, capture};
    auto runResult = (*hostResult)->run(application);
    hostResult->reset();
    if (!runResult)
    {
        writeError(runResult.error());
        return 1;
    }

    // uniformPublishes counts the onEnter publish plus one per frame, so the whole run is covered:
    // a device that accepted the upload but never re-published values would show a smaller count.
    if (*runResult != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame ||
        counters.frameUpdates != options.targetFrameCount ||
        counters.renderExtractions != options.targetFrameCount ||
        counters.uniformPublishes != options.targetFrameCount + 1U || counters.stateEnters != 1 ||
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
            writer.member("frames", counters.frameUpdates);
            writer.member("renderExtractions", counters.renderExtractions);
            writer.member("uniformPublishes", counters.uniformPublishes);
            writer.member("shaderBlobCount", counters.shaderBlobCount);
            writer.member("shaderRetired", counters.shaderRetired);
            writer.member("textureRetired", counters.textureRetired);
            writer.member("evidenceCollected", counters.evidenceCollected);
            writer.member("customSpriteDelta", counters.customSpriteDelta);
            writer.member("engineSpriteDelta", counters.engineSpriteDelta);
            writer.member("checkerSamplingMatched", counters.checkerSamplingMatched);
            writer.member("checkerTopLeftR", counters.checkerTopLeftR);
            writer.member("checkerTopLeftG", counters.checkerTopLeftG);
            writer.member("checkerTopLeftB", counters.checkerTopLeftB);
            writer.member("checkerTopRightR", counters.checkerTopRightR);
            writer.member("checkerTopRightG", counters.checkerTopRightG);
            writer.member("checkerTopRightB", counters.checkerTopRightB);
            writer.member("checkerBottomLeftR", counters.checkerBottomLeftR);
            writer.member("checkerBottomLeftG", counters.checkerBottomLeftG);
            writer.member("checkerBottomLeftB", counters.checkerBottomLeftB);
            writer.member("checkerBottomRightR", counters.checkerBottomRightR);
            writer.member("checkerBottomRightG", counters.checkerBottomRightG);
            writer.member("checkerBottomRightB", counters.checkerBottomRightB);
            writer.member("evidenceError", counters.evidenceError.has_value()
                                               ? errorCodeName(counters.evidenceError->code)
                                               : std::string{});
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
        writer.member("frames", counters.frameUpdates);
        writer.member("spritesPerFrame", SpriteCount);
        writer.member("customShaderSprites", SpriteCount - 1U);
        writer.member("shaderPayloadBytes", counters.shaderPayloadBytes);
        writer.member("shaderBlobCount", counters.shaderBlobCount);
        writer.member("uniformPublishes", counters.uniformPublishes);
        writer.member("shaderRetired", counters.shaderRetired);
        writer.member("textureRetired", counters.textureRetired);
        writer.member("evidenceCollected", counters.evidenceCollected);
        writer.member("customSpriteDelta", counters.customSpriteDelta);
        writer.member("engineSpriteDelta", counters.engineSpriteDelta);
        writer.member("checkerSamplingMatched", counters.checkerSamplingMatched);
        writer.member("checkerTopLeftR", counters.checkerTopLeftR);
        writer.member("checkerTopLeftG", counters.checkerTopLeftG);
        writer.member("checkerTopLeftB", counters.checkerTopLeftB);
        writer.member("checkerTopRightR", counters.checkerTopRightR);
        writer.member("checkerTopRightG", counters.checkerTopRightG);
        writer.member("checkerTopRightB", counters.checkerTopRightB);
        writer.member("checkerBottomLeftR", counters.checkerBottomLeftR);
        writer.member("checkerBottomLeftG", counters.checkerBottomLeftG);
        writer.member("checkerBottomLeftB", counters.checkerBottomLeftB);
        writer.member("checkerBottomRightR", counters.checkerBottomRightR);
        writer.member("checkerBottomRightG", counters.checkerBottomRightG);
        writer.member("checkerBottomRightB", counters.checkerBottomRightB);
        writer.member("evidenceError", counters.evidenceError.has_value()
                                           ? errorCodeName(counters.evidenceError->code)
                                           : std::string{});
        writer.member("applicationShutdowns", counters.applicationShutdowns);
        writer.member("engineHostDestroyed", true);
        writer.endObject();
    }
    std::cout << '\n';
    return 0;
}

} // namespace

int runCustomShader2dSample(int argumentCount, char** arguments)
{
    try
    {
        return runSample(argumentCount, arguments);
    } catch (const std::bad_alloc&)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::OutOfMemory,
                                "The 2D custom shader sample ran out of memory"};
        writeError(error);
        return 1;
    } catch (const std::exception& exception)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "An exception crossed the 2D custom shader sample boundary"};
        error.addContext("main", exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    } catch (...)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "A non-standard exception crossed the 2D custom shader sample boundary"};
        writeError(error);
        return 1;
    }
}
