#include "UIButtonActionTestSupport.hpp"

namespace Tina::Tests {
namespace {

TEST_F(UIButtonActionTest, ElementRecipesPublishExpectedDefaultHitPolicies)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 5, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
    const UI::UINodeId label = createLabel(*context, panel);
    const UI::UINodeId button = createButton(*context, panel);
    auto textEditResult = context->rootBuilder().createElement(panel, UI::makeTextEditElement());
    ASSERT_TRUE(textEditResult.has_value());
    const UI::UINodeId textEdit = *textEditResult;
    auto updater = createUpdater(*context, root);

    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(panel, fixedSize(80.0F, 80.0F)));
    assertOk(updater.setLayoutStyle(label, fixedSize(20.0F, 10.0F)));
    assertOk(updater.setLayoutStyle(button, fixedSize(30.0F, 10.0F)));
    assertOk(updater.setLayoutStyle(textEdit, fixedSize(40.0F, 10.0F)));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const UI::UICommittedHitView hit = context->committedHit();
    ASSERT_EQ(hit.size(), 5U);
    EXPECT_EQ(hit.entries()[0].node, root.rootNodeId());
    EXPECT_EQ(hit.entries()[0].policy, UI::UIPointerHitPolicy::Ignore);
    EXPECT_EQ(hit.entries()[1].node, panel);
    EXPECT_EQ(hit.entries()[1].policy, UI::UIPointerHitPolicy::Ignore);
    EXPECT_EQ(hit.entries()[2].node, label);
    EXPECT_EQ(hit.entries()[2].policy, UI::UIPointerHitPolicy::Ignore);
    EXPECT_EQ(hit.entries()[3].node, button);
    EXPECT_EQ(hit.entries()[3].policy, UI::UIPointerHitPolicy::Targetable);
    EXPECT_EQ(hit.entries()[4].node, textEdit);
    EXPECT_EQ(hit.entries()[4].policy, UI::UIPointerHitPolicy::Targetable);
    EXPECT_EQ(context->statistics().committedHitTargetCount, 2U);
}

TEST_F(UIButtonActionTest, PrimaryPointerDownMoveUpPressedAndActivatesOnce)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);
    ActionRecorder recorder;
    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));

    const UI::UIPointerRouteResult down = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    EXPECT_EQ(down.pointQuery.target.node, tree.button);
    EXPECT_TRUE(down.consumed);
    EXPECT_TRUE(claimsPrimaryButton(down));
    expectButtonPressed(tree.updater, tree.button, true);
    EXPECT_EQ(recorder.size, 0U);

    const UI::UIPointerRouteResult move = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::Move, 2));
    EXPECT_EQ(move.pointQuery.target.node, tree.button);
    EXPECT_FALSE(move.consumed);
    EXPECT_TRUE(claimsPrimaryButton(move));
    expectButtonPressed(tree.updater, tree.button, true);

    const UI::UIPointerRouteResult up = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 3));
    EXPECT_EQ(up.pointQuery.target.node, tree.button);
    EXPECT_TRUE(up.consumed);
    EXPECT_FALSE(claimsPrimaryButton(up));
    expectButtonPressed(tree.updater, tree.button, false);
    ASSERT_EQ(recorder.size, 1U);
    EXPECT_EQ(recorder.entries[0].button, tree.button);
    EXPECT_EQ(recorder.entries[0].source, UI::UIButtonActivationSource::PrimaryPointer);
    EXPECT_EQ(recorder.entries[0].platformFrame, Platform::PlatformFrameId{3});
    EXPECT_EQ(recorder.entries[0].sourceSequence, 3U);
    EXPECT_EQ(recorder.entries[0].marker, 1);

    const UI::UIPointerRouteResult extraUp = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 4));
    EXPECT_FALSE(extraUp.consumed);
    EXPECT_EQ(recorder.size, 1U);
}

TEST_F(UIButtonActionTest, PrimaryUpOutsideClearsPressedWithoutActivation)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);
    ActionRecorder recorder;
    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));

    const UI::UIPointerRouteResult down = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    ASSERT_TRUE(down.consumed);
    expectButtonPressed(tree.updater, tree.button, true);

    const UI::UIPointerRouteResult moveOutside = route(
        *tree.context,
        makePointerInput(
            firstWindow,
            UI::UIRoutedPointerEventKind::Move,
            2,
            {.x = 90.0F, .y = 90.0F}));
    EXPECT_FALSE(moveOutside.pointQuery.hasTarget());
    EXPECT_FALSE(moveOutside.consumed);
    EXPECT_TRUE(claimsPrimaryButton(moveOutside));
    expectButtonPressed(tree.updater, tree.button, false);

    const UI::UIPointerRouteResult upOutside = route(
        *tree.context,
        makePointerInput(
            firstWindow,
            UI::UIRoutedPointerEventKind::ButtonUp,
            3,
            {.x = 90.0F, .y = 90.0F}));
    EXPECT_FALSE(upOutside.pointQuery.hasTarget());
    EXPECT_TRUE(upOutside.consumed);
    EXPECT_FALSE(claimsPrimaryButton(upOutside));
    expectButtonPressed(tree.updater, tree.button, false);
    EXPECT_EQ(recorder.size, 0U);
}

TEST_F(UIButtonActionTest, TargetableChildActivatesNearestButtonAncestor)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId button = createButton(*context, root.rootNodeId());
    const UI::UINodeId child = createPanel(*context, button);
    auto updater = createUpdater(*context, root);
    ActionRecorder recorder;

    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(button, fixedSize(60.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(child, fixedSize(20.0F, 20.0F)));
    assertOk(updater.setPointerHitPolicy(child, UI::UIPointerHitPolicy::Targetable));
    assertOk(updater.setButtonAction(button, makeAction(recorder, 1)));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const UI::UIPointerRouteResult down = route(
        *context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    EXPECT_EQ(down.pointQuery.target.node, child);
    EXPECT_EQ(down.routeDepth, 3U);
    expectButtonPressed(updater, button, true);

    const UI::UIPointerRouteResult up = route(
        *context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
    EXPECT_EQ(up.pointQuery.target.node, child);
    expectButtonPressed(updater, button, false);
    ASSERT_EQ(recorder.size, 1U);
    EXPECT_EQ(recorder.entries[0].button, button);
}

TEST_F(
    UIButtonActionTest,
    PropagationConsumptionAndClaimsRemainIndependentFromDefaultAction)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);
    ActionRecorder recorder;
    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));

    auto token = addListener(
        *tree.context,
        {.node = tree.button,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept {
            event.stopPropagation();
            event.consumeInputTransition();
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Secondary));
        }});
    ASSERT_TRUE(token);

    const UI::UIPointerRouteResult down = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    EXPECT_TRUE(down.stopped);
    EXPECT_TRUE(down.consumed);
    EXPECT_TRUE(claimsPrimaryButton(down));
    EXPECT_TRUE(down.claimedPointerButtons.test(
        static_cast<usize>(Platform::PointerButton::Secondary)));
    expectButtonPressed(tree.updater, tree.button, true);

    const UI::UIPointerRouteResult up = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
    EXPECT_TRUE(up.consumed);
    expectButtonPressed(tree.updater, tree.button, false);
    ASSERT_EQ(recorder.size, 1U);
}

TEST_F(UIButtonActionTest, PreventDefaultBlocksArmAndUpActivationOnly)
{
    {
        ButtonTree tree = createButtonTree(firstWindow);
        ASSERT_NE(tree.context, nullptr);
        ActionRecorder recorder;
        assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));
        auto token = addListener(
            *tree.context,
            {.node = tree.button,
             .kind = UI::UIRoutedPointerEventKind::ButtonDown,
             .phases = UI::UIEventPhaseMask::Target},
            UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept {
                EXPECT_FALSE(event.isDefaultActionPrevented());
                event.preventDefaultAction();
                EXPECT_TRUE(event.isDefaultActionPrevented());
            }});
        ASSERT_TRUE(token);

        const UI::UIPointerRouteResult down = route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
        EXPECT_FALSE(down.consumed);
        EXPECT_FALSE(claimsPrimaryButton(down));
        expectButtonPressed(tree.updater, tree.button, false);
        const UI::UIPointerRouteResult up = route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
        EXPECT_FALSE(up.consumed);
        EXPECT_EQ(recorder.size, 0U);
    }

    {
        ButtonTree tree = createButtonTree(firstWindow);
        ASSERT_NE(tree.context, nullptr);
        ActionRecorder recorder;
        assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));
        auto token = addListener(
            *tree.context,
            {.node = tree.button,
             .kind = UI::UIRoutedPointerEventKind::ButtonUp,
             .phases = UI::UIEventPhaseMask::Target},
            UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept {
                event.preventDefaultAction();
            }});
        ASSERT_TRUE(token);

        const UI::UIPointerRouteResult down = route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
        ASSERT_TRUE(down.consumed);
        expectButtonPressed(tree.updater, tree.button, true);
        const UI::UIPointerRouteResult up = route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
        EXPECT_TRUE(up.consumed);
        expectButtonPressed(tree.updater, tree.button, false);
        EXPECT_EQ(recorder.size, 0U);
    }
}

TEST_F(UIButtonActionTest, CancelOrResetClearsPressedWithoutActivation)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);
    ActionRecorder recorder;
    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));

    route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    expectButtonPressed(tree.updater, tree.button, true);
    assertOk(tree.context->cancelPointerInteraction(firstWindow));
    expectButtonPressed(tree.updater, tree.button, false);

    const UI::UIPointerRouteResult up = route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
    EXPECT_FALSE(up.consumed);
    EXPECT_EQ(recorder.size, 0U);

    route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 3));
    expectButtonPressed(tree.updater, tree.button, true);
    assertOk(tree.context->cancelPointerInteraction(firstWindow));
    expectButtonPressed(tree.updater, tree.button, false);
    EXPECT_EQ(recorder.size, 0U);
}


} // namespace
} // namespace Tina::Tests
