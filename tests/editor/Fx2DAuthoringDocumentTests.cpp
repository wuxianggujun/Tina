#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/Fx2DPayload.hpp>
#include <tina/editor/EditorErrors.hpp>
#include <tina/editor/Fx2DAuthoringDocument.hpp>

#include <gtest/gtest.h>

#include <limits>
#include <vector>

namespace Tina::Editor {
namespace {

[[nodiscard]] Core::AssetId assetId(Core::u8 marker)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(marker);
    return *Core::AssetId::fromBytes(bytes);
}

// Field defaults are already a valid payload, so a fixture only needs a sprite id.
[[nodiscard]] AssetFormat::Fx2DPayloadDesc payload(Core::u8 spriteMarker = 1)
{
    AssetFormat::Fx2DPayloadDesc desc{};
    desc.spriteAssetId = assetId(spriteMarker);
    return desc;
}

template <typename Result>
void expectFailureCode(const Result& result, Core::ErrorCode expected)
{
    EXPECT_FALSE(result);
    if (!result) {
        EXPECT_EQ(result.error().code, expected);
    }
}

TEST(Fx2DAuthoringDocumentTests, CreatePublishesTheInitialRevisionAndRejectsInvalidCapacity)
{
    expectFailureCode(
        Fx2DAuthoringDocument::Create(payload(), {.historyEntryCapacity = 0}),
        EditorErrorCode::InvalidConfiguration);
    expectFailureCode(
        Fx2DAuthoringDocument::Create(payload(), {.historyByteCapacity = 0}),
        EditorErrorCode::InvalidConfiguration);
    expectFailureCode(
        Fx2DAuthoringDocument::Create(
            payload(),
            {.historyEntryCapacity = Fx2DAuthoringLimits::MaximumHistoryEntries + 1U}),
        EditorErrorCode::InvalidConfiguration);
    expectFailureCode(
        Fx2DAuthoringDocument::Create(
            payload(),
            {.historyByteCapacity = Fx2DAuthoringLimits::MaximumHistoryBytes + 1U}),
        EditorErrorCode::InvalidConfiguration);

    auto document = Fx2DAuthoringDocument::Create(payload(7));
    ASSERT_TRUE(document) << document.error().message;
    EXPECT_EQ(document->value().spriteAssetId, assetId(7));
    EXPECT_FALSE(document->payloadBytes().empty());
    // A fresh document has nowhere to go in either direction.
    EXPECT_FALSE(document->canUndo());
    EXPECT_FALSE(document->canRedo());
    expectFailureCode(document->undo(), EditorErrorCode::InvalidAuthoringOperation);
    expectFailureCode(document->redo(), EditorErrorCode::InvalidAuthoringOperation);
}

// An invalid desc must be rejected by the payload writer before it can enter history,
// otherwise the document would hold a revision that cannot be cooked.
TEST(Fx2DAuthoringDocumentTests, InvalidPayloadIsRejectedAtCreateAndAtReplace)
{
    AssetFormat::Fx2DPayloadDesc invalid = payload();
    invalid.particle.capacity = 0; // count(1) > capacity(0)

    EXPECT_FALSE(Fx2DAuthoringDocument::Create(invalid));

    auto document = Fx2DAuthoringDocument::Create(payload());
    ASSERT_TRUE(document) << document.error().message;
    const Core::u64 revisionBefore = document->revision();
    EXPECT_FALSE(document->replace(invalid));
    // A rejected replace leaves the document exactly as it was.
    EXPECT_EQ(document->revision(), revisionBefore);
    EXPECT_EQ(document->value().spriteAssetId, assetId(1));
    EXPECT_FALSE(document->canUndo());
}

TEST(Fx2DAuthoringDocumentTests, ReplaceAdvancesRevisionAndUndoRedoWalkHistory)
{
    auto document = Fx2DAuthoringDocument::Create(payload(1));
    ASSERT_TRUE(document) << document.error().message;
    const Core::u64 initialRevision = document->revision();

    ASSERT_TRUE(document->replace(payload(2))) ;
    EXPECT_GT(document->revision(), initialRevision);
    EXPECT_EQ(document->value().spriteAssetId, assetId(2));
    EXPECT_TRUE(document->canUndo());
    EXPECT_FALSE(document->canRedo());

    ASSERT_TRUE(document->replace(payload(3)));
    EXPECT_EQ(document->value().spriteAssetId, assetId(3));

    ASSERT_TRUE(document->undo());
    EXPECT_EQ(document->value().spriteAssetId, assetId(2));
    EXPECT_TRUE(document->canUndo());
    EXPECT_TRUE(document->canRedo());

    ASSERT_TRUE(document->undo());
    EXPECT_EQ(document->value().spriteAssetId, assetId(1));
    EXPECT_FALSE(document->canUndo());

    ASSERT_TRUE(document->redo());
    EXPECT_EQ(document->value().spriteAssetId, assetId(2));
    ASSERT_TRUE(document->redo());
    EXPECT_EQ(document->value().spriteAssetId, assetId(3));
    EXPECT_FALSE(document->canRedo());
    expectFailureCode(document->redo(), EditorErrorCode::InvalidAuthoringOperation);
}

// payloadBytes() is what a caller cooks, so it has to track the cursor rather than
// the newest revision -- otherwise undo would show one value and write another.
TEST(Fx2DAuthoringDocumentTests, PayloadBytesFollowTheCursorAndMatchTheCanonicalWriter)
{
    auto document = Fx2DAuthoringDocument::Create(payload(1));
    ASSERT_TRUE(document) << document.error().message;
    const std::vector<std::byte> initialBytes{
        document->payloadBytes().begin(), document->payloadBytes().end()};

    ASSERT_TRUE(document->replace(payload(2)));
    const std::vector<std::byte> replacedBytes{
        document->payloadBytes().begin(), document->payloadBytes().end()};
    EXPECT_NE(initialBytes, replacedBytes);

    auto expected = AssetFormat::writeFx2DPayloadBytes(document->value());
    ASSERT_TRUE(expected) << expected.error().message;
    EXPECT_EQ(replacedBytes, *expected);

    ASSERT_TRUE(document->undo());
    const std::vector<std::byte> undoneBytes{
        document->payloadBytes().begin(), document->payloadBytes().end()};
    EXPECT_EQ(undoneBytes, initialBytes)
        << "payloadBytes() did not follow the cursor back";
}

// Replacing with an identical value is a no-op: it must not consume a history slot,
// or a caller that re-applies the same edit would exhaust capacity for nothing.
TEST(Fx2DAuthoringDocumentTests, ReplacingWithAnIdenticalValueDoesNotTouchHistory)
{
    auto document = Fx2DAuthoringDocument::Create(payload(4));
    ASSERT_TRUE(document) << document.error().message;
    const Core::u64 revisionBefore = document->revision();

    ASSERT_TRUE(document->replace(payload(4)));
    EXPECT_EQ(document->revision(), revisionBefore)
        << "an identical replace advanced the revision";
    EXPECT_FALSE(document->canUndo())
        << "an identical replace consumed a history entry";
}

// Undo then replace has to drop the redo tail, so history stays a single line of
// descent rather than letting a stale redo resurrect a value the user moved away from.
TEST(Fx2DAuthoringDocumentTests, ReplaceAfterUndoDiscardsTheRedoTail)
{
    auto document = Fx2DAuthoringDocument::Create(payload(1));
    ASSERT_TRUE(document) << document.error().message;
    ASSERT_TRUE(document->replace(payload(2)));
    ASSERT_TRUE(document->undo());
    ASSERT_EQ(document->value().spriteAssetId, assetId(1));
    ASSERT_TRUE(document->canRedo());

    ASSERT_TRUE(document->replace(payload(3)));
    EXPECT_FALSE(document->canRedo()) << "the discarded redo tail is still reachable";
    EXPECT_EQ(document->value().spriteAssetId, assetId(3));

    // Undo now returns to the branch point, not to the abandoned revision.
    ASSERT_TRUE(document->undo());
    EXPECT_EQ(document->value().spriteAssetId, assetId(1));
}

TEST(Fx2DAuthoringDocumentTests, HistoryEntryCapacityFailsClosedAndKeepsTheCurrentValue)
{
    auto document =
        Fx2DAuthoringDocument::Create(payload(1), {.historyEntryCapacity = 2});
    ASSERT_TRUE(document) << document.error().message;

    ASSERT_TRUE(document->replace(payload(2)));
    // The third revision does not fit in two entries.
    expectFailureCode(document->replace(payload(3)),
                      EditorErrorCode::HistoryCapacityExceeded);
    EXPECT_EQ(document->value().spriteAssetId, assetId(2))
        << "a refused commit changed the current value";
    EXPECT_TRUE(document->canUndo());
    EXPECT_FALSE(document->canRedo());

    // Undo frees a retained entry, so a commit from there is accepted again.
    ASSERT_TRUE(document->undo());
    EXPECT_TRUE(document->replace(payload(4)));
    EXPECT_EQ(document->value().spriteAssetId, assetId(4));
}

TEST(Fx2DAuthoringDocumentTests, HistoryByteCapacityIsCheckedAtCreateAndAtReplace)
{
    auto oneByte = AssetFormat::writeFx2DPayloadBytes(payload());
    ASSERT_TRUE(oneByte) << oneByte.error().message;
    const Core::usize revisionBytes = oneByte->size();
    ASSERT_GT(revisionBytes, 0U);

    // A budget smaller than a single revision cannot hold the initial value.
    expectFailureCode(
        Fx2DAuthoringDocument::Create(payload(),
                                      {.historyByteCapacity = revisionBytes - 1U}),
        EditorErrorCode::HistoryCapacityExceeded);

    // Exactly one revision fits, but a second one does not.
    auto document =
        Fx2DAuthoringDocument::Create(payload(1), {.historyByteCapacity = revisionBytes});
    ASSERT_TRUE(document) << document.error().message;
    expectFailureCode(document->replace(payload(2)),
                      EditorErrorCode::HistoryCapacityExceeded);
    EXPECT_EQ(document->value().spriteAssetId, assetId(1));
    EXPECT_FALSE(document->canUndo());
}

// Undo and redo only move the cursor, but they still advance the revision counter:
// a consumer keyed on revision has to refresh after either, since the value changed.
TEST(Fx2DAuthoringDocumentTests, UndoAndRedoAdvanceTheRevisionCounter)
{
    auto document = Fx2DAuthoringDocument::Create(payload(1));
    ASSERT_TRUE(document) << document.error().message;
    ASSERT_TRUE(document->replace(payload(2)));

    const Core::u64 beforeUndo = document->revision();
    ASSERT_TRUE(document->undo());
    const Core::u64 afterUndo = document->revision();
    EXPECT_GT(afterUndo, beforeUndo);

    ASSERT_TRUE(document->redo());
    EXPECT_GT(document->revision(), afterUndo);

    // A refused navigation must not advance it.
    const Core::u64 atTip = document->revision();
    EXPECT_FALSE(document->redo());
    EXPECT_EQ(document->revision(), atTip);
}

// A capacity the API accepts must produce a usable document. Entry capacity 1 leaves
// room for the initial revision and nothing else, so every replace fails -- and it
// fails with HistoryCapacityExceeded, which reads as "try again later" rather than
// "this document was born read-only".
TEST(Fx2DAuthoringDocumentTests, EntryCapacityBelowTwoIsRejectedRatherThanBornReadOnly)
{
    expectFailureCode(Fx2DAuthoringDocument::Create(payload(), {.historyEntryCapacity = 1}),
                      EditorErrorCode::InvalidConfiguration);
}

// reserve() throws length_error, not bad_alloc, when the request exceeds max_size().
// Create only caught bad_alloc, so an absurd capacity threw straight out of a function
// that returns Result -- the one thing a module boundary must never do (ADR 0004).
TEST(Fx2DAuthoringDocumentTests, AbsurdEntryCapacityFailsClosedInsteadOfThrowing)
{
    const auto absurd = (std::numeric_limits<Core::usize>::max)();
    expectFailureCode(Fx2DAuthoringDocument::Create(payload(), {.historyEntryCapacity = absurd}),
                      EditorErrorCode::InvalidConfiguration);
}

} // namespace
} // namespace Tina::Editor
