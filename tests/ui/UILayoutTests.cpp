#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <memory_resource>

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

[[nodiscard]] UI::UINodeId createPanel(UI::UIContext& context, UI::UINodeId parent)
{
    auto panelResult = context.rootBuilder().createElement(parent, UI::makePanelElement());
    EXPECT_TRUE(panelResult.has_value())
        << (panelResult ? "" : panelResult.error().message);
    return panelResult ? *panelResult : UI::UINodeId{};
}

[[nodiscard]] UI::UINodeId createLabel(UI::UIContext& context, UI::UINodeId parent)
{
    auto labelResult = context.rootBuilder().createElement(parent, UI::makeLabelElement());
    EXPECT_TRUE(labelResult.has_value())
        << (labelResult ? "" : labelResult.error().message);
    return labelResult ? *labelResult : UI::UINodeId{};
}

[[nodiscard]] UI::UINodeId createButton(UI::UIContext& context, UI::UINodeId parent)
{
    auto buttonResult = context.rootBuilder().createElement(parent, UI::makeButtonElement());
    EXPECT_TRUE(buttonResult.has_value())
        << (buttonResult ? "" : buttonResult.error().message);
    return buttonResult ? *buttonResult : UI::UINodeId{};
}

[[nodiscard]] UI::UITreeUpdater createUpdater(UI::UIContext& context, UI::UIRootOwner& root)
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

[[nodiscard]] UI::UILayoutStyle percentSize(float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Percent(width);
    style.size.height = UI::UILayoutLength::Percent(height);
    return style;
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] const UI::UICommittedLayoutEntry* findLayoutEntry(
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

const UI::UICommittedLayoutEntry& requireLayoutEntry(
    UI::UICommittedLayoutView view,
    UI::UINodeId node)
{
    const UI::UICommittedLayoutEntry* entry = findLayoutEntry(view, node);
    EXPECT_NE(entry, nullptr);
    static const UI::UICommittedLayoutEntry EmptyEntry{};
    return entry != nullptr ? *entry : EmptyEntry;
}

void expectRectNear(UI::UILogicalRect actual, UI::UILogicalRect expected)
{
    constexpr float Epsilon = 0.001F;
    EXPECT_NEAR(actual.x, expected.x, Epsilon);
    EXPECT_NEAR(actual.y, expected.y, Epsilon);
    EXPECT_NEAR(actual.width, expected.width, Epsilon);
    EXPECT_NEAR(actual.height, expected.height, Epsilon);
}

class UILayoutTest : public testing::Test {
protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(1);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));

        auto windowResult = windows->tryEmplace(7);
        ASSERT_TRUE(windowResult.has_value());
        window = *windowResult;
    }

    [[nodiscard]] std::unique_ptr<UI::UIContext> makeContext(
        UI::UIContextCapacityConfig capacities = {},
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource()) const
    {
        return createContext(window, capacities, resource);
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
};

TEST_F(UILayoutTest, RejectsNonFiniteAndNegativeStylesWithoutDirtyingLayout)
{
    auto context = makeContext({.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
    ASSERT_TRUE(panel.hasValue());
    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));
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

    assertOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));
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
    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));

    const UI::UICommittedStructureView baselineStructure = context->committedStructure();
    const UI::UICommittedLayoutView baselineLayout = context->committedLayout();
    const u64 structureRevision = baselineStructure.revision();
    const u64 layoutRevision = baselineLayout.layoutRevision();
    const usize structureSize = baselineStructure.size();
    const usize layoutSize = baselineLayout.size();
    const UI::UILogicalRect committedPanelRect =
        requireLayoutEntry(baselineLayout, panel).worldRect;

    const auto expectCommittedSnapshotUnchanged = [&] {
        const UI::UICommittedStructureView structure = context->committedStructure();
        const UI::UICommittedLayoutView layout = context->committedLayout();
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
        const Core::Status status = context->commitLayout(viewport);
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

    assertOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));
    EXPECT_EQ(context->committedLayout().layoutRevision(), layoutRevision + 1);
    expectRectNear(
        requireLayoutEntry(context->committedLayout(), panel).worldRect,
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
    assertOk(context->commitLayout({.width = 100.0F, .height = 20.0F}));

    const u64 oldStructureRevision = context->committedStructure().revision();
    const u64 oldLayoutRevision = context->committedLayout().layoutRevision();
    const UI::UILogicalRect oldChildRect =
        requireLayoutEntry(context->committedLayout(), child).worldRect;

    UI::UILayoutStyle overflowingStyle = fixedSize(
        (std::numeric_limits<float>::max)(),
        10.0F);
    overflowingStyle.margin.left = (std::numeric_limits<float>::max)();
    assertOk(updater.setLayoutStyle(child, overflowingStyle));

    const Core::Status rejected =
        context->commitLayout({.width = 100.0F, .height = 20.0F});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidLayout);
    EXPECT_EQ(context->committedStructure().revision(), oldStructureRevision);
    EXPECT_EQ(context->committedLayout().layoutRevision(), oldLayoutRevision);
    expectRectNear(
        requireLayoutEntry(context->committedLayout(), child).worldRect,
        oldChildRect);
    EXPECT_TRUE(context->statistics().layoutDirty);
    EXPECT_GT(context->statistics().dirtyQueuePendingCount, 0U);

    assertOk(updater.setLayoutStyle(child, fixedSize(20.0F, 10.0F)));
    assertOk(context->commitLayout({.width = 100.0F, .height = 20.0F}));
    EXPECT_EQ(context->committedLayout().layoutRevision(), oldLayoutRevision + 1);
    expectRectNear(
        requireLayoutEntry(context->committedLayout(), child).worldRect,
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

    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 100.0F, .height = 80.0F}));
    const UI::UICommittedLayoutView layout = context->committedLayout();

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
    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));

    const UI::UICommittedLayoutView layout = context->committedLayout();
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
    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 100.0F, .height = 60.0F}));
    const u64 structureRevision = context->committedStructure().revision();
    const u64 firstLayoutRevision = context->committedLayout().layoutRevision();

    assertOk(context->commitLayout({.width = 100.0F, .height = 60.0F}));
    EXPECT_EQ(context->committedLayout().layoutRevision(), firstLayoutRevision);
    EXPECT_EQ(context->statistics().lastLayoutPassCount, 0U);

    assertOk(context->commitLayout({.width = 200.0F, .height = 80.0F}));
    const UI::UICommittedLayoutView resizedLayout = context->committedLayout();
    EXPECT_EQ(resizedLayout.layoutRevision(), firstLayoutRevision + 1);
    EXPECT_EQ(resizedLayout.structureRevision(), structureRevision);
    EXPECT_EQ(context->committedStructure().revision(), structureRevision);
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

    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 200.0F, .height = 100.0F}));
    const UI::UICommittedLayoutView layout = context->committedLayout();

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

    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 200.0F, .height = 200.0F}));
    const UI::UICommittedLayoutView layout = context->committedLayout();

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

    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 120.0F, .height = 80.0F}));
    const UI::UICommittedLayoutView layout = context->committedLayout();

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

    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));
    const UI::UICommittedLayoutView layout = context->committedLayout();
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

    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));
    expectRectNear(
        requireLayoutEntry(context->committedLayout(), child).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 25.0F, .height = 40.0F});
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

    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    const UI::UICommittedLayoutView layout = context->committedLayout();

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

    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));
    const UI::UICommittedLayoutView layout = context->committedLayout();

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

    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    expectRectNear(
        requireLayoutEntry(context->committedLayout(), panel).worldRect,
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

    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 300.0F, .height = 20.0F}));
    const UI::UICommittedLayoutView layout = context->committedLayout();
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
    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const u64 layoutRevision = context->committedLayout().layoutRevision();
    const UI::UIContextStatistics before = context->statistics();

    assertOk(updater.setLayoutStyle(panel, style));
    const UI::UIContextStatistics afterSet = context->statistics();
    EXPECT_EQ(afterSet.dirtyQueuePendingCount, before.dirtyQueuePendingCount);
    EXPECT_FALSE(afterSet.layoutDirty);
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    EXPECT_EQ(context->committedLayout().layoutRevision(), layoutRevision);
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

    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
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
    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));

    const u64 layoutRevision = context->committedLayout().layoutRevision();
    const usize allocationCount = resource.allocationCount();
    for (usize frame = 0; frame < 300; ++frame) {
        assertOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));
        EXPECT_EQ(context->committedLayout().layoutRevision(), layoutRevision);
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

    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 200.0F, .height = 50.0F}));
    const UI::UIContextStatistics stats = context->statistics();
    EXPECT_GT(stats.lastLayoutPercentMeasureFallbackCount, 0U);
    expectRectNear(
        requireLayoutEntry(context->committedLayout(), autoPanel).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 10.0F});
    expectRectNear(
        requireLayoutEntry(context->committedLayout(), percentChild).worldRect,
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
    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const UI::UICommittedLayoutView oldLayout = context->committedLayout();
    const UI::UICommittedStructureView oldStructure = context->committedStructure();
    ASSERT_EQ(oldLayout.size(), 2U);
    ASSERT_EQ(oldStructure.size(), 2U);
    const u64 oldLayoutRevision = oldLayout.layoutRevision();
    const u64 oldStructureRevisionInLayout = oldLayout.structureRevision();
    const u64 oldStructureRevision = oldStructure.revision();

    const UI::UINodeId second = createPanel(*context, root.rootNodeId());
    assertOk(updater.setLayoutStyle(second, fixedSize(20.0F, 20.0F)));

    const Core::Status status = context->commitLayout({.width = 100.0F, .height = 100.0F});
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, UI::UIErrorCode::CapacityExceeded);
    const UI::UICommittedLayoutView afterFailure = context->committedLayout();
    EXPECT_EQ(afterFailure.layoutRevision(), oldLayoutRevision);
    EXPECT_EQ(afterFailure.structureRevision(), oldStructureRevisionInLayout);
    EXPECT_EQ(afterFailure.size(), oldLayout.size());
    const UI::UICommittedStructureView structureAfterFailure = context->committedStructure();
    EXPECT_EQ(structureAfterFailure.revision(), oldStructureRevision);
    EXPECT_EQ(structureAfterFailure.size(), oldStructure.size());
    EXPECT_TRUE(std::none_of(
        structureAfterFailure.begin(),
        structureAfterFailure.end(),
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
    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(first, fixedSize(10.0F, 10.0F)));
    const Core::Status rejected = updater.setLayoutStyle(second, fixedSize(20.0F, 20.0F));
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 2U);

    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const UI::UICommittedLayoutView layout = context->committedLayout();
    expectRectNear(
        requireLayoutEntry(layout, first).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 10.0F, .height = 10.0F});
    expectRectNear(
        requireLayoutEntry(layout, second).worldRect,
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
    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

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

    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const UI::UICommittedLayoutView layout = context->committedLayout();
    expectRectNear(
        requireLayoutEntry(layout, sibling).worldRect,
        {.x = 0.0F, .y = 0.0F, .width = 40.0F, .height = 10.0F});
    expectRectNear(
        requireLayoutEntry(layout, reused).worldRect,
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
        assertOk(context->commitStructure());
        assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
        EXPECT_GT(resource.currentBytes(), 0U);
        EXPECT_GT(resource.peakBytes(), 0U);
    }

    EXPECT_EQ(resource.currentBytes(), 0U);
    EXPECT_EQ(resource.allocationCount(), resource.deallocationCount());
}

} // namespace
} // namespace Tina::Tests
