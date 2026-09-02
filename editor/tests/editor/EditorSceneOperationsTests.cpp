#include <tina/editor/EditorSceneOperations.hpp>

#include <tina/editor/EditorErrors.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace Tina::Editor {
namespace {

using AssetFormat::PrefabNodeView;
using AssetFormat::World2DEntityDesc;

[[nodiscard]] World2DAuthoringDocument createWorld2D(
    World2DAuthoringDocumentConfig config = {})
{
    auto document = World2DAuthoringDocument::Create(config);
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

[[nodiscard]] World3DAuthoringDocument createWorld3D(
    World3DAuthoringDocumentConfig config = {})
{
    auto document = World3DAuthoringDocument::Create(config);
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

[[nodiscard]] std::vector<World2DEntityDesc> world2DEntities(
    const World2DAuthoringDocument& document)
{
    std::vector<World2DEntityDesc> storage;
    auto snapshot = document.parseCurrentSnapshot(storage);
    EXPECT_TRUE(snapshot) << (snapshot ? "" : snapshot.error().message);
    return storage;
}

[[nodiscard]] const World2DEntityDesc* findEntity(
    const std::vector<World2DEntityDesc>& entities, Core::u32 stableId)
{
    const auto found = std::find_if(
        entities.begin(), entities.end(), [stableId](const auto& entity) {
            return entity.stableEntityId == stableId;
        });
    return found == entities.end() ? nullptr : &*found;
}

[[nodiscard]] std::vector<PrefabNodeView> world3DNodes(
    const World3DAuthoringDocument& document)
{
    std::vector<PrefabNodeView> storage;
    auto prefab = document.parseCurrentPrefab(storage);
    EXPECT_TRUE(prefab) << (prefab ? "" : prefab.error().message);
    return storage;
}

[[nodiscard]] Core::i32 findNodeIndex(
    const std::vector<PrefabNodeView>& nodes, Core::u32 stableId)
{
    const auto found = std::find_if(
        nodes.begin(), nodes.end(), [stableId](const auto& node) {
            return node.stableNodeId == stableId;
        });
    return found == nodes.end()
               ? -1
               : static_cast<Core::i32>(std::distance(nodes.begin(), found));
}

[[nodiscard]] Core::u32 parentStableId(
    const std::vector<PrefabNodeView>& nodes, Core::u32 stableId)
{
    const Core::i32 index = findNodeIndex(nodes, stableId);
    if (index < 0) {
        return 0;
    }
    const Core::i32 parentIndex = nodes[static_cast<Core::usize>(index)].parentIndex;
    return parentIndex < 0
               ? 0
               : nodes[static_cast<Core::usize>(parentIndex)].stableNodeId;
}

TEST(EditorSceneOperationsTests, World2DCommandsPublishOneRevisionAndKeepHierarchy)
{
    auto document = createWorld2D();
    Core::u64 revision = document.revision();

    auto root = addWorld2DNode(document, World2DNodeTemplate::Node2D);
    ASSERT_TRUE(root);
    EXPECT_EQ(root->primaryStableId, 1U);
    EXPECT_EQ(root->affectedItemCount, 1U);
    EXPECT_EQ(document.revision(), ++revision);

    auto child = addWorld2DNode(
        document, World2DNodeTemplate::Node2D, root->primaryStableId);
    ASSERT_TRUE(child);
    EXPECT_EQ(child->primaryStableId, 2U);
    EXPECT_EQ(document.revision(), ++revision);

    auto duplicate =
        duplicateWorld2DNodeSubtree(document, root->primaryStableId);
    ASSERT_TRUE(duplicate);
    EXPECT_EQ(duplicate->primaryStableId, 3U);
    EXPECT_EQ(duplicate->affectedItemCount, 2U);
    EXPECT_EQ(document.revision(), ++revision);
    auto entities = world2DEntities(document);
    ASSERT_EQ(entities.size(), 4U);
    ASSERT_NE(findEntity(entities, 3), nullptr);
    ASSERT_NE(findEntity(entities, 4), nullptr);
    EXPECT_EQ(findEntity(entities, 3)->parentStableEntityId, 0U);
    EXPECT_EQ(findEntity(entities, 4)->parentStableEntityId, 3U);

    ASSERT_TRUE(reparentWorld2DNode(document, 3, 2));
    EXPECT_EQ(document.revision(), ++revision);
    entities = world2DEntities(document);
    ASSERT_NE(findEntity(entities, 3), nullptr);
    EXPECT_EQ(findEntity(entities, 3)->parentStableEntityId, 2U);

    ASSERT_TRUE(reparentWorld2DNode(document, 3, 2));
    EXPECT_EQ(document.revision(), revision);

    auto removed = deleteWorld2DNodeSubtree(document, 3);
    ASSERT_TRUE(removed);
    EXPECT_EQ(removed->primaryStableId, 2U);
    EXPECT_EQ(removed->affectedItemCount, 2U);
    EXPECT_EQ(document.revision(), ++revision);
    entities = world2DEntities(document);
    ASSERT_EQ(entities.size(), 2U);
    EXPECT_NE(findEntity(entities, 1), nullptr);
    EXPECT_NE(findEntity(entities, 2), nullptr);
}

TEST(EditorSceneOperationsTests, World2DFailuresPreserveCanonicalStateAndHistory)
{
    auto document = createWorld2D({.entityCapacity = 2});
    ASSERT_TRUE(addWorld2DNode(document, World2DNodeTemplate::Node2D));
    ASSERT_TRUE(addWorld2DNode(document, World2DNodeTemplate::Node2D, 1));
    const auto beforeBytes = std::vector(document.snapshotBytes().begin(),
                                         document.snapshotBytes().end());
    const Core::u64 beforeRevision = document.revision();
    const Core::usize beforeHistory = document.historyEntryCount();
    const Core::usize beforeUndo = document.undoDepth();

    auto full = addWorld2DNode(document, World2DNodeTemplate::Node2D);
    ASSERT_FALSE(full);
    EXPECT_EQ(full.error().code, EditorErrorCode::DocumentCapacityExceeded);
    auto duplicate = duplicateWorld2DNodeSubtree(document, 1);
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code,
              EditorErrorCode::DocumentCapacityExceeded);
    auto cycle = reparentWorld2DNode(document, 1, 2);
    ASSERT_FALSE(cycle);
    EXPECT_EQ(cycle.error().code,
              EditorErrorCode::InvalidAuthoringOperation);
    EXPECT_FALSE(reparentWorld2DNode(document, 99, 0));
    EXPECT_FALSE(deleteWorld2DNodeSubtree(document, 99));

    EXPECT_EQ(std::vector(document.snapshotBytes().begin(),
                          document.snapshotBytes().end()),
              beforeBytes);
    EXPECT_EQ(document.revision(), beforeRevision);
    EXPECT_EQ(document.historyEntryCount(), beforeHistory);
    EXPECT_EQ(document.undoDepth(), beforeUndo);
}

TEST(EditorSceneOperationsTests, World3DCommandsPublishOneRevisionAndKeepHierarchy)
{
    auto document = createWorld3D();
    Core::u64 revision = document.revision();

    auto child = addWorld3DNode(
        document, World3DNodeTemplate::Node3D, 1);
    ASSERT_TRUE(child);
    EXPECT_EQ(child->primaryStableId, 2U);
    EXPECT_EQ(document.revision(), ++revision);
    auto secondRoot = addWorld3DNode(document, World3DNodeTemplate::Node3D);
    ASSERT_TRUE(secondRoot);
    EXPECT_EQ(secondRoot->primaryStableId, 3U);
    EXPECT_EQ(document.revision(), ++revision);

    auto duplicate = duplicateWorld3DNodeSubtree(document, 1);
    ASSERT_TRUE(duplicate);
    EXPECT_EQ(duplicate->primaryStableId, 4U);
    EXPECT_EQ(duplicate->affectedItemCount, 2U);
    EXPECT_EQ(document.revision(), ++revision);
    auto nodes = world3DNodes(document);
    ASSERT_EQ(nodes.size(), 5U);
    EXPECT_EQ(parentStableId(nodes, 4), 0U);
    EXPECT_EQ(parentStableId(nodes, 5), 4U);

    ASSERT_TRUE(reparentWorld3DNode(document, 4, 3));
    EXPECT_EQ(document.revision(), ++revision);
    nodes = world3DNodes(document);
    EXPECT_EQ(parentStableId(nodes, 4), 3U);
    EXPECT_EQ(parentStableId(nodes, 5), 4U);

    ASSERT_TRUE(reparentWorld3DNode(document, 4, 3));
    EXPECT_EQ(document.revision(), revision);

    auto removed = deleteWorld3DNodeSubtree(document, 4);
    ASSERT_TRUE(removed);
    EXPECT_EQ(removed->primaryStableId, 3U);
    EXPECT_EQ(removed->affectedItemCount, 2U);
    EXPECT_EQ(document.revision(), ++revision);
    nodes = world3DNodes(document);
    ASSERT_EQ(nodes.size(), 3U);
    EXPECT_GE(findNodeIndex(nodes, 1), 0);
    EXPECT_GE(findNodeIndex(nodes, 2), 0);
    EXPECT_GE(findNodeIndex(nodes, 3), 0);
}

TEST(EditorSceneOperationsTests, World3DFailuresAndNoOpPreserveCanonicalState)
{
    auto document = createWorld3D();
    ASSERT_TRUE(addWorld3DNode(document, World3DNodeTemplate::Node3D, 1));
    const auto beforeBytes = std::vector(document.payloadBytes().begin(),
                                         document.payloadBytes().end());
    const Core::u64 beforeRevision = document.revision();
    const Core::usize beforeHistory = document.historyEntryCount();

    ASSERT_TRUE(reparentWorld3DNode(document, 2, 1));
    EXPECT_EQ(document.revision(), beforeRevision);
    EXPECT_FALSE(addWorld3DNode(
        document, World3DNodeTemplate::Node3D, 99));
    EXPECT_FALSE(duplicateWorld3DNodeSubtree(document, 99));
    auto cycle = reparentWorld3DNode(document, 1, 2);
    ASSERT_FALSE(cycle);
    EXPECT_EQ(cycle.error().code,
              EditorErrorCode::InvalidAuthoringOperation);
    auto eraseAll = deleteWorld3DNodeSubtree(document, 1);
    ASSERT_FALSE(eraseAll);
    EXPECT_EQ(eraseAll.error().code,
              EditorErrorCode::InvalidAuthoringOperation);

    EXPECT_EQ(std::vector(document.payloadBytes().begin(),
                          document.payloadBytes().end()),
              beforeBytes);
    EXPECT_EQ(document.revision(), beforeRevision);
    EXPECT_EQ(document.historyEntryCount(), beforeHistory);

    auto fullDocument = createWorld3D({.nodeCapacity = 1});
    const Core::u64 fullRevision = fullDocument.revision();
    auto full = addWorld3DNode(
        fullDocument, World3DNodeTemplate::Node3D);
    ASSERT_FALSE(full);
    EXPECT_EQ(full.error().code, EditorErrorCode::DocumentCapacityExceeded);
    EXPECT_EQ(fullDocument.revision(), fullRevision);
    EXPECT_EQ(fullDocument.nodeCount(), 1U);
}

} // namespace
} // namespace Tina::Editor
