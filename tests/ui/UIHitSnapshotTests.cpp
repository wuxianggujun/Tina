#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <algorithm>
#include <limits>
#include <memory>
#include <memory_resource>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

class ObservingMemoryResource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] usize allocationCount() const noexcept { return m_allocationCount; }
    [[nodiscard]] usize currentBytes() const noexcept { return m_currentBytes; }

private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        void* storage = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_allocationCount;
        m_currentBytes += bytes;
        return storage;
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        m_currentBytes -= bytes;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize m_allocationCount = 0;
    usize m_currentBytes = 0;
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

[[nodiscard]] UI::UINodeId createPanel(UI::UIContext& context, UI::UINodeId parent)
{
    auto panelResult = context.rootBuilder().createPanel(parent);
    EXPECT_TRUE(panelResult.has_value())
        << (panelResult ? "" : panelResult.error().message);
    return panelResult ? *panelResult : UI::UINodeId{};
}

[[nodiscard]] UI::UINodeId createButton(UI::UIContext& context, UI::UINodeId parent)
{
    auto buttonResult = context.rootBuilder().createButton(parent);
    EXPECT_TRUE(buttonResult.has_value())
        << (buttonResult ? "" : buttonResult.error().message);
    return buttonResult ? *buttonResult : UI::UINodeId{};
}

[[nodiscard]] UI::UITreeUpdater createUpdater(
    UI::UIContext& context,
    UI::UIRootOwner& root)
{
    auto updaterResult = context.treeUpdater(root);
    EXPECT_TRUE(updaterResult.has_value())
        << (updaterResult ? "" : updaterResult.error().message);
    return updaterResult ? std::move(*updaterResult) : UI::UITreeUpdater{};
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] const UI::UICommittedHitEntry* findHitEntry(
    UI::UICommittedHitView view,
    UI::UINodeId node) noexcept
{
    const auto found = std::ranges::find_if(
        view.entries(),
        [node](const UI::UICommittedHitEntry& entry) { return entry.node == node; });
    return found == view.end() ? nullptr : &*found;
}

class UIHitSnapshotTest : public testing::Test {
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

TEST_F(UIHitSnapshotTest, DerivesHitCapacityAndStartsWithAnEmptyRevisionZeroView)
{
    auto context = createContext(
        firstWindow,
        {.nodeCapacity = 8, .rootCapacity = 2});
    ASSERT_NE(context, nullptr);

    const UI::UIContextStatistics statistics = context->statistics();
    EXPECT_EQ(statistics.hitSnapshotCapacity, 8U);
    EXPECT_EQ(statistics.committedHitNodeCount, 0U);
    EXPECT_EQ(statistics.committedHitTargetCount, 0U);
    EXPECT_EQ(statistics.hitRevision, 0U);
    EXPECT_FALSE(statistics.hitDirty);

    const UI::UICommittedHitView hit = context->committedHit();
    EXPECT_TRUE(hit.empty());
    EXPECT_EQ(hit.structureRevision(), 0U);
    EXPECT_EQ(hit.layoutRevision(), 0U);
    EXPECT_EQ(hit.paintOrderRevision(), 0U);
    EXPECT_EQ(hit.hitRevision(), 0U);

    const auto invalidCapacity = UI::UIContext::Create(
        firstWindow,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .hitSnapshotCapacity = 9,
        });
    ASSERT_FALSE(invalidCapacity.has_value());
    EXPECT_EQ(invalidCapacity.error().code, UI::UIErrorCode::InvalidContextConfig);
}

TEST_F(UIHitSnapshotTest, PublishesStableSelfContainedAncestryAndTargetPolicy)
{
    auto context = createContext(
        firstWindow,
        {.nodeCapacity = 8, .rootCapacity = 1, .hitSnapshotCapacity = 8});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
    const UI::UINodeId button = createButton(*context, panel);
    auto updater = createUpdater(*context, root);

    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(panel, fixedSize(150.0F, 80.0F)));
    assertOk(updater.setLayoutStyle(button, fixedSize(60.0F, 24.0F)));
    assertOk(updater.setPointerHitPolicy(button, UI::UIPointerHitPolicy::Targetable));
    assertOk(context->commitLayout({.width = 200.0F, .height = 100.0F}));

    const UI::UICommittedHitView hit = context->committedHit();
    ASSERT_EQ(hit.size(), 3U);
    EXPECT_EQ(hit.entries()[0].node, root.rootNodeId());
    EXPECT_EQ(hit.entries()[0].parentEntryIndex, UI::InvalidUIHitEntryIndex);
    EXPECT_EQ(hit.entries()[0].rootEntryIndex, 0U);
    EXPECT_EQ(hit.entries()[1].node, panel);
    EXPECT_EQ(hit.entries()[1].parentEntryIndex, 0U);
    EXPECT_EQ(hit.entries()[1].rootEntryIndex, 0U);
    EXPECT_EQ(hit.entries()[2].node, button);
    EXPECT_EQ(hit.entries()[2].parentEntryIndex, 1U);
    EXPECT_EQ(hit.entries()[2].rootEntryIndex, 0U);
    EXPECT_LT(hit.entries()[0].paintOrdinal, hit.entries()[1].paintOrdinal);
    EXPECT_LT(hit.entries()[1].paintOrdinal, hit.entries()[2].paintOrdinal);
    EXPECT_EQ(hit.entries()[0].policy, UI::UIPointerHitPolicy::Ignore);
    EXPECT_EQ(hit.entries()[1].policy, UI::UIPointerHitPolicy::Ignore);
    EXPECT_EQ(hit.entries()[2].policy, UI::UIPointerHitPolicy::Targetable);

    const UI::UICommittedLayoutView layout = context->committedLayout();
    EXPECT_EQ(hit.structureRevision(), layout.structureRevision());
    EXPECT_EQ(hit.layoutRevision(), layout.layoutRevision());
    EXPECT_EQ(hit.paintOrderRevision(), hit.structureRevision());
    EXPECT_EQ(context->statistics().committedHitTargetCount, 1U);
}

TEST_F(UIHitSnapshotTest, PublishesOneStrictGlobalPaintOrderAcrossSiblingsAndRoots)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 8, .rootCapacity = 2});
    ASSERT_NE(context, nullptr);
    auto firstRoot = createRoot(*context);
    auto secondRoot = createRoot(*context);
    ASSERT_TRUE(firstRoot);
    ASSERT_TRUE(secondRoot);
    const UI::UINodeId firstSibling = createButton(*context, firstRoot.rootNodeId());
    const UI::UINodeId secondSibling = createButton(*context, firstRoot.rootNodeId());
    const UI::UINodeId topRootChild = createButton(*context, secondRoot.rootNodeId());
    auto firstUpdater = createUpdater(*context, firstRoot);
    auto secondUpdater = createUpdater(*context, secondRoot);
    UI::UILayoutStyle overlappingStyle = fixedSize(50.0F, 50.0F);
    overlappingStyle.position = UI::UILayoutPositionMode::AbsoluteOverlay;
    assertOk(firstUpdater.setLayoutStyle(firstSibling, overlappingStyle));
    assertOk(firstUpdater.setLayoutStyle(secondSibling, overlappingStyle));
    assertOk(secondUpdater.setLayoutStyle(topRootChild, overlappingStyle));
    assertOk(firstUpdater.setPointerHitPolicy(
        firstSibling,
        UI::UIPointerHitPolicy::Targetable));
    assertOk(firstUpdater.setPointerHitPolicy(
        secondSibling,
        UI::UIPointerHitPolicy::Targetable));
    assertOk(secondUpdater.setPointerHitPolicy(
        topRootChild,
        UI::UIPointerHitPolicy::Targetable));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const UI::UICommittedHitView hit = context->committedHit();
    ASSERT_EQ(hit.size(), 5U);
    for (usize index = 1; index < hit.size(); ++index) {
        EXPECT_LT(hit.entries()[index - 1].paintOrdinal, hit.entries()[index].paintOrdinal);
    }
    EXPECT_EQ(hit.entries()[0].node, firstRoot.rootNodeId());
    EXPECT_EQ(hit.entries()[1].node, firstSibling);
    EXPECT_EQ(hit.entries()[2].node, secondSibling);
    EXPECT_EQ(hit.entries()[3].node, secondRoot.rootNodeId());
    EXPECT_EQ(hit.entries()[4].node, topRootChild);
    EXPECT_EQ(hit.entries()[3].rootEntryIndex, 3U);
    EXPECT_EQ(hit.entries()[4].parentEntryIndex, 3U);
    EXPECT_EQ(hit.entries()[4].rootEntryIndex, 3U);
    EXPECT_EQ(hit.paintOrderRevision(), hit.structureRevision());
}

TEST_F(UIHitSnapshotTest, OmitsHiddenAndCollapsedSubtreesButKeepsIgnoredVisibleAncestors)
{
    auto context = createContext(
        firstWindow,
        {.nodeCapacity = 8, .rootCapacity = 1, .hitSnapshotCapacity = 8});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId visible = createPanel(*context, root.rootNodeId());
    const UI::UINodeId hidden = createPanel(*context, root.rootNodeId());
    const UI::UINodeId hiddenChild = createButton(*context, hidden);
    const UI::UINodeId collapsed = createPanel(*context, root.rootNodeId());
    const UI::UINodeId collapsedChild = createButton(*context, collapsed);
    auto updater = createUpdater(*context, root);

    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    assertOk(updater.setPointerHitPolicy(visible, UI::UIPointerHitPolicy::Targetable));
    assertOk(updater.setPointerHitPolicy(hiddenChild, UI::UIPointerHitPolicy::Targetable));
    assertOk(updater.setPointerHitPolicy(collapsedChild, UI::UIPointerHitPolicy::Targetable));

    UI::UILayoutStyle hiddenStyle = fixedSize(20.0F, 20.0F);
    hiddenStyle.visibility = UI::UIVisibility::Hidden;
    assertOk(updater.setLayoutStyle(hidden, hiddenStyle));
    UI::UILayoutStyle collapsedStyle = fixedSize(20.0F, 20.0F);
    collapsedStyle.visibility = UI::UIVisibility::Collapsed;
    assertOk(updater.setLayoutStyle(collapsed, collapsedStyle));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const UI::UICommittedHitView hit = context->committedHit();
    ASSERT_EQ(hit.size(), 2U);
    EXPECT_NE(findHitEntry(hit, root.rootNodeId()), nullptr);
    EXPECT_NE(findHitEntry(hit, visible), nullptr);
    EXPECT_EQ(findHitEntry(hit, hidden), nullptr);
    EXPECT_EQ(findHitEntry(hit, hiddenChild), nullptr);
    EXPECT_EQ(findHitEntry(hit, collapsed), nullptr);
    EXPECT_EQ(findHitEntry(hit, collapsedChild), nullptr);
    EXPECT_EQ(context->statistics().committedHitTargetCount, 1U);
}

TEST_F(UIHitSnapshotTest, HitOnlyCommitPreservesStructureAndLayoutRevisions)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 50.0F)));
    assertOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));

    const u64 structureRevision = context->committedStructure().revision();
    const u64 layoutRevision = context->committedLayout().layoutRevision();
    const u64 paintOrderRevision = context->committedHit().paintOrderRevision();
    const u64 hitRevision = context->committedHit().hitRevision();

    assertOk(updater.setPointerHitPolicy(panel, UI::UIPointerHitPolicy::Targetable));
    assertOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));

    const UI::UIContextStatistics statistics = context->statistics();
    EXPECT_EQ(context->committedStructure().revision(), structureRevision);
    EXPECT_EQ(context->committedLayout().layoutRevision(), layoutRevision);
    EXPECT_EQ(context->committedHit().paintOrderRevision(), paintOrderRevision);
    EXPECT_EQ(context->committedHit().hitRevision(), hitRevision + 1U);
    EXPECT_EQ(statistics.lastLayoutPassCount, 0U);
    EXPECT_EQ(statistics.lastLayoutMeasuredNodeCount, 0U);
    EXPECT_EQ(statistics.lastLayoutArrangedNodeCount, 0U);
    EXPECT_EQ(statistics.lastHitRebuildCount, 1U);
    EXPECT_FALSE(statistics.hitDirty);
}

TEST_F(UIHitSnapshotTest, SamePolicyAndThreeHundredNoChangeCommitsDoNoWorkOrUiAllocation)
{
    ObservingMemoryResource resource;
    auto context = createContext(
        firstWindow,
        {.nodeCapacity = 8, .rootCapacity = 1},
        resource);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setPointerHitPolicy(panel, UI::UIPointerHitPolicy::Targetable));
    assertOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));

    const usize allocationCount = resource.allocationCount();
    u64 hitRevision = context->committedHit().hitRevision();
    assertOk(updater.setPointerHitPolicy(panel, UI::UIPointerHitPolicy::Targetable));
    for (usize frame = 0; frame < 300; ++frame) {
        assertOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));
        EXPECT_EQ(context->committedHit().hitRevision(), hitRevision);
        EXPECT_EQ(context->statistics().lastHitRebuildCount, 0U);
    }

    for (usize frame = 0; frame < 300; ++frame) {
        const UI::UIPointerHitPolicy policy = frame % 2 == 0
            ? UI::UIPointerHitPolicy::Ignore
            : UI::UIPointerHitPolicy::Targetable;
        assertOk(updater.setPointerHitPolicy(panel, policy));
        assertOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));
        ++hitRevision;
        EXPECT_EQ(context->committedHit().hitRevision(), hitRevision);
        EXPECT_EQ(context->statistics().lastLayoutPassCount, 0U);
        EXPECT_EQ(context->statistics().lastHitRebuildCount, 1U);
    }
    EXPECT_EQ(resource.allocationCount(), allocationCount);
}

TEST_F(UIHitSnapshotTest, DiagnosticStructureCommitKeepsOldHitUntilLayoutBarrier)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    assertOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));

    const u64 oldStructureRevision = context->committedStructure().revision();
    const u64 oldLayoutRevision = context->committedLayout().layoutRevision();
    const u64 oldHitRevision = context->committedHit().hitRevision();
    const u64 oldHitStructureRevision = context->committedHit().structureRevision();
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());

    assertOk(context->commitStructure());
    EXPECT_EQ(context->committedStructure().revision(), oldStructureRevision + 1U);
    EXPECT_EQ(context->committedLayout().layoutRevision(), oldLayoutRevision);
    EXPECT_EQ(context->committedHit().hitRevision(), oldHitRevision);
    EXPECT_EQ(context->committedHit().structureRevision(), oldHitStructureRevision);
    EXPECT_EQ(findHitEntry(context->committedHit(), panel), nullptr);
    EXPECT_FALSE(context->statistics().structureDirty);
    EXPECT_TRUE(context->statistics().layoutDirty);
    EXPECT_TRUE(context->statistics().hitDirty);

    assertOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));
    EXPECT_EQ(context->committedLayout().structureRevision(), oldStructureRevision + 1U);
    EXPECT_EQ(context->committedHit().structureRevision(), oldStructureRevision + 1U);
    EXPECT_EQ(context->committedHit().paintOrderRevision(), oldStructureRevision + 1U);
    EXPECT_NE(findHitEntry(context->committedHit(), panel), nullptr);
}

TEST_F(UIHitSnapshotTest, HitCapacityFailureKeepsAllPublishedSnapshotsAndPendingDirty)
{
    auto context = createContext(
        firstWindow,
        {
            .nodeCapacity = 3,
            .rootCapacity = 1,
            .layoutSnapshotCapacity = 3,
            .hitSnapshotCapacity = 1,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    auto updater = createUpdater(*context, root);
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const UI::UICommittedStructureView oldStructure = context->committedStructure();
    const UI::UICommittedLayoutView oldLayout = context->committedLayout();
    const UI::UICommittedHitView oldHit = context->committedHit();
    ASSERT_EQ(oldHit.size(), 1U);
    const UI::UINodeId oldHitNode = oldHit.entries().front().node;
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());

    const Core::Status overflow =
        context->commitLayout({.width = 100.0F, .height = 100.0F});
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->committedStructure().revision(), oldStructure.revision());
    EXPECT_EQ(context->committedLayout().layoutRevision(), oldLayout.layoutRevision());
    EXPECT_EQ(context->committedHit().hitRevision(), oldHit.hitRevision());
    EXPECT_EQ(context->committedHit().size(), oldHit.size());
    EXPECT_EQ(oldHit.entries().front().node, oldHitNode);
    EXPECT_EQ(context->committedHit().entries().front().node, oldHitNode);
    EXPECT_TRUE(context->statistics().structureDirty);
    EXPECT_TRUE(context->statistics().layoutDirty);
    EXPECT_TRUE(context->statistics().hitDirty);

    UI::UILayoutStyle collapsedStyle;
    collapsedStyle.visibility = UI::UIVisibility::Collapsed;
    assertOk(updater.setLayoutStyle(panel, collapsedStyle));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    EXPECT_EQ(context->committedStructure().size(), 2U);
    EXPECT_EQ(context->committedLayout().size(), 2U);
    EXPECT_EQ(context->committedHit().size(), 1U);
}

TEST_F(UIHitSnapshotTest, RejectsInvalidPolicyWithoutChangingDirtyState)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const u64 hitRevision = context->committedHit().hitRevision();

    const Core::Status invalid = updater.setPointerHitPolicy(
        panel,
        static_cast<UI::UIPointerHitPolicy>(255));
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, UI::UIErrorCode::InvalidPointerPolicy);
    EXPECT_FALSE(context->statistics().hitDirty);
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 0U);
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    EXPECT_EQ(context->committedHit().hitRevision(), hitRevision);
}

TEST_F(UIHitSnapshotTest, PointerPolicyDirtyQueueFailureIsAtomic)
{
    auto context = createContext(
        firstWindow,
        {
            .nodeCapacity = 3,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 1,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId acceptedNode = createPanel(*context, root.rootNodeId());
    const UI::UINodeId rejectedNode = createPanel(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const u64 oldHitRevision = context->committedHit().hitRevision();

    assertOk(updater.setPointerHitPolicy(
        acceptedNode,
        UI::UIPointerHitPolicy::Targetable));
    const Core::Status rejected = updater.setPointerHitPolicy(
        rejectedNode,
        UI::UIPointerHitPolicy::Targetable);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->committedHit().hitRevision(), oldHitRevision);
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 1U);

    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const UI::UICommittedHitEntry* acceptedEntry =
        findHitEntry(context->committedHit(), acceptedNode);
    const UI::UICommittedHitEntry* rejectedEntry =
        findHitEntry(context->committedHit(), rejectedNode);
    ASSERT_NE(acceptedEntry, nullptr);
    ASSERT_NE(rejectedEntry, nullptr);
    EXPECT_EQ(acceptedEntry->policy, UI::UIPointerHitPolicy::Targetable);
    EXPECT_EQ(rejectedEntry->policy, UI::UIPointerHitPolicy::Ignore);
    EXPECT_EQ(context->statistics().committedHitTargetCount, 1U);
}

TEST_F(UIHitSnapshotTest, RejectsWrongOwnerWrongContextAndStaleGeneration)
{
    auto firstContext = createContext(firstWindow, {.nodeCapacity = 4, .rootCapacity = 1});
    auto sameWindowContext = createContext(firstWindow, {.nodeCapacity = 4, .rootCapacity = 1});
    auto secondWindowContext = createContext(secondWindow, {.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(firstContext, nullptr);
    ASSERT_NE(sameWindowContext, nullptr);
    ASSERT_NE(secondWindowContext, nullptr);

    auto firstRoot = createRoot(*firstContext);
    auto sameWindowRoot = createRoot(*sameWindowContext);
    auto secondWindowRoot = createRoot(*secondWindowContext);
    ASSERT_TRUE(firstRoot);
    ASSERT_TRUE(sameWindowRoot);
    ASSERT_TRUE(secondWindowRoot);
    const UI::UINodeId stale = createPanel(*firstContext, firstRoot.rootNodeId());
    auto updater = createUpdater(*firstContext, firstRoot);

    const Core::Status wrongContext = updater.setPointerHitPolicy(
        sameWindowRoot.rootNodeId(),
        UI::UIPointerHitPolicy::Targetable);
    ASSERT_FALSE(wrongContext.has_value());
    EXPECT_EQ(wrongContext.error().code, UI::UIErrorCode::WrongContext);

    const Core::Status wrongWindow = updater.setPointerHitPolicy(
        secondWindowRoot.rootNodeId(),
        UI::UIPointerHitPolicy::Targetable);
    ASSERT_FALSE(wrongWindow.has_value());
    EXPECT_EQ(wrongWindow.error().code, UI::UIErrorCode::WrongOwnerWindow);

    assertOk(updater.destroy(stale));
    const Core::Status staleStatus = updater.setPointerHitPolicy(
        stale,
        UI::UIPointerHitPolicy::Targetable);
    ASSERT_FALSE(staleStatus.has_value());
    EXPECT_EQ(staleStatus.error().code, UI::UIErrorCode::InvalidNode);
}

TEST_F(UIHitSnapshotTest, ReusedSlotStartsIgnoredAndRejectsTheStaleIdentity)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 2, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId oldPanel = createPanel(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setPointerHitPolicy(oldPanel, UI::UIPointerHitPolicy::Targetable));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    assertOk(updater.destroy(oldPanel));

    const UI::UINodeId replacement = createPanel(*context, root.rootNodeId());
    ASSERT_EQ(replacement.index(), oldPanel.index());
    EXPECT_NE(replacement.generation(), oldPanel.generation());
    const Core::Status staleStatus = updater.setPointerHitPolicy(
        oldPanel,
        UI::UIPointerHitPolicy::Targetable);
    ASSERT_FALSE(staleStatus.has_value());
    EXPECT_EQ(staleStatus.error().code, UI::UIErrorCode::InvalidNode);

    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const UI::UICommittedHitEntry* replacementEntry =
        findHitEntry(context->committedHit(), replacement);
    ASSERT_NE(replacementEntry, nullptr);
    EXPECT_EQ(replacementEntry->policy, UI::UIPointerHitPolicy::Ignore);
    EXPECT_EQ(context->statistics().committedHitTargetCount, 0U);
}

TEST_F(UIHitSnapshotTest, StaleQueuedGenerationCannotClearReusedSlotPolicy)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 2, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId oldPanel = createPanel(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    assertOk(updater.setPointerHitPolicy(oldPanel, UI::UIPointerHitPolicy::Targetable));
    assertOk(updater.destroy(oldPanel));
    const UI::UINodeId replacement = createPanel(*context, root.rootNodeId());
    ASSERT_EQ(replacement.index(), oldPanel.index());
    ASSERT_NE(replacement.generation(), oldPanel.generation());
    assertOk(updater.setPointerHitPolicy(replacement, UI::UIPointerHitPolicy::Targetable));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const UI::UICommittedHitEntry* replacementEntry =
        findHitEntry(context->committedHit(), replacement);
    ASSERT_NE(replacementEntry, nullptr);
    EXPECT_EQ(replacementEntry->policy, UI::UIPointerHitPolicy::Targetable);
    EXPECT_EQ(context->statistics().committedHitTargetCount, 1U);
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 0U);
}

TEST_F(UIHitSnapshotTest, QueryPointerHitSelectsTheTopmostTargetAndBindsItsSnapshot)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 8, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId lower = createButton(*context, root.rootNodeId());
    const UI::UINodeId upper = createButton(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);

    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    UI::UILayoutStyle overlap = fixedSize(60.0F, 40.0F);
    overlap.position = UI::UILayoutPositionMode::AbsoluteOverlay;
    assertOk(updater.setLayoutStyle(lower, overlap));
    assertOk(updater.setLayoutStyle(upper, overlap));
    assertOk(updater.setPointerHitPolicy(lower, UI::UIPointerHitPolicy::Targetable));
    assertOk(updater.setPointerHitPolicy(upper, UI::UIPointerHitPolicy::Targetable));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const UI::UIPointerHitQueryResult result = context->queryPointerHit({10.0F, 10.0F});
    ASSERT_TRUE(result.hasTarget());
    EXPECT_EQ(result.target.node, upper);
    EXPECT_EQ(result.target.rootNode, root.rootNodeId());
    EXPECT_EQ(result.visitedEntryCount, 1U);

    const UI::UICommittedHitView hit = context->committedHit();
    ASSERT_LT(result.target.hitEntryIndex, hit.size());
    ASSERT_LT(result.target.rootEntryIndex, hit.size());
    const UI::UICommittedHitEntry& targetEntry = hit.entries()[result.target.hitEntryIndex];
    EXPECT_EQ(targetEntry.node, result.target.node);
    EXPECT_EQ(targetEntry.worldRect, result.target.worldRect);
    EXPECT_EQ(targetEntry.effectiveClip, result.target.effectiveClip);
    EXPECT_EQ(targetEntry.paintOrdinal, result.target.paintOrdinal);
    EXPECT_EQ(hit.entries()[result.target.rootEntryIndex].node, result.target.rootNode);
    EXPECT_EQ(result.structureRevision, hit.structureRevision());
    EXPECT_EQ(result.layoutRevision, hit.layoutRevision());
    EXPECT_EQ(result.paintOrderRevision, hit.paintOrderRevision());
    EXPECT_EQ(result.hitRevision, hit.hitRevision());
}

TEST_F(UIHitSnapshotTest, QueryPointerHitSkipsAnIgnoredFrontmostEntry)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 8, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId lower = createButton(*context, root.rootNodeId());
    const UI::UINodeId ignoredUpper = createButton(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);

    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    UI::UILayoutStyle overlap = fixedSize(60.0F, 40.0F);
    overlap.position = UI::UILayoutPositionMode::AbsoluteOverlay;
    assertOk(updater.setLayoutStyle(lower, overlap));
    assertOk(updater.setLayoutStyle(ignoredUpper, overlap));
    assertOk(updater.setPointerHitPolicy(lower, UI::UIPointerHitPolicy::Targetable));
    assertOk(updater.setPointerHitPolicy(ignoredUpper, UI::UIPointerHitPolicy::Ignore));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const UI::UIPointerHitQueryResult result = context->queryPointerHit({10.0F, 10.0F});
    ASSERT_TRUE(result.hasTarget());
    EXPECT_EQ(result.target.node, lower);
    EXPECT_EQ(result.visitedEntryCount, 2U);
}

TEST_F(UIHitSnapshotTest, QueryPointerHitUsesWorldAndClipHalfOpenBounds)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId button = createButton(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);

    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    UI::UILayoutStyle clipped = fixedSize(40.0F, 20.0F);
    clipped.position = UI::UILayoutPositionMode::AbsoluteOverlay;
    clipped.absoluteInset.left = UI::UILayoutLength::Px(80.0F);
    clipped.absoluteInset.top = UI::UILayoutLength::Px(10.0F);
    assertOk(updater.setLayoutStyle(button, clipped));
    assertOk(updater.setPointerHitPolicy(button, UI::UIPointerHitPolicy::Targetable));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    EXPECT_TRUE(context->queryPointerHit({80.0F, 10.0F}).hasTarget());
    EXPECT_TRUE(context->queryPointerHit({99.0F, 29.0F}).hasTarget());
    const UI::UIPointerHitQueryResult clipRightEdge =
        context->queryPointerHit({100.0F, 10.0F});
    const UI::UIPointerHitQueryResult worldBottomEdge =
        context->queryPointerHit({80.0F, 30.0F});
    EXPECT_FALSE(clipRightEdge.hasTarget());
    EXPECT_FALSE(worldBottomEdge.hasTarget());
    EXPECT_EQ(clipRightEdge.visitedEntryCount, context->committedHit().size());
    EXPECT_EQ(worldBottomEdge.visitedEntryCount, context->committedHit().size());
}

TEST_F(UIHitSnapshotTest, QueryPointerHitTreatsNonFiniteCoordinatesAsAnUnscannedMiss)
{
    auto context = createContext(firstWindow, {.nodeCapacity = 2, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const UI::UIPointerHitQueryResult nanResult = context->queryPointerHit(
        {.x = (std::numeric_limits<float>::quiet_NaN)(), .y = 0.0F});
    const UI::UIPointerHitQueryResult infinityResult = context->queryPointerHit(
        {.x = 0.0F, .y = (std::numeric_limits<float>::infinity)()});
    EXPECT_FALSE(nanResult.hasTarget());
    EXPECT_FALSE(infinityResult.hasTarget());
    EXPECT_EQ(nanResult.visitedEntryCount, 0U);
    EXPECT_EQ(infinityResult.visitedEntryCount, 0U);
    EXPECT_EQ(nanResult.hitRevision, context->committedHit().hitRevision());
    EXPECT_EQ(infinityResult.hitRevision, context->committedHit().hitRevision());
}

TEST_F(UIHitSnapshotTest, ThreeHundredPointerQueriesDoNotAllocateOrMutateUiState)
{
    ObservingMemoryResource resource;
    auto context = createContext(
        firstWindow,
        {.nodeCapacity = 8, .rootCapacity = 1},
        resource);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId button = createButton(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(button, fixedSize(40.0F, 20.0F)));
    assertOk(updater.setPointerHitPolicy(button, UI::UIPointerHitPolicy::Targetable));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const usize allocationCount = resource.allocationCount();
    const UI::UIContextStatistics before = context->statistics();
    for (usize queryIndex = 0; queryIndex < 300; ++queryIndex) {
        const UI::UIPointerHitQueryResult result = context->queryPointerHit({10.0F, 10.0F});
        ASSERT_TRUE(result.hasTarget());
        EXPECT_EQ(result.target.node, button);
    }
    const UI::UIContextStatistics after = context->statistics();
    EXPECT_EQ(resource.allocationCount(), allocationCount);
    EXPECT_EQ(after.committedRevision, before.committedRevision);
    EXPECT_EQ(after.layoutRevision, before.layoutRevision);
    EXPECT_EQ(after.hitRevision, before.hitRevision);
    EXPECT_EQ(after.paintOrderRevision, before.paintOrderRevision);
    EXPECT_EQ(after.structureDirty, before.structureDirty);
    EXPECT_EQ(after.layoutDirty, before.layoutDirty);
    EXPECT_EQ(after.hitDirty, before.hitDirty);
    EXPECT_EQ(after.lastLayoutPassCount, before.lastLayoutPassCount);
    EXPECT_EQ(after.lastHitRebuildCount, before.lastHitRebuildCount);
}

TEST_F(UIHitSnapshotTest, BuildsFiftyThousandDeepEntriesWithoutRecursion)
{
    constexpr usize DeepNodeCount = 50'000;
    auto context = createContext(
        firstWindow,
        {
            .nodeCapacity = DeepNodeCount,
            .rootCapacity = 1,
            .dirtyQueueCapacity = DeepNodeCount,
            .layoutSnapshotCapacity = DeepNodeCount,
            .hitSnapshotCapacity = DeepNodeCount,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    UI::UINodeId parent = root.rootNodeId();
    for (usize index = 1; index < DeepNodeCount; ++index) {
        parent = createPanel(*context, parent);
        ASSERT_TRUE(parent);
    }
    auto updater = createUpdater(*context, root);
    assertOk(updater.setPointerHitPolicy(parent, UI::UIPointerHitPolicy::Targetable));
    assertOk(context->commitLayout({.width = 1.0F, .height = 1.0F}));

    const UI::UICommittedHitView hit = context->committedHit();
    ASSERT_EQ(hit.size(), DeepNodeCount);
    EXPECT_EQ(hit.entries().back().node, parent);
    EXPECT_EQ(hit.entries().back().parentEntryIndex, DeepNodeCount - 2U);
    EXPECT_EQ(hit.entries().back().rootEntryIndex, 0U);
    EXPECT_EQ(context->statistics().committedHitTargetCount, 1U);
}

TEST_F(UIHitSnapshotTest, ReleasesAllSuppliedPmrStorage)
{
    ObservingMemoryResource resource;
    {
        auto context = createContext(
            firstWindow,
            {
                .nodeCapacity = 16,
                .rootCapacity = 2,
                .hitSnapshotCapacity = 16,
            },
            resource);
        ASSERT_NE(context, nullptr);
        auto root = createRoot(*context);
        ASSERT_TRUE(root);
        const UI::UINodeId button = createButton(*context, root.rootNodeId());
        auto updater = createUpdater(*context, root);
        assertOk(updater.setPointerHitPolicy(button, UI::UIPointerHitPolicy::Targetable));
        assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
        EXPECT_GT(resource.currentBytes(), 0U);

        root.reset();
        context.reset();
    }
    EXPECT_EQ(resource.currentBytes(), 0U);
}

} // namespace
} // namespace Tina::Tests
