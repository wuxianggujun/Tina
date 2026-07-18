#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/ui/UIContext.hpp>

#include "../../../src/runtime/ui/PrimaryWindowUICapabilityState.hpp"

#include <memory>
#include <optional>
#include <thread>
#include <utility>

namespace Tina::Tests {
namespace {

using CapabilityState = Runtime::Detail::PrimaryWindowUICapabilityState;
using CapabilityPhase = Runtime::Detail::PrimaryWindowUIPhase;
using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

class PrimaryWindowUICapabilityTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto poolResult = WindowPool::Create(1);
        ASSERT_TRUE(poolResult.has_value()) << poolResult.error().message;
        windowPool.emplace(std::move(*poolResult));
        auto windowResult = windowPool->tryEmplace(0);
        ASSERT_TRUE(windowResult.has_value()) << windowResult.error().message;
        window = *windowResult;

        auto contextResult = UI::UIContext::Create(window, {.nodeCapacity = 16, .rootCapacity = 4});
        ASSERT_TRUE(contextResult.has_value()) << contextResult.error().message;
        context = std::move(*contextResult);
    }

    std::optional<WindowPool> windowPool;
    Platform::WindowId window{};
    std::unique_ptr<UI::UIContext> context;
};

TEST_F(PrimaryWindowUICapabilityTest, EnterCapabilityCreatesOneRootScopedTreeAndExpiresUnconditionally)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    EXPECT_TRUE(state.hasPrimaryWindowUI(*epoch, CapabilityPhase::GameStateEnter));

    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;
    auto panel = tree->createPanel(root->rootNodeId());
    ASSERT_TRUE(panel.has_value()) << panel.error().message;
    auto label = tree->createLabel(*panel);
    ASSERT_TRUE(label.has_value()) << label.error().message;
    auto button = tree->createButton(*panel);
    ASSERT_TRUE(button.has_value()) << button.error().message;
    EXPECT_EQ(context->liveRootCount(), 1U);
    EXPECT_EQ(context->liveNodeCount(), 4U);

    ASSERT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
    EXPECT_FALSE(state.hasPrimaryWindowUI(*epoch, CapabilityPhase::GameStateEnter));

    auto expiredTree = tree->createPanel(*panel);
    ASSERT_FALSE(expiredTree.has_value());
    EXPECT_EQ(expiredTree.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto expiredBuilder = builder->createRoot();
    ASSERT_FALSE(expiredBuilder.has_value());
    EXPECT_EQ(expiredBuilder.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, AbortPhaseInvalidatesFacadesAndAllowsTheNextPhase)
{
    CapabilityState state;
    auto enterEpoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(enterEpoch.has_value()) << enterEpoch.error().message;
    auto builder = state.rootBuilder(*enterEpoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;

    state.abortPhase(*enterEpoch, CapabilityPhase::GameStateEnter);
    EXPECT_FALSE(state.hasPrimaryWindowUI(*enterEpoch, CapabilityPhase::GameStateEnter));
    auto expired = builder->createRoot();
    ASSERT_FALSE(expired.has_value());
    EXPECT_EQ(expired.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);

    auto updateEpoch = state.beginUIUpdatePhase(context.get());
    ASSERT_TRUE(updateEpoch.has_value()) << updateEpoch.error().message;
    EXPECT_TRUE(state.finishPhase(*updateEpoch, CapabilityPhase::UIUpdate).has_value());
}

TEST_F(PrimaryWindowUICapabilityTest, HeadlessRequestSticksTheUnavailableErrorUntilPhaseFinish)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(nullptr);
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    EXPECT_FALSE(state.hasPrimaryWindowUI(*epoch, CapabilityPhase::GameStateEnter));

    auto first = state.rootBuilder(*epoch);
    ASSERT_FALSE(first.has_value());
    EXPECT_EQ(first.error().code, RuntimeErrorCode::PrimaryWindowUIUnavailable);
    auto second = state.rootBuilder(*epoch);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, first.error().code);
    EXPECT_EQ(second.error().message, first.error().message);

    auto finish = state.finishPhase(*epoch, CapabilityPhase::GameStateEnter);
    ASSERT_FALSE(finish.has_value());
    EXPECT_EQ(finish.error().code, RuntimeErrorCode::PrimaryWindowUIUnavailable);
}

TEST_F(PrimaryWindowUICapabilityTest, FirstTreeFailureIsStickyAndPreventsLaterMutation)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto firstRoot = builder->createRoot();
    auto secondRoot = builder->createRoot();
    ASSERT_TRUE(firstRoot.has_value()) << firstRoot.error().message;
    ASSERT_TRUE(secondRoot.has_value()) << secondRoot.error().message;
    auto firstTree = builder->treeUpdater(*firstRoot);
    ASSERT_TRUE(firstTree.has_value()) << firstTree.error().message;

    auto crossRoot = firstTree->createPanel(secondRoot->rootNodeId());
    ASSERT_FALSE(crossRoot.has_value());
    EXPECT_EQ(crossRoot.error().code, UI::UIErrorCode::InvalidNode);
    const usize nodesAfterFailure = context->liveNodeCount();

    auto otherwiseValid = firstTree->createPanel(firstRoot->rootNodeId());
    ASSERT_FALSE(otherwiseValid.has_value());
    EXPECT_EQ(otherwiseValid.error().code, crossRoot.error().code);
    EXPECT_EQ(context->liveNodeCount(), nodesAfterFailure);

    auto finish = state.finishPhase(*epoch, CapabilityPhase::GameStateEnter);
    ASSERT_FALSE(finish.has_value());
    EXPECT_EQ(finish.error().code, UI::UIErrorCode::InvalidNode);
}

TEST_F(PrimaryWindowUICapabilityTest, UpdateCapabilityMutatesOwnedTreeThenExpires)
{
    CapabilityState state;
    auto enterEpoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(enterEpoch.has_value()) << enterEpoch.error().message;
    auto builder = state.rootBuilder(*enterEpoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto enterTree = builder->treeUpdater(*root);
    ASSERT_TRUE(enterTree.has_value()) << enterTree.error().message;
    auto panel = enterTree->createPanel(root->rootNodeId());
    ASSERT_TRUE(panel.has_value()) << panel.error().message;
    ASSERT_TRUE(state.finishPhase(*enterEpoch, CapabilityPhase::GameStateEnter).has_value());

    auto updateEpoch = state.beginUIUpdatePhase(context.get());
    ASSERT_TRUE(updateEpoch.has_value()) << updateEpoch.error().message;
    auto updateTree = state.treeUpdater(*updateEpoch, CapabilityPhase::UIUpdate, *root);
    ASSERT_TRUE(updateTree.has_value()) << updateTree.error().message;
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(320.0F);
    ASSERT_TRUE(updateTree->setLayoutStyle(*panel, style).has_value());
    auto alive = updateTree->isAlive(*panel);
    ASSERT_TRUE(alive.has_value()) << alive.error().message;
    EXPECT_TRUE(*alive);
    ASSERT_TRUE(state.finishPhase(*updateEpoch, CapabilityPhase::UIUpdate).has_value());

    auto expired = updateTree->isAlive(*panel);
    ASSERT_FALSE(expired.has_value());
    EXPECT_EQ(expired.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
}

TEST_F(PrimaryWindowUICapabilityTest, CrossThreadUseFailsWithoutPoisoningTheOwnerPhase)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builder = state.rootBuilder(*epoch);
    ASSERT_TRUE(builder.has_value()) << builder.error().message;
    auto root = builder->createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto tree = builder->treeUpdater(*root);
    ASSERT_TRUE(tree.has_value()) << tree.error().message;

    std::optional<Core::Result<bool>> crossThread;
    std::thread worker([&] { crossThread.emplace(tree->isAlive(root->rootNodeId())); });
    worker.join();
    ASSERT_TRUE(crossThread.has_value());
    ASSERT_FALSE(crossThread->has_value());
    EXPECT_EQ(crossThread->error().code, RuntimeErrorCode::WrongOwnerThread);

    auto ownerThread = tree->isAlive(root->rootNodeId());
    ASSERT_TRUE(ownerThread.has_value()) << ownerThread.error().message;
    EXPECT_TRUE(*ownerThread);
    EXPECT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
}

TEST_F(PrimaryWindowUICapabilityTest, MovedFromFacadesReportExpired)
{
    CapabilityState state;
    auto epoch = state.beginGameStateEnterPhase(context.get());
    ASSERT_TRUE(epoch.has_value()) << epoch.error().message;
    auto builderResult = state.rootBuilder(*epoch);
    ASSERT_TRUE(builderResult.has_value()) << builderResult.error().message;
    PrimaryWindowUIRootBuilder builder = std::move(*builderResult);
    PrimaryWindowUIRootBuilder movedBuilder = std::move(builder);

    auto movedFrom = builder.createRoot();
    ASSERT_FALSE(movedFrom.has_value());
    EXPECT_EQ(movedFrom.error().code, RuntimeErrorCode::UIPhaseCapabilityExpired);
    auto root = movedBuilder.createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    EXPECT_TRUE(state.finishPhase(*epoch, CapabilityPhase::GameStateEnter).has_value());
}

} // namespace
} // namespace Tina::Tests
