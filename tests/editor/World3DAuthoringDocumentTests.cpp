#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/editor/EditorErrors.hpp>
#include <tina/editor/World3DAuthoringDocument.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace Tina::Editor {
namespace {

using AssetFormat::PrefabNodeDesc;
using AssetFormat::PrefabNodeView;
using AssetFormat::PrefabPayloadDesc;

[[nodiscard]] World3DAuthoringDocument createDocument(World3DAuthoringDocumentConfig config = {})
{
    auto document = World3DAuthoringDocument::Create(config);
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

[[nodiscard]] std::vector<PrefabNodeView> nodes(const World3DAuthoringDocument& document)
{
    std::vector<PrefabNodeView> storage;
    auto prefab = document.parseCurrentPrefab(storage);
    EXPECT_TRUE(prefab) << (prefab ? "" : prefab.error().message);
    return storage;
}

TEST(World3DAuthoringDocumentTests, CreatesCanonicalPrefabV2RootAndRejectsInvalidConfig)
{
    auto document = World3DAuthoringDocument::Create();
    ASSERT_TRUE(document);
    EXPECT_EQ(document->schemaVersion(), AssetFormat::PrefabWire::SchemaVersion);
    EXPECT_EQ(document->nodeCount(), 1U);
    EXPECT_EQ(document->historyEntryCount(), 1U);
    EXPECT_FALSE(document->canUndo());

    const auto root = nodes(*document);
    ASSERT_EQ(root.size(), 1U);
    EXPECT_EQ(root[0].stableNodeId, World3DAuthoringLimits::DefaultRootStableNodeId);
    EXPECT_EQ(root[0].parentIndex, -1);

    auto invalid = World3DAuthoringDocument::Create({.historyEntryCapacity = 1});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, EditorErrorCode::InvalidConfiguration);
}

TEST(World3DAuthoringDocumentTests, ReplacesUpsertsAndUndoRedoCanonicalRevisions)
{
    auto document = createDocument();
    const std::array initial{
        PrefabNodeDesc{.stableNodeId = 10},
        PrefabNodeDesc{.stableNodeId = 20, .parentIndex = 0, .positionX = 2.0F},
    };
    ASSERT_TRUE(document.replace(PrefabPayloadDesc{.nodes = initial}));
    const auto replacedRevision = document.revision();

    ASSERT_TRUE(document.upsertNode(PrefabNodeDesc{
        .stableNodeId = 20,
        .parentIndex = 0,
        .positionX = 4.0F,
        .positionY = 5.0F,
        .positionZ = 6.0F,
        .rotationY = 0.7071067F,
        .rotationW = 0.7071067F,
        .scaleX = 2.0F,
        .scaleY = 3.0F,
        .scaleZ = 4.0F,
        .visible = false,
    }));
    ASSERT_GT(document.revision(), replacedRevision);
    auto current = nodes(document);
    ASSERT_EQ(current.size(), 2U);
    EXPECT_FLOAT_EQ(current[1].positionZ, 6.0F);
    EXPECT_FLOAT_EQ(current[1].scaleY, 3.0F);
    EXPECT_FALSE(current[1].visible);

    ASSERT_TRUE(document.undo());
    current = nodes(document);
    EXPECT_FLOAT_EQ(current[1].positionX, 2.0F);
    ASSERT_TRUE(document.redo());
    current = nodes(document);
    EXPECT_FLOAT_EQ(current[1].positionX, 4.0F);
}

TEST(World3DAuthoringDocumentTests, ErasesCompleteSubtreeAndRemapsRetainedParentIndices)
{
    auto document = createDocument();
    const std::array hierarchy{
        PrefabNodeDesc{.stableNodeId = 1},
        PrefabNodeDesc{.stableNodeId = 2, .parentIndex = 0},
        PrefabNodeDesc{.stableNodeId = 3, .parentIndex = 1},
        PrefabNodeDesc{.stableNodeId = 4},
        PrefabNodeDesc{.stableNodeId = 5, .parentIndex = 3},
    };
    ASSERT_TRUE(document.replace(PrefabPayloadDesc{.nodes = hierarchy}));
    ASSERT_TRUE(document.eraseNodeSubtree(2));

    const auto retained = nodes(document);
    ASSERT_EQ(retained.size(), 3U);
    EXPECT_EQ(retained[0].stableNodeId, 1U);
    EXPECT_EQ(retained[1].stableNodeId, 4U);
    EXPECT_EQ(retained[2].stableNodeId, 5U);
    EXPECT_EQ(retained[2].parentIndex, 1);
}

TEST(World3DAuthoringDocumentTests, InvalidEditsPreserveCurrentAndHistoryBranches)
{
    auto document = createDocument();
    ASSERT_TRUE(document.upsertNode(PrefabNodeDesc{.stableNodeId = 2, .parentIndex = 0}));
    ASSERT_TRUE(document.undo());
    ASSERT_TRUE(document.canRedo());
    const auto beforeBytes = std::vector(document.payloadBytes().begin(), document.payloadBytes().end());
    const auto beforeRevision = document.revision();

    const std::array duplicateIds{
        PrefabNodeDesc{.stableNodeId = 7},
        PrefabNodeDesc{.stableNodeId = 7},
    };
    const auto duplicate = document.replace(PrefabPayloadDesc{.nodes = duplicateIds});
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, AssetFormat::AssetFormatErrorCode::InvalidLayout);
    EXPECT_EQ(std::vector(document.payloadBytes().begin(), document.payloadBytes().end()), beforeBytes);
    EXPECT_EQ(document.revision(), beforeRevision);
    EXPECT_TRUE(document.canRedo());

    const auto eraseFinalRoot = document.eraseNodeSubtree(World3DAuthoringLimits::DefaultRootStableNodeId);
    ASSERT_FALSE(eraseFinalRoot);
    EXPECT_EQ(eraseFinalRoot.error().code, AssetFormat::AssetFormatErrorCode::InvalidLayout);
    EXPECT_EQ(std::vector(document.payloadBytes().begin(), document.payloadBytes().end()), beforeBytes);
    EXPECT_EQ(document.revision(), beforeRevision);
    EXPECT_TRUE(document.canRedo());
}

} // namespace
} // namespace Tina::Editor
