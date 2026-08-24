#include "UIInputRouteTestSupport.hpp"

namespace Tina::Tests {
namespace {

using namespace UIInputRouteTestSupport;

TEST_F(UIInputRouteTest, RoutesCaptureTargetAndBubbleInStableOrder)
{
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(tree.context, nullptr);
    const UI::UIPointerInputEvent input = makePointerInput(window);

    struct State final {
        RouteTrace trace;
        UI::UIPointerInputEvent input{};
        UI::UINodeId root{};
        UI::UINodeId target{};
        bool identityValid = true;
    } state{
        .trace = {},
        .input = input,
        .root = tree.root.rootNodeId(),
        .target = tree.target,
    };

    const auto routePhases =
        UI::UIEventPhaseMask::Capture | UI::UIEventPhaseMask::Bubble;
    auto rootToken = addListener(
        *tree.context,
        {.node = tree.root.rootNodeId(),
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = routePhases},
        UI::UIRoutedPointerCallback{[&state](UI::UIRoutedPointerEvent& event) noexcept {
            state.identityValid = state.identityValid
                && event.input() == state.input
                && event.rootNode() == state.root
                && event.targetNode() == state.target;
            state.trace.push(event, 1);
        }});
    auto panelToken = addListener(
        *tree.context,
        {.node = tree.panel,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = routePhases},
        UI::UIRoutedPointerCallback{[&state](UI::UIRoutedPointerEvent& event) noexcept {
            state.identityValid = state.identityValid
                && event.input() == state.input
                && event.rootNode() == state.root
                && event.targetNode() == state.target;
            state.trace.push(event, 2);
        }});
    auto firstTargetToken = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&state](UI::UIRoutedPointerEvent& event) noexcept {
            state.identityValid = state.identityValid
                && event.input() == state.input
                && event.rootNode() == state.root
                && event.targetNode() == state.target;
            state.trace.push(event, 3);
        }});
    auto secondTargetToken = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&state](UI::UIRoutedPointerEvent& event) noexcept {
            state.trace.push(event, 4);
            event.consumeInputTransition();
        }});
    ASSERT_TRUE(rootToken && panelToken && firstTargetToken && secondTargetToken);

    auto routed = tree.context->input().routePointerInput(input);
    ASSERT_TRUE(routed.has_value()) << (routed ? "" : routed.error().message);
    EXPECT_TRUE(state.identityValid);
    ASSERT_EQ(state.trace.size, 6U);
    const std::array<TraceEntry, 6> expected{
        TraceEntry{tree.root.rootNodeId(), UI::UIEventPhase::Capture, 1},
        TraceEntry{tree.panel, UI::UIEventPhase::Capture, 2},
        TraceEntry{tree.target, UI::UIEventPhase::Target, 3},
        TraceEntry{tree.target, UI::UIEventPhase::Target, 4},
        TraceEntry{tree.panel, UI::UIEventPhase::Bubble, 2},
        TraceEntry{tree.root.rootNodeId(), UI::UIEventPhase::Bubble, 1},
    };
    for (usize index = 0; index < expected.size(); ++index) {
        EXPECT_EQ(state.trace.entries[index].node, expected[index].node);
        EXPECT_EQ(state.trace.entries[index].phase, expected[index].phase);
        EXPECT_EQ(state.trace.entries[index].marker, expected[index].marker);
    }
    EXPECT_EQ(routed->routeDepth, 3U);
    EXPECT_EQ(routed->listenerInvocationCount, 6U);
    EXPECT_TRUE(routed->consumed);
    EXPECT_FALSE(routed->stopped);
    EXPECT_FALSE(routed->targetInvalidated);
    EXPECT_TRUE(routed->hasRoutedTarget());
}

TEST_F(UIInputRouteTest, PointerButtonClaimsMergeAcrossPhasesAndDuplicates)
{
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(tree.context, nullptr);
    RouteTrace trace;

    auto rootCaptureToken = addListener(
        *tree.context,
        {.node = tree.root.rootNodeId(),
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Capture},
        UI::UIRoutedPointerCallback{[&trace](UI::UIRoutedPointerEvent& event) noexcept {
            trace.push(event, 1);
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Primary));
        }});
    auto targetToken = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&trace](UI::UIRoutedPointerEvent& event) noexcept {
            trace.push(event, 2);
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Primary));
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Secondary));
        }});
    auto panelBubbleToken = addListener(
        *tree.context,
        {.node = tree.panel,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Bubble},
        UI::UIRoutedPointerCallback{[&trace](UI::UIRoutedPointerEvent& event) noexcept {
            trace.push(event, 3);
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Middle));
            event.stopPropagation();
        }});
    auto skippedRootBubbleToken = addListener(
        *tree.context,
        {.node = tree.root.rootNodeId(),
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Bubble},
        UI::UIRoutedPointerCallback{[&trace](UI::UIRoutedPointerEvent& event) noexcept {
            trace.push(event, 4);
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Button4));
        }});
    ASSERT_TRUE(
        rootCaptureToken && targetToken && panelBubbleToken
        && skippedRootBubbleToken);

    auto routed = tree.context->input().routePointerInput(makePointerInput(window));
    ASSERT_TRUE(routed.has_value()) << (routed ? "" : routed.error().message);

    ASSERT_EQ(trace.size, 3U);
    EXPECT_EQ(trace.entries[0].marker, 1);
    EXPECT_EQ(trace.entries[1].marker, 2);
    EXPECT_EQ(trace.entries[2].marker, 3);
    EXPECT_TRUE(routed->claimedPointerButtons.test(
        static_cast<usize>(Platform::PointerButton::Primary)));
    EXPECT_TRUE(routed->claimedPointerButtons.test(
        static_cast<usize>(Platform::PointerButton::Secondary)));
    EXPECT_TRUE(routed->claimedPointerButtons.test(
        static_cast<usize>(Platform::PointerButton::Middle)));
    EXPECT_FALSE(routed->claimedPointerButtons.test(
        static_cast<usize>(Platform::PointerButton::Button4)));
    EXPECT_EQ(routed->listenerInvocationCount, 3U);
    EXPECT_TRUE(routed->stopped);
    EXPECT_FALSE(routed->targetInvalidated);
}

TEST_F(UIInputRouteTest, PointerButtonClaimRejectsInvalidButton)
{
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(tree.context, nullptr);
    bool rejectedInvalidButton = false;

    auto token = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{
            [&rejectedInvalidButton](UI::UIRoutedPointerEvent& event) noexcept {
                event.preventDefaultAction();
                rejectedInvalidButton = !event.claimPointerButton(
                    static_cast<Platform::PointerButton>(
                        Platform::PointerButtonCount));
                EXPECT_TRUE(event.claimPointerButton(
                    Platform::PointerButton::Secondary));
            }});
    ASSERT_TRUE(token);

    auto routed = tree.context->input().routePointerInput(makePointerInput(window));
    ASSERT_TRUE(routed.has_value()) << (routed ? "" : routed.error().message);
    EXPECT_TRUE(rejectedInvalidButton);
    EXPECT_FALSE(routed->claimedPointerButtons.test(
        static_cast<usize>(Platform::PointerButton::Primary)));
    EXPECT_TRUE(routed->claimedPointerButtons.test(
        static_cast<usize>(Platform::PointerButton::Secondary)));
    EXPECT_EQ(routed->claimedPointerButtons.count(), 1U);
}

TEST_F(UIInputRouteTest, PointerButtonClaimResultIsEmptyWhenNoHit)
{
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(tree.context, nullptr);
    usize callbackCount = 0;

    auto token = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&callbackCount](
                                        UI::UIRoutedPointerEvent& event) noexcept {
            ++callbackCount;
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Primary));
        }});
    ASSERT_TRUE(token);

    UI::UIPointerInputEvent input = makePointerInput(window);
    input.position = {.x = 90.0F, .y = 90.0F};

    auto routed = tree.context->input().routePointerInput(input);
    ASSERT_TRUE(routed.has_value()) << (routed ? "" : routed.error().message);
    EXPECT_FALSE(routed->pointQuery.hasTarget());
    EXPECT_EQ(callbackCount, 0U);
    EXPECT_TRUE(routed->claimedPointerButtons.none());
    EXPECT_EQ(routed->listenerInvocationCount, 0U);
    EXPECT_FALSE(routed->hasRoutedTarget());
}

TEST_F(UIInputRouteTest, StopPropagationFinishesCurrentNodeAndSkipsLaterNodes)
{
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(tree.context, nullptr);
    RouteTrace trace;

    auto firstRootToken = addListener(
        *tree.context,
        {.node = tree.root.rootNodeId(),
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Capture},
        UI::UIRoutedPointerCallback{[&trace](UI::UIRoutedPointerEvent& event) noexcept {
            trace.push(event, 1);
            event.stopPropagation();
        }});
    auto secondRootToken = addListener(
        *tree.context,
        {.node = tree.root.rootNodeId(),
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Capture},
        UI::UIRoutedPointerCallback{[&trace](UI::UIRoutedPointerEvent& event) noexcept {
            trace.push(event, 2);
        }});
    auto panelToken = addListener(
        *tree.context,
        {.node = tree.panel,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Capture},
        UI::UIRoutedPointerCallback{[&trace](UI::UIRoutedPointerEvent& event) noexcept {
            trace.push(event, 3);
        }});
    ASSERT_TRUE(firstRootToken && secondRootToken && panelToken);

    auto routed = tree.context->input().routePointerInput(makePointerInput(window));
    ASSERT_TRUE(routed.has_value());
    ASSERT_EQ(trace.size, 2U);
    EXPECT_EQ(trace.entries[0].marker, 1);
    EXPECT_EQ(trace.entries[1].marker, 2);
    EXPECT_EQ(routed->listenerInvocationCount, 2U);
    EXPECT_TRUE(routed->stopped);
}

TEST_F(UIInputRouteTest, StopImmediatePropagationSkipsSameNodeRemainderAndBubble)
{
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(tree.context, nullptr);
    RouteTrace trace;

    auto firstTargetToken = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&trace](UI::UIRoutedPointerEvent& event) noexcept {
            trace.push(event, 1);
            event.stopImmediatePropagation();
        }});
    auto secondTargetToken = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&trace](UI::UIRoutedPointerEvent& event) noexcept {
            trace.push(event, 2);
        }});
    auto bubbleToken = addListener(
        *tree.context,
        {.node = tree.root.rootNodeId(),
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Bubble},
        UI::UIRoutedPointerCallback{[&trace](UI::UIRoutedPointerEvent& event) noexcept {
            trace.push(event, 3);
        }});
    ASSERT_TRUE(firstTargetToken && secondTargetToken && bubbleToken);

    auto routed = tree.context->input().routePointerInput(makePointerInput(window));
    ASSERT_TRUE(routed.has_value());
    ASSERT_EQ(trace.size, 1U);
    EXPECT_EQ(trace.entries[0].marker, 1);
    EXPECT_EQ(routed->listenerInvocationCount, 1U);
    EXPECT_TRUE(routed->stopped);
}

TEST_F(UIInputRouteTest, ResettingLaterListenerDuringDispatchSkipsItImmediately)
{
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(tree.context, nullptr);
    usize firstCount = 0;
    usize secondCount = 0;
    UI::UIRoutedPointerListenerToken secondToken;

    auto firstToken = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&firstCount, &secondToken](
                                        UI::UIRoutedPointerEvent&) noexcept {
            ++firstCount;
            secondToken.reset();
        }});
    secondToken = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&secondCount](UI::UIRoutedPointerEvent&) noexcept {
            ++secondCount;
        }});
    ASSERT_TRUE(firstToken && secondToken);

    auto firstRoute = tree.context->input().routePointerInput(makePointerInput(window, 1));
    ASSERT_TRUE(firstRoute.has_value());
    EXPECT_EQ(firstRoute->listenerInvocationCount, 1U);
    EXPECT_EQ(firstCount, 1U);
    EXPECT_EQ(secondCount, 0U);
    EXPECT_FALSE(secondToken);
    EXPECT_EQ(tree.context->statistics().activeRoutedPointerListenerCount, 1U);

    auto secondRoute = tree.context->input().routePointerInput(makePointerInput(window, 2));
    ASSERT_TRUE(secondRoute.has_value());
    EXPECT_EQ(secondRoute->listenerInvocationCount, 1U);
    EXPECT_EQ(firstCount, 2U);
    EXPECT_EQ(secondCount, 0U);
}

TEST_F(UIInputRouteTest, ListenerAddedDuringDispatchStartsWithNextTransition)
{
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(tree.context, nullptr);

    struct State final {
        UI::UIContext* context = nullptr;
        UI::UINodeId target{};
        UI::UIRoutedPointerListenerToken addedToken;
        usize firstCount = 0;
        usize addedCount = 0;
        bool attempted = false;
        bool addSucceeded = false;
    } state{
        .context = tree.context.get(),
        .target = tree.target,
        .addedToken = {},
    };

    auto firstToken = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&state](UI::UIRoutedPointerEvent&) noexcept {
            ++state.firstCount;
            if (state.attempted) {
                return;
            }
            state.attempted = true;
            auto added = state.context->input().addRoutedPointerListener(
                {.node = state.target,
                 .kind = UI::UIRoutedPointerEventKind::ButtonDown,
                 .phases = UI::UIEventPhaseMask::Target},
                UI::UIRoutedPointerCallback{[&state](
                                                UI::UIRoutedPointerEvent&) noexcept {
                    ++state.addedCount;
                }});
            state.addSucceeded = added.has_value();
            if (added) {
                state.addedToken = std::move(*added);
            }
        }});
    ASSERT_TRUE(firstToken);

    auto firstRoute = tree.context->input().routePointerInput(makePointerInput(window, 1));
    ASSERT_TRUE(firstRoute.has_value());
    EXPECT_TRUE(state.addSucceeded);
    EXPECT_TRUE(state.addedToken);
    EXPECT_EQ(firstRoute->listenerInvocationCount, 1U);
    EXPECT_EQ(state.firstCount, 1U);
    EXPECT_EQ(state.addedCount, 0U);

    auto secondRoute = tree.context->input().routePointerInput(makePointerInput(window, 2));
    ASSERT_TRUE(secondRoute.has_value());
    EXPECT_EQ(secondRoute->listenerInvocationCount, 2U);
    EXPECT_EQ(state.firstCount, 2U);
    EXPECT_EQ(state.addedCount, 1U);
}

TEST_F(UIInputRouteTest, DestroyingTargetDuringCaptureDoesNotRouteToReusedGeneration)
{
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(tree.context, nullptr);

    struct State final {
        UI::UIContext* context = nullptr;
        UI::UITreeUpdater* updater = nullptr;
        UI::UINodeId panel{};
        UI::UINodeId original{};
        UI::UINodeId replacement{};
        UI::UIRoutedPointerListenerToken replacementToken;
        usize oldTargetCount = 0;
        usize replacementCount = 0;
        bool attempted = false;
        bool destroySucceeded = false;
        bool createSucceeded = false;
        bool listenerSucceeded = false;
    } state{
        .context = tree.context.get(),
        .updater = &tree.updater,
        .panel = tree.panel,
        .original = tree.target,
        .replacementToken = {},
    };

    auto oldTargetToken = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&state](UI::UIRoutedPointerEvent&) noexcept {
            ++state.oldTargetCount;
        }});
    auto rootToken = addListener(
        *tree.context,
        {.node = tree.root.rootNodeId(),
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Capture},
        UI::UIRoutedPointerCallback{[&state](UI::UIRoutedPointerEvent&) noexcept {
            if (state.attempted) {
                return;
            }
            state.attempted = true;
            const Core::Status destroyed = state.updater->destroy(state.original);
            state.destroySucceeded = destroyed.has_value();
            if (!destroyed) {
                return;
            }
            auto replacement = state.context->authoring().rootBuilder().createElement(state.panel, UI::makeButtonElement());
            state.createSucceeded = replacement.has_value();
            if (!replacement) {
                return;
            }
            state.replacement = *replacement;
            auto listener = state.context->input().addRoutedPointerListener(
                {.node = state.replacement,
                 .kind = UI::UIRoutedPointerEventKind::ButtonDown,
                 .phases = UI::UIEventPhaseMask::Target},
                UI::UIRoutedPointerCallback{[&state](
                                                UI::UIRoutedPointerEvent&) noexcept {
                    ++state.replacementCount;
                }});
            state.listenerSucceeded = listener.has_value();
            if (listener) {
                state.replacementToken = std::move(*listener);
            }
        }});
    ASSERT_TRUE(oldTargetToken && rootToken);

    auto firstRoute = tree.context->input().routePointerInput(makePointerInput(window, 1));
    ASSERT_TRUE(firstRoute.has_value())
        << (firstRoute ? "" : firstRoute.error().message);
    EXPECT_TRUE(state.destroySucceeded);
    EXPECT_TRUE(state.createSucceeded);
    EXPECT_TRUE(state.listenerSucceeded);
    EXPECT_TRUE(state.replacementToken);
    EXPECT_EQ(state.replacement.index(), state.original.index());
    EXPECT_NE(state.replacement.generation(), state.original.generation());
    EXPECT_EQ(state.oldTargetCount, 0U);
    EXPECT_EQ(state.replacementCount, 0U);
    EXPECT_FALSE(oldTargetToken);
    EXPECT_TRUE(firstRoute->targetInvalidated);
    EXPECT_EQ(firstRoute->listenerInvocationCount, 1U);

    expectOk(tree.updater.setLayoutStyle(
        state.replacement,
        fixedSize(40.0F, 40.0F)));
    expectOk(tree.updater.setPointerHitPolicy(
        state.replacement,
        UI::UIPointerHitPolicy::Targetable));
    expectOk(tree.context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    auto secondRoute = tree.context->input().routePointerInput(makePointerInput(window, 2));
    ASSERT_TRUE(secondRoute.has_value());
    EXPECT_FALSE(secondRoute->targetInvalidated);
    EXPECT_EQ(secondRoute->pointQuery.target.node, state.replacement);
    EXPECT_EQ(secondRoute->listenerInvocationCount, 2U);
    EXPECT_EQ(state.oldTargetCount, 0U);
    EXPECT_EQ(state.replacementCount, 1U);
}

} // namespace
} // namespace Tina::Tests
