#include "UIInputRouteProducerTestSupport.hpp"

#include <array>

namespace Tina::Tests {
namespace {

[[nodiscard]] UI::UIContextCapacityConfig focusNavigationCapacities() noexcept
{
    return {
        .nodeCapacity = 32,
        .rootCapacity = 1,
        .dirtyQueueCapacity = 32,
        .paintSnapshotCapacity = 32,
        .routePathCapacity = 16,
        .routedPointerListenerCapacity = 8,
        .buttonActionCapacity = 8,
        .textByteCapacity = 1024,
    };
}

struct TabRouteNodes final {
    UI::UINodeId tabView{};
    UI::UINodeId firstTab{};
    UI::UINodeId secondTab{};
    UI::UINodeId firstPanel{};
    UI::UINodeId secondPanel{};
};

[[nodiscard]] TabRouteNodes createTabRouteTree(
    RouteTree& tree, UI::UITabViewConfig config = {})
{
    expectOk(tree.updater.destroy(tree.target));
    auto tabView = tree.updater.createElement(
        tree.panel, UI::makeTabViewElement(config, fixedSize(80.0F, 80.0F)));
    if (!tabView)
    {
        ADD_FAILURE() << tabView.error().message;
        return {};
    }
    auto firstTab = tree.updater.createElement(
        *tabView, UI::makeTabElement("First", {}, fixedSize(40.0F, 20.0F)));
    auto secondTab = tree.updater.createElement(
        *tabView, UI::makeTabElement("Second", {}, fixedSize(40.0F, 20.0F)));
    auto firstPanel = tree.updater.createElement(
        *tabView, UI::makePanelElement(fixedSize(40.0F, 40.0F)));
    auto secondPanel = tree.updater.createElement(
        *tabView, UI::makePanelElement(fixedSize(40.0F, 40.0F)));
    if (!firstTab || !secondTab || !firstPanel || !secondPanel)
    {
        ADD_FAILURE() << "failed to create TabView route nodes";
        return {};
    }
    const std::array items{
        UI::UITabViewItem{.tab = *firstTab, .panel = *firstPanel},
        UI::UITabViewItem{.tab = *secondTab, .panel = *secondPanel},
    };
    expectOk(tree.updater.setTabViewItems(*tabView, items));
    tree.target = *firstTab;
    return {
        .tabView = *tabView,
        .firstTab = *firstTab,
        .secondTab = *secondTab,
        .firstPanel = *firstPanel,
        .secondPanel = *secondPanel,
    };
}

TEST_F(UIInputRouteProducerTest, KeyboardArrowsMoveSpatialFocusAndConsumeMatchingRelease)
{
    auto tree = createRouteTree(window, focusNavigationCapacities());
    ASSERT_NE(tree.context, nullptr);
    auto second = tree.updater.createElement(tree.panel, UI::makeSliderElement());
    ASSERT_TRUE(second.has_value()) << (second ? "" : second.error().message);
    expectOk(tree.updater.setLayoutStyle(*second, fixedSize(40.0F, 40.0F)));
    expectOk(tree.updater.setSliderRange(*second, 0.0F, 100.0F, 1.0F));
    expectOk(tree.updater.setSliderValue(*second, 25.0F));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    expectOk(tree.context->requestFocus(tree.target));

    auto producer = createProducer();
    ASSERT_NE(producer, nullptr);
    auto down = buildFrame(*builder, window, {
                                                     .frameId = {300},
                                                     .transitions = {keyDown(window, Platform::Key::Down)},
                                                     .heldKeys = {Platform::Key::Down},
                                                 });
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    auto downOutput = producer->produce(tree.context.get(), *down);
    ASSERT_TRUE(downOutput.has_value()) << (downOutput ? "" : downOutput.error().message);
    EXPECT_TRUE(downOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), *second);

    auto up = buildFrame(*builder, window, {
                                                   .frameId = {301},
                                                   .transitions = {keyUp(window, Platform::Key::Down)},
                                               });
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    auto upOutput = producer->produce(tree.context.get(), *up);
    ASSERT_TRUE(upOutput.has_value()) << (upOutput ? "" : upOutput.error().message);
    EXPECT_TRUE(upOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), *second);
    auto sliderValue = tree.updater.sliderValue(*second);
    ASSERT_TRUE(sliderValue.has_value()) << (sliderValue ? "" : sliderValue.error().message);
    EXPECT_FLOAT_EQ(*sliderValue, 25.0F);
}

TEST_F(UIInputRouteProducerTest, GamepadDpadUsesTheSameSpatialFocusRoute)
{
    auto tree = createRouteTree(window, focusNavigationCapacities());
    ASSERT_NE(tree.context, nullptr);
    auto second = tree.updater.createElement(tree.panel, UI::makeSliderElement());
    ASSERT_TRUE(second.has_value()) << (second ? "" : second.error().message);
    expectOk(tree.updater.setLayoutStyle(*second, fixedSize(40.0F, 40.0F)));
    expectOk(tree.updater.setSliderRange(*second, 0.0F, 100.0F, 1.0F));
    expectOk(tree.updater.setSliderValue(*second, 25.0F));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    expectOk(tree.context->requestFocus(tree.target));

    auto producer = createProducer();
    ASSERT_NE(producer, nullptr);
    Platform::GamepadSnapshot heldDpad{
        .gamepad = gamepad,
        .revision = 310,
    };
    heldDpad.heldButtons.set(static_cast<usize>(Platform::GamepadButton::DpadDown));
    auto frame = buildFrame(*builder, window, {
                                                      .frameId = {310},
                                                      .transitions = {gamepadButton(
                                                          window,
                                                          gamepad,
                                                          Platform::GamepadButton::DpadDown,
                                                          Platform::DigitalTransition::Down)},
                                                      .gamepadSnapshots = {heldDpad},
                                                  });
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
    auto output = producer->produce(tree.context.get(), *frame);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    EXPECT_TRUE(output->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), *second);
    auto sliderValue = tree.updater.sliderValue(*second);
    ASSERT_TRUE(sliderValue.has_value()) << (sliderValue ? "" : sliderValue.error().message);
    EXPECT_FLOAT_EQ(*sliderValue, 25.0F);
}

TEST_F(UIInputRouteProducerTest, HorizontalTabViewOwnsArrowAndHomeEndBeforeSpatialFocus)
{
    auto tree = createRouteTree(window, focusNavigationCapacities());
    ASSERT_NE(tree.context, nullptr);
    const TabRouteNodes tabs = createTabRouteTree(tree);
    ASSERT_TRUE(tabs.tabView.hasValue());
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    expectOk(tree.context->requestFocus(tabs.firstTab));

    auto producer = createProducer();
    ASSERT_NE(producer, nullptr);
    auto nextDown = buildFrame(*builder, window, {
                                                     .frameId = {320},
                                                     .transitions = {keyDown(window, Platform::Key::Right)},
                                                     .heldKeys = {Platform::Key::Right},
                                                 });
    ASSERT_TRUE(nextDown.has_value()) << (nextDown ? "" : nextDown.error().message);
    auto nextOutput = producer->produce(tree.context.get(), *nextDown);
    ASSERT_TRUE(nextOutput.has_value()) << (nextOutput ? "" : nextOutput.error().message);
    EXPECT_TRUE(nextOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), tabs.secondTab);
    EXPECT_EQ(tree.updater.tabViewActiveTab(tabs.tabView).value(), tabs.secondTab);

    auto nextUp = buildFrame(*builder, window, {
                                                   .frameId = {321},
                                                   .transitions = {keyUp(window, Platform::Key::Right)},
                                               });
    ASSERT_TRUE(nextUp.has_value()) << (nextUp ? "" : nextUp.error().message);
    auto nextUpOutput = producer->produce(tree.context.get(), *nextUp);
    ASSERT_TRUE(nextUpOutput.has_value()) << (nextUpOutput ? "" : nextUpOutput.error().message);
    EXPECT_TRUE(nextUpOutput->consumption.isConsumed(0));

    auto firstDown = buildFrame(*builder, window, {
                                                      .frameId = {322},
                                                      .transitions = {keyDown(window, Platform::Key::Home)},
                                                      .heldKeys = {Platform::Key::Home},
                                                  });
    ASSERT_TRUE(firstDown.has_value()) << (firstDown ? "" : firstDown.error().message);
    auto firstOutput = producer->produce(tree.context.get(), *firstDown);
    ASSERT_TRUE(firstOutput.has_value()) << (firstOutput ? "" : firstOutput.error().message);
    EXPECT_TRUE(firstOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), tabs.firstTab);
    EXPECT_EQ(tree.updater.tabViewActiveTab(tabs.tabView).value(), tabs.firstTab);
}

TEST_F(UIInputRouteProducerTest, ManualTabViewMovesFocusWithoutSelectionUntilAccept)
{
    auto tree = createRouteTree(window, focusNavigationCapacities());
    ASSERT_NE(tree.context, nullptr);
    const TabRouteNodes tabs = createTabRouteTree(
        tree, UI::UITabViewConfig{.activationMode = UI::UITabActivationMode::Manual});
    ASSERT_TRUE(tabs.tabView.hasValue());
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    expectOk(tree.context->requestFocus(tabs.firstTab));

    auto producer = createProducer();
    ASSERT_NE(producer, nullptr);
    auto move = buildFrame(*builder, window, {
                                                 .frameId = {330},
                                                 .transitions = {keyDown(window, Platform::Key::Right)},
                                                 .heldKeys = {Platform::Key::Right},
                                             });
    ASSERT_TRUE(move.has_value()) << (move ? "" : move.error().message);
    auto moveOutput = producer->produce(tree.context.get(), *move);
    ASSERT_TRUE(moveOutput.has_value()) << (moveOutput ? "" : moveOutput.error().message);
    EXPECT_TRUE(moveOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), tabs.secondTab);
    EXPECT_EQ(tree.updater.tabViewActiveTab(tabs.tabView).value(), tabs.firstTab);

    auto activate = buildFrame(*builder, window, {
                                                     .frameId = {331},
                                                     .transitions = {keyDown(window, Platform::Key::Enter)},
                                                     .heldKeys = {Platform::Key::Enter},
                                                 });
    ASSERT_TRUE(activate.has_value()) << (activate ? "" : activate.error().message);
    auto activateOutput = producer->produce(tree.context.get(), *activate);
    ASSERT_TRUE(activateOutput.has_value()) << (activateOutput ? "" : activateOutput.error().message);
    EXPECT_TRUE(activateOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.updater.tabViewActiveTab(tabs.tabView).value(), tabs.secondTab);
}

TEST_F(UIInputRouteProducerTest, VerticalTabViewUsesGamepadDpadAndConsumesRelease)
{
    auto tree = createRouteTree(window, focusNavigationCapacities());
    ASSERT_NE(tree.context, nullptr);
    const TabRouteNodes tabs = createTabRouteTree(
        tree, UI::UITabViewConfig{.placement = UI::UITabViewPlacement::Left});
    ASSERT_TRUE(tabs.tabView.hasValue());
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    expectOk(tree.context->requestFocus(tabs.firstTab));

    auto producer = createProducer();
    ASSERT_NE(producer, nullptr);
    Platform::GamepadSnapshot heldDpad{
        .gamepad = gamepad,
        .revision = 340,
    };
    heldDpad.heldButtons.set(static_cast<usize>(Platform::GamepadButton::DpadDown));
    auto down = buildFrame(*builder, window, {
                                                     .frameId = {340},
                                                     .transitions = {gamepadButton(
                                                         window, gamepad, Platform::GamepadButton::DpadDown,
                                                         Platform::DigitalTransition::Down)},
                                                     .gamepadSnapshots = {heldDpad},
                                                 });
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    auto downOutput = producer->produce(tree.context.get(), *down);
    ASSERT_TRUE(downOutput.has_value()) << (downOutput ? "" : downOutput.error().message);
    EXPECT_TRUE(downOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), tabs.secondTab);
    EXPECT_EQ(tree.updater.tabViewActiveTab(tabs.tabView).value(), tabs.secondTab);

    auto up = buildFrame(*builder, window, {
                                                   .frameId = {341},
                                                   .transitions = {gamepadButton(
                                                       window, gamepad, Platform::GamepadButton::DpadDown,
                                                       Platform::DigitalTransition::Up)},
                                                   .gamepadSnapshots = {Platform::GamepadSnapshot{
                                                       .gamepad = gamepad,
                                                       .revision = 341,
                                                   }},
                                               });
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    auto upOutput = producer->produce(tree.context.get(), *up);
    ASSERT_TRUE(upOutput.has_value()) << (upOutput ? "" : upOutput.error().message);
    EXPECT_TRUE(upOutput->consumption.isConsumed(0));
}

} // namespace
} // namespace Tina::Tests
