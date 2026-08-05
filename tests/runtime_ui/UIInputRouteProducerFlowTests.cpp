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
    EXPECT_EQ(events[0].localUser, UI::UIFlowPrimaryLocalUser);
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
    EXPECT_EQ(events[1].localUser, UI::UIFlowPrimaryLocalUser);
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

TEST_F(UIInputRouteProducerTest, FlowConfirmClaimsUnfocusedAcceptAndYieldsToFocusedButton)
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
            .flowScreenCapacity = 2,
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
        *screen, UI::UIFlowAction::Confirm,
        UI::UIFlowActionCallback{
            [&events, &eventCount](const UI::UIFlowActionEvent& event) noexcept {
                if (eventCount < events.size())
                {
                    events[eventCount] = event;
                }
                ++eventCount;
            }}));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    auto enterDown = buildFrame(
        *builder, window,
        {
            .frameId = {204},
            .transitions = {keyDown(window, Platform::Key::Enter)},
            .heldKeys = {Platform::Key::Enter},
        });
    ASSERT_TRUE(enterDown.has_value());
    auto enterDownOutput = producer->produce(tree.context.get(), *enterDown);
    ASSERT_TRUE(enterDownOutput.has_value());
    EXPECT_TRUE(enterDownOutput->consumption.isConsumed(0));
    ASSERT_EQ(eventCount, 1U);
    EXPECT_EQ(events[0].action, UI::UIFlowAction::Confirm);
    EXPECT_EQ(events[0].localUser, UI::UIFlowPrimaryLocalUser);
    EXPECT_EQ(events[0].source, UI::UIFlowActionSource::Keyboard);

    auto popped = tree.updater.popFlowScreen(*layer);
    ASSERT_TRUE(popped.has_value());
    auto enterUp = buildFrame(
        *builder, window,
        {
            .frameId = {205},
            .transitions = {keyUp(window, Platform::Key::Enter)},
        });
    ASSERT_TRUE(enterUp.has_value());
    auto enterUpOutput = producer->produce(tree.context.get(), *enterUp);
    ASSERT_TRUE(enterUpOutput.has_value());
    EXPECT_TRUE(enterUpOutput->consumption.isConsumed(0));

    expectOk(tree.updater.pushFlowScreen(*screen));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    auto southDown = buildFrame(
        *builder, window,
        {
            .frameId = {206},
            .transitions = {gamepadButtonDown(window, gamepad)},
            .gamepadSnapshots = {heldSouthSnapshot(gamepad, 206)},
        });
    ASSERT_TRUE(southDown.has_value());
    auto southDownOutput = producer->produce(tree.context.get(), *southDown);
    ASSERT_TRUE(southDownOutput.has_value());
    EXPECT_TRUE(southDownOutput->consumption.isConsumed(0));
    ASSERT_EQ(eventCount, 2U);
    EXPECT_EQ(events[1].action, UI::UIFlowAction::Confirm);
    EXPECT_EQ(events[1].localUser, UI::UIFlowPrimaryLocalUser);
    EXPECT_EQ(events[1].source, UI::UIFlowActionSource::Gamepad);

    auto southUp = buildFrame(
        *builder, window,
        {
            .frameId = {207},
            .transitions = {gamepadButtonUp(window, gamepad)},
            .gamepadSnapshots = {releasedSouthSnapshot(gamepad, 207)},
        });
    ASSERT_TRUE(southUp.has_value());
    auto southUpOutput = producer->produce(tree.context.get(), *southUp);
    ASSERT_TRUE(southUpOutput.has_value());
    EXPECT_TRUE(southUpOutput->consumption.isConsumed(0));

    usize buttonActivationCount = 0;
    expectOk(tree.updater.setButtonAction(
        tree.target,
        UI::UIButtonActionCallback{
            [&buttonActivationCount](const UI::UIButtonActionEvent&) noexcept {
                ++buttonActivationCount;
            }}));
    expectOk(tree.context->requestFocus(tree.target));
    auto focusedEnter = buildFrame(
        *builder, window,
        {
            .frameId = {208},
            .transitions = {keyDown(window, Platform::Key::Enter)},
            .heldKeys = {Platform::Key::Enter},
        });
    ASSERT_TRUE(focusedEnter.has_value());
    auto focusedOutput = producer->produce(tree.context.get(), *focusedEnter);
    ASSERT_TRUE(focusedOutput.has_value());
    EXPECT_TRUE(focusedOutput->consumption.isConsumed(0));
    EXPECT_EQ(buttonActivationCount, 1U);
    EXPECT_EQ(eventCount, 2U);
}

TEST_F(UIInputRouteProducerTest, FlowMenuClaimsPAndStartButYieldsPrintablePToTextEdit)
{
    auto producer = createProducer();
    RouteTree tree = createTextEditRouteTree(window);
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
        *screen, UI::UIFlowAction::Menu,
        UI::UIFlowActionCallback{
            [&events, &eventCount](const UI::UIFlowActionEvent& event) noexcept {
                if (eventCount < events.size())
                {
                    events[eventCount] = event;
                }
                ++eventCount;
            }}));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    auto keyDownFrame = buildFrame(
        *builder, window,
        {
            .frameId = {209},
            .transitions = {keyDown(window, Platform::Key::P)},
            .heldKeys = {Platform::Key::P},
        });
    ASSERT_TRUE(keyDownFrame.has_value());
    auto keyDownOutput = producer->produce(tree.context.get(), *keyDownFrame);
    ASSERT_TRUE(keyDownOutput.has_value());
    EXPECT_TRUE(keyDownOutput->consumption.isConsumed(0));
    ASSERT_EQ(eventCount, 1U);
    EXPECT_EQ(events[0].action, UI::UIFlowAction::Menu);
    EXPECT_EQ(events[0].localUser, UI::UIFlowPrimaryLocalUser);
    EXPECT_EQ(events[0].source, UI::UIFlowActionSource::Keyboard);

    auto keyUpFrame = buildFrame(
        *builder, window,
        {
            .frameId = {210},
            .transitions = {keyUp(window, Platform::Key::P)},
        });
    ASSERT_TRUE(keyUpFrame.has_value());
    auto keyUpOutput = producer->produce(tree.context.get(), *keyUpFrame);
    ASSERT_TRUE(keyUpOutput.has_value());
    EXPECT_TRUE(keyUpOutput->consumption.isConsumed(0));

    Platform::GamepadSnapshot startHeld{.gamepad = gamepad, .revision = 211};
    startHeld.heldButtons.set(static_cast<usize>(Platform::GamepadButton::Start));
    auto startDown = buildFrame(
        *builder, window,
        {
            .frameId = {211},
            .transitions = {gamepadButton(window, gamepad, Platform::GamepadButton::Start,
                                          Platform::DigitalTransition::Down)},
            .gamepadSnapshots = {startHeld},
        });
    ASSERT_TRUE(startDown.has_value());
    auto startDownOutput = producer->produce(tree.context.get(), *startDown);
    ASSERT_TRUE(startDownOutput.has_value());
    EXPECT_TRUE(startDownOutput->consumption.isConsumed(0));
    ASSERT_EQ(eventCount, 2U);
    EXPECT_EQ(events[1].action, UI::UIFlowAction::Menu);
    EXPECT_EQ(events[1].localUser, UI::UIFlowPrimaryLocalUser);
    EXPECT_EQ(events[1].source, UI::UIFlowActionSource::Gamepad);

    auto startUp = buildFrame(
        *builder, window,
        {
            .frameId = {212},
            .transitions = {gamepadButton(window, gamepad, Platform::GamepadButton::Start,
                                          Platform::DigitalTransition::Up)},
            .gamepadSnapshots = {
                Platform::GamepadSnapshot{.gamepad = gamepad, .revision = 212}},
        });
    ASSERT_TRUE(startUp.has_value());
    auto startUpOutput = producer->produce(tree.context.get(), *startUp);
    ASSERT_TRUE(startUpOutput.has_value());
    EXPECT_TRUE(startUpOutput->consumption.isConsumed(0));

    expectOk(tree.context->requestFocus(tree.target));
    auto focusedKeyDown = buildFrame(
        *builder, window,
        {
            .frameId = {213},
            .transitions = {keyDown(window, Platform::Key::P)},
            .heldKeys = {Platform::Key::P},
        });
    ASSERT_TRUE(focusedKeyDown.has_value());
    auto focusedKeyDownOutput = producer->produce(tree.context.get(), *focusedKeyDown);
    ASSERT_TRUE(focusedKeyDownOutput.has_value());
    EXPECT_TRUE(focusedKeyDownOutput->consumption.isConsumed(0));
    EXPECT_EQ(eventCount, 2U);

    auto focusedKeyUp = buildFrame(
        *builder, window,
        {
            .frameId = {214},
            .transitions = {keyUp(window, Platform::Key::P)},
        });
    ASSERT_TRUE(focusedKeyUp.has_value());
    auto focusedKeyUpOutput = producer->produce(tree.context.get(), *focusedKeyUp);
    ASSERT_TRUE(focusedKeyUpOutput.has_value());
    EXPECT_TRUE(focusedKeyUpOutput->consumption.isConsumed(0));
    EXPECT_EQ(eventCount, 2U);
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
    auto primaryState =
        tree.context->flowInputDeviceState(UI::UIFlowPrimaryLocalUser);
    ASSERT_TRUE(primaryState.has_value());
    EXPECT_EQ(primaryState->device, UI::UIFlowInputDevice::KeyboardMouse);
    EXPECT_EQ(primaryState->revision, 0U);

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
    primaryState =
        tree.context->flowInputDeviceState(UI::UIFlowPrimaryLocalUser);
    ASSERT_TRUE(primaryState.has_value());
    EXPECT_EQ(primaryState->device, UI::UIFlowInputDevice::Gamepad);
    EXPECT_EQ(primaryState->gamepad, gamepad);
    EXPECT_EQ(primaryState->revision, 1U);

    auto keyboardRelease = buildFrame(
        *builder, window,
        {
            .frameId = {232},
            .transitions = {keyUp(window, Platform::Key::A)},
            .gamepadSnapshots = {heldSouthSnapshot(gamepad, 232)},
        });
    ASSERT_TRUE(keyboardRelease.has_value());
    ASSERT_TRUE(producer->produce(tree.context.get(), *keyboardRelease).has_value());
    primaryState =
        tree.context->flowInputDeviceState(UI::UIFlowPrimaryLocalUser);
    ASSERT_TRUE(primaryState.has_value());
    EXPECT_EQ(primaryState->device, UI::UIFlowInputDevice::Gamepad);
    EXPECT_EQ(primaryState->revision, 1U);

    auto pointerWheelFrame = buildFrame(
        *builder, window,
        {
            .frameId = {233},
            .transitions = {pointerWheel(window, 10.0, 10.0, 0.0, 1.0)},
            .gamepadSnapshots = {heldSouthSnapshot(gamepad, 233)},
        });
    ASSERT_TRUE(pointerWheelFrame.has_value());
    ASSERT_TRUE(producer->produce(tree.context.get(), *pointerWheelFrame).has_value());
    primaryState =
        tree.context->flowInputDeviceState(UI::UIFlowPrimaryLocalUser);
    ASSERT_TRUE(primaryState.has_value());
    EXPECT_EQ(primaryState->device, UI::UIFlowInputDevice::KeyboardMouse);
    EXPECT_EQ(primaryState->revision, 2U);

    auto gamepadRelease = buildFrame(
        *builder, window,
        {
            .frameId = {234},
            .transitions = {gamepadButtonUp(window, gamepad)},
            .gamepadSnapshots = {releasedSouthSnapshot(gamepad, 234)},
        });
    ASSERT_TRUE(gamepadRelease.has_value());
    ASSERT_TRUE(producer->produce(tree.context.get(), *gamepadRelease).has_value());
    primaryState =
        tree.context->flowInputDeviceState(UI::UIFlowPrimaryLocalUser);
    ASSERT_TRUE(primaryState.has_value());
    EXPECT_EQ(primaryState->revision, 2U);

    auto secondGamepadDown = buildFrame(
        *builder, window,
        {
            .frameId = {235},
            .transitions = {gamepadButtonDown(window, gamepad)},
            .gamepadSnapshots = {heldSouthSnapshot(gamepad, 235)},
        });
    ASSERT_TRUE(secondGamepadDown.has_value());
    ASSERT_TRUE(producer->produce(tree.context.get(), *secondGamepadDown).has_value());
    primaryState =
        tree.context->flowInputDeviceState(UI::UIFlowPrimaryLocalUser);
    ASSERT_TRUE(primaryState.has_value());
    EXPECT_EQ(primaryState->device, UI::UIFlowInputDevice::Gamepad);
    EXPECT_EQ(primaryState->revision, 3U);

    auto disconnect = buildFrame(
        *builder, window,
        {
            .frameId = {236},
            .transitions = {Platform::InputCancelTransition{
                .routedWindow = window,
                .reason = Platform::InputCancelReason::DeviceDisconnected,
                .gamepad = gamepad,
            }},
            .platformEvents = {Platform::GamepadDisconnectedEvent{
                .gamepad = gamepad,
            }},
        });
    ASSERT_TRUE(disconnect.has_value());
    ASSERT_TRUE(producer->produce(tree.context.get(), *disconnect).has_value());
    auto finalState =
        tree.context->flowInputDeviceState(UI::UIFlowPrimaryLocalUser);
    ASSERT_TRUE(finalState.has_value());
    EXPECT_EQ(finalState->device, UI::UIFlowInputDevice::KeyboardMouse);
    EXPECT_FALSE(finalState->gamepad.has_value());
    EXPECT_EQ(finalState->revision, 4U);
}

TEST_F(UIInputRouteProducerTest,
       FlowAssignedGamepadRoutesLocalUserAndKeepsReleaseLatchedAfterReassignment)
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

    auto layerNode =
        tree.updater.createElement(tree.root.rootNodeId(), UI::makePanelElement());
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
        *screen, UI::UIFlowAction::Confirm,
        UI::UIFlowActionCallback{
            [&events, &eventCount](const UI::UIFlowActionEvent& event) noexcept {
                if (eventCount < events.size())
                {
                    events[eventCount] = event;
                }
                ++eventCount;
            }}));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    constexpr UI::UIFlowLocalUserId User2{2};
    constexpr UI::UIFlowLocalUserId User3{3};
    expectOk(tree.updater.assignFlowGamepad(gamepad, User2));

    auto firstDown = buildFrame(
        *builder, window,
        {
            .frameId = {240},
            .transitions = {gamepadButtonDown(window, gamepad)},
            .gamepadSnapshots = {heldSouthSnapshot(gamepad, 240)},
        });
    ASSERT_TRUE(firstDown.has_value());
    auto firstDownOutput = producer->produce(tree.context.get(), *firstDown);
    ASSERT_TRUE(firstDownOutput.has_value());
    EXPECT_TRUE(firstDownOutput->consumption.isConsumed(0));
    ASSERT_EQ(eventCount, 1U);
    EXPECT_EQ(events[0].localUser, User2);
    EXPECT_EQ(events[0].source, UI::UIFlowActionSource::Gamepad);

    expectOk(tree.updater.assignFlowGamepad(gamepad, User3));
    auto user2State = tree.context->flowInputDeviceState(User2);
    ASSERT_TRUE(user2State.has_value());
    EXPECT_EQ(user2State->device, UI::UIFlowInputDevice::KeyboardMouse);
    EXPECT_FALSE(user2State->gamepad.has_value());
    EXPECT_EQ(user2State->revision, 2U);

    auto release = buildFrame(
        *builder, window,
        {
            .frameId = {241},
            .transitions = {gamepadButtonUp(window, gamepad)},
            .gamepadSnapshots = {releasedSouthSnapshot(gamepad, 241)},
        });
    ASSERT_TRUE(release.has_value());
    auto releaseOutput = producer->produce(tree.context.get(), *release);
    ASSERT_TRUE(releaseOutput.has_value());
    EXPECT_TRUE(releaseOutput->consumption.isConsumed(0));
    EXPECT_EQ(eventCount, 1U);

    auto secondDown = buildFrame(
        *builder, window,
        {
            .frameId = {242},
            .transitions = {gamepadButtonDown(window, gamepad)},
            .gamepadSnapshots = {heldSouthSnapshot(gamepad, 242)},
        });
    ASSERT_TRUE(secondDown.has_value());
    auto secondDownOutput = producer->produce(tree.context.get(), *secondDown);
    ASSERT_TRUE(secondDownOutput.has_value());
    EXPECT_TRUE(secondDownOutput->consumption.isConsumed(0));
    ASSERT_EQ(eventCount, 2U);
    EXPECT_EQ(events[1].localUser, User3);
    EXPECT_EQ(events[1].source, UI::UIFlowActionSource::Gamepad);

    auto user3State = tree.context->flowInputDeviceState(User3);
    ASSERT_TRUE(user3State.has_value());
    EXPECT_EQ(user3State->device, UI::UIFlowInputDevice::Gamepad);
    EXPECT_EQ(user3State->gamepad, gamepad);
    EXPECT_EQ(user3State->revision, 1U);

    auto reset = buildFrame(
        *builder, window,
        {
            .frameId = {243},
            .transitions = {Platform::InputStreamReset{
                .routedWindow = window,
                .reason = Platform::InputResetReason::BackendRecovery,
            }},
        });
    ASSERT_TRUE(reset.has_value());
    auto resetOutput = producer->produce(tree.context.get(), *reset);
    ASSERT_TRUE(resetOutput.has_value());
    EXPECT_FALSE(resetOutput->consumption.isConsumed(0));

    user3State = tree.context->flowInputDeviceState(User3);
    auto resetOwner = tree.updater.flowLocalUserForGamepad(gamepad);
    ASSERT_TRUE(user3State.has_value());
    ASSERT_TRUE(resetOwner.has_value());
    EXPECT_EQ(user3State->device, UI::UIFlowInputDevice::KeyboardMouse);
    EXPECT_FALSE(user3State->gamepad.has_value());
    EXPECT_EQ(user3State->platformFrame, Platform::PlatformFrameId{243});
    EXPECT_EQ(user3State->revision, 2U);
    EXPECT_EQ(*resetOwner, UI::UIFlowPrimaryLocalUser);

    auto staleRelease = buildFrame(
        *builder, window,
        {
            .frameId = {244},
            .transitions = {gamepadButtonUp(window, gamepad)},
            .gamepadSnapshots = {releasedSouthSnapshot(gamepad, 244)},
        });
    ASSERT_TRUE(staleRelease.has_value());
    auto staleReleaseOutput =
        producer->produce(tree.context.get(), *staleRelease);
    ASSERT_TRUE(staleReleaseOutput.has_value());
    EXPECT_FALSE(staleReleaseOutput->consumption.isConsumed(0));
    EXPECT_EQ(eventCount, 2U);
}

TEST_F(UIInputRouteProducerTest,
       FlowDisconnectFallsBackOnlyTheAssignedLocalUser)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    constexpr UI::UIFlowLocalUserId User2{2};
    expectOk(tree.updater.assignFlowGamepad(gamepad, User2));

    auto keyboardDown = buildFrame(
        *builder, window,
        {
            .frameId = {250},
            .transitions = {keyDown(window, Platform::Key::A)},
            .heldKeys = {Platform::Key::A},
        });
    ASSERT_TRUE(keyboardDown.has_value());
    ASSERT_TRUE(producer->produce(tree.context.get(), *keyboardDown).has_value());

    auto gamepadDown = buildFrame(
        *builder, window,
        {
            .frameId = {251},
            .transitions = {gamepadButtonDown(window, gamepad)},
            .heldKeys = {Platform::Key::A},
            .gamepadSnapshots = {heldSouthSnapshot(gamepad, 251)},
        });
    ASSERT_TRUE(gamepadDown.has_value());
    ASSERT_TRUE(producer->produce(tree.context.get(), *gamepadDown).has_value());

    auto primaryBefore =
        tree.context->flowInputDeviceState(UI::UIFlowPrimaryLocalUser);
    auto user2Before = tree.context->flowInputDeviceState(User2);
    ASSERT_TRUE(primaryBefore.has_value());
    ASSERT_TRUE(user2Before.has_value());
    EXPECT_EQ(primaryBefore->device, UI::UIFlowInputDevice::KeyboardMouse);
    EXPECT_EQ(primaryBefore->sourceSequence, 1U);
    EXPECT_EQ(primaryBefore->revision, 0U);
    EXPECT_EQ(user2Before->device, UI::UIFlowInputDevice::Gamepad);
    EXPECT_EQ(user2Before->gamepad, gamepad);
    EXPECT_EQ(user2Before->revision, 1U);

    auto disconnect = buildFrame(
        *builder, window,
        {
            .frameId = {252},
            .transitions = {Platform::InputCancelTransition{
                .routedWindow = window,
                .reason = Platform::InputCancelReason::DeviceDisconnected,
                .gamepad = gamepad,
            }},
            .platformEvents = {Platform::GamepadDisconnectedEvent{
                .gamepad = gamepad,
            }},
        });
    ASSERT_TRUE(disconnect.has_value());
    ASSERT_TRUE(producer->produce(tree.context.get(), *disconnect).has_value());

    auto primaryAfter =
        tree.context->flowInputDeviceState(UI::UIFlowPrimaryLocalUser);
    auto user2After = tree.context->flowInputDeviceState(User2);
    auto ownerAfter = tree.updater.flowLocalUserForGamepad(gamepad);
    ASSERT_TRUE(primaryAfter.has_value());
    ASSERT_TRUE(user2After.has_value());
    ASSERT_TRUE(ownerAfter.has_value());
    EXPECT_EQ(*primaryAfter, *primaryBefore);
    EXPECT_EQ(user2After->device, UI::UIFlowInputDevice::KeyboardMouse);
    EXPECT_FALSE(user2After->gamepad.has_value());
    EXPECT_EQ(user2After->platformFrame, Platform::PlatformFrameId{252});
    EXPECT_EQ(user2After->revision, 2U);
    EXPECT_EQ(*ownerAfter, UI::UIFlowPrimaryLocalUser);
}

} // namespace
} // namespace Tina::Tests
