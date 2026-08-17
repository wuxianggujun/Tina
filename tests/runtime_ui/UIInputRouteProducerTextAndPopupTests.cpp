#include "UIInputRouteProducerTestSupport.hpp"

#include <string_view>

namespace Tina::Tests {
namespace {

TEST_F(UIInputRouteProducerTest, FocusedTextEditConsumesTabCommandsTextAndAcceptKeys)
{
    constexpr std::string_view InitialUtf8 = "A" "\xE4\xBD\xA0" "B";
    auto producer = createProducer();
    RouteTree tree = createTextEditRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    ASSERT_TRUE(tree.target.hasValue());
    expectOk(tree.updater.setText(tree.target, InitialUtf8));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    auto tabFrame = buildFrame(
        *builder,
        window,
        {
            .frameId = {10},
            .transitions = {keyDown(window, Platform::Key::Tab)},
            .heldKeys = {Platform::Key::Tab},
        });
    ASSERT_TRUE(tabFrame.has_value()) << (tabFrame ? "" : tabFrame.error().message);
    auto tabOutput = producer->produce(tree.context.get(), *tabFrame);
    ASSERT_TRUE(tabOutput.has_value()) << (tabOutput ? "" : tabOutput.error().message);
    EXPECT_TRUE(tabOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.target);
    EXPECT_EQ(tree.context->imeFocus(), tree.target);

    auto compositionFrame = buildFrame(
        *builder,
        window,
        {
            .frameId = {11},
            .transitions = {Platform::TextCompositionTransition{
                .window = window,
                .preeditUtf8 = "ni",
                .cursorCodepoint = 1,
                .stage = Platform::TextCompositionStage::Started,
            }},
        });
    ASSERT_TRUE(compositionFrame.has_value())
        << (compositionFrame ? "" : compositionFrame.error().message);
    auto compositionOutput = producer->produce(tree.context.get(), *compositionFrame);
    ASSERT_TRUE(compositionOutput.has_value())
        << (compositionOutput ? "" : compositionOutput.error().message);
    EXPECT_TRUE(compositionOutput->consumption.isConsumed(0));
    EXPECT_TRUE(tree.context->imeCompositionActive());
    EXPECT_EQ(tree.context->imePreeditUtf8(), "ni");

    auto selectAllFrame = buildFrame(
        *builder,
        window,
        {
            .frameId = {12},
            .transitions = {keyDown(window, Platform::Key::A)},
            .heldKeys = {Platform::Key::A, Platform::Key::LeftControl},
        });
    ASSERT_TRUE(selectAllFrame.has_value())
        << (selectAllFrame ? "" : selectAllFrame.error().message);
    auto selectAllOutput = producer->produce(tree.context.get(), *selectAllFrame);
    ASSERT_TRUE(selectAllOutput.has_value())
        << (selectAllOutput ? "" : selectAllOutput.error().message);
    EXPECT_TRUE(selectAllOutput->consumption.isConsumed(0));
    auto selection = tree.updater.textSelection(tree.target);
    ASSERT_TRUE(selection.has_value()) << (selection ? "" : selection.error().message);
    EXPECT_EQ(
        *selection,
        (UI::UITextSelection{.anchorCodepoint = 0, .caretCodepoint = 3}));
    EXPECT_FALSE(tree.context->imeCompositionActive());

    auto textFrame = buildFrame(
        *builder,
        window,
        {
            .frameId = {13},
            .transitions = {Platform::TextInputTransition{
                .window = window,
                .committedUtf8 = "Z",
            }},
        });
    ASSERT_TRUE(textFrame.has_value()) << (textFrame ? "" : textFrame.error().message);
    auto textOutput = producer->produce(tree.context.get(), *textFrame);
    ASSERT_TRUE(textOutput.has_value()) << (textOutput ? "" : textOutput.error().message);
    EXPECT_TRUE(textOutput->consumption.isConsumed(0));
    auto text = tree.updater.text(tree.target);
    ASSERT_TRUE(text.has_value()) << (text ? "" : text.error().message);
    EXPECT_EQ(*text, "Z");

    auto backspaceFrame = buildFrame(
        *builder,
        window,
        {
            .frameId = {14},
            .transitions = {keyDown(window, Platform::Key::Backspace)},
            .heldKeys = {Platform::Key::Backspace},
        });
    ASSERT_TRUE(backspaceFrame.has_value())
        << (backspaceFrame ? "" : backspaceFrame.error().message);
    auto backspaceOutput = producer->produce(tree.context.get(), *backspaceFrame);
    ASSERT_TRUE(backspaceOutput.has_value())
        << (backspaceOutput ? "" : backspaceOutput.error().message);
    EXPECT_TRUE(backspaceOutput->consumption.isConsumed(0));
    text = tree.updater.text(tree.target);
    ASSERT_TRUE(text.has_value());
    EXPECT_TRUE(text->empty());

    auto acceptFrame = buildFrame(
        *builder,
        window,
        {
            .frameId = {15},
            .transitions = {
                keyDown(window, Platform::Key::Enter),
                keyDown(window, Platform::Key::Space),
            },
            .heldKeys = {Platform::Key::Enter, Platform::Key::Space},
        });
    ASSERT_TRUE(acceptFrame.has_value()) << (acceptFrame ? "" : acceptFrame.error().message);
    auto acceptOutput = producer->produce(tree.context.get(), *acceptFrame);
    ASSERT_TRUE(acceptOutput.has_value())
        << (acceptOutput ? "" : acceptOutput.error().message);
    EXPECT_TRUE(acceptOutput->consumption.isConsumed(0));
    EXPECT_TRUE(acceptOutput->consumption.isConsumed(1));
    text = tree.updater.text(tree.target);
    ASSERT_TRUE(text.has_value());
    EXPECT_TRUE(text->empty());
}

TEST_F(UIInputRouteProducerTest, FocusedSingleLineTextEditConsumesVerticalCommandsAsNoOps)
{
    auto producer = createProducer();
    RouteTree tree = createTextEditRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    ASSERT_TRUE(tree.target.hasValue());
    expectOk(tree.updater.setText(tree.target, "ABC"));
    expectOk(tree.updater.setTextSelection(
        tree.target, {.anchorCodepoint = 1U, .caretCodepoint = 1U}));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    expectOk(tree.context->requestFocus(tree.target));

    u64 nextFrame = 16;
    for (const Platform::Key key : {Platform::Key::Up, Platform::Key::Down})
    {
        auto frame = buildFrame(
            *builder,
            window,
            {
                .frameId = {nextFrame++},
                .transitions = {keyDown(window, key), keyUp(window, key)},
            });
        ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
        auto output = producer->produce(tree.context.get(), *frame);
        ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
        EXPECT_TRUE(output->consumption.isConsumed(0));
        EXPECT_TRUE(output->consumption.isConsumed(1));
        EXPECT_EQ(tree.context->defaultActionFocus(), tree.target);
        EXPECT_EQ(tree.context->imeFocus(), tree.target);

        const auto selection = tree.updater.textSelection(tree.target);
        ASSERT_TRUE(selection.has_value())
            << (selection ? "" : selection.error().message);
        EXPECT_EQ(
            *selection,
            (UI::UITextSelection{.anchorCodepoint = 1U, .caretCodepoint = 1U}));
    }
}

TEST_F(UIInputRouteProducerTest, FocusedMultilineTextEditRoutesVerticalCommandsBeforeSpatialFocus)
{
    constexpr std::string_view MultilineText = "AB\nX\nAB";
    auto producer = createProducer();
    RouteTree tree = createTextEditRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    ASSERT_TRUE(tree.target.hasValue());

    Core::Status destroyDefaultTextEdit = tree.updater.destroy(tree.target);
    ASSERT_TRUE(destroyDefaultTextEdit.has_value())
        << (destroyDefaultTextEdit ? "" : destroyDefaultTextEdit.error().message);

    UI::UIElementDescriptor descriptor = UI::makeTextEditElement();
    descriptor.textEditMultiline = {
        .enabled = true,
        .wrapMode = UI::UITextEditWrapMode::NoWrap,
        .maximumBytes = 64,
        .maximumVisualLines = 8,
    };
    auto textEdit = tree.updater.createElement(tree.panel, descriptor);
    auto adjacentButton = tree.updater.createElement(tree.panel, UI::makeButtonElement());
    ASSERT_TRUE(textEdit.has_value()) << (textEdit ? "" : textEdit.error().message);
    ASSERT_TRUE(adjacentButton.has_value())
        << (adjacentButton ? "" : adjacentButton.error().message);
    tree.target = *textEdit;

    expectOk(tree.updater.setLayoutStyle(tree.target, fixedSize(80.0F, 56.0F)));
    expectOk(tree.updater.setLayoutStyle(*adjacentButton, fixedSize(80.0F, 20.0F)));
    expectOk(tree.updater.setText(tree.target, MultilineText));
    expectOk(tree.updater.setTextSelection(
        tree.target, {.anchorCodepoint = 2U, .caretCodepoint = 2U}));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    expectOk(tree.context->requestFocus(tree.target));
    ASSERT_EQ(tree.context->defaultActionFocus(), tree.target);
    ASSERT_EQ(tree.context->imeFocus(), tree.target);

    u64 nextFrame = 20;
    const auto routeKeyPair = [&](Platform::Key key) -> bool {
        auto frame = buildFrame(
            *builder,
            window,
            {
                .frameId = {nextFrame++},
                .transitions = {keyDown(window, key), keyUp(window, key)},
            });
        if (!frame)
        {
            ADD_FAILURE() << frame.error().message;
            return false;
        }
        auto output = producer->produce(tree.context.get(), *frame);
        if (!output)
        {
            ADD_FAILURE() << output.error().message;
            return false;
        }
        if (!output->consumption.isConsumed(0) || !output->consumption.isConsumed(1))
        {
            ADD_FAILURE() << "TextEdit did not consume a vertical keyboard Down/Up pair";
            return false;
        }
        return true;
    };

    ASSERT_TRUE(routeKeyPair(Platform::Key::Down));
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.target);
    EXPECT_EQ(tree.context->imeFocus(), tree.target);
    auto selection = tree.updater.textSelection(tree.target);
    ASSERT_TRUE(selection.has_value()) << (selection ? "" : selection.error().message);
    EXPECT_EQ(
        *selection,
        (UI::UITextSelection{.anchorCodepoint = 4U, .caretCodepoint = 4U}));

    ASSERT_TRUE(routeKeyPair(Platform::Key::Up));
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.target);
    EXPECT_EQ(tree.context->imeFocus(), tree.target);
    selection = tree.updater.textSelection(tree.target);
    ASSERT_TRUE(selection.has_value()) << (selection ? "" : selection.error().message);
    EXPECT_EQ(
        *selection,
        (UI::UITextSelection{.anchorCodepoint = 2U, .caretCodepoint = 2U}));
}

TEST_F(UIInputRouteProducerTest, DropdownConsumesArrowEscapeAndTabDownUpPairs)
{
    auto producer = createProducer();
    DropdownRouteTree tree = createDropdownRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    ASSERT_TRUE(tree.dropdown.hasValue());
    ASSERT_EQ(tree.context->activePopup(), tree.popup);

    auto down = buildFrame(*builder, window,
                           {
                               .frameId = {60},
                               .transitions = {keyDown(window, Platform::Key::Down)},
                               .heldKeys = {Platform::Key::Down},
                           });
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    auto downOutput = producer->produce(tree.context.get(), *down);
    ASSERT_TRUE(downOutput.has_value()) << (downOutput ? "" : downOutput.error().message);
    EXPECT_TRUE(downOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.firstItem);

    auto downRelease = buildFrame(*builder, window,
                                  {
                                      .frameId = {61},
                                      .transitions = {keyUp(window, Platform::Key::Down)},
                                  });
    ASSERT_TRUE(downRelease.has_value()) << (downRelease ? "" : downRelease.error().message);
    auto downReleaseOutput = producer->produce(tree.context.get(), *downRelease);
    ASSERT_TRUE(downReleaseOutput.has_value())
        << (downReleaseOutput ? "" : downReleaseOutput.error().message);
    EXPECT_TRUE(downReleaseOutput->consumption.isConsumed(0));

    auto up = buildFrame(*builder, window,
                         {
                             .frameId = {62},
                             .transitions = {keyDown(window, Platform::Key::Up)},
                             .heldKeys = {Platform::Key::Up},
                         });
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    auto upOutput = producer->produce(tree.context.get(), *up);
    ASSERT_TRUE(upOutput.has_value()) << (upOutput ? "" : upOutput.error().message);
    EXPECT_TRUE(upOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.firstItem);

    auto upRelease = buildFrame(*builder, window,
                                {
                                    .frameId = {63},
                                    .transitions = {keyUp(window, Platform::Key::Up)},
                                });
    ASSERT_TRUE(upRelease.has_value()) << (upRelease ? "" : upRelease.error().message);
    auto upReleaseOutput = producer->produce(tree.context.get(), *upRelease);
    ASSERT_TRUE(upReleaseOutput.has_value()) << (upReleaseOutput ? "" : upReleaseOutput.error().message);
    EXPECT_TRUE(upReleaseOutput->consumption.isConsumed(0));

    auto tab = buildFrame(*builder, window,
                          {
                              .frameId = {64},
                              .transitions = {keyDown(window, Platform::Key::Tab)},
                              .heldKeys = {Platform::Key::Tab},
                          });
    ASSERT_TRUE(tab.has_value()) << (tab ? "" : tab.error().message);
    auto tabOutput = producer->produce(tree.context.get(), *tab);
    ASSERT_TRUE(tabOutput.has_value()) << (tabOutput ? "" : tabOutput.error().message);
    EXPECT_TRUE(tabOutput->consumption.isConsumed(0));
    EXPECT_FALSE(tree.context->activePopup().hasValue());
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.after);

    auto tabRelease = buildFrame(*builder, window,
                                 {
                                     .frameId = {65},
                                     .transitions = {keyUp(window, Platform::Key::Tab)},
                                 });
    ASSERT_TRUE(tabRelease.has_value()) << (tabRelease ? "" : tabRelease.error().message);
    auto tabReleaseOutput = producer->produce(tree.context.get(), *tabRelease);
    ASSERT_TRUE(tabReleaseOutput.has_value()) << (tabReleaseOutput ? "" : tabReleaseOutput.error().message);
    EXPECT_TRUE(tabReleaseOutput->consumption.isConsumed(0));

    expectOk(tree.updater.setDropdownOpen(tree.dropdown, true));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    auto escape = buildFrame(*builder, window,
                             {
                                 .frameId = {66},
                                 .transitions = {keyDown(window, Platform::Key::Escape)},
                                 .heldKeys = {Platform::Key::Escape},
                             });
    ASSERT_TRUE(escape.has_value()) << (escape ? "" : escape.error().message);
    auto escapeOutput = producer->produce(tree.context.get(), *escape);
    ASSERT_TRUE(escapeOutput.has_value()) << (escapeOutput ? "" : escapeOutput.error().message);
    EXPECT_TRUE(escapeOutput->consumption.isConsumed(0));
    EXPECT_FALSE(tree.context->activePopup().hasValue());

    auto escapeRelease = buildFrame(*builder, window,
                                    {
                                        .frameId = {67},
                                        .transitions = {keyUp(window, Platform::Key::Escape)},
                                    });
    ASSERT_TRUE(escapeRelease.has_value()) << (escapeRelease ? "" : escapeRelease.error().message);
    auto escapeReleaseOutput = producer->produce(tree.context.get(), *escapeRelease);
    ASSERT_TRUE(escapeReleaseOutput.has_value())
        << (escapeReleaseOutput ? "" : escapeReleaseOutput.error().message);
    EXPECT_TRUE(escapeReleaseOutput->consumption.isConsumed(0));
}
TEST_F(UIInputRouteProducerTest, DropdownCommandsTakePriorityThenClosedStateUsesSpatialNavigation)
{
    auto producer = createProducer();
    DropdownRouteTree tree = createDropdownRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    auto shiftTab = buildFrame(*builder, window,
                               {
                                   .frameId = {80},
                                   .transitions = {keyDown(window, Platform::Key::Tab)},
                                   .heldKeys = {Platform::Key::Tab, Platform::Key::LeftShift},
                               });
    ASSERT_TRUE(shiftTab.has_value()) << (shiftTab ? "" : shiftTab.error().message);
    auto shiftTabOutput = producer->produce(tree.context.get(), *shiftTab);
    ASSERT_TRUE(shiftTabOutput.has_value()) << (shiftTabOutput ? "" : shiftTabOutput.error().message);
    EXPECT_TRUE(shiftTabOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.before);
    EXPECT_FALSE(tree.context->activePopup().hasValue());

    // Shift is already released in this snapshot. Tab Up must still release the
    // ExitPrevious command selected by the original key-down.
    auto shiftTabRelease = buildFrame(*builder, window,
                                      {
                                          .frameId = {81},
                                          .transitions = {keyUp(window, Platform::Key::Tab)},
                                      });
    ASSERT_TRUE(shiftTabRelease.has_value())
        << (shiftTabRelease ? "" : shiftTabRelease.error().message);
    auto shiftTabReleaseOutput = producer->produce(tree.context.get(), *shiftTabRelease);
    ASSERT_TRUE(shiftTabReleaseOutput.has_value())
        << (shiftTabReleaseOutput ? "" : shiftTabReleaseOutput.error().message);
    EXPECT_TRUE(shiftTabReleaseOutput->consumption.isConsumed(0));

    expectOk(tree.updater.setDropdownOpen(tree.dropdown, true));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    Platform::GamepadSnapshot dpadHeld{
        .gamepad = gamepad,
        .revision = 82,
    };
    dpadHeld.heldButtons.set(static_cast<usize>(Platform::GamepadButton::DpadDown));
    auto dpadDown = buildFrame(
        *builder, window,
        {
            .frameId = {82},
            .transitions =
                {Platform::GamepadButtonTransition{
                    .routedWindow = window,
                    .gamepad = gamepad,
                    .button = Platform::GamepadButton::DpadDown,
                    .state = Platform::DigitalTransition::Down,
                }},
            .gamepadSnapshots = {dpadHeld},
        });
    ASSERT_TRUE(dpadDown.has_value()) << (dpadDown ? "" : dpadDown.error().message);
    auto dpadDownOutput = producer->produce(tree.context.get(), *dpadDown);
    ASSERT_TRUE(dpadDownOutput.has_value()) << (dpadDownOutput ? "" : dpadDownOutput.error().message);
    EXPECT_TRUE(dpadDownOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.firstItem);

    auto dpadRelease = buildFrame(
        *builder, window,
        {
            .frameId = {83},
            .transitions =
                {Platform::GamepadButtonTransition{
                    .routedWindow = window,
                    .gamepad = gamepad,
                    .button = Platform::GamepadButton::DpadDown,
                    .state = Platform::DigitalTransition::Up,
                }},
            .gamepadSnapshots = {Platform::GamepadSnapshot{.gamepad = gamepad, .revision = 83}},
        });
    ASSERT_TRUE(dpadRelease.has_value()) << (dpadRelease ? "" : dpadRelease.error().message);
    auto dpadReleaseOutput = producer->produce(tree.context.get(), *dpadRelease);
    ASSERT_TRUE(dpadReleaseOutput.has_value())
        << (dpadReleaseOutput ? "" : dpadReleaseOutput.error().message);
    EXPECT_TRUE(dpadReleaseOutput->consumption.isConsumed(0));

    Platform::GamepadSnapshot eastHeld{
        .gamepad = gamepad,
        .revision = 84,
    };
    eastHeld.heldButtons.set(static_cast<usize>(Platform::GamepadButton::East));
    auto cancel = buildFrame(
        *builder, window,
        {
            .frameId = {84},
            .transitions =
                {Platform::GamepadButtonTransition{
                    .routedWindow = window,
                    .gamepad = gamepad,
                    .button = Platform::GamepadButton::East,
                    .state = Platform::DigitalTransition::Down,
                }},
            .gamepadSnapshots = {eastHeld},
        });
    ASSERT_TRUE(cancel.has_value()) << (cancel ? "" : cancel.error().message);
    auto cancelOutput = producer->produce(tree.context.get(), *cancel);
    ASSERT_TRUE(cancelOutput.has_value()) << (cancelOutput ? "" : cancelOutput.error().message);
    EXPECT_TRUE(cancelOutput->consumption.isConsumed(0));
    EXPECT_FALSE(tree.context->activePopup().hasValue());

    auto cancelRelease = buildFrame(
        *builder, window,
        {
            .frameId = {85},
            .transitions =
                {Platform::GamepadButtonTransition{
                    .routedWindow = window,
                    .gamepad = gamepad,
                    .button = Platform::GamepadButton::East,
                    .state = Platform::DigitalTransition::Up,
                }},
            .gamepadSnapshots = {Platform::GamepadSnapshot{.gamepad = gamepad, .revision = 85}},
        });
    ASSERT_TRUE(cancelRelease.has_value()) << (cancelRelease ? "" : cancelRelease.error().message);
    auto cancelReleaseOutput = producer->produce(tree.context.get(), *cancelRelease);
    ASSERT_TRUE(cancelReleaseOutput.has_value())
        << (cancelReleaseOutput ? "" : cancelReleaseOutput.error().message);
    EXPECT_TRUE(cancelReleaseOutput->consumption.isConsumed(0));

    auto closedArrow = buildFrame(*builder, window,
                                  {
                                      .frameId = {86},
                                      .transitions = {keyDown(window, Platform::Key::Down)},
                                      .heldKeys = {Platform::Key::Down},
                                  });
    ASSERT_TRUE(closedArrow.has_value()) << (closedArrow ? "" : closedArrow.error().message);
    auto closedArrowOutput = producer->produce(tree.context.get(), *closedArrow);
    ASSERT_TRUE(closedArrowOutput.has_value())
        << (closedArrowOutput ? "" : closedArrowOutput.error().message);
    EXPECT_TRUE(closedArrowOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.dropdown);
}

} // namespace
} // namespace Tina::Tests
