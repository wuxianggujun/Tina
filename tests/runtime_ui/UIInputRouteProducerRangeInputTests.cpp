#include "UIInputRouteProducerTestSupport.hpp"

namespace Tina::Tests {
namespace {

[[nodiscard]] UI::UIContextCapacityConfig rangeInputCapacities() noexcept
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

TEST_F(UIInputRouteProducerTest, KeyboardRangeInputSuppressesGameplayThroughMatchingUpAfterFocusChange)
{
    RouteTree tree = createRouteTree(window, rangeInputCapacities());
    auto slider = tree.updater.createElement(tree.panel, UI::makeSliderElement());
    ASSERT_TRUE(slider.has_value()) << (slider ? "" : slider.error().message);
    expectOk(tree.updater.setLayoutStyle(*slider, fixedSize(40.0F, 24.0F)));
    expectOk(tree.updater.setSliderRange(*slider, 0.0F, 100.0F, 5.0F));
    expectOk(tree.updater.setSliderValue(*slider, 25.0F));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    expectOk(tree.context->requestFocus(*slider));

    auto producer = createProducer();
    auto mapper = createKeyMapper(Platform::Key::Right);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(mapper, nullptr);

    auto down = buildFrame(*builder, window, {
                                                     .frameId = {400},
                                                     .transitions = {keyDown(window, Platform::Key::Right)},
                                                     .heldKeys = {Platform::Key::Right},
                                                 });
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    auto downOutput = producer->produce(tree.context.get(), *down);
    ASSERT_TRUE(downOutput.has_value()) << (downOutput ? "" : downOutput.error().message);
    EXPECT_TRUE(downOutput->consumption.isConsumed(0));
    auto value = tree.updater.sliderValue(*slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 30.0F);
    ASSERT_TRUE(mapper->mapFrame(*down, downOutput->consumption, downOutput->claims, 0, 0,
                                 &lastPresentedCamera2D)
                    .has_value());
    auto suppressed = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(suppressed.has_value());
    EXPECT_TRUE(suppressed->transitions.empty());
    EXPECT_FALSE(suppressed->isActive(NavigationAction));
    ASSERT_TRUE(mapper->completeSimulationTick(0).has_value());

    expectOk(tree.context->requestFocus(tree.target));
    auto up = buildFrame(*builder, window, {
                                                   .frameId = {401},
                                                   .transitions = {keyUp(window, Platform::Key::Right)},
                                               });
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    auto upOutput = producer->produce(tree.context.get(), *up);
    ASSERT_TRUE(upOutput.has_value()) << (upOutput ? "" : upOutput.error().message);
    EXPECT_TRUE(upOutput->consumption.isConsumed(0));
    ASSERT_TRUE(mapper->mapFrame(*up, upOutput->consumption, upOutput->claims, 1, 1,
                                 &lastPresentedCamera2D)
                    .has_value());
    auto released = mapper->simulationActionsForTick(1);
    ASSERT_TRUE(released.has_value());
    EXPECT_TRUE(released->transitions.empty());
    ASSERT_TRUE(mapper->completeSimulationTick(1).has_value());

    expectOk(tree.context->clearFocus());
    auto gameplayDown = buildFrame(*builder, window, {
                                                             .frameId = {402},
                                                             .transitions = {keyDown(window, Platform::Key::Right)},
                                                             .heldKeys = {Platform::Key::Right},
                                                         });
    ASSERT_TRUE(gameplayDown.has_value());
    auto gameplayOutput = producer->produce(tree.context.get(), *gameplayDown);
    ASSERT_TRUE(gameplayOutput.has_value());
    EXPECT_FALSE(gameplayOutput->consumption.isConsumed(0));
    ASSERT_TRUE(mapper->mapFrame(*gameplayDown, gameplayOutput->consumption, gameplayOutput->claims, 2, 2,
                                 &lastPresentedCamera2D)
                    .has_value());
    auto restored = mapper->simulationActionsForTick(2);
    ASSERT_TRUE(restored.has_value());
    ASSERT_EQ(restored->transitions.size(), 1U);
    ASSERT_NE(digital(restored->transitions[0]), nullptr);
    EXPECT_EQ(digital(restored->transitions[0])->kind, InputActionTransitionKind::Started);
    EXPECT_TRUE(restored->isActive(NavigationAction));
}

TEST_F(UIInputRouteProducerTest, GamepadRangeInputPrecedesSpatialFocusAndLatchesAcrossDisable)
{
    RouteTree tree = createRouteTree(window, rangeInputCapacities());
    auto slider = tree.updater.createElement(tree.panel, UI::makeSliderElement());
    ASSERT_TRUE(slider.has_value()) << (slider ? "" : slider.error().message);
    expectOk(tree.updater.setLayoutStyle(*slider, fixedSize(40.0F, 24.0F)));
    expectOk(tree.updater.setSliderRange(*slider, 0.0F, 100.0F, 5.0F));
    expectOk(tree.updater.setSliderValue(*slider, 25.0F));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    expectOk(tree.context->requestFocus(*slider));

    Platform::GamepadSnapshot held{
        .gamepad = gamepad,
        .revision = 410,
    };
    held.heldButtons.set(static_cast<usize>(Platform::GamepadButton::DpadUp));
    auto down = buildFrame(*builder, window, {
                                                     .frameId = {410},
                                                     .transitions = {gamepadButton(
                                                         window, gamepad, Platform::GamepadButton::DpadUp,
                                                         Platform::DigitalTransition::Down)},
                                                     .gamepadSnapshots = {held},
                                                 });
    ASSERT_TRUE(down.has_value());
    auto producer = createProducer();
    ASSERT_NE(producer, nullptr);
    auto downOutput = producer->produce(tree.context.get(), *down);
    ASSERT_TRUE(downOutput.has_value()) << (downOutput ? "" : downOutput.error().message);
    EXPECT_TRUE(downOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), *slider);
    auto value = tree.updater.sliderValue(*slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 30.0F);

    expectOk(tree.updater.setEnabled(*slider, false));
    auto up = buildFrame(*builder, window, {
                                                   .frameId = {411},
                                                   .transitions = {gamepadButton(
                                                       window, gamepad, Platform::GamepadButton::DpadUp,
                                                       Platform::DigitalTransition::Up)},
                                                   .gamepadSnapshots = {Platform::GamepadSnapshot{
                                                       .gamepad = gamepad,
                                                       .revision = 411,
                                                   }},
                                               });
    ASSERT_TRUE(up.has_value());
    auto upOutput = producer->produce(tree.context.get(), *up);
    ASSERT_TRUE(upOutput.has_value()) << (upOutput ? "" : upOutput.error().message);
    EXPECT_TRUE(upOutput->consumption.isConsumed(0));
}

TEST_F(UIInputRouteProducerTest, ReadOnlyRangeInputLeavesArrowGameplayVisibleWithoutSpatialFallback)
{
    RouteTree tree = createRouteTree(window, rangeInputCapacities());
    UI::UIElementDescriptor descriptor = UI::makeSliderElement();
    descriptor.semantics.readOnly = true;
    auto slider = tree.updater.createElement(tree.panel, descriptor);
    ASSERT_TRUE(slider.has_value()) << (slider ? "" : slider.error().message);
    expectOk(tree.updater.setLayoutStyle(*slider, fixedSize(40.0F, 24.0F)));
    expectOk(tree.updater.setSliderRange(*slider, 0.0F, 100.0F, 5.0F));
    expectOk(tree.updater.setSliderValue(*slider, 25.0F));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    expectOk(tree.context->requestFocus(*slider));

    auto frame = buildFrame(*builder, window, {
                                                      .frameId = {420},
                                                      .transitions = {keyDown(window, Platform::Key::Left)},
                                                      .heldKeys = {Platform::Key::Left},
                                                  });
    ASSERT_TRUE(frame.has_value());
    auto producer = createProducer();
    auto mapper = createKeyMapper(Platform::Key::Left);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(mapper, nullptr);
    auto output = producer->produce(tree.context.get(), *frame);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    EXPECT_FALSE(output->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), *slider);
    ASSERT_TRUE(mapper->mapFrame(*frame, output->consumption, output->claims, 0, 0,
                                 &lastPresentedCamera2D)
                    .has_value());
    auto gameplay = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(gameplay.has_value());
    ASSERT_EQ(gameplay->transitions.size(), 1U);
    ASSERT_NE(digital(gameplay->transitions[0]), nullptr);
    EXPECT_EQ(digital(gameplay->transitions[0])->kind, InputActionTransitionKind::Started);
    EXPECT_TRUE(gameplay->isActive(NavigationAction));
    auto value = tree.updater.sliderValue(*slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 25.0F);
}

} // namespace
} // namespace Tina::Tests
