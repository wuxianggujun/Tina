#include "UIInputRouteProducerTestSupport.hpp"

#include <array>

namespace Tina::Tests {
namespace {

TEST_F(UIInputRouteProducerTest, FlowBackConsumesKeyboardAndGamepadDownUpPairs)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(
        window,
        {
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .routePathCapacity = 8,
            .routedPointerListenerCapacity = 16,
            .flowLayerCapacity = 1,
            .flowScreenCapacity = 1,
        });
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    auto layerNode = tree.updater.createElement(tree.root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(layerNode.has_value());
    auto screenNode = tree.updater.createElement(*layerNode, UI::makePanelElement());
    ASSERT_TRUE(screenNode.has_value());
    expectOk(tree.updater.setLayoutStyle(*layerNode, fixedSize(100.0F, 100.0F)));
    expectOk(tree.updater.setLayoutStyle(*screenNode, fixedSize(100.0F, 100.0F)));
    auto layer = tree.updater.registerFlowLayer(*layerNode);
    ASSERT_TRUE(layer.has_value());
    auto screen = tree.updater.registerFlowScreen(*layer, *screenNode);
    ASSERT_TRUE(screen.has_value());
    expectOk(tree.updater.pushFlowScreen(*screen));

    std::array<UI::UIFlowActionEvent, 2> events{};
    usize eventCount = 0;
    expectOk(tree.updater.setFlowScreenAction(
        *screen, UI::UIFlowAction::Back,
        UI::UIFlowActionCallback{
            [&events, &eventCount](const UI::UIFlowActionEvent& event) noexcept {
                if (eventCount < events.size())
                {
                    events[eventCount] = event;
                }
                ++eventCount;
            }}));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    auto escapeDown = buildFrame(
        *builder, window,
        {
            .frameId = {200},
            .transitions = {keyDown(window, Platform::Key::Escape)},
            .heldKeys = {Platform::Key::Escape},
        });
    ASSERT_TRUE(escapeDown.has_value());
    auto escapeDownOutput = producer->produce(tree.context.get(), *escapeDown);
    ASSERT_TRUE(escapeDownOutput.has_value());
    EXPECT_TRUE(escapeDownOutput->consumption.isConsumed(0));
    ASSERT_EQ(eventCount, 1U);
    EXPECT_EQ(events[0].screen, *screen);
    EXPECT_EQ(events[0].source, UI::UIFlowActionSource::Keyboard);

    auto popped = tree.updater.popFlowScreen(*layer);
    ASSERT_TRUE(popped.has_value());
    auto escapeUp = buildFrame(
        *builder, window,
        {
            .frameId = {201},
            .transitions = {keyUp(window, Platform::Key::Escape)},
        });
    ASSERT_TRUE(escapeUp.has_value());
    auto escapeUpOutput = producer->produce(tree.context.get(), *escapeUp);
    ASSERT_TRUE(escapeUpOutput.has_value());
    EXPECT_TRUE(escapeUpOutput->consumption.isConsumed(0));

    expectOk(tree.updater.pushFlowScreen(*screen));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    Platform::GamepadSnapshot eastHeld{.gamepad = gamepad, .revision = 202};
    eastHeld.heldButtons.set(static_cast<usize>(Platform::GamepadButton::East));
    auto eastDown = buildFrame(
        *builder, window,
        {
            .frameId = {202},
            .transitions = {gamepadButton(window, gamepad, Platform::GamepadButton::East,
                                          Platform::DigitalTransition::Down)},
            .gamepadSnapshots = {eastHeld},
        });
    ASSERT_TRUE(eastDown.has_value());
    auto eastDownOutput = producer->produce(tree.context.get(), *eastDown);
    ASSERT_TRUE(eastDownOutput.has_value());
    EXPECT_TRUE(eastDownOutput->consumption.isConsumed(0));
    ASSERT_EQ(eventCount, 2U);
    EXPECT_EQ(events[1].source, UI::UIFlowActionSource::Gamepad);

    auto eastUp = buildFrame(
        *builder, window,
        {
            .frameId = {203},
            .transitions = {gamepadButton(window, gamepad, Platform::GamepadButton::East,
                                          Platform::DigitalTransition::Up)},
            .gamepadSnapshots = {
                Platform::GamepadSnapshot{.gamepad = gamepad, .revision = 203}},
        });
    ASSERT_TRUE(eastUp.has_value());
    auto eastUpOutput = producer->produce(tree.context.get(), *eastUp);
    ASSERT_TRUE(eastUpOutput.has_value());
    EXPECT_TRUE(eastUpOutput->consumption.isConsumed(0));
}

TEST_F(UIInputRouteProducerTest, FlowBackWithoutRegisteredActionRemainsVisibleToGameplay)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(
        window,
        {
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .routePathCapacity = 8,
            .routedPointerListenerCapacity = 16,
            .flowLayerCapacity = 1,
            .flowScreenCapacity = 1,
        });
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    auto layerNode = tree.updater.createElement(tree.root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(layerNode.has_value());
    auto screenNode = tree.updater.createElement(*layerNode, UI::makePanelElement());
    ASSERT_TRUE(screenNode.has_value());
    expectOk(tree.updater.setLayoutStyle(*layerNode, fixedSize(100.0F, 100.0F)));
    expectOk(tree.updater.setLayoutStyle(*screenNode, fixedSize(100.0F, 100.0F)));
    auto layer = tree.updater.registerFlowLayer(*layerNode);
    ASSERT_TRUE(layer.has_value());
    auto screen = tree.updater.registerFlowScreen(*layer, *screenNode);
    ASSERT_TRUE(screen.has_value());
    expectOk(tree.updater.pushFlowScreen(*screen));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    auto escapeDown = buildFrame(
        *builder, window,
        {
            .frameId = {210},
            .transitions = {keyDown(window, Platform::Key::Escape)},
            .heldKeys = {Platform::Key::Escape},
        });
    ASSERT_TRUE(escapeDown.has_value());
    auto output = producer->produce(tree.context.get(), *escapeDown);
    ASSERT_TRUE(output.has_value());
    EXPECT_FALSE(output->consumption.isConsumed(0));
}

TEST_F(UIInputRouteProducerTest, OpenDropdownDismissesBeforeFlowBack)
{
    auto producer = createProducer();
    DropdownRouteTree tree = createDropdownRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    ASSERT_EQ(tree.context->activePopup(), tree.popup);

    auto layerNode = tree.updater.createElement(tree.root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(layerNode.has_value());
    auto screenNode = tree.updater.createElement(*layerNode, UI::makePanelElement());
    ASSERT_TRUE(screenNode.has_value());
    expectOk(tree.updater.setLayoutStyle(*layerNode, fixedSize(100.0F, 100.0F)));
    expectOk(tree.updater.setLayoutStyle(*screenNode, fixedSize(100.0F, 100.0F)));
    auto layer = tree.updater.registerFlowLayer(*layerNode);
    ASSERT_TRUE(layer.has_value());
    auto screen = tree.updater.registerFlowScreen(*layer, *screenNode);
    ASSERT_TRUE(screen.has_value());
    expectOk(tree.updater.pushFlowScreen(*screen));
    usize invocationCount = 0;
    expectOk(tree.updater.setFlowScreenAction(
        *screen, UI::UIFlowAction::Back,
        UI::UIFlowActionCallback{
            [&invocationCount](const UI::UIFlowActionEvent&) noexcept {
                ++invocationCount;
            }}));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    auto dismiss = buildFrame(
        *builder, window,
        {
            .frameId = {220},
            .transitions = {keyDown(window, Platform::Key::Escape)},
            .heldKeys = {Platform::Key::Escape},
        });
    ASSERT_TRUE(dismiss.has_value());
    auto dismissOutput = producer->produce(tree.context.get(), *dismiss);
    ASSERT_TRUE(dismissOutput.has_value());
    EXPECT_TRUE(dismissOutput->consumption.isConsumed(0));
    EXPECT_FALSE(tree.context->activePopup().hasValue());
    EXPECT_EQ(invocationCount, 0U);

    auto dismissRelease = buildFrame(
        *builder, window,
        {
            .frameId = {221},
            .transitions = {keyUp(window, Platform::Key::Escape)},
        });
    ASSERT_TRUE(dismissRelease.has_value());
    auto dismissReleaseOutput = producer->produce(tree.context.get(), *dismissRelease);
    ASSERT_TRUE(dismissReleaseOutput.has_value());
    EXPECT_TRUE(dismissReleaseOutput->consumption.isConsumed(0));
    EXPECT_EQ(invocationCount, 0U);

    auto back = buildFrame(
        *builder, window,
        {
            .frameId = {222},
            .transitions = {keyDown(window, Platform::Key::Escape)},
            .heldKeys = {Platform::Key::Escape},
        });
    ASSERT_TRUE(back.has_value());
    auto backOutput = producer->produce(tree.context.get(), *back);
    ASSERT_TRUE(backOutput.has_value());
    EXPECT_TRUE(backOutput->consumption.isConsumed(0));
    EXPECT_EQ(invocationCount, 1U);
}

TEST_F(UIInputRouteProducerTest, FlowInputDeviceTracksMeaningfulTransitionsAndDisconnect)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    auto keyboardDown = buildFrame(
        *builder, window,
        {
            .frameId = {230},
            .transitions = {keyDown(window, Platform::Key::A)},
            .heldKeys = {Platform::Key::A},
        });
    ASSERT_TRUE(keyboardDown.has_value());
    ASSERT_TRUE(producer->produce(tree.context.get(), *keyboardDown).has_value());
    EXPECT_EQ(tree.context->flowInputDeviceState().device,
              UI::UIFlowInputDevice::KeyboardMouse);
    EXPECT_EQ(tree.context->flowInputDeviceState().revision, 0U);

    auto gamepadDown = buildFrame(
        *builder, window,
        {
            .frameId = {231},
            .transitions = {gamepadButtonDown(window, gamepad)},
            .heldKeys = {Platform::Key::A},
            .gamepadSnapshots = {heldSouthSnapshot(gamepad, 231)},
        });
    ASSERT_TRUE(gamepadDown.has_value());
    ASSERT_TRUE(producer->produce(tree.context.get(), *gamepadDown).has_value());
    EXPECT_EQ(tree.context->flowInputDeviceState().device,
              UI::UIFlowInputDevice::Gamepad);
    EXPECT_EQ(tree.context->flowInputDeviceState().gamepad, gamepad);
    EXPECT_EQ(tree.context->flowInputDeviceState().revision, 1U);

    auto keyboardRelease = buildFrame(
        *builder, window,
        {
            .frameId = {232},
            .transitions = {keyUp(window, Platform::Key::A)},
            .gamepadSnapshots = {heldSouthSnapshot(gamepad, 232)},
        });
    ASSERT_TRUE(keyboardRelease.has_value());
    ASSERT_TRUE(producer->produce(tree.context.get(), *keyboardRelease).has_value());
    EXPECT_EQ(tree.context->flowInputDeviceState().device,
              UI::UIFlowInputDevice::Gamepad);
    EXPECT_EQ(tree.context->flowInputDeviceState().revision, 1U);

    auto pointerWheelFrame = buildFrame(
        *builder, window,
        {
            .frameId = {233},
            .transitions = {pointerWheel(window, 10.0, 10.0, 0.0, 1.0)},
            .gamepadSnapshots = {heldSouthSnapshot(gamepad, 233)},
        });
    ASSERT_TRUE(pointerWheelFrame.has_value());
    ASSERT_TRUE(producer->produce(tree.context.get(), *pointerWheelFrame).has_value());
    EXPECT_EQ(tree.context->flowInputDeviceState().device,
              UI::UIFlowInputDevice::KeyboardMouse);
    EXPECT_EQ(tree.context->flowInputDeviceState().revision, 2U);

    auto gamepadRelease = buildFrame(
        *builder, window,
        {
            .frameId = {234},
            .transitions = {gamepadButtonUp(window, gamepad)},
            .gamepadSnapshots = {releasedSouthSnapshot(gamepad, 234)},
        });
    ASSERT_TRUE(gamepadRelease.has_value());
    ASSERT_TRUE(producer->produce(tree.context.get(), *gamepadRelease).has_value());
    EXPECT_EQ(tree.context->flowInputDeviceState().revision, 2U);

    auto secondGamepadDown = buildFrame(
        *builder, window,
        {
            .frameId = {235},
            .transitions = {gamepadButtonDown(window, gamepad)},
            .gamepadSnapshots = {heldSouthSnapshot(gamepad, 235)},
        });
    ASSERT_TRUE(secondGamepadDown.has_value());
    ASSERT_TRUE(producer->produce(tree.context.get(), *secondGamepadDown).has_value());
    EXPECT_EQ(tree.context->flowInputDeviceState().device,
              UI::UIFlowInputDevice::Gamepad);
    EXPECT_EQ(tree.context->flowInputDeviceState().revision, 3U);

    auto disconnect = buildFrame(
        *builder, window,
        {
            .frameId = {236},
            .transitions = {Platform::InputCancelTransition{
                .routedWindow = window,
                .reason = Platform::InputCancelReason::DeviceDisconnected,
                .gamepad = gamepad,
            }},
        });
    ASSERT_TRUE(disconnect.has_value());
    ASSERT_TRUE(producer->produce(tree.context.get(), *disconnect).has_value());
    const UI::UIFlowInputDeviceState finalState =
        tree.context->flowInputDeviceState();
    EXPECT_EQ(finalState.device, UI::UIFlowInputDevice::KeyboardMouse);
    EXPECT_FALSE(finalState.gamepad.has_value());
    EXPECT_EQ(finalState.revision, 4U);
}

} // namespace
} // namespace Tina::Tests
