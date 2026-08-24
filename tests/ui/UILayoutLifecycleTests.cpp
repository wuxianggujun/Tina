#include "UILayoutTestSupport.hpp"

namespace Tina::Tests {
namespace {

using namespace UILayoutTestSupport;

TEST_F(UILayoutTest, SettingTheSameNormalizedStyleIsANoOp)
{
    auto context = makeContext({.nodeCapacity = 3, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());

    auto updater = createUpdater(*context, root);
    const UI::UILayoutStyle style = fixedSize(40.0F, 20.0F);
    assertOk(updater.setLayoutStyle(panel, style));
    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    const u64 layoutRevision = context->publication().committedLayout().layoutRevision();
    const UI::UIContextStatistics before = context->statistics();

    assertOk(updater.setLayoutStyle(panel, style));
    const UI::UIContextStatistics afterSet = context->statistics();
    EXPECT_EQ(afterSet.dirtyQueuePendingCount, before.dirtyQueuePendingCount);
    EXPECT_FALSE(afterSet.layoutDirty);
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    EXPECT_EQ(context->publication().committedLayout().layoutRevision(), layoutRevision);
    EXPECT_EQ(context->statistics().lastLayoutPassCount, 0U);
}

TEST_F(UILayoutTest, MultipleMutationsAreCommittedByASingleLayoutPass)
{
    auto context = makeContext({.nodeCapacity = 5, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId first = createPanel(*context, root.rootNodeId());
    const UI::UINodeId second = createPanel(*context, root.rootNodeId());
    const UI::UINodeId third = createPanel(*context, root.rootNodeId());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(first, fixedSize(10.0F, 10.0F)));
    assertOk(updater.setLayoutStyle(second, fixedSize(20.0F, 20.0F)));
    assertOk(updater.setLayoutStyle(third, fixedSize(30.0F, 30.0F)));
    const UI::UIContextStatistics beforeLayout = context->statistics();
    EXPECT_TRUE(beforeLayout.layoutDirty);
    EXPECT_EQ(beforeLayout.dirtyQueuePendingCount, 4U);

    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    const UI::UIContextStatistics afterLayout = context->statistics();
    EXPECT_FALSE(afterLayout.layoutDirty);
    EXPECT_EQ(afterLayout.dirtyQueuePendingCount, 0U);
    EXPECT_EQ(afterLayout.lastLayoutPassCount, 1U);
    EXPECT_EQ(afterLayout.lastLayoutMeasuredNodeCount, 4U);
    EXPECT_EQ(afterLayout.lastLayoutArrangedNodeCount, 4U);
}

TEST_F(UILayoutTest, UnchangedLayoutCommitForThreeHundredFramesDoesNoWorkAndAllocatesNoUiMemory)
{
    ObservingMemoryResource resource;
    auto context = makeContext({.nodeCapacity = 4, .rootCapacity = 1}, resource);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(panel, fixedSize(40.0F, 20.0F)));
    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 50.0F}));

    const u64 layoutRevision = context->publication().committedLayout().layoutRevision();
    const usize allocationCount = resource.allocationCount();
    for (usize frame = 0; frame < 300; ++frame)
    {
        assertOk(context->publication().commitLayout({.width = 100.0F, .height = 50.0F}));
        EXPECT_EQ(context->publication().committedLayout().layoutRevision(), layoutRevision);
        EXPECT_EQ(context->statistics().lastLayoutPassCount, 0U);
    }
    EXPECT_EQ(resource.allocationCount(), allocationCount);
}

TEST_F(UILayoutTest, PercentInAutoAxisFallsBackDeterministicallyAndReportsDiagnosticCount)
{
    auto context = makeContext({.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId autoPanel = createPanel(*context, root.rootNodeId());
    const UI::UINodeId fixedChild = createPanel(*context, autoPanel);
    const UI::UINodeId percentChild = createPanel(*context, autoPanel);

    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle rootStyle;
    rootStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), rootStyle));
    UI::UILayoutStyle autoPanelStyle;
    autoPanelStyle.size.width = UI::UILayoutLength::Auto();
    autoPanelStyle.size.height = UI::UILayoutLength::Auto();
    autoPanelStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    assertOk(updater.setLayoutStyle(autoPanel, autoPanelStyle));
    UI::UILayoutStyle fixedChildStyle = fixedSize(100.0F, 10.0F);
    fixedChildStyle.flexItem.shrink = 0.0F;
    assertOk(updater.setLayoutStyle(fixedChild, fixedChildStyle));
    UI::UILayoutStyle childStyle;
    childStyle.size.width = UI::UILayoutLength::Percent(50.0F);
    childStyle.size.height = UI::UILayoutLength::Px(10.0F);
    childStyle.flexItem.shrink = 0.0F;
    assertOk(updater.setLayoutStyle(percentChild, childStyle));

    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 50.0F}));
    const UI::UIContextStatistics stats = context->statistics();
    EXPECT_GT(stats.lastLayoutPercentMeasureFallbackCount, 0U);
    expectRectNear(requireLayoutEntry(context->publication().committedLayout(), autoPanel).worldRect,
                   {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 10.0F});
    expectRectNear(requireLayoutEntry(context->publication().committedLayout(), percentChild).worldRect,
                   {.x = 100.0F, .y = 0.0F, .width = 50.0F, .height = 10.0F});
}

TEST_F(UILayoutTest, LayoutSnapshotCapacityFailurePreservesOldCommittedLayoutAndDirtyState)
{
    auto context = makeContext({
        .nodeCapacity = 4,
        .rootCapacity = 1,
        .dirtyQueueCapacity = 4,
        .layoutSnapshotCapacity = 2,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId first = createPanel(*context, root.rootNodeId());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(first, fixedSize(10.0F, 10.0F)));
    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    const UI::UICommittedLayoutView oldLayout = context->publication().committedLayout();
    const UI::UICommittedStructureView oldStructure = context->publication().committedStructure();
    ASSERT_EQ(oldLayout.size(), 2U);
    ASSERT_EQ(oldStructure.size(), 2U);
    const u64 oldLayoutRevision = oldLayout.layoutRevision();
    const u64 oldStructureRevisionInLayout = oldLayout.structureRevision();
    const u64 oldStructureRevision = oldStructure.revision();

    const UI::UINodeId second = createPanel(*context, root.rootNodeId());
    assertOk(updater.setLayoutStyle(second, fixedSize(20.0F, 20.0F)));

    const Core::Status status = context->publication().commitLayout({.width = 100.0F, .height = 100.0F});
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, UI::UIErrorCode::CapacityExceeded);
    const UI::UICommittedLayoutView afterFailure = context->publication().committedLayout();
    EXPECT_EQ(afterFailure.layoutRevision(), oldLayoutRevision);
    EXPECT_EQ(afterFailure.structureRevision(), oldStructureRevisionInLayout);
    EXPECT_EQ(afterFailure.size(), oldLayout.size());
    const UI::UICommittedStructureView structureAfterFailure = context->publication().committedStructure();
    EXPECT_EQ(structureAfterFailure.revision(), oldStructureRevision);
    EXPECT_EQ(structureAfterFailure.size(), oldStructure.size());
    EXPECT_TRUE(std::none_of(structureAfterFailure.begin(), structureAfterFailure.end(),
                             [&](const UI::UICommittedNodeEntry& entry) { return entry.node == second; }));
    EXPECT_TRUE(context->statistics().structureDirty);
    EXPECT_TRUE(context->statistics().layoutDirty);
}

TEST_F(UILayoutTest, DirtyQueueCapacityFailureIsAtomicForTheRejectedMutation)
{
    auto context = makeContext({
        .nodeCapacity = 4,
        .rootCapacity = 1,
        .dirtyQueueCapacity = 2,
        .layoutSnapshotCapacity = 4,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId first = createPanel(*context, root.rootNodeId());
    const UI::UINodeId second = createPanel(*context, root.rootNodeId());
    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(first, fixedSize(10.0F, 10.0F)));
    const Core::Status rejected = updater.setLayoutStyle(second, fixedSize(20.0F, 20.0F));
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 2U);

    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    const UI::UICommittedLayoutView layout = context->publication().committedLayout();
    expectRectNear(requireLayoutEntry(layout, first).worldRect,
                   {.x = 0.0F, .y = 0.0F, .width = 10.0F, .height = 10.0F});
    expectRectNear(requireLayoutEntry(layout, second).worldRect,
                   {.x = 0.0F, .y = 10.0F, .width = 100.0F, .height = 0.0F});
}

TEST_F(UILayoutTest, StaleDirtyEntryCannotClearReusedGenerationDirtyState)
{
    auto context = makeContext({
        .nodeCapacity = 3,
        .rootCapacity = 1,
        .dirtyQueueCapacity = 3,
        .layoutSnapshotCapacity = 3,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId staleNode = createPanel(*context, root.rootNodeId());
    const UI::UINodeId sibling = createPanel(*context, root.rootNodeId());
    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(staleNode, fixedSize(5.0F, 5.0F)));
    ASSERT_EQ(context->statistics().dirtyQueuePendingCount, 2U);
    assertOk(updater.destroy(staleNode));

    const UI::UINodeId reused = createPanel(*context, root.rootNodeId());
    ASSERT_EQ(reused.index(), staleNode.index());
    ASSERT_NE(reused.generation(), staleNode.generation());
    assertOk(updater.setLayoutStyle(reused, fixedSize(30.0F, 10.0F)));
    assertOk(updater.setLayoutStyle(sibling, fixedSize(40.0F, 10.0F)));
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 3U);

    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    const UI::UICommittedLayoutView layout = context->publication().committedLayout();
    expectRectNear(requireLayoutEntry(layout, sibling).worldRect,
                   {.x = 0.0F, .y = 0.0F, .width = 40.0F, .height = 10.0F});
    expectRectNear(requireLayoutEntry(layout, reused).worldRect,
                   {.x = 0.0F, .y = 10.0F, .width = 30.0F, .height = 10.0F});
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 0U);
    EXPECT_FALSE(context->statistics().layoutDirty);
}

TEST_F(UILayoutTest, LayoutStorageMemoryReturnsToZeroAfterContextRelease)
{
    ObservingMemoryResource resource;
    {
        auto context = makeContext({
            .nodeCapacity = 16,
            .rootCapacity = 2,
            .dirtyQueueCapacity = 16,
            .layoutSnapshotCapacity = 16,
        }, resource);
        ASSERT_NE(context, nullptr);
        auto root = createRoot(*context);
        ASSERT_TRUE(root);
        const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
        auto updater = createUpdater(*context, root);
        assertOk(updater.setLayoutStyle(panel, fixedSize(25.0F, 25.0F)));
        assertOk(context->publication().commitStructure());
        assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
        EXPECT_GT(resource.currentBytes(), 0U);
        EXPECT_GT(resource.peakBytes(), 0U);
    }

    EXPECT_EQ(resource.currentBytes(), 0U);
    EXPECT_EQ(resource.allocationCount(), resource.deallocationCount());
}

} // namespace
} // namespace Tina::Tests
