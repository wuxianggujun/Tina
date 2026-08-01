#include "UITextEditTestSupport.hpp"

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace Tina::Tests {
namespace {

using namespace UITextEditTestSupport;

TEST_F(UITextEditTest, DefaultsTargetableWhileLabelRemainsReadOnly)
{
    auto labelResult = updater.createElement(root.rootNodeId(), UI::makeLabelElement());
    ASSERT_TRUE(labelResult.has_value());
    const UI::UINodeId label = *labelResult;
    assertOk(updater.setLayoutStyle(label, fixedSize(100.0F, 24.0F)));
    const Core::Status wrongPaint = updater.setTextEditPaint(label, {});
    ASSERT_FALSE(wrongPaint.has_value());
    EXPECT_EQ(wrongPaint.error().code, UI::UIErrorCode::InvalidControlValue);
    auto wrongPaintQuery = updater.textEditPaint(label);
    ASSERT_FALSE(wrongPaintQuery.has_value());
    EXPECT_EQ(wrongPaintQuery.error().code, UI::UIErrorCode::InvalidControlValue);

    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    publishLayout();

    bool sawLabelHit = false;
    bool sawTextEditHit = false;
    for (const UI::UICommittedHitEntry& entry : context->committedHit().entries()) {
        if (entry.node == label) {
            sawLabelHit = true;
            EXPECT_EQ(entry.policy, UI::UIPointerHitPolicy::Ignore);
        }
        if (entry.node == textEdit) {
            sawTextEditHit = true;
            EXPECT_EQ(entry.policy, UI::UIPointerHitPolicy::Targetable);
        }
    }
    EXPECT_TRUE(sawLabelHit);
    EXPECT_TRUE(sawTextEditHit);

    auto focus = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    EXPECT_TRUE(focus->consumed);
    EXPECT_EQ(focus->focus, textEdit);
    EXPECT_EQ(context->defaultActionFocus(), textEdit);
    EXPECT_EQ(context->imeFocus(), textEdit);

    auto selection = updater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value()) << (selection ? "" : selection.error().message);
    EXPECT_EQ(*selection, (UI::UITextSelection{}));
}

TEST_F(UITextEditTest, PaintStatesReuseHoverArmAndFocusWithoutStalePressedFeedback)
{
    constexpr UI::UIStraightSrgba8Color Normal = UI::rgb(0x203040);
    constexpr UI::UITextEditPaint Paint{
        .hoveredBackgroundColor = UI::rgb(0x315170),
        .pressedBackgroundColor = UI::rgb(0x102030),
        .focusedBackgroundColor = UI::rgb(0x406080),
        .disabledBackgroundColor = UI::rgb(0x000000),
    };
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setBoxPaint(textEdit, UI::makeSolidBox(Normal)));
    assertOk(updater.setTextEditPaint(textEdit, Paint));
    auto currentPaint = updater.textEditPaint(textEdit);
    ASSERT_TRUE(currentPaint.has_value()) << (currentPaint ? "" : currentPaint.error().message);
    EXPECT_EQ(*currentPaint, Paint);

    const auto expectBackground = [&](UI::UIPremultipliedRgba8Color expected) {
        const UI::UICommittedPaintEntry* background = nullptr;
        for (const UI::UICommittedPaintEntry& entry : context->committedPaint().entries()) {
            if (entry.node == textEdit && entry.kind != UI::UICommittedPaintKind::Glyph && entry.worldRect.width == 240.0F &&
                entry.worldRect.height == 32.0F) {
                background = &entry;
                break;
            }
        }
        ASSERT_NE(background, nullptr);
        EXPECT_EQ(background->solidFill, expected);
    };

    publishLayout();
    expectBackground(UI::premultiply(Normal));

    auto hovered = context->routePointerInput(makePrimaryPointerInput(
        window, UI::UIRoutedPointerEventKind::Move, 1));
    ASSERT_TRUE(hovered.has_value()) << (hovered ? "" : hovered.error().message);
    publishLayout();
    expectBackground(UI::premultiply(Paint.hoveredBackgroundColor));

    auto down = context->routePointerInput(makePrimaryPointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonDown, 2));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    publishLayout();
    expectBackground(UI::premultiply(Paint.pressedBackgroundColor));

    assertOk(context->cancelPointerInteraction(window));
    publishLayout();
    expectBackground(UI::premultiply(Normal));

    hovered = context->routePointerInput(makePrimaryPointerInput(
        window, UI::UIRoutedPointerEventKind::Move, 3));
    ASSERT_TRUE(hovered.has_value()) << (hovered ? "" : hovered.error().message);
    down = context->routePointerInput(makePrimaryPointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonDown, 4));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    publishLayout();
    expectBackground(UI::premultiply(Paint.pressedBackgroundColor));

    auto up = context->routePointerInput(makePrimaryPointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonUp, 5));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    publishLayout();
    expectBackground(UI::premultiply(Paint.hoveredBackgroundColor));

    auto outside = context->routePointerInput(makePrimaryPointerInput(
        window, UI::UIRoutedPointerEventKind::Move, 6, 300.0F, 10.0F));
    ASSERT_TRUE(outside.has_value()) << (outside ? "" : outside.error().message);
    publishLayout();
    expectBackground(UI::premultiply(Paint.focusedBackgroundColor));

    assertOk(updater.setEnabled(textEdit, false));
    publishLayout();
    expectBackground(UI::UIPremultipliedRgba8Color{.alpha = 140});
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    EXPECT_FALSE(context->imeFocus().hasValue());
}

TEST_F(UITextEditTest, EnforcesSingleLineUtf8AndCodepointSelectionBounds)
{
    constexpr std::string_view MixedUtf8 = "A" "\xE4\xBD\xA0" "B";
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());

    assertOk(updater.setText(textEdit, MixedUtf8));
    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, MixedUtf8);

    assertOk(updater.setTextSelection(
        textEdit,
        {.anchorCodepoint = 1, .caretCodepoint = 2}));
    auto selection = updater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(
        *selection,
        (UI::UITextSelection{.anchorCodepoint = 1, .caretCodepoint = 2}));

    const Core::Status pastEnd = updater.setTextSelection(
        textEdit,
        {.anchorCodepoint = 1, .caretCodepoint = 4});
    ASSERT_FALSE(pastEnd.has_value());
    EXPECT_EQ(pastEnd.error().code, UI::UIErrorCode::InvalidText);

    for (const std::string_view invalid : {std::string_view{"A\nB"}, std::string_view{"A\rB"}}) {
        const Core::Status status = updater.setText(textEdit, invalid);
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, UI::UIErrorCode::InvalidText);
    }
    text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, MixedUtf8);

    assertOk(updater.setText(textEdit, "A"));
    selection = updater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(
        *selection,
        (UI::UITextSelection{.anchorCodepoint = 1, .caretCodepoint = 1}));
}

TEST_F(UITextEditTest, InvalidTextInputAndCompositionLeaveActivePreeditUnchanged)
{
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setText(textEdit, "AB"));
    focusWithTab(textEdit);

    auto started = context->routeTextComposition(
        window,
        Platform::PlatformFrameId{1},
        1,
        "old",
        2,
        Platform::TextCompositionStage::Started);
    ASSERT_TRUE(started.has_value())
        << (started ? "" : started.error().message);

    const auto expectCompositionStateUnchanged = [&] {
        EXPECT_TRUE(context->imeCompositionActive());
        EXPECT_EQ(context->imePreeditUtf8(), "old");
        EXPECT_EQ(context->imePreeditCursorCodepoint(), 2U);
    };
    const auto expectInvalidComposition = [&](u64 sequence,
                                              std::string_view preedit,
                                              Platform::TextCompositionStage stage,
                                              Core::ErrorCode expectedError) {
        auto result = context->routeTextComposition(
            window,
            Platform::PlatformFrameId{sequence},
            sequence,
            preedit,
            0,
            stage);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, expectedError);
        expectCompositionStateUnchanged();
    };

    expectInvalidComposition(
        2,
        "new",
        static_cast<Platform::TextCompositionStage>(255),
        UI::UIErrorCode::InvalidText);
    expectInvalidComposition(
        3,
        std::string_view{"\xC3\x28", 2},
        Platform::TextCompositionStage::Updated,
        UI::UIErrorCode::InvalidText);
    expectInvalidComposition(
        4,
        std::string_view{"A\0B", 3},
        Platform::TextCompositionStage::Updated,
        UI::UIErrorCode::InvalidText);

    const std::string oversizedPreedit(513, 'x');
    expectInvalidComposition(
        5,
        oversizedPreedit,
        Platform::TextCompositionStage::Updated,
        UI::UIErrorCode::CapacityExceeded);

    for (const auto [sequence, invalidInput] : {
             std::pair{6ULL, std::string_view{"\xE2\x82", 2}},
             std::pair{7ULL, std::string_view{"A\0B", 3}},
         }) {
        auto input = context->routeTextInput(
            window,
            Platform::PlatformFrameId{sequence},
            sequence,
            invalidInput);
        ASSERT_FALSE(input.has_value());
        EXPECT_EQ(input.error().code, UI::UIErrorCode::InvalidText);
        expectCompositionStateUnchanged();
    }
}

TEST_F(UITextEditTest, CompositionByteLimitAndProgrammaticTextClampState)
{
    constexpr std::string_view InitialUtf8 = "A" "\xE4\xBD\xA0" "BC";
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setText(textEdit, InitialUtf8));
    assertOk(updater.setTextSelection(
        textEdit,
        {.anchorCodepoint = 1, .caretCodepoint = 4}));
    focusWithTab(textEdit);

    const std::string maximumPreedit(512, 'x');
    auto composition = context->routeTextComposition(
        window,
        Platform::PlatformFrameId{1},
        1,
        maximumPreedit,
        999,
        Platform::TextCompositionStage::Started);
    ASSERT_TRUE(composition.has_value())
        << (composition ? "" : composition.error().message);
    EXPECT_TRUE(composition->consumed);
    EXPECT_TRUE(composition->applied);
    EXPECT_EQ(context->imePreeditUtf8(), maximumPreedit);
    EXPECT_EQ(context->imePreeditCursorCodepoint(), 512U);

    const Core::Status invalidReplacement = updater.setText(
        textEdit,
        std::string_view{"\xED\xA0\x80", 3});
    ASSERT_FALSE(invalidReplacement.has_value());
    EXPECT_EQ(invalidReplacement.error().code, UI::UIErrorCode::InvalidText);
    EXPECT_TRUE(context->imeCompositionActive());
    EXPECT_EQ(context->imePreeditUtf8(), maximumPreedit);

    assertOk(updater.setText(textEdit, "X"));
    EXPECT_FALSE(context->imeCompositionActive());
    EXPECT_TRUE(context->imePreeditUtf8().empty());
    EXPECT_EQ(context->imePreeditCursorCodepoint(), 0U);
    auto selection = updater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value())
        << (selection ? "" : selection.error().message);
    EXPECT_EQ(
        *selection,
        (UI::UITextSelection{.anchorCodepoint = 1, .caretCodepoint = 1}));

    composition = context->routeTextComposition(
        window,
        Platform::PlatformFrameId{2},
        2,
        "again",
        5,
        Platform::TextCompositionStage::Started);
    ASSERT_TRUE(composition.has_value())
        << (composition ? "" : composition.error().message);
    ASSERT_TRUE(context->imeCompositionActive());

    assertOk(updater.setText(textEdit, "X"));
    EXPECT_FALSE(context->imeCompositionActive());
    EXPECT_TRUE(context->imePreeditUtf8().empty());
    EXPECT_EQ(context->imePreeditCursorCodepoint(), 0U);
}

TEST_F(UITextEditTest, TextInputAndImeCommitReplaceUnicodeSelection)
{
    constexpr std::string_view InitialUtf8 = "A" "\xE4\xBD\xA0" "B";
    constexpr std::string_view ReplacedUtf8 = "A" "\xE5\xA5\xBD" "B";
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setText(textEdit, InitialUtf8));
    focusWithTab(textEdit);
    assertOk(updater.setTextSelection(
        textEdit,
        {.anchorCodepoint = 1, .caretCodepoint = 2}));

    auto composition = context->routeTextComposition(
        window,
        Platform::PlatformFrameId{1},
        1,
        "ni",
        1,
        Platform::TextCompositionStage::Started);
    ASSERT_TRUE(composition.has_value()) << (composition ? "" : composition.error().message);
    EXPECT_TRUE(composition->consumed);
    EXPECT_TRUE(composition->applied);
    EXPECT_TRUE(context->imeCompositionActive());
    EXPECT_EQ(context->imePreeditUtf8(), "ni");
    EXPECT_EQ(context->imePreeditCursorCodepoint(), 1U);

    auto beforeCommit = updater.text(textEdit);
    ASSERT_TRUE(beforeCommit.has_value());
    EXPECT_EQ(*beforeCommit, InitialUtf8);

    auto commit = context->routeTextInput(
        window,
        Platform::PlatformFrameId{2},
        2,
        "\xE5\xA5\xBD");
    ASSERT_TRUE(commit.has_value()) << (commit ? "" : commit.error().message);
    EXPECT_TRUE(commit->consumed);
    EXPECT_TRUE(commit->applied);
    EXPECT_FALSE(context->imeCompositionActive());

    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, ReplacedUtf8);
    auto selection = updater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(
        *selection,
        (UI::UITextSelection{.anchorCodepoint = 2, .caretCodepoint = 2}));

    auto lineBreak = context->routeTextInput(
        window,
        Platform::PlatformFrameId{3},
        3,
        "\n");
    ASSERT_TRUE(lineBreak.has_value());
    EXPECT_TRUE(lineBreak->consumed);
    EXPECT_FALSE(lineBreak->applied);
    text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, ReplacedUtf8);
}

TEST_F(UITextEditTest, SameTextReplacementCollapsesSelectionAndRepublishesPaint)
{
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setText(textEdit, "ABA"));
    focusWithTab(textEdit);
    assertOk(updater.setTextSelection(
        textEdit,
        {.anchorCodepoint = 1, .caretCodepoint = 2}));
    publishLayout();

    const UI::UICommittedPaintView selectedPaint = context->committedPaint();
    const u64 selectedPaintRevision = selectedPaint.paintRevision();
    const UI::UICommittedPaintEntry* selectionHighlight = nullptr;
    const UI::UICommittedPaintEntry* selectedCaret = nullptr;
    for (const UI::UICommittedPaintEntry& entry : selectedPaint.entries()) {
        if (entry.kind != UI::UICommittedPaintKind::Glyph && entry.solidFill.alpha == 190) {
            selectionHighlight = &entry;
        }
        if (entry.kind != UI::UICommittedPaintKind::Glyph && entry.solidFill.red == 255
            && entry.solidFill.green == 255 && entry.solidFill.blue == 255
            && entry.worldRect.width == 2.0F) {
            selectedCaret = &entry;
        }
    }
    ASSERT_NE(selectionHighlight, nullptr);
    ASSERT_NE(selectedCaret, nullptr);
    EXPECT_FLOAT_EQ(
        selectedCaret->worldRect.x,
        selectionHighlight->worldRect.x + selectionHighlight->worldRect.width);
    const float expectedCaretX = selectedCaret->worldRect.x;

    auto replacement = context->routeTextInput(
        window,
        Platform::PlatformFrameId{1},
        1,
        "B");
    ASSERT_TRUE(replacement.has_value())
        << (replacement ? "" : replacement.error().message);
    EXPECT_TRUE(replacement->consumed);
    EXPECT_TRUE(replacement->applied);

    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value()) << (text ? "" : text.error().message);
    EXPECT_EQ(*text, "ABA");
    auto selection = updater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value())
        << (selection ? "" : selection.error().message);
    EXPECT_EQ(
        *selection,
        (UI::UITextSelection{.anchorCodepoint = 2, .caretCodepoint = 2}));

    publishLayout();
    const UI::UICommittedPaintView collapsedPaint = context->committedPaint();
    EXPECT_EQ(collapsedPaint.paintRevision(), selectedPaintRevision + 1U);
    const UI::UICommittedPaintEntry* collapsedCaret = nullptr;
    for (const UI::UICommittedPaintEntry& entry : collapsedPaint.entries()) {
        EXPECT_FALSE(entry.kind != UI::UICommittedPaintKind::Glyph && entry.solidFill.alpha == 190);
        if (entry.kind != UI::UICommittedPaintKind::Glyph && entry.solidFill.red == 255
            && entry.solidFill.green == 255 && entry.solidFill.blue == 255
            && entry.worldRect.width == 2.0F) {
            collapsedCaret = &entry;
        }
    }
    ASSERT_NE(collapsedCaret, nullptr);
    EXPECT_FLOAT_EQ(collapsedCaret->worldRect.x, expectedCaretX);
}

TEST_F(UITextEditTest, CommandsNavigateSelectAndDeleteUnicodeScalars)
{
    constexpr std::string_view InitialUtf8 = "A" "\xE4\xBD\xA0" "BC";
    constexpr std::string_view WithoutLastUtf8 = "A" "\xE4\xBD\xA0" "B";
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setText(textEdit, InitialUtf8));
    focusWithTab(textEdit);
    assertOk(updater.setTextSelection(
        textEdit,
        {.anchorCodepoint = 4, .caretCodepoint = 4}));

    const auto routeCommand = [&](u64 sequence,
                                  UI::UITextEditCommand command,
                                  bool extendSelection = false) {
        auto result = context->routeTextEditCommand(
            window,
            Platform::PlatformFrameId{sequence},
            sequence,
            command,
            extendSelection);
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        if (result) {
            EXPECT_TRUE(result->consumed);
        }
        return result;
    };
    const auto expectSelection = [&](u32 anchor, u32 caret) {
        auto selection = updater.textSelection(textEdit);
        ASSERT_TRUE(selection.has_value()) << (selection ? "" : selection.error().message);
        EXPECT_EQ(selection->anchorCodepoint, anchor);
        EXPECT_EQ(selection->caretCodepoint, caret);
    };

    ASSERT_TRUE(routeCommand(1, UI::UITextEditCommand::MoveLeft).has_value());
    expectSelection(3, 3);
    ASSERT_TRUE(routeCommand(2, UI::UITextEditCommand::MoveLeft, true).has_value());
    expectSelection(3, 2);
    ASSERT_TRUE(routeCommand(3, UI::UITextEditCommand::MoveHome, true).has_value());
    expectSelection(3, 0);
    ASSERT_TRUE(routeCommand(4, UI::UITextEditCommand::MoveRight).has_value());
    expectSelection(3, 3);
    ASSERT_TRUE(routeCommand(5, UI::UITextEditCommand::MoveEnd).has_value());
    expectSelection(4, 4);

    auto backspace = routeCommand(6, UI::UITextEditCommand::Backspace);
    ASSERT_TRUE(backspace.has_value());
    EXPECT_TRUE(backspace->applied);
    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, WithoutLastUtf8);
    expectSelection(3, 3);

    assertOk(updater.setTextSelection(
        textEdit,
        {.anchorCodepoint = 1, .caretCodepoint = 2}));
    auto deleteSelection = routeCommand(7, UI::UITextEditCommand::Delete);
    ASSERT_TRUE(deleteSelection.has_value());
    EXPECT_TRUE(deleteSelection->applied);
    text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "AB");
    expectSelection(1, 1);

    ASSERT_TRUE(routeCommand(8, UI::UITextEditCommand::SelectAll).has_value());
    expectSelection(0, 2);
    auto replaceAll = context->routeTextInput(
        window,
        Platform::PlatformFrameId{9},
        9,
        "Z");
    ASSERT_TRUE(replaceAll.has_value());
    EXPECT_TRUE(replaceAll->consumed);
    EXPECT_TRUE(replaceAll->applied);
    text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "Z");
    expectSelection(1, 1);
}

TEST_F(UITextEditTest, PaintPublishesSelectionPreeditAndCaretAtTheEditPosition)
{
    constexpr UI::UITextEditPaint Paint{
        .selectionBackgroundColor = {.red = 18, .green = 102, .blue = 170, .alpha = 210},
        .caretColor = UI::rgb(0xF2C94C),
    };
    const UI::UIPremultipliedRgba8Color expectedSelection = UI::premultiply(Paint.selectionBackgroundColor);
    const UI::UIPremultipliedRgba8Color expectedCaret = UI::premultiply(Paint.caretColor);
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setTextEditPaint(textEdit, Paint));
    assertOk(updater.setText(textEdit, "ABC"));
    focusWithTab(textEdit);
    assertOk(updater.setTextSelection(
        textEdit,
        {.anchorCodepoint = 1, .caretCodepoint = 2}));
    publishLayout();

    const UI::UICommittedPaintView selectedPaint = context->committedPaint();
    ASSERT_EQ(selectedPaint.size(), 5U);
    const UI::UICommittedPaintEntry* selectionPaint = nullptr;
    const UI::UICommittedPaintEntry* selectedCaret = nullptr;
    for (const UI::UICommittedPaintEntry& entry : selectedPaint.entries()) {
        EXPECT_EQ(entry.node, textEdit);
        if (entry.kind != UI::UICommittedPaintKind::Glyph && entry.solidFill == expectedSelection) {
            selectionPaint = &entry;
        }
        if (entry.kind != UI::UICommittedPaintKind::Glyph && entry.solidFill == expectedCaret) {
            selectedCaret = &entry;
        }
    }
    ASSERT_NE(selectionPaint, nullptr);
    EXPECT_GT(selectionPaint->worldRect.width, 2.0F);
    ASSERT_NE(selectedCaret, nullptr);
    EXPECT_FLOAT_EQ(selectedCaret->worldRect.width, 2.0F);
    EXPECT_GT(selectedCaret->worldRect.x, selectionPaint->worldRect.x);

    auto composition = context->routeTextComposition(
        window,
        Platform::PlatformFrameId{1},
        1,
        "XY",
        1,
        Platform::TextCompositionStage::Started);
    ASSERT_TRUE(composition.has_value());
    publishLayout();

    const UI::UICommittedPaintView preeditPaint = context->committedPaint();
    ASSERT_EQ(preeditPaint.size(), 5U);
    usize preeditGlyphCount = 0;
    float firstPreeditX = 0.0F;
    float secondPreeditX = 0.0F;
    const UI::UICommittedPaintEntry* preeditCaret = nullptr;
    for (const UI::UICommittedPaintEntry& entry : preeditPaint.entries()) {
        if (entry.kind == UI::UICommittedPaintKind::Glyph && entry.solidFill.red == 0
            && entry.solidFill.green == 180 && entry.solidFill.blue == 255) {
            if (preeditGlyphCount == 0) {
                firstPreeditX = entry.worldRect.x;
            } else if (preeditGlyphCount == 1) {
                secondPreeditX = entry.worldRect.x;
            }
            ++preeditGlyphCount;
        }
        if (entry.kind != UI::UICommittedPaintKind::Glyph && entry.solidFill == expectedCaret) {
            preeditCaret = &entry;
        }
        EXPECT_FALSE(entry.kind != UI::UICommittedPaintKind::Glyph && entry.solidFill == expectedSelection);
    }
    EXPECT_EQ(preeditGlyphCount, 2U);
    EXPECT_GT(secondPreeditX, firstPreeditX);
    ASSERT_NE(preeditCaret, nullptr);
    EXPECT_FLOAT_EQ(preeditCaret->worldRect.x, secondPreeditX);
}

TEST_F(UITextEditTest, PointerSelectionUsesRasterizedVariableGlyphAdvances)
{
    auto localContextResult = UI::UIContext::Create(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 8,
            .routePathCapacity = 8,
            .textByteCapacity = 64,
        },
        std::make_unique<VariableAdvanceTextRasterizer>());
    ASSERT_TRUE(localContextResult.has_value())
        << (localContextResult ? "" : localContextResult.error().message);
    auto localContext = std::move(*localContextResult);
    auto localRoot = createRoot(*localContext);
    ASSERT_TRUE(localRoot.hasValue());
    auto localUpdater = createUpdater(*localContext, localRoot);
    assertOk(localUpdater.setLayoutStyle(
        localRoot.rootNodeId(),
        fixedSize(120.0F, 40.0F)));
    auto textEditResult = localUpdater.createElement(localRoot.rootNodeId(), UI::makeTextEditElement());
    ASSERT_TRUE(textEditResult.has_value())
        << (textEditResult ? "" : textEditResult.error().message);
    const UI::UINodeId textEdit = *textEditResult;
    UI::UILayoutStyle textEditStyle = fixedSize(120.0F, 32.0F);
    textEditStyle.padding.left = 12.0F;
    assertOk(localUpdater.setLayoutStyle(textEdit, textEditStyle));
    assertOk(localUpdater.setText(textEdit, "Wi"));
    assertOk(localContext->commitLayout({.width = 120.0F, .height = 40.0F}));

    auto down = localContext->routePointerInput(
        makePrimaryPointerDown(window, 1, 30.0F, 10.0F));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    auto selection = localUpdater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value())
        << (selection ? "" : selection.error().message);
    EXPECT_EQ(selection->anchorCodepoint, 1U);
    EXPECT_EQ(selection->caretCodepoint, 1U);
}

TEST_F(UITextEditTest, RejectsUnknownCommandWithoutClearingComposition)
{
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setText(textEdit, "AB"));
    focusWithTab(textEdit);
    auto composition = context->routeTextComposition(
        window,
        Platform::PlatformFrameId{1},
        1,
        "x",
        1,
        Platform::TextCompositionStage::Started);
    ASSERT_TRUE(composition.has_value())
        << (composition ? "" : composition.error().message);
    ASSERT_TRUE(context->imeCompositionActive());

    auto command = context->routeTextEditCommand(
        window,
        Platform::PlatformFrameId{2},
        2,
        static_cast<UI::UITextEditCommand>(255),
        false);
    ASSERT_FALSE(command.has_value());
    EXPECT_EQ(command.error().code, UI::UIErrorCode::InvalidText);
    EXPECT_TRUE(context->imeCompositionActive());
    EXPECT_EQ(context->imePreeditUtf8(), "x");
}

} // namespace
} // namespace Tina::Tests
