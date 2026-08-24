#include <tina/asset/AssetStore.hpp>
#include <tina/asset_format/World2DSnapshot.hpp>
#include <tina/scene/PerspectiveCamera3D.hpp>
#include <tina/scene/SceneErrors.hpp>
#include <tina/scene/World.hpp>
#include <tina/scene/World2DSnapshot.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory_resource>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace Tina::Scene {
namespace {

[[nodiscard]] Core::AssetId assetId(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] World makeWorld(Core::usize capacity = 16)
{
    auto world = World::Create(WorldConfig{.entityCapacity = capacity});
    EXPECT_TRUE(world) << (world ? "" : world.error().message);
    return std::move(*world);
}

[[nodiscard]] Core::u32 stableIdFor(std::span<const World2DEntityBinding> bindings, EntityId entity) noexcept
{
    for (const World2DEntityBinding& binding : bindings)
    {
        if (binding.entity == entity)
        {
            return binding.stableEntityId;
        }
    }
    return 0U;
}

class World2DSnapshotSceneTests : public testing::Test {
  protected:
    void SetUp() override
    {
        auto store = Asset::AssetStore::Create({.capacity = 4, .memoryResource = &memory_});
        ASSERT_TRUE(store) << (store ? "" : store.error().message);
        store_.emplace(std::move(*store));

        auto sprite = store_->beginQueued(spriteId_, AssetFormat::AssetKind::Sprite);
        auto texture = store_->beginQueued(textureId_, AssetFormat::AssetKind::Texture2D);
        auto clip = store_->beginQueued(clipId_, AssetFormat::AssetKind::SpriteAnimationClip);
        ASSERT_TRUE(sprite);
        ASSERT_TRUE(texture);
        ASSERT_TRUE(clip);
        sprite_ = *sprite;
        texture_ = *texture;
        clip_ = *clip;
    }

    [[nodiscard]] World2DSnapshotCaptureConfig captureConfig(std::function<Core::u32(EntityId)> stableEntityId,
                                                             std::span<const std::byte> gameplay = {})
    {
        return World2DSnapshotCaptureConfig{
            .stableEntityId = std::move(stableEntityId),
            .assetIdForHandle = [this](Asset::AssetHandle handle) { return store_->assetId(handle); },
            .gameplaySchema = gameplay.empty() ? 0U : 900U,
            .gameplayVersion = gameplay.empty() ? 0U : 2U,
            .gameplayBytes = gameplay,
        };
    }

    [[nodiscard]] World2DSnapshotAssetResolver resolver() const
    {
        return World2DSnapshotAssetResolver{
            .resolveSprite = [this](Core::AssetId id) { return id == spriteId_ ? sprite_ : Asset::AssetHandle{}; },
            .resolveTexture = [this](Core::AssetId id) { return id == textureId_ ? texture_ : Asset::AssetHandle{}; },
            .resolveAnimationClip =
                [this](Core::AssetId id) { return id == clipId_ ? clip_ : Asset::AssetHandle{}; },
        };
    }

    std::pmr::unsynchronized_pool_resource memory_{};
    std::optional<Asset::AssetStore> store_{};
    Core::AssetId spriteId_ = assetId(1);
    Core::AssetId textureId_ = assetId(2);
    Core::AssetId clipId_ = assetId(3);
    Asset::AssetHandle sprite_{};
    Asset::AssetHandle texture_{};
    Asset::AssetHandle clip_{};
};

// Capture derives exactly one authoring node kind per entity, so every typed 2D
// component lives on its own entity rather than stacked onto the root.
TEST_F(World2DSnapshotSceneTests, CapturesRestoresAndRecapturesIdenticalBytes)
{
    World source = makeWorld();
    LocalTransform rootLocal{};
    rootLocal.position = {2.0F, 3.0F, 0.0F};
    const EntityId root = source.createEntity(rootLocal).value();
    LocalTransform childLocal{};
    childLocal.position = {1.0F, -1.0F, 0.0F};
    childLocal.rotation = {0.0F, 0.0F, 0.5F, 0.8660254F};
    const EntityId child = source.createEntity(childLocal).value();
    ASSERT_TRUE(source.setParent(child, root, ReparentMode::KeepLocal));
    const EntityId light = source.createEntity().value();
    ASSERT_TRUE(source.setParent(light, root, ReparentMode::KeepLocal));
    const EntityId occluder = source.createEntity().value();
    ASSERT_TRUE(source.setParent(occluder, root, ReparentMode::KeepLocal));
    ASSERT_TRUE(source.setCamera2D(root, Camera2D{
                                             .projection =
                                                 Render::PixelPerfect2D{
                                                     .referencePixelsPerMeter = 24.0F,
                                                     .referenceHeightPixels = 360,
                                                 },
                                             .pixelSnap = Render::RenderPixelSnapPolicy::CameraAndSprites,
                                         }));
    ASSERT_TRUE(source.setPointLight2D(light, PointLight2D{
                                                 .color = {.red = 0.25F, .green = 0.5F, .blue = 0.75F, .alpha = 1.0F},
                                                 .intensity = 2.0F,
                                                 .radiusMeters = 6.0F,
                                                 .sourceRadiusMeters = 1.0F,
                                             }));
    ASSERT_TRUE(source.setShadowOccluder2D(occluder, ShadowOccluder2D{
                                                     .localStartX = -2.0F,
                                                     .localEndX = 2.0F,
                                                 }));
    ASSERT_TRUE(source.setSpriteRenderer2D(
        child, SpriteRenderer2D{
                   .sprite = sprite_,
                   .normalTexture = texture_,
                   .overrides = SpriteOverrideFlags::Size | SpriteOverrideFlags::Pivot | SpriteOverrideFlags::UvRect,
                   .sizeOverrideMeters = {2.0F, 3.0F},
                   .pivotOverride = {0.25F, 0.75F},
                   .uvRectOverride = {.u0 = 0.1F, .v0 = 0.2F, .u1 = 0.8F, .v1 = 0.9F},
                   .color = {.red = 1, .green = 2, .blue = 3, .alpha = 4},
                   .sortingLayer = -2,
                   .orderInLayer = 17,
                   .flipX = true,
               }));
    ASSERT_TRUE(source.setSpriteAnimationBinding2D(child, SpriteAnimationBinding2D{
                                                              .clip = clip_,
                                                              .playbackSpeed = 1.5F,
                                                              .autoPlay = false,
                                                          }));
    ASSERT_TRUE(source.updateWorldTransforms());

    const std::array gameplay{std::byte{7}, std::byte{8}, std::byte{9}};
    auto bytes = captureWorld2DSnapshotBytes(source, captureConfig(
                                                         [root, child, light, occluder](EntityId entity) {
                                                             if (entity == root)
                                                                 return 100U;
                                                             if (entity == child)
                                                                 return 200U;
                                                             if (entity == light)
                                                                 return 300U;
                                                             if (entity == occluder)
                                                                 return 400U;
                                                             return 0U;
                                                         },
                                                         gameplay));
    ASSERT_TRUE(bytes) << (bytes ? "" : bytes.error().message);

    std::vector<AssetFormat::World2DEntityDesc> storage;
    auto snapshot = AssetFormat::parseWorld2DSnapshot(*bytes, storage);
    ASSERT_TRUE(snapshot) << (snapshot ? "" : snapshot.error().message);
    ASSERT_EQ(snapshot->entities.size(), 4U);
    EXPECT_EQ(snapshot->entities[0].stableEntityId, 100U);
    EXPECT_EQ(snapshot->entities[0].nodeKind, AssetFormat::World2DNodeKind::Camera2D);
    EXPECT_EQ(snapshot->entities[1].stableEntityId, 200U);
    EXPECT_EQ(snapshot->entities[1].parentStableEntityId, 100U);
    EXPECT_EQ(snapshot->entities[1].nodeKind, AssetFormat::World2DNodeKind::AnimatedSprite2D);
    EXPECT_EQ(snapshot->entities[2].stableEntityId, 300U);
    EXPECT_EQ(snapshot->entities[2].nodeKind, AssetFormat::World2DNodeKind::PointLight2D);
    EXPECT_EQ(snapshot->entities[3].stableEntityId, 400U);
    EXPECT_EQ(snapshot->entities[3].nodeKind, AssetFormat::World2DNodeKind::ShadowOccluder2D);

    World restored = makeWorld();
    auto bindings = instantiateWorld2DSnapshot(restored, *snapshot, resolver());
    ASSERT_TRUE(bindings) << (bindings ? "" : bindings.error().message);
    ASSERT_EQ(bindings->size(), 4U);
    EXPECT_NE((*bindings)[0].entity.owner(), root.owner());
    EXPECT_EQ(restored.parent((*bindings)[1].entity), (*bindings)[0].entity);
    ASSERT_NE(restored.spriteRenderer2D((*bindings)[1].entity), nullptr);
    EXPECT_EQ(restored.spriteRenderer2D((*bindings)[1].entity)->sprite, sprite_);
    EXPECT_EQ(restored.spriteRenderer2D((*bindings)[1].entity)->normalTexture, texture_);
    const SpriteAnimationBinding2D* restoredAnimation =
        restored.spriteAnimationBinding2D((*bindings)[1].entity);
    ASSERT_NE(restoredAnimation, nullptr);
    EXPECT_EQ(restoredAnimation->clip, clip_);
    EXPECT_FLOAT_EQ(restoredAnimation->playbackSpeed, 1.5F);
    EXPECT_FALSE(restoredAnimation->autoPlay);

    auto recaptured = captureWorld2DSnapshotBytes(
        restored, captureConfig([&bindings](EntityId entity) { return stableIdFor(*bindings, entity); }, gameplay));
    ASSERT_TRUE(recaptured) << (recaptured ? "" : recaptured.error().message);
    EXPECT_EQ(*recaptured, *bytes);
}

TEST_F(World2DSnapshotSceneTests, UnresolvedAnimationClipFailsClosedBeforeMutation)
{
    const std::array entities{
        AssetFormat::World2DEntityDesc{
            .stableEntityId = 1,
            .nodeKind = AssetFormat::World2DNodeKind::AnimatedSprite2D,
            .sprite = AssetFormat::World2DSpriteDesc{.spriteId = spriteId_},
            .spriteAnimation =
                AssetFormat::World2DSpriteAnimationDesc{.clipId = assetId(99)},
        },
    };
    auto bytes = AssetFormat::writeWorld2DSnapshotBytes(AssetFormat::World2DSnapshotDesc{.entities = entities});
    ASSERT_TRUE(bytes) << (bytes ? "" : bytes.error().message);
    std::vector<AssetFormat::World2DEntityDesc> storage;
    auto snapshot = AssetFormat::parseWorld2DSnapshot(*bytes, storage);
    ASSERT_TRUE(snapshot);

    World world = makeWorld();
    auto unresolvedClip = instantiateWorld2DSnapshot(world, *snapshot, resolver());
    ASSERT_FALSE(unresolvedClip);
    EXPECT_EQ(unresolvedClip.error().code, SceneErrorCode::UnresolvedSprite);
    EXPECT_EQ(world.entityCount(), 0U);

    // A missing binding on the sprite entity keeps working without the clip resolver.
    World2DSnapshotAssetResolver noClipResolver = resolver();
    noClipResolver.resolveAnimationClip = {};
    const std::array plainEntities{
        AssetFormat::World2DEntityDesc{
            .stableEntityId = 1,
            .nodeKind = AssetFormat::World2DNodeKind::Sprite2D,
            .sprite = AssetFormat::World2DSpriteDesc{.spriteId = spriteId_},
        },
    };
    auto plainBytes = AssetFormat::writeWorld2DSnapshotBytes(
        AssetFormat::World2DSnapshotDesc{.entities = plainEntities});
    ASSERT_TRUE(plainBytes);
    std::vector<AssetFormat::World2DEntityDesc> plainStorage;
    auto plainSnapshot = AssetFormat::parseWorld2DSnapshot(*plainBytes, plainStorage);
    ASSERT_TRUE(plainSnapshot);
    auto restored = instantiateWorld2DSnapshot(world, *plainSnapshot, noClipResolver);
    ASSERT_TRUE(restored) << (restored ? "" : restored.error().message);
}

TEST_F(World2DSnapshotSceneTests, OrdersParentsFirstThenStableId)
{
    World world = makeWorld();
    const EntityId secondChild = world.createEntity().value();
    const EntityId firstChild = world.createEntity().value();
    const EntityId root = world.createEntity().value();
    ASSERT_TRUE(world.setParent(firstChild, root, ReparentMode::KeepLocal));
    ASSERT_TRUE(world.setParent(secondChild, root, ReparentMode::KeepLocal));

    auto bytes = captureWorld2DSnapshotBytes(world, captureConfig([root, firstChild, secondChild](EntityId entity) {
                                                 if (entity == root)
                                                     return 100U;
                                                 if (entity == firstChild)
                                                     return 10U;
                                                 if (entity == secondChild)
                                                     return 20U;
                                                 return 0U;
                                             }));
    ASSERT_TRUE(bytes);
    std::vector<AssetFormat::World2DEntityDesc> storage;
    auto snapshot = AssetFormat::parseWorld2DSnapshot(*bytes, storage);
    ASSERT_TRUE(snapshot);
    ASSERT_EQ(snapshot->entities.size(), 3U);
    EXPECT_EQ(snapshot->entities[0].stableEntityId, 100U);
    EXPECT_EQ(snapshot->entities[1].stableEntityId, 10U);
    EXPECT_EQ(snapshot->entities[2].stableEntityId, 20U);
}

TEST_F(World2DSnapshotSceneTests, PreflightFailuresPreserveExistingWorld)
{
    const std::array entities{
        AssetFormat::World2DEntityDesc{
            .stableEntityId = 1,
            .nodeKind = AssetFormat::World2DNodeKind::Sprite2D,
            .sprite = AssetFormat::World2DSpriteDesc{.spriteId = spriteId_},
        },
    };
    auto bytes = AssetFormat::writeWorld2DSnapshotBytes(AssetFormat::World2DSnapshotDesc{.entities = entities});
    ASSERT_TRUE(bytes);
    std::vector<AssetFormat::World2DEntityDesc> storage;
    auto snapshot = AssetFormat::parseWorld2DSnapshot(*bytes, storage);
    ASSERT_TRUE(snapshot);

    World capacityWorld = makeWorld(1);
    const EntityId existing = capacityWorld.createEntity().value();
    auto overCapacity = instantiateWorld2DSnapshot(capacityWorld, *snapshot, resolver());
    ASSERT_FALSE(overCapacity);
    EXPECT_EQ(overCapacity.error().code, SceneErrorCode::CapacityExceeded);
    EXPECT_EQ(capacityWorld.entityCount(), 1U);
    EXPECT_TRUE(capacityWorld.contains(existing));

    World unresolvedWorld = makeWorld();
    const EntityId unresolvedExisting = unresolvedWorld.createEntity().value();
    auto unresolved = instantiateWorld2DSnapshot(unresolvedWorld, *snapshot);
    ASSERT_FALSE(unresolved);
    EXPECT_EQ(unresolved.error().code, SceneErrorCode::UnresolvedSprite);
    EXPECT_EQ(unresolvedWorld.entityCount(), 1U);
    EXPECT_TRUE(unresolvedWorld.contains(unresolvedExisting));
}

TEST_F(World2DSnapshotSceneTests, TransformPublicationFailureRollsBackEveryCreatedEntity)
{
    constexpr float HalfAngle = 0.25F;
    const std::array entities{
        AssetFormat::World2DEntityDesc{
            .stableEntityId = 1,
            .scaleX = 2.0F,
            .scaleY = 3.0F,
        },
        AssetFormat::World2DEntityDesc{
            .stableEntityId = 2,
            .parentStableEntityId = 1,
            .rotationZ = std::sin(HalfAngle),
            .rotationW = std::cos(HalfAngle),
        },
    };
    auto bytes = AssetFormat::writeWorld2DSnapshotBytes(AssetFormat::World2DSnapshotDesc{.entities = entities});
    ASSERT_TRUE(bytes);
    std::vector<AssetFormat::World2DEntityDesc> storage;
    auto snapshot = AssetFormat::parseWorld2DSnapshot(*bytes, storage);
    ASSERT_TRUE(snapshot);

    World world = makeWorld();
    const EntityId existing = world.createEntity().value();
    auto restored = instantiateWorld2DSnapshot(world, *snapshot);
    ASSERT_FALSE(restored);
    EXPECT_EQ(restored.error().code, SceneErrorCode::UnsupportedTransformComposition);
    EXPECT_EQ(world.entityCount(), 1U);
    EXPECT_TRUE(world.contains(existing));
    ASSERT_TRUE(world.updateWorldTransforms());
}

TEST_F(World2DSnapshotSceneTests, CaptureRejectsUnsupported3DComponents)
{
    World world = makeWorld();
    const EntityId entity = world.createEntity().value();
    ASSERT_TRUE(world.setPerspectiveCamera3D(entity, PerspectiveCamera3D{}));

    auto captured = captureWorld2DSnapshotBytes(
        world, captureConfig([entity](EntityId candidate) { return candidate == entity ? 1U : 0U; }));
    ASSERT_FALSE(captured);
    EXPECT_EQ(captured.error().code, SceneErrorCode::InvalidComponent);
}

} // namespace
} // namespace Tina::Scene
