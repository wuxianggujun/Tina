#include "UIButtonActionTestSupport.hpp"

namespace Tina::Tests {
namespace {

TEST_F(UIButtonActionTest, SetReplaceClearActions)
{
    ButtonTree tree = createButtonTree(firstWindow);
    ASSERT_NE(tree.context, nullptr);
    ActionRecorder recorder;

    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 1)));
    route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
    ASSERT_EQ(recorder.size, 1U);
    EXPECT_EQ(recorder.entries[0].marker, 1);

    assertOk(tree.updater.setButtonAction(tree.button, makeAction(recorder, 2)));
    route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 3));
    route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 4));
    ASSERT_EQ(recorder.size, 2U);
    EXPECT_EQ(recorder.entries[1].marker, 2);

    assertOk(tree.updater.clearButtonAction(tree.button));
    route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 5));
    route(
        *tree.context,
        makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 6));
    EXPECT_EQ(recorder.size, 2U);
    expectButtonPressed(tree.updater, tree.button, false);
}

TEST_F(UIButtonActionTest, RejectsWrongKindRootContextAndStaleButton)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 8, .rootCapacity = 2});
    auto sameWindowContext = createContext(firstWindow, {.nodeCapacity = 4, .rootCapacity = 1});
    auto otherWindowContext = createContext(secondWindow, {.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    ASSERT_NE(sameWindowContext, nullptr);
    ASSERT_NE(otherWindowContext, nullptr);

    auto firstRoot = createRoot(*context);
    auto secondRoot = createRoot(*context);
    auto foreignRoot = createRoot(*sameWindowContext);
    auto otherWindowRoot = createRoot(*otherWindowContext);
    ASSERT_TRUE(firstRoot);
    ASSERT_TRUE(secondRoot);
    ASSERT_TRUE(foreignRoot);
    ASSERT_TRUE(otherWindowRoot);
    const UI::UINodeId panel = createPanel(*context, firstRoot.rootNodeId());
    const UI::UINodeId label = createLabel(*context, panel);
    const UI::UINodeId button = createButton(*context, firstRoot.rootNodeId());
    const UI::UINodeId otherRootButton = createButton(*context, secondRoot.rootNodeId());
    const UI::UINodeId foreignButton =
        createButton(*sameWindowContext, foreignRoot.rootNodeId());
    const UI::UINodeId otherWindowButton =
        createButton(*otherWindowContext, otherWindowRoot.rootNodeId());
    const UI::UINodeId staleButton = createButton(*context, firstRoot.rootNodeId());
    auto updater = createUpdater(*context, firstRoot);
    ActionRecorder recorder;

    const auto expectInvalidButtonAction = [&](Core::Status status) {
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, UI::UIErrorCode::InvalidButtonAction);
    };
    expectInvalidButtonAction(
        updater.setButtonAction(firstRoot.rootNodeId(), makeAction(recorder, 1)));
    expectInvalidButtonAction(updater.setButtonAction(panel, makeAction(recorder, 1)));
    expectInvalidButtonAction(updater.setButtonAction(label, makeAction(recorder, 1)));
    expectInvalidButtonAction(updater.clearButtonAction(panel));
    auto panelPressed = updater.isButtonPressed(panel);
    ASSERT_FALSE(panelPressed.has_value());
    EXPECT_EQ(panelPressed.error().code, UI::UIErrorCode::InvalidButtonAction);

    const Core::Status wrongRoot =
        updater.setButtonAction(otherRootButton, makeAction(recorder, 1));
    ASSERT_FALSE(wrongRoot.has_value());
    EXPECT_EQ(wrongRoot.error().code, UI::UIErrorCode::InvalidNode);

    const Core::Status wrongContext =
        updater.setButtonAction(foreignButton, makeAction(recorder, 1));
    ASSERT_FALSE(wrongContext.has_value());
    EXPECT_EQ(wrongContext.error().code, UI::UIErrorCode::WrongContext);

    const Core::Status wrongWindow =
        updater.setButtonAction(otherWindowButton, makeAction(recorder, 1));
    ASSERT_FALSE(wrongWindow.has_value());
    EXPECT_EQ(wrongWindow.error().code, UI::UIErrorCode::WrongOwnerWindow);

    assertOk(updater.destroy(staleButton));
    const Core::Status stale =
        updater.setButtonAction(staleButton, makeAction(recorder, 1));
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().code, UI::UIErrorCode::InvalidNode);

    assertOk(updater.setButtonAction(button, makeAction(recorder, 2)));
    expectButtonPressed(updater, button, false);
}

TEST_F(UIButtonActionTest, RouteMutationReplaceTakesNextRouteAndClearBlocksCurrentAction)
{
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
            UI::UIRoutedPointerCallback{
                [&tree, &recorder](UI::UIRoutedPointerEvent&) noexcept {
                    expectOk(tree.updater.setButtonAction(
                        tree.button,
                        makeAction(recorder, 2)));
                }});
        ASSERT_TRUE(token);

        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
        ASSERT_EQ(recorder.size, 1U);
        EXPECT_EQ(recorder.entries[0].marker, 1);

        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 3));
        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 4));
        ASSERT_EQ(recorder.size, 2U);
        EXPECT_EQ(recorder.entries[1].marker, 2);
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
            UI::UIRoutedPointerCallback{[&tree](UI::UIRoutedPointerEvent&) noexcept {
                expectOk(tree.updater.clearButtonAction(tree.button));
            }});
        ASSERT_TRUE(token);

        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
        EXPECT_EQ(recorder.size, 0U);
        expectButtonPressed(tree.updater, tree.button, false);
    }
}

TEST_F(UIButtonActionTest, ActionCallbackMayDestroyButtonOrRoot)
{
    {
        ButtonTree tree = createButtonTree(firstWindow);
        ASSERT_NE(tree.context, nullptr);
        usize callbackCount = 0;
        assertOk(tree.updater.setButtonAction(
            tree.button,
            UI::UIButtonActionCallback{[&tree, &callbackCount](
                                           const UI::UIButtonActionEvent&) noexcept {
                ++callbackCount;
                expectOk(tree.updater.destroy(tree.button));
            }}));

        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
        EXPECT_EQ(callbackCount, 1U);
        EXPECT_FALSE(tree.context->contains(tree.button));
        EXPECT_EQ(tree.context->liveNodeCount(), 2U);
    }

    {
        ButtonTree tree = createButtonTree(firstWindow);
        ASSERT_NE(tree.context, nullptr);
        usize callbackCount = 0;
        assertOk(tree.updater.setButtonAction(
            tree.button,
            UI::UIButtonActionCallback{[&tree, &callbackCount](
                                           const UI::UIButtonActionEvent&) noexcept {
                ++callbackCount;
                tree.root.reset();
            }}));

        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonDown, 1));
        route(
            *tree.context,
            makePointerInput(firstWindow, UI::UIRoutedPointerEventKind::ButtonUp, 2));
        EXPECT_EQ(callbackCount, 1U);
        EXPECT_EQ(tree.context->liveRootCount(), 0U);
        EXPECT_EQ(tree.context->liveNodeCount(), 0U);
    }
}


} // namespace
} // namespace Tina::Tests
