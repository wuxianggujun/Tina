#include "UIInputRouteProducerTestSupport.hpp"

#include <array>
#include <optional>

namespace Tina::Tests {
namespace {

TEST_F(UIInputRouteProducerTest, FocusedButtonConsumesKeyboardAndGamepadAcceptWithActivationSources)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    std::array<UI::UIButtonActionEvent, 4> activations{};
    usize activationCount = 0;
    expectOk(tree.updater.setButtonAction(
        tree.target,
        UI::UIButtonActionCallback{
            [&activations, &activationCount](const UI::UIButtonActionEvent& event) noexcept {
                if (activationCount < activations.size())
                {
                    activations[activationCount] = event;
                }
                ++activationCount;
            }}));

    auto tabFrame = buildFrame(
        *builder,
        window,
        {
            .frameId = {70},
            .transitions = {keyDown(window, Platform::Key::Tab)},
            .heldKeys = {Platform::Key::Tab},
        });
    ASSERT_TRUE(tabFrame.has_value()) << (tabFrame ? "" : tabFrame.error().message);
    auto tabOutput = producer->produce(tree.context.get(), *tabFrame);
    ASSERT_TRUE(tabOutput.has_value()) << (tabOutput ? "" : tabOutput.error().message);
    EXPECT_TRUE(tabOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->input().defaultActionFocus(), tree.target);
    EXPECT_EQ(activationCount, 0U);

    auto keyboardFrame = buildFrame(
        *builder,
        window,
        {
            .frameId = {71},
            .transitions = {
                keyDown(window, Platform::Key::Enter),
                keyDown(window, Platform::Key::Space),
                keyDown(window, Platform::Key::KeypadEnter),
            },
            .heldKeys = {
                Platform::Key::Enter,
                Platform::Key::Space,
                Platform::Key::KeypadEnter,
            },
        });
    ASSERT_TRUE(keyboardFrame.has_value())
        << (keyboardFrame ? "" : keyboardFrame.error().message);
    auto keyboardOutput = producer->produce(tree.context.get(), *keyboardFrame);
    ASSERT_TRUE(keyboardOutput.has_value())
        << (keyboardOutput ? "" : keyboardOutput.error().message);
    ASSERT_EQ(keyboardFrame->inputTransitions().size(), 3U);
    EXPECT_TRUE(keyboardOutput->consumption.isConsumed(0));
    EXPECT_TRUE(keyboardOutput->consumption.isConsumed(1));
    EXPECT_TRUE(keyboardOutput->consumption.isConsumed(2));
    ASSERT_EQ(activationCount, 3U);
    for (usize index = 0; index < 3; ++index)
    {
        EXPECT_EQ(activations[index].buttonNode, tree.target);
        EXPECT_EQ(activations[index].source, UI::UIButtonActivationSource::Keyboard);
        EXPECT_EQ(activations[index].platformFrame, keyboardFrame->id());
        EXPECT_EQ(
            activations[index].sourceSequence,
            keyboardFrame->inputTransitions()[index].sequence);
    }

    auto gamepadFrame = buildFrame(
        *builder,
        window,
        {
            .frameId = {72},
            .transitions = {gamepadButtonDown(window, gamepad)},
            .gamepadSnapshots = {heldSouthSnapshot(gamepad, 72)},
        });
    ASSERT_TRUE(gamepadFrame.has_value()) << (gamepadFrame ? "" : gamepadFrame.error().message);
    auto gamepadOutput = producer->produce(tree.context.get(), *gamepadFrame);
    ASSERT_TRUE(gamepadOutput.has_value()) << (gamepadOutput ? "" : gamepadOutput.error().message);
    EXPECT_TRUE(gamepadOutput->consumption.isConsumed(0));
    ASSERT_EQ(activationCount, 4U);
    EXPECT_EQ(activations[3].buttonNode, tree.target);
    EXPECT_EQ(activations[3].source, UI::UIButtonActivationSource::Gamepad);
    EXPECT_EQ(activations[3].platformFrame, gamepadFrame->id());
    EXPECT_EQ(
        activations[3].sourceSequence,
        gamepadFrame->inputTransitions()[0].sequence);
}
TEST_F(UIInputRouteProducerTest, KeyboardAcceptDownUpTracksEachControlAndPaintState)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    std::array<UI::UIButtonActionEvent, 4> activations{};
    usize activationCount = 0;
    expectOk(tree.updater.setButtonAction(
        tree.target,
        UI::UIButtonActionCallback{
            [&activations, &activationCount](const UI::UIButtonActionEvent& event) noexcept {
                if (activationCount < activations.size()) {
                    activations[activationCount] = event;
                }
                ++activationCount;
            }}));

    auto tab = buildFrame(
        *builder,
        window,
        {
            .frameId = {100},
            .transitions = {keyDown(window, Platform::Key::Tab)},
            .heldKeys = {Platform::Key::Tab},
        });
    ASSERT_TRUE(tab.has_value()) << (tab ? "" : tab.error().message);
    ASSERT_TRUE(producer->produce(tree.context.get(), *tab).has_value());

    auto down = buildFrame(
        *builder,
        window,
        {
            .frameId = {101},
            .transitions = {
                keyDown(window, Platform::Key::Enter),
                keyDown(window, Platform::Key::Space),
            },
            .heldKeys = {Platform::Key::Enter, Platform::Key::Space},
        });
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    auto downOutput = producer->produce(tree.context.get(), *down);
    ASSERT_TRUE(downOutput.has_value()) << (downOutput ? "" : downOutput.error().message);
    EXPECT_TRUE(downOutput->consumption.isConsumed(0));
    EXPECT_TRUE(downOutput->consumption.isConsumed(1));
    EXPECT_EQ(activationCount, 2U);
    auto pressed = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(pressed.has_value()) << (pressed ? "" : pressed.error().message);
    EXPECT_TRUE(*pressed);

    auto enterUp = buildFrame(
        *builder,
        window,
        {
            .frameId = {102},
            .transitions = {keyUp(window, Platform::Key::Enter)},
            .heldKeys = {Platform::Key::Space},
        });
    ASSERT_TRUE(enterUp.has_value()) << (enterUp ? "" : enterUp.error().message);
    auto enterUpOutput = producer->produce(tree.context.get(), *enterUp);
    ASSERT_TRUE(enterUpOutput.has_value())
        << (enterUpOutput ? "" : enterUpOutput.error().message);
    EXPECT_TRUE(enterUpOutput->consumption.isConsumed(0));
    pressed = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(pressed.has_value());
    EXPECT_TRUE(*pressed);

    auto spaceUp = buildFrame(
        *builder,
        window,
        {
            .frameId = {103},
            .transitions = {keyUp(window, Platform::Key::Space)},
        });
    ASSERT_TRUE(spaceUp.has_value()) << (spaceUp ? "" : spaceUp.error().message);
    auto spaceUpOutput = producer->produce(tree.context.get(), *spaceUp);
    ASSERT_TRUE(spaceUpOutput.has_value())
        << (spaceUpOutput ? "" : spaceUpOutput.error().message);
    EXPECT_TRUE(spaceUpOutput->consumption.isConsumed(0));
    pressed = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(pressed.has_value());
    EXPECT_FALSE(*pressed);

    auto gamepadDown = buildFrame(
        *builder,
        window,
        {
            .frameId = {104},
            .transitions = {gamepadButtonDown(window, gamepad)},
            .gamepadSnapshots = {heldSouthSnapshot(gamepad, 104)},
        });
    ASSERT_TRUE(gamepadDown.has_value())
        << (gamepadDown ? "" : gamepadDown.error().message);
    auto gamepadDownOutput = producer->produce(tree.context.get(), *gamepadDown);
    ASSERT_TRUE(gamepadDownOutput.has_value())
        << (gamepadDownOutput ? "" : gamepadDownOutput.error().message);
    EXPECT_TRUE(gamepadDownOutput->consumption.isConsumed(0));
    EXPECT_EQ(activationCount, 3U);
    EXPECT_EQ(activations[2].source, UI::UIButtonActivationSource::Gamepad);
    pressed = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(pressed.has_value());
    EXPECT_TRUE(*pressed);

    auto gamepadUp = buildFrame(
        *builder,
        window,
        {
            .frameId = {105},
            .transitions = {gamepadButtonUp(window, gamepad)},
            .gamepadSnapshots = {releasedSouthSnapshot(gamepad, 105)},
        });
    ASSERT_TRUE(gamepadUp.has_value())
        << (gamepadUp ? "" : gamepadUp.error().message);
    auto gamepadUpOutput = producer->produce(tree.context.get(), *gamepadUp);
    ASSERT_TRUE(gamepadUpOutput.has_value())
        << (gamepadUpOutput ? "" : gamepadUpOutput.error().message);
    EXPECT_TRUE(gamepadUpOutput->consumption.isConsumed(0));
    pressed = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(pressed.has_value());
    EXPECT_FALSE(*pressed);
}
TEST_F(UIInputRouteProducerTest, DisabledButtonDoesNotConsumeAcceptOrInvokeAction)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    usize activationCount = 0;
    expectOk(tree.updater.setButtonAction(
        tree.target,
        UI::UIButtonActionCallback{
            [&activationCount](const UI::UIButtonActionEvent&) noexcept {
                ++activationCount;
            }}));
    expectOk(tree.updater.setEnabled(tree.target, false));
    expectOk(tree.context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    auto frame = buildFrame(
        *builder,
        window,
        {
            .frameId = {80},
            .transitions = {
                keyDown(window, Platform::Key::Tab),
                keyDown(window, Platform::Key::Enter),
                keyDown(window, Platform::Key::Space),
                keyDown(window, Platform::Key::KeypadEnter),
                gamepadButtonDown(window, gamepad),
            },
            .heldKeys = {
                Platform::Key::Tab,
                Platform::Key::Enter,
                Platform::Key::Space,
                Platform::Key::KeypadEnter,
            },
            .gamepadSnapshots = {heldSouthSnapshot(gamepad, 80)},
        });
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
    auto output = producer->produce(tree.context.get(), *frame);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    ASSERT_EQ(frame->inputTransitions().size(), 5U);
    for (usize ordinal = 0; ordinal < frame->inputTransitions().size(); ++ordinal)
    {
        EXPECT_FALSE(output->consumption.isConsumed(ordinal));
    }
    EXPECT_FALSE(tree.context->input().defaultActionFocus().hasValue());
    EXPECT_EQ(activationCount, 0U);
}
TEST_F(UIInputRouteProducerTest, CancelAndCoveringResetClearButtonStateWithoutActivation)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    usize activationCount = 0;
    expectOk(tree.updater.setButtonAction(
        tree.target,
        UI::UIButtonActionCallback{
            [&activationCount](const UI::UIButtonActionEvent&) noexcept {
                ++activationCount;
            }}));

    const auto routeDown = [&](u64 frameValue) {
        auto down = buildFrame(
            *builder,
            window,
            {
                .frameId = {frameValue},
                .transitions = {
                    pointerButton(
                        window,
                        Platform::DigitalTransition::Down,
                        10.0,
                        10.0)},
                .heldPointerButtons = {Platform::PointerButton::Primary},
                .pointerX = 10.0,
                .pointerY = 10.0,
            });
        EXPECT_TRUE(down.has_value())
            << (down ? "" : down.error().message);
        if (!down) {
            return;
        }
        auto output = producer->produce(tree.context.get(), *down);
        EXPECT_TRUE(output.has_value())
            << (output ? "" : output.error().message);
        if (output) {
            EXPECT_TRUE(output->consumption.isConsumed(0));
        }
        auto pressed = tree.updater.isButtonPressed(tree.target);
        EXPECT_TRUE(pressed.has_value())
            << (pressed ? "" : pressed.error().message);
        if (pressed) {
            EXPECT_TRUE(*pressed);
        }
    };

    const auto routeUpWithoutActivation = [&](u64 frameValue) {
        auto up = buildFrame(
            *builder,
            window,
            {
                .frameId = {frameValue},
                .transitions = {
                    pointerButton(
                        window,
                        Platform::DigitalTransition::Up,
                        10.0,
                        10.0)},
                .pointerX = 10.0,
                .pointerY = 10.0,
            });
        EXPECT_TRUE(up.has_value()) << (up ? "" : up.error().message);
        if (!up) {
            return;
        }
        auto output = producer->produce(tree.context.get(), *up);
        EXPECT_TRUE(output.has_value())
            << (output ? "" : output.error().message);
        if (output) {
            EXPECT_FALSE(output->consumption.isConsumed(0));
        }
        EXPECT_EQ(activationCount, 0U);
    };

    routeDown(50);
    auto cancel = buildFrame(
        *builder,
        window,
        {
            .frameId = {51},
            .transitions = {
                Platform::InputCancelTransition{
                    .routedWindow = window,
                    .reason = Platform::InputCancelReason::FocusLost,
                }},
            .pointerX = 10.0,
            .pointerY = 10.0,
        });
    ASSERT_TRUE(cancel.has_value())
        << (cancel ? "" : cancel.error().message);
    auto cancelOutput = producer->produce(tree.context.get(), *cancel);
    ASSERT_TRUE(cancelOutput.has_value())
        << (cancelOutput ? "" : cancelOutput.error().message);
    EXPECT_FALSE(cancelOutput->consumption.isConsumed(0));
    auto afterCancel = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(afterCancel.has_value())
        << (afterCancel ? "" : afterCancel.error().message);
    EXPECT_FALSE(*afterCancel);
    EXPECT_EQ(activationCount, 0U);
    routeUpWithoutActivation(52);

    routeDown(53);
    auto reset = buildFrame(
        *builder,
        window,
        {
            .frameId = {54},
            .transitions = {
                Platform::InputStreamReset{
                    .routedWindow = std::nullopt,
                    .reason = Platform::InputResetReason::BackendRecovery,
                }},
            .pointerX = 10.0,
            .pointerY = 10.0,
        });
    ASSERT_TRUE(reset.has_value()) << (reset ? "" : reset.error().message);
    auto resetOutput = producer->produce(tree.context.get(), *reset);
    ASSERT_TRUE(resetOutput.has_value())
        << (resetOutput ? "" : resetOutput.error().message);
    EXPECT_FALSE(resetOutput->consumption.isConsumed(0));
    auto afterReset = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(afterReset.has_value())
        << (afterReset ? "" : afterReset.error().message);
    EXPECT_FALSE(*afterReset);
    EXPECT_EQ(activationCount, 0U);
    routeUpWithoutActivation(55);
}

} // namespace
} // namespace Tina::Tests
