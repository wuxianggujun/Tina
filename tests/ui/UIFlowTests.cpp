#include "UILayoutTestSupport.hpp"

#include <tina/core/id/GenerationPool.hpp>

#include <array>

namespace Tina::Tests {
namespace {

using namespace UILayoutTestSupport;

[[nodiscard]] UI::UIVisibility visibilityOf(UI::UIContext& context, UI::UINodeId node)
{
    return requireLayoutEntry(context.committedLayout(), node).effectiveVisibility;
}

struct FlowTree final {
    UI::UINodeId layer{};
    UI::UINodeId first{};
    UI::UINodeId second{};
    UI::UINodeId third{};
};

[[nodiscard]] FlowTree createFlowTree(UI::UITreeUpdater& updater, UI::UINodeId root)
{
    FlowTree tree{};
    auto layer = updater.createElement(root, UI::makePanelElement());
    EXPECT_TRUE(layer.has_value()) << (layer ? "" : layer.error().message);
    if (!layer)
    {
        return tree;
    }
    tree.layer = *layer;

    const auto createScreen = [&]() {
        auto screen = updater.createElement(tree.layer, UI::makePanelElement());
        EXPECT_TRUE(screen.has_value()) << (screen ? "" : screen.error().message);
        return screen ? *screen : UI::UINodeId{};
    };
    tree.first = createScreen();
    tree.second = createScreen();
    tree.third = createScreen();

    assertOk(updater.setLayoutStyle(root, percentSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(tree.layer, percentSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(tree.first, percentSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(tree.second, percentSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(tree.third, percentSize(100.0F, 100.0F)));
    return tree;
}

TEST_F(UILayoutTest, FlowStackPublishesOnlyTheTopScreenAndPreservesStackOrder)
{
    auto context = makeContext({
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .flowLayerCapacity = 2,
        .flowScreenCapacity = 4,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    auto updater = createUpdater(*context, root);
    const FlowTree tree = createFlowTree(updater, root.rootNodeId());
    ASSERT_TRUE(tree.third.hasValue());

    auto layer = updater.registerFlowLayer(tree.layer);
    ASSERT_TRUE(layer.has_value()) << (layer ? "" : layer.error().message);
    auto first = updater.registerFlowScreen(*layer, tree.first);
    auto second = updater.registerFlowScreen(*layer, tree.second);
    auto third = updater.registerFlowScreen(*layer, tree.third);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(third.has_value());
    EXPECT_EQ(first->nodeId(), tree.first);
    EXPECT_EQ(layer->nodeId(), tree.layer);

    assertOk(context->commitLayout({.width = 320.0F, .height = 180.0F}));
    EXPECT_EQ(visibilityOf(*context, tree.first), UI::UIVisibility::Collapsed);
    EXPECT_EQ(visibilityOf(*context, tree.second), UI::UIVisibility::Collapsed);
    EXPECT_FALSE(updater.activeFlowScreen(*layer)->hasValue());

    assertOk(updater.pushFlowScreen(*first));
    assertOk(context->commitLayout({.width = 320.0F, .height = 180.0F}));
    EXPECT_EQ(visibilityOf(*context, tree.first), UI::UIVisibility::Visible);
    EXPECT_EQ(visibilityOf(*context, tree.second), UI::UIVisibility::Collapsed);
    EXPECT_EQ(*updater.activeFlowScreen(*layer), *first);
    EXPECT_TRUE(*updater.isFlowScreenActive(*first));

    assertOk(updater.pushFlowScreen(*second));
    assertOk(context->commitLayout({.width = 320.0F, .height = 180.0F}));
    EXPECT_EQ(visibilityOf(*context, tree.first), UI::UIVisibility::Collapsed);
    EXPECT_EQ(visibilityOf(*context, tree.second), UI::UIVisibility::Visible);

    auto popped = updater.popFlowScreen(*layer);
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(*popped, *second);
    assertOk(context->commitLayout({.width = 320.0F, .height = 180.0F}));
    EXPECT_EQ(visibilityOf(*context, tree.first), UI::UIVisibility::Visible);
    EXPECT_EQ(visibilityOf(*context, tree.second), UI::UIVisibility::Collapsed);

    auto replaced = updater.replaceFlowScreen(*third);
    ASSERT_TRUE(replaced.has_value());
    EXPECT_EQ(*replaced, *first);
    assertOk(context->commitLayout({.width = 320.0F, .height = 180.0F}));
    EXPECT_EQ(visibilityOf(*context, tree.first), UI::UIVisibility::Collapsed);
    EXPECT_EQ(visibilityOf(*context, tree.third), UI::UIVisibility::Visible);
    EXPECT_EQ(*updater.activeFlowScreen(*layer), *third);

    const UI::UIFlowStatistics statistics = context->statistics().flow;
    EXPECT_EQ(statistics.registeredLayerCount, 1U);
    EXPECT_EQ(statistics.registeredScreenCount, 3U);
    EXPECT_EQ(statistics.stackedScreenCount, 1U);
    EXPECT_EQ(statistics.stackHighWater, 2U);
}

TEST_F(UILayoutTest, FlowStackDirtyCapacityFailureLeavesTheActiveScreenUnchanged)
{
    auto context = makeContext({
        .nodeCapacity = 6,
        .rootCapacity = 1,
        .dirtyQueueCapacity = 3,
        .flowLayerCapacity = 1,
        .flowScreenCapacity = 2,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    auto updater = createUpdater(*context, root);

    auto layerNode = updater.createElement(root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(layerNode.has_value());
    auto firstNode = updater.createElement(*layerNode, UI::makePanelElement());
    auto secondNode = updater.createElement(*layerNode, UI::makePanelElement());
    ASSERT_TRUE(firstNode.has_value());
    ASSERT_TRUE(secondNode.has_value());
    assertOk(updater.setLayoutStyle(root.rootNodeId(), percentSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(*layerNode, percentSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(*firstNode, percentSize(100.0F, 100.0F)));
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    assertOk(updater.setLayoutStyle(*secondNode, percentSize(100.0F, 100.0F)));
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));

    auto layer = updater.registerFlowLayer(*layerNode);
    ASSERT_TRUE(layer.has_value());
    auto first = updater.registerFlowScreen(*layer, *firstNode);
    ASSERT_TRUE(first.has_value());
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    auto second = updater.registerFlowScreen(*layer, *secondNode);
    ASSERT_TRUE(second.has_value());
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    assertOk(updater.pushFlowScreen(*first));
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));

    const UI::UIContextStatistics before = context->statistics();
    const Core::Status rejected = updater.pushFlowScreen(*second);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(*updater.activeFlowScreen(*layer), *first);
    EXPECT_TRUE(*updater.isFlowScreenActive(*first));
    EXPECT_FALSE(*updater.isFlowScreenActive(*second));
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, before.dirtyQueuePendingCount);
    EXPECT_EQ(context->statistics().flow.stackedScreenCount, 1U);
    EXPECT_EQ(context->statistics().flow.capacityFailureCount,
              before.flow.capacityFailureCount + 1U);

    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_EQ(visibilityOf(*context, *firstNode), UI::UIVisibility::Visible);
    EXPECT_EQ(visibilityOf(*context, *secondNode), UI::UIVisibility::Collapsed);
}

TEST_F(UILayoutTest, FlowRegistrationCapacityIsReclaimedByNodeLifetime)
{
    auto context = makeContext({
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .flowLayerCapacity = 1,
        .flowScreenCapacity = 2,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    auto updater = createUpdater(*context, root);
    const FlowTree tree = createFlowTree(updater, root.rootNodeId());
    auto spareLayerNode = updater.createElement(root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(spareLayerNode.has_value());

    auto layer = updater.registerFlowLayer(tree.layer);
    ASSERT_TRUE(layer.has_value());
    const auto layerOverflow = updater.registerFlowLayer(*spareLayerNode);
    ASSERT_FALSE(layerOverflow.has_value());
    EXPECT_EQ(layerOverflow.error().code, UI::UIErrorCode::CapacityExceeded);

    auto first = updater.registerFlowScreen(*layer, tree.first);
    auto second = updater.registerFlowScreen(*layer, tree.second);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    const auto screenOverflow = updater.registerFlowScreen(*layer, tree.third);
    ASSERT_FALSE(screenOverflow.has_value());
    EXPECT_EQ(screenOverflow.error().code, UI::UIErrorCode::CapacityExceeded);
    assertOk(updater.pushFlowScreen(*first));
    assertOk(updater.pushFlowScreen(*second));

    assertOk(updater.destroy(tree.second));
    EXPECT_EQ(*updater.activeFlowScreen(*layer), *first);
    EXPECT_EQ(context->statistics().flow.registeredScreenCount, 1U);
    auto third = updater.registerFlowScreen(*layer, tree.third);
    ASSERT_TRUE(third.has_value());
    assertOk(updater.pushFlowScreen(*third));

    assertOk(updater.destroy(tree.layer));
    const UI::UIFlowStatistics afterLayerDestroy = context->statistics().flow;
    EXPECT_EQ(afterLayerDestroy.registeredLayerCount, 0U);
    EXPECT_EQ(afterLayerDestroy.registeredScreenCount, 0U);
    EXPECT_EQ(afterLayerDestroy.stackedScreenCount, 0U);
    auto replacementLayer = updater.registerFlowLayer(*spareLayerNode);
    ASSERT_TRUE(replacementLayer.has_value());
    EXPECT_EQ(context->statistics().flow.capacityFailureCount, 2U);

    const auto staleScreen = updater.isFlowScreenActive(*third);
    ASSERT_FALSE(staleScreen.has_value());
    EXPECT_EQ(staleScreen.error().code, UI::UIErrorCode::InvalidFlowScreen);
}

TEST_F(UILayoutTest, FlowScreenDeactivationClearsCommittedFocus)
{
    auto context = makeContext({
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .flowLayerCapacity = 1,
        .flowScreenCapacity = 2,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    auto updater = createUpdater(*context, root);
    auto layerNode = updater.createElement(root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(layerNode.has_value());
    auto firstNode = updater.createElement(*layerNode, UI::makePanelElement());
    auto secondNode = updater.createElement(*layerNode, UI::makePanelElement());
    ASSERT_TRUE(firstNode.has_value());
    ASSERT_TRUE(secondNode.has_value());
    auto firstButton = updater.createElement(*firstNode, UI::makeButtonElement());
    auto secondButton = updater.createElement(*secondNode, UI::makeButtonElement());
    ASSERT_TRUE(firstButton.has_value());
    ASSERT_TRUE(secondButton.has_value());
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(*layerNode, fixedSize(200.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(*firstNode, fixedSize(200.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(*secondNode, fixedSize(200.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(*firstButton, fixedSize(80.0F, 30.0F)));
    assertOk(updater.setLayoutStyle(*secondButton, fixedSize(80.0F, 30.0F)));

    auto layer = updater.registerFlowLayer(*layerNode);
    ASSERT_TRUE(layer.has_value());
    auto first = updater.registerFlowScreen(*layer, *firstNode);
    auto second = updater.registerFlowScreen(*layer, *secondNode);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    assertOk(updater.pushFlowScreen(*first));
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    assertOk(updater.requestFocus(*firstButton));
    EXPECT_EQ(context->defaultActionFocus(), *firstButton);

    assertOk(updater.pushFlowScreen(*second));
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    assertOk(updater.requestFocus(*secondButton));
    EXPECT_EQ(context->defaultActionFocus(), *secondButton);
}

TEST_F(UILayoutTest, FlowBackActionRoutesOnceAndClaimsItsReleaseAcrossScreenPop)
{
    auto context = makeContext({
        .nodeCapacity = 6,
        .rootCapacity = 1,
        .flowLayerCapacity = 1,
        .flowScreenCapacity = 2,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    auto updater = createUpdater(*context, root);
    auto layerNode = updater.createElement(root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(layerNode.has_value());
    auto screenNode = updater.createElement(*layerNode, UI::makePanelElement());
    ASSERT_TRUE(screenNode.has_value());
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(*layerNode, fixedSize(200.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(*screenNode, fixedSize(200.0F, 120.0F)));

    auto layer = updater.registerFlowLayer(*layerNode);
    ASSERT_TRUE(layer.has_value());
    auto screen = updater.registerFlowScreen(*layer, *screenNode);
    ASSERT_TRUE(screen.has_value());
    assertOk(updater.pushFlowScreen(*screen));

    usize invocationCount = 0;
    UI::UIFlowActionEvent lastEvent{};
    assertOk(updater.setFlowScreenAction(
        *screen, UI::UIFlowAction::Back,
        UI::UIFlowActionCallback{
            [&invocationCount, &lastEvent](const UI::UIFlowActionEvent& event) noexcept {
                ++invocationCount;
                lastEvent = event;
            }}));
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));

    const Platform::DigitalControlIdentity escape = Platform::KeyControlIdentity{
        .window = context->ownerWindow(),
        .key = Platform::Key::Escape,
    };
    auto down = context->routeFlowAction(
        {1}, 1, UI::UIFlowPrimaryLocalUser, UI::UIFlowAction::Back,
        UI::UIFlowActionSource::Keyboard, true, escape);
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_TRUE(down->invoked);
    EXPECT_EQ(down->screen, *screen);
    EXPECT_EQ(invocationCount, 1U);
    EXPECT_EQ(lastEvent.screen, *screen);
    EXPECT_EQ(lastEvent.localUser, UI::UIFlowPrimaryLocalUser);
    EXPECT_EQ(lastEvent.source, UI::UIFlowActionSource::Keyboard);

    auto repeatedDown = context->routeFlowAction(
        {2}, 2, UI::UIFlowPrimaryLocalUser, UI::UIFlowAction::Back,
        UI::UIFlowActionSource::Keyboard, true, escape);
    ASSERT_TRUE(repeatedDown.has_value());
    EXPECT_TRUE(repeatedDown->consumed);
    EXPECT_FALSE(repeatedDown->invoked);
    EXPECT_EQ(invocationCount, 1U);

    auto popped = updater.popFlowScreen(*layer);
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(*popped, *screen);
    auto release = context->routeFlowAction(
        {3}, 3, UI::UIFlowPrimaryLocalUser, UI::UIFlowAction::Back,
        UI::UIFlowActionSource::Keyboard, false, escape);
    ASSERT_TRUE(release.has_value());
    EXPECT_TRUE(release->consumed);
    EXPECT_FALSE(release->invoked);

    assertOk(updater.clearFlowScreenAction(*screen, UI::UIFlowAction::Back));
    const UI::UIFlowStatistics statistics = context->statistics().flow;
    EXPECT_EQ(statistics.registeredActionCount, 0U);
    EXPECT_EQ(statistics.actionHighWater, 1U);
    EXPECT_EQ(statistics.actionInvocationCount, 1U);
}

TEST_F(UILayoutTest, FlowActionsUseIndependentSlotsAndBoundedCapacity)
{
    auto context = makeContext({
        .nodeCapacity = 6,
        .rootCapacity = 1,
        .flowLayerCapacity = 1,
        .flowScreenCapacity = 3,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    auto updater = createUpdater(*context, root);
    auto layerNode = updater.createElement(root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(layerNode.has_value());
    auto screenNode = updater.createElement(*layerNode, UI::makePanelElement());
    auto secondScreenNode = updater.createElement(*layerNode, UI::makePanelElement());
    ASSERT_TRUE(screenNode.has_value());
    ASSERT_TRUE(secondScreenNode.has_value());
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(*layerNode, fixedSize(200.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(*screenNode, fixedSize(200.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(*secondScreenNode, fixedSize(200.0F, 120.0F)));

    auto layer = updater.registerFlowLayer(*layerNode);
    ASSERT_TRUE(layer.has_value());
    auto screen = updater.registerFlowScreen(*layer, *screenNode);
    auto secondScreen = updater.registerFlowScreen(*layer, *secondScreenNode);
    ASSERT_TRUE(screen.has_value());
    ASSERT_TRUE(secondScreen.has_value());
    assertOk(updater.pushFlowScreen(*screen));

    usize backInvocations = 0;
    usize confirmInvocations = 0;
    usize menuInvocations = 0;
    UI::UIFlowActionEvent confirmEvent{};
    UI::UIFlowActionEvent menuEvent{};
    assertOk(updater.setFlowScreenAction(
        *screen, UI::UIFlowAction::Back,
        UI::UIFlowActionCallback{
            [&backInvocations](const UI::UIFlowActionEvent&) noexcept {
                ++backInvocations;
            }}));
    assertOk(updater.setFlowScreenAction(
        *screen, UI::UIFlowAction::Confirm,
        UI::UIFlowActionCallback{
            [&confirmInvocations, &confirmEvent](const UI::UIFlowActionEvent& event) noexcept {
                ++confirmInvocations;
                confirmEvent = event;
            }}));
    assertOk(updater.setFlowScreenAction(
        *screen, UI::UIFlowAction::Menu,
        UI::UIFlowActionCallback{
            [&menuInvocations, &menuEvent](const UI::UIFlowActionEvent& event) noexcept {
                ++menuInvocations;
                menuEvent = event;
            }}));
    const Core::Status exhausted = updater.setFlowScreenAction(
        *secondScreen, UI::UIFlowAction::Back,
        UI::UIFlowActionCallback{
            [](const UI::UIFlowActionEvent&) noexcept {}});
    ASSERT_FALSE(exhausted.has_value());
    EXPECT_EQ(exhausted.error().code, UI::UIErrorCode::CapacityExceeded);
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));

    const Platform::DigitalControlIdentity enter = Platform::KeyControlIdentity{
        .window = context->ownerWindow(),
        .key = Platform::Key::Enter,
    };
    auto confirmDown = context->routeFlowAction(
        {1}, 1, UI::UIFlowPrimaryLocalUser, UI::UIFlowAction::Confirm,
        UI::UIFlowActionSource::Keyboard, true, enter);
    ASSERT_TRUE(confirmDown.has_value());
    EXPECT_TRUE(confirmDown->consumed);
    EXPECT_TRUE(confirmDown->invoked);
    EXPECT_EQ(confirmInvocations, 1U);
    EXPECT_EQ(backInvocations, 0U);
    EXPECT_EQ(confirmEvent.action, UI::UIFlowAction::Confirm);
    EXPECT_EQ(confirmEvent.localUser, UI::UIFlowPrimaryLocalUser);

    auto confirmRelease = context->routeFlowAction(
        {2}, 2, UI::UIFlowPrimaryLocalUser, UI::UIFlowAction::Confirm,
        UI::UIFlowActionSource::Keyboard, false, enter);
    ASSERT_TRUE(confirmRelease.has_value());
    EXPECT_TRUE(confirmRelease->consumed);
    EXPECT_FALSE(confirmRelease->invoked);

    const Platform::DigitalControlIdentity menuKey = Platform::KeyControlIdentity{
        .window = context->ownerWindow(),
        .key = Platform::Key::P,
    };
    auto menuDown = context->routeFlowAction(
        {3}, 3, UI::UIFlowPrimaryLocalUser, UI::UIFlowAction::Menu,
        UI::UIFlowActionSource::Keyboard, true, menuKey);
    ASSERT_TRUE(menuDown.has_value());
    EXPECT_TRUE(menuDown->consumed);
    EXPECT_TRUE(menuDown->invoked);
    EXPECT_EQ(menuInvocations, 1U);
    EXPECT_EQ(menuEvent.action, UI::UIFlowAction::Menu);
    EXPECT_EQ(menuEvent.localUser, UI::UIFlowPrimaryLocalUser);

    auto menuRelease = context->routeFlowAction(
        {4}, 4, UI::UIFlowPrimaryLocalUser, UI::UIFlowAction::Menu,
        UI::UIFlowActionSource::Keyboard, false, menuKey);
    ASSERT_TRUE(menuRelease.has_value());
    EXPECT_TRUE(menuRelease->consumed);
    EXPECT_FALSE(menuRelease->invoked);

    assertOk(updater.clearFlowScreenAction(*screen, UI::UIFlowAction::Menu));
    assertOk(updater.clearFlowScreenAction(*screen, UI::UIFlowAction::Confirm));
    assertOk(updater.clearFlowScreenAction(*screen, UI::UIFlowAction::Back));
    const UI::UIFlowStatistics statistics = context->statistics().flow;
    EXPECT_EQ(statistics.registeredActionCount, 0U);
    EXPECT_EQ(statistics.actionHighWater, 3U);
    EXPECT_EQ(statistics.actionInvocationCount, 2U);
    EXPECT_EQ(statistics.capacityFailureCount, 1U);
}

TEST_F(UILayoutTest, FlowInputDeviceObservationIsOrderedAndRevisioned)
{
    auto context = makeContext({
        .nodeCapacity = 2,
        .rootCapacity = 1,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    auto updater = createUpdater(*context, root);

    using GamepadPool = Core::GenerationPool<int, Platform::GamepadRegistryTag>;
    auto poolResult = GamepadPool::Create(1);
    ASSERT_TRUE(poolResult.has_value());
    GamepadPool pool = std::move(*poolResult);
    auto gamepad = pool.tryEmplace(1);
    ASSERT_TRUE(gamepad.has_value());

    auto initialState =
        context->flowInputDeviceState(UI::UIFlowPrimaryLocalUser);
    ASSERT_TRUE(initialState.has_value());
    EXPECT_EQ(*initialState, UI::UIFlowInputDeviceState{});
    assertOk(context->observeFlowInputDevice(
        {1}, 10, UI::UIFlowPrimaryLocalUser,
        UI::UIFlowInputDevice::Gamepad, *gamepad));
    auto stateResult =
        context->flowInputDeviceState(UI::UIFlowPrimaryLocalUser);
    ASSERT_TRUE(stateResult.has_value());
    UI::UIFlowInputDeviceState state = *stateResult;
    EXPECT_EQ(state.device, UI::UIFlowInputDevice::Gamepad);
    EXPECT_EQ(state.gamepad, *gamepad);
    EXPECT_EQ(state.platformFrame, Platform::PlatformFrameId{1});
    EXPECT_EQ(state.sourceSequence, 10U);
    EXPECT_EQ(state.revision, 1U);
    auto updaterState =
        updater.flowInputDeviceState(UI::UIFlowPrimaryLocalUser);
    ASSERT_TRUE(updaterState.has_value());
    EXPECT_EQ(*updaterState, state);

    assertOk(context->observeFlowInputDevice(
        {2}, 11, UI::UIFlowPrimaryLocalUser,
        UI::UIFlowInputDevice::Gamepad, *gamepad));
    stateResult = context->flowInputDeviceState(UI::UIFlowPrimaryLocalUser);
    ASSERT_TRUE(stateResult.has_value());
    EXPECT_EQ(stateResult->revision, 1U);

    const Core::Status invalid = context->observeFlowInputDevice(
        {3}, 12, UI::UIFlowPrimaryLocalUser,
        UI::UIFlowInputDevice::KeyboardMouse, *gamepad);
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, UI::UIErrorCode::InvalidFlowOperation);
    stateResult = context->flowInputDeviceState(UI::UIFlowPrimaryLocalUser);
    ASSERT_TRUE(stateResult.has_value());
    EXPECT_EQ(stateResult->sourceSequence, 11U);

    assertOk(context->observeFlowInputDevice(
        {3}, 12, UI::UIFlowPrimaryLocalUser,
        UI::UIFlowInputDevice::KeyboardMouse));
    stateResult = context->flowInputDeviceState(UI::UIFlowPrimaryLocalUser);
    ASSERT_TRUE(stateResult.has_value());
    state = *stateResult;
    EXPECT_EQ(state.device, UI::UIFlowInputDevice::KeyboardMouse);
    EXPECT_FALSE(state.gamepad.has_value());
    EXPECT_EQ(state.revision, 2U);

    const Core::Status regressed = context->observeFlowInputDevice(
        {4}, 12, UI::UIFlowPrimaryLocalUser,
        UI::UIFlowInputDevice::KeyboardMouse);
    ASSERT_FALSE(regressed.has_value());
    EXPECT_EQ(regressed.error().code, UI::UIErrorCode::InvalidFlowOperation);
    stateResult = context->flowInputDeviceState(UI::UIFlowPrimaryLocalUser);
    ASSERT_TRUE(stateResult.has_value());
    EXPECT_EQ(*stateResult, state);
}

TEST_F(UILayoutTest, FlowLocalUsersIsolateGamepadAssignmentsAndInputDeviceState)
{
    auto context = makeContext({
        .nodeCapacity = 4,
        .rootCapacity = 1,
        .flowLayerCapacity = 1,
        .flowScreenCapacity = 1,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    auto updater = createUpdater(*context, root);

    auto layerNode =
        updater.createElement(root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(layerNode.has_value());
    auto screenNode = updater.createElement(*layerNode, UI::makePanelElement());
    ASSERT_TRUE(screenNode.has_value());
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(*layerNode, fixedSize(200.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(*screenNode, fixedSize(200.0F, 120.0F)));
    auto layer = updater.registerFlowLayer(*layerNode);
    ASSERT_TRUE(layer.has_value());
    auto screen = updater.registerFlowScreen(*layer, *screenNode);
    ASSERT_TRUE(screen.has_value());
    assertOk(updater.pushFlowScreen(*screen));

    std::array<UI::UIFlowActionEvent, 2> events{};
    usize eventCount = 0;
    assertOk(updater.setFlowScreenAction(
        *screen, UI::UIFlowAction::Confirm,
        UI::UIFlowActionCallback{
            [&events, &eventCount](const UI::UIFlowActionEvent& event) noexcept {
                if (eventCount < events.size())
                {
                    events[eventCount] = event;
                }
                ++eventCount;
            }}));
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));

    using GamepadPool = Core::GenerationPool<int, Platform::GamepadRegistryTag>;
    auto poolResult = GamepadPool::Create(1);
    ASSERT_TRUE(poolResult.has_value());
    GamepadPool pool = std::move(*poolResult);
    auto gamepad = pool.tryEmplace(1);
    ASSERT_TRUE(gamepad.has_value());

    constexpr UI::UIFlowLocalUserId User2{2};
    constexpr UI::UIFlowLocalUserId User3{3};
    auto initialOwner = updater.flowLocalUserForGamepad(*gamepad);
    ASSERT_TRUE(initialOwner.has_value());
    EXPECT_EQ(*initialOwner, UI::UIFlowPrimaryLocalUser);

    assertOk(updater.assignFlowGamepad(*gamepad, User2));
    auto assignedOwner = updater.flowLocalUserForGamepad(*gamepad);
    ASSERT_TRUE(assignedOwner.has_value());
    EXPECT_EQ(*assignedOwner, User2);
    assertOk(context->observeFlowInputDevice(
        {1}, 10, User2, UI::UIFlowInputDevice::Gamepad, *gamepad));

    auto primaryState =
        context->flowInputDeviceState(UI::UIFlowPrimaryLocalUser);
    auto user2State = context->flowInputDeviceState(User2);
    ASSERT_TRUE(primaryState.has_value());
    ASSERT_TRUE(user2State.has_value());
    EXPECT_EQ(primaryState->device, UI::UIFlowInputDevice::KeyboardMouse);
    EXPECT_EQ(primaryState->revision, 0U);
    EXPECT_EQ(user2State->localUser, User2);
    EXPECT_EQ(user2State->device, UI::UIFlowInputDevice::Gamepad);
    EXPECT_EQ(user2State->gamepad, *gamepad);
    EXPECT_EQ(user2State->revision, 1U);

    const Platform::DigitalControlIdentity south =
        Platform::GamepadButtonControlIdentity{
            .routedWindow = context->ownerWindow(),
            .gamepad = *gamepad,
            .button = Platform::GamepadButton::South,
        };
    auto down = context->routeFlowAction(
        {2}, 11, User2, UI::UIFlowAction::Confirm,
        UI::UIFlowActionSource::Gamepad, true, south);
    ASSERT_TRUE(down.has_value());
    EXPECT_TRUE(down->consumed);
    EXPECT_TRUE(down->invoked);
    ASSERT_EQ(eventCount, 1U);
    EXPECT_EQ(events[0].localUser, User2);

    assertOk(updater.assignFlowGamepad(*gamepad, User3));
    user2State = context->flowInputDeviceState(User2);
    auto user3State = context->flowInputDeviceState(User3);
    ASSERT_TRUE(user2State.has_value());
    ASSERT_TRUE(user3State.has_value());
    EXPECT_EQ(user2State->device, UI::UIFlowInputDevice::KeyboardMouse);
    EXPECT_FALSE(user2State->gamepad.has_value());
    EXPECT_EQ(user2State->revision, 2U);
    EXPECT_EQ(user3State->device, UI::UIFlowInputDevice::KeyboardMouse);
    EXPECT_EQ(user3State->revision, 0U);

    auto release = context->routeFlowAction(
        {3}, 12, User3, UI::UIFlowAction::Confirm,
        UI::UIFlowActionSource::Gamepad, false, south);
    ASSERT_TRUE(release.has_value());
    EXPECT_TRUE(release->consumed);
    EXPECT_FALSE(release->invoked);
    EXPECT_EQ(eventCount, 1U);

    assertOk(context->observeFlowInputDevice(
        {4}, 13, User3, UI::UIFlowInputDevice::Gamepad, *gamepad));
    auto secondDown = context->routeFlowAction(
        {4}, 13, User3, UI::UIFlowAction::Confirm,
        UI::UIFlowActionSource::Gamepad, true, south);
    ASSERT_TRUE(secondDown.has_value());
    EXPECT_TRUE(secondDown->consumed);
    EXPECT_TRUE(secondDown->invoked);
    ASSERT_EQ(eventCount, 2U);
    EXPECT_EQ(events[1].localUser, User3);

    assertOk(updater.clearFlowGamepadAssignment(*gamepad));
    auto clearedOwner = updater.flowLocalUserForGamepad(*gamepad);
    user3State = context->flowInputDeviceState(User3);
    ASSERT_TRUE(clearedOwner.has_value());
    ASSERT_TRUE(user3State.has_value());
    EXPECT_EQ(*clearedOwner, UI::UIFlowPrimaryLocalUser);
    EXPECT_EQ(user3State->device, UI::UIFlowInputDevice::KeyboardMouse);
    EXPECT_FALSE(user3State->gamepad.has_value());
    EXPECT_EQ(user3State->revision, 2U);
}

TEST_F(UILayoutTest, FlowInvalidLocalUserIsRejected)
{
    auto context = makeContext({
        .nodeCapacity = 2,
        .rootCapacity = 1,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    auto updater = createUpdater(*context, root);

    using GamepadPool = Core::GenerationPool<int, Platform::GamepadRegistryTag>;
    auto poolResult = GamepadPool::Create(1);
    ASSERT_TRUE(poolResult.has_value());
    GamepadPool pool = std::move(*poolResult);
    auto gamepad = pool.tryEmplace(1);
    ASSERT_TRUE(gamepad.has_value());

    constexpr UI::UIFlowLocalUserId InvalidZero{};
    constexpr UI::UIFlowLocalUserId InvalidOverflow{
        static_cast<u32>(UI::UIFlowLocalUserCapacity + 1U)};
    constexpr UI::UIFlowLocalUserId User2{2};

    const Core::Status zeroAssignment =
        updater.assignFlowGamepad(*gamepad, InvalidZero);
    ASSERT_FALSE(zeroAssignment.has_value());
    EXPECT_EQ(zeroAssignment.error().code, UI::UIErrorCode::InvalidFlowLocalUser);

    const Core::Status overflowAssignment =
        updater.assignFlowGamepad(*gamepad, InvalidOverflow);
    ASSERT_FALSE(overflowAssignment.has_value());
    EXPECT_EQ(overflowAssignment.error().code,
              UI::UIErrorCode::InvalidFlowLocalUser);

    auto invalidState = context->flowInputDeviceState(InvalidOverflow);
    ASSERT_FALSE(invalidState.has_value());
    EXPECT_EQ(invalidState.error().code, UI::UIErrorCode::InvalidFlowLocalUser);

    const Core::Status invalidObservation = context->observeFlowInputDevice(
        {1}, 1, InvalidZero, UI::UIFlowInputDevice::KeyboardMouse);
    ASSERT_FALSE(invalidObservation.has_value());
    EXPECT_EQ(invalidObservation.error().code,
              UI::UIErrorCode::InvalidFlowLocalUser);

    const Platform::DigitalControlIdentity escape = Platform::KeyControlIdentity{
        .window = context->ownerWindow(),
        .key = Platform::Key::Escape,
    };
    auto nonPrimaryKeyboard = context->routeFlowAction(
        {1}, 1, User2, UI::UIFlowAction::Back,
        UI::UIFlowActionSource::Keyboard, true, escape);
    ASSERT_FALSE(nonPrimaryKeyboard.has_value());
    EXPECT_EQ(nonPrimaryKeyboard.error().code,
              UI::UIErrorCode::InvalidFlowLocalUser);

    assertOk(updater.assignFlowGamepad(*gamepad, User2));
    const Platform::DigitalControlIdentity south =
        Platform::GamepadButtonControlIdentity{
            .routedWindow = context->ownerWindow(),
            .gamepad = *gamepad,
            .button = Platform::GamepadButton::South,
        };
    auto wrongGamepadUser = context->routeFlowAction(
        {2}, 2, UI::UIFlowPrimaryLocalUser, UI::UIFlowAction::Confirm,
        UI::UIFlowActionSource::Gamepad, true, south);
    ASSERT_FALSE(wrongGamepadUser.has_value());
    EXPECT_EQ(wrongGamepadUser.error().code,
              UI::UIErrorCode::InvalidFlowLocalUser);
}

TEST_F(UILayoutTest, FlowGamepadAssignmentUsesFullGenerationIdentity)
{
    auto context = makeContext({
        .nodeCapacity = 2,
        .rootCapacity = 1,
    });
    ASSERT_NE(context, nullptr);

    using GamepadPool = Core::GenerationPool<int, Platform::GamepadRegistryTag>;
    auto poolResult = GamepadPool::Create(1);
    ASSERT_TRUE(poolResult.has_value());
    GamepadPool pool = std::move(*poolResult);
    auto first = pool.tryEmplace(1);
    ASSERT_TRUE(first.has_value());

    constexpr UI::UIFlowLocalUserId User2{2};
    constexpr UI::UIFlowLocalUserId User3{3};
    assertOk(context->assignFlowGamepad(*first, User2));
    EXPECT_EQ(pool.erase(*first), Core::GenerationEraseResult::Erased);
    auto replacement = pool.tryEmplace(2);
    ASSERT_TRUE(replacement.has_value());
    ASSERT_EQ(replacement->index(), first->index());
    ASSERT_NE(*replacement, *first);

    auto replacementOwner = context->flowLocalUserForGamepad(*replacement);
    ASSERT_TRUE(replacementOwner.has_value());
    EXPECT_EQ(*replacementOwner, UI::UIFlowPrimaryLocalUser);

    assertOk(context->assignFlowGamepad(*replacement, User3));
    auto currentOwner = context->flowLocalUserForGamepad(*replacement);
    auto staleOwner = context->flowLocalUserForGamepad(*first);
    ASSERT_TRUE(currentOwner.has_value());
    ASSERT_TRUE(staleOwner.has_value());
    EXPECT_EQ(*currentOwner, User3);
    EXPECT_EQ(*staleOwner, UI::UIFlowPrimaryLocalUser);
}

} // namespace
} // namespace Tina::Tests
