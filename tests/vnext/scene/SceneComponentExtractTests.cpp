#include <tina/scene/ExtractRenderScene.hpp>
#include <tina/scene/World.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

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

[[nodiscard]] SpriteRenderer2D fixtureSprite(
    u32 key,
    float width = 1.0F,
    float height = 1.0F)
{
    return SpriteRenderer2D{
        .fixtureSpriteKey = key,
        .overrides = SpriteOverrideFlags::Size,
        .sizeOverrideMeters = {width, height},
        .visible = true,
    };
}

TEST(SceneComponentStorageTest, SetsClearsAndQueriesCameraAndSprite)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();

    ASSERT_TRUE(world.setCamera2D(entity, fixedCamera()));
    ASSERT_NE(world.camera2D(entity), nullptr);
    EXPECT_TRUE(std::holds_alternative<Render::FixedWorldHeight2D>(
        world.camera2D(entity)->projection));

    ASSERT_TRUE(world.setSpriteRenderer2D(entity, fixtureSprite(1)));
    ASSERT_NE(world.spriteRenderer2D(entity), nullptr);
    EXPECT_EQ(world.spriteRenderer2D(entity)->fixtureSpriteKey, 1U);

    ASSERT_TRUE(world.clearCamera2D(entity));
    EXPECT_EQ(world.camera2D(entity), nullptr);
    ASSERT_TRUE(world.clearSpriteRenderer2D(entity));
    EXPECT_EQ(world.spriteRenderer2D(entity), nullptr);
}

TEST(SceneComponentStorageTest, RejectsInvalidComponents)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();

    Camera2D badCamera = fixedCamera();
    badCamera.normalizedViewport.width = 0.0F;
    EXPECT_EQ(
        world.setCamera2D(entity, badCamera).error().code,
        SceneErrorCode::InvalidComponent);

    SpriteRenderer2D missingKey = fixtureSprite(0);
    EXPECT_EQ(
        world.setSpriteRenderer2D(entity, missingKey).error().code,
        SceneErrorCode::InvalidComponent);

    SpriteRenderer2D badSize = fixtureSprite(1, -1.0F, 1.0F);
    EXPECT_EQ(
        world.setSpriteRenderer2D(entity, badSize).error().code,
        SceneErrorCode::InvalidComponent);

    SpriteRenderer2D badUv = fixtureSprite(1);
    badUv.overrides = SpriteOverrideFlags::Size | SpriteOverrideFlags::UvRect;
    badUv.uvRectOverride = {.u0 = 0.8F, .v0 = 0.0F, .u1 = 0.2F, .v1 = 1.0F};
    EXPECT_EQ(
        world.setSpriteRenderer2D(entity, badUv).error().code,
        SceneErrorCode::InvalidComponent);
}

TEST(SceneComponentStorageTest, DestroyedEntityDropsComponents)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();
    ASSERT_TRUE(world.setCamera2D(entity, fixedCamera()));
    ASSERT_TRUE(world.setSpriteRenderer2D(entity, fixtureSprite(7)));
    ASSERT_TRUE(world.destroyEntity(entity));

    EXPECT_EQ(world.camera2D(entity), nullptr);
    EXPECT_EQ(world.spriteRenderer2D(entity), nullptr);
}

TEST(SceneExtractTest, ExtractsSingleCameraAndSpritesIntoRenderScene)
{
    World world = makeWorld();
    const EntityId cameraEntity = world.createEntity(translated(3.0F, -1.5F)).value();
    ASSERT_TRUE(world.setCamera2D(cameraEntity, fixedCamera(9.0F)));

    const EntityId nearSprite = world.createEntity(translated(0.0F, 0.0F)).value();
    ASSERT_TRUE(world.setSpriteRenderer2D(nearSprite, fixtureSprite(1, 1.5F, 1.5F)));

    LocalTransform farLocal = translated(100.0F, 100.0F);
    const EntityId farSprite = world.createEntity(farLocal).value();
    SpriteRenderer2D far = fixtureSprite(2, 1.0F, 1.0F);
    far.orderInLayer = 5;
    ASSERT_TRUE(world.setSpriteRenderer2D(farSprite, far));

    const EntityId hidden = world.createEntity(translated(1.0F, 1.0F)).value();
    SpriteRenderer2D invisible = fixtureSprite(3);
    invisible.visible = false;
    ASSERT_TRUE(world.setSpriteRenderer2D(hidden, invisible));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();

    const ExtractRenderSceneParams params{
        .surfaceViewport = {.pixelWidth = 1280, .pixelHeight = 720},
    };
    ASSERT_TRUE(extractRenderSceneFromWorld(world, writer, params));

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
        if (item.spriteKey == 1U) {
            foundNear = true;
            EXPECT_FLOAT_EQ(item.centerX, 0.0F);
            EXPECT_FLOAT_EQ(item.centerY, 0.0F);
            EXPECT_FLOAT_EQ(item.widthMeters, 1.5F);
            EXPECT_FLOAT_EQ(item.heightMeters, 1.5F);
        }
        EXPECT_NE(item.spriteKey, 3U);
    }
    EXPECT_TRUE(foundNear);
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

TEST(SceneExtractTest, AppliesPivotAndZRotationToSpriteCenter)
{
    World world = makeWorld();
    const EntityId cameraEntity = world.createEntity().value();
    ASSERT_TRUE(world.setCamera2D(cameraEntity, fixedCamera(20.0F)));

    LocalTransform local = translated(0.0F, 0.0F);
    local.rotation = rotationAroundZ(std::numbers::pi_v<float> * 0.5F);
    const EntityId spriteEntity = world.createEntity(local).value();
    SpriteRenderer2D sprite = fixtureSprite(1, 2.0F, 4.0F);
    sprite.overrides = SpriteOverrideFlags::Size | SpriteOverrideFlags::Pivot;
    sprite.pivotOverride = {0.0F, 0.0F};
    ASSERT_TRUE(world.setSpriteRenderer2D(spriteEntity, sprite));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    ASSERT_TRUE(extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{
            .surfaceViewport = {.pixelWidth = 800, .pixelHeight = 600},
        }));

    auto view = builder->commit();
    ASSERT_TRUE(view);
    ASSERT_EQ(view->sprites2D().size(), 1U);
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

TEST(SceneExtractTest, ForwardsOptionalUvRectOverride)
{
    World world = makeWorld();
    const EntityId cameraEntity = world.createEntity().value();
    ASSERT_TRUE(world.setCamera2D(cameraEntity, fixedCamera(20.0F)));

    const EntityId spriteEntity = world.createEntity(translated(0.0F, 0.0F)).value();
    SpriteRenderer2D sprite = fixtureSprite(1, 1.0F, 1.0F);
    sprite.overrides = SpriteOverrideFlags::Size | SpriteOverrideFlags::UvRect;
    sprite.uvRectOverride = {.u0 = 0.5F, .v0 = 0.0F, .u1 = 1.0F, .v1 = 1.0F};
    ASSERT_TRUE(world.setSpriteRenderer2D(spriteEntity, sprite));

    auto builder = Render::RenderSceneBuilder::Create();
    ASSERT_TRUE(builder);
    ASSERT_TRUE(builder->beginFrame());
    Render::RenderSceneWriter writer = builder->writer();
    ASSERT_TRUE(extractRenderSceneFromWorld(
        world,
        writer,
        ExtractRenderSceneParams{
            .surfaceViewport = {.pixelWidth = 800, .pixelHeight = 600},
        }));

    auto view = builder->commit();
    ASSERT_TRUE(view);
    ASSERT_EQ(view->sprites2D().size(), 1U);
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

} // namespace
} // namespace Tina::Scene
