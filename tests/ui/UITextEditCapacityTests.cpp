#include "UITextEditTestSupport.hpp"

namespace Tina::Tests {
namespace {

using namespace UITextEditTestSupport;

TEST_F(UITextEditTest, ReleasedTextAllocationCannotOverwriteAnotherNode)
{
    const UI::UINodeId first = createTextEdit();
    const UI::UINodeId second = createTextEdit();
    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());

    assertOk(updater.setText(first, "AAAA"));
    assertOk(updater.setText(second, "KEEP-ME"));
    auto secondText = updater.text(second);
    ASSERT_TRUE(secondText.has_value());
    EXPECT_EQ(*secondText, "KEEP-ME");

    assertOk(updater.setText(first, ""));
    assertOk(updater.setText(first, "12345678"));
    auto firstText = updater.text(first);
    ASSERT_TRUE(firstText.has_value());
    EXPECT_EQ(*firstText, "12345678");
    secondText = updater.text(second);
    ASSERT_TRUE(secondText.has_value());
    EXPECT_EQ(*secondText, "KEEP-ME");
    EXPECT_EQ(context->statistics().textByteUsed, 15U);

    assertOk(updater.setText(first, "abcdefghijkl"));
    firstText = updater.text(first);
    ASSERT_TRUE(firstText.has_value());
    EXPECT_EQ(*firstText, "abcdefghijkl");
    secondText = updater.text(second);
    ASSERT_TRUE(secondText.has_value());
    EXPECT_EQ(*secondText, "KEEP-ME");
    EXPECT_EQ(context->statistics().textByteUsed, 19U);
}

TEST_F(UITextEditTest, SmallTextArenaReusesReleasedLargeBlockAtRequestedSize)
{
    auto localContext = createContext(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 4,
            .rootCapacity = 1,
            .textByteCapacity = 16,
        });
    ASSERT_NE(localContext, nullptr);
    auto localRoot = createRoot(*localContext);
    ASSERT_TRUE(localRoot.hasValue());
    auto localUpdater = createUpdater(*localContext, localRoot);

    auto firstResult = localUpdater.createElement(localRoot.rootNodeId(), UI::makeTextEditElement());
    ASSERT_TRUE(firstResult.has_value()) << (firstResult ? "" : firstResult.error().message);
    const UI::UINodeId first = *firstResult;
    auto secondResult = localUpdater.createElement(localRoot.rootNodeId(), UI::makeTextEditElement());
    ASSERT_TRUE(secondResult.has_value()) << (secondResult ? "" : secondResult.error().message);
    const UI::UINodeId second = *secondResult;

    assertOk(localUpdater.setText(first, "abcdefghijkl"));
    assertOk(localUpdater.setText(second, "xy"));
    EXPECT_EQ(localContext->statistics().textByteUsed, 14U);

    assertOk(localUpdater.setText(first, ""));
    EXPECT_EQ(localContext->statistics().textByteUsed, 2U);

    assertOk(localUpdater.setText(first, "Z"));
    EXPECT_EQ(localContext->statistics().textByteUsed, 3U);

    assertOk(localUpdater.setText(second, "0123456789"));
    auto secondText = localUpdater.text(second);
    ASSERT_TRUE(secondText.has_value());
    EXPECT_EQ(*secondText, "0123456789");
    EXPECT_EQ(localContext->statistics().textByteUsed, 11U);

    assertOk(localUpdater.setText(first, "PQRS"));
    auto firstText = localUpdater.text(first);
    ASSERT_TRUE(firstText.has_value());
    EXPECT_EQ(*firstText, "PQRS");
    secondText = localUpdater.text(second);
    ASSERT_TRUE(secondText.has_value());
    EXPECT_EQ(*secondText, "0123456789");
    EXPECT_EQ(localContext->statistics().textByteUsed, 14U);
}

TEST_F(UITextEditTest, ImePreeditDirtyQueueFailureLeavesPreviousComposition)
{
    auto localContext = createContext(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 4,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 1,
            .textByteCapacity = 64,
        });
    ASSERT_NE(localContext, nullptr);
    auto localRoot = createRoot(*localContext);
    ASSERT_TRUE(localRoot.hasValue());
    auto localUpdater = createUpdater(*localContext, localRoot);

    auto textEditResult = localUpdater.createElement(localRoot.rootNodeId(), UI::makeTextEditElement());
    ASSERT_TRUE(textEditResult.has_value())
        << (textEditResult ? "" : textEditResult.error().message);
    const UI::UINodeId textEdit = *textEditResult;
    auto blockerResult = localUpdater.createElement(localRoot.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(blockerResult.has_value())
        << (blockerResult ? "" : blockerResult.error().message);
    const UI::UINodeId blocker = *blockerResult;
    assertOk(localContext->commitLayout({.width = 320.0F, .height = 120.0F}));

    auto focus = localContext->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    EXPECT_TRUE(focus->consumed);
    EXPECT_EQ(focus->focus, textEdit);
    EXPECT_EQ(localContext->imeFocus(), textEdit);

    auto started = localContext->routeTextComposition(
        window,
        Platform::PlatformFrameId{1},
        1,
        "old",
        2,
        Platform::TextCompositionStage::Started);
    ASSERT_TRUE(started.has_value()) << (started ? "" : started.error().message);
    EXPECT_TRUE(started->consumed);
    EXPECT_TRUE(started->applied);
    EXPECT_TRUE(localContext->imeCompositionActive());
    EXPECT_EQ(localContext->imePreeditUtf8(), "old");
    EXPECT_EQ(localContext->imePreeditCursorCodepoint(), 2U);
    assertOk(localContext->commitLayout({.width = 320.0F, .height = 120.0F}));

    assertOk(localUpdater.setPointerHitPolicy(
        blocker,
        UI::UIPointerHitPolicy::Targetable));
    EXPECT_EQ(localContext->statistics().dirtyQueuePendingCount, 1U);

    auto rejected = localContext->routeTextComposition(
        window,
        Platform::PlatformFrameId{2},
        2,
        "new",
        1,
        Platform::TextCompositionStage::Updated);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_TRUE(localContext->imeCompositionActive());
    EXPECT_EQ(localContext->imePreeditUtf8(), "old");
    EXPECT_EQ(localContext->imePreeditCursorCodepoint(), 2U);
}

TEST_F(UITextEditTest, PointerSelectionDirtyQueueFailurePreservesSelectionAndFocus)
{
    auto localContext = createContext(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 5,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 2,
            .textByteCapacity = 64,
        });
    ASSERT_NE(localContext, nullptr);
    auto localRoot = createRoot(*localContext);
    ASSERT_TRUE(localRoot.hasValue());
    auto localUpdater = createUpdater(*localContext, localRoot);

    auto textEditResult = localUpdater.createElement(localRoot.rootNodeId(), UI::makeTextEditElement());
    ASSERT_TRUE(textEditResult.has_value())
        << (textEditResult ? "" : textEditResult.error().message);
    const UI::UINodeId textEdit = *textEditResult;
    auto firstBlockerResult = localUpdater.createElement(localRoot.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(firstBlockerResult.has_value())
        << (firstBlockerResult ? "" : firstBlockerResult.error().message);
    const UI::UINodeId firstBlocker = *firstBlockerResult;
    auto secondBlockerResult = localUpdater.createElement(localRoot.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(secondBlockerResult.has_value())
        << (secondBlockerResult ? "" : secondBlockerResult.error().message);
    const UI::UINodeId secondBlocker = *secondBlockerResult;

    assertOk(localUpdater.setLayoutStyle(
        localRoot.rootNodeId(),
        fixedSize(320.0F, 120.0F)));
    assertOk(localUpdater.setLayoutStyle(textEdit, fixedSize(240.0F, 32.0F)));
    assertOk(localUpdater.setText(textEdit, "ABCD"));
    assertOk(localContext->commitLayout({.width = 320.0F, .height = 120.0F}));

    auto down = localContext->routePointerInput(makePrimaryPointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1,
        1.0F,
        10.0F));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_EQ(localContext->defaultActionFocus(), textEdit);
    EXPECT_EQ(localContext->imeFocus(), textEdit);
    auto selection = localUpdater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value())
        << (selection ? "" : selection.error().message);
    EXPECT_EQ(*selection, (UI::UITextSelection{}));
    assertOk(localContext->commitLayout({.width = 320.0F, .height = 120.0F}));

    assertOk(localUpdater.setPointerHitPolicy(
        firstBlocker,
        UI::UIPointerHitPolicy::Targetable));
    assertOk(localUpdater.setPointerHitPolicy(
        secondBlocker,
        UI::UIPointerHitPolicy::Targetable));
    EXPECT_EQ(localContext->statistics().dirtyQueuePendingCount, 2U);

    auto rejected = localContext->routePointerInput(makePrimaryPointerInput(
        window,
        UI::UIRoutedPointerEventKind::Move,
        2,
        100.0F,
        10.0F));
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    selection = localUpdater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value())
        << (selection ? "" : selection.error().message);
    EXPECT_EQ(*selection, (UI::UITextSelection{}));
    EXPECT_EQ(localContext->defaultActionFocus(), textEdit);
    EXPECT_EQ(localContext->imeFocus(), textEdit);
    EXPECT_EQ(localContext->statistics().dirtyQueuePendingCount, 2U);
}

TEST_F(UITextEditTest, PaintSnapshotCapacityFiveAcceptsSelectionAndPreedit)
{
    auto localContext = createContext(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 5,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 5,
            .textByteCapacity = 64,
        });
    ASSERT_NE(localContext, nullptr);
    auto localRoot = createRoot(*localContext);
    ASSERT_TRUE(localRoot.hasValue());
    auto localUpdater = createUpdater(*localContext, localRoot);

    assertOk(localUpdater.setLayoutStyle(
        localRoot.rootNodeId(),
        fixedSize(200.0F, 40.0F)));
    auto textEditResult = localUpdater.createElement(localRoot.rootNodeId(), UI::makeTextEditElement());
    ASSERT_TRUE(textEditResult.has_value())
        << (textEditResult ? "" : textEditResult.error().message);
    const UI::UINodeId textEdit = *textEditResult;
    assertOk(localUpdater.setLayoutStyle(textEdit, fixedSize(120.0F, 32.0F)));
    assertOk(localUpdater.setText(textEdit, "ABCD"));
    assertOk(localContext->commitLayout({.width = 200.0F, .height = 40.0F}));

    auto focus = localContext->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    EXPECT_EQ(focus->focus, textEdit);

    assertOk(localUpdater.setTextSelection(
        textEdit,
        {.anchorCodepoint = 1, .caretCodepoint = 3}));

    auto composition = localContext->routeTextComposition(
        window,
        Platform::PlatformFrameId{1},
        1,
        "XY",
        2,
        Platform::TextCompositionStage::Started);
    ASSERT_TRUE(composition.has_value())
        << (composition ? "" : composition.error().message);
    EXPECT_TRUE(composition->consumed);
    EXPECT_TRUE(composition->applied);
    assertOk(localContext->commitLayout({.width = 200.0F, .height = 40.0F}));

    const UI::UICommittedPaintView paint = localContext->committedPaint();
    ASSERT_EQ(paint.size(), 5U);
    EXPECT_EQ(paint.entries()[0].node, textEdit);
    EXPECT_EQ(paint.entries()[1].node, textEdit);
    EXPECT_EQ(paint.entries()[2].node, textEdit);
    EXPECT_EQ(paint.entries()[3].node, textEdit);
    EXPECT_EQ(paint.entries()[4].node, textEdit);
    EXPECT_TRUE(localContext->imeCompositionActive());
    EXPECT_EQ(localContext->imePreeditUtf8(), "XY");
}

TEST_F(UITextEditTest, FailedPaintCommitPreservesPublishedCaretGeometry)
{
    auto localContext = createContext(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 4,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 2,
            .textByteCapacity = 32,
        });
    ASSERT_NE(localContext, nullptr);
    auto localRoot = createRoot(*localContext);
    ASSERT_TRUE(localRoot.hasValue());
    auto localUpdater = createUpdater(*localContext, localRoot);
    assertOk(localUpdater.setLayoutStyle(localRoot.rootNodeId(), fixedSize(120.0F, 40.0F)));
    auto textEditResult = localUpdater.createElement(
        localRoot.rootNodeId(), UI::makeTextEditElement());
    ASSERT_TRUE(textEditResult.has_value());
    const UI::UINodeId textEdit = *textEditResult;
    assertOk(localUpdater.setLayoutStyle(textEdit, fixedSize(100.0F, 24.0F)));
    assertOk(localUpdater.setText(textEdit, "A"));
    assertOk(localContext->commitLayout({.width = 120.0F, .height = 40.0F}));
    assertOk(localContext->requestFocus(textEdit));
    assertOk(localUpdater.setTextSelection(textEdit, {.anchorCodepoint = 1, .caretCodepoint = 1}));
    assertOk(localContext->commitLayout({.width = 120.0F, .height = 40.0F}));
    const std::optional<UI::UILogicalRect> published =
        localContext->committedTextInputCaretRect();
    ASSERT_TRUE(published.has_value());

    assertOk(localUpdater.setText(textEdit, "AB"));
    assertOk(localUpdater.setTextSelection(textEdit, {.anchorCodepoint = 2, .caretCodepoint = 2}));
    const Core::Status rejected =
        localContext->commitLayout({.width = 120.0F, .height = 40.0F});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(localContext->committedTextInputCaretRect(), published);
}

} // namespace
} // namespace Tina::Tests
