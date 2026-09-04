// Custom Sprite2D fragment that consumes the engine lighting/normal contract.
//
// samples/2d_custom_shader proved cook/upload/bind and a pulse uniform.
// samples/2d_shader_materials proved one program with three materials and a real albedo.
// This sample covers what those two left unproven: a custom fragment that *reads*
// s_normalTex, u_spriteLight* and u_spriteShadowSegments rather than replacing them.
//
// Six sprites, interleaved so consecutive draws never share a (shader, normal) pair:
//   custom + normal, custom + flat, engine default, then the same three again.
// One point light above and to the left, one vertical shadow segment through the first
// custom+normal sprite. Pixel evidence is intra-frame: a whole-frame brightness change
// cannot satisfy it.

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
#include <tina/render/FrameResource.hpp>
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
#include <vector>

namespace {

using Tina::Core::u16;
using Tina::Core::u32;
using Tina::Core::u64;
using Tina::Core::u8;
using Tina::Core::usize;

constexpr std::string_view SampleName = "tina_sample_2d_shader_lighting";
constexpr std::string_view PayloadFileName = "fs_lit.shaderpayload";
constexpr u64 DefaultFrameCount = 300;
constexpr std::string_view ShaderAssetIdText = "9c4e2a71b8d6035f1a2b3c4d5e6f7081";

constexpr u32 ShaderBindingKey = 1;
constexpr u32 SpriteCount = 6;

constexpr float CameraWorldWidthMeters = 16.0F;
constexpr float CameraWorldHeightMeters = 9.0F;
constexpr float SpriteExtentMeters = 2.2F;

constexpr u64 EvidenceFirstFrame = 8;
constexpr u64 EvidenceRequiredFrames = EvidenceFirstFrame + 6;

// The Lambert term is n.z + dot(n.xy, offset/radius), so a full-scale XY normal
// moves the factor by about half a unit on the away-facing half and almost nothing
// on the facing half -- single-digit luminance after attenuation, not the large
// fraction first assumed.
//
// normalVsFlatSeparation is reported for diagnosis only, never asserted: forcing
// normalMapEnabled = false still measures 3 there, because the two sprites sit at
// different Y and mean-separation picks up their own falloff difference. A threshold
// at or below its own negative-control reading would pass with the normal map dead.
//
// normalLeftVsRight is the criterion that carries the proof. It subtracts the flat
// partner's identical falloff bias, so the same control drives it to a clean 0
// against 10 with the normal map live; 6 sits between the two.
constexpr u32 EvidenceMinimumLeftVsRight = 6;
// Hard shadow vs the mirrored unoccluded half at the same |x|: ambient-only (15)
// against ambient+attenuation (57-63). Removing the lighting loop entirely drops
// this to 31 (albedo alone), so 40 is the level that separates a live shadow from
// that residual.
constexpr u32 EvidenceMinimumShadowedVsLit = 40;
// Four-quadrant albedo still has to be visible on the engine-default sprite. A flat capture
// (blank first frame, or a shader that ignored the sampler) would sit near 0.
constexpr u32 EvidenceMinimumEngineSpread = 40;

constexpr u16 TextureExtent = 4;
using TexturePixels = std::array<std::byte, static_cast<usize>(TextureExtent) * TextureExtent * 4U>;

enum class SpriteKind : u8 {
    CustomNormal = 0,
    CustomFlat = 1,
    EngineDefault = 2,
};

struct SpriteSpec final {
    float centerX = 0.0F;
    float centerY = 0.0F;
    SpriteKind kind = SpriteKind::EngineDefault;
};

// Two rows, mirrored about the origin. The light sits at (0,0), so a sprite at (+X,+Y)
// and its partner at (+X,-Y) are the same distance from the light; a pair at (-X,+Y)
// and (+X,+Y) are the same distance too. Interleaved by kind so consecutive sprites
// never share (shader, normalTexture).
// 2.6 rather than 4.0: the Lambert term is n.z + dot(n.xy, offset/radius), so the
// normal map's contribution scales with how far the fragment sits from the light
// relative to the light radius. Halving the radius below doubles that ratio.
constexpr float PairX = 2.6F;
constexpr float PairY = 1.3F;
constexpr std::array<SpriteSpec, SpriteCount> Sprites{{
    {.centerX = -PairX, .centerY = PairY, .kind = SpriteKind::CustomNormal},
    {.centerX = -PairX, .centerY = -PairY, .kind = SpriteKind::CustomFlat},
    {.centerX = 0.0F, .centerY = PairY, .kind = SpriteKind::EngineDefault},
    {.centerX = PairX, .centerY = PairY, .kind = SpriteKind::CustomNormal},
    {.centerX = PairX, .centerY = -PairY, .kind = SpriteKind::CustomFlat},
    {.centerX = 0.0F, .centerY = -PairY, .kind = SpriteKind::EngineDefault},
}};

constexpr float LightPositionX = 0.0F;
constexpr float LightPositionY = 0.0F;
// 6m balances the two criteria: offset/radius scales the normal map's Lambert
// contribution (favouring a tight radius) while 1 - distance/radius is what the
// shadow criterion measures against ambient (favouring a wide one). At 4m the
// unoccluded mirror half fell to 29 and the shadow delta collapsed to 14.
constexpr float LightRadiusMeters = 6.0F;
// 1.2 keeps ambient+attenuation under 1.0 at these distances, so a Lambert
// difference is visible in 8-bit capture instead of clipping both sides to white.
constexpr float LightIntensity = 1.2F;
constexpr float AmbientScale = 0.15F;
// Vertical occluder through the top-left custom+normal sprite only. Hard shadow.
constexpr float ShadowStartX = -PairX;
constexpr float ShadowStartY = PairY + SpriteExtentMeters * 0.5F;
constexpr float ShadowEndX = -PairX;
constexpr float ShadowEndY = PairY - SpriteExtentMeters * 0.5F;

[[nodiscard]] TexturePixels makeAlbedoTexture() noexcept
{
    // Four distinct primaries, each summing to 300 so a left-half vs right-half luminance
    // comparison cannot pass from albedo alone. Linear bytes so a wrong UV is a wrong
    // primary, not a gamma-shifted near-miss.
    constexpr std::array<std::array<u8, 3>, 4> quadrants{{
        {210, 45, 45},
        {45, 210, 45},
        {45, 45, 210},
        {150, 150, 0},
    }};
    TexturePixels pixels{};
    for (u16 y = 0; y < TextureExtent; ++y)
    {
        for (u16 x = 0; x < TextureExtent; ++x)
        {
            const usize quadrant = (y < TextureExtent / 2U ? 0U : 2U) + (x < TextureExtent / 2U ? 0U : 1U);
            const usize base = (static_cast<usize>(y) * TextureExtent + x) * 4U;
            pixels[base + 0U] = static_cast<std::byte>(quadrants[quadrant][0]);
            pixels[base + 1U] = static_cast<std::byte>(quadrants[quadrant][1]);
            pixels[base + 2U] = static_cast<std::byte>(quadrants[quadrant][2]);
            pixels[base + 3U] = static_cast<std::byte>(255);
        }
    }
    return pixels;
}

[[nodiscard]] TexturePixels makeNormalTexture() noexcept
{
    // Left two columns face -X (0, 128, 255); right two face +X (255, 128, 255).
    // 0/255 rather than 64/192: the Lambert term is n.z + dot(n.xy, L/r), so a 45°
    // map that faces the light is almost identical to flat +Z. Full-scale XY is
    // what makes the away-facing half diverge.
    TexturePixels pixels{};
    for (u16 y = 0; y < TextureExtent; ++y)
    {
        for (u16 x = 0; x < TextureExtent; ++x)
        {
            const usize base = (static_cast<usize>(y) * TextureExtent + x) * 4U;
            const u8 nx = x < TextureExtent / 2U ? u8{0} : u8{255};
            pixels[base + 0U] = static_cast<std::byte>(nx);
            pixels[base + 1U] = static_cast<std::byte>(128);
            pixels[base + 2U] = static_cast<std::byte>(255);
            pixels[base + 3U] = static_cast<std::byte>(255);
        }
    }
    return pixels;
}

struct SampleOptions final {
    u64 targetFrameCount = DefaultFrameCount;
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 renderExtractions = 0;
    u64 stateEnters = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
    u32 shaderBlobCount = 0;
    u32 shaderPayloadBytes = 0;
    bool shaderRetired = false;
    bool albedoRetired = false;
    bool normalRetired = false;
    u32 normalVsFlatSeparation = 0;
    u32 normalLeftVsRight = 0;
    u32 shadowedVsLit = 0;
    u32 engineControlSpread = 0;
    u32 probeNormalLeft = 0;
    u32 probeNormalRight = 0;
    u32 probeFlatLeft = 0;
    u32 probeFlatRight = 0;
    u32 probeShadowed = 0;
    bool evidenceCollected = false;
    std::optional<Tina::Core::Error> evidenceError{};
};

class DeviceCapture final {
  public:
    void set(Tina::Render::IRenderDevice* device) noexcept { device_ = device; }
    [[nodiscard]] Tina::Render::IRenderDevice* get() const noexcept { return device_; }

  private:
    Tina::Render::IRenderDevice* device_ = nullptr;
};

struct ScreenBox final {
    u32 minimumX = 0;
    u32 minimumY = 0;
    u32 maximumX = 0;
    u32 maximumY = 0;
};

[[nodiscard]] ScreenBox makeBox(float centerX, float centerY, float halfExtentX, float halfExtentY,
                                u32 width, u32 height) noexcept
{
    const float pixelsPerMeterX = static_cast<float>(width) / CameraWorldWidthMeters;
    const float pixelsPerMeterY = static_cast<float>(height) / CameraWorldHeightMeters;
    const float centerPixelX = (centerX + CameraWorldWidthMeters * 0.5F) * pixelsPerMeterX;
    const float centerPixelY = (CameraWorldHeightMeters * 0.5F - centerY) * pixelsPerMeterY;
    const float halfPixelsX = halfExtentX * pixelsPerMeterX;
    const float halfPixelsY = halfExtentY * pixelsPerMeterY;
    const auto clampW = [width](float value) noexcept {
        return static_cast<u32>(std::clamp(value, 0.0F, static_cast<float>(width)));
    };
    const auto clampH = [height](float value) noexcept {
        return static_cast<u32>(std::clamp(value, 0.0F, static_cast<float>(height)));
    };
    return ScreenBox{
        .minimumX = clampW(centerPixelX - halfPixelsX),
        .minimumY = clampH(centerPixelY - halfPixelsY),
        .maximumX = clampW(centerPixelX + halfPixelsX),
        .maximumY = clampH(centerPixelY + halfPixelsY),
    };
}

[[nodiscard]] ScreenBox spriteInsetBox(const SpriteSpec& spec, u32 width, u32 height) noexcept
{
    return makeBox(spec.centerX, spec.centerY, SpriteExtentMeters * 0.25F,
                   SpriteExtentMeters * 0.25F, width, height);
}

[[nodiscard]] ScreenBox spriteLeftHalfBox(const SpriteSpec& spec, u32 width, u32 height) noexcept
{
    return makeBox(spec.centerX - SpriteExtentMeters * 0.25F, spec.centerY,
                   SpriteExtentMeters * 0.15F, SpriteExtentMeters * 0.25F, width, height);
}

[[nodiscard]] ScreenBox spriteRightHalfBox(const SpriteSpec& spec, u32 width, u32 height) noexcept
{
    return makeBox(spec.centerX + SpriteExtentMeters * 0.25F, spec.centerY,
                   SpriteExtentMeters * 0.15F, SpriteExtentMeters * 0.25F, width, height);
}

[[nodiscard]] bool boxIsUsable(const Tina::Render::Rgba8FrameCapture& capture, const ScreenBox& box) noexcept
{
    return !capture.empty() && box.maximumX <= capture.width && box.maximumY <= capture.height &&
           box.minimumX < box.maximumX && box.minimumY < box.maximumY;
}

struct RegionMean final {
    u32 red = 0;
    u32 green = 0;
    u32 blue = 0;
};

[[nodiscard]] RegionMean regionMean(const Tina::Render::Rgba8FrameCapture& capture, const ScreenBox& box) noexcept
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

[[nodiscard]] u32 luminance(const RegionMean& mean) noexcept
{
    return (mean.red + mean.green + mean.blue) / 3U;
}

[[nodiscard]] u32 meanSeparation(const RegionMean& left, const RegionMean& right) noexcept
{
    const auto channelDelta = [](u32 a, u32 b) noexcept { return a > b ? a - b : b - a; };
    return (channelDelta(left.red, right.red) + channelDelta(left.green, right.green) +
            channelDelta(left.blue, right.blue)) /
           3U;
}

[[nodiscard]] u32 regionSpread(const Tina::Render::Rgba8FrameCapture& capture, const ScreenBox& box) noexcept
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
        return Tina::Core::failure(std::move(payload.error()).withContext("loadShaderAsset", *payloadPath));
    }
    payloadBytes = static_cast<u32>(payload->size());

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

class ShaderLightingState final : public Tina::IGameState {
  public:
    ShaderLightingState(SampleOptions options, LifecycleCounters& counters,
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
        if (auto status = uploadTexture(*device, albedoPixels_, makeAlbedoTexture(), albedo_,
                                        albedoBindingKey_);
            !status)
        {
            return status;
        }
        if (auto status = uploadTexture(*device, normalPixels_, makeNormalTexture(), normal_,
                                        normalBindingKey_);
            !status)
        {
            return status;
        }
        return uploadShader(*device);
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        ++counters_->stateExits;
        Tina::Render::IRenderDevice* device = capture_->get();
        if (device == nullptr)
        {
            return;
        }
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
        if (albedo_)
        {
            Tina::Render::FramePin completion{};
            if (device->retireTexture2D(albedo_, completion))
            {
                counters_->albedoRetired = true;
            }
            albedo_ = Tina::Render::GpuTextureId{};
        }
        if (normal_)
        {
            Tina::Render::FramePin completion{};
            if (device->retireTexture2D(normal_, completion))
            {
                counters_->normalRetired = true;
            }
            normal_ = Tina::Render::GpuTextureId{};
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

        const std::array<Tina::Render::Sprite2DPointLight, 1> lights{{
            {.positionX = LightPositionX,
             .positionY = LightPositionY,
             .radiusMeters = LightRadiusMeters,
             .sourceRadiusMeters = 0.0F,
             .colorR = LightIntensity,
             .colorG = LightIntensity,
             .colorB = LightIntensity},
        }};
        const std::array<Tina::Render::Sprite2DShadowSegment, 1> segments{{
            {.startX = ShadowStartX, .startY = ShadowStartY, .endX = ShadowEndX, .endY = ShadowEndY},
        }};
        if (auto status = writer.setSprite2DLighting({.pointLights = lights,
                                                      .shadowSegments = segments,
                                                      .ambientScale = AmbientScale});
            !status)
        {
            return status;
        }

        auto& sink = context.frameResourceSink();
        auto albedo = albedoResource_.intern(sink, albedoBindingKey_);
        if (!albedo)
        {
            return Tina::Core::failure(std::move(albedo.error()));
        }
        auto normal = normalResource_.intern(sink, normalBindingKey_);
        if (!normal)
        {
            return Tina::Core::failure(std::move(normal.error()));
        }
        auto shader =
            shaderResource_.intern(sink, Tina::Render::FrameResourceKind::Shader, ShaderBindingKey);
        if (!shader)
        {
            return Tina::Core::failure(std::move(shader.error()));
        }

        for (u32 index = 0; index < SpriteCount; ++index)
        {
            const SpriteSpec& spec = Sprites[index];
            const bool custom = spec.kind != SpriteKind::EngineDefault;
            const bool useNormal = spec.kind == SpriteKind::CustomNormal;
            const Tina::Render::RenderSprite2DInput sprite{
                .texture = *albedo,
                .normalTexture = useNormal ? *normal : Tina::Render::FrameResourceRef{},
                .shader = custom ? *shader : Tina::Render::FrameResourceRef{},
                .stableEntityKey = static_cast<u64>(index) + 1U,
                .centerX = spec.centerX,
                .centerY = spec.centerY,
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
    Tina::Core::Status uploadTexture(Tina::Render::IRenderDevice& device, TexturePixels& storage,
                                     TexturePixels pixels, Tina::Render::GpuTextureId& id,
                                     u64& bindingKey) noexcept
    {
        storage = pixels;
        const std::array<Tina::Render::Texture2DUploadLevel, 1> levels{{
            {.width = TextureExtent, .height = TextureExtent, .bytes = std::span{storage}},
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
        id = *uploaded;
        auto allocated = device.createTexture2DBinding(id);
        if (!allocated)
        {
            return Tina::Core::failure(std::move(allocated.error()));
        }
        bindingKey = *allocated;
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
        // Sprite 0 is the only one the vertical occluder cuts. Sprite 3 is its mirror
        // across the light, so equal-distance halves isolate the shadow from falloff.
        // Sprite 3/4 share X and are symmetric in Y about the light, so a normal-vs-flat
        // comparison cannot pass from attenuation. Left-vs-right subtracts the same
        // comparison on the flat partner, which is the falloff the normal map sits on.
        //
        // n.z + dot(n.xy, L/r) makes the half that faces the light almost identical to
        // flat +Z. The cotangent frame can also flip which screen half that is, so the
        // normal-vs-flat score is the larger of the two half-pair separations rather
        // than a guessed side, and the left-vs-right score is unsigned.
        const SpriteSpec& shadowedNormal = Sprites[0];
        const SpriteSpec& unoccludedNormal = Sprites[3];
        const SpriteSpec& unoccludedFlat = Sprites[4];
        const SpriteSpec& engine = Sprites[2];
        const ScreenBox engineBox = spriteInsetBox(engine, capture.width, capture.height);
        const ScreenBox unoccludedLeft =
            spriteLeftHalfBox(unoccludedNormal, capture.width, capture.height);
        const ScreenBox unoccludedRight =
            spriteRightHalfBox(unoccludedNormal, capture.width, capture.height);
        const ScreenBox flatLeft = spriteLeftHalfBox(unoccludedFlat, capture.width, capture.height);
        const ScreenBox flatRight =
            spriteRightHalfBox(unoccludedFlat, capture.width, capture.height);
        const ScreenBox shadowedFar =
            spriteLeftHalfBox(shadowedNormal, capture.width, capture.height);
        const ScreenBox mirroredFar =
            spriteRightHalfBox(unoccludedNormal, capture.width, capture.height);
        if (!boxIsUsable(capture, engineBox) || !boxIsUsable(capture, unoccludedLeft) ||
            !boxIsUsable(capture, unoccludedRight) || !boxIsUsable(capture, flatLeft) ||
            !boxIsUsable(capture, flatRight) || !boxIsUsable(capture, shadowedFar) ||
            !boxIsUsable(capture, mirroredFar))
        {
            counters_->evidenceError = Tina::Core::Error{
                Tina::Core::CoreErrorCode::Internal,
                "a sprite's screen region fell outside the captured frame"};
            return;
        }

        const RegionMean normalLeftMean = regionMean(capture, unoccludedLeft);
        const RegionMean normalRightMean = regionMean(capture, unoccludedRight);
        const RegionMean flatLeftMean = regionMean(capture, flatLeft);
        const RegionMean flatRightMean = regionMean(capture, flatRight);
        const u32 leftSeparation = meanSeparation(normalLeftMean, flatLeftMean);
        const u32 rightSeparation = meanSeparation(normalRightMean, flatRightMean);
        const u32 normalLeft = luminance(normalLeftMean);
        const u32 normalRight = luminance(normalRightMean);
        const u32 flatLeftLum = luminance(flatLeftMean);
        const u32 flatRightLum = luminance(flatRightMean);
        const u32 normalBias = normalLeft > normalRight ? normalLeft - normalRight : normalRight - normalLeft;
        const u32 flatBias = flatLeftLum > flatRightLum ? flatLeftLum - flatRightLum : flatRightLum - flatLeftLum;
        const u32 shadowedLum = luminance(regionMean(capture, shadowedFar));
        const u32 mirroredLum = luminance(regionMean(capture, mirroredFar));

        counters_->normalVsFlatSeparation = leftSeparation > rightSeparation ? leftSeparation : rightSeparation;
        counters_->normalLeftVsRight = normalBias > flatBias ? normalBias - flatBias : 0U;
        counters_->shadowedVsLit = mirroredLum > shadowedLum ? mirroredLum - shadowedLum : 0U;
        counters_->engineControlSpread = regionSpread(capture, engineBox);
        counters_->probeNormalLeft = normalLeft;
        counters_->probeNormalRight = normalRight;
        counters_->probeFlatLeft = flatLeftLum;
        counters_->probeFlatRight = flatRightLum;
        counters_->probeShadowed = shadowedLum;
        counters_->evidenceCollected = true;
    }

    enum class EvidenceStage : u8 {
        Idle,
        WarmupAwait,
        MeasureAwait,
        Done,
    };

    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    const DeviceCapture* capture_ = nullptr;
    Tina::Render::GpuShaderId shader_{};
    Tina::Render::GpuTextureId albedo_{};
    Tina::Render::GpuTextureId normal_{};
    u64 albedoBindingKey_ = 0;
    u64 normalBindingKey_ = 0;
    bool shaderBound_ = false;
    EvidenceStage stage_ = EvidenceStage::Idle;
    TexturePixels albedoPixels_{};
    TexturePixels normalPixels_{};
    mutable Tina::Samples::SampleSpriteFrameResource albedoResource_{};
    mutable Tina::Samples::SampleSpriteFrameResource normalResource_{};
    mutable KindedFrameResource shaderResource_{};
};

class ShaderLightingApplication final : public Tina::IGameApplication {
  public:
    ShaderLightingApplication(SampleOptions options, LifecycleCounters& counters,
                              const DeviceCapture& capture) noexcept
        : options_(options), counters_(&counters), capture_(&capture)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>>
    createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{
            std::make_unique<ShaderLightingState>(options_, *counters_, *capture_)};
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
    config.applicationName = "Tina vNext 2D Shader Lighting";
    config.primaryWindow.title = "Tina vNext - custom fragment reads engine lights";
    config.primaryWindow.initialLogicalExtent = {1280, 720};
    config.primaryWindow.initiallyVisible = true;
    config.renderSceneCapacities.spriteCapacity = 16;
    return config;
}

[[nodiscard]] bool evidenceAccepted(const LifecycleCounters& counters, u64 targetFrameCount) noexcept
{
    if (counters.evidenceCollected)
    {
        return counters.normalLeftVsRight >= EvidenceMinimumLeftVsRight &&
               counters.shadowedVsLit >= EvidenceMinimumShadowedVsLit &&
               counters.engineControlSpread >= EvidenceMinimumEngineSpread;
    }
    if (counters.evidenceError.has_value())
    {
        return counters.evidenceError->code.domain == Tina::Core::ErrorDomain::Render;
    }
    return targetFrameCount < EvidenceRequiredFrames;
}

void writeCounters(Tina::Core::JsonWriter& writer, const LifecycleCounters& counters)
{
    writer.member("frames", counters.frameUpdates);
    writer.member("renderExtractions", counters.renderExtractions);
    writer.member("spritesPerFrame", SpriteCount);
    writer.member("shaderPayloadBytes", counters.shaderPayloadBytes);
    writer.member("shaderBlobCount", counters.shaderBlobCount);
    writer.member("shaderRetired", counters.shaderRetired);
    writer.member("albedoRetired", counters.albedoRetired);
    writer.member("normalRetired", counters.normalRetired);
    writer.member("evidenceCollected", counters.evidenceCollected);
    writer.member("normalVsFlatSeparation", counters.normalVsFlatSeparation);
    writer.member("normalLeftVsRight", counters.normalLeftVsRight);
    writer.member("shadowedVsLit", counters.shadowedVsLit);
    writer.member("engineControlSpread", counters.engineControlSpread);
    writer.member("probeNormalLeft", counters.probeNormalLeft);
    writer.member("probeNormalRight", counters.probeNormalRight);
    writer.member("probeFlatLeft", counters.probeFlatLeft);
    writer.member("probeFlatRight", counters.probeFlatRight);
    writer.member("probeShadowed", counters.probeShadowed);
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
    ShaderLightingApplication application{options, counters, capture};
    auto runResult = (*hostResult)->run(application);
    hostResult->reset();
    if (!runResult)
    {
        writeError(runResult.error());
        return 1;
    }

    if (*runResult != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame ||
        counters.frameUpdates != options.targetFrameCount ||
        counters.renderExtractions != options.targetFrameCount || counters.stateEnters != 1 ||
        counters.stateExits != 1 || counters.applicationShutdowns != 1 ||
        counters.shaderBlobCount == 0 || !counters.shaderRetired || !counters.albedoRetired ||
        !counters.normalRetired || !evidenceAccepted(counters, options.targetFrameCount))
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

int runShaderLighting2dSample(int argumentCount, char** arguments)
{
    try
    {
        return runSample(argumentCount, arguments);
    } catch (const std::bad_alloc&)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::OutOfMemory,
                                "The 2D shader lighting sample ran out of memory"};
        writeError(error);
        return 1;
    } catch (const std::exception& exception)
    {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "An exception crossed the 2D shader lighting sample boundary"};
        error.addContext("main", exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    } catch (...)
    {
        Tina::Core::Error error{
            Tina::Core::CoreErrorCode::Internal,
            "A non-standard exception crossed the 2D shader lighting sample boundary"};
        writeError(error);
        return 1;
    }
}
