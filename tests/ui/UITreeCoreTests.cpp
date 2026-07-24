#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <algorithm>
#include <memory>
#include <memory_resource>
#include <thread>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

class ObservingMemoryResource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] usize allocationCount() const noexcept { return m_allocationCount; }
    [[nodiscard]] usize deallocationCount() const noexcept { return m_deallocationCount; }
    [[nodiscard]] usize currentBytes() const noexcept { return m_currentBytes; }
    [[nodiscard]] usize peakBytes() const noexcept { return m_peakBytes; }

private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        void* storage = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_allocationCount;
        m_currentBytes += bytes;
        m_peakBytes = (std::max)(m_peakBytes, m_currentBytes);
        return storage;
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        ++m_deallocationCount;
        m_currentBytes -= bytes;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize m_allocationCount = 0;
    usize m_deallocationCount = 0;
    usize m_currentBytes = 0;
    usize m_peakBytes = 0;
};

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId ownerWindow,
    UI::UIContextCapacityConfig capacities = {},
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    capacities.applyDefaultProductChrome = false;
    auto contextResult = UI::UIContext::Create(ownerWindow, capacities, resource);
    EXPECT_TRUE(contextResult.has_value())
        << (contextResult ? "" : contextResult.error().message);
    return contextResult ? std::move(*contextResult) : nullptr;
}

[[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto rootResult = context.rootBuilder().createRoot();
    EXPECT_TRUE(rootResult.has_value())
        << (rootResult ? "" : rootResult.error().message);
    return rootResult ? std::move(*rootResult) : UI::UIRootOwner{};
}

class UITreeCoreTest : public testing::Test {
protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(2);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));

        auto firstResult = windows->tryEmplace(1);
        auto secondResult = windows->tryEmplace(2);
        ASSERT_TRUE(firstResult.has_value());
        ASSERT_TRUE(secondResult.has_value());
        firstWindow = *firstResult;
        secondWindow = *secondResult;
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId firstWindow{};
    Platform::WindowId secondWindow{};
};

TEST(UINodeIdTest, IsEmptyByDefaultAndDoesNotClaimValidity)
{
    const UI::UINodeId empty;

    EXPECT_FALSE(empty.hasValue());
    EXPECT_FALSE(static_cast<bool>(empty));
    EXPECT_FALSE(empty.ownerWindow().hasValue());
    EXPECT_EQ(empty.index(), UI::UINodeId{}.index());
    EXPECT_EQ(empty.generation(), 0U);
}

TEST_F(UITreeCoreTest, RejectsInvalidWindowAndInvalidCapacityWithoutUsingUiMemory)
{
    ObservingMemoryResource resource;

    const auto invalidWindow = UI::UIContext::Create(
        {},
        UI::UIContextCapacityConfig{.nodeCapacity = 4, .rootCapacity = 1},
        resource);
    ASSERT_FALSE(invalidWindow.has_value());
    EXPECT_EQ(invalidWindow.error().code, UI::UIErrorCode::InvalidOwnerWindow);

    const auto expectInvalidCapacity = [&](UI::UIContextCapacityConfig capacities) {
        const Core::Status validation = UI::validateUIContextCapacityConfig(capacities);
        ASSERT_FALSE(validation.has_value());
        EXPECT_EQ(validation.error().code, UI::UIErrorCode::InvalidContextConfig);

        const auto result = UI::UIContext::Create(firstWindow, capacities, resource);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, UI::UIErrorCode::InvalidContextConfig);
    };

    expectInvalidCapacity({.nodeCapacity = 0, .rootCapacity = 1});
    expectInvalidCapacity({.nodeCapacity = 1, .rootCapacity = 0});
    expectInvalidCapacity({.nodeCapacity = 1, .rootCapacity = 2});
    expectInvalidCapacity({
        .nodeCapacity = UI::UIContextCapacityConfig::MaxNodeCapacity + 1,
        .rootCapacity = 1,
    });
    expectInvalidCapacity({
        .nodeCapacity = UI::UIContextCapacityConfig::MaxRootCapacity + 1,
        .rootCapacity = UI::UIContextCapacityConfig::MaxRootCapacity + 1,
    });
    expectInvalidCapacity({.nodeCapacity = 4, .rootCapacity = 1, .dirtyQueueCapacity = 5});
    expectInvalidCapacity({.nodeCapacity = 4, .rootCapacity = 1, .layoutSnapshotCapacity = 5});
    expectInvalidCapacity({.nodeCapacity = 4, .rootCapacity = 1, .hitSnapshotCapacity = 5});
    expectInvalidCapacity({.nodeCapacity = 4, .rootCapacity = 1, .paintSnapshotCapacity = 5});
    expectInvalidCapacity({.nodeCapacity = 4, .rootCapacity = 1, .routePathCapacity = 5});
    expectInvalidCapacity({
        .nodeCapacity = 4,
        .rootCapacity = 1,
        .routedPointerListenerCapacity = UI::UIContextCapacityConfig::MaxRoutedPointerListenerCapacity + 1,
    });

    EXPECT_EQ(resource.allocationCount(), 0U);
    EXPECT_EQ(resource.currentBytes(), 0U);
}

TEST_F(UITreeCoreTest, RejectsCrossWindowAndCrossContextNodes)
{
    auto firstContext = createContext(firstWindow, {.nodeCapacity = 8, .rootCapacity = 2});
    auto secondWindowContext = createContext(secondWindow, {.nodeCapacity = 8, .rootCapacity = 2});
    auto sameWindowContext = createContext(firstWindow, {.nodeCapacity = 8, .rootCapacity = 2});
    ASSERT_NE(firstContext, nullptr);
    ASSERT_NE(secondWindowContext, nullptr);
    ASSERT_NE(sameWindowContext, nullptr);

    auto firstRoot = createRoot(*firstContext);
    ASSERT_TRUE(firstRoot);
    auto panelResult = firstContext->rootBuilder().createPanel(firstRoot.rootNodeId());
    ASSERT_TRUE(panelResult.has_value());
    const UI::UINodeId panel = *panelResult;

    EXPECT_FALSE(secondWindowContext->contains(panel));
    const auto wrongWindowParent = secondWindowContext->rootBuilder().createPanel(panel);
    ASSERT_FALSE(wrongWindowParent.has_value());
    EXPECT_EQ(wrongWindowParent.error().code, UI::UIErrorCode::WrongOwnerWindow);

    EXPECT_FALSE(sameWindowContext->contains(panel));
    const auto wrongContextParent = sameWindowContext->rootBuilder().createPanel(panel);
    ASSERT_FALSE(wrongContextParent.has_value());
    EXPECT_EQ(wrongContextParent.error().code, UI::UIErrorCode::WrongContext);

    auto otherContextRoot = createRoot(*sameWindowContext);
    ASSERT_TRUE(otherContextRoot);
    const auto wrongRootOwner = firstContext->treeUpdater(otherContextRoot);
    ASSERT_FALSE(wrongRootOwner.has_value());
    EXPECT_EQ(wrongRootOwner.error().code, UI::UIErrorCode::WrongContext);
}

TEST_F(UITreeCoreTest, RootOwnerMoveResetAndDestructionReleaseTheWholeRoot)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 8, .rootCapacity = 2});
    ASSERT_NE(context, nullptr);

    UI::UINodeId rootId;
    {
        auto original = createRoot(*context);
        ASSERT_TRUE(original);
        rootId = original.rootNodeId();
        ASSERT_TRUE(context->rootBuilder().createPanel(rootId).has_value());

        UI::UIRootOwner moved = std::move(original);
        EXPECT_FALSE(original.hasValue());
        EXPECT_TRUE(moved.hasValue());
        EXPECT_EQ(moved.rootNodeId(), rootId);
        EXPECT_EQ(context->liveNodeCount(), 2U);

        moved.reset();
        EXPECT_FALSE(moved.hasValue());
        EXPECT_FALSE(context->contains(rootId));
        EXPECT_EQ(context->liveNodeCount(), 0U);
        EXPECT_EQ(context->liveRootCount(), 0U);
    }

    {
        auto scoped = createRoot(*context);
        ASSERT_TRUE(scoped);
        rootId = scoped.rootNodeId();
        ASSERT_TRUE(context->rootBuilder().createButton(rootId).has_value());
    }
    EXPECT_FALSE(context->contains(rootId));
    EXPECT_EQ(context->liveNodeCount(), 0U);
    EXPECT_EQ(context->liveRootCount(), 0U);
}

TEST_F(UITreeCoreTest, RootOwnerCanSafelyOutliveItsContext)
{
    UI::UIRootOwner detachedOwner;
    UI::UINodeId rootId;
    {
        auto context = createContext(firstWindow, {.nodeCapacity = 4, .rootCapacity = 1});
        ASSERT_NE(context, nullptr);
        detachedOwner = createRoot(*context);
        ASSERT_TRUE(detachedOwner);
        rootId = detachedOwner.rootNodeId();
        ASSERT_TRUE(context->contains(rootId));

        context.reset();
    }

    EXPECT_TRUE(detachedOwner.hasValue());
    EXPECT_EQ(detachedOwner.rootNodeId(), rootId);
    detachedOwner.reset();
    EXPECT_FALSE(detachedOwner.hasValue());
}

TEST_F(UITreeCoreTest, RootOwnerDestroyedOffThreadIsReclaimedOnTheOwnerThread)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId rootId = root.rootNodeId();
    ASSERT_TRUE(context->rootBuilder().createPanel(rootId).has_value());

    std::thread releaseThread([ownedRoot = std::move(root)]() mutable {
        ownedRoot.reset();
    });
    releaseThread.join();

    ASSERT_TRUE(context->commitStructure().has_value());
    EXPECT_FALSE(context->contains(rootId));
    EXPECT_EQ(context->liveNodeCount(), 0U);
    EXPECT_EQ(context->liveRootCount(), 0U);
    EXPECT_TRUE(context->committedStructure().empty());

    auto replacement = context->rootBuilder().createRoot();
    ASSERT_TRUE(replacement.has_value());
    EXPECT_EQ(context->liveRootCount(), 1U);
}

TEST_F(UITreeCoreTest, OwnerThreadRootResetAlsoDrainsEarlierOffThreadReleases)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 4, .rootCapacity = 2});
    ASSERT_NE(context, nullptr);
    auto deferredRoot = createRoot(*context);
    auto ownerThreadRoot = createRoot(*context);
    ASSERT_TRUE(deferredRoot);
    ASSERT_TRUE(ownerThreadRoot);
    const UI::UINodeId deferredRootId = deferredRoot.rootNodeId();
    const UI::UINodeId ownerThreadRootId = ownerThreadRoot.rootNodeId();

    std::thread releaseThread([ownedRoot = std::move(deferredRoot)]() mutable {
        ownedRoot.reset();
    });
    releaseThread.join();

    ownerThreadRoot.reset();
    EXPECT_FALSE(context->contains(deferredRootId));
    EXPECT_FALSE(context->contains(ownerThreadRootId));
    EXPECT_EQ(context->liveNodeCount(), 0U);
    EXPECT_EQ(context->liveRootCount(), 0U);
}

TEST_F(UITreeCoreTest, DestroyingANodeRecursivelyDestroysOnlyItsSubtree)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 8, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);

    UI::UIRootBuilder builder = context->rootBuilder();
    auto panelResult = builder.createPanel(root.rootNodeId());
    auto siblingResult = builder.createButton(root.rootNodeId());
    ASSERT_TRUE(panelResult.has_value());
    ASSERT_TRUE(siblingResult.has_value());
    auto labelResult = builder.createLabel(*panelResult);
    ASSERT_TRUE(labelResult.has_value());

    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    ASSERT_TRUE(updater.destroy(*panelResult).has_value());

    EXPECT_FALSE(context->contains(*panelResult));
    EXPECT_FALSE(context->contains(*labelResult));
    EXPECT_TRUE(context->contains(*siblingResult));
    EXPECT_TRUE(context->contains(root.rootNodeId()));
    EXPECT_EQ(context->liveNodeCount(), 2U);
    EXPECT_EQ(context->liveRootCount(), 1U);
}

TEST_F(UITreeCoreTest, ReusingADeletedSlotIncrementsGenerationAndKeepsTheOldIdStale)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 2, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);

    UI::UIRootBuilder builder = context->rootBuilder();
    auto panelResult = builder.createPanel(root.rootNodeId());
    ASSERT_TRUE(panelResult.has_value());
    const UI::UINodeId stalePanel = *panelResult;

    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    ASSERT_TRUE(updaterResult->destroy(stalePanel).has_value());
    EXPECT_FALSE(context->contains(stalePanel));

    auto replacementResult = builder.createLabel(root.rootNodeId());
    ASSERT_TRUE(replacementResult.has_value());
    EXPECT_EQ(replacementResult->index(), stalePanel.index());
    EXPECT_EQ(replacementResult->generation(), stalePanel.generation() + 1);
    EXPECT_FALSE(context->contains(stalePanel));
    EXPECT_TRUE(context->contains(*replacementResult));
}

TEST_F(UITreeCoreTest, NodeAndRootCapacityFailuresDoNotPublishPartialNodes)
{
    auto nodeLimited = createContext(firstWindow, {.nodeCapacity = 2, .rootCapacity = 1});
    ASSERT_NE(nodeLimited, nullptr);
    auto root = createRoot(*nodeLimited);
    ASSERT_TRUE(root);
    auto panelResult = nodeLimited->rootBuilder().createPanel(root.rootNodeId());
    ASSERT_TRUE(panelResult.has_value());

    const auto exhaustedNode = nodeLimited->rootBuilder().createLabel(root.rootNodeId());
    ASSERT_FALSE(exhaustedNode.has_value());
    EXPECT_EQ(exhaustedNode.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(nodeLimited->liveNodeCount(), 2U);
    EXPECT_EQ(nodeLimited->liveRootCount(), 1U);
    ASSERT_TRUE(nodeLimited->commitStructure().has_value());
    EXPECT_EQ(nodeLimited->committedStructure().size(), 2U);

    auto rootLimited = createContext(secondWindow, {.nodeCapacity = 2, .rootCapacity = 1});
    ASSERT_NE(rootLimited, nullptr);
    auto onlyRoot = createRoot(*rootLimited);
    ASSERT_TRUE(onlyRoot);
    const auto exhaustedRoot = rootLimited->rootBuilder().createRoot();
    ASSERT_FALSE(exhaustedRoot.has_value());
    EXPECT_EQ(exhaustedRoot.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(rootLimited->liveNodeCount(), 1U);
    EXPECT_EQ(rootLimited->liveRootCount(), 1U);
}

TEST_F(UITreeCoreTest, RejectsInvalidParentAndUpdaterRootOwnership)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 8, .rootCapacity = 2});
    ASSERT_NE(context, nullptr);
    auto firstRoot = createRoot(*context);
    auto secondRoot = createRoot(*context);
    ASSERT_TRUE(firstRoot);
    ASSERT_TRUE(secondRoot);

    const auto emptyParent = context->rootBuilder().createPanel({});
    ASSERT_FALSE(emptyParent.has_value());
    EXPECT_EQ(emptyParent.error().code, UI::UIErrorCode::InvalidParent);

    auto secondPanelResult = context->rootBuilder().createPanel(secondRoot.rootNodeId());
    ASSERT_TRUE(secondPanelResult.has_value());
    auto firstUpdaterResult = context->treeUpdater(firstRoot);
    ASSERT_TRUE(firstUpdaterResult.has_value());
    const auto foreignRootNode = firstUpdaterResult->destroy(*secondPanelResult);
    ASSERT_FALSE(foreignRootNode.has_value());
    EXPECT_EQ(foreignRootNode.error().code, UI::UIErrorCode::InvalidNode);
    EXPECT_TRUE(context->contains(*secondPanelResult));

    UI::UIRootOwner emptyOwner;
    const auto missingRoot = context->treeUpdater(emptyOwner);
    ASSERT_FALSE(missingRoot.has_value());
    EXPECT_EQ(missingRoot.error().code, UI::UIErrorCode::RootRequired);
}

TEST_F(UITreeCoreTest, TreeUpdaterCreatesChildrenOnlyInsideItsRoot)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 8, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);

    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value()) << updaterResult.error().message;
    auto panel = updaterResult->createPanel(root.rootNodeId());
    ASSERT_TRUE(panel.has_value()) << panel.error().message;
    auto label = updaterResult->createLabel(*panel);
    ASSERT_TRUE(label.has_value()) << label.error().message;
    auto button = updaterResult->createButton(root.rootNodeId());
    ASSERT_TRUE(button.has_value()) << button.error().message;

    EXPECT_TRUE(updaterResult->isAlive(*panel));
    EXPECT_TRUE(updaterResult->isAlive(*label));
    EXPECT_TRUE(updaterResult->isAlive(*button));
    EXPECT_EQ(context->liveNodeCount(), 4U);
    ASSERT_TRUE(context->commitStructure().has_value());

    const UI::UICommittedStructureView committed = context->committedStructure();
    ASSERT_EQ(committed.size(), 4U);
    EXPECT_EQ(committed.entries()[0].node, root.rootNodeId());
    EXPECT_EQ(committed.entries()[0].kind, UI::UIWidgetKind::Root);
    EXPECT_EQ(committed.entries()[1].node, *panel);
    EXPECT_EQ(committed.entries()[1].kind, UI::UIWidgetKind::Panel);
    EXPECT_EQ(committed.entries()[2].node, *label);
    EXPECT_EQ(committed.entries()[2].kind, UI::UIWidgetKind::Label);
    EXPECT_EQ(committed.entries()[3].node, *button);
    EXPECT_EQ(committed.entries()[3].kind, UI::UIWidgetKind::Button);
}

TEST_F(UITreeCoreTest, TreeUpdaterRejectsCreatingChildrenUnderAnotherRoot)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 8, .rootCapacity = 2});
    ASSERT_NE(context, nullptr);
    auto firstRoot = createRoot(*context);
    auto secondRoot = createRoot(*context);
    ASSERT_TRUE(firstRoot);
    ASSERT_TRUE(secondRoot);

    auto firstUpdaterResult = context->treeUpdater(firstRoot);
    auto secondUpdaterResult = context->treeUpdater(secondRoot);
    ASSERT_TRUE(firstUpdaterResult.has_value()) << firstUpdaterResult.error().message;
    ASSERT_TRUE(secondUpdaterResult.has_value()) << secondUpdaterResult.error().message;

    auto secondPanel = secondUpdaterResult->createPanel(secondRoot.rootNodeId());
    ASSERT_TRUE(secondPanel.has_value()) << secondPanel.error().message;
    const usize liveNodeCountBeforeRejectedCreate = context->liveNodeCount();

    const auto rejectedLabel = firstUpdaterResult->createLabel(*secondPanel);
    ASSERT_FALSE(rejectedLabel.has_value());
    EXPECT_EQ(rejectedLabel.error().code, UI::UIErrorCode::InvalidNode);
    EXPECT_TRUE(context->contains(*secondPanel));
    EXPECT_EQ(context->liveNodeCount(), liveNodeCountBeforeRejectedCreate);
}

TEST_F(UITreeCoreTest, TreeUpdaterRejectsCreationAfterRootOwnerReset)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId rootId = root.rootNodeId();

    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value()) << updaterResult.error().message;
    root.reset();

    const auto rejectedPanel = updaterResult->createPanel(rootId);
    ASSERT_FALSE(rejectedPanel.has_value());
    EXPECT_EQ(rejectedPanel.error().code, UI::UIErrorCode::RootRequired);
    EXPECT_FALSE(updaterResult->isAlive(rootId));
    EXPECT_EQ(context->liveNodeCount(), 0U);
    EXPECT_EQ(context->liveRootCount(), 0U);
}

TEST_F(UITreeCoreTest, CommitPublishesStablePreorderParentDepthKindAndPaintOrdinal)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 8, .rootCapacity = 2});
    ASSERT_NE(context, nullptr);

    const UI::UICommittedStructureView beforeCreate = context->committedStructure();
    ASSERT_TRUE(beforeCreate.empty());
    ASSERT_EQ(beforeCreate.revision(), 0U);

    auto firstRoot = createRoot(*context);
    auto secondRoot = createRoot(*context);
    ASSERT_TRUE(firstRoot);
    ASSERT_TRUE(secondRoot);
    UI::UIRootBuilder builder = context->rootBuilder();
    auto panel = builder.createPanel(firstRoot.rootNodeId());
    auto button = builder.createButton(firstRoot.rootNodeId());
    ASSERT_TRUE(panel.has_value());
    ASSERT_TRUE(button.has_value());
    auto label = builder.createLabel(*panel);
    auto secondPanel = builder.createPanel(secondRoot.rootNodeId());
    ASSERT_TRUE(label.has_value());
    ASSERT_TRUE(secondPanel.has_value());

    EXPECT_TRUE(beforeCreate.empty());
    EXPECT_EQ(beforeCreate.revision(), 0U);
    EXPECT_TRUE(context->committedStructure().empty());
    EXPECT_TRUE(context->statistics().structureDirty);

    ASSERT_TRUE(context->commitStructure().has_value());
    const UI::UICommittedStructureView committed = context->committedStructure();
    ASSERT_EQ(committed.revision(), 1U);
    ASSERT_EQ(committed.size(), 6U);

    const UI::UINodeId expectedNodes[] = {
        firstRoot.rootNodeId(), *panel, *label, *button, secondRoot.rootNodeId(), *secondPanel,
    };
    const UI::UINodeId expectedParents[] = {
        {}, firstRoot.rootNodeId(), *panel, firstRoot.rootNodeId(), {}, secondRoot.rootNodeId(),
    };
    const u32 expectedDepths[] = {0, 1, 2, 1, 0, 1};
    const UI::UIWidgetKind expectedKinds[] = {
        UI::UIWidgetKind::Root,
        UI::UIWidgetKind::Panel,
        UI::UIWidgetKind::Label,
        UI::UIWidgetKind::Button,
        UI::UIWidgetKind::Root,
        UI::UIWidgetKind::Panel,
    };

    for (usize index = 0; index < committed.size(); ++index) {
        const UI::UICommittedNodeEntry& entry = committed.entries()[index];
        EXPECT_EQ(entry.node, expectedNodes[index]);
        EXPECT_EQ(entry.parent, expectedParents[index]);
        EXPECT_EQ(entry.depth, expectedDepths[index]);
        EXPECT_EQ(entry.preorder, index);
        EXPECT_EQ(entry.paintOrdinal, index);
        EXPECT_EQ(entry.kind, expectedKinds[index]);
    }
}

TEST_F(UITreeCoreTest, UnchangedCommitPreservesRevisionAndDoesNotAllocateUiPersistentMemory)
{
    ObservingMemoryResource resource;
    auto context = createContext(
        firstWindow,
        {.nodeCapacity = 16, .rootCapacity = 2},
        resource);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    ASSERT_TRUE(context->rootBuilder().createPanel(root.rootNodeId()).has_value());
    ASSERT_TRUE(context->commitStructure().has_value());

    const u64 revision = context->committedStructure().revision();
    const usize allocationCount = resource.allocationCount();
    for (usize frame = 0; frame < 300; ++frame) {
        ASSERT_TRUE(context->commitStructure().has_value());
        EXPECT_EQ(context->committedStructure().revision(), revision);
    }

    EXPECT_EQ(resource.allocationCount(), allocationCount);
    EXPECT_FALSE(context->statistics().structureDirty);
}

TEST_F(UITreeCoreTest, DestroyInvalidatesImmediatelyButKeepsCommittedSnapshotUntilNextCommit)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 8, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    auto panel = context->rootBuilder().createPanel(root.rootNodeId());
    ASSERT_TRUE(panel.has_value());
    auto label = context->rootBuilder().createLabel(*panel);
    ASSERT_TRUE(label.has_value());
    ASSERT_TRUE(context->commitStructure().has_value());

    const UI::UICommittedStructureView oldSnapshot = context->committedStructure();
    ASSERT_EQ(oldSnapshot.revision(), 1U);
    ASSERT_EQ(oldSnapshot.size(), 3U);
    EXPECT_EQ(oldSnapshot.entries()[1].node, *panel);
    EXPECT_EQ(oldSnapshot.entries()[2].node, *label);

    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    ASSERT_TRUE(updaterResult->destroy(*panel).has_value());

    EXPECT_FALSE(context->contains(*panel));
    EXPECT_FALSE(context->contains(*label));
    EXPECT_EQ(oldSnapshot.revision(), 1U);
    EXPECT_EQ(oldSnapshot.size(), 3U);
    EXPECT_EQ(context->committedStructure().revision(), 1U);
    EXPECT_EQ(context->committedStructure().size(), 3U);

    ASSERT_TRUE(context->commitStructure().has_value());
    const UI::UICommittedStructureView replacement = context->committedStructure();
    EXPECT_EQ(replacement.revision(), 2U);
    ASSERT_EQ(replacement.size(), 1U);
    EXPECT_EQ(replacement.entries()[0].node, root.rootNodeId());
}

TEST_F(UITreeCoreTest, DeepTreesCommitAndDestroyWithoutRecursiveStackGrowth)
{
    constexpr usize DeepNodeCount = 50'000;
    auto context = createContext(
        firstWindow,
        {.nodeCapacity = DeepNodeCount, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);

    UI::UIRootBuilder builder = context->rootBuilder();
    UI::UINodeId parent = root.rootNodeId();
    UI::UINodeId firstChild;
    for (usize nodeIndex = 1; nodeIndex < DeepNodeCount; ++nodeIndex) {
        auto childResult = builder.createPanel(parent);
        ASSERT_TRUE(childResult.has_value()) << "nodeIndex=" << nodeIndex;
        parent = *childResult;
        if (nodeIndex == 1) {
            firstChild = parent;
        }
    }

    ASSERT_TRUE(context->commitStructure().has_value());
    const UI::UICommittedStructureView committed = context->committedStructure();
    ASSERT_EQ(committed.size(), DeepNodeCount);
    EXPECT_EQ(committed.entries().back().node, parent);
    EXPECT_EQ(committed.entries().back().depth, DeepNodeCount - 1);

    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    ASSERT_TRUE(updaterResult->destroy(firstChild).has_value());
    EXPECT_EQ(context->liveNodeCount(), 1U);
    EXPECT_FALSE(context->contains(parent));

    ASSERT_TRUE(context->commitStructure().has_value());
    EXPECT_EQ(context->committedStructure().size(), 1U);
    root.reset();
    EXPECT_EQ(context->liveNodeCount(), 0U);
}

TEST_F(UITreeCoreTest, UiTreeStorageMemoryReturnsToZeroAfterAllOwnersAndContextAreReleased)
{
    ObservingMemoryResource resource;
    {
        auto context = createContext(
            firstWindow,
            {.nodeCapacity = 32, .rootCapacity = 4},
            resource);
        ASSERT_NE(context, nullptr);
        auto firstRoot = createRoot(*context);
        auto secondRoot = createRoot(*context);
        ASSERT_TRUE(firstRoot);
        ASSERT_TRUE(secondRoot);
        ASSERT_TRUE(context->rootBuilder().createPanel(firstRoot.rootNodeId()).has_value());
        ASSERT_TRUE(context->rootBuilder().createLabel(secondRoot.rootNodeId()).has_value());
        ASSERT_TRUE(context->commitStructure().has_value());
        EXPECT_GT(resource.currentBytes(), 0U);
        EXPECT_GT(resource.peakBytes(), 0U);
    }

    EXPECT_EQ(resource.currentBytes(), 0U);
    EXPECT_EQ(resource.allocationCount(), resource.deallocationCount());
}

} // namespace
} // namespace Tina::Tests
