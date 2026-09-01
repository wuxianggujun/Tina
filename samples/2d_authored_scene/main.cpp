// Runs a scene the Editor could have saved, using Gameplay2D::Scene2DRuntime.
//
// This is the first consumer of Tina::Gameplay2D outside the tests, and it exists
// to prove one claim from ADR 0031: that a game no longer hand-writes the per-frame
// order and lease lifetime of authored resource nodes. samples/2d_tilemap_bgfx does
// all of that by hand and is 6660 lines; the orchestration here is a handful of
// calls, and every ordering mistake it used to be possible to make now returns an
// error instead of quietly drawing or playing the wrong thing.
//
// Deliberately headless (null render device, disabled task system): the contract
// under test is ownership and ordering, not pixels, so this runs anywhere without a
// GPU. Pixel evidence stays with the bgfx product samples.
//
// The scene is written to a real .tworld through the same writer the Editor uses,
// then read back through loadWorld2DSceneFromFile. Authoring it in-process rather
// than checking in a binary fixture keeps the authored intent readable and means a
// schema bump breaks this sample loudly instead of leaving a stale blob behind.

#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/GridCollision.hpp>
#include <tina/asset_format/World2DSnapshot.hpp>
#include <tina/audio/AudioEngine.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/gameplay2d/Scene2DRuntime.hpp>
#include <tina/physics2d/PhysicsWorld2D.hpp>
#include <tina/platform/headless/HeadlessPlatformFactory.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include <tina/scene/ExtractRenderScene.hpp>
#include <tina/scene/World.hpp>
#include <tina/scene/World2DSnapshot.hpp>
#include <tina/task/disabled/DisabledTaskSystemFactory.hpp>

#include <array>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using Tina::Core::u32;
using Tina::Core::u64;
using Tina::Core::u8;
using Tina::Core::usize;

// Bumped whenever a field is added, removed or redefined, so a gate cannot silently
// compare against a different contract.
inline constexpr u32 EvidenceSchema = 1;

inline constexpr u64 DefaultFrameCount = 300;

// Asset seeds. The trailing byte differs from the leading one so a truncated or
// byte-swapped id cannot alias another asset.
inline constexpr u8 TextureSeed = 0x21;
inline constexpr u8 SpriteSeed = 0x22;
inline constexpr u8 TilesetSeed = 0x23;
inline constexpr u8 TileMapSeed = 0x24;
inline constexpr u8 Fx2DSeed = 0x25;
inline constexpr u8 NavigationSeed = 0x26;
inline constexpr u8 AudioSeed = 0x27;

// Authored stable entity ids. These are what a game keys its own state off, and
// what the .tworld persists; runtime EntityId values never reach the file.
inline constexpr u32 RootStableId = 1;
inline constexpr u32 TileMapStableId = 2;
inline constexpr u32 FxStableId = 3;
inline constexpr u32 NavigationStableId = 4;
inline constexpr u32 AudioStableId = 5;
inline constexpr u32 CameraStableId = 6;
inline constexpr u32 CrateBodyStableId = 7;
inline constexpr u32 CrateShapeStableId = 8;
inline constexpr u32 FloorBodyStableId = 9;
inline constexpr u32 FloorShapeStableId = 10;

// The authored map is 4x2 with two tile layers: layer 10 visible (6 non-empty
// cells), layer 20 invisible collision (4 cells along the bottom row). Only the
// visible layer may emit sprites; both must stream, because the invisible one is
// what a game queries for collision.
inline constexpr Tina::AssetFormat::TileMapLayerId VisualLayerId = 10;
inline constexpr Tina::AssetFormat::TileMapLayerId CollisionLayerId = 20;
inline constexpr u64 ExpectedVisibleTiles = 6;
inline constexpr u64 ExpectedCollisionCells = 4;
inline constexpr usize ExpectedTileLayers = 2;
inline constexpr u64 ExpectedTileChunks = 2; // one chunk per tile layer at this size
// The cooked Fx2D emits a fixed 10-particle burst on instantiation.
inline constexpr u64 ExpectedParticleSprites = 10;
// What one frame writes: visible tiles plus the Fx burst. The collision layer is
// absent on purpose, which is the property this pins.
inline constexpr u64 ExpectedFrameSprites = ExpectedVisibleTiles + ExpectedParticleSprites;
// Ten authored nodes: root, four resource kinds, camera, crate body + shape,
// floor body + shape.
inline constexpr usize ExpectedAuthoredEntities = 10;

// Authored world position of the TileMap node. Non-zero on purpose: it proves the
// authored transform places the map rather than the map always sitting at origin.
inline constexpr float TileMapOriginX = 8.0F;
inline constexpr float TileMapOriginY = 3.0F;
// The crate drops from CrateStartY onto a static floor at FloorY. Both are authored,
// so the settled height is a property of the scene rather than of the frame count.
inline constexpr float CrateStartY = 6.0F;
inline constexpr float FloorY = 0.0F;
// Floor top (0.5) + crate half extent (0.5), the height a box settles at.
inline constexpr float ExpectedCrateRestY = 1.0F;

// Stand-in for a framebuffer extent. Headless reports no surface, and a 0x0 viewport
// leaves an authored Camera2D unresolvable, so the projection needs a reference size.
inline constexpr u32 SurfacePixelWidth = 640;
inline constexpr u32 SurfacePixelHeight = 360;

[[nodiscard]] Tina::Core::AssetId assetId(u8 seed)
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xA5U);
    return *Tina::Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] std::string idText(u8 seed)
{
    const auto text = assetId(seed).canonicalText();
    return std::string{text.data(), text.size()};
}

// The catalog this scene's AssetIds resolve against. Cooked at startup so the sample
// is self-contained; a shipped game would open a catalog the Cooker produced.
[[nodiscard]] std::string recipeText()
{
    std::string recipe;
    recipe += "platform WindowsX64\n";
    recipe += "texture2d " + idText(TextureSeed) + " 2 1 F08C28FF 28C8DCFF\n";
    recipe += "sprite " + idText(SpriteSeed) + " " + idText(TextureSeed) + " 0 0 0.5 1 0.5 0.5 1\n";
    recipe += "tileset " + idText(TilesetSeed) + " " + idText(TextureSeed) + " 1 1\n";
    recipe += "tile 1 1 0 0 0.5 1\n";
    recipe += "tile 2 0 0.5 0 1 1\n";
    recipe += "tilemap " + idText(TileMapSeed) + " " + idText(TilesetSeed) + " 4 2 1.0\n";
    recipe += "tilelayer 10 1 visual\n";
    recipe += "row 1 1 2 2\n";
    recipe += "row 0 1 0 2\n";
    recipe += "endlayer\n";
    recipe += "tilelayer 20 0 collision\n";
    recipe += "row 1 1 1 1\n";
    recipe += "row 0 0 0 0\n";
    recipe += "endlayer\n";
    recipe += "endtilemap\n";
    recipe += "navigation2d " + idText(NavigationSeed) + " 2 2 0 0 1 0:1 0:1 1:1 0:1\n";
    recipe += "audioclip " + idText(AudioSeed) + " 48000 1 480 sine 880\n";
    recipe += "fx2d " + idText(Fx2DSeed) + " " + idText(SpriteSeed) +
              " 12 10 1414090305 4294967296 4.0 2.0 -0.55 -0.35 0.55 0.35 -0.02 -0.01"
              " 0.02 0.02 10 10 0.20 0.20 0.12 0.12 3875527770 2030013520 0.0 2 11 8 10"
              " 0.18 0.04 8589934592 0 0 1 1 3535070790 1 8\n";
    return recipe;
}

// The scene, in the same descriptors the Editor writes. One resource node per kind,
// plus a physics body with a child shape so the bridge has something to simulate.
[[nodiscard]] std::vector<Tina::AssetFormat::World2DEntityDesc> authoredEntities()
{
    using Tina::AssetFormat::World2DNodeKind;
    using Tina::AssetFormat::World2DPhysicsBodyDesc;
    using Tina::AssetFormat::World2DPhysicsShapeDesc;
    using Tina::AssetFormat::World2DResourceNodeDesc;

    std::vector<Tina::AssetFormat::World2DEntityDesc> entities;

    Tina::AssetFormat::World2DEntityDesc root{};
    root.stableEntityId = RootStableId;
    root.nodeKind = World2DNodeKind::Node2D;
    root.name = "Level";
    entities.push_back(root);

    Tina::AssetFormat::World2DEntityDesc tileMap{};
    tileMap.stableEntityId = TileMapStableId;
    tileMap.parentStableEntityId = RootStableId;
    tileMap.nodeKind = World2DNodeKind::TileMap2D;
    tileMap.name = "Ground";
    tileMap.positionX = TileMapOriginX;
    tileMap.positionY = TileMapOriginY;
    tileMap.resource = World2DResourceNodeDesc{.assetId = assetId(TileMapSeed), .active = true};
    entities.push_back(tileMap);

    Tina::AssetFormat::World2DEntityDesc fx{};
    fx.stableEntityId = FxStableId;
    fx.parentStableEntityId = RootStableId;
    fx.nodeKind = World2DNodeKind::FxEmitter2D;
    fx.name = "Sparks";
    fx.positionX = 2.0F;
    fx.positionY = 1.0F;
    fx.resource = World2DResourceNodeDesc{.assetId = assetId(Fx2DSeed), .active = true};
    entities.push_back(fx);

    Tina::AssetFormat::World2DEntityDesc navigation{};
    navigation.stableEntityId = NavigationStableId;
    navigation.parentStableEntityId = RootStableId;
    navigation.nodeKind = World2DNodeKind::NavigationRegion2D;
    navigation.name = "Walkable";
    navigation.resource = World2DResourceNodeDesc{.assetId = assetId(NavigationSeed), .active = true};
    entities.push_back(navigation);

    Tina::AssetFormat::World2DEntityDesc audio{};
    audio.stableEntityId = AudioStableId;
    audio.parentStableEntityId = RootStableId;
    audio.nodeKind = World2DNodeKind::AudioPlayer2D;
    audio.name = "Chime";
    audio.resource = World2DResourceNodeDesc{.assetId = assetId(AudioSeed), .active = true};
    entities.push_back(audio);

    Tina::AssetFormat::World2DEntityDesc camera{};
    camera.stableEntityId = CameraStableId;
    camera.parentStableEntityId = RootStableId;
    camera.nodeKind = World2DNodeKind::Camera2D;
    camera.name = "MainCamera";
    camera.positionX = TileMapOriginX + 2.0F;
    camera.positionY = TileMapOriginY + 1.0F;
    camera.camera = Tina::AssetFormat::World2DCameraDesc{};
    entities.push_back(camera);

    // Unparented, so the bridge writes simulated transforms straight back into its
    // LocalTransform. A parented body would be skipped: its local transform is
    // parent-relative while the body is world-space.
    Tina::AssetFormat::World2DEntityDesc crate{};
    crate.stableEntityId = CrateBodyStableId;
    crate.nodeKind = World2DNodeKind::RigidBody2D;
    crate.name = "Crate";
    crate.positionX = TileMapOriginX + 1.0F;
    crate.positionY = CrateStartY;
    crate.physicsBody = World2DPhysicsBodyDesc{};
    entities.push_back(crate);

    // Shape ownership follows the hierarchy: nearest physics-body ancestor.
    Tina::AssetFormat::World2DEntityDesc crateShape{};
    crateShape.stableEntityId = CrateShapeStableId;
    crateShape.parentStableEntityId = CrateBodyStableId;
    crateShape.nodeKind = World2DNodeKind::CollisionShape2D;
    crateShape.name = "CrateShape";
    crateShape.physicsShape = World2DPhysicsShapeDesc{};
    entities.push_back(crateShape);

    // A static floor, so the crate lands on something authored instead of falling
    // forever. That turns the physics evidence into a settled resting height rather
    // than a number that only ever grows.
    Tina::AssetFormat::World2DEntityDesc floorBody{};
    floorBody.stableEntityId = FloorBodyStableId;
    floorBody.nodeKind = World2DNodeKind::StaticBody2D;
    floorBody.name = "Floor";
    floorBody.positionX = TileMapOriginX + 2.0F;
    floorBody.positionY = FloorY;
    // Static-ness comes from nodeKind above, not from the payload: the wire format
    // carries no body-kind field.
    floorBody.physicsBody = World2DPhysicsBodyDesc{};
    entities.push_back(floorBody);

    Tina::AssetFormat::World2DEntityDesc floorShape{};
    floorShape.stableEntityId = FloorShapeStableId;
    floorShape.parentStableEntityId = FloorBodyStableId;
    floorShape.nodeKind = World2DNodeKind::CollisionShape2D;
    floorShape.name = "FloorShape";
    floorShape.physicsShape = World2DPhysicsShapeDesc{.halfExtentX = 6.0F, .halfExtentY = 0.5F};
    entities.push_back(floorShape);

    return entities;
}

// Everything the run has to prove, collected so main() can decide ok/failed in one
// place and print the same fields either way.
struct SampleCapture final {
    u64 submittedFrames = 0;
    u64 presentedFrames = 0;
    u64 lastSpriteCount = 0;
    u64 minSpriteCount = ~u64{0};
    u64 maxSpriteCount = 0;
    bool lastFrameHadCamera = false;

    // Scene load
    bool sceneLoaded = false;
    usize authoredEntityCount = 0;
    bool foundNodesByAuthoredName = false;

    // Instantiation
    usize tileMapCount = 0;
    usize tileLayerCount = 0;
    usize fxCount = 0;
    usize navigationCount = 0;
    usize audioCount = 0;
    usize unresolvedCount = 0;
    usize residentTileChunks = 0;

    // Per-frame ordering
    u64 demandUpdates = 0;
    u64 commits = 0;
    u64 extracts = 0;
    u64 physicsSteps = 0;
    bool extractBeforeCommitRejected = false;

    // Borrowed map reachability
    bool tileMapReachable = false;
    u64 collisionCellsFound = 0;
    bool navigationGridReachable = false;

    // Authored transform placement
    float fxOriginX = 0.0F;
    float fxOriginY = 0.0F;

    // Physics write-back
    float crateStartY = 0.0F;
    float crateEndY = 0.0F;

    // Audio lifetime
    bool audioVoiceStarted = false;

    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
    u64 renderShutdowns = 0;
    bool runtimeShutdownOk = false;
};

// Counts what reached the frame without needing a GPU. Frame index contiguity and
// submit/present pairing are checked because a phase-ordering regression in the
// runtime would show up here first.
class RecordingNullRenderDevice final : public Tina::Render::IRenderDevice {
  public:
    explicit RecordingNullRenderDevice(SampleCapture& capture) noexcept : capture_(&capture) {}

    [[nodiscard]] Tina::Core::Result<Tina::Render::RenderFrameSubmission>
    submitFrame(const Tina::Render::RenderFrame& frame) override
    {
        if (stopped_)
        {
            return Tina::Core::failure(Tina::Render::RenderErrorCode::DeviceStopped,
                                       "the authored scene render device is stopped");
        }
        if (frameOpen_)
        {
            return Tina::Core::failure(Tina::Render::RenderErrorCode::FrameAlreadyOpen,
                                       "the authored scene device requires present between submits");
        }
        if (frame.frameIndex != nextFrameIndex_)
        {
            return Tina::Core::failure(Tina::Render::RenderErrorCode::UnexpectedFrameIndex,
                                       "authored scene frame indices must be contiguous");
        }
        const Tina::Render::RenderSceneView scene = frame.primaryWorldScene;
        capture_->lastFrameHadCamera = scene.camera2D().has_value();
        capture_->lastSpriteCount = scene.sprites2D().size();
        if (scene.sprites2D().size() < capture_->minSpriteCount)
        {
            capture_->minSpriteCount = scene.sprites2D().size();
        }
        if (scene.sprites2D().size() > capture_->maxSpriteCount)
        {
            capture_->maxSpriteCount = scene.sprites2D().size();
        }
        ++capture_->submittedFrames;
        ++nextFrameIndex_;
        frameOpen_ = true;
        return Tina::Render::RenderFrameSubmission::Submitted(capture_->submittedFrames - 1U);
    }

    [[nodiscard]] Tina::Core::Status present() override
    {
        if (stopped_ || !frameOpen_)
        {
            return Tina::Core::failure(Tina::Render::RenderErrorCode::NoFrameSubmitted,
                                       "the authored scene device has no open frame");
        }
        frameOpen_ = false;
        ++capture_->presentedFrames;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Render::RenderStatistics statistics() const noexcept override
    {
        return Tina::Render::RenderStatistics{
            .submitted = capture_->submittedFrames,
            .presented = capture_->presentedFrames,
            .liveResources = 0,
        };
    }

    void shutdown() noexcept override
    {
        if (stopped_)
        {
            return;
        }
        stopped_ = true;
        frameOpen_ = false;
        ++capture_->renderShutdowns;
    }

  private:
    SampleCapture* capture_ = nullptr;
    u64 nextFrameIndex_ = 0;
    bool frameOpen_ = false;
    bool stopped_ = false;
};

[[nodiscard]] Tina::Core::Status writeSceneFile(const std::filesystem::path& path)
{
    const auto entities = authoredEntities();
    auto bytes = Tina::AssetFormat::writeWorld2DSnapshotBytes(
        Tina::AssetFormat::World2DSnapshotDesc{.entities = entities});
    if (!bytes)
    {
        return Tina::Core::failure(std::move(bytes.error()));
    }
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    if (!file)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Io,
                                   "could not open the authored scene file for writing");
    }
    file.write(reinterpret_cast<const char*>(bytes->data()),
               static_cast<std::streamsize>(bytes->size()));
    if (!file)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Io,
                                   "could not write the authored scene file");
    }
    return Tina::Core::success();
}

// A texture binding stand-in. A product resolves these through its Sprite/Tileset
// registry; extraction only needs a live FrameResourceRef, and which device binding
// it names is the product's business rather than the runtime's.
struct SampleBindingResolver final {
    [[nodiscard]] Tina::Asset::AssetFrameResourceResolver resolver() noexcept
    {
        return Tina::Asset::AssetFrameResourceResolver{.userData = this, .resolve = &resolve};
    }

    [[nodiscard]] static Tina::Core::Result<Tina::Render::FrameResourceRef>
    resolve(void* userData, Tina::Asset::AssetHandle asset,
            Tina::Render::FrameResourceSink& sink) noexcept
    {
        auto& self = *static_cast<SampleBindingResolver*>(userData);
        static_cast<void>(asset);
        ++self.resolveCalls;
        Tina::Render::FramePin pin{Tina::Render::FramePinKind::Custom, self.bindingKey, nullptr,
                                   nullptr};
        return sink.intern(
            Tina::Render::FrameResourceDescriptor{
                .kind = Tina::Render::FrameResourceKind::Texture2D,
                .deviceBindingKey = self.bindingKey,
            },
            std::move(pin));
    }

    u32 bindingKey = 91U;
    u64 resolveCalls = 0;
};

class AuthoredSceneState final : public Tina::IGameState {
  public:
    AuthoredSceneState(u64 targetFrames, std::filesystem::path catalogRoot,
                       std::filesystem::path scenePath, SampleCapture& capture) noexcept
        : targetFrames_(targetFrames), catalogRoot_(std::move(catalogRoot)),
          scenePath_(std::move(scenePath)), capture_(&capture)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext&) override
    {
        if (auto status = openCatalog(); !status)
        {
            return status;
        }
        if (auto status = loadScene(); !status)
        {
            return status;
        }
        return buildRuntime();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        ++capture_->stateExits;
        // Reverse of construction, and the ordering the runtime documents: it must go
        // down before the AssetSystem, AudioEngine and PhysicsWorld2D it borrows, and
        // before anything still holding a reference to its resident map.
        collision_.reset();
        capture_->runtimeShutdownOk = static_cast<bool>(runtime_.shutdown());
        if (physics_)
        {
            physics_->shutdown();
        }
        if (audio_)
        {
            audio_->shutdown();
        }
        physics_.reset();
        audio_.reset();
        world_.reset();
        assets_.reset();
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override { return {}; }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        // The whole per-frame orchestration for four authored resource kinds plus
        // physics. Ordering is the runtime's problem now, not this loop's.
        if (auto status = runtime_.updateDemand(cameraQuery()); !status)
        {
            return status;
        }
        ++capture_->demandUpdates;
        // Stays out here on purpose: AssetSystem serves the whole game, so a scene
        // object must not implicitly drive global resource loading.
        if (auto pumped = assets_->pump(8); !pumped)
        {
            return Tina::Core::failure(std::move(pumped.error()));
        }
        if (auto status = runtime_.commitReady(); !status)
        {
            return status;
        }
        ++capture_->commits;
        // Once the first chunks are resident, borrow the map for collision queries.
        // Deferred to here rather than build() because residency needs a commit.
        if (!collision_.has_value())
        {
            if (auto status = prepareCollisionBorrow(); !status)
            {
                return status;
            }
        }
        if (auto status = runtime_.fixedUpdate(Tina::Core::Duration{1.0 / 60.0}); !status)
        {
            return status;
        }
        if (auto status = runtime_.fixedUpdatePhysics(*world_); !status)
        {
            return status;
        }
        // A one-shot voice on the first frame, kept playing across the run so the
        // clip lease has to stay valid until shutdown stops the voice.
        if (!capture_->audioVoiceStarted)
        {
            auto voice = runtime_.playAudio(audioEntity_);
            if (!voice)
            {
                return Tina::Core::failure(std::move(voice.error()));
            }
            capture_->audioVoiceStarted = true;
        }
        if (auto pumped = audio_->pumpCompletions(); !pumped)
        {
            return Tina::Core::failure(std::move(pumped.error()));
        }
        if (auto status = runtime_.releaseFinishedVoices(); !status)
        {
            return status;
        }
        capture_->physicsSteps = runtime_.stats().physicsSteps;
        capture_->residentTileChunks = runtime_.stats().residentTileChunks;
        recordCrateHeight();

        if (context.frameTiming().frameIndex + 1U == targetFrames_)
        {
            context.requestExitAfterFrame();
        }
        return Tina::Core::success();
    }

    Tina::Core::Status extractRenderScene(Tina::RenderSceneExtractionContext& context) const override
    {
        auto& writer = context.renderSceneWriter();
        // The authored Camera2D reaches the frame through the normal Scene path; the
        // runtime only owns the resource nodes.
        Tina::Scene::ExtractRenderSceneParams params{};
        params.spriteBindingResolver = binding_.resolver();
        // A headless platform reports no surface, and a 0x0 viewport makes the
        // authored Camera2D unresolvable, so the projection reference is supplied
        // here. A windowed product reads this from its real framebuffer extent.
        params.surfaceViewport = Tina::Render::Camera2DSurfaceViewport{
            .pixelWidth = SurfacePixelWidth,
            .pixelHeight = SurfacePixelHeight,
        };
        if (auto status = Tina::Scene::extractRenderSceneFromWorld(
                *world_, writer, context.frameResourceSink(), params);
            !status)
        {
            return status;
        }
        if (auto status = runtime_.extract(*world_, writer, context.frameResourceSink(),
                                           binding_.resolver());
            !status)
        {
            return status;
        }
        ++capture_->extracts;
        return Tina::Core::success();
    }

  private:
    [[nodiscard]] Tina::Asset::TileChunkCameraQuery cameraQuery() const noexcept
    {
        // World meters; the runtime rebases into map-local per node using the
        // authored transform, so this does not need to know where the map sits.
        return Tina::Asset::TileChunkCameraQuery{
            .centerX = TileMapOriginX + 2.0F,
            .centerY = TileMapOriginY + 1.0F,
            .halfWidth = 8.0F,
            .halfHeight = 6.0F,
        };
    }

    Tina::Core::Status openCatalog()
    {
        auto request = Tina::Asset::parseCatalogCookRecipe(recipeText(), catalogRoot_.string());
        if (!request)
        {
            return Tina::Core::failure(std::move(request.error()));
        }
        if (auto status = Tina::Asset::cookAndPublishCatalogPackage(catalogRoot_.string(), *request);
            !status)
        {
            return status;
        }
        auto assets = Tina::Asset::AssetSystem::Create(Tina::Asset::AssetSystemConfig{
            .storeCapacity = 32,
            .memoryResource = &memory_,
            .batch =
                Tina::Asset::CookedAssetBatchLoadConfig{
                    .file = Tina::Asset::CookedAssetFileLoadConfig{.memoryResource = &memory_},
                    .memoryResource = &memory_,
                },
            .queueCapacity = 32,
            .defaultPumpBudget = 8,
        });
        if (!assets)
        {
            return Tina::Core::failure(std::move(assets.error()));
        }
        assets_.emplace(std::move(*assets));
        if (auto status = assets_->openAndBindCatalog(catalogRoot_.string()); !status)
        {
            return status;
        }
        // The runtime acquires leases from resident handles; it does not trigger
        // loads. Chunks stay absent on purpose -- those are deferred dependencies
        // the stream requests itself.
        for (const u8 seed :
             {TextureSeed, SpriteSeed, TilesetSeed, TileMapSeed, Fx2DSeed, NavigationSeed, AudioSeed})
        {
            if (auto loaded = assets_->loadOne(assetId(seed)); !loaded)
            {
                return Tina::Core::failure(std::move(loaded.error()));
            }
        }
        return Tina::Core::success();
    }

    Tina::Core::Status loadScene()
    {
        auto world = Tina::Scene::World::Create(Tina::Scene::WorldConfig{.entityCapacity = 64});
        if (!world)
        {
            return Tina::Core::failure(std::move(world.error()));
        }
        world_.emplace(std::move(*world));

        // Sprite/texture resolvers are supplied because a real scene carries sprite
        // nodes; this one does not, but a null resolver would fail closed if it did.
        Tina::Scene::World2DSnapshotAssetResolver resolver{};
        resolver.resolveSprite = [this](Tina::Core::AssetId id) {
            auto handle = assets_->find(id);
            return handle.has_value() ? *handle : Tina::Asset::AssetHandle{};
        };
        resolver.resolveTexture = resolver.resolveSprite;
        resolver.resolveAnimationClip = resolver.resolveSprite;

        auto loaded = Tina::Scene::loadWorld2DSceneFromFile(*world_, scenePath_.string(), resolver);
        if (!loaded)
        {
            return Tina::Core::failure(std::move(loaded.error()));
        }
        capture_->authoredEntityCount = loaded->bindings.size();
        capture_->sceneLoaded = true;

        // Authored names are the minimum a game needs to attach logic to a scene it
        // did not build in code.
        tileMapEntity_ = loaded->index.entityForName("Ground");
        fxEntity_ = loaded->index.entityForName("Sparks");
        navigationEntity_ = loaded->index.entityForName("Walkable");
        audioEntity_ = loaded->index.entityForName("Chime");
        crateEntity_ = loaded->index.entityForName("Crate");
        capture_->foundNodesByAuthoredName =
            tileMapEntity_.hasValue() && fxEntity_.hasValue() && navigationEntity_.hasValue() &&
            audioEntity_.hasValue() && crateEntity_.hasValue();
        if (!capture_->foundNodesByAuthoredName)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::NotFound,
                                       "authored node names did not resolve after load");
        }
        return world_->updateWorldTransforms();
    }

    Tina::Core::Status buildRuntime()
    {
        auto audio = Tina::Audio::AudioEngine::Create(Tina::Audio::AudioEngineConfig{}, memory_);
        if (!audio)
        {
            return Tina::Core::failure(std::move(audio.error()));
        }
        audio_.emplace(std::move(*audio));

        auto physics = Tina::Physics2D::PhysicsWorld2D::Create(Tina::Physics2D::PhysicsWorld2DConfig{
            .gravityMetersPerSecondSquared = {0.0F, -10.0F},
            .fixedDeltaSeconds = 1.0 / 60.0,
        });
        if (!physics)
        {
            return Tina::Core::failure(std::move(physics.error()));
        }
        physics_.emplace(std::move(*physics));

        if (auto status = runtime_.build(*world_, *assets_, &*audio_, &*physics_); !status)
        {
            return status;
        }
        const auto& stats = runtime_.stats();
        capture_->tileMapCount = stats.tileMapCount;
        capture_->tileLayerCount = stats.tileLayerCount;
        capture_->fxCount = stats.fxCount;
        capture_->navigationCount = stats.navigationCount;
        capture_->audioCount = stats.audioCount;
        capture_->unresolvedCount = stats.unresolvedCount;

        // extract before commitReady must be refused rather than drawing a stale or
        // partial map. Probed once here because the steady loop can never do it.
        if (auto tooEarly = probeExtractBeforeCommit(); !tooEarly)
        {
            capture_->extractBeforeCommitRejected = true;
        }

        const Tina::Math::Vec2 origin = runtime_.fxOrigin(fxEntity_);
        capture_->fxOriginX = origin.x;
        capture_->fxOriginY = origin.y;
        capture_->navigationGridReachable = runtime_.navigationGrid(navigationEntity_) != nullptr;
        recordCrateHeight();
        capture_->crateStartY = capture_->crateEndY;
        return Tina::Core::success();
    }

    // Runs the whole streaming sequence once, then queries the resident map through
    // the borrowed reference -- the reason tileMap() has to exist at all.
    Tina::Core::Status prepareCollisionBorrow()
    {
        const Tina::Asset::TileMapInstance* map = runtime_.tileMap(tileMapEntity_);
        if (map == nullptr)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "the authored TileMap node exposed no resident map");
        }
        capture_->tileMapReachable = true;
        // Layer ids are authored in the asset, so the collision layer is discovered
        // rather than hardcoded.
        Tina::AssetFormat::TileMapLayerId collisionLayer = 0;
        for (const Tina::Gameplay2D::Scene2DTileLayer& layer : runtime_.tileLayers(tileMapEntity_))
        {
            if (!layer.visible)
            {
                collisionLayer = layer.layerId;
                break;
            }
        }
        if (collisionLayer == 0)
        {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::NotFound,
                                       "the authored TileMap has no collision layer");
        }
        // Borrows the instance for its whole life, which is why Scene2DRuntime is
        // immovable.
        collision_.emplace(*map, collisionLayer);
        std::pmr::vector<Tina::Asset::TileMapSolidHit> hits{&memory_};
        auto found = collision_->querySolidAabb(
            Tina::Asset::TileMapSolidQuery{.minX = 0.0F, .minY = 0.0F, .maxX = 4.0F, .maxY = 1.0F},
            hits);
        if (!found)
        {
            return Tina::Core::failure(std::move(found.error()));
        }
        capture_->collisionCellsFound = *found;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status probeExtractBeforeCommit()
    {
        Tina::Render::RenderFramePacket packet{};
        if (auto status = packet.beginFrame(1); !status)
        {
            return status;
        }
        auto builder = Tina::Render::RenderSceneBuilder::Create(
            Tina::Render::RenderSceneCapacity{.spriteCapacity = 64}, memory_);
        if (!builder)
        {
            return Tina::Core::failure(std::move(builder.error()));
        }
        if (auto status = builder->beginFrame({}); !status)
        {
            return status;
        }
        auto writer = builder->writer();
        return runtime_.extract(*world_, writer, packet.resourceSink(), binding_.resolver());
    }

    void recordCrateHeight() noexcept
    {
        if (const Tina::Scene::WorldTransform* transform = world_->worldTransform(crateEntity_);
            transform != nullptr)
        {
            capture_->crateEndY = transform->position.y;
        }
    }

    u64 targetFrames_ = 0;
    std::filesystem::path catalogRoot_{};
    std::filesystem::path scenePath_{};
    SampleCapture* capture_ = nullptr;
    mutable std::pmr::unsynchronized_pool_resource memory_{};
    std::optional<Tina::Asset::AssetSystem> assets_{};
    // Mutable because extractRenderScene() is const on IGameState while both the
    // Scene extraction and the runtime need non-const access -- the same shape the
    // other 2D samples use.
    mutable std::optional<Tina::Scene::World> world_{};
    std::optional<Tina::Audio::AudioEngine> audio_{};
    std::optional<Tina::Physics2D::PhysicsWorld2D> physics_{};
    mutable Tina::Gameplay2D::Scene2DRuntime runtime_{};
    std::optional<Tina::Asset::TileMapGridCollision> collision_{};
    mutable SampleBindingResolver binding_{};
    Tina::Scene::EntityId tileMapEntity_{};
    Tina::Scene::EntityId fxEntity_{};
    Tina::Scene::EntityId navigationEntity_{};
    Tina::Scene::EntityId audioEntity_{};
    Tina::Scene::EntityId crateEntity_{};
};

class AuthoredSceneApplication final : public Tina::IGameApplication {
  public:
    AuthoredSceneApplication(u64 targetFrames, std::filesystem::path catalogRoot,
                             std::filesystem::path scenePath, SampleCapture& capture) noexcept
        : targetFrames_(targetFrames), catalogRoot_(std::move(catalogRoot)),
          scenePath_(std::move(scenePath)), capture_(&capture)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>>
    createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{
            std::make_unique<AuthoredSceneState>(targetFrames_, catalogRoot_, scenePath_, *capture_)};
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
        ++capture_->applicationShutdowns;
    }

  private:
    u64 targetFrames_ = 0;
    std::filesystem::path catalogRoot_{};
    std::filesystem::path scenePath_{};
    SampleCapture* capture_ = nullptr;
};

[[nodiscard]] Tina::EngineCompositionFactories makeFactories(SampleCapture& capture)
{
    return Tina::EngineCompositionFactories{
        .createMonotonicClock = []() -> Tina::Core::Result<std::unique_ptr<Tina::Core::IMonotonicClock>> {
            return std::unique_ptr<Tina::Core::IMonotonicClock>{
                std::make_unique<Tina::Core::SteadyMonotonicClock>()};
        },
        .createTaskSystem = Tina::Task::createDisabledTaskSystem,
        .platformRender =
            Tina::IndependentPlatformRenderFactories{
                .createPlatformBackend = Tina::Platform::createHeadlessPlatformBackend,
                .createRenderDevice =
                    [&capture](const Tina::Render::RenderDeviceCreateParams&)
                        -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
                        return std::unique_ptr<Tina::Render::IRenderDevice>{
                            std::make_unique<RecordingNullRenderDevice>(capture)};
                    },
            },
    };
}

[[nodiscard]] Tina::Core::Result<u64> parseFrameCount(int argumentCount, char** arguments)
{
    constexpr std::string_view prefix = "--frames=";
    if (argumentCount == 1)
    {
        return DefaultFrameCount;
    }
    if (argumentCount != 2 || !std::string_view{arguments[1]}.starts_with(prefix))
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "expected at most one --frames=N argument");
    }
    const std::string_view text = std::string_view{arguments[1]}.substr(prefix.size());
    u64 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "--frames must be an unsigned integer greater than zero");
    }
    return value;
}

void printError(const Tina::Core::Error& error)
{
    Tina::Core::JsonWriter writer(std::cerr);
    writer.beginObject();
    writer.member("status", "error");
    writer.member("sample", "tina_sample_2d_authored_scene");
    writer.member("code", error.code.value);
    writer.member("message", error.message);
    writer.endObject();
    std::cerr << '\n';
}

// Every field the gate reads, emitted identically on success and failure so a failing
// run is diagnosable from the same output shape.
void printEvidence(std::ostream& out, const char* status, const SampleCapture& capture, u64 frames)
{
    {
        Tina::Core::JsonWriter writer(out);
        writer.beginObject();
        writer.member("status", status);
        writer.member("sample", "tina_sample_2d_authored_scene");
        writer.member("evidenceSchema", EvidenceSchema);
        writer.member("frames", frames);
        writer.member("submittedFrames", capture.submittedFrames);
        writer.member("presentedFrames", capture.presentedFrames);
        writer.member("sceneLoadedFromFile", capture.sceneLoaded);
        writer.member("authoredEntities", capture.authoredEntityCount);
        writer.member("resolvedNodesByAuthoredName", capture.foundNodesByAuthoredName);
        writer.member("tileMapNodes", capture.tileMapCount);
        writer.member("tileLayers", capture.tileLayerCount);
        writer.member("fxNodes", capture.fxCount);
        writer.member("navigationNodes", capture.navigationCount);
        writer.member("audioNodes", capture.audioCount);
        writer.member("unresolvedNodes", capture.unresolvedCount);
        writer.member("residentTileChunks", capture.residentTileChunks);
        writer.member("demandUpdates", capture.demandUpdates);
        writer.member("commits", capture.commits);
        writer.member("extracts", capture.extracts);
        writer.member("physicsSteps", capture.physicsSteps);
        writer.member("extractBeforeCommitRejected", capture.extractBeforeCommitRejected);
        writer.member("tileMapReachable", capture.tileMapReachable);
        writer.member("collisionCellsFound", capture.collisionCellsFound);
        writer.member("navigationGridReachable", capture.navigationGridReachable);
        writer.member("fxOriginX", capture.fxOriginX);
        writer.member("fxOriginY", capture.fxOriginY);
        writer.member("crateStartY", capture.crateStartY);
        writer.member("crateEndY", capture.crateEndY);
        writer.member("audioVoiceStarted", capture.audioVoiceStarted);
        writer.member("visibleTileSprites", capture.lastSpriteCount);
        writer.member("lastFrameHadCamera", capture.lastFrameHadCamera);
        writer.member("runtimeShutdownOk", capture.runtimeShutdownOk);
        writer.member("stateExits", capture.stateExits);
        writer.member("applicationShutdowns", capture.applicationShutdowns);
        writer.member("renderShutdowns", capture.renderShutdowns);
        writer.endObject();
    }
    out << '\n';
}

} // namespace

int main(int argumentCount, char** arguments)
{
    auto frameCountResult = parseFrameCount(argumentCount, arguments);
    if (!frameCountResult)
    {
        printError(frameCountResult.error());
        return 2;
    }
    const u64 frameCount = *frameCountResult;

    std::error_code cleanup;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "tina_sample_2d_authored_scene";
    std::filesystem::remove_all(root, cleanup);
    const std::filesystem::path catalogRoot = root / "catalog";
    const std::filesystem::path scenePath = root / "level.tworld";
    std::filesystem::create_directories(catalogRoot, cleanup);
    if (cleanup)
    {
        {
            Tina::Core::JsonWriter writer(std::cerr);
            writer.beginObject();
            writer.member("status", "error");
            writer.member("sample", "tina_sample_2d_authored_scene");
            writer.member("message", "could not create the sample working directory");
            writer.endObject();
        }
        std::cerr << '\n';
        return 1;
    }
    if (auto status = writeSceneFile(scenePath); !status)
    {
        printError(status.error());
        return 1;
    }

    SampleCapture capture;
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.renderSceneCapacities.spriteCapacity = 256;
    auto hostResult = Tina::EngineHost::Create(config, makeFactories(capture));
    if (!hostResult)
    {
        printError(hostResult.error());
        return 1;
    }

    AuthoredSceneApplication application{frameCount, catalogRoot, scenePath, capture};
    auto runResult = (*hostResult)->run(application);
    hostResult->reset();
    std::filesystem::remove_all(root, cleanup);

    const bool ok =
        runResult && *runResult == Tina::RunExitReason::GameRequestedExitAfterCurrentFrame &&
        capture.submittedFrames == frameCount && capture.presentedFrames == frameCount &&
        capture.lastFrameHadCamera &&
        // The scene came from a file, and its nodes were found by authored name.
        capture.sceneLoaded && capture.authoredEntityCount == ExpectedAuthoredEntities &&
        capture.foundNodesByAuthoredName &&
        // One node of each kind was instantiated, and nothing failed to resolve.
        capture.tileMapCount == 1 && capture.fxCount == 1 && capture.navigationCount == 1 &&
        capture.audioCount == 1 && capture.unresolvedCount == 0 &&
        // Both tile layers are driven, and both stream.
        capture.tileLayerCount == ExpectedTileLayers &&
        capture.residentTileChunks == ExpectedTileChunks &&
        // The per-frame sequence ran every frame, and ordering is enforced.
        capture.demandUpdates == frameCount && capture.commits == frameCount &&
        capture.extracts == frameCount && capture.physicsSteps == frameCount &&
        capture.extractBeforeCommitRejected &&
        // The resident map is reachable, and the invisible layer really carries the
        // collision cells a game would query.
        capture.tileMapReachable && capture.collisionCellsFound == ExpectedCollisionCells &&
        capture.navigationGridReachable &&
        // The authored transform placed the emitter rather than the payload origin.
        capture.fxOriginX == 2.0F && capture.fxOriginY == 1.0F &&
        // Physics is authoritative and written back: the crate fell from its authored
        // height and settled on the authored floor rather than falling forever.
        capture.crateStartY == CrateStartY && capture.crateEndY < CrateStartY &&
        capture.crateEndY > ExpectedCrateRestY - 0.1F &&
        capture.crateEndY < ExpectedCrateRestY + 0.1F &&
        // Only the visible tile layer emitted, so the collision mask was not drawn
        // over the map. Particles come from the same extract call.
        capture.lastSpriteCount == ExpectedFrameSprites &&
        capture.minSpriteCount == ExpectedFrameSprites &&
        capture.maxSpriteCount == ExpectedFrameSprites &&
        // The clip lease outlived playback and shutdown unwound cleanly.
        capture.audioVoiceStarted && capture.runtimeShutdownOk && capture.stateExits == 1 &&
        capture.applicationShutdowns == 1 && capture.renderShutdowns == 1;

    if (!ok)
    {
        if (!runResult)
        {
            printError(runResult.error());
        }
        printEvidence(std::cerr, "error", capture, frameCount);
        return 1;
    }
    printEvidence(std::cout, "ok", capture, frameCount);
    return 0;
}
