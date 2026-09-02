#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <memory>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

struct LayoutFixture final {
    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UINodeId left{};
    UI::UINodeId leftLeaf{};
    UI::UINodeId right{};
    UI::UINodeId rightLeaf{};
};

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] UI::UIBoxPaint solidFill(
    u8 red,
    u8 green,
    u8 blue,
    u8 alpha = 255) noexcept
{
    UI::UIBoxPaint paint;
    paint.solidFill = UI::UISolidFill{
        .color = UI::UIStraightSrgba8Color{
            .red = red,
            .green = green,
            .blue = blue,
            .alpha = alpha,
        },
    };
    return paint;
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] LayoutFixture makeFixture(
    float leftLeafWidth = 20.0F,
    usize paintSnapshotCapacity = 0,
    UI::UIVisibility rightVisibility = UI::UIVisibility::Visible,
    float rootWidth = 200.0F)
{
    LayoutFixture fixture;
    auto windowsResult = WindowPool::Create(1);
    EXPECT_TRUE(windowsResult.has_value());
    if (!windowsResult) {
        return fixture;
    }
    fixture.windows = std::make_unique<WindowPool>(std::move(*windowsResult));
    auto windowResult = fixture.windows->tryEmplace(17);
    EXPECT_TRUE(windowResult.has_value());
    if (!windowResult) {
        return fixture;
    }
    fixture.window = *windowResult;

    UI::UIContextCapacityConfig capacity{
        .nodeCapacity = 16,
        .rootCapacity = 1,
        .applyDefaultProductChrome = false,
    };
    if (paintSnapshotCapacity != 0) {
        capacity.paintSnapshotCapacity = paintSnapshotCapacity;
    }
    auto contextResult = UI::UIContext::Create(fixture.window, capacity);
    EXPECT_TRUE(contextResult.has_value())
        << (contextResult ? "" : contextResult.error().message);
    if (!contextResult) {
        return fixture;
    }
    fixture.context = std::move(*contextResult);

    auto rootResult = fixture.context->authoring().rootBuilder().createRoot();
    EXPECT_TRUE(rootResult.has_value())
        << (rootResult ? "" : rootResult.error().message);
    if (!rootResult) {
        return fixture;
    }
    fixture.root = std::move(*rootResult);

    auto updaterResult = fixture.context->authoring().treeUpdater(fixture.root);
    EXPECT_TRUE(updaterResult.has_value())
        << (updaterResult ? "" : updaterResult.error().message);
    if (!updaterResult) {
        return fixture;
    }
    UI::UITreeUpdater updater = std::move(*updaterResult);

    auto leftResult = updater.createElement(fixture.root.rootNodeId(), UI::makePanelElement());
    auto rightResult = updater.createElement(fixture.root.rootNodeId(), UI::makePanelElement());
    EXPECT_TRUE(leftResult.has_value());
    EXPECT_TRUE(rightResult.has_value());
    if (!leftResult || !rightResult) {
        return fixture;
    }
    fixture.left = *leftResult;
    fixture.right = *rightResult;

    // Leaves are Panels (not Labels): Labels publish semantics and share
    // paintSnapshotCapacity; capacity-stress tests (paint cap=1) would fail
    // makeFixture before exercising paint reuse.
    auto leftLeafResult = updater.createElement(fixture.left, UI::makePanelElement());
    auto rightLeafResult = updater.createElement(fixture.right, UI::makePanelElement());
    EXPECT_TRUE(leftLeafResult.has_value());
    EXPECT_TRUE(rightLeafResult.has_value());
    if (!leftLeafResult || !rightLeafResult) {
        return fixture;
    }
    fixture.leftLeaf = *leftLeafResult;
    fixture.rightLeaf = *rightLeafResult;

    UI::UILayoutStyle rootStyle = fixedSize(rootWidth, 100.0F);
    rootStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    UI::UILayoutStyle leftBranchStyle = fixedSize(200.0F, 40.0F);
    assertOk(updater.setLayoutStyle(fixture.root.rootNodeId(), rootStyle));
    assertOk(updater.setLayoutStyle(fixture.left, leftBranchStyle));
    assertOk(updater.setLayoutStyle(
        fixture.leftLeaf,
        fixedSize(leftLeafWidth, 10.0F)));
    assertOk(updater.setLayoutStyle(
        fixture.rightLeaf,
        fixedSize(20.0F, 10.0F)));
    if (rightVisibility != UI::UIVisibility::Visible) {
        UI::UILayoutStyle rightStyle;
        rightStyle.visibility = rightVisibility;
        assertOk(updater.setLayoutStyle(fixture.right, rightStyle));
    }
    assertOk(fixture.context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));
    return fixture;
}

[[nodiscard]] const UI::UICommittedLayoutEntry* findEntry(
    UI::UICommittedLayoutView view,
    UI::UINodeId node) noexcept
{
    for (const UI::UICommittedLayoutEntry& entry : view.entries()) {
        if (entry.node == node) {
            return &entry;
        }
    }
    return nullptr;
}

TEST(UIDirtySubtreeTest, ReusesCleanSiblingSubtreeAfterLeafStyleMutation)
{
    LayoutFixture fixture = makeFixture();
    // Build the oracle with the final style before its first commit. This
    // forces a complete layout pass instead of accidentally testing reuse
    // against another incremental commit.
    LayoutFixture oracle = makeFixture(36.0F);
    ASSERT_NE(fixture.context, nullptr);
    ASSERT_NE(oracle.context, nullptr);
    ASSERT_TRUE(fixture.root);
    ASSERT_TRUE(oracle.root);

    const UI::UICommittedLayoutView before = fixture.context->publication().committedLayout();
    ASSERT_EQ(before.entries().size(), 5U);
    const UI::UICommittedLayoutEntry* rightBefore = findEntry(before, fixture.right);
    const UI::UICommittedLayoutEntry* rightLeafBefore =
        findEntry(before, fixture.rightLeaf);
    ASSERT_NE(rightBefore, nullptr);
    ASSERT_NE(rightLeafBefore, nullptr);
    const UI::UILogicalRect rightWorldBefore = rightBefore->worldRect;
    const UI::UILogicalRect rightClipBefore = rightBefore->effectiveClip;
    const UI::UILogicalRect rightLeafWorldBefore = rightLeafBefore->worldRect;
    const UI::UILogicalRect rightLeafClipBefore = rightLeafBefore->effectiveClip;

    auto updaterResult = fixture.context->authoring().treeUpdater(fixture.root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    assertOk(updater.setLayoutStyle(fixture.leftLeaf, fixedSize(36.0F, 10.0F)));
    assertOk(fixture.context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));

    const UI::UIContextStatistics statistics = fixture.context->statistics();
    EXPECT_EQ(statistics.lastLayoutPassCount, 1U);
    EXPECT_EQ(statistics.lastLayoutMeasuredNodeCount, 3U);
    EXPECT_EQ(statistics.lastLayoutArrangedNodeCount, 3U);

    const UI::UICommittedLayoutView after = fixture.context->publication().committedLayout();
    const UI::UICommittedLayoutEntry* rightAfter = findEntry(after, fixture.right);
    const UI::UICommittedLayoutEntry* rightLeafAfter =
        findEntry(after, fixture.rightLeaf);
    ASSERT_NE(rightAfter, nullptr);
    ASSERT_NE(rightLeafAfter, nullptr);
    EXPECT_EQ(rightAfter->worldRect, rightWorldBefore);
    EXPECT_EQ(rightAfter->effectiveClip, rightClipBefore);
    EXPECT_EQ(rightLeafAfter->worldRect, rightLeafWorldBefore);
    EXPECT_EQ(rightLeafAfter->effectiveClip, rightLeafClipBefore);

    const UI::UICommittedLayoutView oracleLayout = oracle.context->publication().committedLayout();
    ASSERT_EQ(after.entries().size(), oracleLayout.entries().size());
    for (usize index = 0; index < after.entries().size(); ++index) {
        const UI::UICommittedLayoutEntry& actual = after.entries()[index];
        const UI::UICommittedLayoutEntry& expected = oracleLayout.entries()[index];
        EXPECT_EQ(actual.localRect, expected.localRect);
        EXPECT_EQ(actual.worldRect, expected.worldRect);
        EXPECT_EQ(actual.effectiveClip, expected.effectiveClip);
        EXPECT_EQ(actual.effectiveVisibility, expected.effectiveVisibility);
        EXPECT_EQ(actual.layoutOrdinal, expected.layoutOrdinal);
        EXPECT_EQ(actual.paintOrdinal, expected.paintOrdinal);
    }
}

TEST(UIDirtySubtreeTest, ViewportConstraintChangeForcesCompleteLayoutPass)
{
    LayoutFixture fixture = makeFixture();
    ASSERT_NE(fixture.context, nullptr);
    ASSERT_TRUE(fixture.root);

    auto updaterResult = fixture.context->authoring().treeUpdater(fixture.root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    UI::UILayoutStyle rootStyle = fixedSize(240.0F, 100.0F);
    rootStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    assertOk(updater.setLayoutStyle(fixture.root.rootNodeId(), rootStyle));
    assertOk(fixture.context->publication().commitLayout({.width = 240.0F, .height = 100.0F}));

    const UI::UIContextStatistics statistics = fixture.context->statistics();
    EXPECT_EQ(statistics.lastLayoutPassCount, 1U);
    EXPECT_EQ(statistics.lastLayoutMeasuredNodeCount, 5U);
    EXPECT_EQ(statistics.lastLayoutArrangedNodeCount, 5U);
}

TEST(UIDirtySubtreeTest, ParentStyleConstraintChangeForcesCompleteLayoutPass)
{
    LayoutFixture fixture = makeFixture();
    LayoutFixture oracle = makeFixture(
        20.0F,
        0,
        UI::UIVisibility::Visible,
        240.0F);
    ASSERT_NE(fixture.context, nullptr);
    ASSERT_NE(oracle.context, nullptr);
    ASSERT_TRUE(fixture.root);
    ASSERT_TRUE(oracle.root);

    auto updaterResult = fixture.context->authoring().treeUpdater(fixture.root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    UI::UILayoutStyle rootStyle = fixedSize(240.0F, 100.0F);
    rootStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    assertOk(updater.setLayoutStyle(fixture.root.rootNodeId(), rootStyle));

    // Keep the viewport unchanged so this exercises the parent-style dirty
    // path rather than the independent viewport-change full rebuild gate.
    assertOk(fixture.context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));

    const UI::UIContextStatistics statistics = fixture.context->statistics();
    EXPECT_EQ(statistics.lastLayoutPassCount, 1U);
    EXPECT_EQ(statistics.lastLayoutMeasuredNodeCount, 5U);
    EXPECT_EQ(statistics.lastLayoutArrangedNodeCount, 5U);

    const UI::UICommittedLayoutView actual = fixture.context->publication().committedLayout();
    const UI::UICommittedLayoutView expected = oracle.context->publication().committedLayout();
    ASSERT_EQ(actual.entries().size(), expected.entries().size());
    for (usize index = 0; index < actual.entries().size(); ++index) {
        EXPECT_EQ(actual.entries()[index].localRect, expected.entries()[index].localRect);
        EXPECT_EQ(actual.entries()[index].worldRect, expected.entries()[index].worldRect);
        EXPECT_EQ(actual.entries()[index].effectiveClip, expected.entries()[index].effectiveClip);
        EXPECT_EQ(actual.entries()[index].effectiveVisibility, expected.entries()[index].effectiveVisibility);
    }
}

TEST(UIDirtySubtreeTest, RecomputesAutoAncestorAfterLeafStyleMutation)
{
    LayoutFixture fixture = makeFixture();
    ASSERT_NE(fixture.context, nullptr);
    ASSERT_TRUE(fixture.root);

    auto updaterResult = fixture.context->authoring().treeUpdater(fixture.root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    UI::UILayoutStyle rootStyle = fixedSize(200.0F, 100.0F);
    rootStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    rootStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    UI::UILayoutStyle autoBranchStyle;
    assertOk(updater.setLayoutStyle(fixture.root.rootNodeId(), rootStyle));
    assertOk(updater.setLayoutStyle(fixture.left, autoBranchStyle));
    assertOk(fixture.context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));

    const UI::UICommittedLayoutEntry* initialBranch =
        findEntry(fixture.context->publication().committedLayout(), fixture.left);
    const UI::UICommittedLayoutEntry* initialFollowingBranch =
        findEntry(fixture.context->publication().committedLayout(), fixture.right);
    ASSERT_NE(initialBranch, nullptr);
    ASSERT_NE(initialFollowingBranch, nullptr);
    EXPECT_EQ(initialBranch->worldRect.width, 20.0F);
    EXPECT_EQ(initialBranch->worldRect.height, 10.0F);
    const float initialFollowingBranchY = initialFollowingBranch->worldRect.y;

    assertOk(updater.setLayoutStyle(fixture.leftLeaf, fixedSize(36.0F, 24.0F)));
    assertOk(fixture.context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));

    const UI::UIContextStatistics statistics = fixture.context->statistics();
    EXPECT_EQ(statistics.lastLayoutPassCount, 1U);
    EXPECT_LT(statistics.lastLayoutMeasuredNodeCount, 5U);
    // The following flex sibling moves when the auto branch grows, so its
    // subtree must be arranged even though it does not need remeasurement.
    EXPECT_EQ(statistics.lastLayoutArrangedNodeCount, 5U);

    const UI::UICommittedLayoutEntry* branch =
        findEntry(fixture.context->publication().committedLayout(), fixture.left);
    const UI::UICommittedLayoutEntry* followingBranch =
        findEntry(fixture.context->publication().committedLayout(), fixture.right);
    ASSERT_NE(branch, nullptr);
    ASSERT_NE(followingBranch, nullptr);
    EXPECT_EQ(branch->worldRect.width, 36.0F);
    EXPECT_EQ(branch->worldRect.height, 24.0F);
    EXPECT_GT(followingBranch->worldRect.y, initialFollowingBranchY);
}

TEST(UIDirtySubtreeTest, CollapsingSubtreeMatchesFullRebuildAndKeepsSiblingClean)
{
    LayoutFixture fixture = makeFixture();
    LayoutFixture oracle = makeFixture(
        20.0F,
        0,
        UI::UIVisibility::Collapsed);
    ASSERT_NE(fixture.context, nullptr);
    ASSERT_NE(oracle.context, nullptr);
    ASSERT_TRUE(fixture.root);
    ASSERT_TRUE(oracle.root);

    auto updaterResult = fixture.context->authoring().treeUpdater(fixture.root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    UI::UILayoutStyle collapsedStyle;
    collapsedStyle.visibility = UI::UIVisibility::Collapsed;
    assertOk(updater.setLayoutStyle(fixture.right, collapsedStyle));
    assertOk(fixture.context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));

    const UI::UIContextStatistics statistics = fixture.context->statistics();
    EXPECT_EQ(statistics.lastLayoutPassCount, 1U);
    EXPECT_EQ(statistics.lastLayoutMeasuredNodeCount, 3U);
    EXPECT_EQ(statistics.lastLayoutArrangedNodeCount, 3U);

    const UI::UICommittedLayoutView actual = fixture.context->publication().committedLayout();
    const UI::UICommittedLayoutView expected = oracle.context->publication().committedLayout();
    ASSERT_EQ(actual.entries().size(), expected.entries().size());
    for (usize index = 0; index < actual.entries().size(); ++index) {
        EXPECT_EQ(actual.entries()[index].localRect, expected.entries()[index].localRect);
        EXPECT_EQ(actual.entries()[index].worldRect, expected.entries()[index].worldRect);
        EXPECT_EQ(actual.entries()[index].effectiveClip, expected.entries()[index].effectiveClip);
        EXPECT_EQ(
            actual.entries()[index].effectiveVisibility,
            expected.entries()[index].effectiveVisibility);
    }
}

TEST(UIDirtySubtreeTest, FailedPaintCandidateDisablesReuseForNextLayout)
{
    LayoutFixture fixture = makeFixture(20.0F, 1);
    ASSERT_NE(fixture.context, nullptr);
    ASSERT_TRUE(fixture.root);

    auto updaterResult = fixture.context->authoring().treeUpdater(fixture.root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);
    assertOk(updater.setLayoutStyle(fixture.leftLeaf, fixedSize(36.0F, 10.0F)));
    assertOk(updater.setBoxPaint(fixture.left, solidFill(10, 20, 30)));
    assertOk(updater.setBoxPaint(fixture.right, solidFill(40, 50, 60)));

    const u64 publishedLayoutRevision = fixture.context->publication().committedLayout().layoutRevision();
    const Core::Status rejected =
        fixture.context->publication().commitLayout({.width = 200.0F, .height = 100.0F});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(
        fixture.context->publication().committedLayout().layoutRevision(),
        publishedLayoutRevision);

    assertOk(updater.setBoxPaint(fixture.right, {}));
    assertOk(fixture.context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));

    const UI::UIContextStatistics statistics = fixture.context->statistics();
    EXPECT_EQ(statistics.lastLayoutPassCount, 1U);
    EXPECT_EQ(statistics.lastLayoutMeasuredNodeCount, 5U);
    EXPECT_EQ(statistics.lastLayoutArrangedNodeCount, 5U);
}

TEST(UIDirtySubtreeTest, PhaseDirtyStatsTrackStructureLayoutHitPaintAndSemantics)
{
    LayoutFixture fixture = makeFixture();
    ASSERT_NE(fixture.context, nullptr);
    ASSERT_TRUE(fixture.root);

    UI::UIContextStatistics clean = fixture.context->statistics();
    EXPECT_FALSE(clean.structureDirty);
    EXPECT_FALSE(clean.layoutDirty);
    EXPECT_FALSE(clean.hitDirty);
    EXPECT_FALSE(clean.paintDirty);
    EXPECT_FALSE(clean.semanticsDirty);
    EXPECT_EQ(clean.dirtyQueuePendingCount, 0U);

    auto updaterResult = fixture.context->authoring().treeUpdater(fixture.root);
    ASSERT_TRUE(updaterResult.has_value());
    UI::UITreeUpdater updater = std::move(*updaterResult);

    assertOk(updater.setLayoutStyle(fixture.leftLeaf, fixedSize(36.0F, 10.0F)));
    UI::UIContextStatistics afterLayout = fixture.context->statistics();
    EXPECT_FALSE(afterLayout.structureDirty);
    EXPECT_TRUE(afterLayout.layoutDirty);
    EXPECT_TRUE(afterLayout.hitDirty);
    EXPECT_FALSE(afterLayout.paintDirty);
    EXPECT_FALSE(afterLayout.semanticsDirty);
    EXPECT_GT(afterLayout.dirtyQueuePendingCount, 0U);

    assertOk(updater.setBoxPaint(fixture.leftLeaf, solidFill(1, 2, 3)));
    UI::UIContextStatistics afterPaint = fixture.context->statistics();
    EXPECT_FALSE(afterPaint.structureDirty);
    EXPECT_TRUE(afterPaint.layoutDirty);
    EXPECT_TRUE(afterPaint.hitDirty);
    EXPECT_TRUE(afterPaint.paintDirty);
    EXPECT_TRUE(afterPaint.semanticsDirty);

    auto panelResult = updater.createElement(fixture.root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(panelResult.has_value());
    UI::UIContextStatistics afterStructure = fixture.context->statistics();
    EXPECT_TRUE(afterStructure.structureDirty);
    EXPECT_TRUE(afterStructure.layoutDirty);
    EXPECT_TRUE(afterStructure.hitDirty);
    EXPECT_TRUE(afterStructure.paintDirty);
    EXPECT_TRUE(afterStructure.semanticsDirty);
    static_cast<void>(*panelResult);

    assertOk(fixture.context->publication().commitStructure());
    UI::UIContextStatistics afterStructureCommit = fixture.context->statistics();
    EXPECT_FALSE(afterStructureCommit.structureDirty);
    EXPECT_TRUE(afterStructureCommit.layoutDirty);
    EXPECT_TRUE(afterStructureCommit.hitDirty);
    EXPECT_TRUE(afterStructureCommit.paintDirty);
    EXPECT_TRUE(afterStructureCommit.semanticsDirty);

    assertOk(fixture.context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));
    UI::UIContextStatistics afterLayoutCommit = fixture.context->statistics();
    EXPECT_FALSE(afterLayoutCommit.structureDirty);
    EXPECT_FALSE(afterLayoutCommit.layoutDirty);
    EXPECT_FALSE(afterLayoutCommit.hitDirty);
    EXPECT_FALSE(afterLayoutCommit.paintDirty);
    EXPECT_FALSE(afterLayoutCommit.semanticsDirty);
    EXPECT_EQ(afterLayoutCommit.dirtyQueuePendingCount, 0U);
}

// UIDirty declares 11 of the 16 bits in its u16, so it keeps a masked operator~ instead
// of the raw complement TINA_ENUM_FLAG_OPERATORS would have to assume. clearDirty()
// depends on the complement setting no bit that names no channel.
TEST(UIDirtyFlagsTest, ComplementStaysInsideTheDeclaredBits)
{
    constexpr UI::UIDirty complement = ~UI::UIDirty::Paint;
    EXPECT_EQ(UI::dirtyMaskValue(complement),
              static_cast<u16>(UI::dirtyMaskValue(UI::UIDirtyMaskAll) &
                               ~UI::dirtyMaskValue(UI::UIDirty::Paint)));
    EXPECT_FALSE(UI::hasDirty(complement, UI::UIDirty::Paint));
    EXPECT_TRUE(UI::hasDirty(complement, UI::UIDirty::Style));
    EXPECT_EQ(UI::clearDirty(UI::UIDirty::Paint | UI::UIDirty::Style, UI::UIDirty::Paint),
              UI::UIDirty::Style);
}

TEST(UIDirtyFlagsTest, MacroOperatorsRoundTrip)
{
    UI::UIDirty flags = UI::UIDirty::None;
    flags |= UI::UIDirty::Paint;
    flags |= UI::UIDirty::Measure;
    EXPECT_TRUE(UI::hasAllDirty(flags, UI::UIDirty::Paint | UI::UIDirty::Measure));

    flags &= UI::UIDirty::Paint;
    EXPECT_TRUE(UI::hasDirty(flags, UI::UIDirty::Paint));
    EXPECT_FALSE(UI::hasDirty(flags, UI::UIDirty::Measure));

    flags ^= UI::UIDirty::Paint;
    EXPECT_FALSE(UI::anyDirty(flags));
}

} // namespace
} // namespace Tina::Tests
