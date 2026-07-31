#include <tina/asset/AssetStore.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include <tina/scene/ExtractRenderScene.hpp>
#include <tina/scene/PrefabInstantiate.hpp>
#include <tina/scene/World.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <memory_resource>
#include <numbers>
#include <optional>
#include <utility>

namespace Tina::Scene {
namespace {

[[nodiscard]] World makeWorld(usize capacity = 32)
{
    auto world = World::Create(WorldConfig{capacity});
    EXPECT_TRUE(world.has_value()) << world.error().message;
    return std::move(*world);
}

[[nodiscard]] LocalTransform translated(float x, float y, float z = 0.0F)
{
    LocalTransform transform;
    transform.position = {x, y, z};
    return transform;
}

[[nodiscard]] Quaternion rotationAroundZ(float radians) noexcept
{
    const float halfAngle = radians * 0.5F;
    return {0.0F, 0.0F, std::sin(halfAngle), std::cos(halfAngle)};
}

[[nodiscard]] Camera2D fixedCamera(float heightMeters = 18.0F)
{
    return Camera2D{
        .projection = Render::FixedWorldHeight2D{.heightMeters = heightMeters},
        .pixelSnap = Render::RenderPixelSnapPolicy::Disabled,
        .active = true,
    };
}

[[nodiscard]] Core::AssetId fixtureAssetId(u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] SpriteRenderer2D fixtureSprite(
    Asset::AssetHandle sprite,
    float width = 1.0F,
    float height = 1.0F)
{
    return SpriteRenderer2D{
        .sprite = sprite,
        .overrides = SpriteOverrideFlags::Size,
        .sizeOverrideMeters = {width, height},
        .visible = true,
    };
}

struct TestFrameResourceLifetime final {
    u32 borrows = 0;
};

[[nodiscard]] TestFrameResourceLifetime& testFrameResourceLifetime() noexcept
{
    static TestFrameResourceLifetime lifetime{};
    return lifetime;
}

[[nodiscard]] Render::RenderFramePacket& testFramePacket() noexcept
{
    (void)testFrameResourceLifetime();
    static Render::RenderFramePacket packet{};
    return packet;
}

[[nodiscard]] Render::FrameResourceSink& beginTestFrameResources()
{
    static u64 frameIndex = 0;
    const Core::Status status = testFramePacket().beginFrame(frameIndex++);
    EXPECT_TRUE(status) << (status ? "" : status.error().message);
    return testFramePacket().resourceSink();
}

[[nodiscard]] Core::Result<Render::FrameResourceRef> internTestResource(
    Render::FrameResourceSink& sink,
    Render::FrameResourceKind kind,
    u32 bindingKey) noexcept
{
    TestFrameResourceLifetime& lifetime = testFrameResourceLifetime();
    ++lifetime.borrows;
    Render::FramePin pin{
        Render::FramePinKind::Custom,
        bindingKey,
        &lifetime,
        [](void* userData) noexcept {
            auto& pinned = *static_cast<TestFrameResourceLifetime*>(userData);
            if (pinned.borrows > 0)
            {
                --pinned.borrows;
            }
        },
    };
    return sink.intern(
        Render::FrameResourceDescriptor{
            .kind = kind,
            .deviceBindingKey = bindingKey,
        },
        std::move(pin));
}

[[nodiscard]] Core::Result<Render::FrameResourceRef> internTestTexture(
    Render::FrameResourceSink& sink,
    u32 bindingKey) noexcept
{
    return internTestResource(sink, Render::FrameResourceKind::Sprite2DTexture, bindingKey);
}

[[nodiscard]] u64 frameResourceBindingKey(
    Render::FrameResourceRef resource,
    Render::FrameResourceKind kind) noexcept
{
    const Render::FrameResourceDescriptor* descriptor =
        testFramePacket().resourceTableView().resolve(resource, kind);
    return descriptor == nullptr ? 0 : descriptor->deviceBindingKey;
}

[[nodiscard]] u64 textureBindingKey(Render::FrameResourceRef texture) noexcept
{
    const Render::FrameResourceDescriptor* descriptor = testFramePacket().resourceTableView().resolve(
        texture, Render::FrameResourceKind::Sprite2DTexture);
    return descriptor == nullptr ? 0 : descriptor->deviceBindingKey;
}

[[nodiscard]] Core::Status extractRenderSceneFromWorld(
    World& world,
    Render::RenderSceneWriter& writer,
    ExtractRenderSceneParams params = {}) noexcept
{
    return ::Tina::Scene::extractRenderSceneFromWorld(
        world, writer, beginTestFrameResources(), params);
}

struct TestSpriteBindings final {
    struct Binding final {
        Asset::AssetHandle sprite{};
        u32 key = 0;
    };

    [[nodiscard]] Asset::AssetFrameResourceResolver resolver() noexcept
    {
        return Asset::AssetFrameResourceResolver{.userData = this, .resolve = &resolve};
    }

    void bind(Asset::AssetHandle sprite, u32 key) noexcept
    {
        bindings[bindingCount++] = Binding{.sprite = sprite, .key = key};
    }

    [[nodiscard]] static Core::Result<Render::FrameResourceRef> resolve(
        void* userData,
        Asset::AssetHandle sprite,
        Render::FrameResourceSink& frameResources) noexcept
    {
        auto& self = *static_cast<TestSpriteBindings*>(userData);
        ++self.resolveCalls;
        if (self.store == nullptr || !sprite || self.store->assetKind(sprite) != AssetFormat::AssetKind::Sprite ||
            self.store->state(sprite) == Asset::AssetLogicalState::Unloaded)
        {
            return Render::FrameResourceRef{};
        }
        for (usize index = 0; index < self.bindingCount; ++index)
        {
            if (self.bindings[index].sprite == sprite)
            {
                return internTestTexture(frameResources, self.bindings[index].key);
            }
        }
        return Render::FrameResourceRef{};
    }

    Asset::AssetStore* store = nullptr;
    std::array<Binding, 4> bindings{};
    usize bindingCount = 0;
    u32 resolveCalls = 0;
};

class SceneSpriteAssetTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto store = Asset::AssetStore::Create({.capacity = 8, .memoryResource = &memory_});
        ASSERT_TRUE(store.has_value()) << (store ? "" : store.error().message);
        store_.emplace(std::move(*store));

        auto first = store_->beginQueued(fixtureAssetId(1), AssetFormat::AssetKind::Sprite);
        auto second = store_->beginQueued(fixtureAssetId(2), AssetFormat::AssetKind::Sprite);
        auto third = store_->beginQueued(fixtureAssetId(3), AssetFormat::AssetKind::Sprite);
        auto wrongKind = store_->beginQueued(fixtureAssetId(4), AssetFormat::AssetKind::Texture2D);
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        ASSERT_TRUE(third.has_value());
        ASSERT_TRUE(wrongKind.has_value());
        firstSprite_ = *first;
        secondSprite_ = *second;
        thirdSprite_ = *third;
        wrongKind_ = *wrongKind;
    }

    [[nodiscard]] Asset::AssetStore& store() noexcept
    {
        return *store_;
    }

    std::pmr::unsynchronized_pool_resource memory_{};
    std::optional<Asset::AssetStore> store_{};
    Asset::AssetHandle firstSprite_{};
    Asset::AssetHandle secondSprite_{};
    Asset::AssetHandle thirdSprite_{};
    Asset::AssetHandle wrongKind_{};
};

[[nodiscard]] Core::Status extractSingleSprite(
    Asset::AssetHandle sprite,
    Asset::AssetFrameResourceResolver resolver = {},
    bool visible = true)
{
    World world = makeWorld();
    auto entity = world.createEntity();
    if (!entity)
    {
        return Core::failure(std::move(entity.error()));
    }
    SpriteRenderer2D component = fixtureSprite(sprite);
    component.visible = visible;
    if (auto status = world.setSpriteRenderer2D(*entity, component); !status)
    {
        return status;
    }

    auto builder = Render::RenderSceneBuilder::Create();
    if (!builder)
    {
        return Core::failure(std::move(builder.error()));
    }
    if (auto status = builder->beginFrame(); !status)
    {
        return status;
    }
    Render::RenderSceneWriter writer = builder->writer();
    return extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{.spriteBindingResolver = resolver});
}

[[nodiscard]] Core::Result<Render::FrameResourceRef> resolveToEmpty(
    void*, Asset::AssetHandle, Render::FrameResourceSink&) noexcept
{
    return Render::FrameResourceRef{};
}

TEST_F(SceneSpriteAssetTest, SetsClearsAndQueriesCameraAndSprite)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();

    ASSERT_TRUE(world.setCamera2D(entity, fixedCamera()));
    ASSERT_NE(world.camera2D(entity), nullptr);
    EXPECT_TRUE(std::holds_alternative<Render::FixedWorldHeight2D>(
        world.camera2D(entity)->projection));

    ASSERT_TRUE(world.setSpriteRenderer2D(entity, fixtureSprite(firstSprite_)));
    ASSERT_NE(world.spriteRenderer2D(entity), nullptr);
    EXPECT_EQ(world.spriteRenderer2D(entity)->sprite, firstSprite_);

    ASSERT_TRUE(world.clearCamera2D(entity));
    EXPECT_EQ(world.camera2D(entity), nullptr);
    ASSERT_TRUE(world.clearSpriteRenderer2D(entity));
    EXPECT_EQ(world.spriteRenderer2D(entity), nullptr);
}

TEST_F(SceneSpriteAssetTest, RejectsInvalidRenderPropertiesButStoresWeakHandleState)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();

    Camera2D badCamera = fixedCamera();
    badCamera.normalizedViewport.width = 0.0F;
    EXPECT_EQ(
        world.setCamera2D(entity, badCamera).error().code,
        SceneErrorCode::InvalidComponent);

    ASSERT_TRUE(world.setSpriteRenderer2D(entity, fixtureSprite({})));
    ASSERT_NE(world.spriteRenderer2D(entity), nullptr);
    EXPECT_FALSE(world.spriteRenderer2D(entity)->sprite);

    SpriteRenderer2D badSize = fixtureSprite(firstSprite_, -1.0F, 1.0F);
    EXPECT_EQ(
        world.setSpriteRenderer2D(entity, badSize).error().code,
        SceneErrorCode::InvalidComponent);

    SpriteRenderer2D badUv = fixtureSprite(firstSprite_);
    badUv.overrides = SpriteOverrideFlags::Size | SpriteOverrideFlags::UvRect;
    badUv.uvRectOverride = {.u0 = 0.8F, .v0 = 0.0F, .u1 = 0.2F, .v1 = 1.0F};
    EXPECT_EQ(
        world.setSpriteRenderer2D(entity, badUv).error().code,
        SceneErrorCode::InvalidComponent);
}

TEST_F(SceneSpriteAssetTest, DestroyedEntityDropsComponents)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();
    ASSERT_TRUE(world.setCamera2D(entity, fixedCamera()));
    ASSERT_TRUE(world.setSpriteRenderer2D(entity, fixtureSprite(firstSprite_)));
    ASSERT_TRUE(world.destroyEntity(entity));

    EXPECT_EQ(world.camera2D(entity), nullptr);
    EXPECT_EQ(world.spriteRenderer2D(entity), nullptr);
}

TEST_F(SceneSpriteAssetTest, ExtractsSingleCameraAndSpritesIntoRenderScene)
{
    World world = makeWorld();
    const EntityId cameraEntity = world.createEntity(translated(3.0F, -1.5F)).value();
    ASSERT_TRUE(world.setCamera2D(cameraEntity, fixedCamera(9.0F)));

    const EntityId nearSprite = world.createEntity(translated(0.0F, 0.0F)).value();
    SpriteRenderer2D near = fixtureSprite(firstSprite_, 1.5F, 1.5F);
    near.color = {.red = 17, .green = 34, .blue = 51, .alpha = 68};
    near.sortingLayer = -2;
    near.orderInLayer = 9;
    ASSERT_TRUE(world.setSpriteRenderer2D(nearSprite, near));

    LocalTransform farLocal = translated(100.0F, 100.0F);
    const EntityId farSprite = world.createEntity(farLocal).value();
    SpriteRenderer2D far = fixtureSprite(secondSprite_, 1.0F, 1.0F);
    far.orderInLayer = 5;
    ASSERT_TRUE(world.setSpriteRenderer2D(farSprite, far));

    const EntityId hidden = world.createEntity(translated(1.0F, 1.0F)).value();
    SpriteRenderer2D invisible = fixtureSprite(thirdSprite_);
    invisible.visible = false;
    ASSERT_TRUE(world.setSpriteRenderer2D(hidden, invisible));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();

    TestSpriteBindings bindings{.store = &store()};
    bindings.bind(firstSprite_, 1);
    bindings.bind(secondSprite_, 2);
    const ExtractRenderSceneParams params{
        .surfaceViewport = {.pixelWidth = 1280, .pixelHeight = 720},
        .spriteBindingResolver = bindings.resolver(),
    };
    ASSERT_TRUE(extractRenderSceneFromWorld(world, writer, params));
    EXPECT_EQ(testFramePacket().resourceCount(), 2U);

    auto view = builder->commit();
    ASSERT_TRUE(view);
    ASSERT_TRUE(view->camera2D().has_value());
    EXPECT_FLOAT_EQ(view->camera2D()->centerX, 3.0F);
    EXPECT_FLOAT_EQ(view->camera2D()->centerY, -1.5F);
    EXPECT_FLOAT_EQ(view->camera2D()->worldHeight, 9.0F);
    EXPECT_NEAR(view->camera2D()->worldWidth, 9.0F * (1280.0F / 720.0F), 1.0e-4F);

    // Hidden sprite pruned; far sprite may be culled by camera frustum.
    ASSERT_GE(view->sprites2D().size(), 1U);
    bool foundNear = false;
    for (const Render::RenderSprite2DItem& item : view->sprites2D()) {
        if (textureBindingKey(item.texture) == 1U) {
            foundNear = true;
            EXPECT_FLOAT_EQ(item.centerX, 0.0F);
            EXPECT_FLOAT_EQ(item.centerY, 0.0F);
            EXPECT_FLOAT_EQ(item.widthMeters, 1.5F);
            EXPECT_FLOAT_EQ(item.heightMeters, 1.5F);
            EXPECT_EQ(item.red, 17U);
            EXPECT_EQ(item.green, 34U);
            EXPECT_EQ(item.blue, 51U);
            EXPECT_EQ(item.alpha, 68U);
            EXPECT_EQ(item.sortingLayer, -2);
            EXPECT_EQ(item.orderInLayer, 9);
        }
        EXPECT_NE(textureBindingKey(item.texture), 3U);
    }
    EXPECT_TRUE(foundNear);
    EXPECT_EQ(bindings.resolveCalls, 2U);
}

TEST_F(SceneSpriteAssetTest, MissingResolverRejectsVisibleSprite)
{
    const Core::Status status = extractSingleSprite(firstSprite_);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, SceneErrorCode::UnresolvedSprite);
}

TEST_F(SceneSpriteAssetTest, InvalidHandleRejectsBeforeCallingResolver)
{
    TestSpriteBindings bindings{.store = &store()};
    const Core::Status status = extractSingleSprite({}, bindings.resolver());
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, SceneErrorCode::UnresolvedSprite);
    EXPECT_EQ(bindings.resolveCalls, 0U);
}

TEST_F(SceneSpriteAssetTest, StaleHandleIsUnresolved)
{
    TestSpriteBindings bindings{.store = &store()};
    bindings.bind(firstSprite_, 7);
    ASSERT_TRUE(store().unload(firstSprite_));

    const Core::Status status = extractSingleSprite(firstSprite_, bindings.resolver());
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, SceneErrorCode::UnresolvedSprite);
    EXPECT_EQ(bindings.resolveCalls, 1U);
}

TEST_F(SceneSpriteAssetTest, CrossStoreHandleIsUnresolved)
{
    std::pmr::unsynchronized_pool_resource otherMemory;
    auto otherStore = Asset::AssetStore::Create({.capacity = 1, .memoryResource = &otherMemory});
    ASSERT_TRUE(otherStore.has_value());
    auto otherSprite = otherStore->beginQueued(fixtureAssetId(20), AssetFormat::AssetKind::Sprite);
    ASSERT_TRUE(otherSprite.has_value());

    TestSpriteBindings bindings{.store = &store()};
    bindings.bind(*otherSprite, 7);
    const Core::Status status = extractSingleSprite(*otherSprite, bindings.resolver());
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, SceneErrorCode::UnresolvedSprite);
    EXPECT_EQ(bindings.resolveCalls, 1U);
}

TEST_F(SceneSpriteAssetTest, WrongAssetKindIsUnresolved)
{
    TestSpriteBindings bindings{.store = &store()};
    bindings.bind(wrongKind_, 7);
    const Core::Status status = extractSingleSprite(wrongKind_, bindings.resolver());
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, SceneErrorCode::UnresolvedSprite);
    EXPECT_EQ(bindings.resolveCalls, 1U);
}

TEST_F(SceneSpriteAssetTest, UnboundSpriteAssetIsUnresolved)
{
    TestSpriteBindings bindings{.store = &store()};
    bindings.bind(firstSprite_, 7);
    const Core::Status status = extractSingleSprite(secondSprite_, bindings.resolver());
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, SceneErrorCode::UnresolvedSprite);
    EXPECT_EQ(bindings.resolveCalls, 1U);
}

TEST_F(SceneSpriteAssetTest, ResolverReturningEmptyRefIsUnresolved)
{
    const Core::Status status = extractSingleSprite(
        firstSprite_,
        Asset::AssetFrameResourceResolver{.resolve = &resolveToEmpty});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, SceneErrorCode::UnresolvedSprite);
}

TEST_F(SceneSpriteAssetTest, HiddenSpriteSkipsBindingResolution)
{
    TestSpriteBindings bindings{.store = &store()};
    const Core::Status status = extractSingleSprite({}, bindings.resolver(), false);
    ASSERT_TRUE(status) << (status ? "" : status.error().message);
    EXPECT_EQ(bindings.resolveCalls, 0U);
}

TEST_F(SceneSpriteAssetTest, WriterFailureIsReturnedAfterSuccessfulResolution)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();
    ASSERT_TRUE(world.setSpriteRenderer2D(entity, fixtureSprite(firstSprite_)));

    auto builder = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{1});
    ASSERT_TRUE(builder.has_value());
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    ASSERT_TRUE(writer.addSprite2D(Render::RenderSprite2DInput{
        .texture = *internTestTexture(beginTestFrameResources(), 99),
        .stableEntityKey = 99,
        .widthMeters = 1.0F,
        .heightMeters = 1.0F,
    }));

    TestSpriteBindings bindings{.store = &store()};
    bindings.bind(firstSprite_, 7);
    const Core::Status status = extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{.spriteBindingResolver = bindings.resolver()});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::RenderSceneCapacityExceeded);
    EXPECT_EQ(bindings.resolveCalls, 1U);
}

TEST(SceneExtractTest, RejectsMultipleActiveCameras)
{
    World world = makeWorld();
    const EntityId first = world.createEntity().value();
    const EntityId second = world.createEntity().value();
    ASSERT_TRUE(world.setCamera2D(first, fixedCamera()));
    ASSERT_TRUE(world.setCamera2D(second, fixedCamera()));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();

    const Core::Status status = extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{
            .surfaceViewport = {.pixelWidth = 800, .pixelHeight = 600},
        });
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, SceneErrorCode::MultipleActiveCameras);
}

TEST(SceneExtractTest, ZeroActiveCamerasIsValidWithoutSetCamera)
{
    // Pure UI / no World view: extract may omit setCamera2D. Sprites without a
    // camera remain a RenderSceneBuilder commit error (existing M8-B contract).
    World world = makeWorld();
    const EntityId inactive = world.createEntity().value();
    Camera2D camera = fixedCamera();
    camera.active = false;
    ASSERT_TRUE(world.setCamera2D(inactive, camera));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();

    ASSERT_TRUE(extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{
            .surfaceViewport = {.pixelWidth = 640, .pixelHeight = 360},
        }));

    auto view = builder->commit();
    ASSERT_TRUE(view);
    EXPECT_FALSE(view->camera2D().has_value());
    EXPECT_TRUE(view->sprites2D().empty());
}

TEST(SceneExtractTest, SuspendedSurfaceSkipsCameraWithoutFailing)
{
    World world = makeWorld();
    const EntityId cameraEntity = world.createEntity().value();
    ASSERT_TRUE(world.setCamera2D(cameraEntity, fixedCamera()));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();

    ASSERT_TRUE(extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{
            .surfaceViewport = {.pixelWidth = 0, .pixelHeight = 0},
        }));

    auto view = builder->commit();
    ASSERT_TRUE(view);
    EXPECT_FALSE(view->camera2D().has_value());
}

TEST_F(SceneSpriteAssetTest, AppliesPivotAndZRotationToSpriteCenter)
{
    World world = makeWorld();
    const EntityId cameraEntity = world.createEntity().value();
    ASSERT_TRUE(world.setCamera2D(cameraEntity, fixedCamera(20.0F)));

    LocalTransform local = translated(0.0F, 0.0F);
    local.rotation = rotationAroundZ(std::numbers::pi_v<float> * 0.5F);
    const EntityId spriteEntity = world.createEntity(local).value();
    SpriteRenderer2D sprite = fixtureSprite(firstSprite_, 2.0F, 4.0F);
    sprite.overrides = SpriteOverrideFlags::Size | SpriteOverrideFlags::Pivot;
    sprite.pivotOverride = {0.0F, 0.0F};
    ASSERT_TRUE(world.setSpriteRenderer2D(spriteEntity, sprite));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    TestSpriteBindings bindings{.store = &store()};
    bindings.bind(firstSprite_, 17);
    ASSERT_TRUE(extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{
            .surfaceViewport = {.pixelWidth = 800, .pixelHeight = 600},
            .spriteBindingResolver = bindings.resolver(),
        }));

    auto view = builder->commit();
    ASSERT_TRUE(view);
    ASSERT_EQ(view->sprites2D().size(), 1U);
    EXPECT_EQ(textureBindingKey(view->sprites2D()[0].texture), 17U);
    // Pivot bottom-left: local offset to geometric center is (+1, +2). After
    // +90° Z rotation: (x,y) -> (-y, x) => (-2, 1).
    EXPECT_NEAR(view->sprites2D()[0].centerX, -2.0F, 1.0e-4F);
    EXPECT_NEAR(view->sprites2D()[0].centerY, 1.0F, 1.0e-4F);
    EXPECT_NEAR(
        view->sprites2D()[0].rotationRadians,
        std::numbers::pi_v<float> * 0.5F,
        1.0e-4F);
    // Without UvRect override, extract keeps full-texture defaults.
    EXPECT_FLOAT_EQ(view->sprites2D()[0].u0, 0.0F);
    EXPECT_FLOAT_EQ(view->sprites2D()[0].v0, 0.0F);
    EXPECT_FLOAT_EQ(view->sprites2D()[0].u1, 1.0F);
    EXPECT_FLOAT_EQ(view->sprites2D()[0].v1, 1.0F);
}

TEST_F(SceneSpriteAssetTest, ForwardsOptionalUvRectOverride)
{
    World world = makeWorld();
    const EntityId cameraEntity = world.createEntity().value();
    ASSERT_TRUE(world.setCamera2D(cameraEntity, fixedCamera(20.0F)));

    const EntityId spriteEntity = world.createEntity(translated(0.0F, 0.0F)).value();
    SpriteRenderer2D sprite = fixtureSprite(firstSprite_, 1.0F, 1.0F);
    sprite.overrides = SpriteOverrideFlags::Size | SpriteOverrideFlags::UvRect;
    sprite.uvRectOverride = {.u0 = 0.5F, .v0 = 0.0F, .u1 = 1.0F, .v1 = 1.0F};
    ASSERT_TRUE(world.setSpriteRenderer2D(spriteEntity, sprite));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    TestSpriteBindings bindings{.store = &store()};
    bindings.bind(firstSprite_, 23);
    ASSERT_TRUE(extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{
            .surfaceViewport = {.pixelWidth = 800, .pixelHeight = 600},
            .spriteBindingResolver = bindings.resolver(),
        }));

    auto view = builder->commit();
    ASSERT_TRUE(view);
    ASSERT_EQ(view->sprites2D().size(), 1U);
    EXPECT_EQ(textureBindingKey(view->sprites2D()[0].texture), 23U);
    EXPECT_FLOAT_EQ(view->sprites2D()[0].u0, 0.5F);
    EXPECT_FLOAT_EQ(view->sprites2D()[0].v0, 0.0F);
    EXPECT_FLOAT_EQ(view->sprites2D()[0].u1, 1.0F);
    EXPECT_FLOAT_EQ(view->sprites2D()[0].v1, 1.0F);
}

TEST(SceneExtractTest, InactiveCameraIsIgnored)
{
    World world = makeWorld();
    const EntityId inactive = world.createEntity(translated(5.0F, 5.0F)).value();
    Camera2D camera = fixedCamera();
    camera.active = false;
    ASSERT_TRUE(world.setCamera2D(inactive, camera));

    const EntityId active = world.createEntity(translated(1.0F, 2.0F)).value();
    ASSERT_TRUE(world.setCamera2D(active, fixedCamera(10.0F)));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    ASSERT_TRUE(extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{
            .surfaceViewport = {.pixelWidth = 1000, .pixelHeight = 500},
        }));

    auto view = builder->commit();
    ASSERT_TRUE(view);
    ASSERT_TRUE(view->camera2D().has_value());
    EXPECT_FLOAT_EQ(view->camera2D()->centerX, 1.0F);
    EXPECT_FLOAT_EQ(view->camera2D()->centerY, 2.0F);
    EXPECT_FLOAT_EQ(view->camera2D()->worldHeight, 10.0F);
}

[[nodiscard]] PerspectiveCamera3D fixturePerspectiveCamera()
{
    return PerspectiveCamera3D{
        .verticalFovDegrees = 60.0F,
        .nearPlaneMeters = 0.1F,
        .farPlaneMeters = 100.0F,
        .active = true,
    };
}

[[nodiscard]] MeshRenderer3D fixtureMesh(
    Asset::AssetHandle mesh,
    Asset::AssetHandle material)
{
    return MeshRenderer3D{
        .mesh = mesh,
        .material = material,
        .localBounds = Render::RenderBoundingSphereInput{.radius = 0.5F},
        .visible = true,
    };
}

struct TestMeshBindings final {
    struct Binding final {
        Asset::AssetHandle asset{};
        u32 key = 0;
    };

    [[nodiscard]] Asset::AssetFrameResourceResolver meshResolver() noexcept
    {
        return Asset::AssetFrameResourceResolver{.userData = this, .resolve = &resolveMesh};
    }

    [[nodiscard]] Asset::AssetFrameResourceResolver materialResolver() noexcept
    {
        return Asset::AssetFrameResourceResolver{.userData = this, .resolve = &resolveMaterial};
    }

    void bind(Asset::AssetHandle asset, u32 key) noexcept
    {
        bindings[bindingCount++] = Binding{.asset = asset, .key = key};
    }

    [[nodiscard]] static Core::Result<Render::FrameResourceRef> resolveMesh(
        void* userData,
        Asset::AssetHandle asset,
        Render::FrameResourceSink& sink) noexcept
    {
        auto& self = *static_cast<TestMeshBindings*>(userData);
        ++self.meshResolveCalls;
        return self.resolveExpected(
            asset, AssetFormat::AssetKind::StaticMesh,
            Render::FrameResourceKind::Mesh3DGeometry, sink);
    }

    [[nodiscard]] static Core::Result<Render::FrameResourceRef> resolveMaterial(
        void* userData,
        Asset::AssetHandle asset,
        Render::FrameResourceSink& sink) noexcept
    {
        auto& self = *static_cast<TestMeshBindings*>(userData);
        ++self.materialResolveCalls;
        return self.resolveExpected(
            asset, AssetFormat::AssetKind::Material,
            Render::FrameResourceKind::Mesh3DMaterial, sink);
    }

    [[nodiscard]] Core::Result<Render::FrameResourceRef> resolveExpected(
        Asset::AssetHandle asset,
        AssetFormat::AssetKind expectedKind,
        Render::FrameResourceKind resourceKind,
        Render::FrameResourceSink& sink) const noexcept
    {
        if (store == nullptr || !asset
            || store->state(asset) == Asset::AssetLogicalState::Unloaded
            || store->assetKind(asset) != expectedKind) {
            return Render::FrameResourceRef{};
        }
        for (usize index = 0; index < bindingCount; ++index) {
            if (bindings[index].asset == asset) {
                return internTestResource(sink, resourceKind, bindings[index].key);
            }
        }
        return Render::FrameResourceRef{};
    }

    Asset::AssetStore* store = nullptr;
    std::array<Binding, 8> bindings{};
    usize bindingCount = 0;
    u32 meshResolveCalls = 0;
    u32 materialResolveCalls = 0;
};

class SceneMeshAssetTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto store = Asset::AssetStore::Create({.capacity = 6, .memoryResource = &memory_});
        ASSERT_TRUE(store.has_value()) << (store ? "" : store.error().message);
        store_.emplace(std::move(*store));

        auto meshA = store_->beginQueued(fixtureAssetId(1), AssetFormat::AssetKind::StaticMesh);
        auto meshB = store_->beginQueued(fixtureAssetId(2), AssetFormat::AssetKind::StaticMesh);
        auto materialA = store_->beginQueued(fixtureAssetId(3), AssetFormat::AssetKind::Material);
        auto materialB = store_->beginQueued(fixtureAssetId(4), AssetFormat::AssetKind::Material);
        auto wrongKind = store_->beginQueued(fixtureAssetId(5), AssetFormat::AssetKind::Texture2D);
        ASSERT_TRUE(meshA.has_value());
        ASSERT_TRUE(meshB.has_value());
        ASSERT_TRUE(materialA.has_value());
        ASSERT_TRUE(materialB.has_value());
        ASSERT_TRUE(wrongKind.has_value());
        meshA_ = *meshA;
        meshB_ = *meshB;
        materialA_ = *materialA;
        materialB_ = *materialB;
        wrongKind_ = *wrongKind;
    }

    [[nodiscard]] Asset::AssetStore& store() noexcept { return *store_; }

    std::pmr::unsynchronized_pool_resource memory_{};
    std::optional<Asset::AssetStore> store_{};
    Asset::AssetHandle meshA_{};
    Asset::AssetHandle meshB_{};
    Asset::AssetHandle materialA_{};
    Asset::AssetHandle materialB_{};
    Asset::AssetHandle wrongKind_{};
};

TEST_F(SceneMeshAssetTest, SetsClearsAndQueriesPerspectiveCameraAndMesh)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();

    ASSERT_TRUE(world.setPerspectiveCamera3D(entity, fixturePerspectiveCamera()));
    ASSERT_NE(world.perspectiveCamera3D(entity), nullptr);
    EXPECT_FLOAT_EQ(world.perspectiveCamera3D(entity)->verticalFovDegrees, 60.0F);

    ASSERT_TRUE(world.setMeshRenderer3D(entity, fixtureMesh(meshA_, materialA_)));
    ASSERT_NE(world.meshRenderer3D(entity), nullptr);
    EXPECT_EQ(world.meshRenderer3D(entity)->mesh, meshA_);
    EXPECT_EQ(world.meshRenderer3D(entity)->material, materialA_);

    ASSERT_TRUE(world.clearPerspectiveCamera3D(entity));
    EXPECT_EQ(world.perspectiveCamera3D(entity), nullptr);
    ASSERT_TRUE(world.clearMeshRenderer3D(entity));
    EXPECT_EQ(world.meshRenderer3D(entity), nullptr);
}

TEST(ScenePointLight2DTest, SetsClearsQueriesAndRejectsInvalidComponent)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();
    PointLight2D light{
        .color = {.red = 0.25F, .green = 0.5F, .blue = 0.75F},
        .intensity = 2.0F,
        .radiusMeters = 6.0F,
    };
    ASSERT_TRUE(world.setPointLight2D(entity, light));
    ASSERT_NE(world.pointLight2D(entity), nullptr);
    EXPECT_EQ(*world.pointLight2D(entity), light);

    light.radiusMeters = 0.0F;
    auto invalid = world.setPointLight2D(entity, light);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, SceneErrorCode::InvalidComponent);
    EXPECT_FLOAT_EQ(world.pointLight2D(entity)->radiusMeters, 6.0F);

    ASSERT_TRUE(world.clearPointLight2D(entity));
    EXPECT_EQ(world.pointLight2D(entity), nullptr);
}

TEST(SceneShadowOccluder2DTest, SetsClearsQueriesAndRejectsInvalidComponent)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();
    ShadowOccluder2D occluder{
        .localStartX = -2.0F,
        .localStartY = 1.0F,
        .localEndX = 3.0F,
        .localEndY = 1.0F,
    };
    ASSERT_TRUE(world.setShadowOccluder2D(entity, occluder));
    ASSERT_NE(world.shadowOccluder2D(entity), nullptr);
    EXPECT_EQ(*world.shadowOccluder2D(entity), occluder);

    occluder.localEndX = occluder.localStartX;
    occluder.localEndY = occluder.localStartY;
    auto invalid = world.setShadowOccluder2D(entity, occluder);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, SceneErrorCode::InvalidComponent);
    EXPECT_FLOAT_EQ(world.shadowOccluder2D(entity)->localEndX, 3.0F);

    ASSERT_TRUE(world.clearShadowOccluder2D(entity));
    EXPECT_EQ(world.shadowOccluder2D(entity), nullptr);
}

TEST(ScenePointLight2DTest, DestroyedEntityDoesNotLeakLightIntoReusedSlot)
{
    World world = makeWorld(1);
    const EntityId original = world.createEntity().value();
    ASSERT_TRUE(world.setPointLight2D(original, PointLight2D{}));
    ASSERT_TRUE(world.setShadowOccluder2D(original, ShadowOccluder2D{}));
    ASSERT_TRUE(world.destroyEntity(original));

    const EntityId replacement = world.createEntity().value();
    EXPECT_EQ(replacement.index(), original.index());
    EXPECT_NE(replacement.generation(), original.generation());
    EXPECT_EQ(world.pointLight2D(original), nullptr);
    EXPECT_EQ(world.pointLight2D(replacement), nullptr);
    EXPECT_EQ(world.shadowOccluder2D(original), nullptr);
    EXPECT_EQ(world.shadowOccluder2D(replacement), nullptr);
}

TEST(ScenePointLight2DTest, ExtractsStableWorldPositionsColorsRadiusAndAmbientIntoRenderScene)
{
    World world = makeWorld();
    const EntityId first = world.createEntity(translated(1.0F, 2.0F)).value();
    const EntityId second = world.createEntity(translated(-3.0F, 4.0F)).value();
    const EntityId inactive = world.createEntity().value();
    ASSERT_TRUE(world.setPointLight2D(first, PointLight2D{
        .color = {.red = 0.5F, .green = 0.25F, .blue = 0.125F},
        .intensity = 2.0F,
        .radiusMeters = 5.0F,
    }));
    ASSERT_TRUE(world.setPointLight2D(second, PointLight2D{
        .color = {.red = 0.1F, .green = 0.2F, .blue = 0.3F},
        .intensity = 3.0F,
        .radiusMeters = 7.0F,
    }));
    ASSERT_TRUE(world.setPointLight2D(inactive, PointLight2D{.active = false}));
    LocalTransform shadowTransform = translated(4.0F, 5.0F);
    shadowTransform.scale = {2.0F, 3.0F, 1.0F};
    const float halfAngle = std::numbers::pi_v<float> * 0.25F;
    shadowTransform.rotation = {0.0F, 0.0F, std::sin(halfAngle), std::cos(halfAngle)};
    const EntityId shadow = world.createEntity(shadowTransform).value();
    const EntityId secondShadow = world.createEntity(translated(-2.0F, -1.0F)).value();
    const EntityId inactiveShadow = world.createEntity().value();
    ASSERT_TRUE(world.setShadowOccluder2D(shadow, ShadowOccluder2D{
        .localStartX = -1.0F,
        .localEndX = 1.0F,
    }));
    ASSERT_TRUE(world.setShadowOccluder2D(secondShadow, ShadowOccluder2D{
        .localStartX = 0.0F,
        .localStartY = -1.0F,
        .localEndX = 0.0F,
        .localEndY = 1.0F,
    }));
    ASSERT_TRUE(world.setShadowOccluder2D(
        inactiveShadow,
        ShadowOccluder2D{.active = false}));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder.has_value());
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(0));
    ASSERT_TRUE(extractRenderSceneFromWorld(
        world,
        writer,
        packet.resourceSink(),
        ExtractRenderSceneParams{.ambientLight2DScale = 0.3F}));

    auto view = builder->commit();
    ASSERT_TRUE(view.has_value());
    ASSERT_TRUE(view->sprite2DLighting().has_value());
    const auto lights = view->sprite2DLighting()->pointLights();
    ASSERT_EQ(lights.size(), 2U);
    EXPECT_FLOAT_EQ(lights[0].positionX, 1.0F);
    EXPECT_FLOAT_EQ(lights[0].positionY, 2.0F);
    EXPECT_FLOAT_EQ(lights[0].radiusMeters, 5.0F);
    EXPECT_FLOAT_EQ(lights[0].colorR, 1.0F);
    EXPECT_FLOAT_EQ(lights[0].colorG, 0.5F);
    EXPECT_FLOAT_EQ(lights[0].colorB, 0.25F);
    EXPECT_FLOAT_EQ(lights[1].positionX, -3.0F);
    EXPECT_FLOAT_EQ(lights[1].positionY, 4.0F);
    EXPECT_FLOAT_EQ(lights[1].radiusMeters, 7.0F);
    EXPECT_FLOAT_EQ(lights[1].colorR, 0.3F);
    EXPECT_FLOAT_EQ(lights[1].colorG, 0.6F);
    EXPECT_FLOAT_EQ(lights[1].colorB, 0.9F);
    const auto shadowSegments = view->sprite2DLighting()->shadowSegments();
    ASSERT_EQ(shadowSegments.size(), 2U);
    EXPECT_NEAR(shadowSegments[0].startX, 4.0F, 0.0001F);
    EXPECT_NEAR(shadowSegments[0].startY, 3.0F, 0.0001F);
    EXPECT_NEAR(shadowSegments[0].endX, 4.0F, 0.0001F);
    EXPECT_NEAR(shadowSegments[0].endY, 7.0F, 0.0001F);
    EXPECT_FLOAT_EQ(shadowSegments[1].startX, -2.0F);
    EXPECT_FLOAT_EQ(shadowSegments[1].startY, -2.0F);
    EXPECT_FLOAT_EQ(shadowSegments[1].endX, -2.0F);
    EXPECT_FLOAT_EQ(shadowSegments[1].endY, 0.0F);
    EXPECT_EQ(view->statistics().shadowOccluder2DCount, 2U);
    EXPECT_FLOAT_EQ(view->sprite2DLighting()->ambientScale(), 0.3F);
}

TEST(ScenePointLight2DTest, EnforcesCapacityAndPublishesInactiveAmbientOnly)
{
    World world = makeWorld(Render::Sprite2DLightingDesc::MaximumPointLightCount + 1U);
    for (usize index = 0; index < Render::Sprite2DLightingDesc::MaximumPointLightCount + 1U; ++index) {
        const EntityId entity = world.createEntity().value();
        ASSERT_TRUE(world.setPointLight2D(entity, PointLight2D{}));
    }

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder.has_value());
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(0));
    auto tooMany = extractRenderSceneFromWorld(world, writer, packet.resourceSink());
    ASSERT_FALSE(tooMany);
    EXPECT_EQ(tooMany.error().code, SceneErrorCode::TooManyActivePointLights2D);
    builder->rollback();

    World inactiveWorld = makeWorld();
    const EntityId inactive = inactiveWorld.createEntity().value();
    ASSERT_TRUE(inactiveWorld.setPointLight2D(inactive, PointLight2D{.active = false}));
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter inactiveWriter = builder->writer();
    ASSERT_TRUE(extractRenderSceneFromWorld(
        inactiveWorld,
        inactiveWriter,
        packet.resourceSink(),
        ExtractRenderSceneParams{.ambientLight2DScale = 0.4F}));
    auto view = builder->commit();
    ASSERT_TRUE(view.has_value());
    ASSERT_TRUE(view->sprite2DLighting().has_value());
    EXPECT_TRUE(view->sprite2DLighting()->pointLights().empty());
    EXPECT_FLOAT_EQ(view->sprite2DLighting()->ambientScale(), 0.4F);

    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter invalidWriter = builder->writer();
    auto invalidAmbient = extractRenderSceneFromWorld(
        inactiveWorld,
        invalidWriter,
        packet.resourceSink(),
        ExtractRenderSceneParams{
            .ambientLight2DScale = std::numeric_limits<float>::quiet_NaN(),
        });
    ASSERT_FALSE(invalidAmbient);
    EXPECT_EQ(invalidAmbient.error().code, SceneErrorCode::InvalidComponent);
    builder->rollback();
}

TEST(SceneShadowOccluder2DTest, EnforcesFixedActiveSegmentCapacity)
{
    World world = makeWorld(Render::Sprite2DLightingDesc::MaximumShadowSegmentCount + 2U);
    const EntityId light = world.createEntity().value();
    ASSERT_TRUE(world.setPointLight2D(light, PointLight2D{}));
    for (usize index = 0;
         index < Render::Sprite2DLightingDesc::MaximumShadowSegmentCount + 1U;
         ++index) {
        const EntityId entity = world.createEntity().value();
        ASSERT_TRUE(world.setShadowOccluder2D(entity, ShadowOccluder2D{}));
    }

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder.has_value());
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(0));
    auto tooMany = extractRenderSceneFromWorld(world, writer, packet.resourceSink());
    ASSERT_FALSE(tooMany);
    EXPECT_EQ(tooMany.error().code, SceneErrorCode::TooManyActiveShadowOccluders2D);
    builder->rollback();
}

TEST(SceneShadowOccluder2DTest, OccluderWithoutPointLightPreservesUnlitScene)
{
    World world = makeWorld();
    const EntityId occluder = world.createEntity().value();
    ASSERT_TRUE(world.setShadowOccluder2D(occluder, ShadowOccluder2D{}));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder.has_value());
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(0));
    ASSERT_TRUE(extractRenderSceneFromWorld(world, writer, packet.resourceSink()));

    auto view = builder->commit();
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->sprite2DLighting().has_value());
    EXPECT_FALSE(view->statistics().sprite2DLightingConfigured);
    EXPECT_EQ(view->statistics().shadowOccluder2DCount, 0U);
}

TEST(SceneDirectionalLightTest, SetsClearsQueriesAndRejectsInvalidComponent)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();
    DirectionalLight3D light{
        .color = {.red = 0.25F, .green = 0.5F, .blue = 0.75F},
        .intensity = 2.0F,
    };
    ASSERT_TRUE(world.setDirectionalLight3D(entity, light));
    ASSERT_NE(world.directionalLight3D(entity), nullptr);
    EXPECT_EQ(*world.directionalLight3D(entity), light);

    light.intensity = std::numeric_limits<float>::quiet_NaN();
    auto invalid = world.setDirectionalLight3D(entity, light);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, SceneErrorCode::InvalidComponent);
    EXPECT_FLOAT_EQ(world.directionalLight3D(entity)->intensity, 2.0F);

    ASSERT_TRUE(world.clearDirectionalLight3D(entity));
    EXPECT_EQ(world.directionalLight3D(entity), nullptr);
}

TEST(SceneDirectionalLightTest, DestroyedEntityDoesNotLeakLightIntoReusedSlot)
{
    World world = makeWorld(1);
    const EntityId original = world.createEntity().value();
    ASSERT_TRUE(world.setDirectionalLight3D(original, DirectionalLight3D{}));
    ASSERT_TRUE(world.destroyEntity(original));

    const EntityId replacement = world.createEntity().value();
    EXPECT_EQ(replacement.index(), original.index());
    EXPECT_NE(replacement.generation(), original.generation());
    EXPECT_EQ(world.directionalLight3D(original), nullptr);
    EXPECT_EQ(world.directionalLight3D(replacement), nullptr);
}

TEST(SceneDirectionalLightTest, ExtractsStableWorldDirectionsColorsAndAmbientIntoRenderScene)
{
    World world = makeWorld();
    constexpr float HalfSqrtTwo = 0.70710678118F;
    const EntityId first = world.createEntity().value();
    const EntityId second = world.createEntity(LocalTransform{
        .rotation = {.y = HalfSqrtTwo, .w = HalfSqrtTwo},
    }).value();
    const EntityId inactive = world.createEntity().value();
    ASSERT_TRUE(world.setDirectionalLight3D(first, DirectionalLight3D{
        .color = {.red = 0.5F, .green = 0.25F, .blue = 0.125F},
        .intensity = 2.0F,
    }));
    ASSERT_TRUE(world.setDirectionalLight3D(second, DirectionalLight3D{
        .color = {.red = 0.1F, .green = 0.2F, .blue = 0.3F},
        .intensity = 3.0F,
    }));
    ASSERT_TRUE(world.setDirectionalLight3D(inactive, DirectionalLight3D{.active = false}));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder.has_value());
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(0));
    ASSERT_TRUE(extractRenderSceneFromWorld(
        world,
        writer,
        packet.resourceSink(),
        ExtractRenderSceneParams{.ambientLightScale = 0.3F}));

    auto view = builder->commit();
    ASSERT_TRUE(view.has_value());
    ASSERT_TRUE(view->mesh3DLighting().has_value());
    const auto lights = view->mesh3DLighting()->directionalLights();
    ASSERT_EQ(lights.size(), 2U);
    EXPECT_NEAR(lights[0].directionTowardLightX, 0.0F, 1.0e-5F);
    EXPECT_NEAR(lights[0].directionTowardLightY, 0.0F, 1.0e-5F);
    EXPECT_NEAR(lights[0].directionTowardLightZ, 1.0F, 1.0e-5F);
    EXPECT_FLOAT_EQ(lights[0].colorR, 1.0F);
    EXPECT_FLOAT_EQ(lights[0].colorG, 0.5F);
    EXPECT_FLOAT_EQ(lights[0].colorB, 0.25F);
    EXPECT_NEAR(lights[1].directionTowardLightX, 1.0F, 1.0e-5F);
    EXPECT_NEAR(lights[1].directionTowardLightY, 0.0F, 1.0e-5F);
    EXPECT_NEAR(lights[1].directionTowardLightZ, 0.0F, 1.0e-5F);
    EXPECT_FLOAT_EQ(lights[1].colorR, 0.3F);
    EXPECT_FLOAT_EQ(lights[1].colorG, 0.6F);
    EXPECT_FLOAT_EQ(lights[1].colorB, 0.9F);
    EXPECT_FLOAT_EQ(view->mesh3DLighting()->ambientScale(), 0.3F);
}

TEST(SceneDirectionalLightTest, RejectsMoreThanTheFixedActiveLightLimit)
{
    World world = makeWorld(5);
    for (usize index = 0; index < 5; ++index) {
        const EntityId entity = world.createEntity().value();
        ASSERT_TRUE(world.setDirectionalLight3D(entity, DirectionalLight3D{}));
    }

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder.has_value());
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(0));
    auto status = extractRenderSceneFromWorld(world, writer, packet.resourceSink());
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, SceneErrorCode::TooManyActiveDirectionalLights);
    builder->rollback();
}

TEST(SceneDirectionalLightTest, InactiveLightsPublishAmbientOnlyAndInvalidAmbientAlwaysFails)
{
    World world = makeWorld();
    const EntityId inactive = world.createEntity().value();
    ASSERT_TRUE(world.setDirectionalLight3D(inactive, DirectionalLight3D{.active = false}));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder.has_value());
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(0));
    ASSERT_TRUE(extractRenderSceneFromWorld(
        world,
        writer,
        packet.resourceSink(),
        ExtractRenderSceneParams{.ambientLightScale = 0.4F}));

    auto view = builder->commit();
    ASSERT_TRUE(view.has_value());
    ASSERT_TRUE(view->mesh3DLighting().has_value());
    EXPECT_TRUE(view->mesh3DLighting()->directionalLights().empty());
    EXPECT_FLOAT_EQ(view->mesh3DLighting()->ambientScale(), 0.4F);

    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter invalidWriter = builder->writer();
    auto invalid = extractRenderSceneFromWorld(
        world,
        invalidWriter,
        packet.resourceSink(),
        ExtractRenderSceneParams{
            .ambientLightScale = std::numeric_limits<float>::quiet_NaN(),
        });
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, SceneErrorCode::InvalidComponent);
    builder->rollback();
}

TEST_F(SceneMeshAssetTest, RejectsInvalidPropertiesButStoresWeakMeshHandles)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();

    PerspectiveCamera3D badCamera = fixturePerspectiveCamera();
    badCamera.nearPlaneMeters = 10.0F;
    badCamera.farPlaneMeters = 1.0F;
    EXPECT_EQ(
        world.setPerspectiveCamera3D(entity, badCamera).error().code,
        SceneErrorCode::InvalidComponent);

    ASSERT_TRUE(world.setMeshRenderer3D(entity, fixtureMesh({}, {})));
    EXPECT_FALSE(world.meshRenderer3D(entity)->mesh);
    EXPECT_FALSE(world.meshRenderer3D(entity)->material);

    MeshRenderer3D invalidBounds = fixtureMesh(meshA_, materialA_);
    invalidBounds.localBounds.radius = 0.0F;
    EXPECT_EQ(
        world.setMeshRenderer3D(entity, invalidBounds).error().code,
        SceneErrorCode::InvalidComponent);
}

TEST_F(SceneMeshAssetTest, ExtractsPerspectiveCameraAndResolvedMeshIntoRenderScene)
{
    World world = makeWorld();
    const EntityId cameraEntity = world.createEntity(translated(0.0F, 0.35F, 8.0F)).value();
    ASSERT_TRUE(world.setPerspectiveCamera3D(cameraEntity, fixturePerspectiveCamera()));

    const EntityId meshEntity = world.createEntity(translated(0.0F, 0.0F, 0.0F)).value();
    ASSERT_TRUE(world.setMeshRenderer3D(meshEntity, fixtureMesh(meshA_, materialA_)));

    const EntityId hidden = world.createEntity(translated(1.0F, 0.0F, 0.0F)).value();
    MeshRenderer3D invisible = fixtureMesh({}, {});
    invisible.visible = false;
    ASSERT_TRUE(world.setMeshRenderer3D(hidden, invisible));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame(Render::RenderSceneFrameParameters{
        .primarySurfaceAspectRatio = 16.0F / 9.0F,
    }));
    Render::RenderSceneWriter writer = builder->writer();
    TestMeshBindings bindings{.store = &store()};
    bindings.bind(meshA_, 7);
    bindings.bind(materialA_, 11);
    ASSERT_TRUE(extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{
            .surfaceViewport = {.pixelWidth = 1280, .pixelHeight = 720},
            .mesh3DBindingResolver = bindings.meshResolver(),
            .material3DBindingResolver = bindings.materialResolver(),
        }));

    auto view = builder->commit();
    ASSERT_TRUE(view);
    ASSERT_TRUE(view->perspectiveCamera().has_value());
    EXPECT_FLOAT_EQ(view->perspectiveCamera()->positionX, 0.0F);
    EXPECT_FLOAT_EQ(view->perspectiveCamera()->positionY, 0.35F);
    EXPECT_FLOAT_EQ(view->perspectiveCamera()->positionZ, 8.0F);
    EXPECT_FLOAT_EQ(view->perspectiveCamera()->verticalFovDegrees, 60.0F);

    ASSERT_EQ(view->meshes3D().size(), 1U);
    EXPECT_EQ(frameResourceBindingKey(
                  view->meshes3D()[0].mesh, Render::FrameResourceKind::Mesh3DGeometry),
              7U);
    EXPECT_EQ(frameResourceBindingKey(
                  view->meshes3D()[0].material, Render::FrameResourceKind::Mesh3DMaterial),
              11U);
    EXPECT_EQ(bindings.meshResolveCalls, 1U);
    EXPECT_EQ(bindings.materialResolveCalls, 1U);
}

TEST_F(SceneMeshAssetTest, MissingResolverRejectsVisibleMesh)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();
    ASSERT_TRUE(world.setMeshRenderer3D(entity, fixtureMesh(meshA_, materialA_)));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    const Core::Status status = extractRenderSceneFromWorld(world, writer);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, SceneErrorCode::UnresolvedMesh);
}

TEST_F(SceneMeshAssetTest, EmptyHandleRejectsBeforeCallingResolver)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();
    ASSERT_TRUE(world.setMeshRenderer3D(entity, fixtureMesh(meshA_, {})));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    TestMeshBindings bindings{.store = &store()};
    bindings.bind(meshA_, 7);
    const Core::Status status = extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{
            .mesh3DBindingResolver = bindings.meshResolver(),
            .material3DBindingResolver = bindings.materialResolver(),
        });
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, SceneErrorCode::UnresolvedMesh);
    EXPECT_EQ(bindings.meshResolveCalls, 0U);
    EXPECT_EQ(bindings.materialResolveCalls, 0U);
}

TEST_F(SceneMeshAssetTest, WrongKindOrUnboundMeshAssetIsUnresolved)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();
    ASSERT_TRUE(world.setMeshRenderer3D(entity, fixtureMesh(wrongKind_, materialA_)));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    TestMeshBindings bindings{.store = &store()};
    bindings.bind(wrongKind_, 7);
    bindings.bind(materialA_, 11);
    const Core::Status status = extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{
            .mesh3DBindingResolver = bindings.meshResolver(),
            .material3DBindingResolver = bindings.materialResolver(),
        });
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, SceneErrorCode::UnresolvedMesh);
    EXPECT_EQ(bindings.meshResolveCalls, 1U);
    EXPECT_EQ(bindings.materialResolveCalls, 0U);
}

TEST_F(SceneMeshAssetTest, MaterialResolverRejectsStaticMeshHandleInMaterialField)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();
    ASSERT_TRUE(world.setMeshRenderer3D(entity, fixtureMesh(meshA_, meshB_)));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    TestMeshBindings bindings{.store = &store()};
    bindings.bind(meshA_, 7);
    bindings.bind(meshB_, 11);
    const Core::Status status = extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{
            .mesh3DBindingResolver = bindings.meshResolver(),
            .material3DBindingResolver = bindings.materialResolver(),
        });
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, SceneErrorCode::UnresolvedMesh);
    EXPECT_EQ(bindings.meshResolveCalls, 1U);
    EXPECT_EQ(bindings.materialResolveCalls, 1U);
}

TEST_F(SceneMeshAssetTest, StaleMeshHandleIsUnresolved)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();
    ASSERT_TRUE(world.setMeshRenderer3D(entity, fixtureMesh(meshA_, materialA_)));
    ASSERT_TRUE(store().unload(meshA_));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    TestMeshBindings bindings{.store = &store()};
    bindings.bind(meshA_, 7);
    bindings.bind(materialA_, 11);
    const Core::Status status = extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{
            .mesh3DBindingResolver = bindings.meshResolver(),
            .material3DBindingResolver = bindings.materialResolver(),
        });
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, SceneErrorCode::UnresolvedMesh);
    EXPECT_EQ(bindings.meshResolveCalls, 1U);
    EXPECT_EQ(bindings.materialResolveCalls, 0U);
}

TEST_F(SceneMeshAssetTest, CrossStoreMeshHandleIsUnresolved)
{
    std::pmr::unsynchronized_pool_resource foreignMemory;
    auto foreignStore = Asset::AssetStore::Create({.capacity = 1, .memoryResource = &foreignMemory});
    ASSERT_TRUE(foreignStore.has_value());
    auto foreignMesh = foreignStore->beginQueued(
        fixtureAssetId(6),
        AssetFormat::AssetKind::StaticMesh);
    ASSERT_TRUE(foreignMesh.has_value());

    World world = makeWorld();
    const EntityId entity = world.createEntity().value();
    ASSERT_TRUE(world.setMeshRenderer3D(entity, fixtureMesh(*foreignMesh, materialA_)));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    TestMeshBindings bindings{.store = &store()};
    bindings.bind(*foreignMesh, 7);
    bindings.bind(materialA_, 11);
    const Core::Status status = extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{
            .mesh3DBindingResolver = bindings.meshResolver(),
            .material3DBindingResolver = bindings.materialResolver(),
        });
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, SceneErrorCode::UnresolvedMesh);
    EXPECT_EQ(bindings.meshResolveCalls, 1U);
    EXPECT_EQ(bindings.materialResolveCalls, 0U);
}

TEST(SceneExtractTest, RejectsMultipleActivePerspectiveCameras)
{
    World world = makeWorld();
    const EntityId first = world.createEntity().value();
    const EntityId second = world.createEntity().value();
    ASSERT_TRUE(world.setPerspectiveCamera3D(first, fixturePerspectiveCamera()));
    ASSERT_TRUE(world.setPerspectiveCamera3D(second, fixturePerspectiveCamera()));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame(Render::RenderSceneFrameParameters{
        .primarySurfaceAspectRatio = 1.0F,
    }));
    Render::RenderSceneWriter writer = builder->writer();

    const Core::Status status = extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{
            .surfaceViewport = {.pixelWidth = 800, .pixelHeight = 600},
        });
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, SceneErrorCode::MultipleActiveCameras);
}

TEST(SceneExtractTest, ZeroActivePerspectiveCamerasIsValid)
{
    World world = makeWorld();
    const EntityId inactive = world.createEntity().value();
    PerspectiveCamera3D camera = fixturePerspectiveCamera();
    camera.active = false;
    ASSERT_TRUE(world.setPerspectiveCamera3D(inactive, camera));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    ASSERT_TRUE(extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{
            .surfaceViewport = {.pixelWidth = 640, .pixelHeight = 360},
        }));

    auto view = builder->commit();
    ASSERT_TRUE(view);
    EXPECT_FALSE(view->perspectiveCamera().has_value());
    EXPECT_TRUE(view->meshes3D().empty());
}

TEST_F(SceneMeshAssetTest, InstantiatesHierarchyAndMeshes)
{
    World world = makeWorld();
    const std::array nodes{
        AssetFormat::PrefabNodeView{
            .stableNodeId = 1,
            .parentIndex = -1,
            .positionY = 0.25F,
            .hasMesh = true,
            .hasMaterial = true,
            .visible = true,
        },
        AssetFormat::PrefabNodeView{
            .stableNodeId = 2,
            .parentIndex = 0,
            .positionX = 1.0F,
            .hasMesh = true,
            .hasMaterial = true,
            .visible = true,
        },
    };
    AssetFormat::PrefabPayloadView prefab{.schemaVersion = 1, .nodes = nodes};
    auto created = instantiatePrefab(
        world,
        prefab,
        PrefabMeshBinding{.mesh = meshA_, .material = materialA_});
    ASSERT_TRUE(created.has_value()) << (created ? "" : created.error().message);
    ASSERT_EQ(created->size(), 2U);
    EXPECT_EQ(world.entityCount(), 2U);
    ASSERT_NE(world.meshRenderer3D((*created)[0]), nullptr);
    ASSERT_NE(world.meshRenderer3D((*created)[1]), nullptr);
    EXPECT_EQ(world.parent((*created)[1]), (*created)[0]);

    const EntityId cameraEntity = world.createEntity(translated(0.0F, 0.0F, 6.0F)).value();
    ASSERT_TRUE(world.setPerspectiveCamera3D(cameraEntity, fixturePerspectiveCamera()));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame(Render::RenderSceneFrameParameters{
        .primarySurfaceAspectRatio = 16.0F / 9.0F,
    }));
    Render::RenderSceneWriter writer = builder->writer();
    TestMeshBindings bindings{.store = &store()};
    bindings.bind(meshA_, 1);
    bindings.bind(materialA_, 1);
    ASSERT_TRUE(extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{
            .surfaceViewport = {.pixelWidth = 1280, .pixelHeight = 720},
            .mesh3DBindingResolver = bindings.meshResolver(),
            .material3DBindingResolver = bindings.materialResolver(),
        }));
    auto view = builder->commit();
    ASSERT_TRUE(view);
    EXPECT_TRUE(view->perspectiveCamera().has_value());
    EXPECT_EQ(view->meshes3D().size(), 2U);
}

TEST(ScenePrefabInstantiateTest, RollsBackOnInvalidParentIndex)
{
    World world = makeWorld();
    const std::array nodes{
        AssetFormat::PrefabNodeView{
            .stableNodeId = 1,
            .parentIndex = -1,
            .hasMesh = false,
            .hasMaterial = false,
        },
        AssetFormat::PrefabNodeView{
            .stableNodeId = 2,
            .parentIndex = 5, // invalid: not a prior node
            .hasMesh = false,
            .hasMaterial = false,
        },
    };
    AssetFormat::PrefabPayloadView prefab{.schemaVersion = 1, .nodes = nodes};
    auto created = instantiatePrefab(world, prefab);
    ASSERT_FALSE(created.has_value());
    EXPECT_EQ(world.entityCount(), 0U);
}

TEST_F(SceneMeshAssetTest, ResolvesPerNodeMeshHandlesFromAssetIds)
{
    World world = makeWorld();
    const auto meshA = *Core::AssetId::fromBytes(Core::AssetId::Bytes{std::byte{1}});
    const auto meshB = *Core::AssetId::fromBytes(Core::AssetId::Bytes{std::byte{2}});
    const auto matA = *Core::AssetId::fromBytes(Core::AssetId::Bytes{std::byte{3}});
    const auto matB = *Core::AssetId::fromBytes(Core::AssetId::Bytes{std::byte{4}});
    const std::array nodes{
        AssetFormat::PrefabNodeView{
            .stableNodeId = 1,
            .parentIndex = -1,
            .hasMesh = true,
            .hasMaterial = true,
            .visible = true,
            .meshId = meshA,
            .materialId = matA,
        },
        AssetFormat::PrefabNodeView{
            .stableNodeId = 2,
            .parentIndex = -1,
            .positionX = 1.0F,
            .hasMesh = true,
            .hasMaterial = true,
            .visible = true,
            .meshId = meshB,
            .materialId = matB,
        },
    };
    AssetFormat::PrefabPayloadView prefab{.schemaVersion = 1, .nodes = nodes};
    PrefabMeshBinding binding{
        .mesh = meshA_,
        .material = materialA_,
        .resolveMesh =
            [meshA, meshB, first = meshA_, second = meshB_](Core::AssetId id) -> Asset::AssetHandle {
                if (id == meshA)
                {
                    return first;
                }
                if (id == meshB)
                {
                    return second;
                }
                return {};
            },
        .resolveMaterial =
            [matA, matB, first = materialA_, second = materialB_](Core::AssetId id) -> Asset::AssetHandle {
                if (id == matA)
                {
                    return first;
                }
                if (id == matB)
                {
                    return second;
                }
                return {};
            },
    };
    auto created = instantiatePrefab(world, prefab, binding);
    ASSERT_TRUE(created.has_value()) << (created ? "" : created.error().message);
    ASSERT_EQ(created->size(), 2U);
    const MeshRenderer3D* a = world.meshRenderer3D((*created)[0]);
    const MeshRenderer3D* b = world.meshRenderer3D((*created)[1]);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->mesh, meshA_);
    EXPECT_EQ(a->material, materialA_);
    EXPECT_EQ(b->mesh, meshB_);
    EXPECT_EQ(b->material, materialB_);
}

TEST_F(SceneMeshAssetTest, RollsBackWhenAssetIdResolverReturnsEmptyHandle)
{
    World world = makeWorld();
    const std::array nodes{
        AssetFormat::PrefabNodeView{
            .stableNodeId = 1,
            .parentIndex = -1,
            .hasMesh = true,
            .hasMaterial = true,
            .visible = true,
            .meshId = fixtureAssetId(1),
            .materialId = fixtureAssetId(3),
        },
    };
    AssetFormat::PrefabPayloadView prefab{.schemaVersion = 1, .nodes = nodes};
    auto created = instantiatePrefab(
        world,
        prefab,
        PrefabMeshBinding{
            .resolveMesh = [](Core::AssetId) -> Asset::AssetHandle { return {}; },
            .resolveMaterial = [material = materialA_](Core::AssetId) { return material; },
        });
    ASSERT_FALSE(created.has_value());
    EXPECT_EQ(created.error().code, SceneErrorCode::UnresolvedMesh);
    EXPECT_EQ(world.entityCount(), 0U);
}

TEST_F(SceneMeshAssetTest, RollsBackEarlierNodesWhenLaterAssetResolverReturnsEmptyHandle)
{
    World world = makeWorld();
    const std::array nodes{
        AssetFormat::PrefabNodeView{
            .stableNodeId = 1,
            .parentIndex = -1,
            .hasMesh = true,
            .hasMaterial = true,
            .visible = true,
            .meshId = fixtureAssetId(1),
            .materialId = fixtureAssetId(3),
        },
        AssetFormat::PrefabNodeView{
            .stableNodeId = 2,
            .parentIndex = 0,
            .hasMesh = true,
            .hasMaterial = true,
            .visible = true,
            .meshId = fixtureAssetId(2),
            .materialId = fixtureAssetId(3),
        },
    };
    AssetFormat::PrefabPayloadView prefab{.schemaVersion = 1, .nodes = nodes};
    auto created = instantiatePrefab(
        world,
        prefab,
        PrefabMeshBinding{
            .resolveMesh = [firstId = fixtureAssetId(1), mesh = meshA_](Core::AssetId id) {
                return id == firstId ? mesh : Asset::AssetHandle{};
            },
            .resolveMaterial = [material = materialA_](Core::AssetId) { return material; },
        });
    ASSERT_FALSE(created.has_value());
    EXPECT_EQ(created.error().code, SceneErrorCode::UnresolvedMesh);
    EXPECT_EQ(world.entityCount(), 0U);
}

} // namespace
} // namespace Tina::Scene
