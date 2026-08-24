#include "UIButtonActionTestSupport.hpp"

namespace Tina::Tests {
namespace {

TEST_F(UIButtonActionTest, ReentrantDefaultActionActivationIsRejectedAndGuardSurvives)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);

    struct State final {
        UI::UIContext* context = nullptr;
        Core::ErrorCode nestedError{};
        Core::ErrorCode commitError{};
        usize callbackCount = 0;
        bool nestedRejected = false;
    } state{
        .context = tree.context.get(),
    };

    assertOk(tree.updater.setButtonAction(
        tree.button,
        UI::UIButtonActionCallback{[&state](const UI::UIButtonActionEvent&) noexcept {
            ++state.callbackCount;
            if (state.callbackCount != 1U) {
                return;
            }
            auto nested = state.context->input().routeDefaultActionActivate(
                Platform::PlatformFrameId{2},
                20,
                UI::UIButtonActivationSource::Keyboard);
            state.nestedRejected = !nested.has_value();
            if (!nested) {
                state.nestedError = nested.error().code;
            }
            const Core::Status commit = state.context->publication().commitLayout(
                {.width = 100.0F, .height = 100.0F});
            if (!commit) {
                state.commitError = commit.error().code;
            }
        }}));

    auto focus = tree.context->input().routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    ASSERT_EQ(focus->focus, tree.button);
    ASSERT_EQ(tree.context->input().defaultActionFocus(), tree.button);
    assertOk(tree.context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    assertOk(tree.updater.setBoxPaint(
        tree.button,
        UI::UIBoxPaint{
            .solidFill = UI::UISolidFill{
                .color = {.red = 68, .green = 85, .blue = 102, .alpha = 255},
            },
        }));
    const u64 paintRevisionBefore = tree.context->statistics().paintRevision;

    auto outer = tree.context->input().routeDefaultActionActivate(
        Platform::PlatformFrameId{1},
        10,
        UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(outer.has_value()) << (outer ? "" : outer.error().message);
    EXPECT_TRUE(outer->consumed);
    EXPECT_TRUE(outer->activated);
    EXPECT_TRUE(state.nestedRejected);
    EXPECT_EQ(state.nestedError, UI::UIErrorCode::PointerRouteAlreadyInProgress);
    EXPECT_EQ(state.commitError, UI::UIErrorCode::PointerRouteAlreadyInProgress);
    EXPECT_EQ(state.callbackCount, 1U);
    EXPECT_EQ(tree.context->input().defaultActionFocus(), tree.button);
    EXPECT_FALSE(buttonPressed(tree.updater, tree.button));
    EXPECT_EQ(tree.context->statistics().paintRevision, paintRevisionBefore);
}

TEST_F(UIButtonActionTest, KeyboardAndGamepadAcceptActivateDefaultFocusedButton)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);
    ActionRecorder recorder;
    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 7)));

    // Without pointer arm, Accept does nothing and is not consumed.
    auto idle = tree.context->input().routeDefaultActionActivate(
        Platform::PlatformFrameId{1},
        10,
        UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(idle.has_value()) << (idle ? "" : idle.error().message);
    EXPECT_FALSE(idle->consumed);
    EXPECT_FALSE(idle->activated);
    EXPECT_EQ(recorder.size, 0U);

    // Pointer down sets default-action focus (and arms).
    const UI::UIPointerRouteResult down = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    ASSERT_TRUE(down.consumed);
    expectButtonPressed(tree.updater, tree.button, true);

    // Keyboard Accept activates once and does not require pointer Up.
    auto keyboard = tree.context->input().routeDefaultActionActivate(
        Platform::PlatformFrameId{2},
        20,
        UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(keyboard.has_value()) << (keyboard ? "" : keyboard.error().message);
    EXPECT_TRUE(keyboard->consumed);
    EXPECT_TRUE(keyboard->activated);
    ASSERT_EQ(recorder.size, 1U);
    EXPECT_EQ(recorder.entries[0].source, UI::UIButtonActivationSource::Keyboard);
    EXPECT_EQ(recorder.entries[0].sourceSequence, 20U);
    EXPECT_EQ(recorder.entries[0].marker, 7);

    auto gamepad = tree.context->input().routeDefaultActionActivate(
        Platform::PlatformFrameId{3},
        30,
        UI::UIButtonActivationSource::Gamepad);
    ASSERT_TRUE(gamepad.has_value()) << (gamepad ? "" : gamepad.error().message);
    EXPECT_TRUE(gamepad->consumed);
    EXPECT_TRUE(gamepad->activated);
    ASSERT_EQ(recorder.size, 2U);
    EXPECT_EQ(recorder.entries[1].source, UI::UIButtonActivationSource::Gamepad);
    EXPECT_EQ(recorder.entries[1].sourceSequence, 30U);
}

TEST_F(UIButtonActionTest, DisablingUnrelatedButtonPreservesDefaultActionPress)
{
    auto context = createContext(
        firstWindow,
        UI::UIContextCapacityConfig{.nodeCapacity = 8, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UINodeId firstPanel = createPanel(*context, root.rootNodeId());
    const UI::UINodeId firstButton = createButton(*context, firstPanel);
    const UI::UINodeId unrelatedButton = createButton(*context, root.rootNodeId());

    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(firstPanel, fixedSize(50.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(firstButton, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(unrelatedButton, fixedSize(50.0F, 40.0F)));
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 40.0F}));

    auto focus = context->input().routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    ASSERT_EQ(focus->focus, firstButton);

    const Platform::DigitalControlIdentity enter =
        Platform::KeyControlIdentity{firstWindow, Platform::Key::Enter};
    auto down = context->input().routeDefaultActionActivate(
        Platform::PlatformFrameId{1},
        10,
        UI::UIButtonActivationSource::Keyboard,
        enter);
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    expectButtonPressed(updater, firstButton, true);

    assertOk(updater.setEnabled(unrelatedButton, false));
    EXPECT_EQ(context->input().defaultActionFocus(), firstButton);
    expectButtonPressed(updater, firstButton, true);

    auto up = context->input().routeDefaultActionRelease(
        Platform::PlatformFrameId{2},
        20,
        UI::UIButtonActivationSource::Keyboard,
        enter);
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_TRUE(up->consumed);
    expectButtonPressed(updater, firstButton, false);
}

TEST_F(UIButtonActionTest, DisablingPressedButtonClearsPressAndQueuesDirtyState)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);

    auto focus = tree.context->input().routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    ASSERT_EQ(focus->focus, tree.button);
    assertOk(tree.context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    const Platform::DigitalControlIdentity enter =
        Platform::KeyControlIdentity{firstWindow, Platform::Key::Enter};
    auto down = tree.context->input().routeDefaultActionActivate(
        Platform::PlatformFrameId{1},
        10,
        UI::UIButtonActivationSource::Keyboard,
        enter);
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    expectButtonPressed(tree.updater, tree.button, true);
    assertOk(tree.context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    EXPECT_EQ(tree.context->statistics().dirtyQueuePendingCount, 0U);

    assertOk(tree.updater.setEnabled(tree.button, false));
    EXPECT_FALSE(tree.context->input().defaultActionFocus().hasValue());
    expectButtonPressed(tree.updater, tree.button, false);
    EXPECT_EQ(tree.context->statistics().dirtyQueuePendingCount, 1U);
}

TEST_F(UIButtonActionTest, KeyboardAcceptPressTracksEachKeyUntilItsMatchingUp)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);
    ActionRecorder recorder;
    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));

    auto focus = tree.context->input().routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    ASSERT_EQ(focus->focus, tree.button);

    const Platform::DigitalControlIdentity enter =
        Platform::KeyControlIdentity{firstWindow, Platform::Key::Enter};
    const Platform::DigitalControlIdentity space =
        Platform::KeyControlIdentity{firstWindow, Platform::Key::Space};
    auto enterDown = tree.context->input().routeDefaultActionActivate(
        Platform::PlatformFrameId{3}, 30, UI::UIButtonActivationSource::Keyboard, enter);
    ASSERT_TRUE(enterDown.has_value()) << (enterDown ? "" : enterDown.error().message);
    EXPECT_TRUE(enterDown->consumed);
    EXPECT_TRUE(enterDown->activated);
    expectButtonPressed(tree.updater, tree.button, true);

    auto spaceDown = tree.context->input().routeDefaultActionActivate(
        Platform::PlatformFrameId{4}, 40, UI::UIButtonActivationSource::Keyboard, space);
    ASSERT_TRUE(spaceDown.has_value()) << (spaceDown ? "" : spaceDown.error().message);
    EXPECT_TRUE(spaceDown->consumed);
    expectButtonPressed(tree.updater, tree.button, true);

    auto enterUp = tree.context->input().routeDefaultActionRelease(
        Platform::PlatformFrameId{5}, 50, UI::UIButtonActivationSource::Keyboard, enter);
    ASSERT_TRUE(enterUp.has_value()) << (enterUp ? "" : enterUp.error().message);
    EXPECT_TRUE(enterUp->consumed);
    expectButtonPressed(tree.updater, tree.button, true);

    auto spaceUp = tree.context->input().routeDefaultActionRelease(
        Platform::PlatformFrameId{6}, 60, UI::UIButtonActivationSource::Keyboard, space);
    ASSERT_TRUE(spaceUp.has_value()) << (spaceUp ? "" : spaceUp.error().message);
    EXPECT_TRUE(spaceUp->consumed);
    expectButtonPressed(tree.updater, tree.button, false);
    EXPECT_EQ(recorder.size, 2U);
}

TEST_F(UIButtonActionTest, DefaultActionWithoutRegisteredCallbackConsumesButDoesNotActivate)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);

    const UI::UIPointerRouteResult down = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    ASSERT_TRUE(down.consumed);

    auto result = tree.context->input().routeDefaultActionActivate(
        Platform::PlatformFrameId{4},
        40,
        UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    EXPECT_TRUE(result->consumed);
    EXPECT_FALSE(result->activated);
}

TEST_F(UIButtonActionTest, TabCyclesDefaultActionFocusAmongButtons)
{
    auto context = createContext(firstWindow);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 40.0F)));

    const UI::UINodeId first = createButton(*context, root.rootNodeId());
    const UI::UINodeId second = createButton(*context, root.rootNodeId());
    const UI::UINodeId third = createButton(*context, root.rootNodeId());
    assertOk(updater.setLayoutStyle(first, fixedSize(40.0F, 20.0F)));
    assertOk(updater.setLayoutStyle(second, fixedSize(40.0F, 20.0F)));
    assertOk(updater.setLayoutStyle(third, fixedSize(40.0F, 20.0F)));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 40.0F}));

    EXPECT_FALSE(context->input().defaultActionFocus().hasValue());

    auto step1 = context->input().routeDefaultActionFocusStep(false);
    ASSERT_TRUE(step1.has_value()) << (step1 ? "" : step1.error().message);
    EXPECT_TRUE(step1->consumed);
    EXPECT_TRUE(step1->moved);
    EXPECT_EQ(step1->focus, first);
    EXPECT_EQ(context->input().defaultActionFocus(), first);

    auto step2 = context->input().routeDefaultActionFocusStep(false);
    ASSERT_TRUE(step2.has_value());
    EXPECT_TRUE(step2->consumed);
    EXPECT_EQ(step2->focus, second);

    auto step3 = context->input().routeDefaultActionFocusStep(false);
    ASSERT_TRUE(step3.has_value());
    EXPECT_EQ(step3->focus, third);

    auto wrap = context->input().routeDefaultActionFocusStep(false);
    ASSERT_TRUE(wrap.has_value());
    EXPECT_EQ(wrap->focus, first);

    auto reverse = context->input().routeDefaultActionFocusStep(true);
    ASSERT_TRUE(reverse.has_value());
    EXPECT_EQ(reverse->focus, third);
}


} // namespace
} // namespace Tina::Tests
