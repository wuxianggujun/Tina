#include <gtest/gtest.h>

#include "ui/UIAction.hpp"

#include <memory>
#include <stdexcept>

namespace Tina::UI {
namespace {

void invokeHandler(const UIAction::Handler& handler)
{
    if (handler) handler();
}

TEST(UIActionTest, AllowsNestedActionsButRejectsSameActionRecursion)
{
    UIAction first;
    UIAction second;
    int firstCalls = 0;
    int secondCalls = 0;

    second.setHandler([&secondCalls]() { ++secondCalls; });
    first.setHandler([&]() {
        ++firstCalls;
        EXPECT_EQ(second.dispatch(invokeHandler),
                  UIActionDispatchResult::Dispatched);
        EXPECT_EQ(first.dispatch(invokeHandler),
                  UIActionDispatchResult::Reentrant);
    });

    EXPECT_EQ(first.dispatch(invokeHandler),
              UIActionDispatchResult::Dispatched);
    EXPECT_EQ(firstCalls, 1);
    EXPECT_EQ(secondCalls, 1);
}

TEST(UIActionTest, RestoresDispatchStateAfterHandlerException)
{
    UIAction action;
    int calls = 0;
    action.setHandler([&calls]() {
        if (++calls == 1) throw std::runtime_error("expected test failure");
    });

    EXPECT_THROW(action.dispatch(invokeHandler), std::runtime_error);
    EXPECT_EQ(action.dispatch(invokeHandler),
              UIActionDispatchResult::Dispatched);
    EXPECT_EQ(calls, 2);
}

TEST(UIActionTest, CallbackMayDestroyOwningAction)
{
    auto action = std::make_unique<UIAction>();
    action->setHandler([&action]() { action.reset(); });

    EXPECT_EQ(action->dispatch(invokeHandler),
              UIActionDispatchResult::Dispatched);
    EXPECT_EQ(action, nullptr);
}

} // namespace
} // namespace Tina::UI
