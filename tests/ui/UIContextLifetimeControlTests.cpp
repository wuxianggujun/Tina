#include <gtest/gtest.h>

#include "detail/UIContextLifetimeControl.hpp"

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;
using LifetimeControl = UI::Detail::UIContextLifetimeControl;
using ListenerRelease = UI::Detail::DeferredRoutedPointerListenerRelease;

class UIContextLifetimeControlTests : public testing::Test {
protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(1);
        ASSERT_TRUE(windowsResult.has_value());
        windows_ = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto windowResult = windows_->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());

        UI::UIContextCapacityConfig capacities{
            .nodeCapacity = 4,
            .rootCapacity = RootCapacity,
            .routedPointerListenerCapacity = ListenerCapacity,
        };
        capacities.applyDefaultProductChrome = false;
        auto contextResult = UI::UIContext::Create(*windowResult, capacities);
        ASSERT_TRUE(contextResult.has_value())
            << (contextResult ? "" : contextResult.error().message);
        context_ = std::move(*contextResult);
        auto rootResult = context_->authoring().rootBuilder().createRoot();
        ASSERT_TRUE(rootResult.has_value())
            << (rootResult ? "" : rootResult.error().message);
        root_ = std::move(*rootResult);

        control_ = std::make_unique<LifetimeControl>(
            std::this_thread::get_id(), RootCapacity, ListenerCapacity);
        control_->attach(*context_);
        deferredRoots_.reserve(RootCapacity);
        deferredListeners_.reserve(ListenerCapacity);
    }

    static constexpr usize RootCapacity = 2;
    static constexpr usize ListenerCapacity = 2;
    std::unique_ptr<WindowPool> windows_;
    std::unique_ptr<UI::UIContext> context_;
    UI::UIRootOwner root_;
    std::unique_ptr<LifetimeControl> control_;
    std::vector<UI::UINodeId> deferredRoots_;
    std::vector<ListenerRelease> deferredListeners_;
};

TEST_F(UIContextLifetimeControlTests, AttachPublishAndDetachInvalidateEveryToken)
{
    ASSERT_EQ(control_->attachedContext(), context_.get());
    control_->publishRoutedPointerListenerState(0, 1, true);
    control_->publishRoutedPointerListenerState(1, 9, true);
    EXPECT_TRUE(control_->isRoutedPointerListenerActive(0, 1));
    EXPECT_TRUE(control_->isRoutedPointerListenerActive(1, 9));

    control_->detach(*context_);
    EXPECT_EQ(control_->attachedContext(), nullptr);
    EXPECT_FALSE(control_->isRoutedPointerListenerActive(0, 1));
    EXPECT_FALSE(control_->isRoutedPointerListenerActive(1, 9));
    EXPECT_EQ(control_->releaseRoot(root_.rootNodeId()), nullptr);
}

TEST_F(UIContextLifetimeControlTests, OwnerThreadReleaseReturnsContextWithoutQueueing)
{
    control_->publishRoutedPointerListenerState(0, 3, true);
    EXPECT_EQ(control_->releaseRoutedPointerListener(0, 3), context_.get());
    EXPECT_FALSE(control_->isRoutedPointerListenerActive(0, 3));
    EXPECT_EQ(control_->releaseRoot(root_.rootNodeId()), context_.get());

    control_->takeDeferredRoutedPointerListenerReleases(deferredListeners_);
    control_->takeDeferredRootDestroys(deferredRoots_);
    EXPECT_TRUE(deferredListeners_.empty());
    EXPECT_TRUE(deferredRoots_.empty());
}

TEST_F(UIContextLifetimeControlTests, DetachDropsAlreadyQueuedWorkerReleases)
{
    control_->publishRoutedPointerListenerState(0, 4, true);
    std::thread worker([&] {
        static_cast<void>(
            control_->releaseRoutedPointerListener(0, 4));
        static_cast<void>(control_->releaseRoot(root_.rootNodeId()));
    });
    worker.join();

    control_->detach(*context_);
    control_->takeDeferredRoutedPointerListenerReleases(deferredListeners_);
    control_->takeDeferredRootDestroys(deferredRoots_);
    EXPECT_TRUE(deferredListeners_.empty());
    EXPECT_TRUE(deferredRoots_.empty());
    EXPECT_FALSE(control_->isRoutedPointerListenerActive(0, 4));
}

TEST_F(UIContextLifetimeControlTests, WorkerThreadReleaseQueuesAndDrainsWithoutLosingCapacity)
{
    control_->publishRoutedPointerListenerState(0, 5, true);
    UI::UIContext* listenerContext = context_.get();
    UI::UIContext* rootContext = context_.get();
    std::thread worker([&] {
        listenerContext = control_->releaseRoutedPointerListener(0, 5);
        rootContext = control_->releaseRoot(root_.rootNodeId());
    });
    worker.join();
    EXPECT_EQ(listenerContext, nullptr);
    EXPECT_EQ(rootContext, nullptr);

    control_->takeDeferredRoutedPointerListenerReleases(deferredListeners_);
    control_->takeDeferredRootDestroys(deferredRoots_);
    ASSERT_EQ(deferredListeners_.size(), 1U);
    EXPECT_EQ(deferredListeners_[0].slot, 0U);
    EXPECT_EQ(deferredListeners_[0].generation, 5U);
    ASSERT_EQ(deferredRoots_.size(), 1U);
    EXPECT_EQ(deferredRoots_[0], root_.rootNodeId());

    control_->publishRoutedPointerListenerState(1, 7, true);
    std::thread secondWorker([&] {
        static_cast<void>(
            control_->releaseRoutedPointerListener(1, 7));
    });
    secondWorker.join();
    control_->takeDeferredRoutedPointerListenerReleases(deferredListeners_);
    ASSERT_EQ(deferredListeners_.size(), 1U);
    EXPECT_EQ(deferredListeners_[0].slot, 1U);
    EXPECT_EQ(deferredListeners_[0].generation, 7U);
}

TEST_F(UIContextLifetimeControlTests, StaleGenerationCannotDeactivateReusedSlot)
{
    control_->publishRoutedPointerListenerState(0, 11, true);
    control_->publishRoutedPointerListenerState(0, 12, true);
    control_->publishRoutedPointerListenerState(0, 11, false);
    EXPECT_TRUE(control_->isRoutedPointerListenerActive(0, 12));

    EXPECT_EQ(control_->releaseRoutedPointerListener(0, 11), nullptr);
    EXPECT_TRUE(control_->isRoutedPointerListenerActive(0, 12));
    EXPECT_EQ(control_->releaseRoutedPointerListener(0, 12), context_.get());
    EXPECT_FALSE(control_->isRoutedPointerListenerActive(0, 12));
}

} // namespace
} // namespace Tina::Tests
