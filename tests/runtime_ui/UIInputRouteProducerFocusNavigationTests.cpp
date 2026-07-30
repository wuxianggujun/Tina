#include "UIInputRouteProducerTestSupport.hpp"

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

TEST_F(UIInputRouteProducerTest, KeyboardArrowsMoveSpatialFocusAndConsumeMatchingRelease)
{
    auto tree = createRouteTree(window, focusNavigationCapacities());
    ASSERT_NE(tree.context, nullptr);
    auto second = tree.updater.createElement(tree.panel, UI::makeButtonElement());
    ASSERT_TRUE(second.has_value()) << (second ? "" : second.error().message);
    expectOk(tree.updater.setLayoutStyle(*second, fixedSize(40.0F, 40.0F)));
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
}

TEST_F(UIInputRouteProducerTest, GamepadDpadUsesTheSameSpatialFocusRoute)
{
    auto tree = createRouteTree(window, focusNavigationCapacities());
    ASSERT_NE(tree.context, nullptr);
    auto second = tree.updater.createElement(tree.panel, UI::makeButtonElement());
    ASSERT_TRUE(second.has_value()) << (second ? "" : second.error().message);
    expectOk(tree.updater.setLayoutStyle(*second, fixedSize(40.0F, 40.0F)));
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
}

} // namespace
} // namespace Tina::Tests
