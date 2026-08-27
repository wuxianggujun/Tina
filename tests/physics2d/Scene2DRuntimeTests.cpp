#include <tina/gameplay2d/Scene2DRuntime.hpp>

#include <tina/asset/CatalogCook.hpp>
#include <tina/audio/AudioErrors.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include <tina/scene/SceneErrors.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory_resource>
#include <string>
#include <system_error>
#include <utility>

namespace Tina::Gameplay2D {
namespace {

// Ids match the recipe below. Kept apart from the wrong-kind probe so a mismatch
// test cannot accidentally reference a real asset of the right kind.
constexpr Core::u8 TextureSeed = 0x11;
constexpr Core::u8 SpriteSeed = 0x12;
constexpr Core::u8 Fx2DSeed = 0x13;
constexpr Core::u8 NavigationSeed = 0x14;
constexpr Core::u8 AudioSeed = 0x15;
constexpr Core::u8 TilesetSeed = 0x16;
constexpr Core::u8 TileMapSeed = 0x17;

// Layer 10 is authored visible, layer 20 invisible. Both must stream -- a game
// queries the invisible one for collision -- but only the visible one may emit.
constexpr AssetFormat::TileMapLayerId VisualLayerId = 10;
constexpr AssetFormat::TileMapLayerId CollisionLayerId = 20;

[[nodiscard]] Core::AssetId assetId(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xA5U);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] std::string idText(Core::u8 seed)
{
    const auto text = assetId(seed).canonicalText();
    return std::string{text.data(), text.size()};
}

// One Fx2D, one NavigationGrid2D and one AudioClip, which is enough to prove the
// build/lease/shutdown contract for three of the four kinds. TileMap needs a
// chunked multi-line fixture and is covered by its own suites; what matters here
// is that the runtime enforces ordering, which the commitReady guard asserts.
[[nodiscard]] std::string recipeText()
{
    std::string recipe;
    recipe += "texture2d " + idText(TextureSeed) + " 2 1 F08C28FF 28C8DCFF\n";
    recipe += "sprite " + idText(SpriteSeed) + " " + idText(TextureSeed) +
              " 0 0 0.5 1 0.5 0.5 1\n";
    recipe += "navigation2d " + idText(NavigationSeed) +
              " 2 2 0 0 1 0:1 0:1 1:1 0:1\n";
    recipe += "audioclip " + idText(AudioSeed) + " 48000 1 480 sine 880\n";
    // A real chunked TileMap: 4x2 cells at 1 m, so the cooker emits one 16-cell
    // chunk per layer. Without this fixture the TileMap path has no coverage at all.
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
    recipe += "fx2d " + idText(Fx2DSeed) + " " + idText(SpriteSeed) +
              " 12 10 1414090305 4294967296 4.0 2.0 -0.55 -0.35 0.55 0.35 -0.02 -0.01"
              " 0.02 0.02 10 10 0.20 0.20 0.12 0.12 3875527770 2030013520 0.0 2 11 8 10"
              " 0.18 0.04 8589934592 0 0 1 1 3535070790 1 8\n";
    return recipe;
}

// Minimal stand-in for the Sprite/Tileset registry a product owns. Extraction only
// needs a live FrameResourceRef; what device binding it names is the product's
// business, not the runtime's.
struct TestBindingResolver final {
    [[nodiscard]] Asset::AssetFrameResourceResolver resolver() noexcept
    {
        return Asset::AssetFrameResourceResolver{.userData = this, .resolve = &resolve};
    }

    [[nodiscard]] static Core::Result<Render::FrameResourceRef>
    resolve(void* userData, Asset::AssetHandle asset, Render::FrameResourceSink& sink) noexcept
    {
        auto& self = *static_cast<TestBindingResolver*>(userData);
        ++self.resolveCalls;
        Render::FramePin pin{Render::FramePinKind::Custom, self.bindingKey, nullptr, nullptr};
        static_cast<void>(asset);
        return sink.intern(
            Render::FrameResourceDescriptor{
                .kind = Render::FrameResourceKind::Texture2D,
                .deviceBindingKey = self.bindingKey,
            },
            std::move(pin));
    }

    Core::u32 bindingKey = 71U;
    Core::usize resolveCalls = 0;
};

class Scene2DRuntimeTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        catalogRoot_ = std::filesystem::temp_directory_path() / "tina_scene2d_runtime_test";
        std::error_code cleanup;
        std::filesystem::remove_all(catalogRoot_, cleanup);

        auto request = Asset::parseCatalogCookRecipe(recipeText(), catalogRoot_.string());
        ASSERT_TRUE(request.has_value()) << request.error().message;
        ASSERT_TRUE(Asset::cookAndPublishCatalogPackage(catalogRoot_.string(), *request))
            << "failed to publish the test catalog";

        auto system = Asset::AssetSystem::Create(Asset::AssetSystemConfig{
            .storeCapacity = 16,
            .memoryResource = &memory_,
            .batch =
                Asset::CookedAssetBatchLoadConfig{
                    .file = Asset::CookedAssetFileLoadConfig{.memoryResource = &memory_},
                    .memoryResource = &memory_,
                },
            .queueCapacity = 16,
            .defaultPumpBudget = 8,
        });
        ASSERT_TRUE(system.has_value()) << system.error().message;
        assets_.emplace(std::move(*system));
        ASSERT_TRUE(assets_->openAndBindCatalog(catalogRoot_.string()))
            << "failed to bind the test catalog";
        // Everything the runtime resolves must already be resident: it acquires
        // leases from handles, it does not trigger loads.
        // Chunks are deliberately absent: they are deferred dependencies the stream
        // requests itself, which is the behaviour the TileMap test exercises.
        for (const Core::u8 seed :
             {TextureSeed, SpriteSeed, Fx2DSeed, NavigationSeed, AudioSeed, TilesetSeed, TileMapSeed})
        {
            auto loaded = assets_->loadOne(assetId(seed));
            ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
        }
    }

    void TearDown() override
    {
        assets_.reset();
        std::error_code cleanup;
        std::filesystem::remove_all(catalogRoot_, cleanup);
    }

    [[nodiscard]] Scene::World makeWorld(Core::usize capacity = 16)
    {
        auto world = Scene::World::Create(Scene::WorldConfig{.entityCapacity = capacity});
        EXPECT_TRUE(world) << (world ? "" : world.error().message);
        return std::move(*world);
    }

    [[nodiscard]] Scene::EntityId addResourceNode(Scene::World& world, Core::u8 seed,
                                                 Scene::ResourceBindingKind2D kind,
                                                 bool active = true)
    {
        const Scene::EntityId entity = world.createEntity().value();
        EXPECT_TRUE(world.setResourceBinding2D(entity, Scene::ResourceBinding2D{
                                                          .assetId = assetId(seed),
                                                          .kind = kind,
                                                          .active = active,
                                                      }));
        return entity;
    }

    // Places the node so a transform-sensitive assertion can tell the authored
    // position apart from the payload's own origin.
    void setWorldPosition(Scene::World& world, Scene::EntityId entity, float x, float y)
    {
        Scene::LocalTransform transform{};
        transform.position = {x, y, 0.0F};
        EXPECT_TRUE(world.setLocalTransform(entity, transform));
    }

    [[nodiscard]] Audio::AudioEngine makeAudioEngine()
    {
        auto engine = Audio::AudioEngine::Create(Audio::AudioEngineConfig{}, memory_);
        EXPECT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);
        return std::move(*engine);
    }

    std::filesystem::path catalogRoot_{};
    std::pmr::unsynchronized_pool_resource memory_{};
    std::optional<Asset::AssetSystem> assets_{};
    TestBindingResolver binding_{};
};

TEST_F(Scene2DRuntimeTest, InstantiatesEachResourceKindAndReleasesOnShutdown)
{
    Scene::World world = makeWorld();
    const Scene::EntityId fx = addResourceNode(world, Fx2DSeed, Scene::ResourceBindingKind2D::FxEmitter);
    const Scene::EntityId nav =
        addResourceNode(world, NavigationSeed, Scene::ResourceBindingKind2D::NavigationRegion);
    const Scene::EntityId audio =
        addResourceNode(world, AudioSeed, Scene::ResourceBindingKind2D::AudioPlayer);
    ASSERT_TRUE(world.updateWorldTransforms());

    Scene2DRuntime runtime;
    // No AudioEngine composed, so audio nodes count as unresolved rather than
    // failing the whole scene.
    ASSERT_TRUE(runtime.build(world, *assets_, nullptr));
    EXPECT_EQ(runtime.stats().fxCount, 1U);
    EXPECT_EQ(runtime.stats().navigationCount, 1U);
    EXPECT_EQ(runtime.stats().audioCount, 0U);
    EXPECT_EQ(runtime.stats().unresolvedCount, 1U);

    // The authored grid is reachable so a game can path against it without
    // rebuilding it from the asset.
    EXPECT_NE(runtime.navigationGrid(nav), nullptr);
    EXPECT_EQ(runtime.navigationGrid(fx), nullptr);

    // Without an engine, playback is refused rather than silently doing nothing.
    auto voice = runtime.playAudio(audio);
    ASSERT_FALSE(voice);
    EXPECT_EQ(voice.error().code, Core::CoreErrorCode::Unsupported);

    ASSERT_TRUE(runtime.shutdown());
    EXPECT_EQ(runtime.stats().fxCount, 0U);
    EXPECT_EQ(runtime.navigationGrid(nav), nullptr);
    // Idempotent, so a product may call it on an unwound path.
    ASSERT_TRUE(runtime.shutdown());
}

// Rebuilding without shutdown would leak every lease the first build acquired.
TEST_F(Scene2DRuntimeTest, RebuildWithoutShutdownIsRefused)
{
    Scene::World world = makeWorld();
    static_cast<void>(addResourceNode(world, Fx2DSeed, Scene::ResourceBindingKind2D::FxEmitter));
    ASSERT_TRUE(world.updateWorldTransforms());

    Scene2DRuntime runtime;
    ASSERT_TRUE(runtime.build(world, *assets_, nullptr));
    auto again = runtime.build(world, *assets_, nullptr);
    ASSERT_FALSE(again);
    EXPECT_EQ(again.error().code, Core::CoreErrorCode::AlreadyExists);
    ASSERT_TRUE(runtime.shutdown());
}

// A stale or wrong-kind reference is the common case in a scene edited over time.
// It must not make the scene unloadable.
TEST_F(Scene2DRuntimeTest, WrongKindAndMissingAssetsCountAsUnresolved)
{
    Scene::World world = makeWorld();
    // A real asset, bound to a node that declares a different kind.
    static_cast<void>(addResourceNode(world, SpriteSeed, Scene::ResourceBindingKind2D::FxEmitter));
    // An id that is not in the catalog at all.
    static_cast<void>(addResourceNode(world, 0x7EU, Scene::ResourceBindingKind2D::NavigationRegion));
    const Scene::EntityId good =
        addResourceNode(world, NavigationSeed, Scene::ResourceBindingKind2D::NavigationRegion);
    ASSERT_TRUE(world.updateWorldTransforms());

    Scene2DRuntime runtime;
    ASSERT_TRUE(runtime.build(world, *assets_, nullptr));
    EXPECT_EQ(runtime.stats().unresolvedCount, 2U);
    EXPECT_EQ(runtime.stats().navigationCount, 1U);
    EXPECT_NE(runtime.navigationGrid(good), nullptr);
    ASSERT_TRUE(runtime.shutdown());
}

// active=false keeps the leases so toggling back is a bool flip, not a reload,
// but must not simulate.
TEST_F(Scene2DRuntimeTest, InactiveNodesInstantiateButDoNotSimulate)
{
    Scene::World world = makeWorld();
    static_cast<void>(
        addResourceNode(world, Fx2DSeed, Scene::ResourceBindingKind2D::FxEmitter, false));
    ASSERT_TRUE(world.updateWorldTransforms());

    Scene2DRuntime runtime;
    ASSERT_TRUE(runtime.build(world, *assets_, nullptr));
    EXPECT_EQ(runtime.stats().fxCount, 1U);
    // fixedUpdate must skip it rather than fail.
    ASSERT_TRUE(runtime.fixedUpdate(Core::Duration{1.0 / 60.0}));
    ASSERT_TRUE(runtime.shutdown());
}

// The authored node transform has to place the emitter, or dragging an
// FxEmitter2D in the Editor changes nothing at runtime.
TEST_F(Scene2DRuntimeTest, FxParticlesSpawnAtTheAuthoredNodeTransform)
{
    Scene::World world = makeWorld();
    const Scene::EntityId origin =
        addResourceNode(world, Fx2DSeed, Scene::ResourceBindingKind2D::FxEmitter);
    const Scene::EntityId moved =
        addResourceNode(world, Fx2DSeed, Scene::ResourceBindingKind2D::FxEmitter);
    setWorldPosition(world, moved, 40.0F, -25.0F);
    ASSERT_TRUE(world.updateWorldTransforms());

    Scene2DRuntime runtime;
    ASSERT_TRUE(runtime.build(world, *assets_, nullptr));
    EXPECT_EQ(runtime.fxOrigin(origin).x, 0.0F);
    EXPECT_EQ(runtime.fxOrigin(moved).x, 40.0F);
    EXPECT_EQ(runtime.fxOrigin(moved).y, -25.0F);

    Scene::Fx2DInstance* atOrigin = runtime.fxInstance(origin);
    Scene::Fx2DInstance* atMoved = runtime.fxInstance(moved);
    ASSERT_NE(atOrigin, nullptr);
    ASSERT_NE(atMoved, nullptr);
    const auto first = atOrigin->particles.particles();
    const auto second = atMoved->particles.particles();
    ASSERT_FALSE(first.empty());
    ASSERT_EQ(first.size(), second.size());
    // Both emitters share a seed, so the only difference must be the node offset.
    for (Core::usize index = 0; index < first.size(); ++index)
    {
        EXPECT_FLOAT_EQ(second[index].position.x, first[index].position.x + 40.0F);
        EXPECT_FLOAT_EQ(second[index].position.y, first[index].position.y - 25.0F);
    }
    ASSERT_TRUE(runtime.shutdown());
}

// active==false must make a node unreachable, not merely unsimulated: a game that
// could still path against an authored-off region would be blocked by geometry the
// author switched off.
TEST_F(Scene2DRuntimeTest, InactiveNavigationAndAudioNodesAreNotReachable)
{
    Scene::World world = makeWorld();
    const Scene::EntityId nav = addResourceNode(
        world, NavigationSeed, Scene::ResourceBindingKind2D::NavigationRegion, false);
    const Scene::EntityId audio =
        addResourceNode(world, AudioSeed, Scene::ResourceBindingKind2D::AudioPlayer, false);
    const Scene::EntityId fx =
        addResourceNode(world, Fx2DSeed, Scene::ResourceBindingKind2D::FxEmitter, false);
    ASSERT_TRUE(world.updateWorldTransforms());

    Audio::AudioEngine engine = makeAudioEngine();
    Scene2DRuntime runtime;
    ASSERT_TRUE(runtime.build(world, *assets_, &engine));
    // Instantiated, so re-activating stays a bool flip rather than a load.
    EXPECT_EQ(runtime.stats().navigationCount, 1U);
    EXPECT_EQ(runtime.stats().audioCount, 1U);

    EXPECT_EQ(runtime.navigationGrid(nav), nullptr);
    EXPECT_EQ(runtime.fxInstance(fx), nullptr);
    auto voice = runtime.playAudio(audio);
    ASSERT_FALSE(voice);
    EXPECT_EQ(voice.error().code, Core::CoreErrorCode::InvalidArgument);

    ASSERT_TRUE(runtime.shutdown());
    engine.shutdown();
}

// AudioPcmClipView is non-owning, so a voice still playing when its clip lease is
// released reads freed memory. shutdown() must stop and pump first.
TEST_F(Scene2DRuntimeTest, ShutdownStopsVoicesBeforeReleasingClipLeases)
{
    Scene::World world = makeWorld();
    const Scene::EntityId audio =
        addResourceNode(world, AudioSeed, Scene::ResourceBindingKind2D::AudioPlayer);
    ASSERT_TRUE(world.updateWorldTransforms());

    Audio::AudioEngine engine = makeAudioEngine();
    Scene2DRuntime runtime;
    ASSERT_TRUE(runtime.build(world, *assets_, &engine));
    EXPECT_EQ(runtime.stats().audioCount, 1U);

    auto voice = runtime.playAudio(audio);
    ASSERT_TRUE(voice) << voice.error().message;
    ASSERT_TRUE(engine.pumpCompletions());
    auto live = engine.isVoiceLive(*voice);
    ASSERT_TRUE(live);
    ASSERT_TRUE(*live) << "the voice must still be live for this test to prove anything";

    ASSERT_TRUE(runtime.shutdown());
    // Retired by shutdown, so nothing is reading the payload the lease just freed.
    auto afterShutdown = engine.isVoiceLive(*voice);
    ASSERT_TRUE(afterShutdown);
    EXPECT_FALSE(*afterShutdown);
    engine.shutdown();
}

// Long-running scenes must not accumulate finished one-shots until the tracking
// table fills up.
TEST_F(Scene2DRuntimeTest, FinishedVoicesAreReleasedAndCapacityIsEnforced)
{
    Scene::World world = makeWorld();
    const Scene::EntityId audio =
        addResourceNode(world, AudioSeed, Scene::ResourceBindingKind2D::AudioPlayer);
    ASSERT_TRUE(world.updateWorldTransforms());

    Audio::AudioEngine engine = makeAudioEngine();
    Scene2DRuntime runtime;
    ASSERT_TRUE(runtime.build(world, *assets_, &engine, {.audioVoiceCapacity = 1}));

    auto first = runtime.playAudio(audio);
    ASSERT_TRUE(first) << first.error().message;
    // A second voice cannot be tracked while the first is live, and failing closed
    // is better than starting playback shutdown could not stop.
    auto second = runtime.playAudio(audio);
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error().code, Scene::SceneErrorCode::CapacityExceeded);

    // Once the engine retires it, the slot is reclaimed.
    ASSERT_TRUE(engine.enqueueStop(*first));
    ASSERT_TRUE(engine.pumpCompletions());
    ASSERT_TRUE(runtime.releaseFinishedVoices());
    auto third = runtime.playAudio(audio);
    ASSERT_TRUE(third) << third.error().message;

    ASSERT_TRUE(runtime.shutdown());
    engine.shutdown();
}

// The whole point of this owner: a TileMap node has to survive the real
// updateDemand -> pump -> commitReady -> extract sequence. Before this test the path
// returned an error on the very first frame and nothing noticed.
TEST_F(Scene2DRuntimeTest, TileMapStreamsEveryLayerAndEmitsOnlyVisibleOnes)
{
    Scene::World world = makeWorld();
    const Scene::EntityId map =
        addResourceNode(world, TileMapSeed, Scene::ResourceBindingKind2D::TileMap);
    ASSERT_TRUE(world.updateWorldTransforms());

    Scene2DRuntime runtime;
    ASSERT_TRUE(runtime.build(world, *assets_, nullptr));
    EXPECT_EQ(runtime.stats().tileMapCount, 1U);
    // Both layers are driven; the map has no field to select one.
    EXPECT_EQ(runtime.stats().tileLayerCount, 2U);

    const Asset::TileChunkCameraQuery camera{
        .centerX = 2.0F, .centerY = 1.0F, .halfWidth = 4.0F, .halfHeight = 3.0F};

    // extract before commitReady would draw a stale or partial map, and that is
    // invisible on screen, so it must be reported.
    Render::RenderFramePacket earlyFrame{};
    ASSERT_TRUE(earlyFrame.beginFrame(1));
    auto earlyBuilder = Render::RenderSceneBuilder::Create(
        Render::RenderSceneCapacity{.spriteCapacity = 64}, memory_);
    ASSERT_TRUE(earlyBuilder.has_value());
    ASSERT_TRUE(earlyBuilder->beginFrame({}));
    auto earlyWriter = earlyBuilder->writer();
    auto tooEarly = runtime.extract(world, earlyWriter, earlyFrame.resourceSink(),
                                    binding_.resolver());
    ASSERT_FALSE(tooEarly);
    EXPECT_EQ(tooEarly.error().code, Core::CoreErrorCode::InvalidArgument);

    ASSERT_TRUE(runtime.updateDemand(camera));
    // The runtime deliberately does not pump: AssetSystem serves the whole game.
    ASSERT_TRUE(assets_->pump(16));
    ASSERT_TRUE(runtime.commitReady());
    // One chunk per layer at this map size, so residency proves both streamed.
    EXPECT_EQ(runtime.stats().residentTileChunks, 2U);

    Render::RenderFramePacket frame{};
    ASSERT_TRUE(frame.beginFrame(2));
    auto builder = Render::RenderSceneBuilder::Create(
        Render::RenderSceneCapacity{.spriteCapacity = 64}, memory_);
    ASSERT_TRUE(builder.has_value());
    ASSERT_TRUE(builder->beginFrame({}));
    auto writer = builder->writer();
    ASSERT_TRUE(writer.setCamera2D(Render::RenderCamera2DInput{
        .stableCameraKey = 1,
        .centerX = 2.0F,
        .centerY = 1.0F,
        .worldWidth = 8.0F,
        .worldHeight = 6.0F,
        .actualPixelsPerMeter = 16.0F,
    }));
    ASSERT_TRUE(runtime.extract(world, writer, frame.resourceSink(), binding_.resolver()));

    // Visual layer authors 6 non-empty cells, collision authors 4. Only the visible
    // one may emit, or the collision mask would be drawn over the map.
    EXPECT_EQ(runtime.stats().tileSpritesEmitted, 6U);
    auto view = builder->commit();
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->sprites2D().size(), 6U);

    ASSERT_TRUE(runtime.shutdown());
}

// A node authored inactive must not stream: its chunks would occupy residency that
// active maps need.
TEST_F(Scene2DRuntimeTest, InactiveTileMapDoesNotStreamOrEmit)
{
    Scene::World world = makeWorld();
    static_cast<void>(
        addResourceNode(world, TileMapSeed, Scene::ResourceBindingKind2D::TileMap, false));
    ASSERT_TRUE(world.updateWorldTransforms());

    Scene2DRuntime runtime;
    ASSERT_TRUE(runtime.build(world, *assets_, nullptr));
    // Instantiated with its leases, so re-activating stays a bool flip.
    EXPECT_EQ(runtime.stats().tileMapCount, 1U);
    EXPECT_EQ(runtime.stats().tileLayerCount, 2U);

    ASSERT_TRUE(runtime.updateDemand(Asset::TileChunkCameraQuery{
        .centerX = 2.0F, .centerY = 1.0F, .halfWidth = 4.0F, .halfHeight = 3.0F}));
    ASSERT_TRUE(assets_->pump(16));
    ASSERT_TRUE(runtime.commitReady());
    EXPECT_EQ(runtime.stats().residentTileChunks, 0U);
    EXPECT_EQ(runtime.stats().tileSpritesEmitted, 0U);
    ASSERT_TRUE(runtime.shutdown());
}

TEST_F(Scene2DRuntimeTest, CapacityFailureLeavesNothingBehind)
{
    Scene::World world = makeWorld();
    static_cast<void>(addResourceNode(world, Fx2DSeed, Scene::ResourceBindingKind2D::FxEmitter));
    static_cast<void>(addResourceNode(world, Fx2DSeed, Scene::ResourceBindingKind2D::FxEmitter));
    ASSERT_TRUE(world.updateWorldTransforms());

    Scene2DRuntime runtime;
    auto overCapacity = runtime.build(world, *assets_, nullptr, {.fxCapacity = 1});
    ASSERT_FALSE(overCapacity);
    EXPECT_EQ(overCapacity.error().code, Scene::SceneErrorCode::CapacityExceeded);
    // A failed build must not leave a partially wired scene holding leases.
    EXPECT_EQ(runtime.stats().fxCount, 0U);
    // The runtime is reusable afterwards, which proves the rollback fully unwound.
    ASSERT_TRUE(runtime.build(world, *assets_, nullptr));
    EXPECT_EQ(runtime.stats().fxCount, 2U);
    ASSERT_TRUE(runtime.shutdown());
}

} // namespace
} // namespace Tina::Gameplay2D
