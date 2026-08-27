#include <tina/editor/EditorNodePropertyOperations.hpp>

#include <tina/editor/EditorErrors.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <string_view>
#include <vector>

namespace Tina::Editor {
namespace {

[[nodiscard]] Core::AssetId testAssetId(char digit)
{
    std::array<char, 32> text{};
    text.fill(digit);
    const auto parsed =
        Core::AssetId::parseCanonical(std::string_view{text.data(), text.size()});
    EXPECT_TRUE(parsed.has_value());
    return *parsed;
}

[[nodiscard]] World2DAuthoringDocument createWorld2D()
{
    auto document = World2DAuthoringDocument::Create();
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

[[nodiscard]] World3DAuthoringDocument createWorld3D()
{
    auto document = World3DAuthoringDocument::Create();
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

[[nodiscard]] std::vector<AssetFormat::World2DEntityDesc>
world2DEntities(const World2DAuthoringDocument& document)
{
    std::vector<AssetFormat::World2DEntityDesc> storage;
    auto snapshot = document.parseCurrentSnapshot(storage);
    EXPECT_TRUE(snapshot) << (snapshot ? "" : snapshot.error().message);
    return storage;
}

[[nodiscard]] std::vector<AssetFormat::PrefabNodeView>
world3DNodes(const World3DAuthoringDocument& document)
{
    std::vector<AssetFormat::PrefabNodeView> storage;
    auto prefab = document.parseCurrentPrefab(storage);
    EXPECT_TRUE(prefab) << (prefab ? "" : prefab.error().message);
    return storage;
}

TEST(EditorNodePropertyOperationsTests, SpritePropertiesPublishWithoutChangingNodeKind)
{
    auto document = createWorld2D();
    auto added = addWorld2DNode(
        document, World2DNodeTemplate::Sprite2D, 0U,
        {.spriteId = testAssetId('a')});
    ASSERT_TRUE(added) << added.error().message;
    const std::array ids{added->primaryStableId};
    const Core::u64 revision = document.revision();

    auto edited = applyWorld2DSpriteNodeProperties(
        document, ids, {.sizeX = 3.0F, .visible = false});
    ASSERT_TRUE(edited) << edited.error().message;
    EXPECT_EQ(edited->affectedItemCount, 1U);
    EXPECT_EQ(document.revision(), revision + 1U);

    const auto entities = world2DEntities(document);
    ASSERT_EQ(entities.size(), 1U);
    auto nodeKind = classifyWorld2DNodeTemplate(entities.front());
    ASSERT_TRUE(nodeKind) << nodeKind.error().message;
    EXPECT_EQ(*nodeKind, World2DNodeTemplate::Sprite2D);
    ASSERT_TRUE(entities.front().sprite.has_value());
    EXPECT_FLOAT_EQ(entities.front().sprite->sizeX, 3.0F);
    EXPECT_FALSE(entities.front().sprite->visible);
}

TEST(EditorNodePropertyOperationsTests, PropertyEditNoOpPublishesNoRevision)
{
    auto document = createWorld2D();
    auto added = addWorld2DNode(
        document, World2DNodeTemplate::Camera2D);
    ASSERT_TRUE(added);
    const std::array ids{added->primaryStableId};
    const auto entities = world2DEntities(document);
    ASSERT_TRUE(entities.front().camera.has_value());
    const Core::u64 revision = document.revision();

    auto edited = applyWorld2DCameraNodeProperties(
        document, ids, {.active = entities.front().camera->active});
    ASSERT_TRUE(edited) << edited.error().message;
    EXPECT_EQ(edited->affectedItemCount, 0U);
    EXPECT_EQ(document.revision(), revision);
}

TEST(EditorNodePropertyOperationsTests, KindMismatchFailsClosed)
{
    auto document = createWorld2D();
    auto added = addWorld2DNode(document, World2DNodeTemplate::Node2D);
    ASSERT_TRUE(added);
    const std::array ids{added->primaryStableId};
    const std::vector<std::byte> before(document.snapshotBytes().begin(),
                                        document.snapshotBytes().end());

    auto edited = applyWorld2DPointLightNodeProperties(
        document, ids, {.intensity = 2.0F});
    ASSERT_FALSE(edited);
    EXPECT_EQ(edited.error().code, EditorErrorCode::NodePropertyUnavailable);
    EXPECT_EQ(std::vector<std::byte>(document.snapshotBytes().begin(),
                                     document.snapshotBytes().end()),
              before);
}

TEST(EditorNodePropertyOperationsTests, MultiSelectionPublishesOneRevision)
{
    auto document = createWorld2D();
    auto first = addWorld2DNode(
        document, World2DNodeTemplate::PointLight2D);
    auto second = addWorld2DNode(
        document, World2DNodeTemplate::PointLight2D);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    const std::array ids{first->primaryStableId, second->primaryStableId};
    const Core::u64 revision = document.revision();

    auto edited = applyWorld2DPointLightNodeProperties(
        document, ids, {.intensity = 4.0F, .active = false});
    ASSERT_TRUE(edited) << edited.error().message;
    EXPECT_EQ(edited->affectedItemCount, 2U);
    EXPECT_EQ(document.revision(), revision + 1U);
}

TEST(EditorNodePropertyOperationsTests, PhysicsBodyPropertiesPublishAsOneRevision)
{
    auto document = createWorld2D();
    auto added = addWorld2DNode(document, World2DNodeTemplate::RigidBody2D);
    ASSERT_TRUE(added) << added.error().message;
    const std::array ids{added->primaryStableId};
    const Core::u64 revision = document.revision();

    auto edited = applyWorld2DPhysicsBodyNodeProperties(
        document, ids,
        {.linearVelocityX = 3.0F,
         .linearDamping = 0.25F,
         .gravityScale = 0.5F,
         .enabled = false});
    ASSERT_TRUE(edited) << edited.error().message;
    EXPECT_EQ(edited->affectedItemCount, 1U);
    EXPECT_EQ(document.revision(), revision + 1U);

    const auto entities = world2DEntities(document);
    ASSERT_TRUE(entities.front().physicsBody.has_value());
    EXPECT_FLOAT_EQ(entities.front().physicsBody->linearVelocityX, 3.0F);
    EXPECT_FLOAT_EQ(entities.front().physicsBody->linearDamping, 0.25F);
    EXPECT_FLOAT_EQ(entities.front().physicsBody->gravityScale, 0.5F);
    EXPECT_FALSE(entities.front().physicsBody->enabled);
}

TEST(EditorNodePropertyOperationsTests, PhysicsShapePropertiesValidateAndPublish)
{
    auto document = createWorld2D();
    auto body = addWorld2DNode(document, World2DNodeTemplate::StaticBody2D);
    ASSERT_TRUE(body);
    auto added = addWorld2DNode(
        document, World2DNodeTemplate::CollisionShape2D, body->primaryStableId);
    ASSERT_TRUE(added) << added.error().message;
    const std::array ids{added->primaryStableId};

    auto edited = applyWorld2DPhysicsShapeNodeProperties(
        document, ids,
        {.kind = AssetFormat::World2DPhysicsShapeKind::Circle,
         .radius = 2.0F,
         .density = 2.5F,
         .friction = 0.2F,
         .restitution = 0.75F});
    ASSERT_TRUE(edited) << edited.error().message;
    const auto entities = world2DEntities(document);
    const auto shape = std::find_if(
        entities.begin(), entities.end(), [&](const auto& entity) {
            return entity.stableEntityId == added->primaryStableId;
        });
    ASSERT_NE(shape, entities.end());
    ASSERT_TRUE(shape->physicsShape.has_value());
    EXPECT_EQ(shape->physicsShape->kind,
              AssetFormat::World2DPhysicsShapeKind::Circle);
    EXPECT_FLOAT_EQ(shape->physicsShape->radius, 2.0F);
    EXPECT_FLOAT_EQ(shape->physicsShape->density, 2.5F);
    EXPECT_FLOAT_EQ(shape->physicsShape->friction, 0.2F);
    EXPECT_FLOAT_EQ(shape->physicsShape->restitution, 0.75F);
}

TEST(EditorNodePropertyOperationsTests, PhysicsPropertiesRejectInvalidValues)
{
    auto document = createWorld2D();
    auto added = addWorld2DNode(document, World2DNodeTemplate::RigidBody2D);
    ASSERT_TRUE(added);
    const std::array ids{added->primaryStableId};
    const Core::u64 revision = document.revision();
    auto invalid = applyWorld2DPhysicsBodyNodeProperties(
        document, ids,
        {.linearVelocityX = std::numeric_limits<float>::quiet_NaN(),
         .linearDamping = -1.0F});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, EditorErrorCode::InvalidAuthoringOperation);
    EXPECT_EQ(document.revision(), revision);
}

TEST(EditorNodePropertyOperationsTests, PhysicsShapeKindMismatchFailsClosed)
{
    auto document = createWorld2D();
    auto added = addWorld2DNode(document, World2DNodeTemplate::Node2D);
    ASSERT_TRUE(added);
    const std::array ids{added->primaryStableId};
    const Core::u64 revision = document.revision();
    auto invalid = applyWorld2DPhysicsShapeNodeProperties(
        document, ids, {.radius = 2.0F});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, EditorErrorCode::NodePropertyUnavailable);
    EXPECT_EQ(document.revision(), revision);
}

TEST(EditorNodePropertyOperationsTests, AnimatedSpritePropertiesKeepCompleteNodeKind)
{
    auto document = createWorld2D();
    auto added = addWorld2DNode(
        document, World2DNodeTemplate::AnimatedSprite2D, 0U,
        {.spriteId = testAssetId('a'),
         .animationClipId = testAssetId('b')});
    ASSERT_TRUE(added);
    const std::array ids{added->primaryStableId};

    auto edited = applyWorld2DAnimatedSpriteNodeProperties(
        document, ids, {.playbackSpeed = 1.5F, .autoPlay = false});
    ASSERT_TRUE(edited) << edited.error().message;
    const auto entities = world2DEntities(document);
    auto nodeKind = classifyWorld2DNodeTemplate(entities.front());
    ASSERT_TRUE(nodeKind) << nodeKind.error().message;
    EXPECT_EQ(*nodeKind, World2DNodeTemplate::AnimatedSprite2D);
    ASSERT_TRUE(entities.front().spriteAnimation.has_value());
    EXPECT_FLOAT_EQ(entities.front().spriteAnimation->playbackSpeed, 1.5F);
}

TEST(EditorNodePropertyOperationsTests, MeshPropertiesKeepMeshNodeKind)
{
    auto document = createWorld3D();
    auto added = addWorld3DNode(
        document, World3DNodeTemplate::Mesh3D, 0U,
        {.meshId = testAssetId('c'), .materialId = testAssetId('d')});
    ASSERT_TRUE(added);
    const std::array ids{added->primaryStableId};

    auto edited = applyWorld3DMeshNodeProperties(
        document, ids, {.visible = false});
    ASSERT_TRUE(edited) << edited.error().message;
    const auto nodes = world3DNodes(document);
    ASSERT_EQ(nodes.size(), 2U);
    const auto node = std::find_if(
        nodes.begin(), nodes.end(), [&](const auto& candidate) {
            return candidate.stableNodeId == added->primaryStableId;
        });
    ASSERT_NE(node, nodes.end());
    auto nodeKind = classifyWorld3DNodeTemplate(*node);
    ASSERT_TRUE(nodeKind) << nodeKind.error().message;
    EXPECT_EQ(*nodeKind, World3DNodeTemplate::Mesh3D);
    EXPECT_FALSE(node->visible);
}

// Authoring any UV component must set the UvRect override, or the runtime would
// keep sampling the full texture and a spritesheet slice would never appear.
TEST(EditorNodePropertyOperationsTests, SpriteUvRectSetsOverrideAndRejectsInvertedRects)
{
    auto document = createWorld2D();
    auto added = addWorld2DNode(
        document, World2DNodeTemplate::Sprite2D, 0U,
        {.spriteId = testAssetId('a')});
    ASSERT_TRUE(added) << added.error().message;
    const std::array ids{added->primaryStableId};

    auto edited = applyWorld2DSpriteNodeProperties(
        document, ids,
        {.uvU0 = 0.25F, .uvV0 = 0.5F, .uvU1 = 0.75F, .uvV1 = 1.0F});
    ASSERT_TRUE(edited) << edited.error().message;
    EXPECT_EQ(edited->affectedItemCount, 1U);
    const auto entities = world2DEntities(document);
    const auto entity = std::find_if(
        entities.begin(), entities.end(), [&](const auto& candidate) {
            return candidate.stableEntityId == added->primaryStableId;
        });
    ASSERT_NE(entity, entities.end());
    ASSERT_TRUE(entity->sprite.has_value());
    EXPECT_TRUE(hasFlag(entity->sprite->overrides,
                        AssetFormat::World2DSpriteOverrideFlags::UvRect));
    EXPECT_FLOAT_EQ(entity->sprite->uvU0, 0.25F);
    EXPECT_FLOAT_EQ(entity->sprite->uvV1, 1.0F);

    const Core::u64 revision = document.revision();
    // u1 below u0 would produce a degenerate rect; the document must not move.
    auto inverted = applyWorld2DSpriteNodeProperties(
        document, ids, {.uvU0 = 0.9F, .uvU1 = 0.1F});
    ASSERT_FALSE(inverted);
    EXPECT_EQ(inverted.error().code,
              EditorErrorCode::InvalidAuthoringOperation);
    EXPECT_EQ(document.revision(), revision);

    auto outOfRange = applyWorld2DSpriteNodeProperties(
        document, ids, {.uvV1 = 1.5F});
    ASSERT_FALSE(outOfRange);
    EXPECT_EQ(document.revision(), revision);
}

// Resource nodes could previously only receive an asset at create time.
TEST(EditorNodePropertyOperationsTests, ResourcePropertiesRebindAssetAndFailClosed)
{
    auto document = createWorld2D();
    auto added = addWorld2DNode(
        document, World2DNodeTemplate::TileMap2D, 0U,
        {.resourceId = testAssetId('a')});
    ASSERT_TRUE(added) << added.error().message;
    const std::array ids{added->primaryStableId};

    auto edited = applyWorld2DResourceNodeProperties(
        document, ids, {.assetId = testAssetId('b'), .active = false});
    ASSERT_TRUE(edited) << edited.error().message;
    EXPECT_EQ(edited->affectedItemCount, 1U);
    const auto entities = world2DEntities(document);
    const auto entity = std::find_if(
        entities.begin(), entities.end(), [&](const auto& candidate) {
            return candidate.stableEntityId == added->primaryStableId;
        });
    ASSERT_NE(entity, entities.end());
    ASSERT_TRUE(entity->resource.has_value());
    EXPECT_EQ(entity->resource->assetId, testAssetId('b'));
    EXPECT_FALSE(entity->resource->active);
    auto nodeKind = classifyWorld2DNodeTemplate(*entity);
    ASSERT_TRUE(nodeKind) << nodeKind.error().message;
    EXPECT_EQ(*nodeKind, World2DNodeTemplate::TileMap2D);

    const Core::u64 revision = document.revision();
    auto zeroId = applyWorld2DResourceNodeProperties(
        document, ids, {.assetId = Core::AssetId{}});
    ASSERT_FALSE(zeroId);
    EXPECT_EQ(document.revision(), revision);

    // A Sprite2D owns no resource payload, so the edit must be refused.
    auto sprite = addWorld2DNode(
        document, World2DNodeTemplate::Sprite2D, 0U,
        {.spriteId = testAssetId('c')});
    ASSERT_TRUE(sprite) << sprite.error().message;
    const std::array spriteIds{sprite->primaryStableId};
    auto mismatch = applyWorld2DResourceNodeProperties(
        document, spriteIds, {.active = false});
    ASSERT_FALSE(mismatch);
    EXPECT_EQ(mismatch.error().code, EditorErrorCode::NodePropertyUnavailable);
}

} // namespace
} // namespace Tina::Editor
