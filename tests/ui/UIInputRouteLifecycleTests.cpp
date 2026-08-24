#include "UIInputRouteTestSupport.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <thread>
#include <utility>

namespace Tina::Tests {
namespace {

using namespace UIInputRouteTestSupport;

TEST_F(UIInputRouteTest, TreeUpdaterListenerRegistrationIsRootScopedAndAtomic)
{
    auto context = createContext(
        window,
        {.nodeCapacity = 8,
         .rootCapacity = 2,
         .routePathCapacity = 8,
         .routedPointerListenerCapacity = 1});
    ASSERT_NE(context, nullptr);
    UI::UIRootOwner firstRoot = createRoot(*context);
    UI::UIRootOwner secondRoot = createRoot(*context);
    ASSERT_TRUE(firstRoot && secondRoot);
    UI::UITreeUpdater firstTree = createUpdater(*context, firstRoot);
    const UI::UINodeId firstButton = createButton(*context, firstRoot.rootNodeId());
    const UI::UINodeId secondButton = createButton(*context, secondRoot.rootNodeId());

    usize rejectedCount = 0;
    auto rejected = firstTree.addRoutedPointerListener(
        {.node = secondButton,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&rejectedCount](
                                        UI::UIRoutedPointerEvent&) noexcept {
            ++rejectedCount;
        }});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidNode);
    UI::UIContextStatistics statistics = context->statistics();
    EXPECT_EQ(statistics.activeRoutedPointerListenerCount, 0U);
    EXPECT_EQ(statistics.routedPointerListenerHighWater, 0U);

    usize acceptedCount = 0;
    auto accepted = firstTree.addRoutedPointerListener(
        {.node = firstButton,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&acceptedCount](
                                        UI::UIRoutedPointerEvent&) noexcept {
            ++acceptedCount;
        }});
    ASSERT_TRUE(accepted.has_value()) << accepted.error().message;
    statistics = context->statistics();
    EXPECT_EQ(statistics.activeRoutedPointerListenerCount, 1U);
    EXPECT_EQ(statistics.routedPointerListenerHighWater, 1U);
    EXPECT_EQ(rejectedCount, 0U);
    EXPECT_EQ(acceptedCount, 0U);
}

TEST_F(UIInputRouteTest, CallbackMoveRootReleaseRollsBackTreeUpdaterRegistration)
{
    auto context = createContext(
        window,
        {.nodeCapacity = 8,
         .rootCapacity = 1,
         .routePathCapacity = 8,
         .routedPointerListenerCapacity = 1});
    ASSERT_NE(context, nullptr);
    UI::UIRootOwner root = createRoot(*context);
    ASSERT_TRUE(root);
    UI::UITreeUpdater tree = createUpdater(*context, root);
    const UI::UINodeId button = createButton(*context, root.rootNodeId());

    struct ReleaseRootOnMove final {
        UI::UIRootOwner* root = nullptr;

        explicit ReleaseRootOnMove(UI::UIRootOwner& owner) noexcept
            : root(&owner)
        {
        }

        ReleaseRootOnMove(const ReleaseRootOnMove&) noexcept = default;

        ReleaseRootOnMove(ReleaseRootOnMove&& other) noexcept
            : root(std::exchange(other.root, nullptr))
        {
            if (root != nullptr) {
                root->reset();
            }
        }

        void operator()(UI::UIRoutedPointerEvent&) noexcept
        {
        }
    } releaseRoot(root);

    auto rejected = tree.addRoutedPointerListener(
        {.node = button,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{releaseRoot});

    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::RootRequired);
    EXPECT_FALSE(root);
    EXPECT_EQ(context->liveRootCount(), 0U);
    EXPECT_EQ(context->liveNodeCount(), 0U);
    UI::UIContextStatistics statistics = context->statistics();
    EXPECT_EQ(statistics.activeRoutedPointerListenerCount, 0U);
    EXPECT_EQ(statistics.routedPointerListenerHighWater, 0U);

    UI::UIRootOwner replacementRoot = createRoot(*context);
    ASSERT_TRUE(replacementRoot);
    UI::UITreeUpdater replacementTree = createUpdater(*context, replacementRoot);
    const UI::UINodeId replacementButton =
        createButton(*context, replacementRoot.rootNodeId());
    auto accepted = replacementTree.addRoutedPointerListener(
        {.node = replacementButton,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent&) noexcept {}});
    ASSERT_TRUE(accepted.has_value()) << accepted.error().message;
    EXPECT_TRUE(*accepted);
    statistics = context->statistics();
    EXPECT_EQ(statistics.activeRoutedPointerListenerCount, 1U);
    EXPECT_EQ(statistics.routedPointerListenerHighWater, 1U);
}

TEST_F(UIInputRouteTest, ListenerCapacityFailureIsAtomicAndFreedSlotIsReusable)
{
    RouteTree tree = createRouteTree(
        window,
        {.nodeCapacity = 8,
         .rootCapacity = 1,
         .routePathCapacity = 8,
         .routedPointerListenerCapacity = 1});
    ASSERT_NE(tree.context, nullptr);
    usize firstCount = 0;
    usize rejectedCount = 0;
    usize replacementCount = 0;

    auto firstToken = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&firstCount](UI::UIRoutedPointerEvent&) noexcept {
            ++firstCount;
        }});
    ASSERT_TRUE(firstToken);
    auto rejected = tree.context->input().addRoutedPointerListener(
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&rejectedCount](UI::UIRoutedPointerEvent&) noexcept {
            ++rejectedCount;
        }});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    UI::UIContextStatistics statistics = tree.context->statistics();
    EXPECT_EQ(statistics.activeRoutedPointerListenerCount, 1U);
    EXPECT_EQ(statistics.routedPointerListenerHighWater, 1U);

    auto firstRoute = tree.context->input().routePointerInput(makePointerInput(window, 1));
    ASSERT_TRUE(firstRoute.has_value());
    EXPECT_EQ(firstRoute->listenerInvocationCount, 1U);
    EXPECT_EQ(firstCount, 1U);
    EXPECT_EQ(rejectedCount, 0U);

    firstToken.reset();
    EXPECT_EQ(tree.context->statistics().activeRoutedPointerListenerCount, 0U);
    auto replacementToken = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&replacementCount](
                                        UI::UIRoutedPointerEvent&) noexcept {
            ++replacementCount;
        }});
    ASSERT_TRUE(replacementToken);

    auto secondRoute = tree.context->input().routePointerInput(makePointerInput(window, 2));
    ASSERT_TRUE(secondRoute.has_value());
    EXPECT_EQ(secondRoute->listenerInvocationCount, 1U);
    EXPECT_EQ(firstCount, 1U);
    EXPECT_EQ(replacementCount, 1U);
    statistics = tree.context->statistics();
    EXPECT_EQ(statistics.activeRoutedPointerListenerCount, 1U);
    EXPECT_EQ(statistics.routedPointerListenerHighWater, 1U);
}

TEST_F(UIInputRouteTest, RouteDepthCapacityFailureInvokesNoPartialCallbacks)
{
    RouteTree tree = createRouteTree(
        window,
        {.nodeCapacity = 8,
         .rootCapacity = 1,
         .routePathCapacity = 2,
         .routedPointerListenerCapacity = 4});
    ASSERT_NE(tree.context, nullptr);
    usize callbackCount = 0;
    auto rootToken = addListener(
        *tree.context,
        {.node = tree.root.rootNodeId(),
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Capture},
        UI::UIRoutedPointerCallback{[&callbackCount](UI::UIRoutedPointerEvent&) noexcept {
            ++callbackCount;
        }});
    auto targetToken = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&callbackCount](UI::UIRoutedPointerEvent&) noexcept {
            ++callbackCount;
        }});
    ASSERT_TRUE(rootToken && targetToken);

    const UI::UICommittedStructureView structure = tree.context->publication().committedStructure();
    const UI::UICommittedLayoutView layout = tree.context->publication().committedLayout();
    const UI::UICommittedHitView hit = tree.context->publication().committedHit();
    const UI::UIContextStatistics before = tree.context->statistics();
    auto routed = tree.context->input().routePointerInput(makePointerInput(window));
    ASSERT_FALSE(routed.has_value());
    EXPECT_EQ(routed.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(callbackCount, 0U);
    EXPECT_EQ(tree.context->publication().committedStructure().revision(), structure.revision());
    EXPECT_EQ(tree.context->publication().committedLayout().layoutRevision(), layout.layoutRevision());
    EXPECT_EQ(tree.context->publication().committedHit().hitRevision(), hit.hitRevision());
    EXPECT_EQ(
        tree.context->statistics().activeRoutedPointerListenerCount,
        before.activeRoutedPointerListenerCount);
    const UI::UIPointerHitQueryResult query =
        tree.context->input().queryPointerHit({.x = 10.0F, .y = 10.0F});
    EXPECT_TRUE(query.hasTarget());
    EXPECT_EQ(query.target.node, tree.target);
}

TEST_F(UIInputRouteTest, DirtyQueueCapacityFailureInvokesNoCallbacks)
{
    RouteTree tree = createRouteTree(
        window,
        {.nodeCapacity = 8,
         .rootCapacity = 1,
         .dirtyQueueCapacity = 3,
         .routePathCapacity = 8,
         .routedPointerListenerCapacity = 1});
    ASSERT_NE(tree.context, nullptr);
    const UI::UINodeId blocker =
        createPanel(*tree.context, tree.root.rootNodeId());
    ASSERT_TRUE(blocker.hasValue());
    usize callbackCount = 0;
    auto token = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&callbackCount](
                                        UI::UIRoutedPointerEvent&) noexcept {
            ++callbackCount;
        }});
    ASSERT_TRUE(token);

    const auto fillDirtySlot = [&tree](
                                   UI::UINodeId node,
                                   UI::UIStraightSrgba8Color color) {
        expectOk(tree.updater.setBoxPaint(
            node,
            UI::UIBoxPaint{
                .solidFill = UI::UISolidFill{.color = color},
            }));
    };
    fillDirtySlot(
        tree.root.rootNodeId(),
        {.red = 1, .green = 2, .blue = 3, .alpha = 255});
    fillDirtySlot(
        tree.panel,
        {.red = 4, .green = 5, .blue = 6, .alpha = 255});
    fillDirtySlot(
        blocker,
        {.red = 7, .green = 8, .blue = 9, .alpha = 255});
    ASSERT_EQ(tree.context->statistics().dirtyQueuePendingCount, 3U);

    auto routed = tree.context->input().routePointerInput(makePointerInput(window));
    ASSERT_FALSE(routed.has_value());
    EXPECT_EQ(routed.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(callbackCount, 0U);
    EXPECT_EQ(tree.context->statistics().dirtyQueuePendingCount, 3U);
}

TEST_F(UIInputRouteTest, ListenerCannotStealReservedPointerRouteDirtySlot)
{
    RouteTree tree = createRouteTree(
        window,
        {.nodeCapacity = 8,
         .rootCapacity = 1,
         .dirtyQueueCapacity = 4,
         .routePathCapacity = 8,
         .routedPointerListenerCapacity = 1});
    ASSERT_NE(tree.context, nullptr);
    const UI::UINodeId blocker =
        createPanel(*tree.context, tree.root.rootNodeId());
    const UI::UINodeId existingDirty =
        createPanel(*tree.context, tree.root.rootNodeId());
    const UI::UINodeId existingDirty2 =
        createPanel(*tree.context, tree.root.rootNodeId());
    const UI::UINodeId existingDirty3 =
        createPanel(*tree.context, tree.root.rootNodeId());
    ASSERT_TRUE(blocker.hasValue());
    ASSERT_TRUE(existingDirty.hasValue());
    ASSERT_TRUE(existingDirty2.hasValue());
    ASSERT_TRUE(existingDirty3.hasValue());
    expectOk(tree.updater.setBoxPaint(
        existingDirty,
        UI::UIBoxPaint{
            .solidFill = UI::UISolidFill{
                .color = {.red = 1, .green = 2, .blue = 3, .alpha = 255},
            },
        }));
    ASSERT_EQ(tree.context->statistics().dirtyQueuePendingCount, 1U);
    expectOk(tree.updater.setBoxPaint(
        existingDirty2,
        UI::UIBoxPaint{
            .solidFill = UI::UISolidFill{
                .color = {.red = 4, .green = 5, .blue = 6, .alpha = 255},
            },
        }));
    expectOk(tree.updater.setBoxPaint(
        existingDirty3,
        UI::UIBoxPaint{
            .solidFill = UI::UISolidFill{
                .color = {.red = 7, .green = 8, .blue = 9, .alpha = 255},
            },
        }));
    ASSERT_EQ(tree.context->statistics().dirtyQueuePendingCount, 3U);

    bool listenerInvoked = false;
    bool unrelatedMutationRejected = false;
    auto token = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{
            [&tree, blocker, &listenerInvoked, &unrelatedMutationRejected](
                UI::UIRoutedPointerEvent&) noexcept {
                listenerInvoked = true;
                const Core::Status mutation = tree.updater.setBoxPaint(
                    blocker,
                    UI::UIBoxPaint{
                        .solidFill = UI::UISolidFill{
                            .color = {.red = 12, .green = 34, .blue = 56, .alpha = 255},
                        },
                    });
                unrelatedMutationRejected = !mutation
                    && mutation.error().code == UI::UIErrorCode::CapacityExceeded;
            }});
    ASSERT_TRUE(token);

    auto routed = tree.context->input().routePointerInput(makePointerInput(window));
    ASSERT_TRUE(routed.has_value()) << (routed ? "" : routed.error().message);
    EXPECT_TRUE(listenerInvoked);
    EXPECT_TRUE(unrelatedMutationRejected);
    EXPECT_TRUE(routed->consumed);
    EXPECT_TRUE(routed->claimedPointerButtons.test(
        static_cast<usize>(Platform::PointerButton::Primary)));
    EXPECT_EQ(tree.context->statistics().dirtyQueuePendingCount, 4U);
    auto pressed = tree.updater.isButtonPressed(tree.target);
    ASSERT_TRUE(pressed.has_value());
    EXPECT_TRUE(*pressed);
}

TEST_F(UIInputRouteTest, GenericRangeInputReservesActivatableAncestorDirtySlot)
{
    auto context = createContext(window, {.nodeCapacity = 8,
                                          .rootCapacity = 1,
                                          .dirtyQueueCapacity = 4,
                                          .routePathCapacity = 8,
                                          .routedPointerListenerCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);

    UI::UIElementDescriptor ancestorDescriptor = UI::makePanelElement();
    ancestorDescriptor.behaviors = UI::UIElementBehavior::Focusable | UI::UIElementBehavior::Activate;
    auto ancestorResult = context->authoring().rootBuilder().createElement(root.rootNodeId(), ancestorDescriptor);
    ASSERT_TRUE(ancestorResult) << (ancestorResult ? "" : ancestorResult.error().message);
    const UI::UINodeId ancestor = *ancestorResult;
    UI::UIElementDescriptor rangeDescriptor = UI::makeLabelElement("Range");
    rangeDescriptor.behaviors = UI::UIElementBehavior::RangeInput;
    auto rangeResult = context->authoring().rootBuilder().createElement(ancestor, rangeDescriptor);
    ASSERT_TRUE(rangeResult) << (rangeResult ? "" : rangeResult.error().message);
    const UI::UINodeId range = *rangeResult;
    const UI::UINodeId blocker = createPanel(*context, root.rootNodeId());
    const UI::UINodeId existingDirty = createPanel(*context, root.rootNodeId());
    const UI::UINodeId existingDirty2 = createPanel(*context, root.rootNodeId());
    const UI::UINodeId existingDirty3 = createPanel(*context, root.rootNodeId());
    ASSERT_TRUE(blocker.hasValue());
    ASSERT_TRUE(existingDirty.hasValue());
    ASSERT_TRUE(existingDirty2.hasValue());
    ASSERT_TRUE(existingDirty3.hasValue());

    auto updater = createUpdater(*context, root);
    expectOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    expectOk(updater.setLayoutStyle(ancestor, fixedSize(80.0F, 80.0F)));
    expectOk(updater.setLayoutStyle(range, fixedSize(40.0F, 40.0F)));
    expectOk(updater.setPointerHitPolicy(range, UI::UIPointerHitPolicy::Targetable));
    expectOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    UI::UIPointerInputEvent hover = makePointerInput(window, 1);
    hover.kind = UI::UIRoutedPointerEventKind::Move;
    auto hovered = context->input().routePointerInput(hover);
    ASSERT_TRUE(hovered) << (hovered ? "" : hovered.error().message);
    expectOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    const auto fillDirtySlot = [&updater](UI::UINodeId node, UI::UIStraightSrgba8Color color) {
        expectOk(updater.setBoxPaint(node, UI::UIBoxPaint{.solidFill = UI::UISolidFill{.color = color}}));
    };
    fillDirtySlot(existingDirty, {.red = 1, .green = 2, .blue = 3, .alpha = 255});
    fillDirtySlot(existingDirty2, {.red = 4, .green = 5, .blue = 6, .alpha = 255});
    fillDirtySlot(existingDirty3, {.red = 7, .green = 8, .blue = 9, .alpha = 255});
    ASSERT_EQ(context->statistics().dirtyQueuePendingCount, 3U);

    bool listenerInvoked = false;
    bool unrelatedMutationRejected = false;
    auto token = addListener(
        *context,
        {.node = range, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&updater, blocker, &listenerInvoked,
                                     &unrelatedMutationRejected](UI::UIRoutedPointerEvent&) noexcept {
            listenerInvoked = true;
            const Core::Status mutation =
                updater.setBoxPaint(blocker, UI::UIBoxPaint{
                                                 .solidFill =
                                                     UI::UISolidFill{
                                                         .color = {.red = 12, .green = 34, .blue = 56, .alpha = 255},
                                                     },
                                             });
            unrelatedMutationRejected = !mutation && mutation.error().code == UI::UIErrorCode::CapacityExceeded;
        }});
    ASSERT_TRUE(token);

    auto routed = context->input().routePointerInput(makePointerInput(window, 2));
    ASSERT_TRUE(routed) << (routed ? "" : routed.error().message);
    EXPECT_TRUE(listenerInvoked);
    EXPECT_TRUE(unrelatedMutationRejected);
    EXPECT_TRUE(routed->consumed);
    EXPECT_EQ(context->input().defaultActionFocus(), ancestor);
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 4U);
}

TEST_F(UIInputRouteTest, ListenerTokenMovesAndSurvivesContextDestroyedFirst)
{
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(tree.context, nullptr);
    auto token = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent&) noexcept {}});
    ASSERT_TRUE(token);

    UI::UIRoutedPointerListenerToken moved(std::move(token));
    EXPECT_FALSE(token);
    EXPECT_TRUE(moved);
    UI::UIRoutedPointerListenerToken finalToken;
    finalToken = std::move(moved);
    EXPECT_FALSE(moved);
    EXPECT_TRUE(finalToken);

    tree.context.reset();
    EXPECT_FALSE(finalToken.isActive());
    EXPECT_NO_THROW(finalToken.reset());
    EXPECT_FALSE(finalToken);
}

TEST_F(UIInputRouteTest, OffThreadListenerResetIsAppliedBeforeTheNextRoute)
{
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(tree.context, nullptr);
    usize callbackCount = 0;
    auto token = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&callbackCount](UI::UIRoutedPointerEvent&) noexcept {
            ++callbackCount;
        }});
    ASSERT_TRUE(token);

    std::thread releaseThread([token = std::move(token)]() mutable noexcept {
        token.reset();
    });
    releaseThread.join();

    auto routed = tree.context->input().routePointerInput(makePointerInput(window));
    ASSERT_TRUE(routed.has_value()) << (routed ? "" : routed.error().message);
    EXPECT_EQ(callbackCount, 0U);
    EXPECT_EQ(routed->listenerInvocationCount, 0U);
    EXPECT_EQ(tree.context->statistics().activeRoutedPointerListenerCount, 0U);
}

TEST_F(UIInputRouteTest, ReentrantCallableDestructionSafelyReleasesItsOwnedRoot)
{
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(tree.context, nullptr);
    UI::UIRoutedPointerListenerToken token;
    UI::UIRootOwner callbackOwnedRoot = std::move(tree.root);

    token = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{
            [&token, ownedRoot = std::move(callbackOwnedRoot)](
                UI::UIRoutedPointerEvent&) noexcept {
                token.reset();
            }});
    ASSERT_TRUE(token);

    auto routed = tree.context->input().routePointerInput(makePointerInput(window));
    ASSERT_TRUE(routed.has_value()) << (routed ? "" : routed.error().message);
    EXPECT_EQ(routed->listenerInvocationCount, 1U);
    EXPECT_EQ(tree.context->liveRootCount(), 0U);
    EXPECT_EQ(tree.context->liveNodeCount(), 0U);
    EXPECT_EQ(tree.context->statistics().activeRoutedPointerListenerCount, 0U);
    EXPECT_FALSE(token);
}

TEST_F(UIInputRouteTest, CommitAttemptsDuringRouteAreRejectedAndOuterRouteCompletes)
{
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(tree.context, nullptr);
    struct State final {
        UI::UIContext* context = nullptr;
        Core::ErrorCode structureError{};
        Core::ErrorCode layoutError{};
        usize laterListenerCount = 0;
    } state{.context = tree.context.get()};

    auto guardToken = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&state](UI::UIRoutedPointerEvent&) noexcept {
            Core::Status structure = state.context->publication().commitStructure();
            Core::Status layout = state.context->publication().commitLayout(
                {.width = 100.0F, .height = 100.0F});
            if (!structure) {
                state.structureError = structure.error().code;
            }
            if (!layout) {
                state.layoutError = layout.error().code;
            }
        }});
    auto laterToken = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&state](UI::UIRoutedPointerEvent&) noexcept {
            ++state.laterListenerCount;
        }});
    ASSERT_TRUE(guardToken && laterToken);

    auto routed = tree.context->input().routePointerInput(makePointerInput(window));
    ASSERT_TRUE(routed.has_value()) << (routed ? "" : routed.error().message);
    EXPECT_EQ(state.structureError, UI::UIErrorCode::PointerRouteAlreadyInProgress);
    EXPECT_EQ(state.layoutError, UI::UIErrorCode::PointerRouteAlreadyInProgress);
    EXPECT_EQ(state.laterListenerCount, 1U);
    EXPECT_EQ(routed->listenerInvocationCount, 2U);
}

TEST_F(UIInputRouteTest, DefaultActionActivationFromListenerRetainsRouteGuard)
{
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(tree.context, nullptr);

    struct State final {
        UI::UIContext* context = nullptr;
        Core::ErrorCode nestedError{};
        Core::ErrorCode commitError{};
        usize callbackCount = 0;
        bool nestedRejected = false;
    } state{.context = tree.context.get()};

    auto action = tree.updater.setButtonAction(
        tree.target,
        UI::UIButtonActionCallback{[&state](const UI::UIButtonActionEvent&) noexcept {
            ++state.callbackCount;
        }});
    ASSERT_TRUE(action.has_value()) << (action ? "" : action.error().message);

    auto focus = tree.context->input().routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    ASSERT_EQ(focus->focus, tree.target);
    ASSERT_EQ(tree.context->input().defaultActionFocus(), tree.target);
    expectOk(tree.context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    expectOk(tree.updater.setBoxPaint(
        tree.panel,
        UI::UIBoxPaint{
            .solidFill = UI::UISolidFill{
                .color = {.red = 17, .green = 34, .blue = 51, .alpha = 255},
            },
        }));
    const u64 paintRevisionBefore = tree.context->statistics().paintRevision;

    auto token = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&state](UI::UIRoutedPointerEvent&) noexcept {
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
        }});
    ASSERT_TRUE(token);

    auto routed = tree.context->input().routePointerInput(makePointerInput(window));
    ASSERT_TRUE(routed.has_value()) << (routed ? "" : routed.error().message);
    EXPECT_TRUE(state.nestedRejected);
    EXPECT_EQ(state.nestedError, UI::UIErrorCode::PointerRouteAlreadyInProgress);
    EXPECT_EQ(state.commitError, UI::UIErrorCode::PointerRouteAlreadyInProgress);
    EXPECT_EQ(state.callbackCount, 0U);
    EXPECT_EQ(tree.context->input().defaultActionFocus(), tree.target);
    EXPECT_EQ(tree.context->statistics().paintRevision, paintRevisionBefore);
    EXPECT_EQ(routed->listenerInvocationCount, 1U);
    EXPECT_FALSE(routed->targetInvalidated);
}

TEST_F(UIInputRouteTest, DestroyingContextFromItsCallbackTerminates)
{
    EXPECT_EXIT(
        {
            std::set_terminate([]() noexcept {
                std::fputs("TINA_UI_CONTEXT_LIFETIME_VIOLATION\n", stderr);
                std::fflush(stderr);
                std::_Exit(86);
            });
            RouteTree tree = createRouteTree(window);
            if (!tree.context) {
                std::abort();
            }
            std::unique_ptr<UI::UIContext>* owner = &tree.context;
            auto token = addListener(
                *tree.context,
                {.node = tree.target,
                 .kind = UI::UIRoutedPointerEventKind::ButtonDown,
                 .phases = UI::UIEventPhaseMask::Target},
                UI::UIRoutedPointerCallback{
                    [owner](UI::UIRoutedPointerEvent&) noexcept {
                        owner->reset();
                    }});
            if (!token) {
                std::abort();
            }
            UI::UIContext* context = tree.context.get();
            (void)context->input().routePointerInput(makePointerInput(window));
        },
        ::testing::ExitedWithCode(86),
        ".*");
}

TEST_F(UIInputRouteTest, DestroyingContextFromListenerCallbackMoveTerminates)
{
    EXPECT_EXIT(
        {
            std::set_terminate([]() noexcept {
                std::_Exit(86);
            });
            auto context = createContext(
                window,
                {.nodeCapacity = 8,
                 .rootCapacity = 1,
                 .routePathCapacity = 8,
                 .routedPointerListenerCapacity = 1});
            if (!context) {
                std::abort();
            }
            UI::UIRootOwner root = createRoot(*context);
            if (!root) {
                std::abort();
            }
            const UI::UINodeId button = createButton(*context, root.rootNodeId());

            struct ReleaseContextOnMove final {
                std::unique_ptr<UI::UIContext>* context = nullptr;

                explicit ReleaseContextOnMove(
                    std::unique_ptr<UI::UIContext>& owner) noexcept
                    : context(&owner)
                {
                }

                ReleaseContextOnMove(const ReleaseContextOnMove&) noexcept = default;

                ReleaseContextOnMove(ReleaseContextOnMove&& other) noexcept
                    : context(std::exchange(other.context, nullptr))
                {
                    if (context != nullptr) {
                        context->reset();
                    }
                }

                void operator()(UI::UIRoutedPointerEvent&) noexcept
                {
                }
            } releaseContext(context);

            (void)context->input().addRoutedPointerListener(
                {.node = button,
                 .kind = UI::UIRoutedPointerEventKind::ButtonDown,
                 .phases = UI::UIEventPhaseMask::Target},
                UI::UIRoutedPointerCallback{releaseContext});
            std::abort();
        },
        ::testing::ExitedWithCode(86),
        ".*");
}

TEST_F(UIInputRouteTest, DestroyingContextOffOwnerThreadTerminates)
{
    EXPECT_EXIT(
        {
            RouteTree tree = createRouteTree(window);
            if (!tree.context) {
                std::abort();
            }
            std::thread destroyThread(
                [context = std::move(tree.context)]() mutable noexcept {
                    // MSVC keeps the terminate handler in thread-local CRT
                    // state, so install the sentinel in the violating thread.
                    std::set_terminate([]() noexcept {
                        std::_Exit(86);
                    });
                    context.reset();
                });
            destroyThread.join();
        },
        ::testing::ExitedWithCode(86),
        ".*");
}

TEST_F(UIInputRouteTest, ThreeHundredRoutesDoNotAllocateOrMutateCommittedState)
{
    ObservingMemoryResource resource;
    RouteTree tree = createRouteTree(
        window,
        {.nodeCapacity = 8,
         .rootCapacity = 1,
         .routePathCapacity = 8,
         .routedPointerListenerCapacity = 4},
        resource);
    ASSERT_NE(tree.context, nullptr);
    usize callbackCount = 0;
    auto token = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&callbackCount](UI::UIRoutedPointerEvent&) noexcept {
            ++callbackCount;
        }});
    ASSERT_TRUE(token);

    const UI::UICommittedStructureView structure = tree.context->publication().committedStructure();
    const UI::UICommittedLayoutView layout = tree.context->publication().committedLayout();
    const UI::UICommittedHitView hit = tree.context->publication().committedHit();
    const UI::UIContextStatistics before = tree.context->statistics();
    const usize allocationCount = resource.allocationCount();
    const UI::UICommittedNodeEntry* const structureData = structure.entries().data();
    const UI::UICommittedLayoutEntry* const layoutData = layout.entries().data();
    const UI::UICommittedHitEntry* const hitData = hit.entries().data();

    for (u64 frame = 1; frame <= 300; ++frame) {
        auto routed = tree.context->input().routePointerInput(makePointerInput(window, frame));
        ASSERT_TRUE(routed.has_value())
            << (routed ? "" : routed.error().message);
        EXPECT_EQ(routed->listenerInvocationCount, 1U);
        EXPECT_FALSE(routed->targetInvalidated);
    }

    EXPECT_EQ(callbackCount, 300U);
    EXPECT_EQ(resource.allocationCount(), allocationCount);
    EXPECT_EQ(tree.context->publication().committedStructure().entries().data(), structureData);
    EXPECT_EQ(tree.context->publication().committedLayout().entries().data(), layoutData);
    EXPECT_EQ(tree.context->publication().committedHit().entries().data(), hitData);
    const UI::UIContextStatistics after = tree.context->statistics();
    EXPECT_EQ(after.committedRevision, before.committedRevision);
    EXPECT_EQ(after.layoutRevision, before.layoutRevision);
    EXPECT_EQ(after.hitRevision, before.hitRevision);
    EXPECT_EQ(after.paintOrderRevision, before.paintOrderRevision);
    EXPECT_EQ(after.activeRoutedPointerListenerCount,
              before.activeRoutedPointerListenerCount);
    EXPECT_EQ(after.structureDirty, before.structureDirty);
    EXPECT_EQ(after.layoutDirty, before.layoutDirty);
    EXPECT_EQ(after.hitDirty, before.hitDirty);
}

TEST_F(UIInputRouteTest, RecursiveRouteIsRejectedWhileOuterDispatchCompletes)
{
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(tree.context, nullptr);
    struct State final {
        UI::UIContext* context = nullptr;
        UI::UIPointerInputEvent input{};
        Core::ErrorCode nestedError{};
        usize firstCount = 0;
        usize secondCount = 0;
        bool nestedReturned = false;
        bool nestedRejected = false;
    } state{.context = tree.context.get(), .input = makePointerInput(window)};

    auto firstToken = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&state](UI::UIRoutedPointerEvent&) noexcept {
            ++state.firstCount;
            auto nested = state.context->input().routePointerInput(state.input);
            state.nestedReturned = true;
            state.nestedRejected = !nested.has_value();
            if (!nested) {
                state.nestedError = nested.error().code;
            }
        }});
    auto secondToken = addListener(
        *tree.context,
        {.node = tree.target,
         .kind = UI::UIRoutedPointerEventKind::ButtonDown,
         .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&state](UI::UIRoutedPointerEvent&) noexcept {
            ++state.secondCount;
        }});
    ASSERT_TRUE(firstToken && secondToken);

    auto outer = tree.context->input().routePointerInput(state.input);
    ASSERT_TRUE(outer.has_value()) << (outer ? "" : outer.error().message);
    EXPECT_TRUE(state.nestedReturned);
    EXPECT_TRUE(state.nestedRejected);
    EXPECT_EQ(state.nestedError, UI::UIErrorCode::PointerRouteAlreadyInProgress);
    EXPECT_EQ(state.firstCount, 1U);
    EXPECT_EQ(state.secondCount, 1U);
    EXPECT_EQ(outer->listenerInvocationCount, 2U);
    EXPECT_FALSE(outer->stopped);
    EXPECT_FALSE(outer->targetInvalidated);
}

} // namespace
} // namespace Tina::Tests
