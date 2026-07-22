#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <memory_resource>
#include <thread>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

class ObservingMemoryResource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] usize allocationCount() const noexcept
    {
        return m_allocationCount;
    }

private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        void* storage = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_allocationCount;
        return storage;
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(
        const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize m_allocationCount = 0;
};

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window,
    UI::UIContextCapacityConfig capacities,
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    auto result = UI::UIContext::Create(window, capacities, resource);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

[[nodiscard]] UI::UINodeId createPanel(
    UI::UIContext& context,
    UI::UINodeId parent)
{
    auto result = context.rootBuilder().createPanel(parent);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] UI::UINodeId createButton(
    UI::UIContext& context,
    UI::UINodeId parent)
{
    auto result = context.rootBuilder().createButton(parent);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] UI::UITreeUpdater createUpdater(
    UI::UIContext& context,
    UI::UIRootOwner& root)
{
    auto result = context.treeUpdater(root);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UITreeUpdater{};
}

void expectOk(Core::Status status)
{
    EXPECT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] UI::UIRoutedPointerListenerToken addListener(
    UI::UIContext& context,
    UI::UIRoutedPointerListenerDesc descriptor,
    UI::UIRoutedPointerCallback callback)
{
    auto result = context.addRoutedPointerListener(
        descriptor,
        std::move(callback));
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRoutedPointerListenerToken{};
}

struct RouteTree final {
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UITreeUpdater updater;
    UI::UINodeId panel{};
    UI::UINodeId target{};
};

[[nodiscard]] RouteTree createRouteTree(
    Platform::WindowId window,
    UI::UIContextCapacityConfig capacities = {
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .routePathCapacity = 8,
        .routedPointerListenerCapacity = 16,
    },
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    RouteTree tree;
    tree.context = createContext(window, capacities, resource);
    if (!tree.context) {
        return tree;
    }

    tree.root = createRoot(*tree.context);
    if (!tree.root) {
        return tree;
    }
    tree.panel = createPanel(*tree.context, tree.root.rootNodeId());
    tree.target = createButton(*tree.context, tree.panel);
    tree.updater = createUpdater(*tree.context, tree.root);

    expectOk(tree.updater.setLayoutStyle(
        tree.root.rootNodeId(),
        fixedSize(100.0F, 100.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.panel, fixedSize(80.0F, 80.0F)));
    expectOk(tree.updater.setLayoutStyle(tree.target, fixedSize(40.0F, 40.0F)));
    expectOk(tree.updater.setPointerHitPolicy(
        tree.target,
        UI::UIPointerHitPolicy::Targetable));
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));
    return tree;
}

[[nodiscard]] UI::UIPointerInputEvent makePointerInput(
    Platform::WindowId window,
    u64 sequence = 1) noexcept
{
    return UI::UIPointerInputEvent{
        .platformFrame = Platform::PlatformFrameId{sequence},
        .transitionOrdinal = static_cast<usize>(sequence - 1),
        .sourceSequence = sequence,
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .kind = UI::UIRoutedPointerEventKind::ButtonDown,
        .position = {.x = 10.0F, .y = 10.0F},
        .delta = {},
        .button = Platform::PointerButton::Primary,
    };
}

struct TraceEntry final {
    UI::UINodeId node{};
    UI::UIEventPhase phase = UI::UIEventPhase::Target;
    int marker = 0;
};

struct RouteTrace final {
    void push(const UI::UIRoutedPointerEvent& event, int marker) noexcept
    {
        if (size < entries.size()) {
            entries[size++] = TraceEntry{
                .node = event.currentNode(),
                .phase = event.currentPhase(),
                .marker = marker,
            };
        }
    }

    std::array<TraceEntry, 16> entries{};
    usize size = 0;
};

class UIInputRouteTest : public testing::Test {
protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(1);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto windowResult = windows->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());
        window = *windowResult;
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
};

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

    auto routed = tree.context->routePointerInput(input);
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

    auto routed = tree.context->routePointerInput(makePointerInput(window));
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

    auto routed = tree.context->routePointerInput(makePointerInput(window));
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

    auto routed = tree.context->routePointerInput(input);
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

    auto routed = tree.context->routePointerInput(makePointerInput(window));
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

    auto routed = tree.context->routePointerInput(makePointerInput(window));
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

    auto firstRoute = tree.context->routePointerInput(makePointerInput(window, 1));
    ASSERT_TRUE(firstRoute.has_value());
    EXPECT_EQ(firstRoute->listenerInvocationCount, 1U);
    EXPECT_EQ(firstCount, 1U);
    EXPECT_EQ(secondCount, 0U);
    EXPECT_FALSE(secondToken);
    EXPECT_EQ(tree.context->statistics().activeRoutedPointerListenerCount, 1U);

    auto secondRoute = tree.context->routePointerInput(makePointerInput(window, 2));
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
            auto added = state.context->addRoutedPointerListener(
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

    auto firstRoute = tree.context->routePointerInput(makePointerInput(window, 1));
    ASSERT_TRUE(firstRoute.has_value());
    EXPECT_TRUE(state.addSucceeded);
    EXPECT_TRUE(state.addedToken);
    EXPECT_EQ(firstRoute->listenerInvocationCount, 1U);
    EXPECT_EQ(state.firstCount, 1U);
    EXPECT_EQ(state.addedCount, 0U);

    auto secondRoute = tree.context->routePointerInput(makePointerInput(window, 2));
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
            auto replacement = state.context->rootBuilder().createButton(state.panel);
            state.createSucceeded = replacement.has_value();
            if (!replacement) {
                return;
            }
            state.replacement = *replacement;
            auto listener = state.context->addRoutedPointerListener(
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

    auto firstRoute = tree.context->routePointerInput(makePointerInput(window, 1));
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
    expectOk(tree.context->commitLayout({.width = 100.0F, .height = 100.0F}));

    auto secondRoute = tree.context->routePointerInput(makePointerInput(window, 2));
    ASSERT_TRUE(secondRoute.has_value());
    EXPECT_FALSE(secondRoute->targetInvalidated);
    EXPECT_EQ(secondRoute->pointQuery.target.node, state.replacement);
    EXPECT_EQ(secondRoute->listenerInvocationCount, 2U);
    EXPECT_EQ(state.oldTargetCount, 0U);
    EXPECT_EQ(state.replacementCount, 1U);
}

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
    auto rejected = tree.context->addRoutedPointerListener(
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

    auto firstRoute = tree.context->routePointerInput(makePointerInput(window, 1));
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

    auto secondRoute = tree.context->routePointerInput(makePointerInput(window, 2));
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

    const UI::UICommittedStructureView structure = tree.context->committedStructure();
    const UI::UICommittedLayoutView layout = tree.context->committedLayout();
    const UI::UICommittedHitView hit = tree.context->committedHit();
    const UI::UIContextStatistics before = tree.context->statistics();
    auto routed = tree.context->routePointerInput(makePointerInput(window));
    ASSERT_FALSE(routed.has_value());
    EXPECT_EQ(routed.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(callbackCount, 0U);
    EXPECT_EQ(tree.context->committedStructure().revision(), structure.revision());
    EXPECT_EQ(tree.context->committedLayout().layoutRevision(), layout.layoutRevision());
    EXPECT_EQ(tree.context->committedHit().hitRevision(), hit.hitRevision());
    EXPECT_EQ(
        tree.context->statistics().activeRoutedPointerListenerCount,
        before.activeRoutedPointerListenerCount);
    const UI::UIPointerHitQueryResult query =
        tree.context->queryPointerHit({.x = 10.0F, .y = 10.0F});
    EXPECT_TRUE(query.hasTarget());
    EXPECT_EQ(query.target.node, tree.target);
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

    auto routed = tree.context->routePointerInput(makePointerInput(window));
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

    auto routed = tree.context->routePointerInput(makePointerInput(window));
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
            Core::Status structure = state.context->commitStructure();
            Core::Status layout = state.context->commitLayout(
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

    auto routed = tree.context->routePointerInput(makePointerInput(window));
    ASSERT_TRUE(routed.has_value()) << (routed ? "" : routed.error().message);
    EXPECT_EQ(state.structureError, UI::UIErrorCode::PointerRouteAlreadyInProgress);
    EXPECT_EQ(state.layoutError, UI::UIErrorCode::PointerRouteAlreadyInProgress);
    EXPECT_EQ(state.laterListenerCount, 1U);
    EXPECT_EQ(routed->listenerInvocationCount, 2U);
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
            (void)context->routePointerInput(makePointerInput(window));
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

            (void)context->addRoutedPointerListener(
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

    const UI::UICommittedStructureView structure = tree.context->committedStructure();
    const UI::UICommittedLayoutView layout = tree.context->committedLayout();
    const UI::UICommittedHitView hit = tree.context->committedHit();
    const UI::UIContextStatistics before = tree.context->statistics();
    const usize allocationCount = resource.allocationCount();
    const UI::UICommittedNodeEntry* const structureData = structure.entries().data();
    const UI::UICommittedLayoutEntry* const layoutData = layout.entries().data();
    const UI::UICommittedHitEntry* const hitData = hit.entries().data();

    for (u64 frame = 1; frame <= 300; ++frame) {
        auto routed = tree.context->routePointerInput(makePointerInput(window, frame));
        ASSERT_TRUE(routed.has_value())
            << (routed ? "" : routed.error().message);
        EXPECT_EQ(routed->listenerInvocationCount, 1U);
        EXPECT_FALSE(routed->targetInvalidated);
    }

    EXPECT_EQ(callbackCount, 300U);
    EXPECT_EQ(resource.allocationCount(), allocationCount);
    EXPECT_EQ(tree.context->committedStructure().entries().data(), structureData);
    EXPECT_EQ(tree.context->committedLayout().entries().data(), layoutData);
    EXPECT_EQ(tree.context->committedHit().entries().data(), hitData);
    const UI::UIContextStatistics after = tree.context->statistics();
    EXPECT_EQ(after.committedRevision, before.committedRevision);
    EXPECT_EQ(after.layoutRevision, before.layoutRevision);
    EXPECT_EQ(after.hitRevision, before.hitRevision);
    EXPECT_EQ(after.paintOrderRevision, before.paintOrderRevision);
    EXPECT_EQ(after.activeRoutedPointerListenerCount,
              before.activeRoutedPointerListenerCount);
    EXPECT_EQ(after.dirty, before.dirty);
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
            auto nested = state.context->routePointerInput(state.input);
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

    auto outer = tree.context->routePointerInput(state.input);
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
