#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetGpuTexture.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/Sprite2DBindingRegistry.hpp>
#include <tina/asset/CharacterController2D.hpp>
#include <tina/asset/GridCollision.hpp>
#include <tina/asset/TileChunkDirtyCache.hpp>
#include <tina/asset/TileChunkRender.hpp>
#include <tina/asset/TileMapInstance.hpp>
#include <tina/asset/TileMapStream.hpp>
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
#include <tina/asset/TileMapPhysicsSync.hpp>
#include <tina/physics2d/PhysicsWorld2D.hpp>
#endif
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/asset_format/SpriteAnimationClipPayload.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/core/hash/ContentHash.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/Camera2DProjection.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/platform/Input.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/InputActionMap.hpp>
#include <tina/runtime/InputActions.hpp>
#include <tina/runtime/PlatformEvents.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/audio/AudioClipView.hpp>
#include <tina/audio/AudioEngine.hpp>
#include <tina/scene/Camera2D.hpp>
#include <tina/scene/ExtractRenderScene.hpp>
#include <tina/scene/ParticleSystem2D.hpp>
#include <tina/scene/SpriteAnimator2D.hpp>
#include <tina/scene/SpriteRenderer2D.hpp>
#include <tina/scene/Trail2D.hpp>
#include <tina/scene/World.hpp>
#if defined(TINA_SAMPLE_TILEMAP_AUDIO_MINIAUDIO)
#include <tina/audio/miniaudio/MiniaudioDevice.hpp>
#endif
#include <tina/ui/UIAccessibility.hpp>
#include <tina/ui/UIButton.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UIProgressBar.hpp>
#include <tina/ui/UIRadioButton.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/UITextEdit.hpp>
#include <tina/ui/UITheme.hpp>

#include "AudioControlState.hpp"
#include "DeviceCapture.hpp"
#include "SampleTempDirectory.hpp"
#include "TileSelection.hpp"
#include "WindowVisibilityPacing.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Tina::Core::u16;
using Tina::Core::u32;
using Tina::Core::u64;
using Tina::Core::u8;

inline constexpr u64 DefaultFrameCount = 300;
inline constexpr u32 DefaultFrameDelayMilliseconds = 0;
inline constexpr Tina::AssetFormat::TileMapLayerId VisualTileLayerId = 10;
inline constexpr Tina::AssetFormat::TileMapLayerId CollisionTileLayerId = 20;
inline constexpr Tina::AssetFormat::TileMapLayerId GameplayObjectLayerId = 30;
inline constexpr Tina::AssetFormat::TileMapObjectId PlayerSpawnObjectId = 101;
inline constexpr Tina::AssetFormat::TileMapObjectId CrateSpawnObjectId = 102;
inline constexpr u32 ExpectedCharacterAnimationClips = 3;
inline constexpr u32 ExpectedCharacterAnimationSprites = 3;
inline constexpr u32 ExpectedSceneCrateSprites = 1;
inline constexpr u32 ExpectedCharacterAnimationResolvedFrames = 5;
inline constexpr u32 ExpectedTileMapStreamChunks = 2;
inline constexpr u32 ExpectedAudioClipFrames = 480;
inline constexpr u32 ExpectedCatalogRecipeAssets =
    5 + ExpectedCharacterAnimationClips + ExpectedCharacterAnimationSprites + ExpectedSceneCrateSprites +
    ExpectedTileMapStreamChunks;
inline constexpr u32 ExpectedUploadedTextures = 2;
inline constexpr u32 ExpectedNonEmptyTiles = 11; // 8 floor + 3 wall
inline constexpr u32 ProductParticleCapacity = 12;
inline constexpr u32 ProductParticleEmitted = 10;
inline constexpr u32 ProductParticleExpiredAt300Frames = 4;
inline constexpr u32 ProductParticleActiveAt300Frames = 6;
inline constexpr u64 ProductParticleRandomSeed = 0x54494E41ULL;
inline constexpr u64 ProductParticleStableKeyBase = 0x100000000ULL;
inline constexpr u32 ProductTrailCapacity = 8;
inline constexpr u32 ProductTrailSegments = 3;
inline constexpr u64 ProductTrailStableKeyBase = 0x200000000ULL;
inline constexpr Tina::InputActionId MoveLeftAction{1};
inline constexpr Tina::InputActionId MoveRightAction{2};
inline constexpr Tina::InputActionId SelectTileAction{3};
inline constexpr float DemoWalkSpeedMetersPerSecond = 4.0f;
inline constexpr std::string_view InitialProfileNameText = "玩家名：星河";
inline constexpr u32 InitialProfileNameCodepointCount = 6;

// M11-D0: frame-count-independent product evidence fingerprint (screenshot precursor).
// Only stable structural counters/flags are hashed —not per-frame animation values.
[[nodiscard]] std::string contentHashToHex(const Tina::Core::ContentHash& hash)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(32);
    const auto& bytes = hash.bytes();
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        const auto value = static_cast<unsigned>(std::to_integer<unsigned char>(bytes[i]));
        out[i * 2] = kHex[(value >> 4U) & 0x0FU];
        out[i * 2 + 1] = kHex[value & 0x0FU];
    }
    return out;
}

void appendLeU32(std::vector<std::byte>& out, u32 value)
{
    for (int i = 0; i < 4; ++i)
    {
        out.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xFFU));
    }
}

void appendLeU64(std::vector<std::byte>& out, u64 value)
{
    for (int i = 0; i < 8; ++i)
    {
        out.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xFFU));
    }
}

void appendF32Bits(std::vector<std::byte>& out, float value)
{
    static_assert(sizeof(float) == 4);
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendLeU32(out, bits);
}

void appendF64Bits(std::vector<std::byte>& out, double value)
{
    static_assert(sizeof(double) == 8);
    u64 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendLeU64(out, bits);
}

struct SampleOptions final {
    u64 targetFrameCount = DefaultFrameCount;
    u32 frameDelayMilliseconds = DefaultFrameDelayMilliseconds;
    // Primary window logical size (UI-003 multi-size matrix). Design baseline 960x540.
    u32 windowLogicalWidth = 960;
    u32 windowLogicalHeight = 540;
    // Optional visual gate: publish the Demo Button in its real disabled state.
    bool uiDisabledDemoButton = false;
    // Optional controlled product gate: seed one map cell selection after enter
    // without synthesizing OS pointer events (GLFW smoke stays hermetic).
    bool seedTileSelection = false;
    u32 seedTileCellX = 0;
    u32 seedTileCellY = 0;
    // M11-D2: optional golden pixel fingerprint (32 lowercase hex chars).
    // Empty = capture-only (must succeed); non-empty = must match exactly.
    std::string expectPixelFingerprint{};
};

[[nodiscard]] constexpr u32 minimumWindowVisibilityMilliseconds(const SampleOptions& options) noexcept
{
#if defined(TINA_SAMPLE_TILEMAP_FREETYPE)
    return Tina::Sample2D::useProductUiVisibilityPacing(options.frameDelayMilliseconds)
               ? Tina::Sample2D::MinimumProductUiVisibilityMilliseconds
               : 0;
#else
    static_cast<void>(options);
    return 0;
#endif
}

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 renderExtractions = 0;
    u64 stateEnters = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
    u64 texturesUploaded = 0;
    u64 spriteBindingTextures = 0;
    u64 spriteBindingsReleased = 0;
    u64 spriteBindingTexturesDestroyed = 0;
    u64 spriteBindingResolverHits = 0;
    u64 lastTileSprites = 0;
    u64 lastTotalSprites = 0;
    u64 controllerGroundedFrames = 0;
    u64 controllerWalkFrames = 0;
    u64 controllerHitRightFrames = 0;
    u64 characterAnimationUpdates = 0;
    u64 characterAnimationFrameChanges = 0;
    u64 characterAnimationIdleEntries = 0;
    u64 characterAnimationWalkEntries = 0;
    u64 characterAnimationHitWallEntries = 0;
    u64 characterAnimationResolvedFrames = 0;
    u64 characterAnimationLastFrame = 0;
    bool characterAnimationFromCatalog = false;
    bool characterAnimationHitCompleted = false;
    float maxControllerX = 0.0f;
    u64 uiRootsCreated = 0;
    u64 uiPanelsCreated = 0;
    u64 uiRootsReleased = 0;
    u64 uiTextLabelsCreated = 0;
    u64 uiTextEditsCreated = 0;
    bool uiTextEditInitialTextVerified = false;
    u64 uiButtonsCreated = 0;
    u64 uiButtonActionsWired = 0;
    bool uiButtonPaintVerified = false;
    bool uiDisabledDemoButtonRequested = false;
    bool uiDemoButtonEnabled = true;
    bool uiDisabledDemoButtonVerified = false;
    // M11-C1/C2: Master/Music/SFX volume Sliders wired to AudioEngine buses.
    u64 uiSlidersCreated = 0;
    u64 uiSliderChanges = 0;
    float lastMasterVolume = 1.0F;
    float lastMusicVolume = 1.0F;
    float lastSfxVolume = 1.0F;
    bool masterVolumeFromSlider = false;
    bool musicVolumeFromSlider = false;
    bool sfxVolumeFromSlider = false;
    // M11-C3/C5: Master/Music/SFX mute Checkboxes -> AudioEngine setBusMuted.
    u64 uiCheckboxesCreated = 0;
    u64 uiCheckboxActions = 0;
    u64 uiProgressBarsCreated = 0;
    bool uiProgressBarValueVerified = false;
    u64 uiRadioButtonsCreated = 0;
    u64 uiRadioButtonActionsWired = 0;
    bool uiRadioSelectionVerified = false;
    bool lastMasterMuted = false;
    bool lastMusicMuted = false;
    bool lastSfxMuted = false;
    bool masterMutedFromCheckbox = false;
    bool musicMutedFromCheckbox = false;
    bool sfxMutedFromCheckbox = false;
    // M11-D1: optional GPU pixel evidence (primary backbuffer RGBA8 hash).
    bool pixelCaptureAttempted = false;
    bool pixelCaptureOk = false;
    u32 pixelCaptureWidth = 0;
    u32 pixelCaptureHeight = 0;
    u64 pixelCaptureBytes = 0;
    std::string pixelFingerprint{};
    Tina::Sample2D::TileSelectionCounters tileSelection{};
    u16 lastSelectedTileId = 0;
    u64 selectionHighlightSprites = 0;
    u64 lastHighlightSprites = 0;
    u64 catalogRecipeAssets = 0;
    bool catalogFromRecipeFile = false;
    u64 objectLayerObjectCount = 0;
    bool objectLayerConsumed = false;
    bool seedTileSelectionApplied = false;
    // 2D-TILEMAP-STREAM: product-path lazy chunk residency evidence.
    u64 tileMapStreamDemandUpdates = 0;
    u64 tileMapStreamRequests = 0;
    u64 tileMapStreamCommitted = 0;
    u64 tileMapStreamResident = 0;
    u64 tileMapStreamPeakResident = 0;
    // 2D-FX: deterministic fixed-capacity particle/trail product evidence.
    u64 particleEmitted = 0;
    u64 particleExpired = 0;
    u64 particleActive = 0;
    u64 particleExtracted = 0;
    u64 trailSegmentsCreated = 0;
    u64 trailActive = 0;
    u64 trailExtracted = 0;
    u64 trailBreaks = 0;
    std::string fxInitialFingerprint{};
    // M11-B0: surface-driven Camera2D projection (FixedWorldHeight).
    u32 surfacePixelWidth = 960;
    u32 surfacePixelHeight = 540;
    // UI-003: last GLFW window metrics (logical vs framebuffer vs contentScale).
    u32 logicalPixelWidth = 960;
    u32 logicalPixelHeight = 540;
    u32 framebufferPixelWidth = 960;
    u32 framebufferPixelHeight = 540;
    float contentScaleX = 1.0F;
    float contentScaleY = 1.0F;
    u64 windowMetricsEvents = 0;
    u64 cameraProjectionResolves = 0;
    float lastCameraWorldWidth = 0.0f;
    float lastCameraWorldHeight = 0.0f;
    float lastCameraActualPpm = 0.0f;
    // M11-B1: TileChunkDirtyCache product evidence (CPU revision gate).
    u64 chunkDirtyFramesSynced = 0;
    u64 chunkDirtyVisibleObservations = 0;
    u64 chunkDirtyRebuilds = 0;
    u64 chunkDirtyCacheHits = 0;
    u64 lastChunkDirtyRebuilds = 0;
    u64 lastChunkDirtyCacheHits = 0;
    u64 lastChunkDirtyVisible = 0;
    // M11-B2: Camera2D follow + presentation interpolation evidence.
    u64 cameraFollowUpdates = 0;
    u64 cameraInterpolatedExtracts = 0;
    float lastCameraCenterX = 0.0f;
    float lastCameraCenterY = 0.0f;
    float lastCameraInterpolation = 0.0f;
    float maxCameraCenterX = 0.0f;
    float minCameraCenterX = 0.0f;
    bool cameraFollowPrimed = false;
    // M11-A15/A19: EngineHost AudioEngine + catalog AudioClip lease product evidence.
    bool audioEnginePresent = false;
    bool audioOneShotQueued = false;
    bool audioStartedObserved = false;
    bool audioVoiceParamsConfigured = false;
    bool audioFadeStarted = false;
    bool audioFadeCancelled = false;
    bool audioFadeStopped = false;
    bool audioOneShotRetired = false;
    // N7-B: bounded owner-thread PCM stream from the same catalog-held clip.
    bool audioStreamQueued = false;
    bool audioStreamSubmitted = false;
    bool audioStreamEofSignaled = false;
    bool audioStreamStartedObserved = false;
    bool audioStreamMixed = false;
    bool audioStreamDrained = false;
    bool audioStreamStopped = false;
    bool audioStreamRetired = false;
    bool audioFromCatalogLease = false;
    u64 audioStartedCount = 0;
    u64 audioStoppedCount = 0;
    u64 audioStreamSubmittedFrames = 0;
    u64 audioStreamConsumedFrames = 0;
    u64 audioStreamUnderrunFrames = 0;
    u64 audioClipFrameCount = 0;
    u32 audioClipSampleRate = 0;
    float audioVoiceGain = 0.0F;
    float audioPitch = 0.0F;
    float audioPan = 0.0F;
#if defined(TINA_SAMPLE_TILEMAP_AUDIO_MINIAUDIO)
    // M11-A16: optional null-backend device + attachMixer SFX evidence.
    bool audioDeviceCreated = false;
    bool audioDeviceStarted = false;
    bool audioDeviceNullBackend = false;
    u64 audioDeviceCallbacks = 0;
    u64 audioMixFramesRendered = 0;
#endif
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
    u64 physicsSteps = 0;
    u64 physicsStaticBodies = 0;
    u64 physicsDynamicContacts = 0;
    u64 physicsSensorEnters = 0;
    u64 physicsSensorExits = 0;
    float lastDynamicY = 0.0f;
    bool physicsReady = false;
    bool physicsJointReady = false;
#endif
    // RUNTIME-001 product evidence: pause overlay push/pop + policy blocks below.
    u64 pauseOverlayPushes = 0;
    u64 pauseOverlayPops = 0;
    u64 pauseOverlayFrames = 0;
    bool pauseOverlayPushQueued = false;
    // UI-002-SPI product evidence: accessibility tree from committedSemantics.
    u64 accessibilityPublishCount = 0;
    u64 accessibilityNodeCount = 0;
    u64 accessibilitySemanticsRevision = 0;
    bool accessibilityHasButton = false;
    bool accessibilityHasCheckbox = false;
    bool accessibilityHasSlider = false;
    bool accessibilityHasProgressBar = false;
    bool accessibilityHasRadio = false;
    bool accessibilityHasTextEdit = false;
    bool accessibilityPublished = false;
};

inline constexpr u32 ExpectedUIPanelCount = 3;
inline constexpr u32 ExpectedUITextLabelCount = 8;
inline constexpr u32 ExpectedUITextEditCount = 1;
inline constexpr u32 ExpectedUIButtonCount = 1;
inline constexpr u32 ExpectedUIProgressBarCount = 1;
inline constexpr u32 ExpectedUIRadioButtonCount = 2;
// Authored FixedWorldHeight for product sample; world width/ppm come from surface.
// Keep height small enough that half-width fits the 8m-wide sample map so follow
// can pan (height 6m →halfW≥.3m > map half →clamp freezes at center).
inline constexpr float ProductCameraHeightMeters = 4.0f;

// Clamp camera center so the orthographic view stays over the map (map-local meters).
// Uses authored height + surface aspect for half-extents; does not write Simulation.
[[nodiscard]] inline void clampCameraCenterToMap(const Tina::Asset::TileMapInstance& map, float worldHeightMeters,
                                                 float surfaceAspect, float& centerX, float& centerY) noexcept
{
    if (!map || worldHeightMeters <= 0.0f || !std::isfinite(worldHeightMeters) || !std::isfinite(surfaceAspect) ||
        surfaceAspect <= 0.0f)
    {
        return;
    }
    const float mapW = static_cast<float>(map.widthCells()) * map.cellSizeMeters();
    const float mapH = static_cast<float>(map.heightCells()) * map.cellSizeMeters();
    const float halfH = worldHeightMeters * 0.5f;
    const float halfW = halfH * surfaceAspect;
    const float minX = halfW;
    const float maxX = mapW - halfW;
    const float minY = halfH;
    const float maxY = mapH - halfH;
    if (minX <= maxX)
    {
        centerX = (std::min)(maxX, (std::max)(minX, centerX));
    }
    else
    {
        centerX = mapW * 0.5f;
    }
    if (minY <= maxY)
    {
        centerY = (std::min)(maxY, (std::max)(minY, centerY));
    }
    else
    {
        centerY = mapH * 0.5f;
    }
}
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
inline constexpr u32 ExpectedPhysicsStaticBodies = ExpectedNonEmptyTiles;
inline constexpr u32 ExpectedSpritesWithPhysics = ExpectedNonEmptyTiles + 2; // tiles + character + crate
#else
inline constexpr u32 ExpectedSpritesWithPhysics = ExpectedNonEmptyTiles + 1;
#endif

[[nodiscard]] Tina::UI::UILayoutStyle absolutePanelStyle(Tina::UI::UILayoutLength left, Tina::UI::UILayoutLength top,
                                                         Tina::UI::UILayoutLength width,
                                                         Tina::UI::UILayoutLength height) noexcept
{
    Tina::UI::UILayoutStyle style{};
    style.position = Tina::UI::UILayoutPositionMode::AbsoluteOverlay;
    style.absoluteInset.left = left;
    style.absoluteInset.top = top;
    style.size.width = width;
    style.size.height = height;
    return style;
}

[[nodiscard]] Tina::UI::UIBoxPaint solidFill(u8 red, u8 green, u8 blue, u8 alpha = 255) noexcept
{
    return Tina::UI::UIBoxPaint{
        .solidFill = Tina::UI::UISolidFill{.color = Tina::UI::rgba8(red, green, blue, alpha)},
    };
}

[[nodiscard]] Tina::UI::UIBoxPaint solidFill(u32 hexRgb, u8 alpha = 255) noexcept
{
    return Tina::UI::UIBoxPaint{
        .solidFill = Tina::UI::UISolidFill{.color = Tina::UI::rgb(hexRgb, alpha)},
    };
}

void writeJsonString(std::ostream& output, std::string_view value)
{
    output.put('"');
    for (const unsigned char byte : value)
    {
        if (byte == '"' || byte == '\\')
        {
            output.put('\\');
            output.put(static_cast<char>(byte));
        } else if (byte >= 0x20U)
        {
            output.put(static_cast<char>(byte));
        }
    }
    output.put('"');
}

void writeError(const Tina::Core::Error& error)
{
    std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_2d\",\"message\":";
    writeJsonString(std::cerr, error.message);
    std::cerr << "}\n";
}

[[nodiscard]] Tina::Core::AssetId::Bytes idBytes(u8 seed)
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xA5U);
    return bytes;
}

[[nodiscard]] Tina::Core::Result<SampleOptions> parseOptions(int argc, char** argv)
{
    SampleOptions options{};
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument{argv[index]};
        if (argument.starts_with("--frames="))
        {
            const auto text = argument.substr(std::string_view{"--frames="}.size());
            u64 value = 0;
            const auto [end, err] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (err != std::errc{} || end != text.data() + text.size() || value == 0)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "invalid --frames");
            }
            options.targetFrameCount = value;
            continue;
        }
        if (argument.starts_with("--frame-delay-ms="))
        {
            const auto text = argument.substr(std::string_view{"--frame-delay-ms="}.size());
            u32 value = 0;
            const auto [end, err] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (err != std::errc{} || end != text.data() + text.size())
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "invalid --frame-delay-ms");
            }
            options.frameDelayMilliseconds = value;
            continue;
        }
        if (argument.starts_with("--width="))
        {
            const auto text = argument.substr(std::string_view{"--width="}.size());
            u32 value = 0;
            const auto [end, err] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (err != std::errc{} || end != text.data() + text.size() || value < 320U || value > 3840U)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "invalid --width (expected 320..3840)");
            }
            options.windowLogicalWidth = value;
            continue;
        }
        if (argument.starts_with("--height="))
        {
            const auto text = argument.substr(std::string_view{"--height="}.size());
            u32 value = 0;
            const auto [end, err] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (err != std::errc{} || end != text.data() + text.size() || value < 180U || value > 2160U)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "invalid --height (expected 180..2160)");
            }
            options.windowLogicalHeight = value;
            continue;
        }
        if (argument == "--ui-disabled-demo-button")
        {
            options.uiDisabledDemoButton = true;
            continue;
        }
        if (argument.starts_with("--seed-tile-selection="))
        {
            const auto text = argument.substr(std::string_view{"--seed-tile-selection="}.size());
            const auto comma = text.find(',');
            if (comma == std::string_view::npos)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "invalid --seed-tile-selection (expected cellX,cellY)");
            }
            u32 cellX = 0;
            u32 cellY = 0;
            const auto [endX, errX] =
                std::from_chars(text.data(), text.data() + comma, cellX);
            const auto [endY, errY] =
                std::from_chars(text.data() + comma + 1, text.data() + text.size(), cellY);
            if (errX != std::errc{} || errY != std::errc{} || endX != text.data() + comma ||
                endY != text.data() + text.size())
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "invalid --seed-tile-selection (expected cellX,cellY)");
            }
            options.seedTileSelection = true;
            options.seedTileCellX = cellX;
            options.seedTileCellY = cellY;
            continue;
        }
        if (argument.starts_with("--expect-pixel-fingerprint="))
        {
            const auto text = argument.substr(std::string_view{"--expect-pixel-fingerprint="}.size());
            if (text.size() != 32)
            {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "invalid --expect-pixel-fingerprint (expected 32 lowercase hex chars)");
            }
            for (const char ch : text)
            {
                const bool isHex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
                if (!isHex)
                {
                    return Tina::Core::failure(
                        Tina::Core::CoreErrorCode::InvalidArgument,
                        "invalid --expect-pixel-fingerprint (expected 32 lowercase hex chars)");
                }
            }
            options.expectPixelFingerprint.assign(text.data(), text.size());
            continue;
        }
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "unsupported argument");
    }
    return options;
}

enum class CharacterAnimationState : u8 {
    Idle,
    Walk,
    HitWall,
};

struct ResolvedCharacterAnimationClip final {
    explicit ResolvedCharacterAnimationClip(std::pmr::memory_resource& resource)
        : frames(&resource)
    {
    }

    [[nodiscard]] Tina::Scene::SpriteAnimationClip2D view() const noexcept
    {
        return {
            .frames = frames,
            .playbackMode = playbackMode,
        };
    }

    std::pmr::vector<Tina::Scene::SpriteAnimationFrame2D> frames;
    Tina::Scene::SpriteAnimationPlaybackMode playbackMode = Tina::Scene::SpriteAnimationPlaybackMode::Loop;
};

struct TileMapResources final {
    std::pmr::unsynchronized_pool_resource memory{};
    std::unique_ptr<Tina::Asset::AssetSystem> system{};
    Tina::Asset::AssetHandle tileTextureHandle{};
    Tina::Asset::AssetHandle characterTextureHandle{};
    Tina::Asset::AssetHandle crateSpriteHandle{};
    Tina::Asset::AssetHandle tilesetHandle{};
    Tina::Asset::AssetHandle tileMapHandle{};
    Tina::Asset::AssetHandle audioClipHandle{};
    // Keeps cooked AudioClip CPU payload alive across playOneShot/mix (M11-A19).
    Tina::Asset::AssetLease audioClipLease{};
    // Must remain at its final address: TileMapGridCollision borrows stream.map().
    std::optional<Tina::Asset::TileMapStream> tileMapStream{};
    std::optional<Tina::Asset::TileMapGridCollision> grid{};
    std::optional<Tina::Asset::CharacterController2D> controller{};
    std::optional<Tina::Asset::TileChunkDirtyCache> chunkDirtyCache{};
    std::pmr::vector<Tina::Asset::TileChunkView> chunkDirtyRebuilt{&memory};
    std::pmr::vector<Tina::Asset::TileMapSolidHit> solidScratch{&memory};
    float characterSpawnX = 1.0f;
    float characterSpawnY = 3.0f;
    // Presentation camera: previous/current sim targets; extract lerps with FrameTiming.interpolation.
    float cameraPreviousX = 4.0f;
    float cameraPreviousY = 2.0f;
    float cameraCurrentX = 4.0f;
    float cameraCurrentY = 2.0f;
    // M8-C1: Scene World owns Camera2D + character (+ optional crate) for extract.
    // TileMap tiles and selection highlight remain feature-side emit.
    std::optional<Tina::Scene::World> sceneWorld{};
    Tina::Scene::EntityId cameraEntity{};
    Tina::Scene::EntityId characterEntity{};
    ResolvedCharacterAnimationClip idleAnimation{memory};
    ResolvedCharacterAnimationClip walkAnimation{memory};
    ResolvedCharacterAnimationClip hitWallAnimation{memory};
    std::optional<Tina::Scene::SpriteAnimator2D> characterAnimator{};
    std::optional<Tina::Scene::ParticleSystem2D> particles{};
    std::optional<Tina::Scene::Trail2D> trail{};
    CharacterAnimationState characterAnimationState = CharacterAnimationState::Idle;
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
    Tina::Scene::EntityId crateEntity{};
#endif
    std::filesystem::path catalogRoot{};
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
    std::optional<Tina::Physics2D::PhysicsWorld2D> physicsWorld{};
    Tina::Physics2D::PhysicsBodyId dynamicBody{};
    Tina::Physics2D::PhysicsShapeId dynamicShape{};
    Tina::Physics2D::PhysicsBodyId sensorBody{};
    Tina::Physics2D::PhysicsShapeId sensorShape{};
    Tina::Physics2D::PhysicsBodyId jointAnchorBody{};
    Tina::Physics2D::PhysicsBodyId jointFollowerBody{};
    Tina::Physics2D::PhysicsShapeId jointFollowerShape{};
    Tina::Physics2D::PhysicsJointId demoJoint{};
    Tina::Physics2D::PhysicsBodyId staticBodies[32]{};
    Tina::Physics2D::PhysicsGridSolidCell2D solidCellScratch[32]{};
    float dynamicHalfExtent = 0.25f;
    float lastDynamicX = 3.0f;
    float lastDynamicY = 3.5f;
#endif
#if defined(TINA_SAMPLE_TILEMAP_AUDIO_MINIAUDIO)
    std::optional<Tina::Audio::MiniaudioDevice> audioDevice{};
#endif
};

[[nodiscard]] Tina::Core::Result<std::string> makeInitialFxFingerprint(
    const Tina::Scene::ParticleSystem2D& particles,
    const Tina::Scene::Trail2D& trail)
{
    std::vector<std::byte> bytes;
    bytes.reserve(640);
    appendLeU32(bytes, 1U); // FX fingerprint schema
    appendLeU64(bytes, particles.randomSeed());
    appendLeU64(bytes, particles.capacity());
    appendLeU64(bytes, particles.liveCount());
    for (const Tina::Scene::Particle2D& particle : particles.particles())
    {
        appendLeU64(bytes, particle.stableParticleKey);
        appendLeU32(bytes, particle.spriteKey);
        appendF32Bits(bytes, particle.position.x);
        appendF32Bits(bytes, particle.position.y);
        appendF32Bits(bytes, particle.velocity.x);
        appendF32Bits(bytes, particle.velocity.y);
        appendF64Bits(bytes, particle.age.count());
        appendF64Bits(bytes, particle.lifetime.count());
        appendF32Bits(bytes, particle.startSizeMeters.x);
        appendF32Bits(bytes, particle.startSizeMeters.y);
        appendF32Bits(bytes, particle.endSizeMeters.x);
        appendF32Bits(bytes, particle.endSizeMeters.y);
        appendLeU32(bytes, static_cast<u32>(particle.startColor.red) |
                               (static_cast<u32>(particle.startColor.green) << 8U) |
                               (static_cast<u32>(particle.startColor.blue) << 16U) |
                               (static_cast<u32>(particle.startColor.alpha) << 24U));
        appendLeU32(bytes, static_cast<u32>(particle.endColor.red) |
                               (static_cast<u32>(particle.endColor.green) << 8U) |
                               (static_cast<u32>(particle.endColor.blue) << 16U) |
                               (static_cast<u32>(particle.endColor.alpha) << 24U));
        appendF32Bits(bytes, particle.rotationRadians);
        appendLeU32(bytes, static_cast<u32>(static_cast<std::uint16_t>(particle.sortingLayer)));
        appendLeU32(bytes, static_cast<u32>(particle.orderInLayer));
    }

    const Tina::Scene::Trail2DConfig trailConfig = trail.config();
    appendLeU64(bytes, trailConfig.segmentCapacity);
    appendF64Bits(bytes, trailConfig.segmentLifetime.count());
    appendF32Bits(bytes, trailConfig.startWidthMeters);
    appendF32Bits(bytes, trailConfig.endWidthMeters);
    appendLeU32(bytes, trailConfig.spriteKey);
    appendLeU64(bytes, trailConfig.stableEntityKeyBase);
    appendF32Bits(bytes, trailConfig.uvRect.u0);
    appendF32Bits(bytes, trailConfig.uvRect.v0);
    appendF32Bits(bytes, trailConfig.uvRect.u1);
    appendF32Bits(bytes, trailConfig.uvRect.v1);
    appendLeU32(bytes, static_cast<u32>(trailConfig.color.red) |
                           (static_cast<u32>(trailConfig.color.green) << 8U) |
                           (static_cast<u32>(trailConfig.color.blue) << 16U) |
                           (static_cast<u32>(trailConfig.color.alpha) << 24U));
    appendLeU32(bytes, static_cast<u32>(static_cast<std::uint16_t>(trailConfig.sortingLayer)));
    appendLeU32(bytes, static_cast<u32>(trailConfig.orderInLayer));
    appendLeU64(bytes, trail.segmentCount());
    for (const Tina::Scene::Trail2DSegment& segment : trail.segments())
    {
        appendF32Bits(bytes, segment.start.x);
        appendF32Bits(bytes, segment.start.y);
        appendF32Bits(bytes, segment.end.x);
        appendF32Bits(bytes, segment.end.y);
        appendF64Bits(bytes, segment.age.count());
        appendF64Bits(bytes, segment.lifetime.count());
        appendLeU64(bytes, segment.stableEntityKey);
    }

    auto fingerprint = Tina::Core::digestContentHashV1(bytes);
    if (!fingerprint)
    {
        return Tina::Core::failure(std::move(fingerprint.error()));
    }
    return contentHashToHex(*fingerprint);
}

[[nodiscard]] Tina::Core::Status prepare2dEffects(
    TileMapResources& resources,
    LifecycleCounters& counters,
    u32 characterSpriteKey)
{
    auto particles = Tina::Scene::ParticleSystem2D::Create(
        Tina::Scene::ParticleSystem2DConfig{
            .capacity = ProductParticleCapacity,
            .randomSeed = ProductParticleRandomSeed,
            .firstStableParticleKey = ProductParticleStableKeyBase,
        },
        resources.memory);
    if (!particles)
    {
        return Tina::Core::failure(std::move(particles.error()));
    }

    const Tina::Scene::ParticleBurst2D shortBurst{
        .count = 4,
        .spriteKey = characterSpriteKey,
        .origin = {4.0F, 2.1F},
        .positionOffset = {.minimum = {-0.45F, -0.2F}, .maximum = {0.45F, 0.2F}},
        .velocity = {.minimum = {-0.25F, 0.25F}, .maximum = {0.25F, 0.55F}},
        .lifetime = {.minimum = Tina::Core::Duration{0.5}, .maximum = Tina::Core::Duration{0.5}},
        .startSizeMeters = {0.28F, 0.28F},
        .endSizeMeters = {0.1F, 0.1F},
        .startColor = {.red = 255, .green = 215, .blue = 96, .alpha = 255},
        .endColor = {.red = 255, .green = 105, .blue = 64, .alpha = 32},
        .rotationRadians = 0.0F,
        .sortingLayer = 2,
        .orderInLayer = 10,
    };
    if (const auto status = particles->emitBurst(shortBurst); !status)
    {
        return status;
    }

    const Tina::Scene::ParticleBurst2D persistentBurst{
        .count = 6,
        .spriteKey = characterSpriteKey,
        .origin = {4.0F, 2.0F},
        .positionOffset = {.minimum = {-0.55F, -0.35F}, .maximum = {0.55F, 0.35F}},
        .velocity = {.minimum = {-0.02F, -0.01F}, .maximum = {0.02F, 0.02F}},
        .lifetime = {.minimum = Tina::Core::Duration{10.0}, .maximum = Tina::Core::Duration{10.0}},
        .startSizeMeters = {0.2F, 0.2F},
        .endSizeMeters = {0.12F, 0.12F},
        .startColor = {.red = 90, .green = 220, .blue = 255, .alpha = 230},
        .endColor = {.red = 80, .green = 140, .blue = 255, .alpha = 120},
        .rotationRadians = 0.0F,
        .sortingLayer = 2,
        .orderInLayer = 11,
    };
    if (const auto status = particles->emitBurst(persistentBurst); !status)
    {
        return status;
    }

    auto trail = Tina::Scene::Trail2D::Create(
        Tina::Scene::Trail2DConfig{
            .segmentCapacity = ProductTrailCapacity,
            .segmentLifetime = Tina::Core::Duration{10.0},
            .startWidthMeters = 0.18F,
            .endWidthMeters = 0.04F,
            .spriteKey = characterSpriteKey,
            .stableEntityKeyBase = ProductTrailStableKeyBase,
            .color = {.red = 70, .green = 230, .blue = 180, .alpha = 210},
            .sortingLayer = 1,
            .orderInLayer = 8,
        },
        resources.memory);
    if (!trail)
    {
        return Tina::Core::failure(std::move(trail.error()));
    }

    constexpr std::array firstTrailPoints{
        Tina::Scene::Vec2{3.2F, 1.4F},
        Tina::Scene::Vec2{3.7F, 1.8F},
        Tina::Scene::Vec2{4.2F, 1.5F},
    };
    for (const Tina::Scene::Vec2 point : firstTrailPoints)
    {
        if (const auto status = trail->appendPoint(point); !status)
        {
            return status;
        }
    }
    trail->breakTrail();
    ++counters.trailBreaks;
    constexpr std::array secondTrailPoints{
        Tina::Scene::Vec2{4.35F, 2.25F},
        Tina::Scene::Vec2{4.85F, 2.65F},
    };
    for (const Tina::Scene::Vec2 point : secondTrailPoints)
    {
        if (const auto status = trail->appendPoint(point); !status)
        {
            return status;
        }
    }

    auto fxFingerprint = makeInitialFxFingerprint(*particles, *trail);
    if (!fxFingerprint)
    {
        return Tina::Core::failure(std::move(fxFingerprint.error()));
    }

    counters.particleEmitted = particles->liveCount();
    counters.particleActive = particles->liveCount();
    counters.trailSegmentsCreated = trail->segmentCount();
    counters.trailActive = trail->segmentCount();
    counters.fxInitialFingerprint = std::move(*fxFingerprint);
    resources.particles.emplace(std::move(*particles));
    resources.trail.emplace(std::move(*trail));
    return Tina::Core::success();
}

[[nodiscard]] Tina::Core::Status consumeGameplayObjectLayer(TileMapResources& resources, LifecycleCounters& counters)
{
    if (!resources.tileMapStream)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "tilemap is required for object layer setup");
    }
    auto layer = resources.tileMapStream->map().layer(GameplayObjectLayerId);
    if (!layer)
    {
        return Tina::Core::failure(std::move(layer.error()));
    }
    if (layer->kind != Tina::AssetFormat::TileMapLayerKind::Object || !layer->visible ||
        layer->name != "gameplay")
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidCatalogConfig,
                                   "gameplay object layer metadata is invalid");
    }
    const auto layerProperty = layer->findProperty("domain");
    if (layer->propertyCount != 1U || !layerProperty || layerProperty->value != "gameplay")
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidCatalogConfig,
                                   "gameplay object layer property is invalid");
    }

    if (layer->objectCount != 2U)
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidCatalogConfig,
                                   "gameplay object layer must contain exactly two objects");
    }
    const auto playerSpawn = layer->findObject(PlayerSpawnObjectId);
    const auto crateSpawn = layer->findObject(CrateSpawnObjectId);
    if (!playerSpawn || !crateSpawn)
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidCatalogConfig,
                                   "gameplay object layer is missing required stable object ids");
    }
    const auto playerRole = playerSpawn->findProperty("role");
    if (playerSpawn->kind != Tina::AssetFormat::TileMapObjectKind::Point || !playerSpawn->visible ||
        playerSpawn->name != "player_spawn" || playerSpawn->propertyCount != 1U || !playerRole ||
        playerRole->value != "player")
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidCatalogConfig,
                                   "player spawn object is invalid");
    }
    const auto crateRole = crateSpawn->findProperty("role");
    if (crateSpawn->kind != Tina::AssetFormat::TileMapObjectKind::Rectangle || !crateSpawn->visible ||
        crateSpawn->name != "crate_spawn" || crateSpawn->propertyCount != 1U || !crateRole ||
        crateRole->value != "crate")
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidCatalogConfig,
                                   "crate spawn object is invalid");
    }
    resources.characterSpawnX = playerSpawn->x;
    resources.characterSpawnY = playerSpawn->y;
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
    resources.lastDynamicX = crateSpawn->x + crateSpawn->width * 0.5f;
    resources.lastDynamicY = crateSpawn->y + crateSpawn->height * 0.5f;
#endif
    counters.objectLayerObjectCount = layer->objectCount;
    counters.objectLayerConsumed = true;
    return Tina::Core::success();
}

[[nodiscard]] Tina::Core::Status advanceTileMapStream(TileMapResources& resources, LifecycleCounters& counters,
                                                      const Tina::Asset::TileChunkCameraQuery& camera)
{
    if (resources.system == nullptr || !resources.tileMapStream)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "tilemap stream or asset system is missing");
    }

    const std::array demands{
        Tina::Asset::TileMapChunkDemand{.layerId = VisualTileLayerId, .camera = camera},
        Tina::Asset::TileMapChunkDemand{.layerId = CollisionTileLayerId, .camera = camera},
    };
    if (auto status = resources.tileMapStream->updateDemand(demands); !status)
    {
        return status;
    }
    ++counters.tileMapStreamDemandUpdates;

    auto pumped = resources.system->pump(ExpectedTileMapStreamChunks);
    if (!pumped)
    {
        return Tina::Core::failure(std::move(pumped.error()));
    }
    auto committed = resources.tileMapStream->commitReady();
    if (!committed)
    {
        return Tina::Core::failure(std::move(committed.error()));
    }
    counters.tileMapStreamRequests = committed->totalRequests;
    counters.tileMapStreamCommitted = committed->totalCommitted;
    counters.tileMapStreamResident = committed->residentSlots;
    counters.tileMapStreamPeakResident = committed->peakResidentSlots;
    if (committed->failedSlots != 0)
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::AssetFailed,
                                   "one or more demanded tilemap chunks failed to load");
    }
    return Tina::Core::success();
}

[[nodiscard]] Tina::Scene::LocalTransform sceneTranslation(float x, float y) noexcept
{
    Tina::Scene::LocalTransform local{};
    local.position = {x, y, 0.0f};
    return local;
}

[[nodiscard]] Tina::Core::Result<Tina::Scene::SpriteAnimationPlaybackMode>
toScenePlaybackMode(Tina::AssetFormat::SpriteAnimationPlaybackMode mode) noexcept
{
    switch (mode)
    {
    case Tina::AssetFormat::SpriteAnimationPlaybackMode::Once:
        return Tina::Scene::SpriteAnimationPlaybackMode::Once;
    case Tina::AssetFormat::SpriteAnimationPlaybackMode::Loop:
        return Tina::Scene::SpriteAnimationPlaybackMode::Loop;
    case Tina::AssetFormat::SpriteAnimationPlaybackMode::PingPong:
        return Tina::Scene::SpriteAnimationPlaybackMode::PingPong;
    }
    return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidCatalogConfig,
                               "unsupported character animation playback mode");
}

[[nodiscard]] Tina::Core::Status resolveCharacterAnimationClip(
    Tina::Asset::AssetSystem& system,
    Tina::Core::AssetId clipId,
    Tina::Core::AssetId expectedTextureId,
    Tina::Scene::Vec2 characterSize,
    ResolvedCharacterAnimationClip& output)
{
    auto clipHandle = system.find(clipId);
    if (!clipHandle)
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidHandle,
                                   "character animation clip is not loaded");
    }
    const auto* clipFile = system.tryGet(*clipHandle);
    if (clipFile == nullptr)
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::AssetNotReady,
                                   "character animation clip CPU payload is missing");
    }
    auto clip = Tina::Asset::parseSpriteAnimationClipFromCooked(*clipFile);
    if (!clip)
    {
        return Tina::Core::failure(std::move(clip.error()));
    }
    auto playbackMode = toScenePlaybackMode(clip->playbackMode);
    if (!playbackMode)
    {
        return Tina::Core::failure(std::move(playbackMode.error()));
    }

    std::pmr::vector<Tina::Scene::SpriteAnimationFrame2D> resolvedFrames{
        output.frames.get_allocator().resource()};
    try
    {
        resolvedFrames.reserve(clip->frameCount);
        for (u32 frameIndex = 0; frameIndex < clip->frameCount; ++frameIndex)
        {
            const auto frame = clip->frame(frameIndex);
            if (!frame)
            {
                return Tina::Core::failure(Tina::Asset::AssetErrorCode::CatalogEntryMismatch,
                                           "character animation frame could not be decoded");
            }
            const auto spriteDependency = clipFile->dependency(frame->spriteDependencyIndex);
            if (!spriteDependency)
            {
                return Tina::Core::failure(Tina::Asset::AssetErrorCode::CatalogEntryMismatch,
                                           "character animation Sprite dependency is missing");
            }
            auto spriteHandle = system.find(spriteDependency->assetId);
            if (!spriteHandle)
            {
                return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidHandle,
                                           "character animation Sprite dependency is not loaded");
            }
            const auto* spriteFile = system.tryGet(*spriteHandle);
            if (spriteFile == nullptr)
            {
                return Tina::Core::failure(Tina::Asset::AssetErrorCode::AssetNotReady,
                                           "character animation Sprite CPU payload is missing");
            }
            const auto textureDependency = spriteFile->dependency(0);
            if (spriteFile->header().dependencyCount != 1U || !textureDependency ||
                textureDependency->expectedKind != Tina::AssetFormat::AssetKind::Texture2D ||
                textureDependency->assetId != expectedTextureId)
            {
                return Tina::Core::failure(Tina::Asset::AssetErrorCode::CatalogEntryMismatch,
                                           "character animation Sprite must use the character texture atlas");
            }
            auto sprite = Tina::Asset::parseSpriteFromCooked(*spriteFile);
            if (!sprite)
            {
                return Tina::Core::failure(std::move(sprite.error()));
            }

            resolvedFrames.push_back(Tina::Scene::SpriteAnimationFrame2D{
                .sprite = Tina::Scene::SpriteRenderer2D{
                    .sprite = *spriteHandle,
                    .overrides = Tina::Scene::SpriteOverrideFlags::Size |
                                 Tina::Scene::SpriteOverrideFlags::Pivot |
                                 Tina::Scene::SpriteOverrideFlags::UvRect,
                    .sizeOverrideMeters = characterSize,
                    .pivotOverride = {.x = sprite->pivotX, .y = sprite->pivotY},
                    .uvRectOverride = {
                        .u0 = sprite->u0,
                        .v0 = sprite->v0,
                        .u1 = sprite->u1,
                        .v1 = sprite->v1,
                    },
                    .sortingLayer = 1,
                    .orderInLayer = 0,
                    .visible = true,
                },
                .duration = Tina::Core::Duration{frame->durationSeconds},
            });
        }
    }
    catch (const std::bad_alloc&)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "character animation frame resolution ran out of memory");
    }

    output.frames.swap(resolvedFrames);
    output.playbackMode = *playbackMode;
    return Tina::Core::success();
}

[[nodiscard]] Tina::Core::Status prepareCharacterAnimations(
    TileMapResources& resources,
    LifecycleCounters& counters,
    Tina::Asset::AssetSystem& system,
    Tina::Core::AssetId characterTextureId,
    Tina::Core::AssetId idleClipId,
    Tina::Core::AssetId walkClipId,
    Tina::Core::AssetId hitWallClipId)
{
    if (!resources.controller)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "controller required for character animation");
    }
    const auto& config = resources.controller->config();
    const Tina::Scene::Vec2 characterSize{config.halfWidth * 2.0F, config.halfHeight * 2.0F};
    if (auto status = resolveCharacterAnimationClip(
            system, idleClipId, characterTextureId, characterSize, resources.idleAnimation);
        !status)
    {
        return status;
    }
    if (auto status = resolveCharacterAnimationClip(
            system, walkClipId, characterTextureId, characterSize, resources.walkAnimation);
        !status)
    {
        return status;
    }
    if (auto status = resolveCharacterAnimationClip(
            system, hitWallClipId, characterTextureId, characterSize, resources.hitWallAnimation);
        !status)
    {
        return status;
    }

    auto animator = Tina::Scene::SpriteAnimator2D::Create(resources.idleAnimation.view(), resources.memory);
    if (!animator)
    {
        return Tina::Core::failure(std::move(animator.error()));
    }
    resources.characterAnimator.emplace(std::move(*animator));
    resources.characterAnimationState = CharacterAnimationState::Idle;
    counters.characterAnimationIdleEntries = 1;
    counters.characterAnimationResolvedFrames = resources.idleAnimation.frames.size() +
                                                resources.walkAnimation.frames.size() +
                                                resources.hitWallAnimation.frames.size();
    counters.characterAnimationFromCatalog = true;
    return Tina::Core::success();
}

[[nodiscard]] Tina::Core::Status setCharacterAnimationState(
    TileMapResources& resources,
    LifecycleCounters& counters,
    CharacterAnimationState nextState)
{
    if (!resources.characterAnimator)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "character animator is not initialized");
    }
    if (resources.characterAnimationState == nextState)
    {
        return Tina::Core::success();
    }

    const ResolvedCharacterAnimationClip* clip = nullptr;
    switch (nextState)
    {
    case CharacterAnimationState::Idle:
        clip = &resources.idleAnimation;
        ++counters.characterAnimationIdleEntries;
        break;
    case CharacterAnimationState::Walk:
        clip = &resources.walkAnimation;
        ++counters.characterAnimationWalkEntries;
        break;
    case CharacterAnimationState::HitWall:
        clip = &resources.hitWallAnimation;
        ++counters.characterAnimationHitWallEntries;
        break;
    }
    if (clip == nullptr)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "character animation state has no clip");
    }
    if (auto status = resources.characterAnimator->setClip(clip->view()); !status)
    {
        return status;
    }
    resources.characterAnimationState = nextState;
    return Tina::Core::success();
}

[[nodiscard]] Tina::Core::Status updateCharacterAnimation(
    TileMapResources& resources,
    LifecycleCounters& counters,
    Tina::Core::Duration delta)
{
    if (!resources.characterAnimator || !resources.sceneWorld)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "character animation Scene state is not initialized");
    }
    auto update = resources.characterAnimator->update(delta);
    if (!update)
    {
        return Tina::Core::failure(std::move(update.error()));
    }
    ++counters.characterAnimationUpdates;
    if (update->currentFrameChanged)
    {
        ++counters.characterAnimationFrameChanges;
    }
    counters.characterAnimationLastFrame = update->currentFrameIndex;
    if (resources.characterAnimationState == CharacterAnimationState::HitWall &&
        resources.characterAnimator->isCompleted())
    {
        counters.characterAnimationHitCompleted = true;
    }
    const auto* sprite = resources.characterAnimator->currentSprite();
    if (sprite == nullptr)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "character animator has no current Sprite frame");
    }
    return resources.sceneWorld->setSpriteRenderer2D(resources.characterEntity, *sprite);
}

// Bootstrap Scene World for product extract (camera + character [+ crate]).
// Called once after controller (and optional physics) are ready in prepareCatalog.
[[nodiscard]] Tina::Core::Status prepareSceneWorld(TileMapResources& resources)
{
    if (!resources.controller || !resources.characterAnimator)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "controller and character animator required for Scene World");
    }

    auto world = Tina::Scene::World::Create(Tina::Scene::WorldConfig{.entityCapacity = 16});
    if (!world)
    {
        return Tina::Core::failure(std::move(world.error()));
    }

    auto cameraEntity = world->createEntity(sceneTranslation(resources.cameraCurrentX, resources.cameraCurrentY));
    if (!cameraEntity)
    {
        return Tina::Core::failure(std::move(cameraEntity.error()));
    }
    const Tina::Scene::Camera2D camera{
        .projection = Tina::Render::FixedWorldHeight2D{.heightMeters = ProductCameraHeightMeters},
        .pixelSnap = Tina::Render::RenderPixelSnapPolicy::CameraTranslation,
        .active = true,
    };
    if (const auto status = world->setCamera2D(*cameraEntity, camera); !status)
    {
        return status;
    }

    auto characterEntity =
        world->createEntity(sceneTranslation(resources.controller->state().positionX, resources.controller->state().positionY));
    if (!characterEntity)
    {
        return Tina::Core::failure(std::move(characterEntity.error()));
    }
    const auto* characterSprite = resources.characterAnimator->currentSprite();
    if (characterSprite == nullptr)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "character animator has no initial Sprite frame");
    }
    if (const auto status = world->setSpriteRenderer2D(*characterEntity, *characterSprite); !status)
    {
        return status;
    }

#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
    auto crateEntity = world->createEntity(sceneTranslation(resources.lastDynamicX, resources.lastDynamicY));
    if (!crateEntity)
    {
        return Tina::Core::failure(std::move(crateEntity.error()));
    }
    const float crateSize = resources.dynamicHalfExtent * 2.0f;
    const Tina::Scene::SpriteRenderer2D crateSprite{
        .sprite = resources.crateSpriteHandle,
        .overrides = Tina::Scene::SpriteOverrideFlags::Size | Tina::Scene::SpriteOverrideFlags::UvRect,
        .sizeOverrideMeters = {crateSize, crateSize},
        // Right half of the product atlas (pre-M8-C1 direct emit fidelity).
        .uvRectOverride = {.u0 = 0.5f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f},
        .color = {.red = 120, .green = 220, .blue = 255, .alpha = 255},
        .sortingLayer = 1,
        .orderInLayer = 1,
        .visible = true,
    };
    if (const auto status = world->setSpriteRenderer2D(*crateEntity, crateSprite); !status)
    {
        return status;
    }
    resources.crateEntity = *crateEntity;
#endif

    resources.cameraEntity = *cameraEntity;
    resources.characterEntity = *characterEntity;
    resources.sceneWorld.emplace(std::move(*world));
    return Tina::Core::success();
}

[[nodiscard]] Tina::Core::Status prepareCatalog(TileMapResources& resources, LifecycleCounters& counters)
{
    // Stable product ids must match samples/2d_tilemap_bgfx/catalog/sample_2d.recipe.
    const auto tileTextureId = *Tina::Core::AssetId::fromBytes(idBytes(1U));
    const auto tilesetId = *Tina::Core::AssetId::fromBytes(idBytes(2U));
    const auto tileMapId = *Tina::Core::AssetId::fromBytes(idBytes(3U));
    const auto audioClipId = *Tina::Core::AssetId::fromBytes(idBytes(4U));
    const auto characterTextureId = *Tina::Core::AssetId::fromBytes(idBytes(5U));
    const auto idleClipId = *Tina::Core::AssetId::fromBytes(idBytes(9U));
    const auto walkClipId = *Tina::Core::AssetId::fromBytes(idBytes(10U));
    const auto hitWallClipId = *Tina::Core::AssetId::fromBytes(idBytes(11U));
    const auto crateSpriteId = *Tina::Core::AssetId::fromBytes(idBytes(12U));

#if !defined(TINA_SAMPLE_2D_RECIPE_PATH)
#error "TINA_SAMPLE_2D_RECIPE_PATH must be defined for tina_sample_2d catalog recipe load"
#endif
    // M10-A38/M11-A19: cook from an on-disk catalog recipe file (product asset path),
    // not in-process payload assembly. Still a hermetic fixture recipe, not
    // the full external cooker CLI pipeline.
    auto request = Tina::Asset::loadCatalogCookRecipeFile(TINA_SAMPLE_2D_RECIPE_PATH);
    if (!request)
    {
        return Tina::Core::failure(std::move(request.error()));
    }
    if (request->assets.size() != ExpectedCatalogRecipeAssets)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "sample_2d.recipe must declare the 2D map, audio, and character animation assets");
    }
    counters.catalogRecipeAssets = request->assets.size();
    counters.catalogFromRecipeFile = true;

    auto catalogRoot = Tina::Sample::createUniqueTempDirectory("tina_sample_2d_pkg");
    if (!catalogRoot)
    {
        return Tina::Core::failure(std::move(catalogRoot.error()));
    }
    resources.catalogRoot = std::move(*catalogRoot);
    auto catalogCleanup = Tina::Core::makeScopeExit([&resources]() noexcept {
        std::error_code cleanupError;
        std::filesystem::remove_all(resources.catalogRoot, cleanupError);
    });
    const auto rootUtf8 = [&] {
        const auto u8 = resources.catalogRoot.u8string();
        return std::string(u8.begin(), u8.end());
    }();
    if (auto cookStatus = Tina::Asset::cookAndPublishCatalogPackage(rootUtf8, *request); !cookStatus)
    {
        return cookStatus;
    }

    auto system = Tina::Asset::AssetSystem::Create(Tina::Asset::AssetSystemConfig{
        .storeCapacity = 16,
        .memoryResource = &resources.memory,
        .batch =
            Tina::Asset::CookedAssetBatchLoadConfig{
                .file = Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &resources.memory},
                .memoryResource = &resources.memory,
            },
        .requireTyped2dPayloads = true,
    });
    if (!system)
    {
        return Tina::Core::failure(std::move(system.error()));
    }
    if (auto bindStatus = system->openAndBindCatalog(rootUtf8); !bindStatus)
    {
        return bindStatus;
    }
    auto loaded = system->load(
        std::array{tileMapId, audioClipId, idleClipId, walkClipId, hitWallClipId, crateSpriteId});
    if (!loaded)
    {
        return Tina::Core::failure(std::move(loaded.error()));
    }
    static_cast<void>(loaded);
    auto tileMapHandle = system->find(tileMapId);
    auto tilesetHandle = system->find(tilesetId);
    auto tileTextureHandle = system->find(tileTextureId);
    auto characterTextureHandle = system->find(characterTextureId);
    auto crateSpriteHandle = system->find(crateSpriteId);
    if (!tileMapHandle || !tilesetHandle || !tileTextureHandle || !characterTextureHandle || !crateSpriteHandle)
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidHandle,
                                   "tilemap/tileset/texture/sprite dependencies are not loaded");
    }
    resources.tileMapHandle = *tileMapHandle;
    resources.tilesetHandle = *tilesetHandle;
    resources.tileTextureHandle = *tileTextureHandle;
    resources.characterTextureHandle = *characterTextureHandle;
    resources.crateSpriteHandle = *crateSpriteHandle;

    // Resolve AudioClip by id (load() return order is plan order, not request order).
    auto audioHandle = system->find(audioClipId);
    if (!audioHandle)
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidHandle, "audioclip not loaded");
    }
    resources.audioClipHandle = *audioHandle;

    // AssetLease stores an AssetStore pointer. Move the system into its final
    // owner before acquiring the stream root/tileset and audio leases.
    resources.system = std::make_unique<Tina::Asset::AssetSystem>(std::move(*system));
    auto rootLease = resources.system->acquire(resources.tileMapHandle);
    auto tilesetLease = resources.system->acquire(resources.tilesetHandle);
    auto audioLease = resources.system->acquire(resources.audioClipHandle);
    if (!rootLease || !tilesetLease || !audioLease)
    {
        if (!rootLease)
        {
            return Tina::Core::failure(std::move(rootLease.error()));
        }
        if (!tilesetLease)
        {
            return Tina::Core::failure(std::move(tilesetLease.error()));
        }
        return Tina::Core::failure(std::move(audioLease.error()));
    }
    resources.audioClipLease = std::move(*audioLease);

    auto tileMapStream = Tina::Asset::TileMapStream::Create(
        *resources.system, std::move(*rootLease), std::move(*tilesetLease),
        Tina::Asset::TileMapStreamConfig{.residentCapacity = ExpectedTileMapStreamChunks,
                                         .requestBudgetPerUpdate = ExpectedTileMapStreamChunks,
                                         .retainMarginChunks = 0,
                                         .memoryResource = &resources.memory});
    if (!tileMapStream)
    {
        return Tina::Core::failure(std::move(tileMapStream.error()));
    }
    // TileMapGridCollision borrows TileMapInstance; emplace the stream at its
    // final address before constructing any borrower.
    resources.tileMapStream.emplace(std::move(*tileMapStream));

    // Product recipe is 8x4 with cooker chunk size 16: one visual and one
    // collision chunk. Make both resident before collision/Physics bootstrap.
    const Tina::Asset::TileChunkCameraQuery initialDemand{
        .centerX = 4.0F,
        .centerY = 2.0F,
        .halfWidth = 4.0F,
        .halfHeight = 2.0F,
    };
    if (const auto status = advanceTileMapStream(resources, counters, initialDemand); !status)
    {
        return status;
    }
    const Tina::Asset::TileMapChunkCoord rootChunk{};
    const Tina::Asset::TileMapInstance& residentMap = resources.tileMapStream->map();
    if (counters.tileMapStreamResident != ExpectedTileMapStreamChunks ||
        !residentMap.isChunkResident(VisualTileLayerId, rootChunk) ||
        !residentMap.isChunkResident(CollisionTileLayerId, rootChunk))
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::AssetNotReady,
                                   "initial visual/collision tilemap chunks are not resident");
    }
    if (const auto status = consumeGameplayObjectLayer(resources, counters); !status)
    {
        return status;
    }
    resources.grid.emplace(resources.tileMapStream->map(), CollisionTileLayerId);
    {
        // M11-B1 product path: fixed-capacity revision cache for visible chunks.
        auto dirty = Tina::Asset::TileChunkDirtyCache::Create(
            Tina::Asset::TileChunkDirtyCacheConfig{.capacity = 64, .memoryResource = &resources.memory});
        if (!dirty)
        {
            return Tina::Core::failure(std::move(dirty.error()));
        }
        resources.chunkDirtyCache.emplace(std::move(*dirty));
    }
    resources.controller.emplace(Tina::Asset::CharacterController2DConfig{
        .halfWidth = 0.3f,
        .halfHeight = 0.5f,
        .gravity = 40.0f,
        .maxFallSpeed = 50.0f,
        .skin = 0.01f,
    });
    resources.controller->teleport(resources.characterSpawnX, resources.characterSpawnY, true);

    if (const auto status = prepareCharacterAnimations(resources, counters, *resources.system, characterTextureId,
                                                       idleClipId, walkClipId, hitWallClipId);
        !status)
    {
        return status;
    }

#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
    Tina::Physics2D::PhysicsWorld2DConfig worldConfig;
    worldConfig.bodyCapacity = 64;
    worldConfig.shapeCapacity = 64;
    worldConfig.jointCapacity = 8;
    worldConfig.contactBeginCapacity = 32;
    worldConfig.contactEndCapacity = 32;
    worldConfig.contactHitCapacity = 8;
    worldConfig.commandCapacity = 16;
    worldConfig.solverSubStepCount = 1;
    worldConfig.gravityMetersPerSecondSquared = {0.0F, -20.0F};
    auto worldResult = Tina::Physics2D::PhysicsWorld2D::Create(worldConfig, resources.memory);
    if (!worldResult)
    {
        return Tina::Core::failure(std::move(worldResult.error()));
    }
    resources.physicsWorld.emplace(std::move(*worldResult));

    Tina::Physics2D::PhysicsGridBodySyncConfig2D syncConfig;
    syncConfig.cellSizeMeters = 0.0F;
    syncConfig.enableContactEvents = true;
    auto synced = Tina::Asset::syncTileMapSolidsToStaticBodies(
        *resources.grid, *resources.physicsWorld, syncConfig, resources.staticBodies, resources.solidCellScratch);
    if (!synced)
    {
        return Tina::Core::failure(std::move(synced.error()));
    }
    if (synced->written != ExpectedPhysicsStaticBodies)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "unexpected static tile body count");
    }

    Tina::Physics2D::PhysicsBody2DDesc dynamicDesc;
    dynamicDesc.type = Tina::Physics2D::PhysicsBodyType2D::Dynamic;
    dynamicDesc.positionMeters = {resources.lastDynamicX, resources.lastDynamicY};
    Tina::Physics2D::PhysicsShape2DDesc box;
    box.kind = Tina::Physics2D::PhysicsShapeKind2D::Box;
    box.halfExtentsMeters = {resources.dynamicHalfExtent, resources.dynamicHalfExtent};
    box.density = 1.0F;
    box.enableContactEvents = true;
    box.enableSensorEvents = true;
    auto dynamic = resources.physicsWorld->createBody(dynamicDesc);
    if (!dynamic)
    {
        return Tina::Core::failure(std::move(dynamic.error()));
    }
    auto dynamicShape = resources.physicsWorld->createShape(*dynamic, box);
    if (!dynamicShape)
    {
        return Tina::Core::failure(std::move(dynamicShape.error()));
    }
    resources.dynamicBody = *dynamic;
    resources.dynamicShape = *dynamicShape;
    resources.lastDynamicX = dynamicDesc.positionMeters.x;
    resources.lastDynamicY = dynamicDesc.positionMeters.y;

    Tina::Physics2D::PhysicsBody2DDesc sensorBodyDesc;
    sensorBodyDesc.type = Tina::Physics2D::PhysicsBodyType2D::Static;
    sensorBodyDesc.positionMeters = dynamicDesc.positionMeters;
    auto sensorBody = resources.physicsWorld->createBody(sensorBodyDesc);
    if (!sensorBody)
    {
        return Tina::Core::failure(std::move(sensorBody.error()));
    }
    Tina::Physics2D::PhysicsShape2DDesc sensorDesc;
    sensorDesc.kind = Tina::Physics2D::PhysicsShapeKind2D::Circle;
    sensorDesc.radiusMeters = 0.75F;
    sensorDesc.density = 0.0F;
    sensorDesc.isSensor = true;
    sensorDesc.enableSensorEvents = true;
    auto sensorShape = resources.physicsWorld->createShape(*sensorBody, sensorDesc);
    if (!sensorShape)
    {
        return Tina::Core::failure(std::move(sensorShape.error()));
    }
    resources.sensorBody = *sensorBody;
    resources.sensorShape = *sensorShape;

    Tina::Physics2D::PhysicsBody2DDesc anchorDesc;
    anchorDesc.type = Tina::Physics2D::PhysicsBodyType2D::Static;
    anchorDesc.positionMeters = {-20.0F, 10.0F};
    auto anchorBody = resources.physicsWorld->createBody(anchorDesc);
    if (!anchorBody)
    {
        return Tina::Core::failure(std::move(anchorBody.error()));
    }
    Tina::Physics2D::PhysicsBody2DDesc followerDesc;
    followerDesc.type = Tina::Physics2D::PhysicsBodyType2D::Dynamic;
    followerDesc.positionMeters = {-19.0F, 10.0F};
    auto followerBody = resources.physicsWorld->createBody(followerDesc);
    if (!followerBody)
    {
        return Tina::Core::failure(std::move(followerBody.error()));
    }
    Tina::Physics2D::PhysicsShape2DDesc followerShapeDesc;
    followerShapeDesc.kind = Tina::Physics2D::PhysicsShapeKind2D::Circle;
    followerShapeDesc.radiusMeters = 0.2F;
    followerShapeDesc.density = 1.0F;
    auto followerShape = resources.physicsWorld->createShape(*followerBody, followerShapeDesc);
    if (!followerShape)
    {
        return Tina::Core::failure(std::move(followerShape.error()));
    }
    Tina::Physics2D::PhysicsJoint2DDesc jointDesc;
    jointDesc.bodyA = *anchorBody;
    jointDesc.bodyB = *followerBody;
    jointDesc.lengthMeters = 1.0F;
    jointDesc.enableSpring = true;
    jointDesc.hertz = 3.0F;
    jointDesc.dampingRatio = 0.5F;
    auto joint = resources.physicsWorld->createJoint(jointDesc);
    if (!joint)
    {
        return Tina::Core::failure(std::move(joint.error()));
    }
    resources.jointAnchorBody = *anchorBody;
    resources.jointFollowerBody = *followerBody;
    resources.jointFollowerShape = *followerShape;
    resources.demoJoint = *joint;
    counters.physicsJointReady = resources.physicsWorld->jointState(*joint).has_value();
#endif

    const auto* audioFile = resources.audioClipLease.get();
    if (audioFile == nullptr)
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::AssetNotReady, "audioclip CPU missing after lease");
    }
    auto audioClip = Tina::Asset::parseAudioClipFromCooked(*audioFile);
    if (!audioClip)
    {
        return Tina::Core::failure(std::move(audioClip.error()));
    }
    counters.audioClipFrameCount = audioClip->frameCount;
    counters.audioClipSampleRate = audioClip->sampleRate;
    counters.audioFromCatalogLease = true;

    // M8-C1: Scene World for camera/character/(crate) extract path.
    if (const auto status = prepareSceneWorld(resources); !status)
    {
        return status;
    }
    catalogCleanup.release();
    return Tina::Core::success();
}

// Pause-style overlay: freezes base fixed/frame/render/UI while itself runs a few frames then pops.
// Product evidence for RUNTIME-001 stack + policy blocksBelow (does not own the product UI root).
class PauseOverlayState final : public Tina::IGameState {
  public:
    explicit PauseOverlayState(LifecycleCounters& counters) noexcept : counters_(&counters) {}

    Tina::Core::Status onEnter(Tina::GameStateEnterContext&) override
    {
        ++counters_->pauseOverlayPushes;
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        ++counters_->pauseOverlayPops;
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override
    {
        // Freeze sim/frame/UI below; keep base extract so the frozen world still draws and
        // product counters (renderExtractions vs frameUpdates) stay coherent.
        return Tina::GameStatePolicy{
            .blocksGameplayInputBelow = true,
            .blocksUIUpdateBelow = true,
            .blocksFixedUpdateBelow = true,
            .blocksFrameUpdateBelow = true,
            .blocksRenderBelow = false,
        };
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_->pauseOverlayFrames;
        // Keep overlay short so product smoke still finishes walk/physics evidence on base.
        if (counters_->pauseOverlayFrames >= 3U)
        {
            if (auto status = context.requestPop(); !status)
            {
                return status;
            }
        }
        return Tina::Core::success();
    }

  private:
    LifecycleCounters* counters_ = nullptr;
};

class TileMapBgfxState final : public Tina::IGameState {
  public:
    TileMapBgfxState(SampleOptions options, LifecycleCounters& counters, TileMapResources& resources,
                     Tina::Sample2D::DeviceCapture& capture) noexcept
        : options_(options), counters_(&counters), resources_(&resources), capture_(&capture)
    {
    }

    ~TileMapBgfxState() override
    {
        releaseSpriteBindings();
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        ++counters_->stateEnters;
        auto* device = capture_->get();
        if (device == nullptr || resources_->system == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "render device or catalog missing");
        }
        const auto* tileTextureFile = resources_->system->tryGet(resources_->tileTextureHandle);
        const auto* characterTextureFile = resources_->system->tryGet(resources_->characterTextureHandle);
        if (tileTextureFile == nullptr || characterTextureFile == nullptr)
        {
            return Tina::Core::failure(Tina::Asset::AssetErrorCode::AssetNotReady,
                                       "tile or character texture CPU payload is missing");
        }
        auto tileTexture = Tina::Asset::uploadTexture2DFromCooked(*device, *tileTextureFile);
        if (!tileTexture)
        {
            return Tina::Core::failure(std::move(tileTexture.error()));
        }
        tileGpuTexture_ = *tileTexture;
        auto characterTexture = Tina::Asset::uploadTexture2DFromCooked(*device, *characterTextureFile);
        if (!characterTexture)
        {
            return Tina::Core::failure(std::move(characterTexture.error()));
        }
        characterGpuTexture_ = *characterTexture;

        auto spriteBindings = Tina::Asset::Sprite2DBindingRegistry::Create(
            resources_->system->store(), *device,
            Tina::Asset::Sprite2DBindingRegistryConfig{
                .textureCapacity = ExpectedUploadedTextures,
                .memoryResource = &resources_->memory,
            });
        if (!spriteBindings)
        {
            return Tina::Core::failure(std::move(spriteBindings.error()));
        }
        spriteBindings_.emplace(std::move(*spriteBindings));
        spriteBindingResolverContext_.registry = &*spriteBindings_;
        spriteBindingResolverContext_.counters = counters_;

        auto tileSpriteKey =
            spriteBindings_->registerTextureBinding(resources_->tileTextureHandle, tileGpuTexture_);
        if (!tileSpriteKey)
        {
            return Tina::Core::failure(std::move(tileSpriteKey.error()));
        }
        tileSpriteKey_ = *tileSpriteKey;
        tileBindingRegistered_ = true;
        ++counters_->spriteBindingTextures;

        auto characterSpriteKey =
            spriteBindings_->registerTextureBinding(resources_->characterTextureHandle, characterGpuTexture_);
        if (!characterSpriteKey)
        {
            return Tina::Core::failure(std::move(characterSpriteKey.error()));
        }
        characterSpriteKey_ = *characterSpriteKey;
        characterBindingRegistered_ = true;
        ++counters_->spriteBindingTextures;
        counters_->texturesUploaded += 2U;

        if (const auto status = prepare2dEffects(*resources_, *counters_, characterSpriteKey_); !status)
        {
            return status;
        }
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
        if (resources_->physicsWorld)
        {
            counters_->physicsStaticBodies = ExpectedPhysicsStaticBodies;
            counters_->physicsReady = true;
        }
#endif

        auto rootBuilder = context.primaryWindowUIRootBuilder();
        if (!rootBuilder)
        {
            return Tina::Core::failure(std::move(rootBuilder.error()));
        }
        auto root = rootBuilder->createRoot();
        if (!root)
        {
            return Tina::Core::failure(std::move(root.error()));
        }
        auto tree = rootBuilder->treeUpdater(*root);
        if (!tree)
        {
            return Tina::Core::failure(std::move(tree.error()));
        }
        Tina::UI::UILayoutStyle rootStyle{};
        rootStyle.size.width = Tina::UI::UILayoutLength::Percent(100.0F);
        rootStyle.size.height = Tina::UI::UILayoutLength::Percent(100.0F);
        if (auto status = tree->setLayoutStyle(root->rootNodeId(), rootStyle); !status)
        {
            return status;
        }

        // Phase A/B product chrome tokens (not CSS Theme). Escape hatch remains UIBoxPaint.
        const Tina::UI::UITheme theme = Tina::UI::makeDefaultProductTheme();
        struct PanelSpec final {
            Tina::UI::UILayoutStyle layout{};
            Tina::UI::UIBoxPaint paint{};
        };
        // Top-left title plate (wide enough for CJK), right settings card with elevation, accent strip.
        const std::array panels{
            PanelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(16.0F), Tina::UI::UILayoutLength::Px(12.0F),
                                             Tina::UI::UILayoutLength::Px(320.0F), Tina::UI::UILayoutLength::Px(56.0F)),
                .paint = Tina::UI::makePanelBoxPaint(
                    theme, Tina::UI::scaleColorAlpha(theme.surface1, 230), Tina::UI::UIElevation::None),
            },
            PanelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(668.0F), Tina::UI::UILayoutLength::Px(8.0F),
                                             Tina::UI::UILayoutLength::Px(276.0F), Tina::UI::UILayoutLength::Px(448.0F)),
                .paint = Tina::UI::makePanelBoxPaint(
                    theme, Tina::UI::scaleColorAlpha(theme.surface0, 236), Tina::UI::UIElevation::Low),
            },
            PanelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(16.0F), Tina::UI::UILayoutLength::Px(480.0F),
                                             Tina::UI::UILayoutLength::Px(320.0F), Tina::UI::UILayoutLength::Px(10.0F)),
                .paint = Tina::UI::makeSolidBox(Tina::UI::scaleColorAlpha(theme.textAccent, 230)),
            },
        };
        for (const PanelSpec& panelSpec : panels)
        {
            auto panel = tree->createPanel(root->rootNodeId());
            if (!panel)
            {
                return Tina::Core::failure(std::move(panel.error()));
            }
            if (auto status = tree->setLayoutStyle(*panel, panelSpec.layout); !status)
            {
                return status;
            }
            if (auto status = tree->setBoxPaint(*panel, panelSpec.paint); !status)
            {
                return status;
            }
        }

        // HUD and settings labels. Without FreeType these paint as SolidQuad placeholder
        // bars; with FreeType (TINA_SAMPLE_TILEMAP_FREETYPE) Desktop-style SourceHan
        // injection yields real CJK glyphs.
        struct LabelSpec final {
            Tina::UI::UILayoutStyle layout{};
            std::string_view text{};
            Tina::UI::UITextStyle style{};
        };
        const std::array labels{
            LabelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(28.0F), Tina::UI::UILayoutLength::Px(20.0F),
                                             Tina::UI::UILayoutLength::Px(300.0F), Tina::UI::UILayoutLength::Px(28.0F)),
                .text = "TileMap 2D",
                .style = Tina::UI::makeTitleTextStyle(theme),
            },
            LabelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(28.0F), Tina::UI::UILayoutLength::Px(44.0F),
                                             Tina::UI::UILayoutLength::Px(300.0F), Tina::UI::UILayoutLength::Px(28.0F)),
                .text = "中文地图",
                .style = Tina::UI::makeAccentTextStyle(theme, 22.0F),
            },
            LabelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(684.0F), Tina::UI::UILayoutLength::Px(78.0F),
                                             Tina::UI::UILayoutLength::Px(72.0F), Tina::UI::UILayoutLength::Px(24.0F)),
                .text = "Master",
                .style = Tina::UI::makeBodyTextStyle(theme),
            },
            LabelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(684.0F), Tina::UI::UILayoutLength::Px(108.0F),
                                             Tina::UI::UILayoutLength::Px(72.0F), Tina::UI::UILayoutLength::Px(24.0F)),
                .text = "Music",
                .style = Tina::UI::makeBodyTextStyle(theme),
            },
            LabelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(684.0F), Tina::UI::UILayoutLength::Px(138.0F),
                                             Tina::UI::UILayoutLength::Px(72.0F), Tina::UI::UILayoutLength::Px(24.0F)),
                .text = "SFX",
                .style = Tina::UI::makeBodyTextStyle(theme),
            },
            LabelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(724.0F), Tina::UI::UILayoutLength::Px(174.0F),
                                             Tina::UI::UILayoutLength::Px(180.0F), Tina::UI::UILayoutLength::Px(24.0F)),
                .text = "Mute Master",
                .style = Tina::UI::makeSecondaryTextStyle(theme),
            },
            LabelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(724.0F), Tina::UI::UILayoutLength::Px(210.0F),
                                             Tina::UI::UILayoutLength::Px(180.0F), Tina::UI::UILayoutLength::Px(24.0F)),
                .text = "Mute Music",
                .style = Tina::UI::makeSecondaryTextStyle(theme),
            },
            LabelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(724.0F), Tina::UI::UILayoutLength::Px(246.0F),
                                             Tina::UI::UILayoutLength::Px(180.0F), Tina::UI::UILayoutLength::Px(24.0F)),
                .text = "Mute SFX",
                .style = Tina::UI::makeSecondaryTextStyle(theme),
            },
        };
        for (const LabelSpec& labelSpec : labels)
        {
            auto label = tree->createLabel(root->rootNodeId());
            if (!label)
            {
                return Tina::Core::failure(std::move(label.error()));
            }
            if (auto status = tree->setLayoutStyle(*label, labelSpec.layout); !status)
            {
                return status;
            }
            if (auto status = tree->setTextStyle(*label, labelSpec.style); !status)
            {
                return status;
            }
            if (auto status = tree->setText(*label, labelSpec.text); !status)
            {
                return status;
            }
        }

        // HUD Button is product UI surface for pointer/default-action path. Automated
        // smoke does not synthesize clicks; wiring + create counts are gated. Interactive
        // runs can click "Demo Button" (no world side-effect required for the JSON gate).
        {
            auto button = tree->createButton(root->rootNodeId());
            if (!button)
            {
                return Tina::Core::failure(std::move(button.error()));
            }
            if (auto status = tree->setLayoutStyle(
                    *button, absolutePanelStyle(Tina::UI::UILayoutLength::Px(700.0F),
                                                Tina::UI::UILayoutLength::Px(24.0F),
                                                Tina::UI::UILayoutLength::Px(136.0F),
                                                Tina::UI::UILayoutLength::Px(40.0F)));
                !status)
            {
                return status;
            }
            const Tina::UI::UIStraightSrgba8Color buttonNormal =
                Tina::UI::scaleColorAlpha(Tina::UI::rgb(0x287850), 230);
            if (auto status = tree->setBoxPaint(*button, Tina::UI::makeSolidBox(buttonNormal)); !status)
            {
                return status;
            }
            const Tina::UI::UIButtonPaint buttonPaint{
                .hoveredBackgroundColor = Tina::UI::scaleColorAlpha(Tina::UI::lightenChannel(buttonNormal, 28), 240),
                .pressedBackgroundColor = Tina::UI::darkenChannel(buttonNormal, 36),
                .focusedBackgroundColor = Tina::UI::rgb(0x529AD0, 245),
                .disabledBackgroundColor = Tina::UI::rgb(0x4C5258, 230),
            };
            if (auto status = tree->setButtonPaint(*button, buttonPaint); !status)
            {
                return status;
            }
            auto configuredButtonPaint = tree->buttonPaint(*button);
            if (!configuredButtonPaint || *configuredButtonPaint != buttonPaint)
            {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "Button visual paint round-trip failed");
            }
            counters_->uiButtonPaintVerified = true;
            if (auto status = tree->setTextStyle(
                    *button, Tina::UI::UITextStyle{
                                 .logicalSize = 18.0F,
                                 .advanceScale = 0.62F,
                                 .lineHeightScale = 1.15F,
                                  .color = theme.textPrimary,
                             });
                !status)
            {
                return status;
            }
            if (auto status = tree->setText(*button, "Demo Button"); !status)
            {
                return status;
            }
            if (auto status = tree->setButtonAction(
                    *button, Tina::UI::UIButtonActionCallback{[](const Tina::UI::UIButtonActionEvent&) noexcept {
                        // Intentionally empty: proves action slot wiring without side effects.
                    }});
                !status)
            {
                return status;
            }
            ++counters_->uiButtonsCreated;
            ++counters_->uiButtonActionsWired;

            // Keep the visual gate tied to the real retained enabled state. The
            // default path still queries the state so both modes verify the same
            // owner-thread contract without synthesizing paint or input state.
            if (options_.uiDisabledDemoButton)
            {
                if (auto status = tree->setEnabled(*button, false); !status)
                {
                    return status;
                }
            }
            auto enabled = tree->isEnabled(*button);
            if (!enabled)
            {
                return Tina::Core::failure(std::move(enabled.error()));
            }
            counters_->uiDemoButtonEnabled = *enabled;
            counters_->uiDisabledDemoButtonVerified =
                *enabled == !options_.uiDisabledDemoButton;
            if (!counters_->uiDisabledDemoButtonVerified)
            {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "Demo Button enabled state verification failed");
            }
        }

        // M11-C1/C2: Master/Music/SFX volume Sliders -> AudioEngine buses.
        // Layout stacked in the right-side settings surface. Smoke only requires create counts;
        // interactive drag applies via pending flags on next updateFrame.
        const auto wireVolumeSlider =
            [&](float y, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                float initialValue,
                float& pendingVolume, bool& hasPending, float& lastVolume, bool& fromSlider)
            -> Tina::Core::Status {
                auto slider = tree->createSlider(root->rootNodeId());
                if (!slider)
                {
                    return Tina::Core::failure(std::move(slider.error()));
                }
                if (auto status = tree->setLayoutStyle(
                        *slider, absolutePanelStyle(Tina::UI::UILayoutLength::Px(764.0F),
                                                    Tina::UI::UILayoutLength::Px(y),
                                                    Tina::UI::UILayoutLength::Px(158.0F),
                                                    Tina::UI::UILayoutLength::Px(20.0F)));
                    !status)
                {
                    return status;
                }
                if (auto status = tree->setBoxPaint(
                        *slider,
                        Tina::UI::makeSolidBox(Tina::UI::scaleColorAlpha(theme.surface2, 230)));
                    !status)
                {
                    return status;
                }
                if (auto status = tree->setSliderPaint(
                        *slider,
                        Tina::UI::UISliderPaint{
                            .filledTrackColor = Tina::UI::rgba8(r, g, b),
                            .thumbColor = theme.textPrimary,
                            .draggingThumbColor = theme.textAccent,
                            .contentInset = 4.0F,
                            .thumbWidth = 8.0F,
                        });
                    !status)
                {
                    return status;
                }
                if (auto status = tree->setSliderRange(*slider, 0.0F, 1.0F, 0.05F); !status)
                {
                    return status;
                }
                if (auto status = tree->setSliderValue(*slider, initialValue); !status)
                {
                    return status;
                }
                pendingVolume = initialValue;
                hasPending = true;
                if (auto status = tree->setSliderChangeCallback(
                        *slider,
                        Tina::UI::UISliderChangeCallback{[&, this](const Tina::UI::UISliderChangeEvent& event) noexcept {
                            ++counters_->uiSliderChanges;
                            lastVolume = event.value;
                            fromSlider = true;
                            pendingVolume = event.value;
                            hasPending = true;
                        }});
                    !status)
                {
                    return status;
                }
                ++counters_->uiSlidersCreated;
                return Tina::Core::success();
            };

        if (auto status = wireVolumeSlider(82.0F, 110, 130, 230, 0.75F,
                                           pendingMasterVolume_, hasPendingMasterVolume_,
                                           counters_->lastMasterVolume, counters_->masterVolumeFromSlider);
            !status)
        {
            return status;
        }
        if (auto status = wireVolumeSlider(112.0F, 70, 185, 125, 0.55F,
                                           pendingMusicVolume_, hasPendingMusicVolume_,
                                           counters_->lastMusicVolume, counters_->musicVolumeFromSlider);
            !status)
        {
            return status;
        }
        if (auto status = wireVolumeSlider(142.0F, 225, 155, 70, 0.35F,
                                           pendingSfxVolume_, hasPendingSfxVolume_,
                                           counters_->lastSfxVolume, counters_->sfxVolumeFromSlider);
            !status)
        {
            return status;
        }

        // M11-C3/C5: Master/Music/SFX mute Checkboxes (product settings surface).
        // UI toggles checked before the action; sample queues the matching mute transition.
        // Capture bus id by value so the action callback does not dangle on stack refs.
        enum class MuteBus : u8 { Master = 0, Music = 1, Sfx = 2 };
        const auto wireMuteCheckbox =
            [&](float y, std::uint8_t r, std::uint8_t g, std::uint8_t b, MuteBus bus,
                bool initiallyChecked) -> Tina::Core::Status {
                auto checkbox = tree->createCheckbox(root->rootNodeId());
                if (!checkbox)
                {
                    return Tina::Core::failure(std::move(checkbox.error()));
                }
                if (auto status = tree->setLayoutStyle(
                        *checkbox, absolutePanelStyle(Tina::UI::UILayoutLength::Px(684.0F),
                                                      Tina::UI::UILayoutLength::Px(y),
                                                      Tina::UI::UILayoutLength::Px(28.0F),
                                                      Tina::UI::UILayoutLength::Px(28.0F)));
                    !status)
                {
                    return status;
                }
                if (auto status = tree->setBoxPaint(*checkbox, solidFill(r, g, b, 230)); !status)
                {
                    return status;
                }
                if (auto status = tree->setCheckboxPaint(
                        *checkbox,
                        Tina::UI::UICheckboxPaint{
                            .checkedIndicatorColor = theme.textPrimary,
                            .checkedIndicatorInset = 6.0F,
                        });
                    !status)
                {
                    return status;
                }
                if (auto status = tree->setChecked(*checkbox, initiallyChecked); !status)
                {
                    return status;
                }
                if (initiallyChecked)
                {
                    switch (bus)
                    {
                    case MuteBus::Master:
                        masterMuteState_.pending = true;
                        break;
                    case MuteBus::Music:
                        musicMuteState_.pending = true;
                        break;
                    case MuteBus::Sfx:
                        sfxMuteState_.pending = true;
                        break;
                    }
                }
                if (auto status = tree->setCheckboxAction(
                        *checkbox,
                        Tina::UI::UIButtonActionCallback{[this, bus](const Tina::UI::UIButtonActionEvent&) noexcept {
                            ++counters_->uiCheckboxActions;
                            switch (bus)
                            {
                            case MuteBus::Master:
                                static_cast<void>(Tina::Sample2D::togglePendingAudioMute(masterMuteState_));
                                counters_->masterMutedFromCheckbox = true;
                                break;
                            case MuteBus::Music:
                                static_cast<void>(Tina::Sample2D::togglePendingAudioMute(musicMuteState_));
                                counters_->musicMutedFromCheckbox = true;
                                break;
                            case MuteBus::Sfx:
                                static_cast<void>(Tina::Sample2D::togglePendingAudioMute(sfxMuteState_));
                                counters_->sfxMutedFromCheckbox = true;
                                break;
                            }
                        }});
                    !status)
                {
                    return status;
                }
                ++counters_->uiCheckboxesCreated;
                return Tina::Core::success();
            };

        if (auto status = wireMuteCheckbox(172.0F, 120, 50, 50, MuteBus::Master, false); !status)
        {
            return status;
        }
        if (auto status = wireMuteCheckbox(208.0F, 50, 100, 70, MuteBus::Music, true); !status)
        {
            return status;
        }
        if (auto status = wireMuteCheckbox(244.0F, 110, 80, 40, MuteBus::Sfx, false); !status)
        {
            return status;
        }

        // Single-line profile-name TextEdit. It is separated from the final mute
        // checkbox by 16 px so the settings column remains readable at 960x540.
        {
            auto profileName = tree->createTextEdit(root->rootNodeId());
            if (!profileName)
            {
                return Tina::Core::failure(std::move(profileName.error()));
            }
            if (auto status = tree->setLayoutStyle(
                    *profileName, absolutePanelStyle(Tina::UI::UILayoutLength::Px(700.0F),
                                                     Tina::UI::UILayoutLength::Px(292.0F),
                                                     Tina::UI::UILayoutLength::Px(220.0F),
                                                     Tina::UI::UILayoutLength::Px(42.0F)));
                !status)
            {
                return status;
            }
            if (auto status = tree->setBoxPaint(
                    *profileName,
                    Tina::UI::makeSolidBox(Tina::UI::scaleColorAlpha(theme.surface2, 245)));
                !status)
            {
                return status;
            }
            if (auto status = tree->setTextStyle(
                    *profileName, Tina::UI::makeBodyTextStyle(theme, 22.0F));
                !status)
            {
                return status;
            }
            if (auto status = tree->setText(*profileName, InitialProfileNameText); !status)
            {
                return status;
            }
            if (auto status = tree->setTextSelection(
                    *profileName, Tina::UI::UITextSelection{
                                      .anchorCodepoint = InitialProfileNameCodepointCount,
                                      .caretCodepoint = InitialProfileNameCodepointCount,
                                  });
                !status)
            {
                return status;
            }
            auto initialText = tree->text(*profileName);
            if (!initialText)
            {
                return Tina::Core::failure(std::move(initialText.error()));
            }
            if (*initialText != InitialProfileNameText)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "profile-name TextEdit did not retain its initial UTF-8 text");
            }
            ++counters_->uiTextEditsCreated;
            counters_->uiTextEditInitialTextVerified = true;
        }

        // Determinate loading/status indicator. UIBoxPaint is the track and the
        // ProgressBar-specific paint emits the value-derived foreground fill.
        {
            auto progress = tree->createProgressBar(root->rootNodeId());
            if (!progress)
            {
                return Tina::Core::failure(std::move(progress.error()));
            }
            if (auto status = tree->setLayoutStyle(
                    *progress, absolutePanelStyle(Tina::UI::UILayoutLength::Px(700.0F),
                                                  Tina::UI::UILayoutLength::Px(350.0F),
                                                  Tina::UI::UILayoutLength::Px(220.0F),
                                                  Tina::UI::UILayoutLength::Px(20.0F)));
                !status)
            {
                return status;
            }
            if (auto status = tree->setBoxPaint(
                    *progress,
                    Tina::UI::makeSolidBox(Tina::UI::scaleColorAlpha(theme.surface2, 240)));
                !status)
            {
                return status;
            }
            if (auto status = tree->setProgressBarPaint(
                    *progress,
                    Tina::UI::UIProgressBarPaint{
                        .fillColor = theme.accent,
                    });
                !status)
            {
                return status;
            }
            if (auto status = tree->setProgressBarRange(*progress, 0.0F, 100.0F); !status)
            {
                return status;
            }
            if (auto status = tree->setProgressBarValue(*progress, 65.0F); !status)
            {
                return status;
            }
            auto value = tree->progressBarValue(*progress);
            if (!value)
            {
                return Tina::Core::failure(std::move(value.error()));
            }
            ++counters_->uiProgressBarsCreated;
            counters_->uiProgressBarValueVerified = *value == 65.0F;
        }

        // Same-parent RadioButtons form one exclusive display-mode group. The
        // label text is also the committed semantics name.
        {
            struct RadioSpec final {
                float y = 0.0F;
                std::string_view text{};
            };
            constexpr std::array radioSpecs{
                RadioSpec{.y = 386.0F, .text = "Windowed"},
                RadioSpec{.y = 416.0F, .text = "Fullscreen"},
            };
            std::array<Tina::UI::UINodeId, radioSpecs.size()> radioButtons{};
            for (std::size_t index = 0; index < radioSpecs.size(); ++index)
            {
                auto radioButton = tree->createRadioButton(root->rootNodeId());
                if (!radioButton)
                {
                    return Tina::Core::failure(std::move(radioButton.error()));
                }
                radioButtons[index] = *radioButton;
                if (auto status = tree->setLayoutStyle(
                        *radioButton, absolutePanelStyle(Tina::UI::UILayoutLength::Px(700.0F),
                                                        Tina::UI::UILayoutLength::Px(radioSpecs[index].y),
                                                        Tina::UI::UILayoutLength::Px(220.0F),
                                                        Tina::UI::UILayoutLength::Px(28.0F)));
                    !status)
                {
                    return status;
                }
                if (auto status = tree->setRadioButtonPaint(
                        *radioButton,
                        Tina::UI::UIRadioButtonPaint{
                            .indicatorColor = theme.surface2,
                            .selectedIndicatorColor = theme.textAccent,
                            .selectedIndicatorInset = 6.0F,
                            .labelGap = 8.0F,
                            .focusedIndicatorColor = Tina::UI::rgb(0x529AD0, 245),
                            .pressedIndicatorColor = Tina::UI::darkenChannel(theme.accent, 40),
                        });
                    !status)
                {
                    return status;
                }
                if (auto status = tree->setTextStyle(
                        *radioButton,
                        Tina::UI::makeBodyTextStyle(theme));
                    !status)
                {
                    return status;
                }
                if (auto status = tree->setText(*radioButton, radioSpecs[index].text); !status)
                {
                    return status;
                }
                if (auto status = tree->setRadioButtonAction(
                        *radioButton,
                        Tina::UI::UIButtonActionCallback{
                            [](const Tina::UI::UIButtonActionEvent&) noexcept {}});
                    !status)
                {
                    return status;
                }
                ++counters_->uiRadioButtonsCreated;
                ++counters_->uiRadioButtonActionsWired;
            }
            if (auto status = tree->setRadioButtonSelected(radioButtons[0], true); !status)
            {
                return status;
            }
            auto firstSelected = tree->isRadioButtonSelected(radioButtons[0]);
            auto secondSelected = tree->isRadioButtonSelected(radioButtons[1]);
            if (!firstSelected)
            {
                return Tina::Core::failure(std::move(firstSelected.error()));
            }
            if (!secondSelected)
            {
                return Tina::Core::failure(std::move(secondSelected.error()));
            }
            counters_->uiRadioSelectionVerified = *firstSelected && !*secondSelected;
        }

        uiRoot_ = std::move(*root);
        ++counters_->uiRootsCreated;
        counters_->uiPanelsCreated += panels.size();
        counters_->uiTextLabelsCreated += labels.size();
#if defined(TINA_SAMPLE_TILEMAP_FREETYPE)
        productUiVisibilityStartedAt_ = std::chrono::steady_clock::now();
#endif
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
#if defined(TINA_SAMPLE_TILEMAP_AUDIO_MINIAUDIO)
        if (resources_->audioDevice.has_value())
        {
            counters_->audioDeviceCallbacks = resources_->audioDevice->callbackInvocations();
            resources_->audioDevice->stop();
            resources_->audioDevice->shutdown();
            resources_->audioDevice.reset();
            counters_->audioDeviceStarted = false;
        }
#endif
        if (uiRoot_)
        {
            uiRoot_.reset();
            ++counters_->uiRootsReleased;
        }
        accessibilityProbe_.clear();
        accessibilityTree_ = Tina::UI::UIAccessibilityTree{};
        releaseSpriteBindings();
        ++counters_->stateExits;
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override { return {}; }

    Tina::Core::Status fixedUpdate(Tina::FixedUpdateContext& context) override
    {
        if (!resources_->tileMapStream)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "tilemap selection map missing");
        }

        const Tina::Asset::TileMapInstance& map = resources_->tileMapStream->map();
        const Tina::Sample2D::TileSelectionGrid grid{
            .widthCells = map.widthCells(),
            .heightCells = map.heightCells(),
            .cellSizeMeters = map.cellSizeMeters(),
        };

        // Controlled product gate (M10-A44): once per run, inject a locked
        // Started transition with a hit WorldPointerSample into the same consumer as A42
        // edges. This is sample-private (not OS/GLFW injection) and does not
        // re-project with live Camera/Platform.
        if (options_.seedTileSelection && !counters_->seedTileSelectionApplied)
        {
            auto scripted = Tina::Sample2D::makeScriptedWorldCellPress(
                SelectTileAction, grid, options_.seedTileCellX, options_.seedTileCellY, /*sourceSequence=*/9001);
            if (!scripted)
            {
                return Tina::Core::failure(std::move(scripted.error()));
            }
            const Tina::SimulationActionTransition seededTransition = *scripted;
            Tina::Sample2D::consumeTileSelectionTransitions(std::span{&seededTransition, 1}, SelectTileAction, grid,
                                                            counters_->tileSelection);
            if (!counters_->tileSelection.lastSelection.has_value() ||
                counters_->tileSelection.lastSelection->cellX != options_.seedTileCellX ||
                counters_->tileSelection.lastSelection->cellY != options_.seedTileCellY)
            {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "seed tile selection did not lock the requested cell");
            }
            counters_->seedTileSelectionApplied = true;
            auto tileId = map.tileIdAt(VisualTileLayerId, options_.seedTileCellX, options_.seedTileCellY);
            if (!tileId)
            {
                return Tina::Core::failure(std::move(tileId.error()));
            }
            counters_->lastSelectedTileId = *tileId;
        }

        const u64 previousSelectionHits = counters_->tileSelection.selectionHits;
        Tina::Sample2D::consumeTileSelectionTransitions(context.simulationActions().transitions, SelectTileAction,
                                                        grid, counters_->tileSelection);

        if (counters_->tileSelection.selectionHits != previousSelectionHits &&
            counters_->tileSelection.lastSelection.has_value())
        {
            const Tina::Sample2D::SelectedTile& selection = *counters_->tileSelection.lastSelection;
            auto tileId = map.tileIdAt(VisualTileLayerId, selection.cellX, selection.cellY);
            if (!tileId)
            {
                return Tina::Core::failure(std::move(tileId.error()));
            }
            counters_->lastSelectedTileId = *tileId;
        }
        return Tina::Core::success();
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_->frameUpdates;
        if (Tina::Audio::AudioEngine* audio = context.audioEngine(); audio != nullptr)
        {
            counters_->audioEnginePresent = true;
            if (hasPendingMasterVolume_)
            {
                if (auto status = audio->setBusVolume(Tina::Audio::AudioBusId::Master, pendingMasterVolume_);
                    !status)
                {
                    return Tina::Core::failure(std::move(status.error()));
                }
                hasPendingMasterVolume_ = false;
            }
            if (hasPendingMusicVolume_)
            {
                if (auto status = audio->setBusVolume(Tina::Audio::AudioBusId::Music, pendingMusicVolume_); !status)
                {
                    return Tina::Core::failure(std::move(status.error()));
                }
                hasPendingMusicVolume_ = false;
            }
            if (hasPendingSfxVolume_)
            {
                if (auto status = audio->setBusVolume(Tina::Audio::AudioBusId::Sfx, pendingSfxVolume_); !status)
                {
                    return Tina::Core::failure(std::move(status.error()));
                }
                hasPendingSfxVolume_ = false;
            }
            if (masterMuteState_.pending.has_value())
            {
                if (auto status = audio->setBusMuted(Tina::Audio::AudioBusId::Master, *masterMuteState_.pending);
                    !status)
                {
                    return Tina::Core::failure(std::move(status.error()));
                }
                Tina::Sample2D::commitPendingAudioMute(masterMuteState_);
            }
            if (musicMuteState_.pending.has_value())
            {
                if (auto status = audio->setBusMuted(Tina::Audio::AudioBusId::Music, *musicMuteState_.pending);
                    !status)
                {
                    return Tina::Core::failure(std::move(status.error()));
                }
                Tina::Sample2D::commitPendingAudioMute(musicMuteState_);
            }
            if (sfxMuteState_.pending.has_value())
            {
                if (auto status = audio->setBusMuted(Tina::Audio::AudioBusId::Sfx, *sfxMuteState_.pending); !status)
                {
                    return Tina::Core::failure(std::move(status.error()));
                }
                Tina::Sample2D::commitPendingAudioMute(sfxMuteState_);
            }
            if (auto bus = audio->busState(Tina::Audio::AudioBusId::Master); bus.has_value())
            {
                counters_->lastMasterVolume = bus->volume;
                counters_->lastMasterMuted = bus->muted;
                masterMuteState_.committed = bus->muted;
            }
            if (auto bus = audio->busState(Tina::Audio::AudioBusId::Music); bus.has_value())
            {
                counters_->lastMusicVolume = bus->volume;
                counters_->lastMusicMuted = bus->muted;
                musicMuteState_.committed = bus->muted;
            }
            if (auto bus = audio->busState(Tina::Audio::AudioBusId::Sfx); bus.has_value())
            {
                counters_->lastSfxVolume = bus->volume;
                counters_->lastSfxMuted = bus->muted;
                sfxMuteState_.committed = bus->muted;
            }
            if (!counters_->audioOneShotQueued)
            {
                // N7-A: play cooked AudioClip held by AssetLease and configure
                // gain/pitch/pan before the queued Play is applied by the Host pump.
                if (!resources_->audioClipLease)
                {
                    return Tina::Core::failure(Tina::Asset::AssetErrorCode::AssetNotReady,
                                               "audioclip lease missing for product SFX");
                }
                const auto* audioFile = resources_->audioClipLease.get();
                if (audioFile == nullptr)
                {
                    return Tina::Core::failure(Tina::Asset::AssetErrorCode::AssetNotReady,
                                               "audioclip payload missing under lease");
                }
                auto clip = Tina::Asset::parseAudioClipFromCooked(*audioFile);
                if (!clip)
                {
                    return Tina::Core::failure(std::move(clip.error()));
                }
                auto pcmView = Tina::Audio::pcmClipViewFromAudioClipPayload(*clip);
                if (!pcmView)
                {
                    return Tina::Core::failure(std::move(pcmView.error()));
                }
                auto voice = audio->playOneShotPcm(*pcmView);
                if (!voice)
                {
                    return Tina::Core::failure(std::move(voice.error()));
                }
                if (auto status = audio->setVoiceGain(*voice, 0.8F); !status)
                {
                    return Tina::Core::failure(std::move(status.error()));
                }
                if (auto status = audio->setVoicePitch(*voice, 0.75F); !status)
                {
                    return Tina::Core::failure(std::move(status.error()));
                }
                if (auto status = audio->setVoicePan(*voice, -0.25F); !status)
                {
                    return Tina::Core::failure(std::move(status.error()));
                }
                auto playbackState = audio->voicePlaybackState(*voice);
                if (!playbackState)
                {
                    return Tina::Core::failure(std::move(playbackState.error()));
                }
                counters_->audioVoiceGain = playbackState->gain;
                counters_->audioPitch = playbackState->pitch;
                counters_->audioPan = playbackState->pan;
                counters_->audioVoiceParamsConfigured =
                    playbackState->gain == 0.8F && playbackState->pitch == 0.75F &&
                    playbackState->pan == -0.25F;
                audioOneShotVoice_ = *voice;
                counters_->audioOneShotQueued = true;
                counters_->audioFromCatalogLease = true;
                counters_->audioClipFrameCount = clip->frameCount;
                counters_->audioClipSampleRate = clip->sampleRate;
            }

            // Host pumps completions after updateFrame; Started/Stopped counters
            // become visible on a following frame.
            if (auto stats = audio->stats(); stats.has_value())
            {
                counters_->audioStartedCount = stats->completedStarted;
                counters_->audioStoppedCount = stats->completedStopped;
                if (stats->completedStarted > 0)
                {
                    counters_->audioStartedObserved = true;
                }
#if defined(TINA_SAMPLE_TILEMAP_AUDIO_MINIAUDIO)
                counters_->audioMixFramesRendered = stats->mixFramesRendered;
#endif
            }

            // Exercise fade start/cancel/fade-to-stop deterministically before
            // starting the asynchronous miniaudio device. Each explicit mix call
            // is one callback block and keeps the product gate independent of OS
            // thread scheduling.
            if (counters_->audioStartedObserved && audioOneShotVoice_.hasValue() && !audioFadeStopMixed_)
            {
                if (auto status = audio->startVoiceFade(
                        audioOneShotVoice_,
                        Tina::Audio::AudioVoiceFadeDesc{
                            .targetGain = 0.2F,
                            .duration = Tina::Core::Duration{128.0 / 48000.0},
                            .endAction = Tina::Audio::AudioFadeEndAction::KeepPlaying,
                        });
                    !status)
                {
                    return Tina::Core::failure(std::move(status.error()));
                }
                std::array<float, 128> fadeStartOutput{};
                audio->mixRealtime(fadeStartOutput.data(), 64, 2, 48000);
                auto fadingState = audio->voicePlaybackState(audioOneShotVoice_);
                if (!fadingState)
                {
                    return Tina::Core::failure(std::move(fadingState.error()));
                }
                counters_->audioFadeStarted =
                    fadingState->fadeActive && fadingState->gain < 0.8F && fadingState->gain > 0.2F;

                if (auto status = audio->cancelVoiceFade(audioOneShotVoice_); !status)
                {
                    return Tina::Core::failure(std::move(status.error()));
                }
                std::array<float, 128> fadeCancelOutput{};
                audio->mixRealtime(fadeCancelOutput.data(), 64, 2, 48000);
                auto cancelledState = audio->voicePlaybackState(audioOneShotVoice_);
                if (!cancelledState)
                {
                    return Tina::Core::failure(std::move(cancelledState.error()));
                }
                counters_->audioFadeCancelled =
                    !cancelledState->fadeActive && cancelledState->gain > 0.2F && cancelledState->gain < 0.8F;

                if (auto status = audio->startVoiceFade(
                        audioOneShotVoice_,
                        Tina::Audio::AudioVoiceFadeDesc{
                            .targetGain = 0.0F,
                            .duration = Tina::Core::Duration{64.0 / 48000.0},
                            .endAction = Tina::Audio::AudioFadeEndAction::StopVoice,
                        });
                    !status)
                {
                    return Tina::Core::failure(std::move(status.error()));
                }
                std::array<float, 128> fadeStopOutput{};
                audio->mixRealtime(fadeStopOutput.data(), 64, 2, 48000);
                audioFadeStopMixed_ = true;
            }

            if (audioFadeStopMixed_ && audioOneShotVoice_.hasValue())
            {
                auto live = audio->isVoiceLive(audioOneShotVoice_);
                if (!live)
                {
                    return Tina::Core::failure(std::move(live.error()));
                }
                if (!*live)
                {
                    counters_->audioOneShotRetired = true;
                    counters_->audioFadeStopped = counters_->audioStoppedCount > 0;
                    audioOneShotVoice_ = {};
                }
            }

            // N7-B: after N7-A retires, feed the same catalog-held PCM through
            // the fixed-capacity stream ring, signal EOF, and let the Host apply
            // the queued Play at the frame boundary.
            if (counters_->audioOneShotRetired && !counters_->audioStreamQueued)
            {
                const auto* audioFile = resources_->audioClipLease.get();
                if (audioFile == nullptr)
                {
                    return Tina::Core::failure(Tina::Asset::AssetErrorCode::AssetNotReady,
                                               "audioclip payload missing for bounded PCM stream");
                }
                auto clip = Tina::Asset::parseAudioClipFromCooked(*audioFile);
                if (!clip)
                {
                    return Tina::Core::failure(std::move(clip.error()));
                }
                auto pcmView = Tina::Audio::pcmClipViewFromAudioClipPayload(*clip);
                if (!pcmView)
                {
                    return Tina::Core::failure(std::move(pcmView.error()));
                }
                if (pcmView->frameCount != ExpectedAudioClipFrames)
                {
                    return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                               "product audioclip frame count changed");
                }
                if (pcmView->frameCount >
                    static_cast<Tina::Core::u64>(Tina::Audio::AudioEngineConfig{}.streamBufferFrameCapacity))
                {
                    return Tina::Core::failure(Tina::Core::CoreErrorCode::CapacityExceeded,
                                               "product audioclip exceeds default PCM stream capacity");
                }

                auto voice = audio->playPcmStream(Tina::Audio::AudioPcmStreamDesc{
                    .channels = pcmView->channels,
                    .sampleRate = pcmView->sampleRate,
                    .bufferCapacityFrames = static_cast<Tina::Core::usize>(pcmView->frameCount),
                });
                if (!voice)
                {
                    return Tina::Core::failure(std::move(voice.error()));
                }
                if (auto status = audio->submitPcmStreamFrames(
                        *voice,
                        Tina::Audio::AudioPcmStreamChunkView{
                            .frames = pcmView->frames,
                            .frameCount = pcmView->frameCount,
                        });
                    !status)
                {
                    static_cast<void>(audio->cancelPcmStream(*voice));
                    return Tina::Core::failure(std::move(status.error()));
                }
                counters_->audioStreamSubmitted = true;
                counters_->audioStreamSubmittedFrames = pcmView->frameCount;
                if (auto status = audio->signalPcmStreamEof(*voice); !status)
                {
                    static_cast<void>(audio->cancelPcmStream(*voice));
                    return Tina::Core::failure(std::move(status.error()));
                }
                counters_->audioStreamEofSignaled = true;
                counters_->audioStreamQueued = true;
                audioStreamVoice_ = *voice;
            }

            if (audioStreamVoice_.hasValue() && !counters_->audioStreamStartedObserved)
            {
                auto playing = audio->isVoicePlaying(audioStreamVoice_);
                if (!playing)
                {
                    return Tina::Core::failure(std::move(playing.error()));
                }
                counters_->audioStreamStartedObserved = *playing;
            }

            // A fixed 480-frame block makes EOF drain deterministic and proves
            // submitted == consumed with no callback-thread scheduling dependency.
            if (counters_->audioStreamStartedObserved && audioStreamVoice_.hasValue() && !audioStreamMixed_)
            {
                std::array<float, static_cast<std::size_t>(ExpectedAudioClipFrames) * 2U> streamOutput{};
                audio->mixRealtime(streamOutput.data(), ExpectedAudioClipFrames, 2, 48000);
                auto streamState = audio->pcmStreamState(audioStreamVoice_);
                if (!streamState)
                {
                    return Tina::Core::failure(std::move(streamState.error()));
                }
                counters_->audioStreamConsumedFrames = streamState->consumedFrames;
                counters_->audioStreamUnderrunFrames = streamState->underrunFrames;
                counters_->audioStreamMixed = streamState->consumedFrames == ExpectedAudioClipFrames;
                counters_->audioStreamDrained =
                    streamState->eofSignaled && streamState->bufferedFrames == 0 &&
                    streamState->submittedFrames == ExpectedAudioClipFrames &&
                    streamState->consumedFrames == ExpectedAudioClipFrames &&
                    streamState->underrunFrames == 0;
                audioStreamMixed_ = true;
            }

            if (audioStreamMixed_ && audioStreamVoice_.hasValue())
            {
                auto live = audio->isVoiceLive(audioStreamVoice_);
                if (!live)
                {
                    return Tina::Core::failure(std::move(live.error()));
                }
                if (!*live)
                {
                    counters_->audioStreamRetired = true;
                    // The generation-qualified stream id was observed playing and
                    // became stale only after its terminal completion was pumped.
                    counters_->audioStreamStopped = counters_->audioStreamStartedObserved;
                    audioStreamVoice_ = {};
                }
            }

#if defined(TINA_SAMPLE_TILEMAP_AUDIO_MINIAUDIO)
            // Start the null device only after deterministic N7-A/N7-B evidence
            // has retired both transient voices, avoiding callback/test contention.
            if (counters_->audioStreamRetired && !resources_->audioDevice.has_value())
            {
                auto device = Tina::Audio::MiniaudioDevice::Create(Tina::Audio::MiniaudioDeviceConfig{
                    .useNullBackend = true,
                    .sampleRate = 48000,
                    .channels = 2,
                    .periodFrames = 256,
                });
                if (!device)
                {
                    return Tina::Core::failure(std::move(device.error()));
                }
                device->attachMixer(audio);
                if (auto status = device->start(); !status)
                {
                    return Tina::Core::failure(std::move(status.error()));
                }
                counters_->audioDeviceCreated = true;
                counters_->audioDeviceStarted = device->isRunning();
                counters_->audioDeviceNullBackend = device->isNullBackend();
                resources_->audioDevice = std::move(*device);
            }
            else if (resources_->audioDevice.has_value() && resources_->audioDevice->isRunning())
            {
                counters_->audioDeviceCallbacks = resources_->audioDevice->callbackInvocations();
            }
#endif
        }

        if (!resources_->particles || !resources_->trail)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "2D particle and trail systems are not initialized");
        }
        auto particleUpdate = resources_->particles->update(context.frameTiming().fixedDelta);
        if (!particleUpdate)
        {
            return Tina::Core::failure(std::move(particleUpdate.error()));
        }
        counters_->particleExpired += particleUpdate->expired;
        counters_->particleActive = particleUpdate->alive;
        if (const auto status = resources_->trail->update(context.frameTiming().fixedDelta); !status)
        {
            return status;
        }
        counters_->trailActive = resources_->trail->segmentCount();

        // Stream both render and collision layers from the current simulation
        // camera before CharacterController2D queries the borrowed collision grid.
        const float streamAspect =
            (counters_->surfacePixelHeight != 0)
                ? static_cast<float>(static_cast<double>(counters_->surfacePixelWidth) /
                                     static_cast<double>(counters_->surfacePixelHeight))
                : (10.0F / 6.0F);
        const Tina::Asset::TileChunkCameraQuery streamCamera{
            .centerX = resources_->cameraCurrentX,
            .centerY = resources_->cameraCurrentY,
            .halfWidth = ProductCameraHeightMeters * 0.5F * streamAspect,
            .halfHeight = ProductCameraHeightMeters * 0.5F,
        };
        if (const auto status = advanceTileMapStream(*resources_, *counters_, streamCamera); !status)
        {
            return status;
        }
        if (resources_->controller && resources_->grid)
        {
            // Hermetic product demo: after first ground contact, walk right until wall.
            // Keyboard and LeftX contributions share scalar Move Actions.
            const float moveLeft = context.frameActions().value(MoveLeftAction);
            const float moveRight = context.frameActions().value(MoveRightAction);
            float wishX = (moveRight - moveLeft) * DemoWalkSpeedMetersPerSecond;
            if (wishX == 0.0f && counters_->controllerGroundedFrames > 0)
            {
                wishX = DemoWalkSpeedMetersPerSecond;
            }

            if (auto status = resources_->controller->move(
                    *resources_->grid, 1.0f / 60.0f,
                    Tina::Asset::CharacterController2DMoveInput{.wishVelocityX = wishX}, resources_->solidScratch);
                !status)
            {
                return status;
            }
            const auto& st = resources_->controller->state();
            if (st.grounded)
            {
                ++counters_->controllerGroundedFrames;
            }
            if (wishX > 0.0f)
            {
                ++counters_->controllerWalkFrames;
            }
            if (st.hitRight)
            {
                ++counters_->controllerHitRightFrames;
            }
            if (st.positionX > counters_->maxControllerX)
            {
                counters_->maxControllerX = st.positionX;
            }

            CharacterAnimationState animationState = CharacterAnimationState::Idle;
            if (st.hitRight)
            {
                animationState = CharacterAnimationState::HitWall;
            }
            else if (st.grounded && std::abs(wishX) > 0.0F)
            {
                animationState = CharacterAnimationState::Walk;
            }
            if (auto status = setCharacterAnimationState(*resources_, *counters_, animationState); !status)
            {
                return status;
            }
            // Product evidence is deterministic even when --frame-delay-ms=0;
            // the runtime Animator itself accepts arbitrary caller-owned deltas.
            if (auto status = updateCharacterAnimation(
                    *resources_, *counters_, context.frameTiming().fixedDelta);
                !status)
            {
                return status;
            }

            // M11-B2: direct follow target after character step. previous = last
            // current; extract lerps with FrameTiming.interpolation (snap later).
            resources_->cameraPreviousX = resources_->cameraCurrentX;
            resources_->cameraPreviousY = resources_->cameraCurrentY;
            float followX = st.positionX;
            float followY = st.positionY;
            const float aspect =
                (counters_->surfacePixelHeight != 0)
                    ? static_cast<float>(static_cast<double>(counters_->surfacePixelWidth) /
                                         static_cast<double>(counters_->surfacePixelHeight))
                    : (10.0f / 6.0f);
            if (resources_->tileMapStream)
            {
                clampCameraCenterToMap(resources_->tileMapStream->map(), ProductCameraHeightMeters, aspect, followX,
                                       followY);
            }
            resources_->cameraCurrentX = followX;
            resources_->cameraCurrentY = followY;
            ++counters_->cameraFollowUpdates;
            if (!counters_->cameraFollowPrimed)
            {
                counters_->minCameraCenterX = followX;
                counters_->maxCameraCenterX = followX;
                counters_->cameraFollowPrimed = true;
            }
            else
            {
                counters_->minCameraCenterX = (std::min)(counters_->minCameraCenterX, followX);
                counters_->maxCameraCenterX = (std::max)(counters_->maxCameraCenterX, followX);
            }
        }
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
        if (resources_->physicsWorld)
        {
            if (auto status = resources_->physicsWorld->step(); !status)
            {
                return status;
            }
            ++counters_->physicsSteps;
            auto contacts = resources_->physicsWorld->contactEvents();
            if (!contacts)
            {
                return Tina::Core::failure(std::move(contacts.error()));
            }
            for (const auto& begin : contacts->beginEvents)
            {
                if (begin.isSensor && begin.shapeA == resources_->sensorShape)
                {
                    ++counters_->physicsSensorEnters;
                }
                else if (begin.bodyA == resources_->dynamicBody || begin.bodyB == resources_->dynamicBody)
                {
                    ++counters_->physicsDynamicContacts;
                }
            }
            for (const auto& end : contacts->endEvents)
            {
                if (end.isSensor && end.shapeA == resources_->sensorShape)
                {
                    ++counters_->physicsSensorExits;
                }
            }
            if (auto state = resources_->physicsWorld->bodyState(resources_->dynamicBody); state)
            {
                resources_->lastDynamicX = state->positionMeters.x;
                resources_->lastDynamicY = state->positionMeters.y;
                counters_->lastDynamicY = state->positionMeters.y;
            }
        }
#endif
        if (options_.frameDelayMilliseconds != 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{options_.frameDelayMilliseconds});
        }
#if defined(TINA_SAMPLE_TILEMAP_FREETYPE)
        else
        {
            const u32 targetElapsedMilliseconds = Tina::Sample2D::productUiTargetElapsedMilliseconds(
                counters_->frameUpdates, options_.targetFrameCount);
            std::this_thread::sleep_until(
                productUiVisibilityStartedAt_ + std::chrono::milliseconds{targetElapsedMilliseconds});
        }
#endif
        // Late-run pause overlay (RUNTIME-001 product evidence). Requires long enough smoke so
        // walk/physics still complete on base before the push. Short --frames=30 skips this.
        constexpr u64 kMinFramesForPauseDemo = 60;
        if (options_.targetFrameCount >= kMinFramesForPauseDemo && !counters_->pauseOverlayPushQueued
            && counters_->pauseOverlayPushes == 0
            && counters_->frameUpdates + 12U == options_.targetFrameCount)
        {
            if (auto status = context.requestPush(std::make_unique<PauseOverlayState>(*counters_)); !status)
            {
                return status;
            }
            counters_->pauseOverlayPushQueued = true;
        }

        if (counters_->frameUpdates >= options_.targetFrameCount)
        {
            // Request pixel capture on this frame's upcoming present (M11-D1).
            if (capture_ != nullptr)
            {
                capture_->requestCaptureNextPresent();
            }
            context.requestExitAfterFrame();
        }
        return Tina::Core::success();
    }

    Tina::Core::Status extractRenderScene(Tina::RenderSceneExtractionContext& context) const override
    {
        if (!resources_->tileMapStream || !resources_->controller || !resources_->sceneWorld ||
            !resources_->particles || !resources_->trail)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "tilemap state not ready");
        }

        auto& writer = context.renderSceneWriter();
        // Suspended surface (0×0): skip world extract; not a Camera config error.
        if (counters_->surfacePixelWidth == 0 || counters_->surfacePixelHeight == 0)
        {
            ++counters_->renderExtractions;
            return Tina::Core::success();
        }

        // previous/current interpolation -> presentation camera pose.
        // M8-C1: sample mutates Scene World camera transform immediately before
        // extract so extractRenderSceneFromWorld sees the interpolated center.
        // Character/crate use current sim pose (same as pre-C1 direct emit).
        const double alpha = context.frameTiming().interpolation;
        const float alphaF =
            static_cast<float>(std::clamp(alpha, 0.0, 1.0));
        const float centerX =
            resources_->cameraPreviousX + (resources_->cameraCurrentX - resources_->cameraPreviousX) * alphaF;
        const float centerY =
            resources_->cameraPreviousY + (resources_->cameraCurrentY - resources_->cameraPreviousY) * alphaF;

        Tina::Scene::World& sceneWorld = *resources_->sceneWorld;
        if (const auto status =
                sceneWorld.setLocalTransform(resources_->cameraEntity, sceneTranslation(centerX, centerY));
            !status)
        {
            return status;
        }
        const auto& st = resources_->controller->state();
        if (const auto status = sceneWorld.setLocalTransform(
                resources_->characterEntity, sceneTranslation(st.positionX, st.positionY));
            !status)
        {
            return status;
        }
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
        if (const auto status = sceneWorld.setLocalTransform(
                resources_->crateEntity, sceneTranslation(resources_->lastDynamicX, resources_->lastDynamicY));
            !status)
        {
            return status;
        }
#endif

        // Scene extract: Camera2D + SpriteRenderer2D (character [+ crate]).
        // TileMap tiles stay feature-side; selection highlight stays sample-side.
        if (const auto status = Tina::Scene::extractRenderSceneFromWorld(
                sceneWorld,
                writer,
                Tina::Scene::ExtractRenderSceneParams{
                    .surfaceViewport =
                        Tina::Render::Camera2DSurfaceViewport{
                            .pixelWidth = counters_->surfacePixelWidth,
                            .pixelHeight = counters_->surfacePixelHeight,
                        },
                    .spriteBindingResolver = Tina::Scene::Sprite2DBindingResolver{
                        .userData = &spriteBindingResolverContext_,
                        .resolve = &TileMapBgfxState::resolveSceneSpriteBinding,
                    },
                });
            !status)
        {
            return status;
        }

        // Gate counters from resolved camera fields after Scene extract.
        const Tina::Render::Camera2DProjectionQuery projectionQuery{
            .stableCameraKey = 1,
            .centerX = centerX,
            .centerY = centerY,
            .projection = Tina::Render::FixedWorldHeight2D{.heightMeters = ProductCameraHeightMeters},
            .pixelSnap = Tina::Render::RenderPixelSnapPolicy::CameraTranslation,
            .surfaceViewport =
                Tina::Render::Camera2DSurfaceViewport{
                    .pixelWidth = counters_->surfacePixelWidth,
                    .pixelHeight = counters_->surfacePixelHeight,
                },
        };
        auto camera = Tina::Render::makeResolvedCamera2DInput(projectionQuery);
        if (!camera)
        {
            return Tina::Core::failure(std::move(camera.error()));
        }
        ++counters_->cameraProjectionResolves;
        counters_->lastCameraWorldWidth = camera->worldWidth;
        counters_->lastCameraWorldHeight = camera->worldHeight;
        counters_->lastCameraActualPpm = camera->actualPixelsPerMeter;
        counters_->lastCameraCenterX = camera->centerX;
        counters_->lastCameraCenterY = camera->centerY;
        counters_->lastCameraInterpolation = alphaF;
        if (alphaF > 0.0f && alphaF < 1.0f)
        {
            ++counters_->cameraInterpolatedExtracts;
        }

        std::pmr::vector<Tina::Render::RenderSprite2DInput> tileSprites{&resources_->memory};
        const Tina::Asset::TileChunkCameraQuery query{
            .centerX = camera->centerX,
            .centerY = camera->centerY,
            .halfWidth = camera->worldWidth * 0.5f,
            .halfHeight = camera->worldHeight * 0.5f,
        };
        // Still emit all visible sprites for the visual product path. Dirty cache
        // runs in parallel as the CPU revision gate (M11-B1 evidence).
        auto emitted = Tina::Asset::emitVisibleTileMapSprites(
            resources_->tileMapStream->map(), VisualTileLayerId, query,
            Tina::Asset::TileChunkSpriteEmitParams{.spriteKey = tileSpriteKey_}, tileSprites);
        if (!emitted)
        {
            return Tina::Core::failure(std::move(emitted.error()));
        }
        counters_->lastTileSprites = *emitted;
        for (const auto& sprite : tileSprites)
        {
            if (auto status = writer.addSprite2D(sprite); !status)
            {
                return status;
            }
        }
        if (resources_->chunkDirtyCache)
        {
            const auto statsBefore = resources_->chunkDirtyCache->stats();
            resources_->chunkDirtyRebuilt.clear();
            auto rebuilds = resources_->chunkDirtyCache->syncVisible(
                resources_->tileMapStream->map(), VisualTileLayerId, query, resources_->chunkDirtyRebuilt);
            if (!rebuilds)
            {
                return Tina::Core::failure(std::move(rebuilds.error()));
            }
            const auto statsAfter = resources_->chunkDirtyCache->stats();
            counters_->lastChunkDirtyRebuilds = *rebuilds;
            counters_->lastChunkDirtyCacheHits = statsAfter.cacheHits - statsBefore.cacheHits;
            counters_->lastChunkDirtyVisible =
                statsAfter.visibleChunkObservations - statsBefore.visibleChunkObservations;
            counters_->chunkDirtyFramesSynced = statsAfter.framesSynced;
            counters_->chunkDirtyVisibleObservations = statsAfter.visibleChunkObservations;
            counters_->chunkDirtyRebuilds = statsAfter.rebuilds;
            counters_->chunkDirtyCacheHits = statsAfter.cacheHits;
        }

        // Scene contributed character [+ crate]; tiles from feature emit above.
        u64 totalSprites = *emitted + 1U;
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
        ++totalSprites;
#endif
        // M10-A44: when lastSelection is set, emit exactly one highlight sprite
        // (layer 2, above tiles/character/crate). Fail closed on build/capacity
        // errors —no silent half-state with selection but missing overlay.
        u64 highlightSprites = 0;
        if (counters_->tileSelection.lastSelection.has_value())
        {
            const Tina::Sample2D::SelectedTile& selection = *counters_->tileSelection.lastSelection;
            auto highlight = Tina::Sample2D::makeSelectionHighlightSprite(
                selection,
                Tina::Sample2D::TileSelectionGrid{
                    .widthCells = resources_->tileMapStream->map().widthCells(),
                    .heightCells = resources_->tileMapStream->map().heightCells(),
                    .cellSizeMeters = resources_->tileMapStream->map().cellSizeMeters(),
                },
                tileSpriteKey_);
            if (!highlight)
            {
                return Tina::Core::failure(std::move(highlight.error()));
            }
            if (auto status = writer.addSprite2D(*highlight); !status)
            {
                return status;
            }
            highlightSprites = 1;
            ++totalSprites;
        }
        counters_->lastHighlightSprites = highlightSprites;
        if (highlightSprites != 0)
        {
            ++counters_->selectionHighlightSprites;
        }

        auto particleExtract = resources_->particles->extract(writer);
        if (!particleExtract)
        {
            return Tina::Core::failure(std::move(particleExtract.error()));
        }
        if (const auto status = resources_->trail->extract(writer); !status)
        {
            return status;
        }
        counters_->particleExtracted = particleExtract->submitted;
        counters_->trailExtracted = resources_->trail->segmentCount();
        totalSprites += counters_->particleExtracted + counters_->trailExtracted;
        counters_->lastTotalSprites = totalSprites;
        ++counters_->renderExtractions;
        return Tina::Core::success();
    }

    Tina::Core::Status updateUI(Tina::UIUpdateContext& context) override
    {
        if (!context.hasPrimaryWindowUI() || !uiRoot_)
        {
            return Tina::Core::success();
        }
        // Publish accessibility snapshot from the last committed layout (startup/previous frame).
        // Real UIA/AT-SPI adapters would poll this same SPI; probe is product smoke only.
        auto semantics = context.committedSemantics();
        if (!semantics)
        {
            return Tina::Core::failure(std::move(semantics.error()));
        }
        if (auto status = accessibilityTree_.rebuildFrom(*semantics); !status)
        {
            return status;
        }
        if (auto status = accessibilityProbe_.publish(accessibilityTree_); !status)
        {
            return status;
        }
        ++counters_->accessibilityPublishCount;
        counters_->accessibilityNodeCount = accessibilityTree_.size();
        counters_->accessibilitySemanticsRevision = accessibilityTree_.semanticsRevision();
        counters_->accessibilityPublished = accessibilityProbe_.hasPublishedTree();
        counters_->accessibilityHasButton =
            accessibilityTree_.findByRole(Tina::UI::UISemanticsRole::Button) != nullptr;
        counters_->accessibilityHasCheckbox =
            accessibilityTree_.findByRole(Tina::UI::UISemanticsRole::Checkbox) != nullptr;
        counters_->accessibilityHasSlider =
            accessibilityTree_.findByRole(Tina::UI::UISemanticsRole::Slider) != nullptr;
        counters_->accessibilityHasProgressBar =
            accessibilityTree_.findByRole(Tina::UI::UISemanticsRole::ProgressBar) != nullptr;
        counters_->accessibilityHasRadio =
            accessibilityTree_.findByRole(Tina::UI::UISemanticsRole::RadioButton) != nullptr;
        counters_->accessibilityHasTextEdit =
            accessibilityTree_.findByRole(Tina::UI::UISemanticsRole::TextEdit) != nullptr;
        return Tina::Core::success();
    }

  private:
    struct SpriteBindingResolverContext final {
        const Tina::Asset::Sprite2DBindingRegistry* registry = nullptr;
        LifecycleCounters* counters = nullptr;
    };

    [[nodiscard]] static u32 resolveSceneSpriteBinding(
        void* userData,
        Tina::Asset::AssetHandle spriteHandle) noexcept
    {
        auto* resolverContext = static_cast<SpriteBindingResolverContext*>(userData);
        if (resolverContext == nullptr || resolverContext->registry == nullptr ||
            resolverContext->counters == nullptr)
        {
            return 0;
        }
        const u32 bindingKey = resolverContext->registry->resolveSprite(spriteHandle);
        if (bindingKey != 0)
        {
            ++resolverContext->counters->spriteBindingResolverHits;
        }
        return bindingKey;
    }

    void releaseTextureBinding(
        Tina::Asset::AssetHandle textureAsset,
        Tina::Render::GpuTextureId& gpuTexture,
        bool& bindingRegistered) noexcept
    {
        if (!gpuTexture || capture_ == nullptr)
        {
            return;
        }
        Tina::Render::IRenderDevice* device = capture_->get();
        if (device == nullptr)
        {
            return;
        }
        if (bindingRegistered)
        {
            if (!spriteBindings_)
            {
                return;
            }
            if (const auto status = spriteBindings_->unbindTextureBinding(textureAsset); !status)
            {
                return;
            }
            bindingRegistered = false;
            ++counters_->spriteBindingsReleased;
        }
        if (const auto status = device->destroyTexture2D(gpuTexture); status)
        {
            gpuTexture = {};
            ++counters_->spriteBindingTexturesDestroyed;
        }
    }

    void releaseSpriteBindings() noexcept
    {
        releaseTextureBinding(resources_->characterTextureHandle, characterGpuTexture_,
                              characterBindingRegistered_);
        releaseTextureBinding(resources_->tileTextureHandle, tileGpuTexture_, tileBindingRegistered_);
        if (spriteBindings_ && spriteBindings_->bindingCount() == 0)
        {
            spriteBindingResolverContext_.registry = nullptr;
            spriteBindings_.reset();
        }
        if (!characterGpuTexture_)
        {
            characterSpriteKey_ = 0;
        }
        if (!tileGpuTexture_)
        {
            tileSpriteKey_ = 0;
        }
    }

    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    TileMapResources* resources_ = nullptr;
    Tina::Sample2D::DeviceCapture* capture_ = nullptr;
    Tina::UI::UIRootOwner uiRoot_{};
    Tina::UI::UIAccessibilityTree accessibilityTree_{};
    Tina::UI::UIAccessibilityProbeProvider accessibilityProbe_{};
    mutable SpriteBindingResolverContext spriteBindingResolverContext_{};
    std::optional<Tina::Asset::Sprite2DBindingRegistry> spriteBindings_{};
    Tina::Render::GpuTextureId tileGpuTexture_{};
    Tina::Render::GpuTextureId characterGpuTexture_{};
    u32 tileSpriteKey_ = 0;
    u32 characterSpriteKey_ = 0;
    bool tileBindingRegistered_ = false;
    bool characterBindingRegistered_ = false;
    // Applied on next updateFrame when audioEngine is available (phase-local).
    float pendingMasterVolume_ = 1.0F;
    float pendingMusicVolume_ = 1.0F;
    float pendingSfxVolume_ = 1.0F;
    Tina::Sample2D::AudioMuteControlState masterMuteState_{};
    Tina::Sample2D::AudioMuteControlState musicMuteState_{};
    Tina::Sample2D::AudioMuteControlState sfxMuteState_{};
    Tina::Audio::AudioVoiceId audioOneShotVoice_{};
    Tina::Audio::AudioVoiceId audioStreamVoice_{};
    bool audioFadeStopMixed_ = false;
    bool audioStreamMixed_ = false;
    bool hasPendingMasterVolume_ = false;
    bool hasPendingMusicVolume_ = false;
    bool hasPendingSfxVolume_ = false;
#if defined(TINA_SAMPLE_TILEMAP_FREETYPE)
    std::chrono::steady_clock::time_point productUiVisibilityStartedAt_{};
#endif
};

class TileMapBgfxApplication final : public Tina::IGameApplication {
  public:
    TileMapBgfxApplication(SampleOptions options, LifecycleCounters& counters, TileMapResources& resources,
                           Tina::Sample2D::DeviceCapture& capture) noexcept
        : options_(options), counters_(&counters), resources_(&resources), capture_(&capture)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(Tina::GameStartupContext& context) override
    {
        // Seed surface from engine primary-window config; metrics events refine
        // framebuffer size after DPI/resize (M11-B0). UI-003 records logical/fb/scale.
        const auto& window = context.engineConfig().primaryWindow;
        counters_->surfacePixelWidth = window.initialLogicalExtent.width;
        counters_->surfacePixelHeight = window.initialLogicalExtent.height;
        counters_->logicalPixelWidth = window.initialLogicalExtent.width;
        counters_->logicalPixelHeight = window.initialLogicalExtent.height;
        counters_->framebufferPixelWidth = window.initialLogicalExtent.width;
        counters_->framebufferPixelHeight = window.initialLogicalExtent.height;
        counters_->contentScaleX = 1.0F;
        counters_->contentScaleY = 1.0F;
        auto subscription = context.platformEventSubscriptions().subscribe(
            [this](const Tina::PlatformEventNotification& notification) {
                if (!std::holds_alternative<Tina::Platform::WindowMetricsChangedEvent>(
                        notification.event().payload))
                {
                    return;
                }
                ++counters_->windowMetricsEvents;
                if (const auto* metrics = notification.primaryWindowMetrics(); metrics != nullptr)
                {
                    const bool hasFb =
                        metrics->framebufferExtent.width != 0 && metrics->framebufferExtent.height != 0;
                    counters_->logicalPixelWidth = metrics->logicalExtent.width;
                    counters_->logicalPixelHeight = metrics->logicalExtent.height;
                    counters_->framebufferPixelWidth = metrics->framebufferExtent.width;
                    counters_->framebufferPixelHeight = metrics->framebufferExtent.height;
                    counters_->contentScaleX = metrics->contentScale.x;
                    counters_->contentScaleY = metrics->contentScale.y;
                    counters_->surfacePixelWidth =
                        hasFb ? metrics->framebufferExtent.width : metrics->logicalExtent.width;
                    counters_->surfacePixelHeight =
                        hasFb ? metrics->framebufferExtent.height : metrics->logicalExtent.height;
                }
            });
        if (!subscription)
        {
            return Tina::Core::failure(std::move(subscription.error()));
        }
        platformEvents_.emplace(std::move(*subscription));
        return std::unique_ptr<Tina::IGameState>{
            std::make_unique<TileMapBgfxState>(options_, *counters_, *resources_, *capture_)};
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
        platformEvents_.reset();
        ++counters_->applicationShutdowns;
    }

  private:
    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    TileMapResources* resources_ = nullptr;
    Tina::Sample2D::DeviceCapture* capture_ = nullptr;
    std::optional<Tina::PlatformEventSubscription> platformEvents_{};
};

[[nodiscard]] Tina::EngineConfig createEngineConfig(const SampleOptions& options)
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina Sample 2D";
    config.primaryWindow.title = "Tina Sample 2D — TileMap + Character + UI";
    config.primaryWindow.initialLogicalExtent = {options.windowLogicalWidth, options.windowLogicalHeight};
    config.primaryWindow.initiallyVisible = true;
    config.renderSceneCapacities.spriteCapacity = 64;
    // A/D + arrows for interactive walk; automated smoke uses scripted walk after land.
    config.inputActions.bindings.push_back(Tina::InputActionBinding{
        .input = Tina::PrimaryWindowKeyBinding{.key = Tina::Platform::Key::A},
        .action = MoveLeftAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    config.inputActions.bindings.push_back(Tina::InputActionBinding{
        .input = Tina::PrimaryWindowKeyBinding{.key = Tina::Platform::Key::Left},
        .action = MoveLeftAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    config.inputActions.bindings.push_back(Tina::InputActionBinding{
        .input = Tina::StandardGamepadAxisBinding{
            .axis = Tina::Platform::GamepadAxis::LeftX,
            .valueMode = Tina::GamepadAxisValueMode::NegativeHalf,
        },
        .action = MoveLeftAction,
        .domain = Tina::InputActionDomain::Frame,
        .deadzone = 0.2F,
    });
    config.inputActions.bindings.push_back(Tina::InputActionBinding{
        .input = Tina::PrimaryWindowKeyBinding{.key = Tina::Platform::Key::D},
        .action = MoveRightAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    config.inputActions.bindings.push_back(Tina::InputActionBinding{
        .input = Tina::PrimaryWindowKeyBinding{.key = Tina::Platform::Key::Right},
        .action = MoveRightAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    config.inputActions.bindings.push_back(Tina::InputActionBinding{
        .input = Tina::StandardGamepadAxisBinding{
            .axis = Tina::Platform::GamepadAxis::LeftX,
            .valueMode = Tina::GamepadAxisValueMode::PositiveHalf,
        },
        .action = MoveRightAction,
        .domain = Tina::InputActionDomain::Frame,
        .deadzone = 0.2F,
    });
    config.inputActions.bindings.push_back(Tina::InputActionBinding{
        .input = Tina::PrimaryPointerButtonBinding{
            .pointer = Tina::Platform::PrimaryPointerId,
            .button = Tina::Platform::PointerButton::Primary,
        },
        .action = SelectTileAction,
        .domain = Tina::InputActionDomain::Simulation,
    });
    return config;
}

} // namespace

int main(int argc, char** argv)
{
    auto options = parseOptions(argc, argv);
    if (!options)
    {
        writeError(options.error());
        return 2;
    }

    LifecycleCounters counters{};
    counters.uiDisabledDemoButtonRequested = options->uiDisabledDemoButton;
    TileMapResources resources{};
    if (const auto status = prepareCatalog(resources, counters); !status)
    {
        writeError(status.error());
        return 1;
    }
    auto catalogCleanup = Tina::Core::makeScopeExit([&resources]() noexcept {
        std::error_code cleanupError;
        std::filesystem::remove_all(resources.catalogRoot, cleanupError);
    });

    Tina::Sample2D::DeviceCapture capture{};
    Tina::Desktop::CreateEngineOptions desktopOptions{};
    desktopOptions.wrapWindowSurfaceRenderDevice =
        [&capture](std::unique_ptr<Tina::Render::IRenderDevice> device)
            -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
            return Tina::Sample2D::wrapCapturingRenderDevice(std::move(device), capture);
        };
    auto host = Tina::Desktop::CreateEngine(createEngineConfig(*options), std::move(desktopOptions));
    if (!host)
    {
        writeError(host.error());
        return 1;
    }

    TileMapBgfxApplication application{*options, counters, resources, capture};
    auto run = (*host)->run(application);
    if (!run)
    {
        writeError(run.error());
        return 1;
    }

    // M11-D1: prefer capture taken on the final present; fall back to post-run capture.
    counters.pixelCaptureAttempted = true;
    if (capture.hasLastCapture() && capture.lastCapture() != nullptr && !capture.lastCapture()->empty())
    {
        const auto& captured = *capture.lastCapture();
        counters.pixelCaptureOk = true;
        counters.pixelCaptureWidth = captured.width;
        counters.pixelCaptureHeight = captured.height;
        counters.pixelCaptureBytes = static_cast<u64>(captured.byteCount());
        auto pixelHash = Tina::Core::digestContentHashV1(captured.rgba8Pixels);
        if (pixelHash.has_value() && pixelHash->hasValue())
        {
            counters.pixelFingerprint = contentHashToHex(*pixelHash);
        }
        else
        {
            counters.pixelCaptureOk = false;
        }
    }
    else if (Tina::Render::IRenderDevice* device = capture.get(); device != nullptr)
    {
        auto captured = device->capturePrimaryFrameRgba8();
        if (captured.has_value() && !captured->empty())
        {
            counters.pixelCaptureOk = true;
            counters.pixelCaptureWidth = captured->width;
            counters.pixelCaptureHeight = captured->height;
            counters.pixelCaptureBytes = static_cast<u64>(captured->byteCount());
            auto pixelHash = Tina::Core::digestContentHashV1(captured->rgba8Pixels);
            if (pixelHash.has_value() && pixelHash->hasValue())
            {
                counters.pixelFingerprint = contentHashToHex(*pixelHash);
            }
            else
            {
                counters.pixelCaptureOk = false;
            }
        }
    }

    (*host).reset();

    const Tina::Sample2D::SelectedTile* lastSelection =
        counters.tileSelection.lastSelection.has_value() ? &*counters.tileSelection.lastSelection : nullptr;
    const u64 classifiedPointerPresses = counters.tileSelection.missingWorldPointerSamples +
                                         counters.tileSelection.viewportMisses + counters.tileSelection.mapMisses +
                                         counters.tileSelection.selectionHits;
    const bool selectionCountersValid = counters.tileSelection.pointerPresses == classifiedPointerPresses;
    const bool selectionLatchValid =
        (counters.tileSelection.selectionHits == 0 && lastSelection == nullptr) ||
        (counters.tileSelection.selectionHits > 0 && lastSelection != nullptr && resources.tileMapStream.has_value() &&
         lastSelection->cellX < resources.tileMapStream->map().widthCells() &&
         lastSelection->cellY < resources.tileMapStream->map().heightCells());
    const bool selectionStateValid = selectionCountersValid && selectionLatchValid;
    const u64 expectedHighlightSprites = lastSelection != nullptr ? 1U : 0U;
    const u64 expectedTotalSprites = ExpectedSpritesWithPhysics + expectedHighlightSprites +
                                     counters.particleActive + counters.trailActive;
    // Seed path: selection from frame 0 →highlight every extract. Accidental OS
    // clicks during interactive/smoke only require last-frame highlight match.
    const bool highlightValid =
        counters.lastHighlightSprites == expectedHighlightSprites &&
        ((expectedHighlightSprites == 0 &&
          (options->seedTileSelection ? counters.selectionHighlightSprites == 0
                                      : counters.selectionHighlightSprites <= counters.renderExtractions)) ||
         (expectedHighlightSprites == 1 && counters.selectionHighlightSprites >= 1 &&
          (!options->seedTileSelection ||
           counters.selectionHighlightSprites == counters.renderExtractions)));
    const bool seededSelectionValid =
        !options->seedTileSelection ||
        (counters.seedTileSelectionApplied && counters.tileSelection.selectionHits >= 1 && lastSelection != nullptr &&
         lastSelection->cellX == options->seedTileCellX && lastSelection->cellY == options->seedTileCellY);

    const bool cameraProjectionValid =
        counters.cameraProjectionResolves == counters.renderExtractions && counters.lastCameraWorldHeight > 0.0f &&
        counters.lastCameraWorldWidth > 0.0f && counters.lastCameraActualPpm > 0.0f &&
        counters.surfacePixelWidth > 0 && counters.surfacePixelHeight > 0;
    // Follow the walking character: camera X must pan (clamp allows motion on 8m map).
    const bool cameraFollowValid =
        counters.cameraFollowUpdates > 0 && counters.cameraFollowPrimed &&
        counters.maxCameraCenterX > counters.minCameraCenterX + 0.25f &&
        counters.lastCameraCenterX > 0.0f && counters.lastCameraCenterY > 0.0f;
    // Static sample map: first extract rebuilds all visible chunks; subsequent
    // frames are pure cache hits (no setTile). rebuilds << visibleObservations.
    const bool chunkDirtyValid =
        counters.chunkDirtyFramesSynced == counters.renderExtractions && counters.chunkDirtyVisibleObservations > 0 &&
        counters.chunkDirtyRebuilds > 0 && counters.chunkDirtyCacheHits > 0 &&
        counters.chunkDirtyRebuilds < counters.chunkDirtyVisibleObservations &&
        counters.lastChunkDirtyRebuilds == 0 && counters.lastChunkDirtyCacheHits > 0;
    const bool tileMapStreamValid =
        resources.tileMapStream.has_value() &&
        counters.tileMapStreamDemandUpdates == counters.frameUpdates + 1U &&
        counters.tileMapStreamRequests == ExpectedTileMapStreamChunks &&
        counters.tileMapStreamCommitted == ExpectedTileMapStreamChunks &&
        counters.tileMapStreamResident == ExpectedTileMapStreamChunks &&
        counters.tileMapStreamPeakResident >= ExpectedTileMapStreamChunks;
    const bool product300EffectsValid =
        options->targetFrameCount != 300U ||
        (counters.particleExpired == ProductParticleExpiredAt300Frames &&
         counters.particleActive == ProductParticleActiveAt300Frames &&
         counters.trailActive == ProductTrailSegments);
    const bool effectsValid =
        resources.particles.has_value() && resources.trail.has_value() &&
        counters.particleEmitted == ProductParticleEmitted &&
        counters.particleExpired + counters.particleActive == counters.particleEmitted &&
        counters.particleActive == resources.particles->liveCount() &&
        counters.particleExtracted == counters.particleActive &&
        counters.trailSegmentsCreated == ProductTrailSegments && counters.trailBreaks == 1U &&
        counters.trailActive == resources.trail->segmentCount() &&
        counters.trailExtracted == counters.trailActive && counters.fxInitialFingerprint.size() == 32U &&
        product300EffectsValid;

    const bool audioValid =
        counters.audioEnginePresent && counters.audioOneShotQueued && counters.audioStartedObserved &&
        counters.audioStartedCount >= 1 && counters.audioFromCatalogLease && counters.audioClipFrameCount > 0 &&
        counters.audioClipSampleRate == 48000 && counters.audioVoiceParamsConfigured &&
        counters.audioVoiceGain == 0.8F && counters.audioPitch == 0.75F && counters.audioPan == -0.25F &&
        counters.audioFadeStarted && counters.audioFadeCancelled && counters.audioFadeStopped &&
        counters.audioStoppedCount >= 2 && counters.audioOneShotRetired && counters.audioStreamQueued &&
        counters.audioStreamSubmitted && counters.audioStreamEofSignaled &&
        counters.audioStreamStartedObserved && counters.audioStreamMixed && counters.audioStreamDrained &&
        counters.audioStreamUnderrunFrames == 0 && counters.audioStreamStopped &&
        counters.audioStreamRetired && counters.audioStreamSubmittedFrames == ExpectedAudioClipFrames &&
        counters.audioStreamConsumedFrames == ExpectedAudioClipFrames;
    const bool characterAnimationValid =
        counters.characterAnimationFromCatalog &&
        counters.characterAnimationResolvedFrames == ExpectedCharacterAnimationResolvedFrames &&
        counters.characterAnimationUpdates == counters.frameUpdates &&
        counters.characterAnimationFrameChanges > 0 &&
        counters.characterAnimationIdleEntries >= 1 &&
        counters.characterAnimationWalkEntries >= 1 &&
        counters.characterAnimationHitWallEntries >= 1 &&
        counters.characterAnimationHitCompleted;
    const bool spriteBindingsValid =
        counters.spriteBindingTextures == ExpectedUploadedTextures &&
        counters.spriteBindingsReleased == ExpectedUploadedTextures &&
        counters.spriteBindingTexturesDestroyed == ExpectedUploadedTextures &&
        counters.spriteBindingResolverHits > 0;
#if defined(TINA_SAMPLE_TILEMAP_AUDIO_MINIAUDIO)
    // M11-A16: null-backend device must start, run callbacks, and advance mixRealtime.
    const bool audioDeviceValid =
        counters.audioDeviceCreated && counters.audioDeviceNullBackend && counters.audioDeviceCallbacks > 0 &&
        counters.audioMixFramesRendered > 0;
#else
    const bool audioDeviceValid = true;
#endif

    bool ok = selectionStateValid && highlightValid && seededSelectionValid && cameraProjectionValid &&
              cameraFollowValid && chunkDirtyValid && tileMapStreamValid && effectsValid && audioValid && audioDeviceValid &&
              characterAnimationValid && spriteBindingsValid &&
              counters.catalogFromRecipeFile &&
              counters.catalogRecipeAssets == ExpectedCatalogRecipeAssets &&
              counters.objectLayerConsumed && counters.objectLayerObjectCount == 2U &&
              counters.texturesUploaded == ExpectedUploadedTextures &&
              counters.lastTileSprites == ExpectedNonEmptyTiles && counters.lastTotalSprites == expectedTotalSprites &&
              counters.controllerGroundedFrames > 0 && counters.controllerWalkFrames > 0 &&
              counters.controllerHitRightFrames > 0 && counters.maxControllerX > 1.5f &&
              counters.renderExtractions >= counters.frameUpdates &&
              counters.applicationShutdowns == 1 && counters.uiRootsCreated == 1 &&
              counters.uiPanelsCreated == ExpectedUIPanelCount &&
              counters.uiTextLabelsCreated == ExpectedUITextLabelCount &&
              counters.uiTextEditsCreated == ExpectedUITextEditCount &&
              counters.uiTextEditInitialTextVerified &&
              counters.uiButtonsCreated == ExpectedUIButtonCount && counters.uiButtonActionsWired == 1 &&
              counters.uiButtonPaintVerified &&
              counters.uiDisabledDemoButtonRequested == options->uiDisabledDemoButton &&
              counters.uiDisabledDemoButtonVerified &&
              counters.uiDemoButtonEnabled == !counters.uiDisabledDemoButtonRequested &&
              counters.uiSlidersCreated == 3 && counters.uiCheckboxesCreated == 3 &&
              counters.uiProgressBarsCreated == ExpectedUIProgressBarCount &&
              counters.uiProgressBarValueVerified &&
              counters.uiRadioButtonsCreated == ExpectedUIRadioButtonCount &&
              counters.uiRadioButtonActionsWired == ExpectedUIRadioButtonCount &&
              counters.uiRadioSelectionVerified &&
              counters.uiRootsReleased == 1 && counters.accessibilityPublished &&
              counters.accessibilityPublishCount >= 1 && counters.accessibilityNodeCount >= 6 &&
              counters.accessibilityHasButton && counters.accessibilityHasCheckbox &&
              counters.accessibilityHasSlider && counters.accessibilityHasProgressBar &&
              counters.accessibilityHasRadio && counters.accessibilityHasTextEdit &&
              *run == Tina::RunExitReason::GameRequestedExitAfterCurrentFrame;
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
    ok = ok && counters.physicsReady && counters.physicsStaticBodies == ExpectedPhysicsStaticBodies &&
         counters.physicsSteps == counters.frameUpdates && counters.physicsDynamicContacts > 0 &&
         counters.physicsSensorEnters > 0 && counters.physicsSensorExits > 0 &&
         counters.physicsJointReady &&
         counters.lastDynamicY < 3.5f && counters.lastDynamicY > 0.5f;
#endif
    // M11-D1: require a successful primary-frame RGBA8 capture when the device supports it.
    ok = ok && counters.pixelCaptureAttempted && counters.pixelCaptureOk && !counters.pixelFingerprint.empty() &&
         counters.pixelCaptureWidth > 0 && counters.pixelCaptureHeight > 0 && counters.pixelCaptureBytes > 0;
    // RUNTIME-001 pause overlay: long smokes (>=60 frames) must push/pop once; short smokes skip.
    // stateExits counts only TileMapBgfxState exits (still 1); overlay uses pauseOverlayPops.
    if (options->targetFrameCount >= 60)
    {
        ok = ok && counters.pauseOverlayPushes == 1 && counters.pauseOverlayPops == 1
             && counters.pauseOverlayFrames == 3;
    }
    ok = ok && counters.stateExits == 1;
    // M11-D2: optional golden pixel fingerprint comparison (exact match, machine-local).
    const bool pixelGoldenChecked = !options->expectPixelFingerprint.empty();
    const bool pixelGoldenMatched =
        !pixelGoldenChecked || counters.pixelFingerprint == options->expectPixelFingerprint;
    ok = ok && pixelGoldenMatched;

    // M11-D0 product evidence fingerprint: structural gates only (not frame count / animation).
    std::vector<std::byte> evidenceBytes;
    evidenceBytes.reserve(512);
    appendLeU32(evidenceBytes, 10U); // schema
    appendLeU32(evidenceBytes, counters.catalogFromRecipeFile ? 1U : 0U);
    appendLeU64(evidenceBytes, counters.catalogRecipeAssets);
    appendLeU64(evidenceBytes, counters.texturesUploaded);
    appendLeU64(evidenceBytes, counters.spriteBindingTextures);
    appendLeU64(evidenceBytes, counters.spriteBindingsReleased);
    appendLeU64(evidenceBytes, counters.spriteBindingTexturesDestroyed);
    appendLeU64(evidenceBytes, counters.spriteBindingResolverHits);
    appendLeU64(evidenceBytes, counters.tileMapStreamDemandUpdates);
    appendLeU64(evidenceBytes, counters.tileMapStreamRequests);
    appendLeU64(evidenceBytes, counters.tileMapStreamCommitted);
    appendLeU64(evidenceBytes, counters.tileMapStreamResident);
    appendLeU64(evidenceBytes, counters.tileMapStreamPeakResident);
    appendLeU32(evidenceBytes, ExpectedCharacterAnimationClips);
    appendLeU32(evidenceBytes, ExpectedCharacterAnimationSprites);
    appendLeU64(evidenceBytes, counters.characterAnimationResolvedFrames);
    appendLeU32(evidenceBytes, counters.characterAnimationFromCatalog ? 1U : 0U);
    appendLeU64(evidenceBytes, ExpectedNonEmptyTiles);
    appendLeU64(evidenceBytes, ExpectedSpritesWithPhysics);
    appendLeU32(evidenceBytes, options->seedTileSelection ? 1U : 0U);
    appendLeU64(evidenceBytes, ProductParticleCapacity);
    appendLeU64(evidenceBytes, ProductParticleEmitted);
    appendLeU64(evidenceBytes, ProductParticleRandomSeed);
    appendLeU64(evidenceBytes, ProductParticleStableKeyBase);
    appendLeU64(evidenceBytes, ProductTrailCapacity);
    appendLeU64(evidenceBytes, ProductTrailSegments);
    appendLeU64(evidenceBytes, ProductTrailStableKeyBase);
    for (const char byte : counters.fxInitialFingerprint)
    {
        evidenceBytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
    }
    appendLeU64(evidenceBytes, ExpectedUIPanelCount);
    appendLeU64(evidenceBytes, ExpectedUITextLabelCount);
    appendLeU64(evidenceBytes, ExpectedUITextEditCount);
    appendLeU32(evidenceBytes, counters.uiTextEditInitialTextVerified ? 1U : 0U);
    appendLeU64(evidenceBytes, ExpectedUIButtonCount);
    appendLeU32(evidenceBytes, counters.uiDisabledDemoButtonRequested ? 1U : 0U);
    appendLeU32(evidenceBytes, counters.uiDemoButtonEnabled ? 1U : 0U);
    appendLeU32(evidenceBytes, counters.uiDisabledDemoButtonVerified ? 1U : 0U);
    appendLeU64(evidenceBytes, counters.uiSlidersCreated);
    appendLeU64(evidenceBytes, counters.uiCheckboxesCreated); // expected 3 after M11-C5
    appendLeU64(evidenceBytes, counters.uiProgressBarsCreated);
    appendLeU32(evidenceBytes, counters.uiProgressBarValueVerified ? 1U : 0U);
    appendLeU64(evidenceBytes, counters.uiRadioButtonsCreated);
    appendLeU32(evidenceBytes, counters.uiRadioSelectionVerified ? 1U : 0U);
    appendLeU64(evidenceBytes, counters.audioClipFrameCount);
    appendLeU32(evidenceBytes, counters.audioClipSampleRate);
    appendLeU32(evidenceBytes, counters.audioEnginePresent ? 1U : 0U);
    appendLeU32(evidenceBytes, counters.audioFromCatalogLease ? 1U : 0U);
    appendLeU32(evidenceBytes, counters.audioVoiceParamsConfigured ? 1U : 0U);
    appendF32Bits(evidenceBytes, counters.audioVoiceGain);
    appendF32Bits(evidenceBytes, counters.audioPitch);
    appendF32Bits(evidenceBytes, counters.audioPan);
    appendLeU32(evidenceBytes, counters.audioFadeStarted ? 1U : 0U);
    appendLeU32(evidenceBytes, counters.audioFadeCancelled ? 1U : 0U);
    appendLeU32(evidenceBytes, counters.audioFadeStopped ? 1U : 0U);
    appendLeU32(evidenceBytes, counters.audioOneShotRetired ? 1U : 0U);
    appendLeU32(evidenceBytes, counters.audioStreamQueued ? 1U : 0U);
    appendLeU32(evidenceBytes, counters.audioStreamSubmitted ? 1U : 0U);
    appendLeU32(evidenceBytes, counters.audioStreamEofSignaled ? 1U : 0U);
    appendLeU32(evidenceBytes, counters.audioStreamStartedObserved ? 1U : 0U);
    appendLeU32(evidenceBytes, counters.audioStreamMixed ? 1U : 0U);
    appendLeU32(evidenceBytes, counters.audioStreamDrained ? 1U : 0U);
    appendLeU32(evidenceBytes, counters.audioStreamStopped ? 1U : 0U);
    appendLeU32(evidenceBytes, counters.audioStreamRetired ? 1U : 0U);
    appendLeU64(evidenceBytes, counters.audioStreamSubmittedFrames);
    appendLeU64(evidenceBytes, counters.audioStreamConsumedFrames);
    appendLeU64(evidenceBytes, counters.audioStreamUnderrunFrames);
    appendLeU32(evidenceBytes, counters.surfacePixelWidth);
    appendLeU32(evidenceBytes, counters.surfacePixelHeight);
    appendF32Bits(evidenceBytes, counters.lastCameraWorldWidth);
    appendF32Bits(evidenceBytes, counters.lastCameraWorldHeight);
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
    appendLeU64(evidenceBytes, counters.physicsStaticBodies);
    appendLeU64(evidenceBytes, counters.physicsSensorEnters);
    appendLeU64(evidenceBytes, counters.physicsSensorExits);
    appendLeU32(evidenceBytes, counters.physicsJointReady ? 1U : 0U);
    appendLeU32(evidenceBytes, 1U);
#else
    appendLeU64(evidenceBytes, 0U);
    appendLeU64(evidenceBytes, 0U);
    appendLeU64(evidenceBytes, 0U);
    appendLeU32(evidenceBytes, 0U);
    appendLeU32(evidenceBytes, 0U);
#endif
#if defined(TINA_SAMPLE_TILEMAP_FREETYPE)
    appendLeU32(evidenceBytes, 1U);
#else
    appendLeU32(evidenceBytes, 0U);
#endif
#if defined(TINA_SAMPLE_TILEMAP_AUDIO_MINIAUDIO)
    appendLeU32(evidenceBytes, 1U);
#else
    appendLeU32(evidenceBytes, 0U);
#endif
    auto evidenceHash = Tina::Core::digestContentHashV1(evidenceBytes);
    const bool evidenceValid = evidenceHash.has_value() && evidenceHash->hasValue();
    ok = ok && evidenceValid;
    const std::string evidenceFingerprint =
        evidenceValid ? contentHashToHex(*evidenceHash) : std::string{};

    if (!ok)
    {
        std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_2d\","
                     "\"message\":\"verification failed\","
                     "\"frames\":"
                  << counters.frameUpdates << ",\"tileSprites\":" << counters.lastTileSprites
                  << ",\"spriteBindingTextures\":" << counters.spriteBindingTextures
                  << ",\"spriteBindingsReleased\":" << counters.spriteBindingsReleased
                  << ",\"spriteBindingTexturesDestroyed\":" << counters.spriteBindingTexturesDestroyed
                  << ",\"spriteBindingResolverHits\":" << counters.spriteBindingResolverHits
                  << ",\"requestedFrameDelayMs\":" << options->frameDelayMilliseconds
                  << ",\"minimumWindowVisibilityMs\":" << minimumWindowVisibilityMilliseconds(*options)
                  << ",\"totalSprites\":" << counters.lastTotalSprites
                  << ",\"particleCapacity\":" << ProductParticleCapacity
                  << ",\"particleRandomSeed\":" << ProductParticleRandomSeed
                  << ",\"particleEmitted\":" << counters.particleEmitted
                  << ",\"particleExpired\":" << counters.particleExpired
                  << ",\"particleActive\":" << counters.particleActive
                  << ",\"particleExtracted\":" << counters.particleExtracted
                  << ",\"trailCapacity\":" << ProductTrailCapacity
                  << ",\"trailSegmentsCreated\":" << counters.trailSegmentsCreated
                  << ",\"trailActive\":" << counters.trailActive
                  << ",\"trailExtracted\":" << counters.trailExtracted
                  << ",\"trailBreaks\":" << counters.trailBreaks
                  << ",\"fxInitialFingerprint\":\"" << counters.fxInitialFingerprint << "\""
                  << ",\"grounded\":" << counters.controllerGroundedFrames
                  << ",\"walkFrames\":" << counters.controllerWalkFrames
                  << ",\"hitRight\":" << counters.controllerHitRightFrames
                  << ",\"animationFromCatalog\":"
                  << (counters.characterAnimationFromCatalog ? "true" : "false")
                  << ",\"animationResolvedFrames\":" << counters.characterAnimationResolvedFrames
                  << ",\"animationUpdates\":" << counters.characterAnimationUpdates
                  << ",\"animationFrameChanges\":" << counters.characterAnimationFrameChanges
                  << ",\"animationIdleEntries\":" << counters.characterAnimationIdleEntries
                  << ",\"animationWalkEntries\":" << counters.characterAnimationWalkEntries
                  << ",\"animationHitWallEntries\":" << counters.characterAnimationHitWallEntries
                  << ",\"animationHitCompleted\":"
                  << (counters.characterAnimationHitCompleted ? "true" : "false")
                  << ",\"maxX\":" << counters.maxControllerX << ",\"uiRoots\":" << counters.uiRootsCreated
                  << ",\"uiPanels\":" << counters.uiPanelsCreated << ",\"uiLabels\":" << counters.uiTextLabelsCreated
                  << ",\"uiTextEdits\":" << counters.uiTextEditsCreated
                  << ",\"uiTextEditInitialTextVerified\":"
                  << (counters.uiTextEditInitialTextVerified ? "true" : "false")
                  << ",\"uiButtons\":" << counters.uiButtonsCreated
                  << ",\"uiButtonPaintVerified\":"
                  << (counters.uiButtonPaintVerified ? "true" : "false")
                  << ",\"uiDisabledDemoButtonRequested\":"
                  << (counters.uiDisabledDemoButtonRequested ? "true" : "false")
                  << ",\"uiDemoButtonEnabled\":"
                  << (counters.uiDemoButtonEnabled ? "true" : "false")
                  << ",\"uiDisabledDemoButtonVerified\":"
                  << (counters.uiDisabledDemoButtonVerified ? "true" : "false")
                  << ",\"uiSliders\":" << counters.uiSlidersCreated
                  << ",\"uiProgressBars\":" << counters.uiProgressBarsCreated
                  << ",\"uiRadioButtons\":" << counters.uiRadioButtonsCreated
                  << ",\"uiReleased\":" << counters.uiRootsReleased
                  << ",\"lastMasterVolume\":" << counters.lastMasterVolume
                  << ",\"worldPointerPresses\":" << counters.tileSelection.pointerPresses
                  << ",\"worldPointerMissingSamples\":" << counters.tileSelection.missingWorldPointerSamples
                  << ",\"worldPointerViewportMisses\":" << counters.tileSelection.viewportMisses
                  << ",\"worldPointerMapMisses\":" << counters.tileSelection.mapMisses
                  << ",\"tileSelectionHits\":" << counters.tileSelection.selectionHits
                  << ",\"hasTileSelection\":" << (lastSelection != nullptr ? "true" : "false")
                  << ",\"selectionHighlightSprites\":" << counters.selectionHighlightSprites
                  << ",\"lastHighlightSprites\":" << counters.lastHighlightSprites
                  << ",\"seedTileSelection\":" << (options->seedTileSelection ? "true" : "false")
                  << ",\"cameraFollowUpdates\":" << counters.cameraFollowUpdates
                  << ",\"minCameraCenterX\":" << counters.minCameraCenterX
                  << ",\"maxCameraCenterX\":" << counters.maxCameraCenterX
                  << ",\"lastCameraCenterX\":" << counters.lastCameraCenterX
                  << ",\"lastCameraCenterY\":" << counters.lastCameraCenterY
                  << ",\"chunkDirtyFrames\":" << counters.chunkDirtyFramesSynced
                  << ",\"chunkDirtyRebuilds\":" << counters.chunkDirtyRebuilds
                  << ",\"chunkDirtyHits\":" << counters.chunkDirtyCacheHits
                  << ",\"lastChunkDirtyRebuilds\":" << counters.lastChunkDirtyRebuilds
                  << ",\"lastChunkDirtyHits\":" << counters.lastChunkDirtyCacheHits
                  << ",\"tileMapStreamDemandUpdates\":" << counters.tileMapStreamDemandUpdates
                  << ",\"tileMapStreamRequests\":" << counters.tileMapStreamRequests
                  << ",\"tileMapStreamCommitted\":" << counters.tileMapStreamCommitted
                  << ",\"tileMapStreamResident\":" << counters.tileMapStreamResident
                  << ",\"tileMapStreamPeakResident\":" << counters.tileMapStreamPeakResident
                  << ",\"cameraProjectionResolves\":" << counters.cameraProjectionResolves
                   << ",\"renderExtractions\":" << counters.renderExtractions
                   << ",\"pauseOverlayPushes\":" << counters.pauseOverlayPushes
                   << ",\"pauseOverlayPops\":" << counters.pauseOverlayPops
                   << ",\"pauseOverlayFrames\":" << counters.pauseOverlayFrames
                   << ",\"accessibilityPublished\":" << (counters.accessibilityPublished ? "true" : "false")
                   << ",\"accessibilityNodeCount\":" << counters.accessibilityNodeCount
                   << ",\"audioEnginePresent\":" << (counters.audioEnginePresent ? "true" : "false")
                   << ",\"audioOneShotQueued\":" << (counters.audioOneShotQueued ? "true" : "false")
                   << ",\"audioStartedObserved\":" << (counters.audioStartedObserved ? "true" : "false")
                  << ",\"audioStartedCount\":" << counters.audioStartedCount
                  << ",\"audioFromCatalogLease\":" << (counters.audioFromCatalogLease ? "true" : "false")
                  << ",\"audioClipFrameCount\":" << counters.audioClipFrameCount
                  << ",\"audioClipSampleRate\":" << counters.audioClipSampleRate
                  << ",\"audioVoiceParamsConfigured\":"
                  << (counters.audioVoiceParamsConfigured ? "true" : "false")
                  << ",\"audioFadeStarted\":" << (counters.audioFadeStarted ? "true" : "false")
                  << ",\"audioFadeCancelled\":" << (counters.audioFadeCancelled ? "true" : "false")
                  << ",\"audioFadeStopped\":" << (counters.audioFadeStopped ? "true" : "false")
                  << ",\"audioOneShotRetired\":" << (counters.audioOneShotRetired ? "true" : "false")
                  << ",\"audioStreamQueued\":" << (counters.audioStreamQueued ? "true" : "false")
                  << ",\"audioStreamSubmitted\":" << (counters.audioStreamSubmitted ? "true" : "false")
                  << ",\"audioStreamEofSignaled\":" << (counters.audioStreamEofSignaled ? "true" : "false")
                  << ",\"audioStreamStartedObserved\":"
                  << (counters.audioStreamStartedObserved ? "true" : "false")
                  << ",\"audioStreamMixed\":" << (counters.audioStreamMixed ? "true" : "false")
                  << ",\"audioStreamDrained\":" << (counters.audioStreamDrained ? "true" : "false")
                  << ",\"audioStreamStopped\":" << (counters.audioStreamStopped ? "true" : "false")
                  << ",\"audioStreamRetired\":" << (counters.audioStreamRetired ? "true" : "false")
                  << ",\"audioStreamSubmittedFrames\":" << counters.audioStreamSubmittedFrames
                  << ",\"audioStreamConsumedFrames\":" << counters.audioStreamConsumedFrames
                  << ",\"audioStreamUnderrunFrames\":" << counters.audioStreamUnderrunFrames
#if defined(TINA_SAMPLE_TILEMAP_AUDIO_MINIAUDIO)
                  << ",\"audioDeviceCreated\":" << (counters.audioDeviceCreated ? "true" : "false")
                  << ",\"audioDeviceNullBackend\":" << (counters.audioDeviceNullBackend ? "true" : "false")
                  << ",\"audioDeviceCallbacks\":" << counters.audioDeviceCallbacks
                  << ",\"audioMixFramesRendered\":" << counters.audioMixFramesRendered
#endif
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
                  << ",\"physicsSteps\":" << counters.physicsSteps
                  << ",\"physicsStatics\":" << counters.physicsStaticBodies
                  << ",\"physicsDynamicContacts\":" << counters.physicsDynamicContacts
                  << ",\"physicsSensorEnters\":" << counters.physicsSensorEnters
                  << ",\"physicsSensorExits\":" << counters.physicsSensorExits
                  << ",\"physicsJointReady\":" << (counters.physicsJointReady ? "true" : "false")
                  << ",\"dynamicY\":" << counters.lastDynamicY
#endif
                  << ",\"pixelCaptureAttempted\":" << (counters.pixelCaptureAttempted ? "true" : "false")
                  << ",\"pixelCaptureOk\":" << (counters.pixelCaptureOk ? "true" : "false")
                  << ",\"pixelCaptureWidth\":" << counters.pixelCaptureWidth
                  << ",\"pixelCaptureHeight\":" << counters.pixelCaptureHeight
                  << ",\"pixelCaptureBytes\":" << counters.pixelCaptureBytes
                  << ",\"pixelFingerprint\":\"" << counters.pixelFingerprint << "\""
                  << ",\"pixelGoldenChecked\":" << (pixelGoldenChecked ? "true" : "false")
                  << ",\"pixelGoldenMatched\":" << (pixelGoldenMatched ? "true" : "false")
                  << ",\"expectPixelFingerprint\":\"" << options->expectPixelFingerprint << "\""
                  << "}\n";
        return 1;
    }

    // Formal product sample name is tina_sample_2d; feature flags report which product
    // slices were compiled (Physics2D / FreeType). M10-A43/A44 consume A42 locked
    // world-pointer payload, draw selection highlight, and optionally seed selection
    // via --seed-tile-selection=cellX,cellY for automated product evidence.
    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_2d\""
              << ",\"frames\":" << counters.frameUpdates << ",\"renderExtractions\":" << counters.renderExtractions
              << ",\"requestedFrameDelayMs\":" << options->frameDelayMilliseconds
              << ",\"minimumWindowVisibilityMs\":" << minimumWindowVisibilityMilliseconds(*options)
              << ",\"catalogFromRecipeFile\":" << (counters.catalogFromRecipeFile ? "true" : "false")
              << ",\"catalogRecipeAssets\":" << counters.catalogRecipeAssets
              << ",\"objectLayerConsumed\":" << (counters.objectLayerConsumed ? "true" : "false")
              << ",\"objectLayerObjects\":" << counters.objectLayerObjectCount
              << ",\"tileMapStreamDemandUpdates\":" << counters.tileMapStreamDemandUpdates
              << ",\"tileMapStreamRequests\":" << counters.tileMapStreamRequests
              << ",\"tileMapStreamCommitted\":" << counters.tileMapStreamCommitted
              << ",\"tileMapStreamResident\":" << counters.tileMapStreamResident
              << ",\"tileMapStreamPeakResident\":" << counters.tileMapStreamPeakResident
              << ",\"texturesUploaded\":" << counters.texturesUploaded
              << ",\"spriteBindingTextures\":" << counters.spriteBindingTextures
              << ",\"spriteBindingsReleased\":" << counters.spriteBindingsReleased
              << ",\"spriteBindingTexturesDestroyed\":" << counters.spriteBindingTexturesDestroyed
              << ",\"spriteBindingResolverHits\":" << counters.spriteBindingResolverHits
              << ",\"tileSpritesPerFrame\":" << ExpectedNonEmptyTiles
              << ",\"spritesPerFrame\":" << expectedTotalSprites
              << ",\"particleCapacity\":" << ProductParticleCapacity
              << ",\"particleRandomSeed\":" << ProductParticleRandomSeed
              << ",\"particleEmitted\":" << counters.particleEmitted
              << ",\"particleExpired\":" << counters.particleExpired
              << ",\"particleActive\":" << counters.particleActive
              << ",\"particleExtracted\":" << counters.particleExtracted
              << ",\"trailCapacity\":" << ProductTrailCapacity
              << ",\"trailSegmentsCreated\":" << counters.trailSegmentsCreated
              << ",\"trailActive\":" << counters.trailActive
              << ",\"trailExtracted\":" << counters.trailExtracted
              << ",\"trailBreaks\":" << counters.trailBreaks
              << ",\"fxInitialFingerprint\":\"" << counters.fxInitialFingerprint << "\""
              << ",\"controllerGroundedFrames\":" << counters.controllerGroundedFrames
              << ",\"controllerWalkFrames\":" << counters.controllerWalkFrames
              << ",\"controllerHitRightFrames\":" << counters.controllerHitRightFrames
              << ",\"characterAnimationFromCatalog\":"
              << (counters.characterAnimationFromCatalog ? "true" : "false")
              << ",\"characterAnimationResolvedFrames\":" << counters.characterAnimationResolvedFrames
              << ",\"characterAnimationUpdates\":" << counters.characterAnimationUpdates
              << ",\"characterAnimationFrameChanges\":" << counters.characterAnimationFrameChanges
              << ",\"characterAnimationIdleEntries\":" << counters.characterAnimationIdleEntries
              << ",\"characterAnimationWalkEntries\":" << counters.characterAnimationWalkEntries
              << ",\"characterAnimationHitWallEntries\":" << counters.characterAnimationHitWallEntries
              << ",\"characterAnimationLastFrame\":" << counters.characterAnimationLastFrame
              << ",\"characterAnimationHitCompleted\":"
              << (counters.characterAnimationHitCompleted ? "true" : "false")
              << ",\"maxControllerX\":" << counters.maxControllerX
              << ",\"uiRootsCreated\":" << counters.uiRootsCreated
              << ",\"uiPanelsCreated\":" << counters.uiPanelsCreated
              << ",\"uiTextLabelsCreated\":" << counters.uiTextLabelsCreated
              << ",\"uiTextEditsCreated\":" << counters.uiTextEditsCreated
              << ",\"uiTextEditInitialTextVerified\":"
              << (counters.uiTextEditInitialTextVerified ? "true" : "false")
              << ",\"uiButtonsCreated\":" << counters.uiButtonsCreated
              << ",\"uiButtonActionsWired\":" << counters.uiButtonActionsWired
              << ",\"uiButtonPaintVerified\":"
              << (counters.uiButtonPaintVerified ? "true" : "false")
              << ",\"uiDisabledDemoButtonRequested\":"
              << (counters.uiDisabledDemoButtonRequested ? "true" : "false")
              << ",\"uiDemoButtonEnabled\":"
              << (counters.uiDemoButtonEnabled ? "true" : "false")
              << ",\"uiDisabledDemoButtonVerified\":"
              << (counters.uiDisabledDemoButtonVerified ? "true" : "false")
              << ",\"uiSlidersCreated\":" << counters.uiSlidersCreated
              << ",\"uiSliderChanges\":" << counters.uiSliderChanges
              << ",\"uiCheckboxesCreated\":" << counters.uiCheckboxesCreated
              << ",\"uiCheckboxActions\":" << counters.uiCheckboxActions
              << ",\"uiProgressBarsCreated\":" << counters.uiProgressBarsCreated
              << ",\"uiProgressBarValueVerified\":"
              << (counters.uiProgressBarValueVerified ? "true" : "false")
              << ",\"uiRadioButtonsCreated\":" << counters.uiRadioButtonsCreated
              << ",\"uiRadioButtonActionsWired\":" << counters.uiRadioButtonActionsWired
              << ",\"uiRadioSelectionVerified\":"
              << (counters.uiRadioSelectionVerified ? "true" : "false")
              << ",\"lastMasterVolume\":" << counters.lastMasterVolume
              << ",\"lastMusicVolume\":" << counters.lastMusicVolume
              << ",\"lastSfxVolume\":" << counters.lastSfxVolume
              << ",\"lastMasterMuted\":" << (counters.lastMasterMuted ? "true" : "false")
              << ",\"lastMusicMuted\":" << (counters.lastMusicMuted ? "true" : "false")
              << ",\"lastSfxMuted\":" << (counters.lastSfxMuted ? "true" : "false")
              << ",\"masterVolumeFromSlider\":" << (counters.masterVolumeFromSlider ? "true" : "false")
              << ",\"musicVolumeFromSlider\":" << (counters.musicVolumeFromSlider ? "true" : "false")
              << ",\"sfxVolumeFromSlider\":" << (counters.sfxVolumeFromSlider ? "true" : "false")
              << ",\"masterMutedFromCheckbox\":" << (counters.masterMutedFromCheckbox ? "true" : "false")
              << ",\"musicMutedFromCheckbox\":" << (counters.musicMutedFromCheckbox ? "true" : "false")
              << ",\"sfxMutedFromCheckbox\":" << (counters.sfxMutedFromCheckbox ? "true" : "false")
              << ",\"uiRootsReleased\":" << counters.uiRootsReleased
              << ",\"worldPointerPresses\":" << counters.tileSelection.pointerPresses
              << ",\"worldPointerMissingSamples\":" << counters.tileSelection.missingWorldPointerSamples
              << ",\"worldPointerViewportMisses\":" << counters.tileSelection.viewportMisses
              << ",\"worldPointerMapMisses\":" << counters.tileSelection.mapMisses
              << ",\"tileSelectionHits\":" << counters.tileSelection.selectionHits
              << ",\"hasTileSelection\":" << (lastSelection != nullptr ? "true" : "false")
              << ",\"lastSelectedCellX\":" << (lastSelection != nullptr ? lastSelection->cellX : 0U)
              << ",\"lastSelectedCellY\":" << (lastSelection != nullptr ? lastSelection->cellY : 0U)
              << ",\"lastSelectedTileId\":" << counters.lastSelectedTileId
              << ",\"lastSelectionWorldX\":"
              << (lastSelection != nullptr ? lastSelection->worldPointer.worldX : 0.0F)
              << ",\"lastSelectionWorldY\":"
              << (lastSelection != nullptr ? lastSelection->worldPointer.worldY : 0.0F)
              << ",\"lastSelectionInputSequence\":"
              << (lastSelection != nullptr ? lastSelection->worldPointer.inputSequence : 0U)
              << ",\"lastSelectionCameraRevision\":"
              << (lastSelection != nullptr ? lastSelection->worldPointer.cameraRevision : 0U)
              << ",\"lastSelectionSurfaceRevision\":"
              << (lastSelection != nullptr ? lastSelection->worldPointer.surfaceRevision : 0U)
              << ",\"selectionHighlightSprites\":" << counters.selectionHighlightSprites
              << ",\"lastHighlightSprites\":" << counters.lastHighlightSprites
              << ",\"seedTileSelection\":" << (options->seedTileSelection ? "true" : "false")
              << ",\"seedTileSelectionApplied\":" << (counters.seedTileSelectionApplied ? "true" : "false")
              << ",\"windowLogicalWidth\":" << options->windowLogicalWidth
              << ",\"windowLogicalHeight\":" << options->windowLogicalHeight
              << ",\"logicalPixelWidth\":" << counters.logicalPixelWidth
              << ",\"logicalPixelHeight\":" << counters.logicalPixelHeight
              << ",\"framebufferPixelWidth\":" << counters.framebufferPixelWidth
              << ",\"framebufferPixelHeight\":" << counters.framebufferPixelHeight
              << ",\"contentScaleX\":" << counters.contentScaleX
              << ",\"contentScaleY\":" << counters.contentScaleY
              << ",\"surfacePixelWidth\":" << counters.surfacePixelWidth
              << ",\"surfacePixelHeight\":" << counters.surfacePixelHeight
              << ",\"windowMetricsEvents\":" << counters.windowMetricsEvents
              << ",\"cameraProjectionResolves\":" << counters.cameraProjectionResolves
              << ",\"lastCameraWorldWidth\":" << counters.lastCameraWorldWidth
              << ",\"lastCameraWorldHeight\":" << counters.lastCameraWorldHeight
              << ",\"lastCameraActualPpm\":" << counters.lastCameraActualPpm
              << ",\"cameraProjection\":\"FixedWorldHeight2D\""
              << ",\"cameraFollowUpdates\":" << counters.cameraFollowUpdates
              << ",\"cameraInterpolatedExtracts\":" << counters.cameraInterpolatedExtracts
              << ",\"lastCameraCenterX\":" << counters.lastCameraCenterX
              << ",\"lastCameraCenterY\":" << counters.lastCameraCenterY
              << ",\"lastCameraInterpolation\":" << counters.lastCameraInterpolation
              << ",\"minCameraCenterX\":" << counters.minCameraCenterX
              << ",\"maxCameraCenterX\":" << counters.maxCameraCenterX
              << ",\"chunkDirtyFramesSynced\":" << counters.chunkDirtyFramesSynced
              << ",\"chunkDirtyVisibleObservations\":" << counters.chunkDirtyVisibleObservations
              << ",\"chunkDirtyRebuilds\":" << counters.chunkDirtyRebuilds
              << ",\"chunkDirtyCacheHits\":" << counters.chunkDirtyCacheHits
              << ",\"lastChunkDirtyRebuilds\":" << counters.lastChunkDirtyRebuilds
              << ",\"lastChunkDirtyCacheHits\":" << counters.lastChunkDirtyCacheHits
              << ",\"lastChunkDirtyVisible\":" << counters.lastChunkDirtyVisible
              << ",\"audioEnginePresent\":" << (counters.audioEnginePresent ? "true" : "false")
              << ",\"audioOneShotQueued\":" << (counters.audioOneShotQueued ? "true" : "false")
              << ",\"audioStartedObserved\":" << (counters.audioStartedObserved ? "true" : "false")
              << ",\"audioStartedCount\":" << counters.audioStartedCount
              << ",\"audioStoppedCount\":" << counters.audioStoppedCount
              << ",\"audioFromCatalogLease\":" << (counters.audioFromCatalogLease ? "true" : "false")
              << ",\"audioClipFrameCount\":" << counters.audioClipFrameCount
              << ",\"audioClipSampleRate\":" << counters.audioClipSampleRate
              << ",\"audioVoiceParamsConfigured\":"
              << (counters.audioVoiceParamsConfigured ? "true" : "false")
              << ",\"audioVoiceGain\":" << counters.audioVoiceGain
              << ",\"audioPitch\":" << counters.audioPitch
              << ",\"audioPan\":" << counters.audioPan
              << ",\"audioFadeStarted\":" << (counters.audioFadeStarted ? "true" : "false")
              << ",\"audioFadeCancelled\":" << (counters.audioFadeCancelled ? "true" : "false")
              << ",\"audioFadeStopped\":" << (counters.audioFadeStopped ? "true" : "false")
              << ",\"audioOneShotRetired\":" << (counters.audioOneShotRetired ? "true" : "false")
              << ",\"audioStreamQueued\":" << (counters.audioStreamQueued ? "true" : "false")
              << ",\"audioStreamSubmitted\":" << (counters.audioStreamSubmitted ? "true" : "false")
              << ",\"audioStreamEofSignaled\":" << (counters.audioStreamEofSignaled ? "true" : "false")
              << ",\"audioStreamStartedObserved\":"
              << (counters.audioStreamStartedObserved ? "true" : "false")
              << ",\"audioStreamMixed\":" << (counters.audioStreamMixed ? "true" : "false")
              << ",\"audioStreamDrained\":" << (counters.audioStreamDrained ? "true" : "false")
              << ",\"audioStreamStopped\":" << (counters.audioStreamStopped ? "true" : "false")
              << ",\"audioStreamRetired\":" << (counters.audioStreamRetired ? "true" : "false")
              << ",\"audioStreamSubmittedFrames\":" << counters.audioStreamSubmittedFrames
              << ",\"audioStreamConsumedFrames\":" << counters.audioStreamConsumedFrames
              << ",\"audioStreamUnderrunFrames\":" << counters.audioStreamUnderrunFrames
#if defined(TINA_SAMPLE_TILEMAP_AUDIO_MINIAUDIO)
              << ",\"audioMiniaudioEnabled\":true"
              << ",\"audioDeviceCreated\":" << (counters.audioDeviceCreated ? "true" : "false")
              << ",\"audioDeviceNullBackend\":" << (counters.audioDeviceNullBackend ? "true" : "false")
              << ",\"audioDeviceCallbacks\":" << counters.audioDeviceCallbacks
              << ",\"audioMixFramesRendered\":" << counters.audioMixFramesRendered
#else
              << ",\"audioMiniaudioEnabled\":false"
#endif
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
              << ",\"physicsEnabled\":true"
              << ",\"physicsSteps\":" << counters.physicsSteps
              << ",\"physicsStaticBodies\":" << counters.physicsStaticBodies
              << ",\"physicsDynamicContacts\":" << counters.physicsDynamicContacts
              << ",\"physicsSensorEnters\":" << counters.physicsSensorEnters
              << ",\"physicsSensorExits\":" << counters.physicsSensorExits
              << ",\"physicsJointReady\":" << (counters.physicsJointReady ? "true" : "false")
              << ",\"lastDynamicY\":" << counters.lastDynamicY
#else
              << ",\"physicsEnabled\":false"
#endif
#if defined(TINA_SAMPLE_TILEMAP_FREETYPE)
              << ",\"freetypeEnabled\":true"
#else
              << ",\"freetypeEnabled\":false"
#endif
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D) && defined(TINA_SAMPLE_TILEMAP_FREETYPE) && \
    defined(TINA_SAMPLE_TILEMAP_AUDIO_MINIAUDIO)
              << ",\"productGate\":\"bgfx-physics-freetype-audio\""
#elif defined(TINA_SAMPLE_TILEMAP_PHYSICS2D) && defined(TINA_SAMPLE_TILEMAP_FREETYPE)
              << ",\"productGate\":\"bgfx-physics-freetype\""
#elif defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
              << ",\"productGate\":\"bgfx-physics\""
#elif defined(TINA_SAMPLE_TILEMAP_FREETYPE)
              << ",\"productGate\":\"bgfx-freetype\""
#else
              << ",\"productGate\":\"bgfx\""
#endif
              << ",\"stateExits\":" << counters.stateExits
              << ",\"pauseOverlayPushes\":" << counters.pauseOverlayPushes
              << ",\"pauseOverlayPops\":" << counters.pauseOverlayPops
              << ",\"pauseOverlayFrames\":" << counters.pauseOverlayFrames
              << ",\"accessibilityPublished\":" << (counters.accessibilityPublished ? "true" : "false")
              << ",\"accessibilityPublishCount\":" << counters.accessibilityPublishCount
              << ",\"accessibilityNodeCount\":" << counters.accessibilityNodeCount
              << ",\"accessibilitySemanticsRevision\":" << counters.accessibilitySemanticsRevision
              << ",\"accessibilityHasButton\":" << (counters.accessibilityHasButton ? "true" : "false")
              << ",\"accessibilityHasCheckbox\":" << (counters.accessibilityHasCheckbox ? "true" : "false")
              << ",\"accessibilityHasSlider\":" << (counters.accessibilityHasSlider ? "true" : "false")
              << ",\"accessibilityHasProgressBar\":" << (counters.accessibilityHasProgressBar ? "true" : "false")
              << ",\"accessibilityHasRadio\":" << (counters.accessibilityHasRadio ? "true" : "false")
              << ",\"accessibilityHasTextEdit\":" << (counters.accessibilityHasTextEdit ? "true" : "false")
              << ",\"applicationShutdowns\":" << counters.applicationShutdowns
              << ",\"evidenceSchema\":10"
              << ",\"evidenceFingerprint\":\"" << evidenceFingerprint << "\""
              << ",\"pixelCaptureAttempted\":" << (counters.pixelCaptureAttempted ? "true" : "false")
              << ",\"pixelCaptureOk\":" << (counters.pixelCaptureOk ? "true" : "false")
              << ",\"pixelCaptureWidth\":" << counters.pixelCaptureWidth
              << ",\"pixelCaptureHeight\":" << counters.pixelCaptureHeight
              << ",\"pixelCaptureBytes\":" << counters.pixelCaptureBytes
              << ",\"pixelFingerprint\":\"" << counters.pixelFingerprint << "\""
              << ",\"pixelGoldenChecked\":" << (pixelGoldenChecked ? "true" : "false")
              << ",\"pixelGoldenMatched\":" << (pixelGoldenMatched ? "true" : "false")
              << ",\"expectPixelFingerprint\":\"" << options->expectPixelFingerprint << "\""
              << ",\"exit\":\""
              << "GameRequestedExitAfterCurrentFrame\"}\n";
    return 0;
}
