// First-person voxel sample: the Minecraft core loop, nothing more.
//
// Walk with WASD, jump with Space, look with the mouse, break with LMB, place with
// RMB, pick a block type with 1-4. No persistence, no crafting, no entities.
//
// Shape of the thing:
//   - VoxelWorld is a dense 128x64x128 block grid split into 16^3 chunks. 16^3 is
//     forced by StaticMeshUploadDesc using u16 indices; see the note there.
//   - ChunkMesh turns a chunk into one StaticMesh with visible-face culling, so a
//     fully enclosed chunk uploads nothing at all.
//   - Movement is an AABB swept per axis against solid blocks. There is no rigid
//     body, which is what a block game wants.
//   - The block under the crosshair comes from a DDA ray through the camera centre,
//     not from a cursor projection: the cursor is locked and hidden, so its window
//     position carries no information.
//
// The cursor lock is required rather than cosmetic. pointerLookDelta stops
// accumulating at the screen edge under PointerCaptureMode::Free, so a camera driven
// by it would jam after a short turn.

#include "BlockAtlas.hpp"
#include "ChunkMesh.hpp"
#include "DeviceCapture.hpp"
#include "VoxelRaycast.hpp"
#include "VoxelWorld.hpp"

#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/text/ArgParser.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/desktop/UiFontFile.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/InputActions.hpp>
#include <tina/runtime/PhaseContexts.hpp>
#include <tina/runtime/RunExitReason.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using Tina::i32;
using Tina::u16;
using Tina::u32;
using Tina::u64;
using Tina::u8;
using Tina::usize;
namespace Voxel = VoxelSample;

constexpr Tina::InputActionId MoveForwardAction{1};
constexpr Tina::InputActionId MoveBackAction{2};
constexpr Tina::InputActionId MoveLeftAction{3};
constexpr Tina::InputActionId MoveRightAction{4};
constexpr Tina::InputActionId JumpAction{5};
constexpr Tina::InputActionId BreakAction{6};
constexpr Tina::InputActionId PlaceAction{7};
constexpr Tina::InputActionId SelectGrassAction{8};
constexpr Tina::InputActionId SelectDirtAction{9};
constexpr Tina::InputActionId SelectStoneAction{10};
constexpr Tina::InputActionId SelectPlanksAction{11};
constexpr Tina::InputActionId ToggleTorchAction{12};
constexpr Tina::InputActionId ExitAction{13};
constexpr Tina::InputActionId FastTimeAction{14};
constexpr Tina::InputActionId ToggleShadowsAction{15};

// Device binding keys. Mesh, material and texture namespaces are independent, so
// the highlight can hold mesh key 1 while chunk N holds key N+2.
constexpr u32 HighlightMeshKey = 1;
constexpr u32 SunMeshKey = 2;
constexpr u32 ChunkMeshKeyBase = 3;
constexpr u32 BlockMaterialKey = 1;
constexpr u32 HighlightMaterialKey = 2;
constexpr u32 SunMaterialKey = 3;

// A capsule would be more forgiving on stairs, but an AABB is what the block grid
// collides against exactly.
constexpr float PlayerHalfWidth = 0.3F;
constexpr float PlayerHeight = 1.8F;
constexpr float PlayerEyeHeight = 1.62F;
constexpr float MoveSpeed = 4.8F;
constexpr float JumpSpeed = 8.4F;
constexpr float Gravity = 24.0F;
constexpr float MaxFallSpeed = 60.0F;
constexpr float ReachMeters = 5.5F;
constexpr float LookRadiansPerLogicalUnit = 0.0032F;
// Straight up/down would make the yaw/pitch basis degenerate.
constexpr float MaxPitchRadians = 1.55F;

constexpr float ChunkBoundsRadius = 13.86F; // sqrt(3) * ChunkSize/2, in chunk-local space
constexpr float Tau = 6.283185307F;

// Lighting. The opaque3D shader divides diffuse by PI but not ambient, so ambient at
// the same nominal scale as the sun outshines it: an ambient of 0.42 against a sun of
// 1.0 yields a diffuse peak near 0.26 against a flat 0.42 floor, which is why a lit
// face and an unlit face were nearly the same brightness. Ambient stays low and the
// sun carries radiance above 1 to restore the face-to-face contrast that makes a
// voxel world readable.
constexpr float AmbientScale = 0.09F;
constexpr float SunRadiance = 3.1F;
// Shadows. The range has two independent ceilings: the four cascades cover it, so a
// larger number spreads the same atlas over more world and coarsens every shadow, and
// the cascade depth bounds are the camera frustum padded by only 10m, so a range far
// below SunDistanceMeters is what keeps the sun disc out of the depth pass. 96m covers
// the visible chunks either way.
constexpr float ShadowDistanceMeters = 96.0F;
// Voxel faces are axis-aligned and the sun grazes them near dawn, which is the worst
// case for shadow acne. Normal bias is raised above the 0.02 default because offsetting
// along the normal costs nothing on a flat face, whereas depth bias trades directly
// against contact accuracy and is left alone.
constexpr float ShadowDepthBias = 0.0015F;
constexpr float ShadowNormalBiasMeters = 0.06F;
// A full azimuth revolution per cycle, with elevation riding a sine. Elevation is
// floored above zero so night dims rather than going fully black, which would look
// like a broken renderer instead of a night.
// 45 seconds was the first value here and it was too fast to read as a day: the light moved
// visibly within a single turn of the camera, so a change of shading could not be told apart
// from a change of viewing angle. At 240 the sun holds still long enough that a face's
// brightness is attributable to the face. Hold T to run the cycle fast when the point is to
// watch the cycle itself rather than to look at the world.
constexpr float DayCycleSeconds = 240.0F;
constexpr float FastTimeMultiplier = 24.0F;
constexpr float SunMinElevation = 0.05F;
constexpr float SunMaxElevation = 0.95F;

// The sky (ADR 0042). Linear, on the same scale as the light radiance above, and
// interpolated by the same normalized sun elevation that warms the sun: a sky that did
// not move while the ground did was what made the day cycle read as "no lighting at
// all", because the sky owns the upper half of the frame.
constexpr Tina::Render::RenderLinearColor DawnSkyColor{
    .red = 0.068478F, .green = 0.027321F, .blue = 0.042311F, .alpha = 1.0F};
constexpr Tina::Render::RenderLinearColor NoonSkyColor{
    .red = 0.138432F, .green = 0.341914F, .blue = 0.672443F, .alpha = 1.0F};
// The sun disc (ADR 0043). Kept at a fixed distance from the eye rather than at a world
// position: it stands in for something astronomically far away, so it must not shift as
// the player walks. 250m clears the 320m far plane with room to spare.
//
// The radius gives a disc about 5 degrees across, roughly ten times the real sun. A
// physically sized sun would be two pixels at this resolution and would read as a dead
// pixel, not as the light source; every block game oversizes it for the same reason.
constexpr float SunDistanceMeters = 250.0F;
constexpr float SunRadiusMeters = 11.0F;
constexpr u32 SunMeridians = 24;
constexpr u32 SunParallels = 12;
// Emissive radiance, above 1 so the disc reads as a light source rather than a pale ball:
// there is no tone mapping, so anything past 1 clamps to white at sRGB encode.
//
// Chosen so the clamp lands between the channels for most of the cycle, not past all of
// them. Only red exceeds 1 until the sun is high, so green and blue survive the encode and
// the disc runs orange at sunrise, warm white by mid-morning, white at noon.
//
// A larger value is tempting and wrong: at 2.2 the green channel only escapes the clamp
// while the sun is below 6% of the cycle, so the disc is a flat white ball all day and the
// warmth computed for it is thrown away at encode. The colour has to be checked against the
// transfer function, not just assigned.
constexpr float SunDiscRadiance = 1.15F;
constexpr float SunDiscDawnGreenScale = 0.42F;
constexpr float SunDiscDawnBlueScale = 0.16F;

// A held torch: a point light at the eye, bright enough that its falloff is visible
// against the sun rather than washed out by it.
constexpr float TorchRadius = 11.0F;
constexpr float TorchRadiance = 5.4F;
// Frame the scripted run toggles the torch on, so an automated run leaves evidence
// that the point-light path was exercised and not merely compiled.
constexpr u64 SelfTestTorchFrame = 40;
// The two extremes of the cycle, where sin() is -1 and +1. Used by the capture sequence
// so the sky pair is the widest difference the cycle can produce.
constexpr float DawnSunPhase = -1.570796327F;
constexpr float NoonSunPhase = 1.570796327F;
// Mid-cycle, where sin() is 0: elevation sits at the middle of its range and warmth at 0.5.
// The disc probe uses this rather than dawn for two reasons. A dawn sun is 12m above the eye
// at 250m out, so terrain occludes it and the probe would measure a hill; and a noon sun is
// saturated white, which is what many unrelated failures also produce. Mid-cycle clears the
// terrain and still leaves two channels below the clamp.
constexpr float SunProbePhase = 0.0F;
// Roughly 20 degrees below the horizon. Shallow enough that the ground band is distant
// terrain carrying long shadows, steep enough that no sky reaches the band.
constexpr float GroundProbePitchRadians = -0.35F;
// The highlight shares the mesh3D item namespace with the chunks, so its stable key
// must sit above every possible chunk key.
constexpr u64 HighlightStableKey = 1'000'000;
constexpr u64 SunStableKey = 1'000'001;
// Late enough that the player has landed, so the scripted edit hits terrain rather
// than the air the spawn starts in.
constexpr u64 SelfTestFrame = 30;

void writeError(const Tina::Core::Error& error)
{
    std::fprintf(stderr, "tina_sample_3d_voxel error: %s\n", error.message.c_str());
}

struct SampleOptions final {
    u32 windowLogicalWidth = 1280;
    u32 windowLogicalHeight = 720;
    // 0 runs until the window closes; a fixed budget is what an automated run wants.
    u32 maxFrames = 0;
    u32 worldSeed = 20260902;
    // Drives one break and one place through the same applyEdit path a click takes,
    // so the core mechanic has evidence without a human clicking.
    bool selfTestEdits = false;
    // Reads back two frames from the same camera pose, one with the torch off and one
    // with it on, and reports luminance statistics for both. Counters prove the
    // lighting reached the scene; only pixels prove it reached the screen. Pins the sun
    // so the only difference between the pair is the point light.
    bool captureLuma = false;
};

[[nodiscard]] Tina::Core::Result<SampleOptions> parseOptions(int argc, char** argv)
{
    SampleOptions options{};
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument{argv[index]};
        if (argument == "--selftest-edits")
        {
            options.selfTestEdits = true;
            continue;
        }
        if (argument == "--capture-luma")
        {
            options.captureLuma = true;
            continue;
        }
        bool matched = false;
        for (const auto& [prefix, target] :
             std::initializer_list<std::pair<std::string_view, u32*>>{
                 {"--frames=", &options.maxFrames},
                 {"--seed=", &options.worldSeed},
                 {"--width=", &options.windowLogicalWidth},
                 {"--height=", &options.windowLogicalHeight}})
        {
            if (!argument.starts_with(prefix))
            {
                continue;
            }
            if (!Tina::Core::parseArgUnsigned(argument.substr(prefix.size()), *target))
            {
                Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                        "Option value must be an unsigned 32-bit integer"};
                error.addContext("parseOptions", argument);
                return Tina::Core::failure(std::move(error));
            }
            matched = true;
            break;
        }
        if (!matched)
        {
            Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                    "Unsupported command-line argument"};
            error.addContext("parseOptions", argument);
            return Tina::Core::failure(std::move(error));
        }
    }
    if (options.windowLogicalWidth == 0 || options.windowLogicalHeight == 0)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "--width and --height must be greater than zero");
    }
    return options;
}

// Luminance summary of one captured frame. Distinct level count is the figure that
// answers the complaint: a flat-lit world collapses onto a handful of levels no matter
// how many faces it shows, because every face of a given block type resolves to the
// same colour.
struct LumaStats final {
    u32 width = 0;
    u32 height = 0;
    u32 distinctLevels = 0;
    u8 minimum = 255;
    u8 maximum = 0;
    double mean = 0.0;
    bool valid = false;
};

// Mean RGB of the topmost rows. Whole-frame luma cannot show a sky change: the ground
// occupies most of the pixels and moves in the opposite direction, so the two average out.
// The top band is the one region that is sky at every camera pitch the capture uses.
struct SkySample final {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    // Taken across all three channels at once, so on a uniform surface these are simply its
    // brightest and dimmest channel rather than a spatial range. With nothing but clear colour
    // in the band the peak equals the largest mean, which is how the ADR 0042 reading proves
    // the band is pure background.
    u8 peak = 0;
    u8 floorValue = 255;
    // The single pixel at the middle of the region. Comparing it against the means is what
    // separates an error in the shading model from an error in the sampling window.
    u8 centerRed = 0;
    u8 centerGreen = 0;
    u8 centerBlue = 0;
    bool valid = false;
};

// Enough rows to average away dither, few enough to stay above the horizon.
constexpr u32 SkySampleRowCount = 24;

// Side of the square sampled at the centre of the frame, used to prove the sun disc is
// actually drawn. Kept well inside the disc's roughly 5-degree width so a small aiming
// error cannot put sky in the patch and turn the reading into an average of the two.
constexpr u32 CenterPatchSize = 48;

[[nodiscard]] SkySample measureCenterPatch(const Tina::Render::Rgba8FrameCapture& capture) noexcept
{
    SkySample sample{};
    if (capture.empty() || capture.width < CenterPatchSize || capture.height < CenterPatchSize)
    {
        return sample;
    }
    const u32 firstColumn = (capture.width - CenterPatchSize) / 2U;
    const u32 firstRow = (capture.height - CenterPatchSize) / 2U;
    const usize rowStride = static_cast<usize>(capture.width) * 4U;
    u64 red = 0;
    u64 green = 0;
    u64 blue = 0;
    u64 samples = 0;
    u8 peak = 0;
    u8 floorValue = 255;
    for (u32 row = firstRow; row < firstRow + CenterPatchSize; ++row)
    {
        for (u32 column = firstColumn; column < firstColumn + CenterPatchSize; ++column)
        {
            const usize offset = static_cast<usize>(row) * rowStride + static_cast<usize>(column) * 4U;
            if (offset + 3 >= capture.rgba8Pixels.size())
            {
                continue;
            }
            const auto r = std::to_integer<u8>(capture.rgba8Pixels[offset]);
            const auto g = std::to_integer<u8>(capture.rgba8Pixels[offset + 1]);
            const auto b = std::to_integer<u8>(capture.rgba8Pixels[offset + 2]);
            red += static_cast<u32>(r);
            green += static_cast<u32>(g);
            blue += static_cast<u32>(b);
            peak = std::max({peak, r, g, b});
            floorValue = std::min({floorValue, r, g, b});
            ++samples;
        }
    }
    if (samples == 0)
    {
        return sample;
    }
    sample.red = static_cast<double>(red) / static_cast<double>(samples);
    sample.green = static_cast<double>(green) / static_cast<double>(samples);
    sample.blue = static_cast<double>(blue) / static_cast<double>(samples);
    sample.peak = peak;
    sample.floorValue = floorValue;
    const usize centerOffset = static_cast<usize>(capture.height / 2U) * rowStride +
                               static_cast<usize>(capture.width / 2U) * 4U;
    if (centerOffset + 3 < capture.rgba8Pixels.size())
    {
        sample.centerRed = std::to_integer<u8>(capture.rgba8Pixels[centerOffset]);
        sample.centerGreen = std::to_integer<u8>(capture.rgba8Pixels[centerOffset + 1]);
        sample.centerBlue = std::to_integer<u8>(capture.rgba8Pixels[centerOffset + 2]);
    }
    sample.valid = true;
    return sample;
}

// Fraction of the frame height, measured up from the bottom, that the shadow pair reads.
// The sky is untouched by shadows, so including it would only dilute the signal; a quarter
// of the frame is entirely terrain at the pair's downward pitch and 70-degree vertical FOV.
constexpr u32 GroundBandDivisor = 4;

// Difference, in encoded luma steps, that counts as a real change rather than dither. The
// atlas jitters every texel, so a threshold of a couple of steps would report noise.
constexpr int ShadowLumaEpsilon = 8;

struct GroundSample final {
    double luma = 0.0;
    u8 minLuma = 255;
    u8 maxLuma = 0;
    u64 totalPixels = 0;
    bool valid = false;
};

// Per-pixel comparison of the two ground bands. An absolute threshold on luma would need a
// known albedo, and the first attempt at one saturated: the whole band sits under 96, so
// "fraction below the ambient ceiling" read 1.0 with shadows both on and off and proved
// nothing. A difference needs no such assumption, and it carries an invariant that a mean
// cannot: shadows only ever remove light, so brightened must be zero.
struct ShadowDiff final {
    u64 darkened = 0;
    u64 brightened = 0;
    u64 unchanged = 0;
    int maximumDarkening = 0;
    int maximumBrightening = 0;
    bool valid = false;

    [[nodiscard]] u64 total() const noexcept { return darkened + brightened + unchanged; }
    [[nodiscard]] double darkenedFraction() const noexcept
    {
        return total() == 0 ? 0.0 : static_cast<double>(darkened) / static_cast<double>(total());
    }
};

// Rec. 709 luma on the encoded bytes. Both captures share an encode, so comparing them in
// sRGB rather than linear costs nothing.
[[nodiscard]] std::vector<u8> extractGroundBandLuma(
    const Tina::Render::Rgba8FrameCapture& capture)
{
    std::vector<u8> luma{};
    if (capture.empty() || capture.width == 0 || capture.height < GroundBandDivisor)
    {
        return luma;
    }
    const u32 firstRow = capture.height - capture.height / GroundBandDivisor;
    const usize rowStride = static_cast<usize>(capture.width) * 4U;
    luma.reserve(static_cast<usize>(capture.width) * (capture.height - firstRow));
    for (u32 row = firstRow; row < capture.height; ++row)
    {
        const usize rowStart = static_cast<usize>(row) * rowStride;
        for (usize offset = rowStart; offset + 3 < rowStart + rowStride &&
                                      offset + 3 < capture.rgba8Pixels.size();
             offset += 4)
        {
            const double value =
                0.2126 * static_cast<double>(std::to_integer<u8>(capture.rgba8Pixels[offset])) +
                0.7152 * static_cast<double>(std::to_integer<u8>(capture.rgba8Pixels[offset + 1])) +
                0.0722 * static_cast<double>(std::to_integer<u8>(capture.rgba8Pixels[offset + 2]));
            luma.push_back(static_cast<u8>(std::clamp(value, 0.0, 255.0)));
        }
    }
    return luma;
}

[[nodiscard]] GroundSample summarizeGroundBand(std::span<const u8> luma) noexcept
{
    GroundSample sample{};
    if (luma.empty())
    {
        return sample;
    }
    u64 sum = 0;
    for (const u8 value : luma)
    {
        sum += static_cast<u64>(value);
        sample.minLuma = std::min(sample.minLuma, value);
        sample.maxLuma = std::max(sample.maxLuma, value);
    }
    sample.totalPixels = luma.size();
    sample.luma = static_cast<double>(sum) / static_cast<double>(luma.size());
    sample.valid = true;
    return sample;
}

// `shadowed` and `unshadowed` must come from frames that differ only in the shadow
// descriptor, so a mismatch in length means the window resized and the comparison is void.
[[nodiscard]] ShadowDiff compareGroundBands(std::span<const u8> shadowed,
                                            std::span<const u8> unshadowed) noexcept
{
    ShadowDiff diff{};
    if (shadowed.empty() || shadowed.size() != unshadowed.size())
    {
        return diff;
    }
    for (usize index = 0; index < shadowed.size(); ++index
        )
    {
        const int delta =
            static_cast<int>(unshadowed[index]) - static_cast<int>(shadowed[index]);
        if (delta >= ShadowLumaEpsilon)
        {
            ++diff.darkened;
            diff.maximumDarkening = std::max(diff.maximumDarkening, delta);
        }
        else if (delta <= -ShadowLumaEpsilon)
        {
            ++diff.brightened;
            diff.maximumBrightening = std::max(diff.maximumBrightening, -delta);
        }
        else
        {
            ++diff.unchanged;
        }
    }
    diff.valid = true;
    return diff;
}

[[nodiscard]] SkySample measureSky(const Tina::Render::Rgba8FrameCapture& capture) noexcept
{
    SkySample sample{};
    if (capture.empty() || capture.width == 0 || capture.height == 0)
    {
        return sample;
    }
    // Capture rows run top-left origin, so row 0 is the top of the screen.
    const u32 rows = std::min(SkySampleRowCount, capture.height);
    const usize rowStride = static_cast<usize>(capture.width) * 4U;
    u64 red = 0;
    u64 green = 0;
    u64 blue = 0;
    u64 samples = 0;
    u8 peak = 0;
    u8 floorValue = 255;
    for (u32 row = 0; row < rows; ++row)
    {
        const usize rowStart = static_cast<usize>(row) * rowStride;
        for (usize offset = rowStart; offset + 3 < rowStart + rowStride &&
                                      offset + 3 < capture.rgba8Pixels.size();
             offset += 4)
        {
            const auto r = std::to_integer<u8>(capture.rgba8Pixels[offset]);
            const auto g = std::to_integer<u8>(capture.rgba8Pixels[offset + 1]);
            const auto b = std::to_integer<u8>(capture.rgba8Pixels[offset + 2]);
            red += static_cast<u32>(r);
            green += static_cast<u32>(g);
            blue += static_cast<u32>(b);
            peak = std::max({peak, r, g, b});
            floorValue = std::min({floorValue, r, g, b});
            ++samples;
        }
    }
    sample.peak = peak;
    sample.floorValue = floorValue;
    if (samples == 0)
    {
        return sample;
    }
    sample.red = static_cast<double>(red) / static_cast<double>(samples);
    sample.green = static_cast<double>(green) / static_cast<double>(samples);
    sample.blue = static_cast<double>(blue) / static_cast<double>(samples);
    sample.valid = true;
    return sample;
}

struct RunCounters final {
    u64 frames = 0;
    u64 fixedSteps = 0;
    u64 chunkMeshUploads = 0;
    u64 chunkMeshDestroys = 0;
    u32 nonEmptyChunks = 0;
    u64 totalChunkVertices = 0;
    u64 lastSubmittedChunks = 0;
    u64 breakRequests = 0;
    u64 breakApplied = 0;
    u64 placeRequests = 0;
    u64 placeApplied = 0;
    u64 placeRejectedNoFace = 0;
    u64 placeRejectedOccupied = 0;
    u64 placeRejectedInsidePlayer = 0;
    u64 raycastMisses = 0;
    u64 lookDeltaFrames = 0;
    u64 torchOnFrames = 0;
    u64 lightingWrites = 0;
    u64 sunMaterialRebinds = 0;
    float sunElevationMin = 2.0F;
    float sunElevationMax = -2.0F;
    bool exitRequestedByKey = false;
    bool cursorReleaseFailed = false;
    bool cursorLocked = false;
    LumaStats torchOffLuma{};
    LumaStats torchOnLuma{};
    SkySample dawnSky{};
    SkySample noonSky{};
    // Centre patch with the camera aimed at the sun, and again with it aimed at the opposite
    // point of the sky. The pair is the evidence: one reading alone cannot distinguish a sun
    // from a bright sky (ADR 0043).
    SkySample sunDisc{};
    SkySample sunAway{};
    // Ground band with cascaded shadows on and off, same camera and same sun. Shadows can
    // only subtract light, so shadowOn must be the darker of the two or the cascade is not
    // reaching the shader.
    GroundSample shadowOn{};
    GroundSample shadowOff{};
    ShadowDiff shadowDiff{};
    std::optional<Tina::Core::Error> captureError{};
    bool playerEverGrounded = false;
    std::optional<Tina::Core::Error> shutdownError{};
};

[[nodiscard]] LumaStats measureLuma(const Tina::Render::Rgba8FrameCapture& capture) noexcept
{
    LumaStats stats{};
    if (capture.empty())
    {
        return stats;
    }
    std::array<bool, 256> seen{};
    u64 total = 0;
    u64 samples = 0;
    for (usize offset = 0; offset + 3 < capture.rgba8Pixels.size(); offset += 4)
    {
        const auto red = static_cast<u32>(std::to_integer<u8>(capture.rgba8Pixels[offset]));
        const auto green = static_cast<u32>(std::to_integer<u8>(capture.rgba8Pixels[offset + 1]));
        const auto blue = static_cast<u32>(std::to_integer<u8>(capture.rgba8Pixels[offset + 2]));
        // Integer Rec. 601 weights. Exactness does not matter here; what matters is that
        // the same function is applied to both frames of the pair.
        const auto luma = static_cast<u8>((red * 77U + green * 150U + blue * 29U) >> 8U);
        seen[luma] = true;
        total += luma;
        ++samples;
    }
    if (samples == 0)
    {
        return stats;
    }
    stats.width = capture.width;
    stats.height = capture.height;
    stats.distinctLevels = static_cast<u32>(std::count(seen.begin(), seen.end(), true));
    for (u32 level = 0; level < 256U; ++level)
    {
        if (!seen[level])
        {
            continue;
        }
        stats.minimum = std::min(stats.minimum, static_cast<u8>(level));
        stats.maximum = std::max(stats.maximum, static_cast<u8>(level));
    }
    stats.mean = static_cast<double>(total) / static_cast<double>(samples);
    stats.valid = true;
    return stats;
}

// One chunk's GPU residency. An empty chunk keeps no mesh and no binding, which is
// most of this world: air above the surface, fully enclosed stone below.
struct ChunkGpu final {
    Tina::Render::GpuMeshId mesh{};
    u32 indexCount = 0;
    bool bound = false;
};

// Refs are interned for resources that outlive every frame (they are released in
// onExit, after EngineHost has already dropped all packets), so the pin has nothing
// to track and the release callback is intentionally empty.
void releaseStaticFrameResource(void*) noexcept {}

[[nodiscard]] Tina::Core::Result<Tina::Render::FrameResourceRef>
internStaticResource(Tina::Render::FrameResourceSink& sink, Tina::Render::FrameResourceKind kind,
                     u64 deviceBindingKey)
{
    Tina::Render::FramePin pin{Tina::Render::FramePinKind::Custom, deviceBindingKey, nullptr,
                               &releaseStaticFrameResource};
    return sink.intern(
        Tina::Render::FrameResourceDescriptor{.kind = kind, .deviceBindingKey = deviceBindingKey},
        std::move(pin));
}

class VoxelState final : public Tina::IGameState {
  public:
    VoxelState(const SampleOptions& options, RunCounters& counters,
               Voxel::DeviceCapture& capture) noexcept
        : options_(&options), counters_(&counters), capture_(&capture)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext&) override
    {
        auto* device = capture_->get();
        if (device == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "render device was not captured");
        }

        world_.emplace(options_->worldSeed);
        chunks_.resize(static_cast<usize>(Voxel::WorldChunkCount));

        // EngineHost discards a failed onEnter candidate without calling onExit, so
        // GPU ownership stays transactional until the state is fully entered.
        auto rollback = Tina::Core::makeScopeExit([this]() noexcept { releaseGpuResources(); });

        if (auto status = uploadAtlasAndMaterials(*device); !status)
        {
            return status;
        }
        if (auto status = uploadHighlightMesh(*device); !status)
        {
            return status;
        }
        if (auto status = uploadSunMesh(*device); !status)
        {
            return status;
        }
        // The sun material carries a colour that depends on the time of day, so it is bound
        // here for the first frame and then only when that colour changes.
        if (auto status = syncSunMaterial(*device); !status)
        {
            return status;
        }
        if (auto status = remeshDirtyChunks(*device); !status)
        {
            return status;
        }
        counters_->nonEmptyChunks = static_cast<u32>(
            std::count_if(chunks_.begin(), chunks_.end(),
                          [](const ChunkGpu& slot) noexcept { return slot.bound; }));
        spawnPlayer();
        rollback.release();
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override { releaseGpuResources(); }

  private:
    [[nodiscard]] Tina::Core::Status uploadAtlasAndMaterials(Tina::Render::IRenderDevice& device)
    {
        const std::vector<std::byte> pixels = Voxel::makeBlockAtlasRgba8();
        const std::array<Tina::Render::Texture2DUploadLevel, 1> levels{{{
            .width = Voxel::AtlasWidth,
            .height = Voxel::AtlasHeight,
            .bytes = std::span<const std::byte>{pixels},
        }}};
        // Point/Clamp: a blocky atlas must not blur, and Repeat would wrap tile 3
        // into tile 0 at the seam.
        auto texture = device.createTexture2D(Tina::Render::Texture2DUploadDesc{
            .format = Tina::Render::GpuTextureFormat::Rgba8Unorm,
            .colorSpace = Tina::Render::GpuTextureColorSpace::Srgb,
            .sampler =
                Tina::Render::GpuTextureSamplerDesc{
                    .wrapU = Tina::Render::GpuTextureWrapMode::Clamp,
                    .wrapV = Tina::Render::GpuTextureWrapMode::Clamp,
                    .minFilter = Tina::Render::GpuTextureFilterMode::Point,
                    .magFilter = Tina::Render::GpuTextureFilterMode::Point,
                    .mipFilter = Tina::Render::GpuTextureMipFilterMode::None,
                },
            .levels = std::span<const Tina::Render::Texture2DUploadLevel>{levels},
        });
        if (!texture)
        {
            return Tina::Core::failure(std::move(texture.error()));
        }
        atlasTexture_ = *texture;

        if (auto status = device.setMesh3DMaterialBinding(
                BlockMaterialKey,
                Tina::Render::Mesh3DMaterialBindingDesc{
                    .baseColorTexture = atlasTexture_,
                    .metallicFactor = 0.0F,
                    .roughnessFactor = 0.85F,
                    .alphaMode = Tina::Render::Mesh3DAlphaMode::Opaque,
                });
            !status)
        {
            return status;
        }
        blockMaterialBound_ = true;

        // Untextured, translucent, drawn inflated around the targeted block.
        if (auto status = device.setMesh3DMaterialBinding(
                HighlightMaterialKey,
                Tina::Render::Mesh3DMaterialBindingDesc{
                    .metallicFactor = 0.0F,
                    .roughnessFactor = 1.0F,
                    .alphaMode = Tina::Render::Mesh3DAlphaMode::Blend,
                });
            !status)
        {
            return status;
        }
        highlightMaterialBound_ = true;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status uploadHighlightMesh(Tina::Render::IRenderDevice& device)
    {
        const Voxel::ChunkMeshData cube = Voxel::makeUnitCubeMesh();
        auto mesh = device.createStaticMesh(Tina::Render::StaticMeshUploadDesc{
            .vertexCount = cube.vertexCount(),
            .indexCount = cube.indexCount(),
            .vertices = std::span<const float>{cube.vertices},
            .indices = std::span<const u16>{cube.indices},
        });
        if (!mesh)
        {
            return Tina::Core::failure(std::move(mesh.error()));
        }
        highlightMesh_ = *mesh;
        highlightIndexCount_ = cube.indexCount();
        if (auto status = device.setMesh3DBinding(HighlightMeshKey, highlightMesh_); !status)
        {
            return status;
        }
        highlightBound_ = true;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status uploadSunMesh(Tina::Render::IRenderDevice& device)
    {
        const Voxel::ChunkMeshData sphere = Voxel::makeUnitSphereMesh(SunMeridians, SunParallels);
        auto mesh = device.createStaticMesh(Tina::Render::StaticMeshUploadDesc{
            .vertexCount = sphere.vertexCount(),
            .indexCount = sphere.indexCount(),
            .vertices = std::span<const float>{sphere.vertices},
            .indices = std::span<const u16>{sphere.indices},
        });
        if (!mesh)
        {
            return Tina::Core::failure(std::move(mesh.error()));
        }
        sunMesh_ = *mesh;
        if (auto status = device.setMesh3DBinding(SunMeshKey, sunMesh_); !status)
        {
            return status;
        }
        sunBound_ = true;
        return Tina::Core::success();
    }

    // Lighting is written per frame into the scene rather than once into the device.
    // Both layers work — with no scene snapshot the device-level uniforms are used as
    // the fallback — but only the scene layer can carry a sun that moves and a torch
    // that toggles, and mixing the two would leave two sources of truth for the same
    // uniforms. The spans are consumed synchronously by the writer, so locals suffice.
    [[nodiscard]] Tina::Core::Status writeSkyAndLighting(Tina::Render::RenderSceneWriter& writer) const
    {
        const float elevation = sunElevation();
        const float azimuth = sunPhaseRadians_;
        const float horizontal = std::sqrt(std::max(1.0F - elevation * elevation, 0.0F));
        // Warm the sun as it nears the horizon. A midday-white sun at a grazing angle
        // reads as an underexposed noon rather than as dawn. The sky below reads the same
        // term, so ground and sky cannot disagree about the time of day.
        const float warmth = dayFraction();
        const std::array<Tina::Render::Mesh3DDirectionalLight, 1> directional{{{
            .directionTowardLightX = std::cos(azimuth) * horizontal,
            .directionTowardLightY = elevation,
            .directionTowardLightZ = std::sin(azimuth) * horizontal,
            .colorR = SunRadiance,
            .colorG = SunRadiance * (0.72F + 0.26F * warmth),
            .colorB = SunRadiance * (0.46F + 0.48F * warmth),
        }}};

        // The torch sits at the eye, so it lights whatever the player faces. Zero point
        // lights is a valid span, which is how "torch off" is expressed.
        const std::array<Tina::Render::Mesh3DPointLight, 1> torch{{{
            .positionX = positionX,
            .positionY = positionY + PlayerEyeHeight,
            .positionZ = positionZ,
            .influenceRadius = TorchRadius,
            .colorR = TorchRadiance,
            .colorG = TorchRadiance * 0.62F,
            .colorB = TorchRadiance * 0.28F,
        }}};

        if (auto status = writer.setMesh3DLighting(Tina::Render::Mesh3DLightingDesc{
                .directionalLights =
                    std::span<const Tina::Render::Mesh3DDirectionalLight>{directional},
                .pointLights = torchOn_
                                   ? std::span<const Tina::Render::Mesh3DPointLight>{torch}
                                   : std::span<const Tina::Render::Mesh3DPointLight>{},
                // Without this an occluded face stays fully lit as long as its normal
                // points at the sun, so brightness tracked the normal and never the
                // occlusion — which is what made the light look like it changed for no
                // reason as the camera moved. The range stays far below SunDistanceMeters
                // so the sun disc falls outside the cascade depth bounds and cannot cast.
                .cascadedDirectionalShadow =
                    shadowsOn_ ? std::optional<Tina::Render::Mesh3DCascadedDirectionalShadow>{
                                     Tina::Render::Mesh3DCascadedDirectionalShadow{
                                         .directionalLightIndex = 0,
                                         .maximumDistanceMeters = ShadowDistanceMeters,
                                         .depthBias = ShadowDepthBias,
                                         .normalBiasMeters = ShadowNormalBiasMeters,
                                     }}
                               : std::nullopt,
                .ambientScale = AmbientScale,
            });
            !status)
        {
            return status;
        }

        // ADR 0042. The sky is half the frame; leaving it fixed while the ground moved is
        // what made the day cycle unreadable.
        return writer.setClearColor(mixLinear(DawnSkyColor, NoonSkyColor, warmth));
    }

    [[nodiscard]] static Tina::Render::RenderLinearColor
    mixLinear(const Tina::Render::RenderLinearColor& from,
              const Tina::Render::RenderLinearColor& to, float t) noexcept
    {
        const auto lerp = [t](float a, float b) noexcept { return a + (b - a) * t; };
        return Tina::Render::RenderLinearColor{
            .red = lerp(from.red, to.red),
            .green = lerp(from.green, to.green),
            .blue = lerp(from.blue, to.blue),
            .alpha = lerp(from.alpha, to.alpha),
        };
    }

    // 0 at the lowest sun, 1 at the highest. Taken from the phase rather than from the
    // elevation so it stays linear in time instead of bunching near the extremes.
    [[nodiscard]] float dayFraction() const noexcept
    {
        return std::clamp((std::sin(sunPhaseRadians_) + 1.0F) * 0.5F, 0.0F, 1.0F);
    }

    // Arms one frame, reads it back on the next, twice: torch off then torch on. Runs
    // only after the body has landed and the aim has settled, so both frames share a
    // camera pose and the pair differs by the point light alone.
    //
    // A capture failure is recorded and the sequence abandoned rather than failing the
    // phase: this is diagnostic instrumentation, and a backend that declines read-back
    // is not a reason for the sample itself to stop.
    void advanceCaptureStage(Tina::Render::IRenderDevice& device) noexcept
    {
        if (!options_->captureLuma || captureStage_ == CaptureStage::Done)
        {
            return;
        }
        const auto abandon = [this](Tina::Core::Error error) noexcept {
            if (!counters_->captureError.has_value())
            {
                counters_->captureError = std::move(error);
            }
            captureStage_ = CaptureStage::Done;
        };

        switch (captureStage_)
        {
        case CaptureStage::Idle:
            if (!grounded_ || counters_->frames < SelfTestFrame)
            {
                return;
            }
            torchOn_ = false;
            if (auto armed = device.requestPrimaryFrameCaptureOnNextPresent(); !armed)
            {
                abandon(std::move(armed.error()));
                return;
            }
            captureStage_ = CaptureStage::AwaitTorchOff;
            return;

        case CaptureStage::AwaitTorchOff:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandon(std::move(captured.error()));
                return;
            }
            counters_->torchOffLuma = measureLuma(*captured);
            torchOn_ = true;
            if (auto armed = device.requestPrimaryFrameCaptureOnNextPresent(); !armed)
            {
                abandon(std::move(armed.error()));
                return;
            }
            captureStage_ = CaptureStage::AwaitTorchOn;
            return;
        }

        case CaptureStage::AwaitTorchOn:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandon(std::move(captured.error()));
                return;
            }
            counters_->torchOnLuma = measureLuma(*captured);
            // Hand off to the sky pair (ADR 0042). The torch goes back off and the sun
            // phase is pinned, so the only difference between the next two frames is the
            // clear colour: same camera, same geometry, same lights.
            torchOn_ = false;
            pinSun(DawnSunPhase);
            // The self-test aimed straight down to break a block, which leaves the whole
            // frame full of terrain. Sampling that band would measure sunlit grass and
            // report it as the sky, so look up before the pair is captured.
            pitchRadians = MaxPitchRadians;
            if (auto armed = device.requestPrimaryFrameCaptureOnNextPresent(); !armed)
            {
                abandon(std::move(armed.error()));
                return;
            }
            captureStage_ = CaptureStage::AwaitDawnSky;
            return;
        }

        case CaptureStage::AwaitDawnSky:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandon(std::move(captured.error()));
                return;
            }
            counters_->dawnSky = measureSky(*captured);
            pinSun(NoonSunPhase);
            if (auto armed = device.requestPrimaryFrameCaptureOnNextPresent(); !armed)
            {
                abandon(std::move(armed.error()));
                return;
            }
            captureStage_ = CaptureStage::AwaitNoonSky;
            return;
        }

        case CaptureStage::AwaitNoonSky:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandon(std::move(captured.error()));
                return;
            }
            counters_->noonSky = measureSky(*captured);
            // Hand off to the sun-disc pair (ADR 0043). The camera is aimed straight at the
            // disc, so the centre patch is disc; the next frame turns 180 degrees to sample sky
            // at the same elevation. Two readings, because a bright centre patch on its own
            // could just as easily be a bright sky.
            //
            // Read at mid-cycle: the disc has one channel clamped and two that survive the
            // encode, so its colour can only come from the emissive values actually written.
            pinSun(SunProbePhase);
            aimAtSun();
            if (auto armed = device.requestPrimaryFrameCaptureOnNextPresent(); !armed)
            {
                abandon(std::move(armed.error()));
                return;
            }
            captureStage_ = CaptureStage::AwaitSunDisc;
            return;
        }

        case CaptureStage::AwaitSunDisc:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandon(std::move(captured.error()));
                return;
            }
            counters_->sunDisc = measureCenterPatch(*captured);
            aimAwayFromSun();
            if (auto armed = device.requestPrimaryFrameCaptureOnNextPresent(); !armed)
            {
                abandon(std::move(armed.error()));
                return;
            }
            captureStage_ = CaptureStage::AwaitSunAway;
            return;
        }

        case CaptureStage::AwaitSunAway:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandon(std::move(captured.error()));
                return;
            }
            counters_->sunAway = measureCenterPatch(*captured);
            // Hand off to the shadow pair. Aim into the sun's azimuth but pitched down, so the
            // terrain is backlit and its shadows stretch toward the eye across the ground band.
            // The sun stays pinned at SunProbePhase, so the two frames differ by the shadow
            // descriptor alone and any luma drop has nowhere else to come from.
            aimAcrossSunlitGround();
            shadowsOn_ = true;
            if (auto armed = device.requestPrimaryFrameCaptureOnNextPresent(); !armed)
            {
                abandon(std::move(armed.error()));
                return;
            }
            captureStage_ = CaptureStage::AwaitShadowOn;
            return;
        }

        case CaptureStage::AwaitShadowOn:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandon(std::move(captured.error()));
                return;
            }
            shadowOnBandLuma_ = extractGroundBandLuma(*captured);
            counters_->shadowOn = summarizeGroundBand(shadowOnBandLuma_);
            shadowsOn_ = false;
            if (auto armed = device.requestPrimaryFrameCaptureOnNextPresent(); !armed)
            {
                abandon(std::move(armed.error()));
                return;
            }
            captureStage_ = CaptureStage::AwaitShadowOff;
            return;
        }

        case CaptureStage::AwaitShadowOff:
        {
            auto captured = device.collectPrimaryFrameCapture();
            if (!captured)
            {
                abandon(std::move(captured.error()));
                return;
            }
            const std::vector<u8> offBandLuma = extractGroundBandLuma(*captured);
            counters_->shadowOff = summarizeGroundBand(offBandLuma);
            counters_->shadowDiff = compareGroundBands(shadowOnBandLuma_, offBandLuma);
            shadowOnBandLuma_.clear();
            shadowOnBandLuma_.shrink_to_fit();
            shadowsOn_ = true;
            pinnedSunPhase_.reset();
            pinnedAim_.reset();
            captureStage_ = CaptureStage::Done;
            return;
        }

        case CaptureStage::Done:
            return;
        }
    }

    // The disc's emissive colour lives on the material binding, not in the scene, so it has
    // to be pushed to the device whenever it changes (ADR 0043). Rebinding is an
    // insert_or_assign, but it is still guarded: the capture pair holds the sun still, and an
    // unconditional rebind there would be a per-frame write that no longer corresponds to a
    // change.
    [[nodiscard]] Tina::Core::Status syncSunMaterial(Tina::Render::IRenderDevice& device)
    {
        if (!sunBound_)
        {
            return Tina::Core::success();
        }
        const float warmth = dayFraction();
        const Tina::Render::Mesh3DMaterialBindingDesc desc{
            .metallicFactor = 0.0F,
            .roughnessFactor = 1.0F,
            .emissiveFactorR = SunDiscRadiance,
            .emissiveFactorG =
                SunDiscRadiance * (SunDiscDawnGreenScale + (1.0F - SunDiscDawnGreenScale) * warmth),
            .emissiveFactorB =
                SunDiscRadiance * (SunDiscDawnBlueScale + (1.0F - SunDiscDawnBlueScale) * warmth),
            .alphaMode = Tina::Render::Mesh3DAlphaMode::Opaque,
        };
        if (sunMaterialBound_ && desc == sunMaterialDesc_)
        {
            return Tina::Core::success();
        }
        if (auto status = device.setMesh3DMaterialBinding(SunMaterialKey, desc); !status)
        {
            return status;
        }
        sunMaterialDesc_ = desc;
        sunMaterialBound_ = true;
        ++counters_->sunMaterialRebinds;
        return Tina::Core::success();
    }

    // Points the camera at the sun. Inverts the forward vector in currentAim():
    // forward = (-sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch)), and the sun sits along
    // (cos(phase)h, elevation, sin(phase)h) with h = cos(asin(elevation)), so pitch is
    // asin(elevation) and the yaw terms fall out of the two horizontal components.
    void aimAtSun() noexcept
    {
        const float elevation = std::clamp(sunElevation(), -1.0F, 1.0F);
        pinAim(std::atan2(-std::cos(sunPhaseRadians_), -std::sin(sunPhaseRadians_)),
               std::clamp(std::asin(elevation), -MaxPitchRadians, MaxPitchRadians));
    }

    // Same elevation, opposite azimuth: the control has to differ from the sun frame in where
    // it looks and nothing else, so a difference in the reading cannot come from the pitch.
    void aimAwayFromSun() noexcept
    {
        aimAtSun();
        pinAim(std::remainder(pinnedAim_->first + Tau * 0.5F, Tau), pinnedAim_->second);
    }

    // The sun's azimuth, but looking down instead of up. Backlit terrain throws its shadows
    // toward the eye, which puts them in the ground band rather than behind the hills that
    // cast them.
    void aimAcrossSunlitGround() noexcept
    {
        aimAtSun();
        pinAim(pinnedAim_->first, GroundProbePitchRadians);
    }

    void pinAim(float yaw, float pitch) noexcept
    {
        pinnedAim_ = std::pair<float, float>{yaw, pitch};
        yawRadians = yaw;
        pitchRadians = pitch;
    }

    // Pins the sun and applies it at once. The frame being armed here has already run its
    // sun update this phase, so a pin that only took effect from the next frame would land
    // one frame behind the capture it exists to control.
    void pinSun(float phase) noexcept
    {
        pinnedSunPhase_ = phase;
        sunPhaseRadians_ = phase;
    }

    [[nodiscard]] float sunElevation() const noexcept
    {
        constexpr float mid = (SunMaxElevation + SunMinElevation) * 0.5F;
        constexpr float amplitude = (SunMaxElevation - SunMinElevation) * 0.5F;
        return mid + amplitude * std::sin(sunPhaseRadians_);
    }

    // Create-new before destroy-old, then rebind. Retiring a mesh erases its
    // setMesh3DBinding entries, so destroying first would leave the key unbound for
    // the rest of this phase. Both happen inside one non-const phase, so no frame
    // ever observes the gap.
    [[nodiscard]] Tina::Core::Status remeshDirtyChunks(Tina::Render::IRenderDevice& device)
    {
        for (i32 cz = 0; cz < Voxel::WorldChunksZ; ++cz)
        {
            for (i32 cy = 0; cy < Voxel::WorldChunksY; ++cy)
            {
                for (i32 cx = 0; cx < Voxel::WorldChunksX; ++cx)
                {
                    const usize index = Voxel::VoxelWorld::chunkIndex(cx, cy, cz);
                    if (!world_->chunkAt(cx, cy, cz).dirty())
                    {
                        continue;
                    }
                    if (auto status = remeshChunk(device, cx, cy, cz, index); !status)
                    {
                        return status;
                    }
                    world_->chunkAt(cx, cy, cz).clearDirty();
                }
            }
        }
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status remeshChunk(Tina::Render::IRenderDevice& device, i32 cx, i32 cy,
                                                i32 cz, usize index)
    {
        const Voxel::ChunkMeshData mesh = Voxel::generateChunkMesh(*world_, cx, cy, cz);
        ChunkGpu& slot = chunks_[index];
        const u32 meshKey = ChunkMeshKeyBase + static_cast<u32>(index);

        Tina::Render::GpuMeshId uploaded{};
        if (!mesh.empty())
        {
            auto created = device.createStaticMesh(Tina::Render::StaticMeshUploadDesc{
                .vertexCount = mesh.vertexCount(),
                .indexCount = mesh.indexCount(),
                .vertices = std::span<const float>{mesh.vertices},
                .indices = std::span<const u16>{mesh.indices},
            });
            if (!created)
            {
                return Tina::Core::failure(std::move(created.error()));
            }
            uploaded = *created;
            ++counters_->chunkMeshUploads;
            counters_->totalChunkVertices += mesh.vertexCount();
        }

        const Tina::Render::GpuMeshId previous = slot.mesh;
        slot.mesh = uploaded;
        slot.indexCount = mesh.empty() ? 0U : mesh.indexCount();

        if (uploaded)
        {
            if (auto status = device.setMesh3DBinding(meshKey, uploaded); !status)
            {
                return status;
            }
            slot.bound = true;
        }
        else if (slot.bound)
        {
            // A chunk that just became fully enclosed must drop its binding, or the
            // key still resolves to a mesh about to be retired.
            if (auto status = device.setMesh3DBinding(meshKey, Tina::Render::GpuMeshId{}); !status)
            {
                return status;
            }
            slot.bound = false;
        }

        if (previous)
        {
            if (auto status = device.destroyGpuMesh(previous); !status)
            {
                return status;
            }
            ++counters_->chunkMeshDestroys;
        }
        return Tina::Core::success();
    }

    void releaseGpuResources() noexcept
    {
        auto* device = capture_->get();
        if (device == nullptr)
        {
            return;
        }

        // onExit runs after the host has stopped submitting, so a failure here can only
        // be recorded, not recovered from. Keep going so one bad key does not leak the rest.
        const auto record = [this](Tina::Core::Status status) noexcept {
            if (!status && !counters_->shutdownError.has_value())
            {
                counters_->shutdownError = std::move(status.error());
            }
        };

        for (usize index = 0; index < chunks_.size(); ++index)
        {
            ChunkGpu& slot = chunks_[index];
            if (slot.bound)
            {
                record(device->setMesh3DBinding(ChunkMeshKeyBase + static_cast<u32>(index),
                                                Tina::Render::GpuMeshId{}));
                slot.bound = false;
            }
            if (slot.mesh)
            {
                record(device->destroyGpuMesh(slot.mesh));
                slot.mesh = Tina::Render::GpuMeshId{};
            }
            slot.indexCount = 0;
        }

        if (highlightBound_)
        {
            record(device->setMesh3DBinding(HighlightMeshKey, Tina::Render::GpuMeshId{}));
            highlightBound_ = false;
        }
        if (highlightMesh_)
        {
            record(device->destroyGpuMesh(highlightMesh_));
            highlightMesh_ = Tina::Render::GpuMeshId{};
        }
        if (blockMaterialBound_)
        {
            record(device->setMesh3DMaterialBinding(BlockMaterialKey,
                                                    Tina::Render::Mesh3DMaterialBindingDesc{}));
            blockMaterialBound_ = false;
        }
        if (highlightMaterialBound_)
        {
            record(device->setMesh3DMaterialBinding(HighlightMaterialKey,
                                                    Tina::Render::Mesh3DMaterialBindingDesc{}));
            highlightMaterialBound_ = false;
        }
        if (sunBound_)
        {
            record(device->setMesh3DBinding(SunMeshKey, Tina::Render::GpuMeshId{}));
            sunBound_ = false;
        }
        if (sunMesh_)
        {
            record(device->destroyGpuMesh(sunMesh_));
            sunMesh_ = Tina::Render::GpuMeshId{};
        }
        if (sunMaterialBound_)
        {
            record(device->setMesh3DMaterialBinding(SunMaterialKey,
                                                    Tina::Render::Mesh3DMaterialBindingDesc{}));
            sunMaterialBound_ = false;
        }
        if (atlasTexture_)
        {
            record(device->destroyTexture2D(atlasTexture_));
            atlasTexture_ = Tina::Render::GpuTextureId{};
        }
    }

  public:
    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_->frames;

        // The window is created already locked via PrimaryWindowConfig::pointerCapture,
        // so this only records what the window actually granted. A backend that refused
        // leaves the camera unusable, and that must show up in the counters rather than
        // look like a dead mouse.
        counters_->cursorLocked =
            context.pointerCaptureSettings().mode() == Tina::Platform::PointerCaptureMode::Locked;

        const Tina::FrameActionSnapshot& actions = context.frameActions();

        if (actions.pointerLookDeltaX != 0.0 || actions.pointerLookDeltaY != 0.0)
        {
            ++counters_->lookDeltaFrames;
        }
        yawRadians -= static_cast<float>(actions.pointerLookDeltaX) * LookRadiansPerLogicalUnit;
        pitchRadians -= static_cast<float>(actions.pointerLookDeltaY) * LookRadiansPerLogicalUnit;
        pitchRadians = std::clamp(pitchRadians, -MaxPitchRadians, MaxPitchRadians);

        // Held against look input during the sun-disc capture: the pair only means anything if
        // the camera is exactly where the stage put it.
        if (pinnedAim_.has_value())
        {
            yawRadians = pinnedAim_->first;
            pitchRadians = pinnedAim_->second;
        }
        yawRadians = std::remainder(yawRadians, Tau);

        // Movement intent is resolved here rather than in fixedUpdate because it is
        // camera-relative, and yaw only exists in the frame domain: it comes from
        // pointer delta, which no simulation snapshot carries.
        moveIntentForward_ = (actions.isActive(MoveForwardAction) ? 1.0F : 0.0F) -
                             (actions.isActive(MoveBackAction) ? 1.0F : 0.0F);
        moveIntentRight_ = (actions.isActive(MoveRightAction) ? 1.0F : 0.0F) -
                           (actions.isActive(MoveLeftAction) ? 1.0F : 0.0F);
        jumpHeld_ = actions.isActive(JumpAction);

        if (actions.isActive(SelectGrassAction))
        {
            selectedBlock_ = Voxel::BlockType::Grass;
        }
        else if (actions.isActive(SelectDirtAction))
        {
            selectedBlock_ = Voxel::BlockType::Dirt;
        }
        else if (actions.isActive(SelectStoneAction))
        {
            selectedBlock_ = Voxel::BlockType::Stone;
        }
        else if (actions.isActive(SelectPlanksAction))
        {
            selectedBlock_ = Voxel::BlockType::Planks;
        }

        // Edits are counted on the press edge, not on held state, or one click would
        // dig a tunnel. Started is the only edge that means "pressed this frame";
        // ValueChanged fires for analog movement within a hold.
        for (const Tina::FrameActionTransition& transition : actions.transitions)
        {
            const auto* edge = std::get_if<Tina::InputActionTransition>(&transition);
            if (edge == nullptr || edge->kind != Tina::InputActionTransitionKind::Started)
            {
                continue;
            }
            if (edge->action == BreakAction)
            {
                ++pendingBreaks_;
                ++counters_->breakRequests;
            }
            else if (edge->action == PlaceAction)
            {
                ++pendingPlaces_;
                ++counters_->placeRequests;
            }
            else if (edge->action == ToggleTorchAction)
            {
                torchOn_ = !torchOn_;
            }
            else if (edge->action == ToggleShadowsAction)
            {
                shadowsOn_ = !shadowsOn_;
            }
            else if (edge->action == ExitAction)
            {
                // Escape has to release the cursor as well as ask for the exit. The
                // request only takes effect after this frame, and a locked cursor with
                // the window already closing leaves the pointer trapped for that frame.
                //
                // A refused unlock is recorded rather than propagated: failing the phase
                // would turn "the backend would not release the cursor" into "the sample
                // could not quit", which is the worse outcome of the two.
                if (auto released =
                        context.pointerCaptureSettings().setMode(Tina::Platform::PointerCaptureMode::Free);
                    !released)
                {
                    counters_->cursorReleaseFailed = true;
                }
                counters_->exitRequestedByKey = true;
                context.requestExitAfterFrame();
            }
        }

        if (torchOn_)
        {
            ++counters_->torchOnFrames;
        }

        // The sun advances on real time, not on fixed steps: it is presentation, so a
        // frame that accumulated no simulation step should still move it.
        //
        // The capture sequence overrides it in two ways. The torch pair needs it held
        // wherever it happens to be, so the point light is the only difference; the sky
        // pair needs it at two specific phases, so the clear colour is. Both are the same
        // requirement: a captured pair must differ by one variable.
        if (pinnedSunPhase_.has_value())
        {
            sunPhaseRadians_ = *pinnedSunPhase_;
        }
        else if (captureStage_ == CaptureStage::Idle)
        {
            const float rate = actions.isActive(FastTimeAction) ? FastTimeMultiplier : 1.0F;
            sunPhaseRadians_ = std::remainder(
                sunPhaseRadians_ +
                    static_cast<float>(context.frameTiming().realDelta.count()) * rate * Tau /
                        DayCycleSeconds,
                Tau);
        }

        // One scripted break and one scripted place through the same queue a click
        // uses, so an automated run exercises the mechanic rather than just the camera.
        //
        // Aim at the floor first. The default orientation is level, and a level ray at
        // eye height leaves the reach distance through open air on flat terrain, so a
        // self-test that kept the starting pitch would only ever record a raycast miss.
        // The block under the player's feet is the one target guaranteed to exist,
        // because the player is standing on it.
        if (options_->selfTestEdits && !selfTestDone_ && counters_->frames >= SelfTestFrame)
        {
            pitchRadians = -MaxPitchRadians;
            ++pendingBreaks_;
            ++counters_->breakRequests;
            ++pendingPlaces_;
            ++counters_->placeRequests;
            selfTestDone_ = true;
        }

        // Toggled after the edits so a scripted run covers both the torch-off and
        // torch-on point-light spans rather than only one of them.
        if (options_->selfTestEdits && !options_->captureLuma && !selfTestTorchDone_ &&
            counters_->frames >= SelfTestTorchFrame)
        {
            torchOn_ = true;
            selfTestTorchDone_ = true;
        }

        // Edits are applied here, not in fixedUpdate, for two reasons: only this phase
        // can reach the render device to remesh what the edit dirtied, and a frame with
        // no accumulated fixed step would otherwise swallow the click entirely.
        applyPendingEdits();

        auto* device = capture_->get();
        if (device == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "render device disappeared mid-run");
        }
        // Failing the phase is correct rather than harsh: a chunk whose binding no
        // longer matches its blocks would keep drawing the pre-edit geometry.
        if (auto status = remeshDirtyChunks(*device); !status)
        {
            return status;
        }

        advanceCaptureStage(*device);

        // Before the capture stage would read pixels, so a pinned frame shows the colour
        // that belongs to its pinned phase.
        if (auto status = syncSunMaterial(*device); !status)
        {
            return status;
        }

        // Recomputed here so the highlight matches the camera the same frame it is
        // submitted from; extractRenderScene is const and cannot update it.
        aimHit_ = currentAim();

        if (options_->maxFrames != 0 && counters_->frames >= options_->maxFrames)
        {
            context.requestExitAfterFrame();
        }
        return Tina::Core::success();
    }

    Tina::Core::Status fixedUpdate(Tina::FixedUpdateContext& context) override
    {
        ++counters_->fixedSteps;
        const float step = static_cast<float>(context.fixedUpdateTiming().fixedDelta.count());
        if (step > 0.0F)
        {
            moveBody(step);
        }
        return Tina::Core::success();
    }

  private:
    void spawnPlayer() noexcept
    {
        const i32 spawnX = Voxel::WorldBlocksX / 2;
        const i32 spawnZ = Voxel::WorldBlocksZ / 2;
        positionX = static_cast<float>(spawnX) + 0.5F;
        positionZ = static_cast<float>(spawnZ) + 0.5F;
        // One block of clearance, so the first frame is a short fall rather than a
        // body already intersecting the surface.
        positionY = static_cast<float>(Voxel::terrainHeight(spawnX, spawnZ, options_->worldSeed) + 2);
        velocityY = 0.0F;
    }

    [[nodiscard]] std::optional<Voxel::VoxelRaycastHit> currentAim() const noexcept
    {
        const float cosPitch = std::cos(pitchRadians);
        const float dirX = -std::sin(yawRadians) * cosPitch;
        const float dirY = std::sin(pitchRadians);
        const float dirZ = -std::cos(yawRadians) * cosPitch;
        return Voxel::raycastVoxel(*world_, positionX, positionY + PlayerEyeHeight, positionZ, dirX,
                                   dirY, dirZ, ReachMeters);
    }

    void applyPendingEdits() noexcept
    {
        while (pendingBreaks_ > 0)
        {
            --pendingBreaks_;
            const std::optional<Voxel::VoxelRaycastHit> hit = currentAim();
            if (!hit.has_value())
            {
                ++counters_->raycastMisses;
                continue;
            }
            world_->setBlock(hit->blockX, hit->blockY, hit->blockZ, Voxel::BlockType::Air);
            ++counters_->breakApplied;
        }

        while (pendingPlaces_ > 0)
        {
            --pendingPlaces_;
            const std::optional<Voxel::VoxelRaycastHit> hit = currentAim();
            if (!hit.has_value())
            {
                ++counters_->raycastMisses;
                continue;
            }
            // A ray that started inside a solid block has no entry face, so there is
            // no adjacent cell to place into.
            if (!hit->hasFace())
            {
                ++counters_->placeRejectedNoFace;
                continue;
            }
            const i32 targetX = hit->blockX + hit->faceX;
            const i32 targetY = hit->blockY + hit->faceY;
            const i32 targetZ = hit->blockZ + hit->faceZ;
            if (!Voxel::VoxelWorld::inBounds(targetX, targetY, targetZ) ||
                world_->solidAt(targetX, targetY, targetZ))
            {
                ++counters_->placeRejectedOccupied;
                continue;
            }
            // Placing into your own body would trap you inside geometry the sweep can
            // never resolve, because it resolves penetration by refusing motion, not
            // by pushing out.
            if (overlapsPlayer(targetX, targetY, targetZ))
            {
                ++counters_->placeRejectedInsidePlayer;
                continue;
            }
            world_->setBlock(targetX, targetY, targetZ, selectedBlock_);
            ++counters_->placeApplied;
        }
    }

    [[nodiscard]] bool overlapsPlayer(i32 blockX, i32 blockY, i32 blockZ) const noexcept
    {
        const float minX = positionX - PlayerHalfWidth;
        const float maxX = positionX + PlayerHalfWidth;
        const float minZ = positionZ - PlayerHalfWidth;
        const float maxZ = positionZ + PlayerHalfWidth;
        const auto low = [](i32 value) noexcept { return static_cast<float>(value); };
        const auto high = [](i32 value) noexcept { return static_cast<float>(value + 1); };
        return maxX > low(blockX) && minX < high(blockX) && positionY + PlayerHeight > low(blockY) &&
               positionY < high(blockY) && maxZ > low(blockZ) && minZ < high(blockZ);
    }

    // Per-axis sweep. Moving all three axes at once and then resolving would let a
    // diagonal step slip through a shared block edge; resolving one axis at a time
    // cannot, and it is also what produces wall-sliding for free.
    void moveBody(float step) noexcept
    {
        const float horizontalForwardX = -std::sin(yawRadians);
        const float horizontalForwardZ = -std::cos(yawRadians);
        const float rightX = -horizontalForwardZ;
        const float rightZ = horizontalForwardX;

        float wishX = horizontalForwardX * moveIntentForward_ + rightX * moveIntentRight_;
        float wishZ = horizontalForwardZ * moveIntentForward_ + rightZ * moveIntentRight_;
        // Diagonal input is two unit vectors summed, which would otherwise be sqrt(2)
        // times faster than walking straight.
        const float wishLength = std::sqrt(wishX * wishX + wishZ * wishZ);
        if (wishLength > 1.0F)
        {
            wishX /= wishLength;
            wishZ /= wishLength;
        }

        if (jumpHeld_ && grounded_)
        {
            velocityY = JumpSpeed;
            grounded_ = false;
        }
        velocityY = std::max(velocityY - Gravity * step, -MaxFallSpeed);

        // A refused horizontal axis needs no handling: leaving the other axis to apply
        // on its own is exactly wall-sliding. Only the vertical result is consumed,
        // because that is what distinguishes landing from a ceiling.
        static_cast<void>(moveAxis(positionX, wishX * MoveSpeed * step));
        static_cast<void>(moveAxis(positionZ, wishZ * MoveSpeed * step));

        const float verticalDelta = velocityY * step;
        const bool blocked = !moveAxis(positionY, verticalDelta);
        if (blocked)
        {
            // Landing and hitting a ceiling are the same collision; only the sign of
            // the attempted motion says which, and only landing grounds the body.
            grounded_ = verticalDelta < 0.0F;
            velocityY = 0.0F;
            if (grounded_)
            {
                counters_->playerEverGrounded = true;
            }
        }
        else if (verticalDelta != 0.0F)
        {
            grounded_ = false;
        }
    }

    // Returns false when the motion was refused. Applies the full delta or none of
    // it: a block game does not need contact-exact placement, and stopping short by
    // a hair is what makes repeated frames creep into geometry.
    [[nodiscard]] bool moveAxis(float& coordinate, float delta) noexcept
    {
        if (delta == 0.0F)
        {
            return true;
        }
        const float original = coordinate;
        coordinate += delta;
        if (!bodyFits())
        {
            coordinate = original;
            return false;
        }
        return true;
    }

    [[nodiscard]] bool bodyFits() const noexcept
    {
        const auto floorToInt = [](float value) noexcept {
            return static_cast<i32>(std::floor(value));
        };
        // Half-open on the maximum edge: a body whose face sits exactly on x=8.0 must
        // not test the block starting at 8, or standing flush against a wall reads as
        // a collision and movement locks up.
        const i32 minX = floorToInt(positionX - PlayerHalfWidth);
        const i32 maxX = floorToInt(std::nextafter(positionX + PlayerHalfWidth, 0.0F));
        const i32 minZ = floorToInt(positionZ - PlayerHalfWidth);
        const i32 maxZ = floorToInt(std::nextafter(positionZ + PlayerHalfWidth, 0.0F));
        const i32 minY = floorToInt(positionY);
        const i32 maxY = floorToInt(std::nextafter(positionY + PlayerHeight, 0.0F));

        for (i32 y = minY; y <= maxY; ++y)
        {
            for (i32 z = minZ; z <= maxZ; ++z)
            {
                for (i32 x = minX; x <= maxX; ++x)
                {
                    // The world's vertical faces are open air, but its floor and walls
                    // must not be walkable-through, or the body falls out of the grid
                    // and every later query reads Air.
                    if (!Voxel::VoxelWorld::inBounds(x, y, z))
                    {
                        if (y < 0 || x < 0 || z < 0 || x >= Voxel::WorldBlocksX ||
                            z >= Voxel::WorldBlocksZ)
                        {
                            return false;
                        }
                        continue;
                    }
                    if (world_->solidAt(x, y, z))
                    {
                        return false;
                    }
                }
            }
        }
        return true;
    }

  public:
    Tina::Core::Status extractRenderScene(Tina::RenderSceneExtractionContext& context) const override
    {
        Tina::Render::RenderSceneWriter& writer = context.renderSceneWriter();
        Tina::Render::FrameResourceSink& resources = context.frameResourceSink();

        if (auto status = writeCamera(writer); !status)
        {
            return status;
        }
        if (auto status = writeSkyAndLighting(writer); !status)
        {
            return status;
        }
        ++counters_->lightingWrites;
        const float elevation = sunElevation();
        counters_->sunElevationMin = std::min(counters_->sunElevationMin, elevation);
        counters_->sunElevationMax = std::max(counters_->sunElevationMax, elevation);

        auto material = internStaticResource(resources, Tina::Render::FrameResourceKind::Mesh3DMaterial,
                                            BlockMaterialKey);
        if (!material)
        {
            return Tina::Core::failure(std::move(material.error()));
        }

        u32 submitted = 0;
        for (usize index = 0; index < chunks_.size(); ++index)
        {
            const ChunkGpu& slot = chunks_[index];
            if (!slot.bound)
            {
                continue;
            }
            auto mesh = internStaticResource(resources, Tina::Render::FrameResourceKind::Mesh3DGeometry,
                                             ChunkMeshKeyBase + static_cast<u64>(index));
            if (!mesh)
            {
                return Tina::Core::failure(std::move(mesh.error()));
            }

            const i32 chunkX = static_cast<i32>(index % static_cast<usize>(Voxel::WorldChunksX));
            const i32 chunkY = static_cast<i32>((index / static_cast<usize>(Voxel::WorldChunksX)) %
                                                static_cast<usize>(Voxel::WorldChunksY));
            const i32 chunkZ = static_cast<i32>(index / static_cast<usize>(Voxel::WorldChunksX *
                                                                          Voxel::WorldChunksY));
            // Chunk meshes are emitted in chunk-local space, so the world transform is
            // the chunk origin and the bounds centre is the chunk middle.
            const float originX = static_cast<float>(chunkX * Voxel::ChunkSize);
            const float originY = static_cast<float>(chunkY * Voxel::ChunkSize);
            const float originZ = static_cast<float>(chunkZ * Voxel::ChunkSize);
            constexpr float half = static_cast<float>(Voxel::ChunkSize) * 0.5F;

            if (auto status = writer.addMesh3D(Tina::Render::RenderMesh3DInput{
                    .mesh = *mesh,
                    .material = *material,
                    .stableEntityKey = ChunkMeshKeyBase + static_cast<u64>(index),
                    .worldTransform =
                        Tina::Render::RenderTransform3DInput{
                            .pose = Tina::Render::RenderPose3DInput{.positionX = originX,
                                                                    .positionY = originY,
                                                                    .positionZ = originZ},
                        },
                    .localBounds = Tina::Render::RenderBoundingSphereInput{.centerX = half,
                                                                            .centerY = half,
                                                                            .centerZ = half,
                                                                            .radius = ChunkBoundsRadius},
                    .alphaMode = Tina::Render::Mesh3DAlphaMode::Opaque,
                });
                !status)
            {
                return status;
            }
            ++submitted;
        }
        counters_->lastSubmittedChunks = submitted;

        if (auto status = writeSun(writer, resources); !status)
        {
            return status;
        }
        return writeHighlight(writer, resources);
    }

  private:
    [[nodiscard]] Tina::Core::Status writeCamera(Tina::Render::RenderSceneWriter& writer) const
    {
        // Yaw about world +Y, then pitch about the yawed local +X. Composed as
        // qYaw * qPitch so pitch stays camera-relative; the reverse order would tilt
        // the horizon as soon as yaw left zero.
        const float halfYaw = yawRadians * 0.5F;
        const float halfPitch = pitchRadians * 0.5F;
        const float sinYaw = std::sin(halfYaw);
        const float cosYaw = std::cos(halfYaw);
        const float sinPitch = std::sin(halfPitch);
        const float cosPitch = std::cos(halfPitch);

        return writer.setPerspectiveCamera(Tina::Render::RenderPerspectiveCameraInput{
            .stableCameraKey = 1,
            .worldPose =
                Tina::Render::RenderPose3DInput{
                    .positionX = positionX,
                    .positionY = positionY + PlayerEyeHeight,
                    .positionZ = positionZ,
                    .rotationX = cosYaw * sinPitch,
                    .rotationY = sinYaw * cosPitch,
                    .rotationZ = -sinYaw * sinPitch,
                    .rotationW = cosYaw * cosPitch,
                },
            .verticalFovDegrees = 70.0F,
            .nearPlaneMeters = 0.1F,
            // The far plane only has to clear the world diagonal; a larger one just
            // spends depth precision on empty space.
            .farPlaneMeters = 320.0F,
        });
    }

    // Drawn at a fixed offset from the eye along the direction the sunlight arrives from, so
    // the disc and the shading always agree about where the sun is. Following the eye also
    // means walking cannot approach it, which is the point: it stands in for something far
    // enough away that parallax does not apply.
    //
    // No billboard rotation because it is a sphere, and no lighting concerns because it is
    // emissive (ADR 0043) — the identity pose is correct.
    [[nodiscard]] Tina::Core::Status writeSun(Tina::Render::RenderSceneWriter& writer,
                                              Tina::Render::FrameResourceSink& resources) const
    {
        if (!sunBound_ || !sunMaterialBound_)
        {
            return Tina::Core::success();
        }
        auto mesh = internStaticResource(resources, Tina::Render::FrameResourceKind::Mesh3DGeometry,
                                         SunMeshKey);
        if (!mesh)
        {
            return Tina::Core::failure(std::move(mesh.error()));
        }
        auto material = internStaticResource(resources, Tina::Render::FrameResourceKind::Mesh3DMaterial,
                                            SunMaterialKey);
        if (!material)
        {
            return Tina::Core::failure(std::move(material.error()));
        }

        const float elevation = sunElevation();
        const float horizontal = std::sqrt(std::max(1.0F - elevation * elevation, 0.0F));
        const float dirX = std::cos(sunPhaseRadians_) * horizontal;
        const float dirZ = std::sin(sunPhaseRadians_) * horizontal;

        return writer.addMesh3D(Tina::Render::RenderMesh3DInput{
            .mesh = *mesh,
            .material = *material,
            .stableEntityKey = SunStableKey,
            .worldTransform =
                Tina::Render::RenderTransform3DInput{
                    .pose =
                        Tina::Render::RenderPose3DInput{
                            .positionX = positionX + dirX * SunDistanceMeters,
                            .positionY = positionY + PlayerEyeHeight + elevation * SunDistanceMeters,
                            .positionZ = positionZ + dirZ * SunDistanceMeters,
                        },
                    .scaleX = SunRadiusMeters,
                    .scaleY = SunRadiusMeters,
                    .scaleZ = SunRadiusMeters,
                },
            // Unit sphere at the origin, so the local bounds are the sphere itself. Scale is
            // uniform, so the culler's radius stays exact.
            .localBounds = Tina::Render::RenderBoundingSphereInput{.radius = 1.0F},
            .alphaMode = Tina::Render::Mesh3DAlphaMode::Opaque,
        });
    }

    [[nodiscard]] Tina::Core::Status writeHighlight(Tina::Render::RenderSceneWriter& writer,
                                                    Tina::Render::FrameResourceSink& resources) const
    {
        if (!aimHit_.has_value() || !highlightBound_)
        {
            return Tina::Core::success();
        }

        auto mesh = internStaticResource(resources, Tina::Render::FrameResourceKind::Mesh3DGeometry,
                                         HighlightMeshKey);
        if (!mesh)
        {
            return Tina::Core::failure(std::move(mesh.error()));
        }
        auto material = internStaticResource(resources, Tina::Render::FrameResourceKind::Mesh3DMaterial,
                                             HighlightMaterialKey);
        if (!material)
        {
            return Tina::Core::failure(std::move(material.error()));
        }

        // Slightly larger than the block and offset by half the growth, so the shell
        // surrounds the target instead of z-fighting its faces.
        constexpr float grow = 0.012F;
        constexpr float scale = 1.0F + (grow * 2.0F);
        return writer.addMesh3D(Tina::Render::RenderMesh3DInput{
            .mesh = *mesh,
            .material = *material,
            .stableEntityKey = HighlightStableKey,
            .worldTransform =
                Tina::Render::RenderTransform3DInput{
                    .pose =
                        Tina::Render::RenderPose3DInput{
                            .positionX = static_cast<float>(aimHit_->blockX) - grow,
                            .positionY = static_cast<float>(aimHit_->blockY) - grow,
                            .positionZ = static_cast<float>(aimHit_->blockZ) - grow,
                        },
                    .scaleX = scale,
                    .scaleY = scale,
                    .scaleZ = scale,
                },
            .localBounds = Tina::Render::RenderBoundingSphereInput{.centerX = 0.5F,
                                                                   .centerY = 0.5F,
                                                                   .centerZ = 0.5F,
                                                                   .radius = 0.9F},
            .baseColorFactor = Tina::Render::RenderLinearColor{.red = 0.05F,
                                                                .green = 0.05F,
                                                                .blue = 0.05F,
                                                                .alpha = 0.35F},
            .alphaMode = Tina::Render::Mesh3DAlphaMode::Blend,
        });
    }

    const SampleOptions* options_ = nullptr;
    RunCounters* counters_ = nullptr;
    Voxel::DeviceCapture* capture_ = nullptr;
    std::optional<Voxel::VoxelWorld> world_{};
    std::vector<ChunkGpu> chunks_{};
    Tina::Render::GpuTextureId atlasTexture_{};
    Tina::Render::GpuMeshId highlightMesh_{};
    u32 highlightIndexCount_ = 0;
    bool blockMaterialBound_ = false;
    bool highlightMaterialBound_ = false;
    bool highlightBound_ = false;
    Tina::Render::GpuMeshId sunMesh_{};
    bool sunBound_ = false;
    bool sunMaterialBound_ = false;
    // Last colour pushed to the device, so an unchanged sun does not rebind.
    Tina::Render::Mesh3DMaterialBindingDesc sunMaterialDesc_{};

    // Camera and body. Position is the AABB centre on X/Z and its base on Y.
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float velocityY = 0.0F;
    float yawRadians = 0.0F;
    float pitchRadians = 0.0F;
    bool grounded_ = false;

    float moveIntentForward_ = 0.0F;
    float moveIntentRight_ = 0.0F;
    bool jumpHeld_ = false;
    Voxel::BlockType selectedBlock_ = Voxel::BlockType::Planks;
    u32 pendingBreaks_ = 0;
    u32 pendingPlaces_ = 0;
    enum class CaptureStage : u8 {
        Idle,
        AwaitTorchOff,
        AwaitTorchOn,
        AwaitDawnSky,
        AwaitNoonSky,
        AwaitSunDisc,
        AwaitSunAway,
        AwaitShadowOn,
        AwaitShadowOff,
        Done,
    };
    CaptureStage captureStage_ = CaptureStage::Idle;
    bool selfTestDone_ = false;
    bool selfTestTorchDone_ = false;
    bool torchOn_ = false;
    bool shadowsOn_ = true;
    // Held only between the two frames of the shadow pair, then released.
    std::vector<u8> shadowOnBandLuma_{};
    // Starts near dawn so the first frames already show a low, warm sun rather than
    // opening on the flattest part of the cycle.
    float sunPhaseRadians_ = 0.35F;
    // Set only by the capture sequence, which needs two frames that differ by the sky
    // alone. A moving sun would change the ground between them too.
    std::optional<float> pinnedSunPhase_{};
    // Yaw, pitch. Set only by the sun-disc capture, which must aim exactly at the disc.
    std::optional<std::pair<float, float>> pinnedAim_{};
    std::optional<Voxel::VoxelRaycastHit> aimHit_{};
};

class VoxelApplication final : public Tina::IGameApplication {
  public:
    VoxelApplication(const SampleOptions& options, RunCounters& counters,
                     Voxel::DeviceCapture& capture) noexcept
        : options_(&options), counters_(&counters), capture_(&capture)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>>
    createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{
            std::make_unique<VoxelState>(*options_, *counters_, *capture_)};
    }

  private:
    const SampleOptions* options_ = nullptr;
    RunCounters* counters_ = nullptr;
    Voxel::DeviceCapture* capture_ = nullptr;
};

[[nodiscard]] Tina::EngineConfig createEngineConfig(const SampleOptions& options)
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina Voxel Sample";
    config.primaryWindow.title =
        "Tina 3D Voxel — WASD/Space move, LMB break, RMB place, 1-4 block, F torch, T fast time, "
        "Esc quit";
    config.primaryWindow.initialLogicalExtent = {options.windowLogicalWidth,
                                                 options.windowLogicalHeight};
    config.primaryWindow.initiallyVisible = true;
    // Locked from creation rather than on frame 1, so the run never shows a frame with
    // a free cursor, and pointerLookDelta keeps accumulating past the screen edge.
    config.primaryWindow.pointerCapture = Tina::Platform::PointerCaptureMode::Locked;
    // Capacity is left at its default: one draw per non-empty chunk plus the highlight
    // is at most WorldChunkCount + 1, well under DefaultMesh3DItemCapacity.
    static_assert(Voxel::WorldChunkCount + 1 <
                  static_cast<int>(Tina::Render::RenderSceneCapacity::DefaultMesh3DItemCapacity));

    // Every action is Frame domain. The aim ray comes from the camera centre, so none
    // of these needs the worldPointerSample that only Simulation actions carry, and
    // movement has to be resolved beside the yaw that only exists in this domain.
    const auto bindKey = [&config](Tina::Platform::Key key, Tina::InputActionId action) {
        config.inputActions.bindings.push_back(Tina::InputActionBinding{
            .input = Tina::PrimaryWindowKeyBinding{.key = key},
            .action = action,
            .domain = Tina::InputActionDomain::Frame,
        });
    };
    bindKey(Tina::Platform::Key::W, MoveForwardAction);
    bindKey(Tina::Platform::Key::Up, MoveForwardAction);
    bindKey(Tina::Platform::Key::S, MoveBackAction);
    bindKey(Tina::Platform::Key::Down, MoveBackAction);
    bindKey(Tina::Platform::Key::A, MoveLeftAction);
    bindKey(Tina::Platform::Key::Left, MoveLeftAction);
    bindKey(Tina::Platform::Key::D, MoveRightAction);
    bindKey(Tina::Platform::Key::Right, MoveRightAction);
    bindKey(Tina::Platform::Key::Space, JumpAction);
    bindKey(Tina::Platform::Key::Digit1, SelectGrassAction);
    bindKey(Tina::Platform::Key::Digit2, SelectDirtAction);
    bindKey(Tina::Platform::Key::Digit3, SelectStoneAction);
    bindKey(Tina::Platform::Key::Digit4, SelectPlanksAction);
    bindKey(Tina::Platform::Key::F, ToggleTorchAction);
    bindKey(Tina::Platform::Key::Escape, ExitAction);
    bindKey(Tina::Platform::Key::T, FastTimeAction);
    bindKey(Tina::Platform::Key::G, ToggleShadowsAction);

    const auto bindPointer = [&config](Tina::Platform::PointerButton button,
                                       Tina::InputActionId action) {
        config.inputActions.bindings.push_back(Tina::InputActionBinding{
            .input = Tina::PointerButtonBinding{.pointer = Tina::Platform::PrimaryPointerId,
                                                .button = button},
            .action = action,
            .domain = Tina::InputActionDomain::Frame,
        });
    };
    bindPointer(Tina::Platform::PointerButton::Primary, BreakAction);
    bindPointer(Tina::Platform::PointerButton::Secondary, PlaceAction);
    return config;
}

} // namespace

int main(int argc, char** argv)
{
    auto parsedOptions = parseOptions(argc, argv);
    if (!parsedOptions)
    {
        writeError(parsedOptions.error());
        return 1;
    }
    const SampleOptions options = *parsedOptions;
    RunCounters counters{};
    Voxel::DeviceCapture capture{};

    Tina::Desktop::CreateEngineOptions desktopOptions{};
    auto uiFont = Tina::Desktop::resolveUiFontBytes();
    if (!uiFont)
    {
        writeError(uiFont.error());
        return 1;
    }
    desktopOptions.uiFontBytes = std::move(uiFont->bytes);
    desktopOptions.wrapWindowSurfaceRenderDevice =
        [&capture](std::unique_ptr<Tina::Render::IRenderDevice> device)
            -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
        return Voxel::captureRenderDevice(std::move(device), capture);
    };

    auto host = Tina::Desktop::CreateEngine(createEngineConfig(options), std::move(desktopOptions));
    if (!host)
    {
        writeError(host.error());
        return 1;
    }

    VoxelApplication application{options, counters, capture};
    auto run = (*host)->run(application);
    if (!run)
    {
        writeError(run.error());
        return 1;
    }
    if (counters.shutdownError.has_value())
    {
        writeError(*counters.shutdownError);
        return 1;
    }

    std::printf("world=%dx%dx%d chunks=%d non_empty_chunks=%u chunk_vertices=%llu\n",
                Voxel::WorldBlocksX, Voxel::WorldBlocksY, Voxel::WorldBlocksZ,
                Voxel::WorldChunkCount, counters.nonEmptyChunks,
                static_cast<unsigned long long>(counters.totalChunkVertices));
    std::printf("frames=%llu fixed_steps=%llu mesh_uploads=%llu mesh_destroys=%llu submitted=%llu\n",
                static_cast<unsigned long long>(counters.frames),
                static_cast<unsigned long long>(counters.fixedSteps),
                static_cast<unsigned long long>(counters.chunkMeshUploads),
                static_cast<unsigned long long>(counters.chunkMeshDestroys),
                static_cast<unsigned long long>(counters.lastSubmittedChunks));
    std::printf("break=%llu/%llu place=%llu/%llu no_face=%llu occupied=%llu inside_player=%llu\n",
                static_cast<unsigned long long>(counters.breakApplied),
                static_cast<unsigned long long>(counters.breakRequests),
                static_cast<unsigned long long>(counters.placeApplied),
                static_cast<unsigned long long>(counters.placeRequests),
                static_cast<unsigned long long>(counters.placeRejectedNoFace),
                static_cast<unsigned long long>(counters.placeRejectedOccupied),
                static_cast<unsigned long long>(counters.placeRejectedInsidePlayer));
    std::printf("raycast_misses=%llu look_delta_frames=%llu cursor_locked=%d grounded=%d\n",
                static_cast<unsigned long long>(counters.raycastMisses),
                static_cast<unsigned long long>(counters.lookDeltaFrames),
                counters.cursorLocked ? 1 : 0, counters.playerEverGrounded ? 1 : 0);
    std::printf("sun_material_rebinds=%llu\n",
                static_cast<unsigned long long>(counters.sunMaterialRebinds));
    std::printf("lighting_writes=%llu torch_on_frames=%llu sun_elevation=%.3f..%.3f exit_key=%d "
                "cursor_release_failed=%d\n",
                static_cast<unsigned long long>(counters.lightingWrites),
                static_cast<unsigned long long>(counters.torchOnFrames),
                static_cast<double>(counters.sunElevationMin),
                static_cast<double>(counters.sunElevationMax), counters.exitRequestedByKey ? 1 : 0,
                counters.cursorReleaseFailed ? 1 : 0);
    if (counters.captureError.has_value())
    {
        std::printf("capture_error=%s\n", counters.captureError->message.c_str());
    }
    for (const auto& [label, stats] :
         std::initializer_list<std::pair<const char*, const LumaStats*>>{
             {"torch_off", &counters.torchOffLuma}, {"torch_on", &counters.torchOnLuma}})
    {
        if (!stats->valid)
        {
            continue;
        }
        std::printf("%s_luma size=%ux%u levels=%u range=%u..%u mean=%.2f\n", label, stats->width,
                    stats->height, stats->distinctLevels, static_cast<unsigned>(stats->minimum),
                    static_cast<unsigned>(stats->maximum), stats->mean);
    }
    // Printed as a pair, not individually: a single sky reading proves nothing, and the
    // delta between two pinned sun phases is the whole evidence for ADR 0042.
    if (counters.dawnSky.valid && counters.noonSky.valid)
    {
        std::printf("sky_dawn=%.2f,%.2f,%.2f sky_noon=%.2f,%.2f,%.2f sky_delta=%.2f,%.2f,%.2f\n",
                    counters.dawnSky.red, counters.dawnSky.green, counters.dawnSky.blue,
                    counters.noonSky.red, counters.noonSky.green, counters.noonSky.blue,
                    counters.noonSky.red - counters.dawnSky.red,
                    counters.noonSky.green - counters.dawnSky.green,
                    counters.noonSky.blue - counters.dawnSky.blue);
        std::printf("sky_peak_dawn=%u sky_peak_noon=%u\n",
                    static_cast<unsigned>(counters.dawnSky.peak),
                    static_cast<unsigned>(counters.noonSky.peak));
    }
    // ADR 0043. Aimed at the disc versus aimed at the opposite sky, same sun, same elevation.
    if (counters.sunDisc.valid && counters.sunAway.valid)
    {
        std::printf("sun_disc=%.2f,%.2f,%.2f range=%u..%u  sun_away=%.2f,%.2f,%.2f range=%u..%u\n",
                    counters.sunDisc.red, counters.sunDisc.green, counters.sunDisc.blue,
                    static_cast<unsigned>(counters.sunDisc.floorValue),
                    static_cast<unsigned>(counters.sunDisc.peak), counters.sunAway.red,
                    counters.sunAway.green, counters.sunAway.blue,
                    static_cast<unsigned>(counters.sunAway.floorValue),
                    static_cast<unsigned>(counters.sunAway.peak));
        std::printf("sun_disc_center=%u,%u,%u sun_away_center=%u,%u,%u\n",
                    static_cast<unsigned>(counters.sunDisc.centerRed),
                    static_cast<unsigned>(counters.sunDisc.centerGreen),
                    static_cast<unsigned>(counters.sunDisc.centerBlue),
                    static_cast<unsigned>(counters.sunAway.centerRed),
                    static_cast<unsigned>(counters.sunAway.centerGreen),
                    static_cast<unsigned>(counters.sunAway.centerBlue));
    }
    // Cascaded directional shadows. Both frames share a camera and a pinned sun, so
    // shadow_delta is attributable to the cascade alone; dark_frac is what separates real
    // occlusion from the whole band merely dimming.
    if (counters.shadowOn.valid && counters.shadowOff.valid)
    {
        std::printf("shadow_on_luma=%.2f shadow_off_luma=%.2f shadow_delta=%.2f\n",
                    counters.shadowOn.luma, counters.shadowOff.luma,
                    counters.shadowOff.luma - counters.shadowOn.luma);
        std::printf("shadow_on_range=%u..%u shadow_off_range=%u..%u\n",
                    static_cast<unsigned>(counters.shadowOn.minLuma),
                    static_cast<unsigned>(counters.shadowOn.maxLuma),
                    static_cast<unsigned>(counters.shadowOff.minLuma),
                    static_cast<unsigned>(counters.shadowOff.maxLuma));
    }
    if (counters.shadowDiff.valid)
    {
        // brightened is the invariant: enabling shadows can only subtract light, so a
        // non-zero count here means the pair does not actually share a camera and sun.
        std::printf("shadow_darkened=%llu shadow_brightened=%llu shadow_unchanged=%llu\n",
                    static_cast<unsigned long long>(counters.shadowDiff.darkened),
                    static_cast<unsigned long long>(counters.shadowDiff.brightened),
                    static_cast<unsigned long long>(counters.shadowDiff.unchanged));
        std::printf("shadow_darkened_frac=%.4f shadow_max_darkening=%d shadow_max_brightening=%d\n",
                    counters.shadowDiff.darkenedFraction(),
                    counters.shadowDiff.maximumDarkening,
                    counters.shadowDiff.maximumBrightening);
    }
    return 0;
}
