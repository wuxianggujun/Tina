#include <tina/gameplay2d/Scene2DRuntime.hpp>

#include <tina/asset/CatalogCook.hpp>
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
    recipe += "fx2d " + idText(Fx2DSeed) + " " + idText(SpriteSeed) +
              " 12 10 1414090305 4294967296 4.0 2.0 -0.55 -0.35 0.55 0.35 -0.02 -0.01"
              " 0.02 0.02 10 10 0.20 0.20 0.12 0.12 3875527770 2030013520 0.0 2 11 8 10"
              " 0.18 0.04 8589934592 0 0 1 1 3535070790 1 8\n";
    return recipe;
}

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
        for (const Core::u8 seed : {TextureSeed, SpriteSeed, Fx2DSeed, NavigationSeed, AudioSeed})
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

    std::filesystem::path catalogRoot_{};
    std::pmr::unsynchronized_pool_resource memory_{};
    std::optional<Asset::AssetSystem> assets_{};
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
