#include "UIButtonActionTestSupport.hpp"

namespace Tina::Tests {
namespace {

TEST_F(
    UIButtonActionTest,
    ButtonActionCapacityRejectsValuesAboveNodeCapacityWithoutUsingUiMemory)
{
    ObservingMemoryResource resource;
    const UI::UIContextCapacityConfig capacities{
        .nodeCapacity = 4,
        .rootCapacity = 1,
        .buttonActionCapacity = 5,
    };

    const Core::Status validation =
        UI::validateUIContextCapacityConfig(capacities);
    ASSERT_FALSE(validation.has_value());
    EXPECT_EQ(validation.error().code, UI::UIErrorCode::InvalidContextConfig);

    const auto context = UI::UIContext::Create(firstWindow, capacities, resource);
    ASSERT_FALSE(context.has_value());
    EXPECT_EQ(context.error().code, UI::UIErrorCode::InvalidContextConfig);
    EXPECT_EQ(resource.allocationCount(), 0U);
    EXPECT_EQ(resource.deallocationCount(), 0U);
    EXPECT_EQ(resource.currentBytes(), 0U);
}

TEST_F(
    UIButtonActionTest,
    ButtonActionCapacityAllowsReplacingFullSlotAndReusingClearedSlot)
{
    auto context = createContext(
        firstWindow,
        {
            .nodeCapacity = 4,
            .rootCapacity = 1,
            .routePathCapacity = 4,
            .buttonActionCapacity = 1,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId first = createButton(*context, root.rootNodeId());
    const UI::UINodeId second = createButton(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    ActionRecorder recorder;

    UI::UILayoutStyle row = fixedSize(100.0F, 100.0F);
    row.flex.direction = UI::UIFlexDirection::Column;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), row));
    assertOk(updater.setLayoutStyle(first, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(second, fixedSize(40.0F, 40.0F)));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    assertOk(updater.setButtonAction(first, makeAction(recorder, 1)));
    const Core::Status capacity =
        updater.setButtonAction(second, makeAction(recorder, 2));
    ASSERT_FALSE(capacity.has_value());
    EXPECT_EQ(capacity.error().code, UI::UIErrorCode::CapacityExceeded);

    assertOk(updater.setButtonAction(first, makeAction(recorder, 3)));
    route(*context, makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    route(*context, makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
    ASSERT_EQ(recorder.size, 1U);
    EXPECT_EQ(recorder.entries[0].marker, 3);

    assertOk(updater.clearButtonAction(first));
    assertOk(updater.setButtonAction(second, makeAction(recorder, 4)));
    route(
        *context,
        makePointerInput(
            firstWindow,
            UI::UIRoutedPointerEventKind::ButtonDown,
            3,
            {.x = 10.0F, .y = 50.0F}));
    route(
        *context,
        makePointerInput(
            firstWindow,
            UI::UIRoutedPointerEventKind::ButtonUp,
            4,
            {.x = 10.0F, .y = 50.0F}));
    ASSERT_EQ(recorder.size, 2U);
    EXPECT_EQ(recorder.entries[1].button, second);
    EXPECT_EQ(recorder.entries[1].marker, 4);
}

TEST_F(UIButtonActionTest, ThreeHundredRepeatedClicksDoNotGrowSuppliedPmr)
{
    ObservingMemoryResource resource;
    {
        ButtonTree tree = createButtonTree(
            firstWindow,
            {
                .nodeCapacity = 4,
                .rootCapacity = 1,
                .routePathCapacity = 4,
                .routedPointerListenerCapacity = 1,
                .buttonActionCapacity = 1,
            },
            resource);
        ASSERT_NE(tree.context, nullptr);
        ActionRecorder recorder;
        assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));

        const usize allocationCount = resource.allocationCount();
        for (u64 click = 0; click < 300; ++click) {
            const u64 downSequence = click * 2 + 1;
            const u64 upSequence = downSequence + 1;
            const UI::UIPointerRouteResult down = route(
                *tree.context,
                makePointerInput(
                    firstWindow,
                    UI::UIRoutedPointerEventKind::ButtonDown,
                    downSequence));
            ASSERT_TRUE(down.consumed) << "click=" << click;
            const UI::UIPointerRouteResult up = route(
                *tree.context,
                makePointerInput(
                    firstWindow,
                    UI::UIRoutedPointerEventKind::ButtonUp,
                    upSequence));
            ASSERT_TRUE(up.consumed) << "click=" << click;
            ASSERT_EQ(recorder.size, click + 1U);
        }

        EXPECT_EQ(resource.allocationCount(), allocationCount);
        EXPECT_GT(resource.currentBytes(), 0U);
    }
    EXPECT_EQ(resource.currentBytes(), 0U);
    EXPECT_EQ(resource.allocationCount(), resource.deallocationCount());
}

TEST_F(UIButtonActionTest, KeyboardAndGamepadAcceptUpAreReleaseBarriersWhenDirtyQueueIsFull)
{
    ButtonTree tree = createButtonTree(
        firstWindow,
        {
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 3,
            .routePathCapacity = 8,
            .routedPointerListenerCapacity = 8,
            .buttonActionCapacity = 1,
        });
    ASSERT_NE(tree.context, nullptr);
    const UI::UINodeId blocker =
        createPanel(*tree.context, tree.root.rootNodeId());
    ASSERT_TRUE(blocker.hasValue());
    ActionRecorder recorder;
    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 9)));

    auto focus = tree.context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    ASSERT_EQ(focus->focus, tree.button);
    assertOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const Platform::DigitalControlIdentity enter =
        Platform::KeyControlIdentity{firstWindow, Platform::Key::Enter};
    auto down = tree.context->routeDefaultActionActivate(
        Platform::PlatformFrameId{1},
        10,
        UI::UIButtonActivationSource::Keyboard,
        enter);
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_TRUE(down->activated);
    expectButtonPressed(tree.updater, tree.button, true);
    EXPECT_EQ(recorder.size, 1U);

    // Publish the pressed state before filling every remaining dirty slot.
    assertOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const UI::UIBoxPaint blockerPaint{
        .solidFill = UI::UISolidFill{
            .color = {.red = 1, .green = 2, .blue = 3, .alpha = 255},
        },
    };
    assertOk(tree.updater.setBoxPaint(tree.root.rootNodeId(), blockerPaint));
    assertOk(tree.updater.setBoxPaint(tree.panel, blockerPaint));
    assertOk(tree.updater.setBoxPaint(blocker, blockerPaint));
    ASSERT_EQ(tree.context->statistics().dirtyQueuePendingCount, 3U);

    auto up = tree.context->routeDefaultActionRelease(
        Platform::PlatformFrameId{2},
        20,
        UI::UIButtonActivationSource::Keyboard,
        enter);
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_TRUE(up->consumed);
    EXPECT_FALSE(up->activated);
    // Up is a release barrier: failure to repaint cannot resurrect the
    // logical pressed state or trigger another action.
    expectButtonPressed(tree.updater, tree.button, false);
    EXPECT_EQ(recorder.size, 1U);

    assertOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    EXPECT_EQ(tree.context->defaultActionFocus(), tree.button);
    auto idleUp = tree.context->routeDefaultActionRelease(
        Platform::PlatformFrameId{3},
        30,
        UI::UIButtonActivationSource::Keyboard,
        enter);
    ASSERT_TRUE(idleUp.has_value()) << (idleUp ? "" : idleUp.error().message);
    EXPECT_FALSE(idleUp->consumed);
    EXPECT_EQ(recorder.size, 1U);

    auto gamepadsResult = GamepadPool::Create(1);
    ASSERT_TRUE(gamepadsResult.has_value());
    auto gamepads = std::make_unique<GamepadPool>(std::move(*gamepadsResult));
    auto gamepadResult = gamepads->tryEmplace(1);
    ASSERT_TRUE(gamepadResult.has_value());
    const Platform::DigitalControlIdentity south =
        Platform::GamepadButtonControlIdentity{
            .routedWindow = firstWindow,
            .gamepad = *gamepadResult,
            .button = Platform::GamepadButton::South,
        };
    auto gamepadDown = tree.context->routeDefaultActionActivate(
        Platform::PlatformFrameId{4},
        40,
        UI::UIButtonActivationSource::Gamepad,
        south);
    ASSERT_TRUE(gamepadDown.has_value())
        << (gamepadDown ? "" : gamepadDown.error().message);
    EXPECT_TRUE(gamepadDown->consumed);
    EXPECT_TRUE(gamepadDown->activated);
    expectButtonPressed(tree.updater, tree.button, true);
    EXPECT_EQ(recorder.size, 2U);
    assertOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const UI::UIBoxPaint secondBlockerPaint{
        .solidFill = UI::UISolidFill{
            .color = {.red = 4, .green = 5, .blue = 6, .alpha = 255},
        },
    };
    assertOk(tree.updater.setBoxPaint(
        tree.root.rootNodeId(),
        secondBlockerPaint));
    assertOk(tree.updater.setBoxPaint(tree.panel, secondBlockerPaint));
    assertOk(tree.updater.setBoxPaint(blocker, secondBlockerPaint));
    ASSERT_EQ(tree.context->statistics().dirtyQueuePendingCount, 3U);

    auto gamepadUp = tree.context->routeDefaultActionRelease(
        Platform::PlatformFrameId{5},
        50,
        UI::UIButtonActivationSource::Gamepad,
        south);
    ASSERT_TRUE(gamepadUp.has_value())
        << (gamepadUp ? "" : gamepadUp.error().message);
    EXPECT_TRUE(gamepadUp->consumed);
    EXPECT_FALSE(gamepadUp->activated);
    expectButtonPressed(tree.updater, tree.button, false);
    EXPECT_EQ(recorder.size, 2U);

    assertOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    auto idleGamepadUp = tree.context->routeDefaultActionRelease(
        Platform::PlatformFrameId{6},
        60,
        UI::UIButtonActivationSource::Gamepad,
        south);
    ASSERT_TRUE(idleGamepadUp.has_value())
        << (idleGamepadUp ? "" : idleGamepadUp.error().message);
    EXPECT_FALSE(idleGamepadUp->consumed);
    EXPECT_EQ(recorder.size, 2U);
}

TEST_F(UIButtonActionTest, KeyboardAcceptDownDirtyFailurePrecedesCallbackAndPress)
{
    ButtonTree tree = createButtonTree(
        firstWindow,
        {
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 3,
            .routePathCapacity = 8,
            .routedPointerListenerCapacity = 8,
            .buttonActionCapacity = 1,
        });
    ASSERT_NE(tree.context, nullptr);
    ActionRecorder recorder;
    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));
    const UI::UINodeId blocker =
        createPanel(*tree.context, tree.root.rootNodeId());
    ASSERT_TRUE(blocker.hasValue());
    assertOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    auto focus = tree.context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    ASSERT_EQ(focus->focus, tree.button);
    assertOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const UI::UIBoxPaint blockerPaint{
        .solidFill = UI::UISolidFill{
            .color = {.red = 1, .green = 2, .blue = 3, .alpha = 255},
        },
    };
    assertOk(tree.updater.setBoxPaint(tree.root.rootNodeId(), blockerPaint));
    assertOk(tree.updater.setBoxPaint(tree.panel, blockerPaint));
    assertOk(tree.updater.setBoxPaint(blocker, blockerPaint));
    ASSERT_EQ(tree.context->statistics().dirtyQueuePendingCount, 3U);

    const Platform::DigitalControlIdentity enter =
        Platform::KeyControlIdentity{firstWindow, Platform::Key::Enter};
    auto rejected = tree.context->routeDefaultActionActivate(
        Platform::PlatformFrameId{3}, 30, UI::UIButtonActivationSource::Keyboard, enter);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    expectButtonPressed(tree.updater, tree.button, false);
    EXPECT_EQ(recorder.size, 0U);
}

TEST_F(UIButtonActionTest, FocusStepDirtyQueueFailurePreservesPreviousFocus)
{
    auto context = createContext(
        firstWindow,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 4,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 2,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UINodeId first = createButton(*context, root.rootNodeId());
    const UI::UINodeId second = createButton(*context, root.rootNodeId());
    const UI::UINodeId blocker = createPanel(*context, root.rootNodeId());

    assertOk(updater.setLayoutStyle(
        root.rootNodeId(),
        fixedSize(200.0F, 80.0F)));
    assertOk(updater.setLayoutStyle(first, fixedSize(40.0F, 20.0F)));
    assertOk(context->commitLayout({.width = 200.0F, .height = 80.0F}));
    assertOk(updater.setLayoutStyle(second, fixedSize(40.0F, 20.0F)));
    assertOk(context->commitLayout({.width = 200.0F, .height = 80.0F}));

    auto initialFocus = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(initialFocus.has_value())
        << (initialFocus ? "" : initialFocus.error().message);
    ASSERT_EQ(initialFocus->focus, first);
    EXPECT_EQ(context->defaultActionFocus(), first);
    EXPECT_FALSE(context->imeFocus().hasValue());
    assertOk(context->commitLayout({.width = 200.0F, .height = 80.0F}));

    assertOk(updater.setPointerHitPolicy(
        blocker,
        UI::UIPointerHitPolicy::Targetable));
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 1U);

    auto rejected = context->routeDefaultActionFocusStep(false);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->defaultActionFocus(), first);
    EXPECT_FALSE(context->imeFocus().hasValue());
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 1U);

    assertOk(context->commitLayout({.width = 200.0F, .height = 80.0F}));
    auto retried = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(retried.has_value()) << (retried ? "" : retried.error().message);
    EXPECT_TRUE(retried->consumed);
    EXPECT_TRUE(retried->moved);
    EXPECT_EQ(retried->focus, second);
    EXPECT_EQ(context->defaultActionFocus(), second);
}

} // namespace
} // namespace Tina::Tests
