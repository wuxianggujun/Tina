#include "UIInputRouteProducerTestSupport.hpp"

namespace Tina::Tests {
namespace {

TEST_F(UIInputRouteProducerTest, ListViewConsumesKeyboardNavigationActivationAndReleaseAfterFocusChange)
{
    auto producer = createProducer();
    CollectionRouteTree tree = createCollectionRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    ASSERT_TRUE(tree.listView.hasValue());
    expectOk(tree.context->requestFocus(tree.listView));

    u64 nextFrame = 100;
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
        if (frame->inputTransitions().size() != 2U || !output->consumption.isConsumed(0) ||
            !output->consumption.isConsumed(1))
        {
            ADD_FAILURE() << "ListView did not consume a keyboard Down/Up pair";
            return false;
        }
        return true;
    };

    ASSERT_TRUE(routeKeyPair(Platform::Key::Down));
    ASSERT_TRUE(routeKeyPair(Platform::Key::Down));
    auto selection = tree.updater.listViewSelection(tree.listView);
    ASSERT_TRUE(selection.has_value()) << (selection ? "" : selection.error().message);
    EXPECT_EQ(*selection, (UI::UIListViewSelection{.key = 3, .logicalIndex = 2}));

    ASSERT_TRUE(routeKeyPair(Platform::Key::End));
    ASSERT_TRUE(routeKeyPair(Platform::Key::Home));
    ASSERT_TRUE(routeKeyPair(Platform::Key::PageDown));
    ASSERT_TRUE(routeKeyPair(Platform::Key::PageUp));
    ASSERT_TRUE(routeKeyPair(Platform::Key::Enter));
    ASSERT_TRUE(routeKeyPair(Platform::Key::KeypadEnter));
    selection = tree.updater.listViewSelection(tree.listView);
    ASSERT_TRUE(selection.has_value()) << (selection ? "" : selection.error().message);
    EXPECT_EQ(*selection, (UI::UIListViewSelection{.key = 1, .logicalIndex = 0}));

    auto down = buildFrame(
        *builder,
        window,
        {
            .frameId = {nextFrame++},
            .transitions = {keyDown(window, Platform::Key::Down)},
            .heldKeys = {Platform::Key::Down},
        });
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    auto downOutput = producer->produce(tree.context.get(), *down);
    ASSERT_TRUE(downOutput.has_value()) << (downOutput ? "" : downOutput.error().message);
    EXPECT_TRUE(downOutput->consumption.isConsumed(0));
    expectOk(tree.context->requestFocus(tree.other));

    auto release = buildFrame(
        *builder,
        window,
        {
            .frameId = {nextFrame++},
            .transitions = {keyUp(window, Platform::Key::Down)},
        });
    ASSERT_TRUE(release.has_value()) << (release ? "" : release.error().message);
    auto releaseOutput = producer->produce(tree.context.get(), *release);
    ASSERT_TRUE(releaseOutput.has_value()) << (releaseOutput ? "" : releaseOutput.error().message);
    EXPECT_TRUE(releaseOutput->consumption.isConsumed(0));
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.other);

    auto unrelatedDown = buildFrame(
        *builder,
        window,
        {
            .frameId = {nextFrame++},
            .transitions = {keyDown(window, Platform::Key::Down)},
            .heldKeys = {Platform::Key::Down},
        });
    ASSERT_TRUE(unrelatedDown.has_value()) << (unrelatedDown ? "" : unrelatedDown.error().message);
    auto unrelatedOutput = producer->produce(tree.context.get(), *unrelatedDown);
    ASSERT_TRUE(unrelatedOutput.has_value()) << (unrelatedOutput ? "" : unrelatedOutput.error().message);
    EXPECT_FALSE(unrelatedOutput->consumption.isConsumed(0));
}
TEST_F(UIInputRouteProducerTest, TreeViewConsumesHierarchyKeyboardAndGamepadCommands)
{
    auto producer = createProducer();
    CollectionRouteTree tree = createCollectionRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    ASSERT_TRUE(tree.treeView.hasValue());
    expectOk(tree.context->requestFocus(tree.treeView));

    u64 nextFrame = 120;
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
        return frame->inputTransitions().size() == 2U && output->consumption.isConsumed(0) &&
               output->consumption.isConsumed(1);
    };
    const auto routeGamepadPair = [&](Platform::GamepadButton button) -> bool {
        const u64 frameId = nextFrame++;
        auto frame = buildFrame(
            *builder,
            window,
            {
                .frameId = {frameId},
                .transitions = {
                    gamepadButton(window, gamepad, button, Platform::DigitalTransition::Down),
                    gamepadButton(window, gamepad, button, Platform::DigitalTransition::Up),
                },
                .gamepadSnapshots = {Platform::GamepadSnapshot{.gamepad = gamepad, .revision = frameId}},
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
        return frame->inputTransitions().size() == 2U && output->consumption.isConsumed(0) &&
               output->consumption.isConsumed(1);
    };

    ASSERT_TRUE(routeKeyPair(Platform::Key::Down));
    ASSERT_TRUE(routeKeyPair(Platform::Key::Right));
    auto selection = tree.updater.treeViewSelection(tree.treeView);
    ASSERT_TRUE(selection.has_value()) << (selection ? "" : selection.error().message);
    EXPECT_EQ(*selection, (UI::UITreeViewSelection{.key = 11, .logicalIndex = 1, .level = 1}));
    ASSERT_TRUE(routeKeyPair(Platform::Key::Down));
    selection = tree.updater.treeViewSelection(tree.treeView);
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(*selection, (UI::UITreeViewSelection{.key = 13, .logicalIndex = 3, .level = 0}));
    ASSERT_TRUE(routeKeyPair(Platform::Key::Left));
    ASSERT_TRUE(routeKeyPair(Platform::Key::Home));
    ASSERT_TRUE(routeKeyPair(Platform::Key::Space));
    EXPECT_FALSE(tree.treeSource->rootExpanded);
    ASSERT_TRUE(routeKeyPair(Platform::Key::Right));
    EXPECT_TRUE(tree.treeSource->rootExpanded);
    ASSERT_TRUE(routeKeyPair(Platform::Key::Enter));

    ASSERT_TRUE(routeGamepadPair(Platform::GamepadButton::DpadRight));
    ASSERT_TRUE(routeGamepadPair(Platform::GamepadButton::DpadLeft));
    ASSERT_TRUE(routeGamepadPair(Platform::GamepadButton::DpadDown));
    ASSERT_TRUE(routeGamepadPair(Platform::GamepadButton::DpadUp));
    ASSERT_TRUE(routeGamepadPair(Platform::GamepadButton::South));
    selection = tree.updater.treeViewSelection(tree.treeView);
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(*selection, (UI::UITreeViewSelection{.key = 10, .logicalIndex = 0, .level = 0}));
}
TEST_F(UIInputRouteProducerTest, CollectionCommandResetClearsPressedDebounceState)
{
    auto producer = createProducer();
    CollectionRouteTree tree = createCollectionRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    expectOk(tree.context->requestFocus(tree.listView));

    auto down = buildFrame(
        *builder,
        window,
        {
            .frameId = {140},
            .transitions = {keyDown(window, Platform::Key::Down)},
            .heldKeys = {Platform::Key::Down},
        });
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    auto downOutput = producer->produce(tree.context.get(), *down);
    ASSERT_TRUE(downOutput.has_value()) << (downOutput ? "" : downOutput.error().message);
    EXPECT_TRUE(downOutput->consumption.isConsumed(0));

    auto reset = buildFrame(
        *builder,
        window,
        {
            .frameId = {141},
            .transitions = {Platform::InputStreamReset{.routedWindow = window}},
        });
    ASSERT_TRUE(reset.has_value()) << (reset ? "" : reset.error().message);
    auto resetOutput = producer->produce(tree.context.get(), *reset);
    ASSERT_TRUE(resetOutput.has_value()) << (resetOutput ? "" : resetOutput.error().message);
    EXPECT_FALSE(resetOutput->consumption.isConsumed(0));
    expectOk(tree.context->requestFocus(tree.listView));

    auto staleRelease = buildFrame(
        *builder,
        window,
        {
            .frameId = {142},
            .transitions = {keyUp(window, Platform::Key::Down)},
        });
    ASSERT_TRUE(staleRelease.has_value()) << (staleRelease ? "" : staleRelease.error().message);
    auto staleOutput = producer->produce(tree.context.get(), *staleRelease);
    ASSERT_TRUE(staleOutput.has_value()) << (staleOutput ? "" : staleOutput.error().message);
    EXPECT_FALSE(staleOutput->consumption.isConsumed(0));

    auto freshDown = buildFrame(
        *builder,
        window,
        {
            .frameId = {143},
            .transitions = {keyDown(window, Platform::Key::Down)},
            .heldKeys = {Platform::Key::Down},
        });
    ASSERT_TRUE(freshDown.has_value()) << (freshDown ? "" : freshDown.error().message);
    auto freshOutput = producer->produce(tree.context.get(), *freshDown);
    ASSERT_TRUE(freshOutput.has_value()) << (freshOutput ? "" : freshOutput.error().message);
    EXPECT_TRUE(freshOutput->consumption.isConsumed(0));
}

} // namespace
} // namespace Tina::Tests
