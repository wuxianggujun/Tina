#include <gtest/gtest.h>

#include "engine/EventSystem.hpp"
#include "engine/UIEvents.hpp"
#include "ui/UILayoutManager.hpp"
#include "ui/UINode.hpp"

#include <array>

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

    void onClick() override { ++clickCount; }

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

    struct Visit {
        Engine::UIEventPhase phase;
        UINode* node;
    };
    Container::Vector<Visit> visits;
    auto token = events.subscribe<Engine::ButtonClickEvent>(
        [&visits](const Engine::ButtonClickEvent& event) {
            visits.push_back({event.phase, event.currentTarget});
        });

    Engine::ButtonClickEvent click(7, "Target");
    events.triggerUIEvent(click, targetNode);

    ASSERT_EQ(visits.size(), 5U);
    EXPECT_EQ(visits[0].phase, Engine::UIEventPhase::Capture);
    EXPECT_EQ(visits[0].node, root.get());
    EXPECT_EQ(visits[1].phase, Engine::UIEventPhase::Capture);
    EXPECT_EQ(visits[2].phase, Engine::UIEventPhase::Target);
    EXPECT_EQ(visits[2].node, targetNode);
    EXPECT_EQ(visits[3].phase, Engine::UIEventPhase::Bubble);
    EXPECT_EQ(visits[4].phase, Engine::UIEventPhase::Bubble);
    EXPECT_EQ(visits[4].node, root.get());
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

} // namespace
} // namespace Tina::UI::Tests
