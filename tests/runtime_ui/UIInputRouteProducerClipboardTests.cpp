#include "UIInputRouteProducerTestSupport.hpp"

#include <tina/platform/ProcessLocalClipboard.hpp>

#include <span>
#include <string>
#include <string_view>

namespace Tina::Tests {
namespace {

[[nodiscard]] std::string readClipboard(Platform::ProcessLocalClipboard& clipboard)
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

TEST_F(UIInputRouteProducerTest, ControlChordsRouteCopyCutAndPasteThroughTheClipboard)
{
    Platform::ProcessLocalClipboard clipboard;
    auto producer = createProducer();
    RouteTree tree = createTextEditRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    ASSERT_TRUE(tree.target.hasValue());
    expectOk(tree.updater.setText(tree.target, "ABCDEF"));
    expectOk(tree.context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    auto focusFrame = buildFrame(
        *builder, window,
        {
            .frameId = {10},
            .transitions = {keyDown(window, Platform::Key::Tab)},
            .heldKeys = {Platform::Key::Tab},
        });
    ASSERT_TRUE(focusFrame.has_value()) << (focusFrame ? "" : focusFrame.error().message);
    auto focusOutput = producer->produce(tree.context.get(), *focusFrame, &clipboard);
    ASSERT_TRUE(focusOutput.has_value()) << (focusOutput ? "" : focusOutput.error().message);
    EXPECT_EQ(tree.context->text().imeFocus(), tree.target);

    expectOk(tree.updater.setTextSelection(
        tree.target, {.anchorCodepoint = 1U, .caretCodepoint = 4U}));

    auto copyFrame = buildFrame(
        *builder, window,
        {
            .frameId = {11},
            .transitions = {keyDown(window, Platform::Key::C)},
            .heldKeys = {Platform::Key::C, Platform::Key::LeftControl},
        });
    ASSERT_TRUE(copyFrame.has_value()) << (copyFrame ? "" : copyFrame.error().message);
    auto copyOutput = producer->produce(tree.context.get(), *copyFrame, &clipboard);
    ASSERT_TRUE(copyOutput.has_value()) << (copyOutput ? "" : copyOutput.error().message);
    EXPECT_TRUE(copyOutput->consumption.isConsumed(0));
    EXPECT_EQ(readClipboard(clipboard), "BCD");
    auto text = tree.updater.text(tree.target);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "ABCDEF");

    auto cutFrame = buildFrame(
        *builder, window,
        {
            .frameId = {12},
            .transitions = {keyDown(window, Platform::Key::X)},
            .heldKeys = {Platform::Key::X, Platform::Key::LeftControl},
        });
    ASSERT_TRUE(cutFrame.has_value()) << (cutFrame ? "" : cutFrame.error().message);
    auto cutOutput = producer->produce(tree.context.get(), *cutFrame, &clipboard);
    ASSERT_TRUE(cutOutput.has_value()) << (cutOutput ? "" : cutOutput.error().message);
    EXPECT_TRUE(cutOutput->consumption.isConsumed(0));
    EXPECT_EQ(readClipboard(clipboard), "BCD");
    text = tree.updater.text(tree.target);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "AEF");

    auto pasteFrame = buildFrame(
        *builder, window,
        {
            .frameId = {13},
            .transitions = {keyDown(window, Platform::Key::V)},
            .heldKeys = {Platform::Key::V, Platform::Key::LeftControl},
        });
    ASSERT_TRUE(pasteFrame.has_value()) << (pasteFrame ? "" : pasteFrame.error().message);
    auto pasteOutput = producer->produce(tree.context.get(), *pasteFrame, &clipboard);
    ASSERT_TRUE(pasteOutput.has_value()) << (pasteOutput ? "" : pasteOutput.error().message);
    EXPECT_TRUE(pasteOutput->consumption.isConsumed(0));
    text = tree.updater.text(tree.target);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "ABCDEF");
}

// Shift+Insert / Ctrl+Insert / Shift+Delete are the pre-Ctrl chords, and Windows
// still delivers them from real keyboards.
TEST_F(UIInputRouteProducerTest, LegacyInsertAndDeleteChordsRouteToTheClipboard)
{
    Platform::ProcessLocalClipboard clipboard;
    auto producer = createProducer();
    RouteTree tree = createTextEditRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_TRUE(tree.target.hasValue());
    expectOk(tree.updater.setText(tree.target, "ABCDEF"));
    expectOk(tree.context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    auto focusFrame = buildFrame(
        *builder, window,
        {
            .frameId = {20},
            .transitions = {keyDown(window, Platform::Key::Tab)},
            .heldKeys = {Platform::Key::Tab},
        });
    ASSERT_TRUE(focusFrame.has_value()) << (focusFrame ? "" : focusFrame.error().message);
    auto focusOutput = producer->produce(tree.context.get(), *focusFrame, &clipboard);
    ASSERT_TRUE(focusOutput.has_value()) << (focusOutput ? "" : focusOutput.error().message);

    expectOk(tree.updater.setTextSelection(
        tree.target, {.anchorCodepoint = 0U, .caretCodepoint = 2U}));

    auto ctrlInsertFrame = buildFrame(
        *builder, window,
        {
            .frameId = {21},
            .transitions = {keyDown(window, Platform::Key::Insert)},
            .heldKeys = {Platform::Key::Insert, Platform::Key::LeftControl},
        });
    ASSERT_TRUE(ctrlInsertFrame.has_value())
        << (ctrlInsertFrame ? "" : ctrlInsertFrame.error().message);
    auto ctrlInsertOutput = producer->produce(tree.context.get(), *ctrlInsertFrame, &clipboard);
    ASSERT_TRUE(ctrlInsertOutput.has_value())
        << (ctrlInsertOutput ? "" : ctrlInsertOutput.error().message);
    EXPECT_TRUE(ctrlInsertOutput->consumption.isConsumed(0));
    EXPECT_EQ(readClipboard(clipboard), "AB");
    auto text = tree.updater.text(tree.target);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "ABCDEF");

    expectOk(tree.updater.setTextSelection(
        tree.target, {.anchorCodepoint = 4U, .caretCodepoint = 6U}));
    auto shiftDeleteFrame = buildFrame(
        *builder, window,
        {
            .frameId = {22},
            .transitions = {keyDown(window, Platform::Key::Delete)},
            .heldKeys = {Platform::Key::Delete, Platform::Key::LeftShift},
        });
    ASSERT_TRUE(shiftDeleteFrame.has_value())
        << (shiftDeleteFrame ? "" : shiftDeleteFrame.error().message);
    auto shiftDeleteOutput = producer->produce(tree.context.get(), *shiftDeleteFrame, &clipboard);
    ASSERT_TRUE(shiftDeleteOutput.has_value())
        << (shiftDeleteOutput ? "" : shiftDeleteOutput.error().message);
    EXPECT_TRUE(shiftDeleteOutput->consumption.isConsumed(0));
    EXPECT_EQ(readClipboard(clipboard), "EF");
    text = tree.updater.text(tree.target);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "ABCD");

    auto shiftInsertFrame = buildFrame(
        *builder, window,
        {
            .frameId = {23},
            .transitions = {keyDown(window, Platform::Key::Insert)},
            .heldKeys = {Platform::Key::Insert, Platform::Key::LeftShift},
        });
    ASSERT_TRUE(shiftInsertFrame.has_value())
        << (shiftInsertFrame ? "" : shiftInsertFrame.error().message);
    auto shiftInsertOutput = producer->produce(tree.context.get(), *shiftInsertFrame, &clipboard);
    ASSERT_TRUE(shiftInsertOutput.has_value())
        << (shiftInsertOutput ? "" : shiftInsertOutput.error().message);
    EXPECT_TRUE(shiftInsertOutput->consumption.isConsumed(0));
    text = tree.updater.text(tree.target);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "ABCDEF");
}

// The data-loss guard: with no clipboard, Shift+Delete must not decay into plain
// Delete. Falling through would remove the selection with nothing copied.
TEST_F(UIInputRouteProducerTest, ShiftDeleteWithoutAClipboardKeepsTheSelection)
{
    auto producer = createProducer();
    RouteTree tree = createTextEditRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_TRUE(tree.target.hasValue());
    expectOk(tree.updater.setText(tree.target, "ABCDEF"));
    expectOk(tree.context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    auto focusFrame = buildFrame(
        *builder, window,
        {
            .frameId = {30},
            .transitions = {keyDown(window, Platform::Key::Tab)},
            .heldKeys = {Platform::Key::Tab},
        });
    ASSERT_TRUE(focusFrame.has_value()) << (focusFrame ? "" : focusFrame.error().message);
    auto focusOutput = producer->produce(tree.context.get(), *focusFrame, nullptr);
    ASSERT_TRUE(focusOutput.has_value()) << (focusOutput ? "" : focusOutput.error().message);

    expectOk(tree.updater.setTextSelection(
        tree.target, {.anchorCodepoint = 1U, .caretCodepoint = 4U}));

    auto shiftDeleteFrame = buildFrame(
        *builder, window,
        {
            .frameId = {31},
            .transitions = {keyDown(window, Platform::Key::Delete)},
            .heldKeys = {Platform::Key::Delete, Platform::Key::LeftShift},
        });
    ASSERT_TRUE(shiftDeleteFrame.has_value())
        << (shiftDeleteFrame ? "" : shiftDeleteFrame.error().message);
    auto shiftDeleteOutput = producer->produce(tree.context.get(), *shiftDeleteFrame, nullptr);
    ASSERT_TRUE(shiftDeleteOutput.has_value())
        << (shiftDeleteOutput ? "" : shiftDeleteOutput.error().message);
    // Still claimed, so the chord does not reach any other handler.
    EXPECT_TRUE(shiftDeleteOutput->consumption.isConsumed(0));
    auto text = tree.updater.text(tree.target);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "ABCDEF");
    auto selection = tree.updater.textSelection(tree.target);
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(*selection,
              (UI::UITextSelection{.anchorCodepoint = 1U, .caretCodepoint = 4U}));
}

// Ctrl+V without a clipboard must not reach the plain-Delete path either, and an
// unmodified V must still type a character.
TEST_F(UIInputRouteProducerTest, UnmodifiedKeysAreNotMistakenForClipboardChords)
{
    Platform::ProcessLocalClipboard clipboard;
    auto producer = createProducer();
    RouteTree tree = createTextEditRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_TRUE(tree.target.hasValue());
    expectOk(tree.updater.setText(tree.target, "AB"));
    expectOk(tree.context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    auto focusFrame = buildFrame(
        *builder, window,
        {
            .frameId = {40},
            .transitions = {keyDown(window, Platform::Key::Tab)},
            .heldKeys = {Platform::Key::Tab},
        });
    ASSERT_TRUE(focusFrame.has_value()) << (focusFrame ? "" : focusFrame.error().message);
    auto focusOutput = producer->produce(tree.context.get(), *focusFrame, &clipboard);
    ASSERT_TRUE(focusOutput.has_value()) << (focusOutput ? "" : focusOutput.error().message);

    expectOk(tree.updater.setTextSelection(
        tree.target, {.anchorCodepoint = 0U, .caretCodepoint = 2U}));

    // Bare C/X/V carry no Control, so they are not clipboard chords. Key
    // transitions alone never insert text; only TextInputTransition does.
    u64 nextFrame = 41;
    for (const Platform::Key key :
         {Platform::Key::C, Platform::Key::X, Platform::Key::V}) {
        auto frame = buildFrame(
            *builder, window,
            {
                .frameId = {nextFrame++},
                .transitions = {keyDown(window, key)},
                .heldKeys = {key},
            });
        ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
        auto output = producer->produce(tree.context.get(), *frame, &clipboard);
        ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    }
    // Nothing was copied and nothing was deleted.
    EXPECT_FALSE(clipboard.hasText());
    auto text = tree.updater.text(tree.target);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "AB");
}

} // namespace
} // namespace Tina::Tests
