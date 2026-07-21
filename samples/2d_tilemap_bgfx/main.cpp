#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetGpuTexture.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CharacterController2D.hpp>
#include <tina/asset/GridCollision.hpp>
#include <tina/asset/TileChunkDirtyCache.hpp>
#include <tina/asset/TileChunkRender.hpp>
#include <tina/asset/TileMapInstance.hpp>
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
#include <tina/asset/TileMapPhysicsSync.hpp>
#include <tina/physics2d/PhysicsWorld2D.hpp>
#endif
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/glfw/GlfwPlatformFactory.hpp>
#include <tina/render/Camera2DProjection.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/platform/Input.hpp>
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
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#if defined(TINA_SAMPLE_TILEMAP_AUDIO_MINIAUDIO)
#include <tina/audio/miniaudio/MiniaudioDevice.hpp>
#endif
#include <tina/task/bounded/BoundedTaskSystemFactory.hpp>
#include <tina/ui/UIButton.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UIText.hpp>
#if defined(TINA_SAMPLE_TILEMAP_FREETYPE)
#include <tina/ui/UIContext.hpp>
#include <tina/ui/text/FreeTypeTextRasterizerFactory.hpp>
#endif

#include "render/bgfx/BgfxRenderDevice.hpp"
#include "TileSelection.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <memory_resource>
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
// bgfx product path currently binds atlas textures per spriteKey; fixture samples
// historically only accepted key 1. Tile + character share the cooked atlas key.
inline constexpr u32 ProductSpriteKey = 1;
inline constexpr u32 ExpectedNonEmptyTiles = 11; // 8 floor + 3 wall
inline constexpr Tina::InputActionId MoveLeftAction{1};
inline constexpr Tina::InputActionId MoveRightAction{2};
inline constexpr Tina::InputActionId SelectTileAction{3};
inline constexpr float DemoWalkSpeedMetersPerSecond = 4.0f;

struct SampleOptions final {
    u64 targetFrameCount = DefaultFrameCount;
    u32 frameDelayMilliseconds = DefaultFrameDelayMilliseconds;
    // Optional controlled product gate: seed one map cell selection after enter
    // without synthesizing OS pointer events (GLFW smoke stays hermetic).
    bool seedTileSelection = false;
    u32 seedTileCellX = 0;
    u32 seedTileCellY = 0;
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 renderExtractions = 0;
    u64 stateEnters = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
    u64 texturesUploaded = 0;
    u64 lastTileSprites = 0;
    u64 lastTotalSprites = 0;
    u64 controllerGroundedFrames = 0;
    u64 controllerWalkFrames = 0;
    u64 controllerHitRightFrames = 0;
    float maxControllerX = 0.0f;
    u64 uiRootsCreated = 0;
    u64 uiPanelsCreated = 0;
    u64 uiRootsReleased = 0;
    u64 uiTextLabelsCreated = 0;
    u64 uiButtonsCreated = 0;
    u64 uiButtonActionsWired = 0;
    Tina::Sample2D::TileSelectionCounters tileSelection{};
    u16 lastSelectedTileId = 0;
    u64 selectionHighlightSprites = 0;
    u64 lastHighlightSprites = 0;
    u64 catalogRecipeAssets = 0;
    bool catalogFromRecipeFile = false;
    bool seedTileSelectionApplied = false;
    // M11-B0: surface-driven Camera2D projection (FixedWorldHeight).
    u32 surfacePixelWidth = 960;
    u32 surfacePixelHeight = 540;
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
    bool audioFromCatalogLease = false;
    u64 audioStartedCount = 0;
    u64 audioClipFrameCount = 0;
    u32 audioClipSampleRate = 0;
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
    float lastDynamicY = 0.0f;
    bool physicsReady = false;
#endif
};

inline constexpr u32 ExpectedUIPanelCount = 2;
inline constexpr u32 ExpectedUITextLabelCount = 2;
inline constexpr u32 ExpectedUIButtonCount = 1;
// Authored FixedWorldHeight for product sample; world width/ppm come from surface.
// Keep height small enough that half-width fits the 8m-wide sample map so follow
// can pan (height 6m → halfW≈5.3m > map half → clamp freezes at center).
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
        .solidFill =
            Tina::UI::UISolidFill{
                .color =
                    {
                        .red = red,
                        .green = green,
                        .blue = blue,
                        .alpha = alpha,
                    },
            },
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
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument, "unsupported argument");
    }
    return options;
}

class DeviceCapture final {
  public:
    void set(Tina::Render::IRenderDevice* device) noexcept { device_ = device; }
    [[nodiscard]] Tina::Render::IRenderDevice* get() const noexcept { return device_; }

  private:
    Tina::Render::IRenderDevice* device_ = nullptr;
};

class CapturingRenderDevice final : public Tina::Render::IRenderDevice {
  public:
    CapturingRenderDevice(std::unique_ptr<Tina::Render::IRenderDevice> inner, DeviceCapture& capture) noexcept
        : inner_(std::move(inner)), capture_(&capture)
    {
        capture_->set(this);
    }

    ~CapturingRenderDevice() override
    {
        if (capture_ != nullptr && capture_->get() == this)
        {
            capture_->set(nullptr);
        }
    }

    [[nodiscard]] Tina::Core::Result<Tina::Render::RenderFrameSubmission>
    submitFrame(const Tina::Render::RenderFrame& frame) override
    {
        return inner_->submitFrame(frame);
    }
    [[nodiscard]] Tina::Core::Status present() override { return inner_->present(); }
    [[nodiscard]] Tina::Render::RenderStatistics statistics() const noexcept override
    {
        return inner_->statistics();
    }
    void shutdown() noexcept override { inner_->shutdown(); }
    [[nodiscard]] Tina::Core::Result<Tina::Render::GpuTextureId>
    createTexture2DRgba8(const Tina::Render::Texture2DUploadDesc& desc) override
    {
        return inner_->createTexture2DRgba8(desc);
    }
    [[nodiscard]] Tina::Core::Status destroyTexture2D(Tina::Render::GpuTextureId texture) noexcept override
    {
        return inner_->destroyTexture2D(texture);
    }
    [[nodiscard]] Tina::Core::Status setSprite2DTextureBinding(u32 spriteKey,
                                                               Tina::Render::GpuTextureId texture) noexcept override
    {
        return inner_->setSprite2DTextureBinding(spriteKey, texture);
    }

  private:
    std::unique_ptr<Tina::Render::IRenderDevice> inner_;
    DeviceCapture* capture_ = nullptr;
};

struct TileMapResources final {
    std::pmr::unsynchronized_pool_resource memory{};
    std::unique_ptr<Tina::Asset::AssetSystem> system{};
    Tina::Asset::AssetHandle textureHandle{};
    Tina::Asset::AssetHandle tilesetHandle{};
    Tina::Asset::AssetHandle tileMapHandle{};
    Tina::Asset::AssetHandle audioClipHandle{};
    // Keeps cooked AudioClip CPU payload alive across playOneShot/mix (M11-A19).
    Tina::Asset::AssetLease audioClipLease{};
    Tina::Render::GpuTextureId gpuTexture{};
    std::optional<Tina::Asset::TileMapInstance> map{};
    std::optional<Tina::Asset::TileMapGridCollision> grid{};
    std::optional<Tina::Asset::CharacterController2D> controller{};
    std::optional<Tina::Asset::TileChunkDirtyCache> chunkDirtyCache{};
    std::pmr::vector<Tina::Asset::TileChunkView> chunkDirtyRebuilt{&memory};
    std::pmr::vector<Tina::Asset::TileMapSolidHit> solidScratch{&memory};
    // Presentation camera: previous/current sim targets; extract lerps with FrameTiming.interpolation.
    float cameraPreviousX = 4.0f;
    float cameraPreviousY = 2.0f;
    float cameraCurrentX = 4.0f;
    float cameraCurrentY = 2.0f;
    std::filesystem::path catalogRoot{};
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
    std::optional<Tina::Physics2D::PhysicsWorld2D> physicsWorld{};
    Tina::Physics2D::PhysicsBodyId dynamicBody{};
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

[[nodiscard]] Tina::Core::Status prepareCatalog(TileMapResources& resources, LifecycleCounters& counters)
{
    // Stable product ids must match samples/2d_tilemap_bgfx/catalog/sample_2d.recipe.
    const auto textureId = *Tina::Core::AssetId::fromBytes(idBytes(1U));
    const auto tilesetId = *Tina::Core::AssetId::fromBytes(idBytes(2U));
    const auto tileMapId = *Tina::Core::AssetId::fromBytes(idBytes(3U));
    const auto audioClipId = *Tina::Core::AssetId::fromBytes(idBytes(4U));

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
    if (request->assets.size() != 4U)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "sample_2d.recipe must declare Texture2D+Tileset+TileMap+AudioClip");
    }
    counters.catalogRecipeAssets = request->assets.size();
    counters.catalogFromRecipeFile = true;

    resources.catalogRoot = std::filesystem::temp_directory_path() / "tina_sample_2d_pkg";
    std::error_code ec;
    std::filesystem::remove_all(resources.catalogRoot, ec);
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
    auto loaded = system->load(std::array{tileMapId, audioClipId});
    if (!loaded)
    {
        return Tina::Core::failure(std::move(loaded.error()));
    }
    if (loaded->size() < 2U)
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidHandle, "tilemap/audioclip load incomplete");
    }
    resources.tileMapHandle = (*loaded)[0];
    resources.audioClipHandle = (*loaded)[1];

    auto tilesetHandle = system->find(tilesetId);
    auto textureHandle = system->find(textureId);
    if (!tilesetHandle || !textureHandle)
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidHandle, "tileset/texture not loaded");
    }
    resources.tilesetHandle = *tilesetHandle;
    resources.textureHandle = *textureHandle;

    // Resolve AudioClip by id (load() return order is plan order, not request order).
    auto audioHandle = system->find(audioClipId);
    if (!audioHandle)
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::InvalidHandle, "audioclip not loaded");
    }
    resources.audioClipHandle = *audioHandle;

    const auto* tileMapFile = system->tryGet(resources.tileMapHandle);
    const auto* tilesetFile = system->tryGet(resources.tilesetHandle);
    if (tileMapFile == nullptr || tilesetFile == nullptr)
    {
        return Tina::Core::failure(Tina::Asset::AssetErrorCode::AssetNotReady, "tilemap/tileset CPU missing");
    }
    auto tileMapView = Tina::Asset::parseTileMapFromCooked(*tileMapFile);
    auto tilesetView = Tina::Asset::parseTilesetFromCooked(*tilesetFile);
    if (!tileMapView || !tilesetView)
    {
        return Tina::Core::failure(tileMapView ? tilesetView.error() : tileMapView.error());
    }
    auto map = Tina::Asset::TileMapInstance::Create(
        *tileMapView, *tilesetView, tileMapId, tilesetId,
        Tina::Asset::TileMapInstanceConfig{.chunkSizeCells = 4, .memoryResource = &resources.memory});
    if (!map)
    {
        return Tina::Core::failure(std::move(map.error()));
    }
    resources.map.emplace(std::move(*map));
    resources.grid.emplace(*resources.map);
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
    resources.controller->teleport(1.0f, 3.0f, true);

#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
    Tina::Physics2D::PhysicsWorld2DConfig worldConfig;
    worldConfig.bodyCapacity = 64;
    worldConfig.shapeCapacity = 64;
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
    dynamicDesc.positionMeters = {3.0F, 3.5F};
    Tina::Physics2D::PhysicsBoxShape2DDesc box;
    box.halfExtentsMeters = {resources.dynamicHalfExtent, resources.dynamicHalfExtent};
    box.density = 1.0F;
    box.enableContactEvents = true;
    auto dynamic = resources.physicsWorld->createBoxBody(dynamicDesc, box);
    if (!dynamic)
    {
        return Tina::Core::failure(std::move(dynamic.error()));
    }
    resources.dynamicBody = dynamic->body;
    resources.lastDynamicX = dynamicDesc.positionMeters.x;
    resources.lastDynamicY = dynamicDesc.positionMeters.y;
#endif

    // AssetLease holds AssetStore*; acquire only after the system owns its final address.
    resources.system = std::make_unique<Tina::Asset::AssetSystem>(std::move(*system));
    auto audioLease = resources.system->acquire(resources.audioClipHandle);
    if (!audioLease)
    {
        return Tina::Core::failure(std::move(audioLease.error()));
    }
    resources.audioClipLease = std::move(*audioLease);
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
    return Tina::Core::success();
}

class TileMapBgfxState final : public Tina::IGameState {
  public:
    TileMapBgfxState(SampleOptions options, LifecycleCounters& counters, TileMapResources& resources,
                     DeviceCapture& capture) noexcept
        : options_(options), counters_(&counters), resources_(&resources), capture_(&capture)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        ++counters_->stateEnters;
        auto* device = capture_->get();
        if (device == nullptr || resources_->system == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "render device or catalog missing");
        }
        const auto* textureFile = resources_->system->tryGet(resources_->textureHandle);
        if (textureFile == nullptr)
        {
            return Tina::Core::failure(Tina::Asset::AssetErrorCode::AssetNotReady, "texture CPU payload missing");
        }
        auto texture = Tina::Asset::uploadTexture2DFromCooked(*device, *textureFile);
        if (!texture)
        {
            return Tina::Core::failure(std::move(texture.error()));
        }
        if (const auto status = device->setSprite2DTextureBinding(ProductSpriteKey, *texture); !status)
        {
            (void)device->destroyTexture2D(*texture);
            return status;
        }
        resources_->gpuTexture = *texture;
        ++counters_->texturesUploaded;
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

        struct PanelSpec final {
            Tina::UI::UILayoutStyle layout{};
            Tina::UI::UIBoxPaint paint{};
        };
        // HUD-style overlays: top-left status bar + bottom accent strip (SolidQuad only, no FreeType).
        const std::array panels{
            PanelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(16.0F), Tina::UI::UILayoutLength::Px(12.0F),
                                             Tina::UI::UILayoutLength::Px(280.0F), Tina::UI::UILayoutLength::Px(48.0F)),
                .paint = solidFill(8, 16, 28, 220),
            },
            PanelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(16.0F), Tina::UI::UILayoutLength::Px(480.0F),
                                             Tina::UI::UILayoutLength::Px(320.0F), Tina::UI::UILayoutLength::Px(10.0F)),
                .paint = solidFill(255, 196, 64, 230),
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

        // HUD labels: English + Chinese. Without FreeType these paint as SolidQuad
        // placeholder bars; with FreeType (TINA_SAMPLE_TILEMAP_FREETYPE) Desktop-style
        // SourceHan injection yields real CJK glyphs.
        struct LabelSpec final {
            Tina::UI::UILayoutStyle layout{};
            std::string_view text{};
            Tina::UI::UITextStyle style{};
        };
        const std::array labels{
            LabelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(28.0F), Tina::UI::UILayoutLength::Px(20.0F),
                                             Tina::UI::UILayoutLength::Px(240.0F), Tina::UI::UILayoutLength::Px(28.0F)),
                .text = "TileMap 2D",
                .style =
                    Tina::UI::UITextStyle{
                        .logicalSize = 22.0F,
                        .advanceScale = 0.65F,
                        .lineHeightScale = 1.15F,
                        .color = {.red = 120, .green = 240, .blue = 255, .alpha = 255},
                    },
            },
            LabelSpec{
                .layout = absolutePanelStyle(Tina::UI::UILayoutLength::Px(28.0F), Tina::UI::UILayoutLength::Px(44.0F),
                                             Tina::UI::UILayoutLength::Px(240.0F), Tina::UI::UILayoutLength::Px(28.0F)),
                .text = "中文地图",
                .style =
                    Tina::UI::UITextStyle{
                        .logicalSize = 22.0F,
                        .advanceScale = 0.65F,
                        .lineHeightScale = 1.15F,
                        .color = {.red = 255, .green = 210, .blue = 80, .alpha = 255},
                    },
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
        // runs can click "Demo" (no world side-effect required for the JSON gate).
        {
            auto button = tree->createButton(root->rootNodeId());
            if (!button)
            {
                return Tina::Core::failure(std::move(button.error()));
            }
            if (auto status = tree->setLayoutStyle(
                    *button, absolutePanelStyle(Tina::UI::UILayoutLength::Px(700.0F),
                                                Tina::UI::UILayoutLength::Px(12.0F),
                                                Tina::UI::UILayoutLength::Px(120.0F),
                                                Tina::UI::UILayoutLength::Px(40.0F)));
                !status)
            {
                return status;
            }
            if (auto status = tree->setBoxPaint(*button, solidFill(40, 120, 80, 230)); !status)
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
        }

        uiRoot_ = std::move(*root);
        ++counters_->uiRootsCreated;
        counters_->uiPanelsCreated += panels.size();
        counters_->uiTextLabelsCreated += labels.size();
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
        if (auto* device = capture_->get(); device != nullptr && resources_->gpuTexture)
        {
            (void)device->setSprite2DTextureBinding(ProductSpriteKey, {});
            (void)device->destroyTexture2D(resources_->gpuTexture);
            resources_->gpuTexture = {};
        }
        ++counters_->stateExits;
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override { return {}; }

    Tina::Core::Status fixedUpdate(Tina::FixedUpdateContext& context) override
    {
        if (!resources_->map)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal, "tilemap selection map missing");
        }

        const Tina::Asset::TileMapInstance& map = *resources_->map;
        const Tina::Sample2D::TileSelectionGrid grid{
            .widthCells = map.widthCells(),
            .heightCells = map.heightCells(),
            .cellSizeMeters = map.cellSizeMeters(),
        };

        // Controlled product gate (M10-A44): once per run, inject a locked
        // Pressed+hit WorldPointerSample edge into the same consumer as A42
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
            counters_->lastSelectedTileId = map.tileIdAt(options_.seedTileCellX, options_.seedTileCellY);
        }

        const u64 previousSelectionHits = counters_->tileSelection.selectionHits;
        Tina::Sample2D::consumeTileSelectionTransitions(context.simulationActions().transitions, SelectTileAction,
                                                        grid, counters_->tileSelection);

        if (counters_->tileSelection.selectionHits != previousSelectionHits &&
            counters_->tileSelection.lastSelection.has_value())
        {
            const Tina::Sample2D::SelectedTile& selection = *counters_->tileSelection.lastSelection;
            counters_->lastSelectedTileId = map.tileIdAt(selection.cellX, selection.cellY);
        }
        return Tina::Core::success();
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_->frameUpdates;
        if (Tina::Audio::AudioEngine* audio = context.audioEngine(); audio != nullptr)
        {
            counters_->audioEnginePresent = true;
#if defined(TINA_SAMPLE_TILEMAP_AUDIO_MINIAUDIO)
            // First frame with Audio: create null device, attach mixer, start.
            if (!resources_->audioDevice.has_value())
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
            else if (resources_->audioDevice->isRunning())
            {
                counters_->audioDeviceCallbacks = resources_->audioDevice->callbackInvocations();
            }
#endif
            if (!counters_->audioOneShotQueued)
            {
                // M11-A19: play cooked AudioClip held by AssetLease (recipe audioclip line).
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
                counters_->audioOneShotQueued = true;
                counters_->audioFromCatalogLease = true;
                counters_->audioClipFrameCount = clip->frameCount;
                counters_->audioClipSampleRate = clip->sampleRate;
            }
            // Host pumps completions after updateFrame; Started is visible next frame.
            if (auto stats = audio->stats(); stats.has_value())
            {
                counters_->audioStartedCount = stats->completedStarted;
                if (stats->completedStarted > 0)
                {
                    counters_->audioStartedObserved = true;
                }
#if defined(TINA_SAMPLE_TILEMAP_AUDIO_MINIAUDIO)
                counters_->audioMixFramesRendered = stats->mixFramesRendered;
#endif
            }
        }
        if (resources_->controller && resources_->grid)
        {
            // Hermetic product demo: after first ground contact, walk right until wall.
            // Keyboard MoveLeft/MoveRight bindings remain available for interactive runs
            // and override the scripted walk when held.
            float wishX = 0.0f;
            if (context.frameActions().isHeld(MoveLeftAction))
            {
                wishX -= DemoWalkSpeedMetersPerSecond;
            }
            if (context.frameActions().isHeld(MoveRightAction))
            {
                wishX += DemoWalkSpeedMetersPerSecond;
            }
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
            if (resources_->map)
            {
                clampCameraCenterToMap(*resources_->map, ProductCameraHeightMeters, aspect, followX, followY);
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
                if (begin.bodyA == resources_->dynamicBody || begin.bodyB == resources_->dynamicBody)
                {
                    ++counters_->physicsDynamicContacts;
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
        if (counters_->frameUpdates >= options_.targetFrameCount)
        {
            context.requestExitAfterFrame();
        }
        return Tina::Core::success();
    }

    Tina::Core::Status extractRenderScene(Tina::RenderSceneExtractionContext& context) const override
    {
        if (!resources_->map || !resources_->controller)
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
        // previous/current interpolation -> camera view -> RenderScene pixel snap.
        const double alpha = context.frameTiming().interpolation;
        const float alphaF =
            static_cast<float>(std::clamp(alpha, 0.0, 1.0));
        const float centerX =
            resources_->cameraPreviousX + (resources_->cameraCurrentX - resources_->cameraPreviousX) * alphaF;
        const float centerY =
            resources_->cameraPreviousY + (resources_->cameraCurrentY - resources_->cameraPreviousY) * alphaF;
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
        if (auto status = writer.setCamera2D(*camera); !status)
        {
            return status;
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
            *resources_->map, query, Tina::Asset::TileChunkSpriteEmitParams{.spriteKey = ProductSpriteKey},
            tileSprites);
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
            auto rebuilds =
                resources_->chunkDirtyCache->syncVisible(*resources_->map, query, resources_->chunkDirtyRebuilt);
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

        const auto& st = resources_->controller->state();
        const Tina::Render::RenderSprite2DInput character{
            .spriteKey = ProductSpriteKey,
            .stableEntityKey = 900001,
            .centerX = st.positionX,
            .centerY = st.positionY,
            .widthMeters = resources_->controller->config().halfWidth * 2.0f,
            .heightMeters = resources_->controller->config().halfHeight * 2.0f,
            .u0 = 0.0f,
            .v0 = 0.0f,
            .u1 = 0.5f,
            .v1 = 1.0f,
            .sortingLayer = 1,
            .orderInLayer = 0,
            .red = 255,
            .green = 220,
            .blue = 80,
            .alpha = 255,
        };
        if (auto status = writer.addSprite2D(character); !status)
        {
            return status;
        }
        u64 totalSprites = *emitted + 1U;
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
        // Product crate sprite follows the dynamic Box2D body (same atlas key as tiles).
        const Tina::Render::RenderSprite2DInput crate{
            .spriteKey = ProductSpriteKey,
            .stableEntityKey = 900002,
            .centerX = resources_->lastDynamicX,
            .centerY = resources_->lastDynamicY,
            .widthMeters = resources_->dynamicHalfExtent * 2.0f,
            .heightMeters = resources_->dynamicHalfExtent * 2.0f,
            .u0 = 0.5f,
            .v0 = 0.0f,
            .u1 = 1.0f,
            .v1 = 1.0f,
            .sortingLayer = 1,
            .orderInLayer = 1,
            .red = 120,
            .green = 220,
            .blue = 255,
            .alpha = 255,
        };
        if (auto status = writer.addSprite2D(crate); !status)
        {
            return status;
        }
        ++totalSprites;
#endif
        // M10-A44: when lastSelection is set, emit exactly one highlight sprite
        // (layer 2, above tiles/character/crate). Fail closed on build/capacity
        // errors — no silent half-state with selection but missing overlay.
        u64 highlightSprites = 0;
        if (counters_->tileSelection.lastSelection.has_value())
        {
            const Tina::Sample2D::SelectedTile& selection = *counters_->tileSelection.lastSelection;
            auto highlight = Tina::Sample2D::makeSelectionHighlightSprite(
                selection,
                Tina::Sample2D::TileSelectionGrid{
                    .widthCells = resources_->map->widthCells(),
                    .heightCells = resources_->map->heightCells(),
                    .cellSizeMeters = resources_->map->cellSizeMeters(),
                },
                ProductSpriteKey);
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
        counters_->lastTotalSprites = totalSprites;
        ++counters_->renderExtractions;
        return Tina::Core::success();
    }

  private:
    SampleOptions options_{};
    LifecycleCounters* counters_ = nullptr;
    TileMapResources* resources_ = nullptr;
    DeviceCapture* capture_ = nullptr;
    Tina::UI::UIRootOwner uiRoot_{};
};

class TileMapBgfxApplication final : public Tina::IGameApplication {
  public:
    TileMapBgfxApplication(SampleOptions options, LifecycleCounters& counters, TileMapResources& resources,
                           DeviceCapture& capture) noexcept
        : options_(options), counters_(&counters), resources_(&resources), capture_(&capture)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(Tina::GameStartupContext& context) override
    {
        // Seed surface from engine primary-window config; metrics events refine
        // framebuffer size after DPI/resize (M11-B0).
        const auto& window = context.engineConfig().primaryWindow;
        counters_->surfacePixelWidth = window.initialLogicalExtent.width;
        counters_->surfacePixelHeight = window.initialLogicalExtent.height;
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
    DeviceCapture* capture_ = nullptr;
    std::optional<Tina::PlatformEventSubscription> platformEvents_{};
};

#if defined(TINA_SAMPLE_TILEMAP_FREETYPE)
[[nodiscard]] std::shared_ptr<std::vector<std::byte>> loadFontFixtureBytes(const char* path)
{
    if (path == nullptr || path[0] == '\0')
    {
        return {};
    }
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return {};
    }
    input.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(input.tellg());
    input.seekg(0, std::ios::beg);
    auto bytes = std::make_shared<std::vector<std::byte>>(size);
    if (size > 0)
    {
        input.read(reinterpret_cast<char*>(bytes->data()), static_cast<std::streamsize>(size));
    }
    if (!input)
    {
        return {};
    }
    return bytes;
}
#endif

[[nodiscard]] Tina::EngineCompositionFactories createFactories(DeviceCapture& capture)
{
    Tina::EngineCompositionFactories factories{
        .createMonotonicClock = []() -> Tina::Core::Result<std::unique_ptr<Tina::Core::IMonotonicClock>> {
            return std::unique_ptr<Tina::Core::IMonotonicClock>{std::make_unique<Tina::Core::SteadyMonotonicClock>()};
        },
        .createTaskSystem =
            [](const Tina::Task::TaskSystemCreateParams& params) {
                Tina::Task::TaskSystemCreateParams effective = params;
                if (effective.ioWorkerCount == 0)
                {
                    effective.ioWorkerCount = 1;
                }
                if (effective.ioQueueCapacity == 0)
                {
                    effective.ioQueueCapacity = 64;
                }
                if (effective.mainQueueCapacity == 0)
                {
                    effective.mainQueueCapacity = 64;
                }
                return Tina::Task::createBoundedTaskSystem(effective);
            },
        .platformRender =
            Tina::WindowSurfacePlatformRenderFactories{
                .createWindowSurfacePlatformBackend = Tina::Platform::createGlfwWindowSurfacePlatformBackend,
                .createWindowSurfaceRenderDevice =
                    [&capture](const Tina::Render::RenderDeviceCreateParams& params,
                               Tina::Integration::NativeWindowSurfaceLease lease)
                        -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
                        auto device = Tina::Render::Bgfx::createBgfxRenderDevice(params, std::move(lease));
                        if (!device)
                        {
                            return device;
                        }
                        std::unique_ptr<Tina::Render::IRenderDevice> capturing =
                            std::make_unique<CapturingRenderDevice>(std::move(*device), capture);
                        return capturing;
                    },
            },
        .createAudioEngine =
            []() -> Tina::Core::Result<Tina::Audio::AudioEngine> {
                return Tina::Audio::AudioEngine::Create(Tina::Audio::AudioEngineConfig{
                    .voiceCapacity = 16,
                    .commandCapacity = 32,
                    .completionCapacity = 32,
                });
            },
    };

#if defined(TINA_SAMPLE_TILEMAP_FREETYPE)
#if defined(TINA_SAMPLE_TILEMAP_FONT_PATH)
    auto fontBytes = loadFontFixtureBytes(TINA_SAMPLE_TILEMAP_FONT_PATH);
#else
    std::shared_ptr<std::vector<std::byte>> fontBytes{};
#endif
    if (fontBytes && !fontBytes->empty())
    {
        factories.createPrimaryWindowUIContext =
            [fontBytes](Tina::Platform::WindowId ownerWindow, const Tina::UI::UIContextCapacityConfig& capacities,
                        std::pmr::memory_resource& resource) -> Tina::Core::Result<std::unique_ptr<Tina::UI::UIContext>> {
                auto rasterizer = Tina::UI::createFreeTypeTextRasterizer({}, resource);
                if (!rasterizer)
                {
                    return Tina::Core::failure(std::move(rasterizer.error()));
                }
                auto context =
                    Tina::UI::UIContext::Create(ownerWindow, capacities, std::move(*rasterizer), resource);
                if (!context)
                {
                    return Tina::Core::failure(std::move(context.error()));
                }
                const auto open =
                    (*context)->openTextFont(std::span<const std::byte>(fontBytes->data(), fontBytes->size()));
                if (!open)
                {
                    return Tina::Core::failure(std::move(open.error()));
                }
                return std::move(*context);
            };
    }
#endif

    return factories;
}

[[nodiscard]] Tina::EngineConfig createEngineConfig()
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina Sample 2D";
    config.primaryWindow.title = "Tina Sample 2D — TileMap + Character + UI";
    config.primaryWindow.initialLogicalExtent = {960, 540};
    config.primaryWindow.initiallyVisible = true;
    config.renderSceneCapacities.spriteCapacity = 64;
    // A/D + arrows for interactive walk; automated smoke uses scripted walk after land.
    config.inputActions.digitalBindings.push_back(Tina::DigitalActionBinding{
        .input = Tina::PrimaryWindowKeyBinding{.key = Tina::Platform::Key::A},
        .action = MoveLeftAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    config.inputActions.digitalBindings.push_back(Tina::DigitalActionBinding{
        .input = Tina::PrimaryWindowKeyBinding{.key = Tina::Platform::Key::Left},
        .action = MoveLeftAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    config.inputActions.digitalBindings.push_back(Tina::DigitalActionBinding{
        .input = Tina::PrimaryWindowKeyBinding{.key = Tina::Platform::Key::D},
        .action = MoveRightAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    config.inputActions.digitalBindings.push_back(Tina::DigitalActionBinding{
        .input = Tina::PrimaryWindowKeyBinding{.key = Tina::Platform::Key::Right},
        .action = MoveRightAction,
        .domain = Tina::InputActionDomain::Frame,
    });
    config.inputActions.digitalBindings.push_back(Tina::DigitalActionBinding{
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
    TileMapResources resources{};
    if (const auto status = prepareCatalog(resources, counters); !status)
    {
        writeError(status.error());
        return 1;
    }

    DeviceCapture capture{};
    auto host = Tina::EngineHost::Create(createEngineConfig(), createFactories(capture));
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

    std::error_code ec;
    std::filesystem::remove_all(resources.catalogRoot, ec);

    const Tina::Sample2D::SelectedTile* lastSelection =
        counters.tileSelection.lastSelection.has_value() ? &*counters.tileSelection.lastSelection : nullptr;
    const u64 classifiedPointerPresses = counters.tileSelection.missingWorldPointerSamples +
                                         counters.tileSelection.viewportMisses + counters.tileSelection.mapMisses +
                                         counters.tileSelection.selectionHits;
    const bool selectionCountersValid = counters.tileSelection.pointerPresses == classifiedPointerPresses;
    const bool selectionLatchValid =
        (counters.tileSelection.selectionHits == 0 && lastSelection == nullptr) ||
        (counters.tileSelection.selectionHits > 0 && lastSelection != nullptr && resources.map.has_value() &&
         lastSelection->cellX < resources.map->widthCells() && lastSelection->cellY < resources.map->heightCells());
    const bool selectionStateValid = selectionCountersValid && selectionLatchValid;
    const u64 expectedHighlightSprites = lastSelection != nullptr ? 1U : 0U;
    const u64 expectedTotalSprites = ExpectedSpritesWithPhysics + expectedHighlightSprites;
    // Seed path: selection from frame 0 → highlight every extract. Accidental OS
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

    const bool audioValid =
        counters.audioEnginePresent && counters.audioOneShotQueued && counters.audioStartedObserved &&
        counters.audioStartedCount >= 1 && counters.audioFromCatalogLease && counters.audioClipFrameCount > 0 &&
        counters.audioClipSampleRate == 48000;
#if defined(TINA_SAMPLE_TILEMAP_AUDIO_MINIAUDIO)
    // M11-A16: null-backend device must start, run callbacks, and advance mixRealtime.
    const bool audioDeviceValid =
        counters.audioDeviceCreated && counters.audioDeviceNullBackend && counters.audioDeviceCallbacks > 0 &&
        counters.audioMixFramesRendered > 0;
#else
    const bool audioDeviceValid = true;
#endif

    bool ok = selectionStateValid && highlightValid && seededSelectionValid && cameraProjectionValid &&
              cameraFollowValid && chunkDirtyValid && audioValid && audioDeviceValid &&
              counters.catalogFromRecipeFile &&
              counters.catalogRecipeAssets == 4 && counters.texturesUploaded == 1 &&
              counters.lastTileSprites == ExpectedNonEmptyTiles && counters.lastTotalSprites == expectedTotalSprites &&
              counters.controllerGroundedFrames > 0 && counters.controllerWalkFrames > 0 &&
              counters.controllerHitRightFrames > 0 && counters.maxControllerX > 1.5f &&
              counters.renderExtractions == counters.frameUpdates && counters.stateExits == 1 &&
              counters.applicationShutdowns == 1 && counters.uiRootsCreated == 1 &&
              counters.uiPanelsCreated == ExpectedUIPanelCount &&
              counters.uiTextLabelsCreated == ExpectedUITextLabelCount &&
              counters.uiButtonsCreated == ExpectedUIButtonCount && counters.uiButtonActionsWired == 1 &&
              counters.uiRootsReleased == 1 && *run == Tina::RunExitReason::GameRequestedExitAfterCurrentFrame;
#if defined(TINA_SAMPLE_TILEMAP_PHYSICS2D)
    ok = ok && counters.physicsReady && counters.physicsStaticBodies == ExpectedPhysicsStaticBodies &&
         counters.physicsSteps == counters.frameUpdates && counters.physicsDynamicContacts > 0 &&
         counters.lastDynamicY < 3.5f && counters.lastDynamicY > 0.5f;
#endif
    if (!ok)
    {
        std::cerr << "{\"status\":\"error\",\"sample\":\"tina_sample_2d\","
                     "\"message\":\"verification failed\","
                     "\"frames\":"
                  << counters.frameUpdates << ",\"tileSprites\":" << counters.lastTileSprites
                  << ",\"totalSprites\":" << counters.lastTotalSprites
                  << ",\"grounded\":" << counters.controllerGroundedFrames
                  << ",\"walkFrames\":" << counters.controllerWalkFrames
                  << ",\"hitRight\":" << counters.controllerHitRightFrames
                  << ",\"maxX\":" << counters.maxControllerX << ",\"uiRoots\":" << counters.uiRootsCreated
                  << ",\"uiPanels\":" << counters.uiPanelsCreated << ",\"uiLabels\":" << counters.uiTextLabelsCreated
                  << ",\"uiButtons\":" << counters.uiButtonsCreated << ",\"uiReleased\":" << counters.uiRootsReleased
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
                  << ",\"cameraProjectionResolves\":" << counters.cameraProjectionResolves
                  << ",\"renderExtractions\":" << counters.renderExtractions
                  << ",\"audioEnginePresent\":" << (counters.audioEnginePresent ? "true" : "false")
                  << ",\"audioOneShotQueued\":" << (counters.audioOneShotQueued ? "true" : "false")
                  << ",\"audioStartedObserved\":" << (counters.audioStartedObserved ? "true" : "false")
                  << ",\"audioStartedCount\":" << counters.audioStartedCount
                  << ",\"audioFromCatalogLease\":" << (counters.audioFromCatalogLease ? "true" : "false")
                  << ",\"audioClipFrameCount\":" << counters.audioClipFrameCount
                  << ",\"audioClipSampleRate\":" << counters.audioClipSampleRate
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
                  << ",\"dynamicY\":" << counters.lastDynamicY
#endif
                  << "}\n";
        return 1;
    }

    // Formal product sample name is tina_sample_2d; feature flags report which product
    // slices were compiled (Physics2D / FreeType). M10-A43/A44 consume A42 locked
    // world-pointer payload, draw selection highlight, and optionally seed selection
    // via --seed-tile-selection=cellX,cellY for automated product evidence.
    std::cout << "{\"status\":\"ok\",\"sample\":\"tina_sample_2d\""
              << ",\"frames\":" << counters.frameUpdates << ",\"renderExtractions\":" << counters.renderExtractions
              << ",\"catalogFromRecipeFile\":" << (counters.catalogFromRecipeFile ? "true" : "false")
              << ",\"catalogRecipeAssets\":" << counters.catalogRecipeAssets
              << ",\"texturesUploaded\":" << counters.texturesUploaded
              << ",\"tileSpritesPerFrame\":" << ExpectedNonEmptyTiles
              << ",\"spritesPerFrame\":" << expectedTotalSprites
              << ",\"controllerGroundedFrames\":" << counters.controllerGroundedFrames
              << ",\"controllerWalkFrames\":" << counters.controllerWalkFrames
              << ",\"controllerHitRightFrames\":" << counters.controllerHitRightFrames
              << ",\"maxControllerX\":" << counters.maxControllerX
              << ",\"uiRootsCreated\":" << counters.uiRootsCreated
              << ",\"uiPanelsCreated\":" << counters.uiPanelsCreated
              << ",\"uiTextLabelsCreated\":" << counters.uiTextLabelsCreated
              << ",\"uiButtonsCreated\":" << counters.uiButtonsCreated
              << ",\"uiButtonActionsWired\":" << counters.uiButtonActionsWired
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
              << ",\"audioFromCatalogLease\":" << (counters.audioFromCatalogLease ? "true" : "false")
              << ",\"audioClipFrameCount\":" << counters.audioClipFrameCount
              << ",\"audioClipSampleRate\":" << counters.audioClipSampleRate
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
              << ",\"applicationShutdowns\":" << counters.applicationShutdowns << ",\"exit\":\""
              << "GameRequestedExitAfterCurrentFrame\"}\n";
    return 0;
}
