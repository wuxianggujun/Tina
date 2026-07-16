#include <gtest/gtest.h>

#include "engine/EventSystem.hpp"
#include "engine/UIEvents.hpp"
#include "ui/UILayoutManager.hpp"
#include "ui/UINode.hpp"
#include "ui/UIScrollView.hpp"
#include "ui/UIUtils.hpp"
#include "ui/UIVirtualList.hpp"

namespace Tina::UI::Tests {
namespace {

class TrackingNode final : public UINode {
public:
    explicit TrackingNode(const std::string& name)
        : UINode(name)
    {
        setInteractable(true);
        setClickable(true);
        setHoverable(true);
    }

    int clickCount = 0;
    int layoutCount = 0;
    int mouseUpCount = 0;
    int pointerMoveCount = 0;
    int focusGainedCount = 0;
    int focusLostCount = 0;
    int captureGainedCount = 0;
    int captureLostCount = 0;
    int keyPressedCount = 0;
    Engine::KeyCode lastKey = Engine::KeyCode::Unknown;
    bool consumeKey = false;
    bool keyboardActivatable = false;

    void onClick() override { ++clickCount; }
    void onMouseUp(float, float) override { ++mouseUpCount; }
    void onPointerMove(float, float) override { ++pointerMoveCount; }
    void onFocusGained() override { ++focusGainedCount; }
    void onFocusLost() override { ++focusLostCount; }
    bool onKeyPressed(Engine::KeyCode key, bool, bool, bool, bool) override {
        ++keyPressedCount;
        lastKey = key;
        return consumeKey;
    }
    bool supportsKeyboardActivation() const override {
        return keyboardActivatable;
    }
    void onPointerCaptureChanged(bool captured) override {
        captured ? ++captureGainedCount : ++captureLostCount;
    }

protected:
    void onLayout() override { ++layoutCount; }
};

TEST(UIRuntimeTest, HitTestNeverPerformsImplicitLayout)
{
    UILayoutManager layouts;
    TrackingNode node("Node");
    node.setLayoutManager(&layouts);
    node.setPosition(10.0f, 20.0f);
    node.setSize(100.0f, 50.0f);
    node.requestLayout();

    EXPECT_TRUE(node.containsPoint(20.0f, 30.0f));
    EXPECT_EQ(node.layoutCount, 0);
    EXPECT_GT(layouts.getPendingLayoutCount(), 0U);

    layouts.performPendingLayouts();
    EXPECT_EQ(node.layoutCount, 1);
    EXPECT_EQ(layouts.getPendingLayoutCount(), 0U);
}

TEST(UIRuntimeTest, PointerClickTargetsOnlyTopmostNode)
{
    Engine::EventSystem events;
    ASSERT_TRUE(events.initialize());

    auto root = Memory::MakeUnique<UINode>("Root");
    root->setSize(200.0f, 200.0f);

    auto bottom = Memory::MakeUnique<TrackingNode>("Bottom");
    bottom->setSize(100.0f, 100.0f);
    bottom->setZIndex(1);
    TrackingNode* bottomNode = root->addChild(std::move(bottom));

    auto top = Memory::MakeUnique<TrackingNode>("Top");
    top->setSize(100.0f, 100.0f);
    top->setZIndex(2);
    TrackingNode* topNode = root->addChild(std::move(top));

    events.setUIRoots({root.get()});
    events.updateUIInput(25.0f, 25.0f, true);
    events.updateUIInput(25.0f, 25.0f, false);

    EXPECT_EQ(topNode->clickCount, 1);
    EXPECT_EQ(bottomNode->clickCount, 0);
}

TEST(UIRuntimeTest, RoutedEventUsesCaptureTargetBubbleOrder)
{
    Engine::EventSystem events;
    ASSERT_TRUE(events.initialize());

    auto root = Memory::MakeUnique<UINode>("Root");
    auto parent = Memory::MakeUnique<UINode>("Parent");
    auto target = Memory::MakeUnique<UINode>("Target");
    UINode* targetNode = target.get();
    parent->addChild(std::move(target));
    root->addChild(std::move(parent));
    events.setUIRoots({root.get()});

    struct Visit {
        Engine::UIEventPhase phase;
        UINode* node;
        NodeId nodeId;
    };
    Container::Vector<Visit> visits;
    auto token = events.subscribe<Engine::ButtonClickEvent>(
        [&visits](const Engine::ButtonClickEvent& event) {
            visits.push_back({event.phase, event.currentTarget, event.currentTargetId});
        });

    Engine::ButtonClickEvent click(7, "Target");
    events.triggerUIEvent(click, targetNode);

    ASSERT_EQ(visits.size(), 5U);
    EXPECT_EQ(visits[0].phase, Engine::UIEventPhase::Capture);
    EXPECT_EQ(visits[0].node, root.get());
    EXPECT_EQ(visits[0].nodeId, root->nodeId());
    EXPECT_EQ(visits[1].phase, Engine::UIEventPhase::Capture);
    EXPECT_EQ(visits[2].phase, Engine::UIEventPhase::Target);
    EXPECT_EQ(visits[2].node, targetNode);
    EXPECT_EQ(visits[2].nodeId, targetNode->nodeId());
    EXPECT_EQ(visits[3].phase, Engine::UIEventPhase::Bubble);
    EXPECT_EQ(visits[4].phase, Engine::UIEventPhase::Bubble);
    EXPECT_EQ(visits[4].node, root.get());
}

TEST(UIRuntimeTest, StaleNodeIdNeverResolvesAfterSlotReuse)
{
    Engine::EventSystem events;
    UINode first("First");
    first.setEventSystem(&events);
    const NodeId staleId = first.nodeId();
    ASSERT_EQ(events.resolveUINode(staleId), &first);

    first.setEventSystem(nullptr);
    EXPECT_EQ(events.resolveUINode(staleId), nullptr);

    UINode replacement("Replacement");
    replacement.setEventSystem(&events);
    const NodeId replacementId = replacement.nodeId();

    EXPECT_EQ(replacementId.index, staleId.index);
    EXPECT_NE(replacementId.generation, staleId.generation);
    EXPECT_EQ(events.resolveUINode(staleId), nullptr);
    EXPECT_EQ(events.resolveUINode(replacementId), &replacement);
}

TEST(UIRuntimeTest, NodeCanOutliveItsWindowEventContextSafely)
{
    UINode node("LongLivedNode");
    NodeId oldId;
    {
        Engine::EventSystem events;
        node.setEventSystem(&events);
        oldId = node.nodeId();
        ASSERT_TRUE(oldId);
        ASSERT_EQ(node.eventSystem(), &events);
    }

    EXPECT_EQ(node.eventSystem(), nullptr);
    EXPECT_NO_FATAL_FAILURE(node.setEventSystem(nullptr));
    EXPECT_FALSE(node.nodeId());
}

TEST(UIRuntimeTest, RemovingFocusedCapturedNodeInvalidatesAllInteractionHandles)
{
    Engine::EventSystem events;
    ASSERT_TRUE(events.initialize());

    auto root = Memory::MakeUnique<UINode>("Root");
    root->setSize(200.0f, 200.0f);
    root->setInteractable(false);

    auto child = Memory::MakeUnique<TrackingNode>("Child");
    child->setSize(100.0f, 100.0f);
    child->setFocusable(true);
    TrackingNode* childNode = root->addChild(std::move(child));
    events.setUIRoots({root.get()});

    const NodeId staleId = childNode->nodeId();
    events.updateUIInput(25.0f, 25.0f, true);
    ASSERT_EQ(events.focusedNodeId(), staleId);
    ASSERT_EQ(events.capturedNodeId(), staleId);

    auto removed = root->removeChild(childNode);
    ASSERT_NE(removed, nullptr);

    EXPECT_EQ(events.resolveUINode(staleId), nullptr);
    EXPECT_FALSE(events.focusedNodeId());
    EXPECT_FALSE(events.capturedNodeId());
    EXPECT_FALSE(events.pressedNodeId());
    EXPECT_FALSE(events.hoveredNodeId());
    EXPECT_EQ(static_cast<TrackingNode*>(removed.get())->focusLostCount, 1);
    EXPECT_EQ(static_cast<TrackingNode*>(removed.get())->captureLostCount, 1);
}

TEST(UIRuntimeTest, RemoveFromParentKeepsNodeAliveUntilDetachCompletes)
{
    class LifetimeNode final : public UINode {
    public:
        explicit LifetimeNode(bool& destroyed)
            : UINode("LifetimeNode"), m_destroyed(destroyed) {}
        ~LifetimeNode() override { m_destroyed = true; }

    private:
        bool& m_destroyed;
    };

    auto root = Memory::MakeUnique<UINode>("Root");
    bool destroyed = false;
    auto child = Memory::MakeUnique<LifetimeNode>(destroyed);
    LifetimeNode* childNode = root->addChild(std::move(child));

    ASSERT_FALSE(destroyed);
    childNode->removeFromParent();

    EXPECT_TRUE(destroyed);
    EXPECT_EQ(root->getChildCount(), 0U);
}

TEST(UIRuntimeTest, PointerCaptureDeliversMoveAndReleaseOutsideWithoutClick)
{
    Engine::EventSystem events;
    ASSERT_TRUE(events.initialize());

    auto root = Memory::MakeUnique<UINode>("Root");
    root->setSize(200.0f, 200.0f);
    root->setInteractable(false);

    auto child = Memory::MakeUnique<TrackingNode>("Child");
    child->setSize(50.0f, 50.0f);
    TrackingNode* childNode = root->addChild(std::move(child));
    events.setUIRoots({root.get()});

    events.updateUIInput(25.0f, 25.0f, true);
    ASSERT_EQ(events.capturedNodeId(), childNode->nodeId());

    events.updateUIInput(150.0f, 150.0f, true);
    events.updateUIInput(150.0f, 150.0f, false);

    EXPECT_EQ(childNode->pointerMoveCount, 1);
    EXPECT_EQ(childNode->mouseUpCount, 1);
    EXPECT_EQ(childNode->clickCount, 0);
    EXPECT_EQ(childNode->captureGainedCount, 1);
    EXPECT_EQ(childNode->captureLostCount, 1);
    EXPECT_FALSE(events.capturedNodeId());
}

TEST(UIRuntimeTest, FocusTraversalUsesTreeOrderAndSupportsReverse)
{
    Engine::EventSystem events;
    ASSERT_TRUE(events.initialize());

    auto root = Memory::MakeUnique<UINode>("Root");
    auto first = Memory::MakeUnique<TrackingNode>("First");
    first->setFocusable(true);
    TrackingNode* firstNode = root->addChild(std::move(first));
    auto second = Memory::MakeUnique<TrackingNode>("Second");
    second->setFocusable(true);
    TrackingNode* secondNode = root->addChild(std::move(second));
    events.setUIRoots({root.get()});

    ASSERT_TRUE(events.focusNext());
    EXPECT_EQ(events.focusedNodeId(), firstNode->nodeId());
    ASSERT_TRUE(events.focusNext());
    EXPECT_EQ(events.focusedNodeId(), secondNode->nodeId());
    ASSERT_TRUE(events.focusNext(true));
    EXPECT_EQ(events.focusedNodeId(), firstNode->nodeId());

    EXPECT_EQ(firstNode->focusGainedCount, 2);
    EXPECT_EQ(firstNode->focusLostCount, 1);
    EXPECT_EQ(secondNode->focusGainedCount, 1);
    EXPECT_EQ(secondNode->focusLostCount, 1);
}

TEST(UIRuntimeTest, FocusedKeyUsesCaptureTargetBubbleAndRunsDefaultActivationOnce)
{
    Engine::EventSystem events;
    ASSERT_TRUE(events.initialize());

    auto root = Memory::MakeUnique<UINode>("Root");
    auto parent = Memory::MakeUnique<UINode>("Parent");
    auto target = Memory::MakeUnique<TrackingNode>("KeyboardTarget");
    target->setFocusable(true);
    target->keyboardActivatable = true;
    TrackingNode* targetNode = target.get();
    UINode* parentNode = parent.get();
    parent->addChild(std::move(target));
    root->addChild(std::move(parent));
    events.setUIRoots({root.get()});
    ASSERT_TRUE(events.setKeyboardFocus(targetNode->nodeId()));

    struct Visit {
        Engine::UIEventPhase phase;
        UINode* node;
    };
    Container::Vector<Visit> visits;
    auto routeToken = events.subscribe<Engine::UIKeyPressedEvent>(
        [&visits](const Engine::UIKeyPressedEvent& event) {
            visits.push_back({event.phase, event.currentTarget});
        });

    EXPECT_TRUE(events.dispatchKeyPressedToFocused(Engine::KeyCode::Space));
    ASSERT_EQ(visits.size(), 5U);
    EXPECT_EQ(visits[0].phase, Engine::UIEventPhase::Capture);
    EXPECT_EQ(visits[0].node, root.get());
    EXPECT_EQ(visits[1].node, parentNode);
    EXPECT_EQ(visits[2].phase, Engine::UIEventPhase::Target);
    EXPECT_EQ(visits[2].node, targetNode);
    EXPECT_EQ(visits[4].phase, Engine::UIEventPhase::Bubble);
    EXPECT_EQ(visits[4].node, root.get());
    EXPECT_EQ(targetNode->keyPressedCount, 1);
    EXPECT_EQ(targetNode->lastKey, Engine::KeyCode::Space);
    EXPECT_EQ(targetNode->clickCount, 1);

    visits.clear();
    EXPECT_FALSE(events.dispatchKeyPressedToFocused(
        Engine::KeyCode::Space, true));
    EXPECT_EQ(targetNode->keyPressedCount, 2);
    EXPECT_EQ(targetNode->clickCount, 1);
}

TEST(UIRuntimeTest, FocusedKeyDefaultCanBeCancelledWithoutStoppingPropagation)
{
    Engine::EventSystem events;
    ASSERT_TRUE(events.initialize());

    auto root = Memory::MakeUnique<UINode>("Root");
    auto target = Memory::MakeUnique<TrackingNode>("KeyboardTarget");
    target->setFocusable(true);
    target->keyboardActivatable = true;
    TrackingNode* targetNode = root->addChild(std::move(target));
    events.setUIRoots({root.get()});
    ASSERT_TRUE(events.setKeyboardFocus(targetNode->nodeId()));

    int bubbleVisits = 0;
    auto token = events.subscribe<Engine::UIKeyPressedEvent>(
        [&bubbleVisits](const Engine::UIKeyPressedEvent& event) {
            if (event.phase == Engine::UIEventPhase::Capture &&
                event.currentTarget == event.target->getParent()) {
                event.preventDefault();
            }
            if (event.phase == Engine::UIEventPhase::Bubble) ++bubbleVisits;
        });

    EXPECT_TRUE(events.dispatchKeyPressedToFocused(Engine::KeyCode::Enter));
    EXPECT_EQ(targetNode->keyPressedCount, 0);
    EXPECT_EQ(targetNode->clickCount, 0);
    EXPECT_EQ(bubbleVisits, 1);
}

TEST(UIRuntimeTest, FocusedKeyRouteRevalidatesGenerationAfterTargetRemoval)
{
    Engine::EventSystem events;
    ASSERT_TRUE(events.initialize());

    auto root = Memory::MakeUnique<UINode>("Root");
    auto target = Memory::MakeUnique<TrackingNode>("RemovedDuringKeyRoute");
    target->setFocusable(true);
    TrackingNode* targetNode = root->addChild(std::move(target));
    events.setUIRoots({root.get()});
    const NodeId staleId = targetNode->nodeId();
    ASSERT_TRUE(events.setKeyboardFocus(staleId));

    bool removedDuringCapture = false;
    auto token = events.subscribe<Engine::UIKeyPressedEvent>(
        [&root, &removedDuringCapture](const Engine::UIKeyPressedEvent& event) {
            if (event.phase != Engine::UIEventPhase::Capture ||
                event.currentTarget != root.get()) {
                return;
            }
            auto removed = root->removeChild(event.target);
            removedDuringCapture = removed != nullptr;
        });

    EXPECT_TRUE(events.dispatchKeyPressedToFocused(Engine::KeyCode::Enter));
    EXPECT_TRUE(removedDuringCapture);
    EXPECT_EQ(events.resolveUINode(staleId), nullptr);
    EXPECT_FALSE(events.focusedNodeId());
}

TEST(UIRuntimeTest, EventContextIsInheritedByDynamicChildren)
{
    Engine::EventSystem events;
    UINode root("Root");
    root.setEventSystem(&events);

    auto child = Memory::MakeUnique<UINode>("Child");
    UINode* childNode = root.addChild(std::move(child));
    auto grandchild = Memory::MakeUnique<UINode>("Grandchild");
    UINode* grandchildNode = childNode->addChild(std::move(grandchild));

    EXPECT_EQ(childNode->eventSystem(), &events);
    EXPECT_EQ(grandchildNode->eventSystem(), &events);

    root.setEventSystem(nullptr);
    EXPECT_EQ(childNode->eventSystem(), nullptr);
    EXPECT_EQ(grandchildNode->eventSystem(), nullptr);
}

TEST(UIRuntimeTest, ThemeAndDpiStateAreIsolatedPerWindowContext)
{
    Engine::EventSystem firstWindow;
    Engine::EventSystem secondWindow;

    const auto firstRevision = firstWindow.windowUIContext().scaleRevision();
    ASSERT_TRUE(firstWindow.updateUIViewport(1280, 720, 2560, 1440));
    const auto& viewport = firstWindow.windowUIContext().viewport();
    EXPECT_FLOAT_EQ(viewport.framebufferScaleX, 2.0f);
    EXPECT_FLOAT_EQ(viewport.framebufferScaleY, 2.0f);
    EXPECT_FLOAT_EQ(viewport.toFramebufferX(100.0f), 200.0f);
    EXPECT_FLOAT_EQ(viewport.toFramebufferY(75.0f), 150.0f);
    EXPECT_FLOAT_EQ(viewport.dp(8.0f), 16.0f);
    EXPECT_EQ(viewport.fontPx(16), 32);
    EXPECT_FLOAT_EQ(UIUtils::calculateCanvasScale(firstWindow.windowUIContext()), 2.0f);
    EXPECT_FLOAT_EQ(UIUtils::calculateUIScale(firstWindow.windowUIContext()), 2.0f);
    EXPECT_GT(firstWindow.windowUIContext().scaleRevision(), firstRevision);

    const auto stableRevision = firstWindow.windowUIContext().scaleRevision();
    EXPECT_FALSE(firstWindow.updateUIViewport(1280, 720, 2560, 1440));
    EXPECT_EQ(firstWindow.windowUIContext().scaleRevision(), stableRevision);

    ASSERT_TRUE(firstWindow.setUIUserScale(1.25f));
    EXPECT_FLOAT_EQ(firstWindow.windowUIContext().viewport().effectiveScale(), 2.5f);
    EXPECT_FLOAT_EQ(secondWindow.windowUIContext().viewport().effectiveScale(), 1.0f);

    firstWindow.setUITheme(UITheme::light());
    EXPECT_EQ(firstWindow.windowUIContext().theme().kind(), UIThemeKind::Light);
    EXPECT_EQ(secondWindow.windowUIContext().theme().kind(), UIThemeKind::Dark);
    EXPECT_LT(firstWindow.windowUIContext().theme().style(UIStyleRole::Label).textColor.r(),
              secondWindow.windowUIContext().theme().style(UIStyleRole::Label).textColor.r());
}

TEST(UIRuntimeTest, LogicalPointerCoordinatesHitFramebufferSpaceNodesAtHighDpi)
{
    Engine::EventSystem events;
    ASSERT_TRUE(events.initialize());
    ASSERT_TRUE(events.updateUIViewport(100, 100, 200, 200));

    auto root = Memory::MakeUnique<UINode>("Root");
    root->setSize(200.0f, 200.0f);
    root->setInteractable(false);

    auto child = Memory::MakeUnique<TrackingNode>("HighDpiTarget");
    child->setPosition(80.0f, 80.0f);
    child->setSize(40.0f, 40.0f);
    TrackingNode* childNode = root->addChild(std::move(child));
    events.setUIRoots({root.get()});

    events.updateUIInputLogical(50.0f, 50.0f, true);
    events.updateUIInputLogical(50.0f, 50.0f, false);

    EXPECT_EQ(childNode->clickCount, 1);
}

TEST(UIRuntimeTest, ClippedChildrenCannotReceivePointerOutsideViewport)
{
    Engine::EventSystem events;
    ASSERT_TRUE(events.initialize());

    auto root = Memory::MakeUnique<UINode>("Root");
    root->setSize(300.0f, 300.0f);
    root->setInteractable(false);

    auto viewport = Memory::MakeUnique<UINode>("Viewport");
    viewport->setSize(100.0f, 100.0f);
    viewport->setInteractable(false);
    viewport->setClipChildren(true);
    UINode* viewportNode = root->addChild(std::move(viewport));

    auto child = Memory::MakeUnique<TrackingNode>("OutsideChild");
    child->setPosition(0.0f, 150.0f);
    child->setSize(50.0f, 50.0f);
    TrackingNode* childNode = viewportNode->addChild(std::move(child));
    events.setUIRoots({root.get()});

    events.updateUIInput(25.0f, 175.0f, true);
    events.updateUIInput(25.0f, 175.0f, false);
    EXPECT_EQ(childNode->clickCount, 0);

    viewportNode->setClipChildren(false);
    events.updateUIInput(25.0f, 175.0f, true);
    events.updateUIInput(25.0f, 175.0f, false);
    EXPECT_EQ(childNode->clickCount, 1);
}

TEST(UIRuntimeTest, ScrollViewClampsContentAndReceivesWheelFromDescendant)
{
    Engine::EventSystem events;
    ASSERT_TRUE(events.initialize());

    auto scroll = Memory::MakeUnique<UIScrollView>("Scroll");
    scroll->setSize(100.0f, 100.0f);
    scroll->setContentSize(100.0f, 300.0f);
    scroll->setSmoothScroll(false);
    UIScrollView* scrollNode = scroll.get();

    auto button = Memory::MakeUnique<TrackingNode>("ContentButton");
    button->setSize(50.0f, 50.0f);
    scrollNode->content()->addChild(std::move(button));
    events.setUIRoots({scrollNode});

    events.updateUIInput(25.0f, 25.0f, false, -1.0f);
    scrollNode->update(0.0f);
    EXPECT_FLOAT_EQ(scrollNode->scrollOffset().y, 48.0f);

    scrollNode->scrollTo(0.0f, 1000.0f);
    EXPECT_FLOAT_EQ(scrollNode->scrollOffset().y, 200.0f);
    EXPECT_FLOAT_EQ(scrollNode->content()->getPosition().y, -200.0f);

    scrollNode->setContentSize(100.0f, 50.0f);
    EXPECT_FLOAT_EQ(scrollNode->scrollOffset().y, 0.0f);
    EXPECT_FLOAT_EQ(scrollNode->targetScrollOffset().y, 0.0f);
}

TEST(UIRuntimeTest, VirtualListRangeStaysBoundedForLargeDataSets)
{
    const UIVisibleRange range = calculateVisibleRows(
        100000, 32.0f, 96.0f, 3200.0f, 1);

    EXPECT_EQ(range.first, 99U);
    EXPECT_EQ(range.end, 104U);
    EXPECT_EQ(range.count(), 5U);
    EXPECT_LT(range.count(), 100000U);

    const UIVisibleRange tail = calculateVisibleRows(
        100000, 32.0f, 96.0f, 99999999.0f, 1);
    EXPECT_EQ(tail.end, 100000U);
    EXPECT_LE(tail.count(), 4U);
}

} // namespace
} // namespace Tina::UI::Tests
