#include <tina/editor/EditorNodePropertyOperations.hpp>

#include <tina/editor/EditorErrors.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
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
    ASSERT_EQ(nodes.size(), 1U);
    auto nodeKind = classifyWorld3DNodeTemplate(nodes.front());
    ASSERT_TRUE(nodeKind) << nodeKind.error().message;
    EXPECT_EQ(*nodeKind, World3DNodeTemplate::Mesh3D);
    EXPECT_FALSE(nodes.front().visible);
}

} // namespace
} // namespace Tina::Editor
