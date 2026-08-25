#include "UILayoutTestSupport.hpp"

#include <cmath>
#include <limits>

namespace Tina::Tests {
namespace {

using namespace UILayoutTestSupport;

TEST_F(UILayoutTest, RejectsNonFiniteAndNegativeStylesWithoutDirtyingLayout)
{
    auto context = makeContext({.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
    ASSERT_TRUE(panel.hasValue());
    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 50.0F}));
    const UI::UIContextStatistics before = context->statistics();

    auto updater = createUpdater(*context, root);
    const auto expectInvalid = [&](UI::UILayoutStyle style) {
        const Core::Status status = updater.setLayoutStyle(panel, style);
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, UI::UIErrorCode::InvalidLayout);
        const UI::UIContextStatistics after = context->statistics();
        EXPECT_EQ(after.layoutRevision, before.layoutRevision);
        EXPECT_EQ(after.dirtyQueuePendingCount, before.dirtyQueuePendingCount);
        EXPECT_EQ(after.layoutDirty, before.layoutDirty);
    };

    UI::UILayoutStyle style = fixedSize(20.0F, 10.0F);
    style.size.width = UI::UILayoutLength::Px(-1.0F);
    expectInvalid(style);

    style = fixedSize(20.0F, 10.0F);
    style.size.height = UI::UILayoutLength::Px((std::numeric_limits<float>::infinity)());
    expectInvalid(style);

    style = fixedSize(20.0F, 10.0F);
    style.margin.left = -0.5F;
    expectInvalid(style);

    style = fixedSize(20.0F, 10.0F);
    style.padding.top = std::nanf("");
    expectInvalid(style);

    style = fixedSize(20.0F, 10.0F);
    style.flexItem.grow = -1.0F;
    expectInvalid(style);

    style = fixedSize(20.0F, 10.0F);
    style.flexContainer.gap.row = -2.0F;
    expectInvalid(style);

    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 50.0F}));
    const UI::UIContextStatistics afterCommit = context->statistics();
    EXPECT_EQ(afterCommit.layoutRevision, before.layoutRevision);
    EXPECT_EQ(afterCommit.lastLayoutPassCount, 0U);
}

TEST_F(UILayoutTest, InvalidPercentEnumAndViewportPreserveCommittedStateAndPendingDirty)
{
    auto context = makeContext({.nodeCapacity = 2, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
    ASSERT_TRUE(panel.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(panel, fixedSize(20.0F, 10.0F)));
    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 50.0F}));

    const UI::UICommittedStructureView baselineStructure = context->publication().committedStructure();
    const UI::UICommittedLayoutView baselineLayout = context->publication().committedLayout();
    const u64 structureRevision = baselineStructure.revision();
    const u64 layoutRevision = baselineLayout.layoutRevision();
    const usize structureSize = baselineStructure.size();
    const usize layoutSize = baselineLayout.size();
    const UI::UILogicalRect committedPanelRect =
        requireLayoutEntry(baselineLayout, panel).worldRect;

    const auto expectCommittedSnapshotUnchanged = [&] {
        const UI::UICommittedStructureView structure = context->publication().committedStructure();
        const UI::UICommittedLayoutView layout = context->publication().committedLayout();
        EXPECT_EQ(structure.revision(), structureRevision);
        EXPECT_EQ(structure.size(), structureSize);
        EXPECT_EQ(layout.structureRevision(), structureRevision);
        EXPECT_EQ(layout.layoutRevision(), layoutRevision);
        EXPECT_EQ(layout.size(), layoutSize);
        expectRectNear(requireLayoutEntry(layout, panel).worldRect, committedPanelRect);
    };

    UI::UILayoutStyle invalidPercent = fixedSize(20.0F, 10.0F);
    invalidPercent.size.width = UI::UILayoutLength::Percent(100.01F);
    Core::Status rejected = updater.setLayoutStyle(panel, invalidPercent);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidLayout);
    expectCommittedSnapshotUnchanged();
    EXPECT_FALSE(context->statistics().structureDirty);
    EXPECT_FALSE(context->statistics().layoutDirty);
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 0U);

    UI::UILayoutStyle invalidEnum = fixedSize(20.0F, 10.0F);
    invalidEnum.flexContainer.direction = static_cast<UI::UIFlexDirection>(0xff);
    rejected = updater.setLayoutStyle(panel, invalidEnum);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidLayout);
    expectCommittedSnapshotUnchanged();
    EXPECT_FALSE(context->statistics().structureDirty);
    EXPECT_FALSE(context->statistics().layoutDirty);
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 0U);

    assertOk(updater.setLayoutStyle(panel, fixedSize(30.0F, 15.0F)));
    const UI::UIContextStatistics pending = context->statistics();
    ASSERT_FALSE(pending.structureDirty);
    ASSERT_TRUE(pending.layoutDirty);
    ASSERT_EQ(pending.dirtyQueuePendingCount, 2U);

    const auto expectInvalidViewport = [&](UI::UILogicalSize viewport) {
        const Core::Status status = context->publication().commitLayout(viewport);
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, UI::UIErrorCode::InvalidLayout);
        expectCommittedSnapshotUnchanged();
        const UI::UIContextStatistics after = context->statistics();
        EXPECT_EQ(after.structureDirty, pending.structureDirty);
        EXPECT_EQ(after.layoutDirty, pending.layoutDirty);
        EXPECT_EQ(after.dirtyQueuePendingCount, pending.dirtyQueuePendingCount);
    };

    expectInvalidViewport({.width = -1.0F, .height = 50.0F});
    expectInvalidViewport({
        .width = 100.0F,
        .height = (std::numeric_limits<float>::infinity)(),
    });
    expectInvalidViewport({.width = std::nanf(""), .height = 50.0F});

    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 50.0F}));
    EXPECT_EQ(context->publication().committedLayout().layoutRevision(), layoutRevision + 1);
    expectRectNear(
        requireLayoutEntry(context->publication().committedLayout(), panel).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 30.0F, .height = 15.0F});
}

TEST_F(UILayoutTest, FiniteInputOverflowCannotPublishNonFiniteGeometry)
{
    auto context = makeContext({.nodeCapacity = 3, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId row = createPanel(*context, root.rootNodeId());
    const UI::UINodeId child = createPanel(*context, row);

    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle rowStyle = fixedSize(100.0F, 20.0F);
    rowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    assertOk(updater.setLayoutStyle(row, rowStyle));
    assertOk(updater.setLayoutStyle(child, fixedSize(10.0F, 10.0F)));
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 20.0F}));

    const u64 oldStructureRevision = context->publication().committedStructure().revision();
    const u64 oldLayoutRevision = context->publication().committedLayout().layoutRevision();
    const UI::UILogicalRect oldChildRect =
        requireLayoutEntry(context->publication().committedLayout(), child).worldRect;

    UI::UILayoutStyle overflowingStyle = fixedSize(
        (std::numeric_limits<float>::max)(),
        10.0F);
    overflowingStyle.margin.left = (std::numeric_limits<float>::max)();
    assertOk(updater.setLayoutStyle(child, overflowingStyle));

    const Core::Status rejected =
        context->publication().commitLayout({.width = 100.0F, .height = 20.0F});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidLayout);
    EXPECT_EQ(context->publication().committedStructure().revision(), oldStructureRevision);
    EXPECT_EQ(context->publication().committedLayout().layoutRevision(), oldLayoutRevision);
    expectRectNear(
        requireLayoutEntry(context->publication().committedLayout(), child).worldRect,
        oldChildRect);
    EXPECT_TRUE(context->statistics().layoutDirty);
    EXPECT_GT(context->statistics().dirtyQueuePendingCount, 0U);

    assertOk(updater.setLayoutStyle(child, fixedSize(20.0F, 10.0F)));
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 20.0F}));
    EXPECT_EQ(context->publication().committedLayout().layoutRevision(), oldLayoutRevision + 1);
    expectRectNear(
        requireLayoutEntry(context->publication().committedLayout(), child).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 20.0F, .height = 10.0F});
}

TEST_F(UILayoutTest, PercentLengthsResolveAgainstFinalClampedParentContentBox)
{
    auto context = makeContext({.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
    const UI::UINodeId child = createPanel(*context, panel);

    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle panelStyle = fixedSize(80.0F, 50.0F);
    panelStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(10.0F, 5.0F);
    panelStyle.minMax.maxWidth = UI::UILayoutLength::Px(40.0F);
    assertOk(updater.setLayoutStyle(panel, panelStyle));
    assertOk(updater.setLayoutStyle(child, percentSize(50.0F, 50.0F)));

    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 80.0F}));
    const UI::UICommittedLayoutView layout = context->publication().committedLayout();

    expectRectNear(
        requireLayoutEntry(layout, root.rootNodeId()).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 80.0F});
    expectRectNear(
        requireLayoutEntry(layout, panel).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 40.0F, .height = 50.0F});
    expectRectNear(
        requireLayoutEntry(layout, child).worldRect,
        {.x = 10.0F, .y = 5.0F, .width = 10.0F, .height = 20.0F});
}

TEST_F(UILayoutTest, DefaultAutoRootUsesViewportContentBoxForDirectPercentChild)
{
    auto context = makeContext({.nodeCapacity = 2, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId child = createPanel(*context, root.rootNodeId());
    ASSERT_TRUE(child.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(child, percentSize(50.0F, 25.0F)));
    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));

    const UI::UICommittedLayoutView layout = context->publication().committedLayout();
    expectRectNear(
        requireLayoutEntry(layout, root.rootNodeId()).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 120.0F});
    expectRectNear(
        requireLayoutEntry(layout, child).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 30.0F});
    EXPECT_EQ(context->statistics().lastLayoutPercentMeasureFallbackCount, 0U);
}

TEST_F(UILayoutTest, ViewportChangeRelayoutsWithoutMutationAndSameViewportIsNoOp)
{
    auto context = makeContext({.nodeCapacity = 2, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId child = createPanel(*context, root.rootNodeId());
    ASSERT_TRUE(child.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(child, percentSize(50.0F, 50.0F)));
    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 60.0F}));
    const u64 structureRevision = context->publication().committedStructure().revision();
    const u64 firstLayoutRevision = context->publication().committedLayout().layoutRevision();

    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 60.0F}));
    EXPECT_EQ(context->publication().committedLayout().layoutRevision(), firstLayoutRevision);
    EXPECT_EQ(context->statistics().lastLayoutPassCount, 0U);

    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 80.0F}));
    const UI::UICommittedLayoutView resizedLayout = context->publication().committedLayout();
    EXPECT_EQ(resizedLayout.layoutRevision(), firstLayoutRevision + 1);
    EXPECT_EQ(resizedLayout.structureRevision(), structureRevision);
    EXPECT_EQ(context->publication().committedStructure().revision(), structureRevision);
    EXPECT_EQ(context->statistics().lastLayoutPassCount, 1U);
    EXPECT_FALSE(context->statistics().layoutDirty);
    expectRectNear(
        requireLayoutEntry(resizedLayout, root.rootNodeId()).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 80.0F});
    expectRectNear(
        requireLayoutEntry(resizedLayout, child).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F});
}

TEST_F(UILayoutTest, AutoColumnContainerIncludesChildMarginPaddingAndGap)
{
    auto context = makeContext({.nodeCapacity = 5, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId container = createPanel(*context, root.rootNodeId());
    const UI::UINodeId first = createLabel(*context, container);
    const UI::UINodeId second = createButton(*context, container);

    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle rootStyle;
    rootStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), rootStyle));
    UI::UILayoutStyle containerStyle;
    containerStyle.padding = UI::UIEdgeSpacing::All(4.0F);
    containerStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    containerStyle.flexContainer.gap.row = 3.0F;
    assertOk(updater.setLayoutStyle(container, containerStyle));

    UI::UILayoutStyle firstStyle = fixedSize(20.0F, 10.0F);
    firstStyle.margin = UI::UIEdgeSpacing{.left = 2.0F, .top = 1.0F, .right = 3.0F, .bottom = 2.0F};
    assertOk(updater.setLayoutStyle(first, firstStyle));

    UI::UILayoutStyle secondStyle = fixedSize(30.0F, 12.0F);
    secondStyle.margin = UI::UIEdgeSpacing{.left = 1.0F, .top = 2.0F, .right = 1.0F, .bottom = 3.0F};
    assertOk(updater.setLayoutStyle(second, secondStyle));

    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));
    const UI::UICommittedLayoutView layout = context->publication().committedLayout();

    expectRectNear(
        requireLayoutEntry(layout, container).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 40.0F, .height = 41.0F});
    expectRectNear(
        requireLayoutEntry(layout, first).worldRect,
        {.x = 6.0F, .y = 5.0F, .width = 20.0F, .height = 10.0F});
    expectRectNear(
        requireLayoutEntry(layout, second).worldRect,
        {.x = 5.0F, .y = 22.0F, .width = 30.0F, .height = 12.0F});
}

TEST_F(UILayoutTest, RowAndColumnGrowDistributeRemainingMainAxisSpace)
{
    auto context = makeContext({.nodeCapacity = 9, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId row = createPanel(*context, root.rootNodeId());
    const UI::UINodeId fixedRowChild = createPanel(*context, row);
    const UI::UINodeId growRowChild = createPanel(*context, row);
    const UI::UINodeId secondGrowRowChild = createPanel(*context, row);
    const UI::UINodeId percentGrowGrandchild = createPanel(*context, growRowChild);
    const UI::UINodeId column = createPanel(*context, root.rootNodeId());
    const UI::UINodeId growColumnChild = createPanel(*context, column);
    const UI::UINodeId percentColumnGrandchild = createPanel(*context, growColumnChild);

    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle rowStyle = fixedSize(120.0F, 20.0F);
    rowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    assertOk(updater.setLayoutStyle(row, rowStyle));
    assertOk(updater.setLayoutStyle(fixedRowChild, fixedSize(20.0F, 20.0F)));
    UI::UILayoutStyle growRowStyle = fixedSize(0.0F, 20.0F);
    growRowStyle.flexItem.grow = (std::numeric_limits<float>::max)();
    assertOk(updater.setLayoutStyle(growRowChild, growRowStyle));
    assertOk(updater.setLayoutStyle(secondGrowRowChild, growRowStyle));
    assertOk(updater.setLayoutStyle(percentGrowGrandchild, percentSize(50.0F, 50.0F)));

    UI::UILayoutStyle columnStyle = fixedSize(40.0F, 100.0F);
    columnStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    assertOk(updater.setLayoutStyle(column, columnStyle));
    UI::UILayoutStyle growColumnStyle = fixedSize(40.0F, 0.0F);
    growColumnStyle.flexItem.grow = 1.0F;
    assertOk(updater.setLayoutStyle(growColumnChild, growColumnStyle));
    assertOk(updater.setLayoutStyle(percentColumnGrandchild, percentSize(25.0F, 50.0F)));

    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 200.0F}));
    const UI::UICommittedLayoutView layout = context->publication().committedLayout();

    expectRectNear(
        requireLayoutEntry(layout, fixedRowChild).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 20.0F, .height = 20.0F});
    expectRectNear(
        requireLayoutEntry(layout, growRowChild).worldRect,
        {.x = 20.0F, .y = 0.0F, .width = 50.0F, .height = 20.0F});
    expectRectNear(
        requireLayoutEntry(layout, secondGrowRowChild).worldRect,
        {.x = 70.0F, .y = 0.0F, .width = 50.0F, .height = 20.0F});
    expectRectNear(
        requireLayoutEntry(layout, percentGrowGrandchild).worldRect,
        {.x = 20.0F, .y = 0.0F, .width = 25.0F, .height = 10.0F});
    expectRectNear(
        requireLayoutEntry(layout, growColumnChild).worldRect,
        {.x = 0.0F, .y = 20.0F, .width = 40.0F, .height = 100.0F});
    expectRectNear(
        requireLayoutEntry(layout, percentColumnGrandchild).worldRect,
        {.x = 0.0F, .y = 20.0F, .width = 10.0F, .height = 50.0F});
}

TEST_F(UILayoutTest, JustifyAndAlignPlaceChildrenDeterministically)
{
    auto context = makeContext({.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId row = createPanel(*context, root.rootNodeId());
    const UI::UINodeId first = createPanel(*context, row);
    const UI::UINodeId second = createPanel(*context, row);

    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle rowStyle = fixedSize(100.0F, 50.0F);
    rowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    rowStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
    rowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    assertOk(updater.setLayoutStyle(row, rowStyle));
    assertOk(updater.setLayoutStyle(first, fixedSize(10.0F, 10.0F)));
    assertOk(updater.setLayoutStyle(second, fixedSize(20.0F, 20.0F)));

    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 120.0F, .height = 80.0F}));
    const UI::UICommittedLayoutView layout = context->publication().committedLayout();

    expectRectNear(
        requireLayoutEntry(layout, first).worldRect,
        {.x = 0.0F, .y = 20.0F, .width = 10.0F, .height = 10.0F});
    expectRectNear(
        requireLayoutEntry(layout, second).worldRect,
        {.x = 80.0F, .y = 15.0F, .width = 20.0F, .height = 20.0F});
}

TEST_F(UILayoutTest, FlexItemBasisShrinkAndAlignSelfOverrideParentPolicy)
{
    auto context = makeContext({.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    const UI::UINodeId row = createPanel(*context, root.rootNodeId());
    const UI::UINodeId centered = createPanel(*context, row);
    const UI::UINodeId ended = createPanel(*context, row);
    auto updater = createUpdater(*context, root);

    UI::UILayoutStyle rowStyle = fixedSize(100.0F, 40.0F);
    rowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    rowStyle.flexContainer.alignItems = UI::UIAxisAlignment::End;
    assertOk(updater.setLayoutStyle(row, rowStyle));

    UI::UILayoutStyle centeredStyle = fixedSize(10.0F, 10.0F);
    centeredStyle.flexItem.basis = UI::UILayoutLength::Px(60.0F);
    centeredStyle.flexItem.shrink = 1.0F;
    centeredStyle.flexItem.alignSelf = UI::UIAlignSelf::Center;
    assertOk(updater.setLayoutStyle(centered, centeredStyle));

    UI::UILayoutStyle endedStyle = fixedSize(20.0F, 10.0F);
    endedStyle.flexItem.basis = UI::UILayoutLength::Px(60.0F);
    endedStyle.flexItem.shrink = 1.0F;
    assertOk(updater.setLayoutStyle(ended, endedStyle));

    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 40.0F}));
    const UI::UICommittedLayoutView layout = context->publication().committedLayout();
    expectRectNear(
        requireLayoutEntry(layout, centered).worldRect,
        {.x = 0.0F, .y = 15.0F, .width = 50.0F, .height = 10.0F});
    expectRectNear(
        requireLayoutEntry(layout, ended).worldRect,
        {.x = 50.0F, .y = 30.0F, .width = 50.0F, .height = 10.0F});
}

TEST_F(UILayoutTest, AlignStretchUsesContainerCrossAxisWhenChildCrossSizeIsAuto)
{
    auto context = makeContext({.nodeCapacity = 3, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId row = createPanel(*context, root.rootNodeId());
    const UI::UINodeId child = createPanel(*context, row);

    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle rowStyle = fixedSize(100.0F, 40.0F);
    rowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    rowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Stretch;
    assertOk(updater.setLayoutStyle(row, rowStyle));
    UI::UILayoutStyle childStyle;
    childStyle.size.width = UI::UILayoutLength::Px(25.0F);
    childStyle.size.height = UI::UILayoutLength::Auto();
    assertOk(updater.setLayoutStyle(child, childStyle));

    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 40.0F}));
    expectRectNear(
        requireLayoutEntry(context->publication().committedLayout(), child).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 25.0F, .height = 40.0F});
}

TEST_F(UILayoutTest, GridTracksSpanAndPerItemAlignmentPublishDeterministicRects)
{
    auto context = makeContext({.nodeCapacity = 5, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId grid = createPanel(*context, root.rootNodeId());
    const UI::UINodeId label = createPanel(*context, grid);
    const UI::UINodeId input = createPanel(*context, grid);
    const UI::UINodeId spanning = createPanel(*context, grid);

    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle gridStyle = fixedSize(200.0F, 80.0F);
    gridStyle.containerLayout = UI::UIContainerLayout::Grid;
    gridStyle.gridContainer.columns = UI::UIGridTrackList::Of({
        UI::UIGridTrack::Px(40.0F), UI::UIGridTrack::Auto(),
        UI::UIGridTrack::Fr(),
    });
    gridStyle.gridContainer.rows = UI::UIGridTrackList::Of({
        UI::UIGridTrack::Auto(), UI::UIGridTrack::Fr(),
    });
    gridStyle.gridContainer.gap = {.row = 4.0F, .column = 5.0F};
    gridStyle.gridContainer.alignItems = UI::UIAxisAlignment::Center;
    assertOk(updater.setLayoutStyle(grid, gridStyle));

    UI::UILayoutStyle labelStyle = fixedSize(30.0F, 10.0F);
    labelStyle.gridItem.row = 0U;
    labelStyle.gridItem.column = 0U;
    labelStyle.gridItem.justifySelf = UI::UIAlignSelf::Center;
    assertOk(updater.setLayoutStyle(label, labelStyle));

    UI::UILayoutStyle inputStyle = fixedSize(30.0F, 20.0F);
    inputStyle.gridItem.row = 0U;
    inputStyle.gridItem.column = 1U;
    assertOk(updater.setLayoutStyle(input, inputStyle));

    UI::UILayoutStyle spanningStyle{};
    spanningStyle.size.height = UI::UILayoutLength::Px(10.0F);
    spanningStyle.gridItem.row = 1U;
    spanningStyle.gridItem.column = 0U;
    spanningStyle.gridItem.columnSpan = 3U;
    spanningStyle.gridItem.alignSelf = UI::UIAlignSelf::Center;
    assertOk(updater.setLayoutStyle(spanning, spanningStyle));

    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 240.0F, .height = 100.0F}));
    const UI::UICommittedLayoutView layout = context->publication().committedLayout();
    expectRectNear(requireLayoutEntry(layout, label).worldRect,
                   {.x = 5.0F, .y = 5.0F, .width = 30.0F, .height = 10.0F});
    expectRectNear(requireLayoutEntry(layout, input).worldRect,
                   {.x = 45.0F, .y = 0.0F, .width = 30.0F, .height = 20.0F});
    expectRectNear(requireLayoutEntry(layout, spanning).worldRect,
                   {.x = 0.0F, .y = 47.0F, .width = 200.0F, .height = 10.0F});
}

TEST_F(UILayoutTest, OverlayDoesNotParticipateInFlowAndKeepsSnapshotOrder)
{
    auto context = makeContext({.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId container = createPanel(*context, root.rootNodeId());
    const UI::UINodeId flowChild = createPanel(*context, container);
    const UI::UINodeId overlay = createPanel(*context, container);

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(container, fixedSize(100.0F, 80.0F)));
    assertOk(updater.setLayoutStyle(flowChild, fixedSize(20.0F, 10.0F)));
    UI::UILayoutStyle overlayStyle;
    overlayStyle.placement = UI::UILayoutPlacement::Overlay;
    overlayStyle.overlay.horizontal = UI::UIAxisAlignment::Stretch;
    overlayStyle.overlay.vertical = UI::UIAxisAlignment::Stretch;
    overlayStyle.margin = UI::UIEdgeSpacing{
        .left = 5.0F,
        .top = 6.0F,
        .right = 10.0F,
        .bottom = 11.0F,
    };
    assertOk(updater.setLayoutStyle(overlay, overlayStyle));

    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    const UI::UICommittedLayoutView layout = context->publication().committedLayout();

    ASSERT_EQ(layout.size(), 4U);
    EXPECT_EQ(layout.entries()[0].node, root.rootNodeId());
    EXPECT_EQ(layout.entries()[1].node, container);
    EXPECT_EQ(layout.entries()[2].node, flowChild);
    EXPECT_EQ(layout.entries()[3].node, overlay);
    expectRectNear(
        requireLayoutEntry(layout, flowChild).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 20.0F, .height = 10.0F});
    expectRectNear(
        requireLayoutEntry(layout, overlay).worldRect,
        {.x = 5.0F, .y = 6.0F, .width = 85.0F, .height = 63.0F});
}

TEST_F(UILayoutTest, PanelDescendantClipIsOptInAndKeepsNegativeOverlayGeometry)
{
    auto context = makeContext({.nodeCapacity = 3, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
    const UI::UINodeId overlay = createPanel(*context, panel);

    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle panelStyle = fixedSize(50.0F, 40.0F);
    panelStyle.placement = UI::UILayoutPlacement::Overlay;
    panelStyle.overlay.offset.x = UI::UILayoutLength::Px(40.0F);
    panelStyle.overlay.offset.y = UI::UILayoutLength::Px(30.0F);
    assertOk(updater.setLayoutStyle(panel, panelStyle));

    UI::UILayoutStyle overlayStyle = fixedSize(70.0F, 60.0F);
    overlayStyle.placement = UI::UILayoutPlacement::Overlay;
    overlayStyle.overlay.offset.x = UI::UILayoutLength::Px(-10.0F);
    overlayStyle.overlay.offset.y = UI::UILayoutLength::Px(-5.0F);
    assertOk(updater.setLayoutStyle(overlay, overlayStyle));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));

    constexpr UI::UILogicalRect PanelRect{.x = 40.0F, .y = 30.0F, .width = 50.0F, .height = 40.0F};
    constexpr UI::UILogicalRect OverlayRect{.x = 30.0F, .y = 25.0F, .width = 70.0F, .height = 60.0F};
    const UI::UICommittedLayoutEntry& defaultOverlay =
        requireLayoutEntry(context->publication().committedLayout(), overlay);
    expectRectNear(defaultOverlay.worldRect, OverlayRect);
    expectRectNear(defaultOverlay.effectiveClip, OverlayRect);
    const u64 defaultLayoutRevision = context->publication().committedLayout().layoutRevision();
    const u64 defaultHitRevision = context->publication().committedHit().hitRevision();
    const u64 defaultPaintRevision = context->publication().committedPaint().paintRevision();
    const u64 defaultSemanticsRevision = context->publication().committedSemantics().semanticsRevision();

    panelStyle.clipDescendants = true;
    assertOk(updater.setLayoutStyle(panel, panelStyle));
    UI::UILayoutStyle overflowingOverlay = overlayStyle;
    overflowingOverlay.size.width = UI::UILayoutLength::Px((std::numeric_limits<float>::max)());
    overflowingOverlay.margin.left = (std::numeric_limits<float>::max)();
    assertOk(updater.setLayoutStyle(overlay, overflowingOverlay));
    const Core::Status rejected = context->publication().commitLayout({.width = 200.0F, .height = 120.0F});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidLayout);
    EXPECT_EQ(context->publication().committedLayout().layoutRevision(), defaultLayoutRevision);
    EXPECT_EQ(context->publication().committedHit().hitRevision(), defaultHitRevision);
    EXPECT_EQ(context->publication().committedPaint().paintRevision(), defaultPaintRevision);
    EXPECT_EQ(context->publication().committedSemantics().semanticsRevision(), defaultSemanticsRevision);
    expectRectNear(requireLayoutEntry(context->publication().committedLayout(), overlay).effectiveClip, OverlayRect);

    assertOk(updater.setLayoutStyle(overlay, overlayStyle));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 120.0F}));
    const UI::UICommittedLayoutEntry& clippedOverlay =
        requireLayoutEntry(context->publication().committedLayout(), overlay);
    expectRectNear(requireLayoutEntry(context->publication().committedLayout(), panel).worldRect, PanelRect);
    expectRectNear(clippedOverlay.worldRect, OverlayRect);
    expectRectNear(clippedOverlay.effectiveClip, PanelRect);
}

TEST_F(UILayoutTest, HiddenParticipatesInLayoutButCollapsedIsExcludedFromFlow)
{
    auto context = makeContext({.nodeCapacity = 5, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId row = createPanel(*context, root.rootNodeId());
    const UI::UINodeId visible = createPanel(*context, row);
    const UI::UINodeId hidden = createPanel(*context, row);
    const UI::UINodeId collapsed = createPanel(*context, row);

    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle rootStyle;
    rootStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), rootStyle));
    UI::UILayoutStyle rowStyle;
    rowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    assertOk(updater.setLayoutStyle(row, rowStyle));
    assertOk(updater.setLayoutStyle(visible, fixedSize(10.0F, 10.0F)));

    UI::UILayoutStyle hiddenStyle = fixedSize(20.0F, 10.0F);
    hiddenStyle.visibility = UI::UIVisibility::Hidden;
    assertOk(updater.setLayoutStyle(hidden, hiddenStyle));

    UI::UILayoutStyle collapsedStyle = fixedSize(30.0F, 30.0F);
    collapsedStyle.visibility = UI::UIVisibility::Collapsed;
    assertOk(updater.setLayoutStyle(collapsed, collapsedStyle));

    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 50.0F}));
    const UI::UICommittedLayoutView layout = context->publication().committedLayout();

    expectRectNear(
        requireLayoutEntry(layout, row).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 30.0F, .height = 10.0F});
    expectRectNear(
        requireLayoutEntry(layout, visible).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 10.0F, .height = 10.0F});
    const UI::UICommittedLayoutEntry& hiddenEntry = requireLayoutEntry(layout, hidden);
    expectRectNear(hiddenEntry.worldRect, {.x = 10.0F, .y = 0.0F, .width = 20.0F, .height = 10.0F});
    EXPECT_EQ(hiddenEntry.effectiveVisibility, UI::UIVisibility::Hidden);
    const UI::UICommittedLayoutEntry& collapsedEntry = requireLayoutEntry(layout, collapsed);
    EXPECT_EQ(collapsedEntry.effectiveVisibility, UI::UIVisibility::Collapsed);
    expectRectNear(collapsedEntry.worldRect, {.x = 0.0F, .y = 0.0F, .width = 0.0F, .height = 0.0F});
}

TEST_F(UILayoutTest, MinConstraintWinsWhenMinAndMaxConflict)
{
    auto context = makeContext({.nodeCapacity = 3, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());

    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle style = fixedSize(50.0F, 20.0F);
    style.minMax.minWidth = UI::UILayoutLength::Px(80.0F);
    style.minMax.maxWidth = UI::UILayoutLength::Px(40.0F);
    style.minMax.minHeight = UI::UILayoutLength::Px(30.0F);
    style.minMax.maxHeight = UI::UILayoutLength::Px(10.0F);
    assertOk(updater.setLayoutStyle(panel, style));

    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    expectRectNear(
        requireLayoutEntry(context->publication().committedLayout(), panel).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 80.0F, .height = 30.0F});
}

TEST_F(UILayoutTest, GrowStretchAndOverlayResultsRemainClampedByMinMax)
{
    auto context = makeContext({.nodeCapacity = 7, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId row = createPanel(*context, root.rootNodeId());
    const UI::UINodeId grow = createPanel(*context, row);
    const UI::UINodeId stretch = createPanel(*context, row);
    const UI::UINodeId overlay = createPanel(*context, row);
    const UI::UINodeId stretchPercentChild = createPanel(*context, stretch);
    const UI::UINodeId overlayPercentChild = createPanel(*context, overlay);

    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle rowStyle = fixedSize(300.0F, 20.0F);
    rowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    rowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Stretch;
    assertOk(updater.setLayoutStyle(row, rowStyle));

    UI::UILayoutStyle growStyle = fixedSize(20.0F, 10.0F);
    growStyle.flexItem.grow = 1.0F;
    growStyle.minMax.minWidth = UI::UILayoutLength::Px(60.0F);
    growStyle.minMax.maxWidth = UI::UILayoutLength::Px(80.0F);
    assertOk(updater.setLayoutStyle(grow, growStyle));

    UI::UILayoutStyle stretchStyle;
    stretchStyle.size.width = UI::UILayoutLength::Px(20.0F);
    stretchStyle.size.height = UI::UILayoutLength::Auto();
    stretchStyle.minMax.minHeight = UI::UILayoutLength::Px(30.0F);
    stretchStyle.minMax.maxHeight = UI::UILayoutLength::Px(40.0F);
    assertOk(updater.setLayoutStyle(stretch, stretchStyle));

    UI::UILayoutStyle overlayStyle;
    overlayStyle.placement = UI::UILayoutPlacement::Overlay;
    overlayStyle.overlay.horizontal = UI::UIAxisAlignment::Stretch;
    overlayStyle.overlay.vertical = UI::UIAxisAlignment::Stretch;
    overlayStyle.margin = UI::UIEdgeSpacing::HorizontalVertical(10.0F, 8.0F);
    overlayStyle.minMax.minWidth = UI::UILayoutLength::Px(50.0F);
    overlayStyle.minMax.maxWidth = UI::UILayoutLength::Px(70.0F);
    overlayStyle.minMax.minHeight = UI::UILayoutLength::Px(12.0F);
    overlayStyle.minMax.maxHeight = UI::UILayoutLength::Px(16.0F);
    assertOk(updater.setLayoutStyle(overlay, overlayStyle));
    assertOk(updater.setLayoutStyle(stretchPercentChild, percentSize(50.0F, 50.0F)));
    assertOk(updater.setLayoutStyle(overlayPercentChild, percentSize(50.0F, 50.0F)));

    assertOk(context->publication().commitStructure());
    assertOk(context->publication().commitLayout({.width = 300.0F, .height = 20.0F}));
    const UI::UICommittedLayoutView layout = context->publication().committedLayout();
    expectRectNear(
        requireLayoutEntry(layout, grow).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 80.0F, .height = 10.0F});
    expectRectNear(
        requireLayoutEntry(layout, stretch).worldRect,
        {.x = 80.0F, .y = 0.0F, .width = 20.0F, .height = 30.0F});
    expectRectNear(
        requireLayoutEntry(layout, overlay).worldRect,
        {.x = 10.0F, .y = 8.0F, .width = 70.0F, .height = 12.0F});
    expectRectNear(
        requireLayoutEntry(layout, stretchPercentChild).worldRect,
        {.x = 80.0F, .y = 0.0F, .width = 10.0F, .height = 15.0F});
    expectRectNear(
        requireLayoutEntry(layout, overlayPercentChild).worldRect,
        {.x = 10.0F, .y = 8.0F, .width = 35.0F, .height = 6.0F});
}

TEST_F(UILayoutTest, FlexWrapUsesFinalMainConstraintAndAutoCrossSize)
{
    auto context = makeContext({.nodeCapacity = 5, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId wrap = createPanel(*context, root.rootNodeId());
    const UI::UINodeId first = createPanel(*context, wrap);
    const UI::UINodeId second = createPanel(*context, wrap);
    const UI::UINodeId third = createPanel(*context, wrap);

    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle rootStyle{};
    rootStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), rootStyle));
    UI::UILayoutStyle wrapStyle{};
    wrapStyle.size.width = UI::UILayoutLength::Px(100.0F);
    wrapStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    wrapStyle.flexContainer.wrap = UI::UIFlexWrap::Wrap;
    wrapStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    wrapStyle.flexContainer.gap = {.row = 7.0F, .column = 5.0F};
    assertOk(updater.setLayoutStyle(wrap, wrapStyle));
    assertOk(updater.setLayoutStyle(first, fixedSize(40.0F, 10.0F)));
    assertOk(updater.setLayoutStyle(second, fixedSize(40.0F, 20.0F)));
    assertOk(updater.setLayoutStyle(third, fixedSize(40.0F, 30.0F)));

    assertOk(context->publication().commitLayout(
        {.width = 100.0F, .height = 100.0F}));
    const auto layout = context->publication().committedLayout();
    expectRectNear(requireLayoutEntry(layout, wrap).worldRect,
                   {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 57.0F});
    expectRectNear(requireLayoutEntry(layout, first).worldRect,
                   {.x = 0.0F, .y = 0.0F, .width = 40.0F, .height = 10.0F});
    expectRectNear(requireLayoutEntry(layout, second).worldRect,
                   {.x = 45.0F, .y = 0.0F, .width = 40.0F, .height = 20.0F});
    expectRectNear(requireLayoutEntry(layout, third).worldRect,
                   {.x = 0.0F, .y = 27.0F, .width = 40.0F, .height = 30.0F});
}

TEST_F(UILayoutTest, FlexAlignContentDistributesWrappedRowsAndColumns)
{
    auto context = makeContext({.nodeCapacity = 9, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId row = createPanel(*context, root.rootNodeId());
    const UI::UINodeId rowFirst = createPanel(*context, row);
    const UI::UINodeId rowSecond = createPanel(*context, row);
    const UI::UINodeId rowThird = createPanel(*context, row);
    const UI::UINodeId column = createPanel(*context, root.rootNodeId());
    const UI::UINodeId columnFirst = createPanel(*context, column);
    const UI::UINodeId columnSecond = createPanel(*context, column);
    const UI::UINodeId columnThird = createPanel(*context, column);

    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle rootStyle{};
    rootStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), rootStyle));

    UI::UILayoutStyle rowStyle = fixedSize(100.0F, 100.0F);
    rowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    rowStyle.flexContainer.wrap = UI::UIFlexWrap::Wrap;
    rowStyle.flexContainer.alignContent = UI::UIAlignContent::Center;
    rowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    rowStyle.flexContainer.gap = {.row = 10.0F, .column = 5.0F};
    assertOk(updater.setLayoutStyle(row, rowStyle));
    assertOk(updater.setLayoutStyle(rowFirst, fixedSize(40.0F, 10.0F)));
    assertOk(updater.setLayoutStyle(rowSecond, fixedSize(40.0F, 10.0F)));
    assertOk(updater.setLayoutStyle(rowThird, fixedSize(40.0F, 10.0F)));

    UI::UILayoutStyle columnStyle = fixedSize(100.0F, 100.0F);
    columnStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    columnStyle.flexContainer.wrap = UI::UIFlexWrap::Wrap;
    columnStyle.flexContainer.alignContent = UI::UIAlignContent::End;
    columnStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    columnStyle.flexContainer.gap = {.row = 5.0F, .column = 10.0F};
    assertOk(updater.setLayoutStyle(column, columnStyle));
    assertOk(updater.setLayoutStyle(columnFirst, fixedSize(10.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(columnSecond, fixedSize(10.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(columnThird, fixedSize(10.0F, 40.0F)));

    assertOk(context->publication().commitLayout(
        {.width = 200.0F, .height = 220.0F}));
    const auto centeredAndEnded = context->publication().committedLayout();
    expectRectNear(requireLayoutEntry(centeredAndEnded, rowFirst).worldRect,
                   {.x = 0.0F, .y = 35.0F, .width = 40.0F, .height = 10.0F});
    expectRectNear(requireLayoutEntry(centeredAndEnded, rowThird).worldRect,
                   {.x = 0.0F, .y = 55.0F, .width = 40.0F, .height = 10.0F});
    expectRectNear(requireLayoutEntry(centeredAndEnded, columnFirst).worldRect,
                   {.x = 70.0F, .y = 100.0F, .width = 10.0F, .height = 40.0F});
    expectRectNear(requireLayoutEntry(centeredAndEnded, columnThird).worldRect,
                   {.x = 90.0F, .y = 100.0F, .width = 10.0F, .height = 40.0F});

    rowStyle.flexContainer.alignContent = UI::UIAlignContent::Stretch;
    rowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    assertOk(updater.setLayoutStyle(row, rowStyle));
    assertOk(context->publication().commitLayout(
        {.width = 200.0F, .height = 220.0F}));
    const auto stretched = context->publication().committedLayout();
    expectRectNear(requireLayoutEntry(stretched, rowFirst).worldRect,
                   {.x = 0.0F, .y = 17.5F, .width = 40.0F, .height = 10.0F});
    expectRectNear(requireLayoutEntry(stretched, rowThird).worldRect,
                   {.x = 0.0F, .y = 72.5F, .width = 40.0F, .height = 10.0F});
}

TEST_F(UILayoutTest, ResponsiveRulesResolveAgainstDirectParentContentWidth)
{
    auto context = makeContext({.nodeCapacity = 8, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId directionContainer =
        createPanel(*context, root.rootNodeId());
    const UI::UINodeId first = createPanel(*context, directionContainer);
    const UI::UINodeId second = createPanel(*context, directionContainer);

    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle directionStyle{};
    directionStyle.size.width = UI::UILayoutLength::Percent(100.0F);
    directionStyle.size.height = UI::UILayoutLength::Px(100.0F);
    directionStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    directionStyle.responsiveRules = UI::UIResponsiveLayoutRuleList::Of({
        {
            .minParentWidth = 0.0F,
            .maxParentWidth = 300.0F,
            .overrides = {
                .flexDirection = UI::UIFlexDirection::Row,
                .gap = UI::UILayoutGap::All(5.0F),
                .padding = UI::UIEdgeSpacing::All(10.0F),
                .minMax = UI::UILayoutMinMaxSpec{
                    .maxWidth = UI::UILayoutLength::Px(180.0F),
                },
            },
        },
    });
    assertOk(updater.setLayoutStyle(directionContainer, directionStyle));
    assertOk(updater.setLayoutStyle(first, fixedSize(40.0F, 20.0F)));
    assertOk(updater.setLayoutStyle(second, fixedSize(40.0F, 20.0F)));

    assertOk(context->publication().commitLayout(
        {.width = 250.0F, .height = 100.0F}));
    auto layout = context->publication().committedLayout();
    expectRectNear(requireLayoutEntry(layout, directionContainer).worldRect,
                   {.x = 0.0F, .y = 0.0F, .width = 180.0F, .height = 100.0F});
    expectRectNear(requireLayoutEntry(layout, first).worldRect,
                   {.x = 10.0F, .y = 10.0F, .width = 40.0F, .height = 20.0F});
    expectRectNear(requireLayoutEntry(layout, second).worldRect,
                   {.x = 55.0F, .y = 10.0F, .width = 40.0F, .height = 20.0F});

    assertOk(context->publication().commitLayout(
        {.width = 400.0F, .height = 100.0F}));
    layout = context->publication().committedLayout();
    expectRectNear(requireLayoutEntry(layout, first).worldRect,
                   {.x = 0.0F, .y = 0.0F, .width = 40.0F, .height = 20.0F});
    expectRectNear(requireLayoutEntry(layout, second).worldRect,
                   {.x = 0.0F, .y = 20.0F, .width = 40.0F, .height = 20.0F});
}

TEST_F(UILayoutTest, ResponsiveRulesSwitchGridTracksAndVisibility)
{
    auto context = makeContext({.nodeCapacity = 8, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId grid = createPanel(*context, root.rootNodeId());
    const UI::UINodeId first = createPanel(*context, grid);
    const UI::UINodeId optional = createPanel(*context, grid);

    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle gridStyle{};
    gridStyle.size.width = UI::UILayoutLength::Percent(100.0F);
    gridStyle.size.height = UI::UILayoutLength::Px(100.0F);
    gridStyle.containerLayout = UI::UIContainerLayout::Grid;
    gridStyle.gridContainer.columns =
        UI::UIGridTrackList::Of({UI::UIGridTrack::Fr(1.0F)});
    gridStyle.responsiveRules = UI::UIResponsiveLayoutRuleList::Of({
        {
            .minParentWidth = 0.0F,
            .maxParentWidth = 200.0F,
            .overrides = {
                .gridColumns = UI::UIGridTrackList::Of({
                    UI::UIGridTrack::Fr(1.0F),
                    UI::UIGridTrack::Fr(1.0F),
                }),
            },
        },
    });
    assertOk(updater.setLayoutStyle(grid, gridStyle));
    assertOk(updater.setLayoutStyle(first, fixedSize(10.0F, 10.0F)));
    UI::UILayoutStyle optionalStyle = fixedSize(10.0F, 10.0F);
    optionalStyle.responsiveRules = UI::UIResponsiveLayoutRuleList::Of({
        {
            .minParentWidth = 200.0F,
            .maxParentWidth = 400.0F,
            .overrides = {.visibility = UI::UIVisibility::Collapsed},
        },
    });
    assertOk(updater.setLayoutStyle(optional, optionalStyle));

    assertOk(context->publication().commitLayout(
        {.width = 100.0F, .height = 100.0F}));
    auto layout = context->publication().committedLayout();
    expectRectNear(requireLayoutEntry(layout, optional).worldRect,
                   {.x = 50.0F, .y = 0.0F, .width = 10.0F, .height = 10.0F});

    assertOk(context->publication().commitLayout(
        {.width = 300.0F, .height = 100.0F}));
    layout = context->publication().committedLayout();
    const auto& collapsed = requireLayoutEntry(layout, optional);
    EXPECT_EQ(collapsed.effectiveVisibility, UI::UIVisibility::Collapsed);
    expectRectNear(collapsed.worldRect, {});
}


} // namespace
} // namespace Tina::Tests
