#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/editor/EditorErrors.hpp>
#include <tina/editor/World2DAuthoringDocument.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <vector>

namespace Tina::Editor {
namespace {

using AssetFormat::World2DEntityDesc;
using AssetFormat::World2DSnapshotDesc;

[[nodiscard]] World2DAuthoringDocument createDocument(World2DAuthoringDocumentConfig config = {})
{
    auto document = World2DAuthoringDocument::Create(config);
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

[[nodiscard]] std::vector<World2DEntityDesc> entities(const World2DAuthoringDocument& document)
{
    std::vector<World2DEntityDesc> storage;
    auto snapshot = document.parseCurrentSnapshot(storage);
    EXPECT_TRUE(snapshot) << (snapshot ? "" : snapshot.error().message);
    return storage;
}

TEST(World2DAuthoringDocumentTests, CreatesCanonicalEmptyDocumentAndRejectsInvalidConfig)
{
    auto document = World2DAuthoringDocument::Create();
    ASSERT_TRUE(document);
    EXPECT_EQ(document->entityCount(), 0U);
    EXPECT_EQ(document->historyEntryCount(), 1U);
    EXPECT_FALSE(document->canUndo());
    EXPECT_FALSE(document->canRedo());

    auto invalid = World2DAuthoringDocument::Create({.historyEntryCapacity = 1});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, EditorErrorCode::InvalidConfiguration);
}

TEST(World2DAuthoringDocumentTests, PublishesOnlyValidatedCanonicalRuntimePreview)
{
    auto document = createDocument();
    const std::array authored{
        World2DEntityDesc{.stableEntityId = 10, .positionX = 2.0F},
        World2DEntityDesc{.stableEntityId = 20, .parentStableEntityId = 10, .positionY = 3.0F},
    };
    const std::array gameplay{std::byte{0x11}, std::byte{0x22}};
    ASSERT_TRUE(document.replace(World2DSnapshotDesc{
        .entities = authored,
        .gameplaySchema = 7,
        .gameplayVersion = 2,
        .gameplayBytes = gameplay,
    }));

    auto expected = AssetFormat::writeWorld2DSnapshotBytes(World2DSnapshotDesc{
        .entities = authored,
        .gameplaySchema = 7,
        .gameplayVersion = 2,
        .gameplayBytes = gameplay,
    });
    ASSERT_TRUE(expected);
    EXPECT_EQ(std::vector(document.snapshotBytes().begin(), document.snapshotBytes().end()), *expected);
    EXPECT_EQ(document.entityCount(), 2U);
    EXPECT_EQ(document.gameplaySchema(), 7U);
    EXPECT_EQ(document.gameplayVersion(), 2U);
    EXPECT_EQ(document.gameplayByteCount(), 2U);
    EXPECT_TRUE(document.canUndo());
}

TEST(World2DAuthoringDocumentTests, LoadsCanonicalSnapshotAndEditsGameplayAsRevisions)
{
    const std::array authored{World2DEntityDesc{.stableEntityId = 42, .positionX = 5.0F}};
    auto source = AssetFormat::writeWorld2DSnapshotBytes(World2DSnapshotDesc{.entities = authored});
    ASSERT_TRUE(source);

    auto document = createDocument();
    ASSERT_TRUE(document.loadSnapshot(*source));
    EXPECT_EQ(document.entityCount(), 1U);
    EXPECT_EQ(std::vector(document.snapshotBytes().begin(), document.snapshotBytes().end()), *source);

    const std::array gameplay{std::byte{0x31}, std::byte{0x32}, std::byte{0x33}};
    ASSERT_TRUE(document.setGameplay(9, 4, gameplay));
    EXPECT_EQ(document.gameplaySchema(), 9U);
    EXPECT_EQ(document.gameplayVersion(), 4U);
    EXPECT_EQ(document.gameplayByteCount(), gameplay.size());
    ASSERT_TRUE(document.undo());
    EXPECT_EQ(document.gameplayByteCount(), 0U);
    ASSERT_TRUE(document.redo());
    EXPECT_EQ(document.gameplayByteCount(), gameplay.size());
}

TEST(World2DAuthoringDocumentTests, InvalidEditPreservesDocumentAndHistoryBranches)
{
    auto document = createDocument();
    ASSERT_TRUE(document.upsertEntity(World2DEntityDesc{.stableEntityId = 1}));
    ASSERT_TRUE(document.upsertEntity(World2DEntityDesc{.stableEntityId = 2}));
    ASSERT_TRUE(document.undo());
    const auto beforeBytes = std::vector(document.snapshotBytes().begin(), document.snapshotBytes().end());
    const auto beforeRevision = document.revision();
    const auto beforeUndo = document.undoDepth();
    const auto beforeRedo = document.redoDepth();

    const auto status = document.upsertEntity(
        World2DEntityDesc{.stableEntityId = 3, .parentStableEntityId = 99});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, AssetFormat::AssetFormatErrorCode::InvalidLayout);
    EXPECT_EQ(std::vector(document.snapshotBytes().begin(), document.snapshotBytes().end()), beforeBytes);
    EXPECT_EQ(document.revision(), beforeRevision);
    EXPECT_EQ(document.undoDepth(), beforeUndo);
    EXPECT_EQ(document.redoDepth(), beforeRedo);
}

TEST(World2DAuthoringDocumentTests, UpsertsAndErasesCompleteEntitySubtreeAtomically)
{
    auto document = createDocument();
    ASSERT_TRUE(document.upsertEntity(World2DEntityDesc{.stableEntityId = 1}));
    ASSERT_TRUE(document.upsertEntity(World2DEntityDesc{.stableEntityId = 2, .parentStableEntityId = 1}));
    ASSERT_TRUE(document.upsertEntity(World2DEntityDesc{.stableEntityId = 3, .parentStableEntityId = 2}));
    ASSERT_TRUE(document.upsertEntity(World2DEntityDesc{.stableEntityId = 4}));

    ASSERT_TRUE(document.eraseEntitySubtree(2));
    const auto remaining = entities(document);
    ASSERT_EQ(remaining.size(), 2U);
    EXPECT_EQ(remaining[0].stableEntityId, 1U);
    EXPECT_EQ(remaining[1].stableEntityId, 4U);

    const auto missing = document.eraseEntitySubtree(99);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, EditorErrorCode::EntityNotFound);
}

TEST(World2DAuthoringDocumentTests, UndoRedoAndBranchReplacementUseBoundedRevisionHistory)
{
    auto document = createDocument({
        .historyEntryCapacity = 3,
        .historyByteCapacity = 4096,
    });
    ASSERT_TRUE(document.upsertEntity(World2DEntityDesc{.stableEntityId = 1}));
    ASSERT_TRUE(document.upsertEntity(World2DEntityDesc{.stableEntityId = 2}));
    ASSERT_TRUE(document.upsertEntity(World2DEntityDesc{.stableEntityId = 3}));
    EXPECT_EQ(document.historyEntryCount(), 3U);
    EXPECT_EQ(document.undoDepth(), 2U);

    ASSERT_TRUE(document.undo());
    EXPECT_EQ(document.entityCount(), 2U);
    ASSERT_TRUE(document.undo());
    EXPECT_EQ(document.entityCount(), 1U);
    EXPECT_FALSE(document.canUndo());
    ASSERT_TRUE(document.redo());
    EXPECT_EQ(document.entityCount(), 2U);

    ASSERT_TRUE(document.upsertEntity(World2DEntityDesc{.stableEntityId = 9}));
    EXPECT_FALSE(document.canRedo());
    const auto current = entities(document);
    ASSERT_EQ(current.size(), 3U);
    EXPECT_EQ(current.back().stableEntityId, 9U);
}

TEST(World2DAuthoringDocumentTests, HistoryBudgetFailureLeavesCurrentAndRedoUntouched)
{
    constexpr Core::usize EmptyBytes = AssetFormat::World2DSnapshotWire::HeaderBytes;
    constexpr Core::usize OneEntityBytes = EmptyBytes + AssetFormat::World2DSnapshotWire::EntityBytes;
    auto document = createDocument({
        .historyEntryCapacity = 4,
        .historyByteCapacity = EmptyBytes + OneEntityBytes,
    });
    ASSERT_TRUE(document.upsertEntity(World2DEntityDesc{.stableEntityId = 1}));
    ASSERT_TRUE(document.undo());
    ASSERT_TRUE(document.canRedo());

    const std::array twoEntities{
        World2DEntityDesc{.stableEntityId = 7},
        World2DEntityDesc{.stableEntityId = 8},
    };
    const auto beforeRevision = document.revision();
    const auto status = document.replace(World2DSnapshotDesc{.entities = twoEntities});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, EditorErrorCode::HistoryCapacityExceeded);
    EXPECT_EQ(document.revision(), beforeRevision);
    EXPECT_EQ(document.entityCount(), 0U);
    EXPECT_TRUE(document.canRedo());
}

TEST(World2DAuthoringDocumentTests, DocumentCapacityFailuresPreserveCurrentAndHistory)
{
    auto document = createDocument({
        .entityCapacity = 1,
        .gameplayByteCapacity = 2,
        .historyEntryCapacity = 4,
        .historyByteCapacity = 4096,
    });
    ASSERT_TRUE(document.upsertEntity(World2DEntityDesc{.stableEntityId = 1}));
    const auto beforeBytes = std::vector(document.snapshotBytes().begin(), document.snapshotBytes().end());
    const auto beforeRevision = document.revision();
    const auto beforeHistory = document.historyEntryCount();

    const auto entityFailure = document.upsertEntity(World2DEntityDesc{.stableEntityId = 2});
    ASSERT_FALSE(entityFailure);
    EXPECT_EQ(entityFailure.error().code, EditorErrorCode::DocumentCapacityExceeded);
    const std::array oversizedGameplay{std::byte{1}, std::byte{2}, std::byte{3}};
    const auto gameplayFailure = document.setGameplay(1, 1, oversizedGameplay);
    ASSERT_FALSE(gameplayFailure);
    EXPECT_EQ(gameplayFailure.error().code, EditorErrorCode::DocumentCapacityExceeded);

    EXPECT_EQ(std::vector(document.snapshotBytes().begin(), document.snapshotBytes().end()), beforeBytes);
    EXPECT_EQ(document.revision(), beforeRevision);
    EXPECT_EQ(document.historyEntryCount(), beforeHistory);
}

TEST(World2DAuthoringDocumentTests, RejectsOldOrNonCanonicalSnapshotWithoutMutation)
{
    auto document = createDocument();
    ASSERT_TRUE(document.upsertEntity(World2DEntityDesc{.stableEntityId = 1}));
    auto invalid = std::vector(document.snapshotBytes().begin(), document.snapshotBytes().end());
    invalid[0] = std::byte{0};
    invalid[1] = std::byte{0};
    const auto beforeRevision = document.revision();

    const auto status = document.loadSnapshot(invalid);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, AssetFormat::AssetFormatErrorCode::UnsupportedSchema);
    EXPECT_EQ(document.revision(), beforeRevision);
    EXPECT_EQ(document.entityCount(), 1U);
}

} // namespace
} // namespace Tina::Editor
