#include "UITextEditTestSupport.hpp"

#include <tina/platform/ProcessLocalClipboard.hpp>

#include <string>
#include <string_view>

namespace Tina::Tests {
namespace {

using namespace UITextEditTestSupport;

// A clipboard that refuses every write, which is the ordinary Win32 outcome when
// another process holds the global lock. Reads still work: the failure being
// modelled is a write refusal, not an unusable clipboard.
class WriteRefusingClipboard final : public Platform::IClipboard {
  public:
    [[nodiscard]] Core::Result<Platform::ClipboardTextRead> readTextUtf8(
        std::span<char> destination) override
    {
        return inner_.readTextUtf8(destination);
    }

    [[nodiscard]] Core::Status writeTextUtf8(std::string_view) override
    {
        ++refusedWrites;
        return Core::failure(Platform::PlatformErrorCode::ClipboardUnavailable,
                             "Test clipboard refuses every write");
    }

    [[nodiscard]] Platform::ProcessLocalClipboard& inner() noexcept { return inner_; }

    usize refusedWrites = 0;

  private:
    Platform::ProcessLocalClipboard inner_{};
};

class UITextClipboardTest : public UITextEditTest {
  protected:
    [[nodiscard]] std::string clipboardText(Platform::IClipboard& clipboard)
    {
        auto probe = clipboard.readTextUtf8({});
        EXPECT_TRUE(probe.has_value()) << (probe ? "" : probe.error().message);
        if (!probe || !probe->hasText) {
            return {};
        }
        std::string text(probe->totalBytes, '\0');
        auto read = clipboard.readTextUtf8(std::span<char>(text.data(), text.size()));
        EXPECT_TRUE(read.has_value()) << (read ? "" : read.error().message);
        if (!read) {
            return {};
        }
        text.resize(read->bytesWritten);
        return text;
    }

    [[nodiscard]] Core::Result<UI::UITextClipboardRouteResult> route(
        u64 sequence, UI::UITextClipboardCommand command, Platform::IClipboard& clipboard)
    {
        return context->text().routeTextClipboardCommand(
            window, Platform::PlatformFrameId{sequence}, sequence, command, clipboard);
    }

    [[nodiscard]] UI::UINodeId createFocusedTextEdit(std::string_view initialText)
    {
        auto result = updater.createElement(
            root.rootNodeId(), UI::makeTextEditElement(initialText));
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        if (!result) {
            return {};
        }
        EXPECT_TRUE(updater.setLayoutStyle(*result, fixedSize(240.0F, 32.0F)).has_value());
        focusWithTab(*result);
        return *result;
    }
};

TEST_F(UITextClipboardTest, CopyPublishesSelectionAndLeavesTextIntact)
{
    Platform::ProcessLocalClipboard clipboard;
    const UI::UINodeId textEdit = createFocusedTextEdit("ABCDEF");
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setTextSelection(
        textEdit, {.anchorCodepoint = 1U, .caretCodepoint = 4U}));

    auto copied = route(1U, UI::UITextClipboardCommand::Copy, clipboard);
    ASSERT_TRUE(copied.has_value()) << (copied ? "" : copied.error().message);
    EXPECT_TRUE(copied->consumed);
    EXPECT_TRUE(copied->applied);
    EXPECT_FALSE(copied->truncatedToFirstLine);
    EXPECT_EQ(clipboardText(clipboard), "BCD");

    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "ABCDEF");
    auto selection = updater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(*selection,
              (UI::UITextSelection{.anchorCodepoint = 1U, .caretCodepoint = 4U}));
}

TEST_F(UITextClipboardTest, CutPublishesSelectionThenRemovesIt)
{
    Platform::ProcessLocalClipboard clipboard;
    const UI::UINodeId textEdit = createFocusedTextEdit("ABCDEF");
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setTextSelection(
        textEdit, {.anchorCodepoint = 2U, .caretCodepoint = 5U}));

    auto cut = route(1U, UI::UITextClipboardCommand::Cut, clipboard);
    ASSERT_TRUE(cut.has_value()) << (cut ? "" : cut.error().message);
    EXPECT_TRUE(cut->consumed);
    EXPECT_TRUE(cut->applied);
    EXPECT_EQ(clipboardText(clipboard), "CDE");

    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "ABF");
}

// The write must precede the delete. If a refused write still deleted, the user
// would lose the selection with nothing on the clipboard to paste back.
TEST_F(UITextClipboardTest, CutKeepsSelectionWhenTheClipboardRefusesTheWrite)
{
    WriteRefusingClipboard clipboard;
    const UI::UINodeId textEdit = createFocusedTextEdit("ABCDEF");
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setTextSelection(
        textEdit, {.anchorCodepoint = 2U, .caretCodepoint = 5U}));

    auto cut = route(1U, UI::UITextClipboardCommand::Cut, clipboard);
    ASSERT_FALSE(cut.has_value());
    EXPECT_EQ(cut.error().code, Platform::PlatformErrorCode::ClipboardUnavailable);
    EXPECT_EQ(clipboard.refusedWrites, 1U);

    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "ABCDEF");
    auto selection = updater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(*selection,
              (UI::UITextSelection{.anchorCodepoint = 2U, .caretCodepoint = 5U}));
}

TEST_F(UITextClipboardTest, CollapsedSelectionCopyDoesNotClearTheClipboard)
{
    Platform::ProcessLocalClipboard clipboard;
    assertOk(clipboard.writeTextUtf8("KEEP"));
    const UI::UINodeId textEdit = createFocusedTextEdit("ABCDEF");
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setTextSelection(
        textEdit, {.anchorCodepoint = 3U, .caretCodepoint = 3U}));

    for (const UI::UITextClipboardCommand command :
         {UI::UITextClipboardCommand::Copy, UI::UITextClipboardCommand::Cut}) {
        auto routed = route(1U, command, clipboard);
        ASSERT_TRUE(routed.has_value()) << (routed ? "" : routed.error().message);
        // Consumed but not applied: the chord belongs to the focused text edit,
        // so it must not fall through, yet there is nothing to publish.
        EXPECT_TRUE(routed->consumed);
        EXPECT_FALSE(routed->applied);
        EXPECT_EQ(clipboardText(clipboard), "KEEP");
    }

    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "ABCDEF");
}

TEST_F(UITextClipboardTest, PasteReplacesTheSelection)
{
    Platform::ProcessLocalClipboard clipboard;
    assertOk(clipboard.writeTextUtf8("XY"));
    const UI::UINodeId textEdit = createFocusedTextEdit("ABCDEF");
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setTextSelection(
        textEdit, {.anchorCodepoint = 1U, .caretCodepoint = 4U}));

    auto pasted = route(1U, UI::UITextClipboardCommand::Paste, clipboard);
    ASSERT_TRUE(pasted.has_value()) << (pasted ? "" : pasted.error().message);
    EXPECT_TRUE(pasted->consumed);
    EXPECT_TRUE(pasted->applied);
    EXPECT_FALSE(pasted->truncatedToFirstLine);

    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "AXYEF");
}

TEST_F(UITextClipboardTest, PasteFromAnEmptyClipboardIsConsumedWithoutChange)
{
    Platform::ProcessLocalClipboard clipboard;
    const UI::UINodeId textEdit = createFocusedTextEdit("ABC");
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setTextSelection(
        textEdit, {.anchorCodepoint = 1U, .caretCodepoint = 1U}));

    auto pasted = route(1U, UI::UITextClipboardCommand::Paste, clipboard);
    ASSERT_TRUE(pasted.has_value()) << (pasted ? "" : pasted.error().message);
    EXPECT_TRUE(pasted->consumed);
    EXPECT_FALSE(pasted->applied);

    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "ABC");
}

// A single-line edit cannot hold the newline, and rejecting the whole paste would
// make copying from any multi-line source do nothing. Truncating is reported so
// the caller can surface it.
TEST_F(UITextClipboardTest, SingleLinePasteTruncatesAtTheFirstNewline)
{
    Platform::ProcessLocalClipboard clipboard;
    assertOk(clipboard.writeTextUtf8("first\nsecond"));
    const UI::UINodeId textEdit = createFocusedTextEdit("");
    ASSERT_TRUE(textEdit.hasValue());

    auto pasted = route(1U, UI::UITextClipboardCommand::Paste, clipboard);
    ASSERT_TRUE(pasted.has_value()) << (pasted ? "" : pasted.error().message);
    EXPECT_TRUE(pasted->consumed);
    EXPECT_TRUE(pasted->applied);
    EXPECT_TRUE(pasted->truncatedToFirstLine);

    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "first");
}

TEST_F(UITextClipboardTest, MultilinePasteKeepsEveryLine)
{
    Platform::ProcessLocalClipboard clipboard;
    assertOk(clipboard.writeTextUtf8("first\nsecond"));
    UI::UIElementDescriptor descriptor = UI::makeTextEditElement("");
    descriptor.textEditMultiline = {
        .enabled = true,
        .wrapMode = UI::UITextEditWrapMode::NoWrap,
        .maximumBytes = 64,
        .maximumVisualLines = 4,
    };
    auto result = updater.createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    const UI::UINodeId textEdit = *result;
    assertOk(updater.setLayoutStyle(textEdit, fixedSize(240.0F, 80.0F)));
    focusWithTab(textEdit);

    auto pasted = route(1U, UI::UITextClipboardCommand::Paste, clipboard);
    ASSERT_TRUE(pasted.has_value()) << (pasted ? "" : pasted.error().message);
    EXPECT_TRUE(pasted->applied);
    EXPECT_FALSE(pasted->truncatedToFirstLine);

    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "first\nsecond");
}

// routeTextInput rejects a lone '\r' outright, so a CRLF clipboard would make
// every Windows paste fail if the platform layer did not normalize. This pins the
// whole chain, not just the normalizer in isolation.
TEST_F(UITextClipboardTest, CrlfOnTheClipboardPastesAsLf)
{
    Platform::ProcessLocalClipboard clipboard;
    assertOk(clipboard.writeTextUtf8("a\r\nb"));
    UI::UIElementDescriptor descriptor = UI::makeTextEditElement("");
    descriptor.textEditMultiline = {
        .enabled = true,
        .wrapMode = UI::UITextEditWrapMode::NoWrap,
        .maximumBytes = 64,
        .maximumVisualLines = 4,
    };
    auto result = updater.createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    const UI::UINodeId textEdit = *result;
    assertOk(updater.setLayoutStyle(textEdit, fixedSize(240.0F, 80.0F)));
    focusWithTab(textEdit);

    auto pasted = route(1U, UI::UITextClipboardCommand::Paste, clipboard);
    ASSERT_TRUE(pasted.has_value()) << (pasted ? "" : pasted.error().message);
    EXPECT_TRUE(pasted->applied);

    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(text->find('\r'), std::string::npos);
    EXPECT_EQ(*text, "a\nb");
}

TEST_F(UITextClipboardTest, WithoutFocusEveryCommandIsUnconsumed)
{
    Platform::ProcessLocalClipboard clipboard;
    assertOk(clipboard.writeTextUtf8("XY"));
    publishLayout();
    ASSERT_FALSE(context->text().imeFocus().hasValue());

    for (const UI::UITextClipboardCommand command :
         {UI::UITextClipboardCommand::Copy, UI::UITextClipboardCommand::Cut,
          UI::UITextClipboardCommand::Paste}) {
        auto routed = route(1U, command, clipboard);
        ASSERT_TRUE(routed.has_value()) << (routed ? "" : routed.error().message);
        EXPECT_FALSE(routed->consumed);
        EXPECT_FALSE(routed->applied);
    }
    EXPECT_EQ(clipboardText(clipboard), "XY");
}

} // namespace
} // namespace Tina::Tests
