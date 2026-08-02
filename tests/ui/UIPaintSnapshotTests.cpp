#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <algorithm>
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
        ++m_deallocationCount;
        m_currentBytes -= bytes;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(
        const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize m_allocationCount = 0;
    usize m_deallocationCount = 0;
    usize m_currentBytes = 0;
};

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window,
    UI::UIContextCapacityConfig capacities,
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    capacities.applyDefaultProductChrome = false;
    auto result = UI::UIContext::Create(window, capacities, resource);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

[[nodiscard]] UI::UINodeId createPanel(
    UI::UIContext& context,
    UI::UINodeId parent)
{
    auto result = context.rootBuilder().createElement(parent, UI::makePanelElement());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] UI::UITreeUpdater createUpdater(
    UI::UIContext& context,
    UI::UIRootOwner& root)
{
    auto result = context.treeUpdater(root);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UITreeUpdater{};
}

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
    return UI::UIBoxPaint{
        .solidFill = UI::UISolidFill{.color = UI::rgba8(red, green, blue, alpha)},
    };
}

TEST(UIPaintColorHelpers, HexAndChannelHelpersMatchExplicitChannels)
{
    constexpr UI::UIStraightSrgba8Color fromChannels = UI::rgba8(110, 130, 230, 200);
    constexpr UI::UIStraightSrgba8Color fromRgb = UI::rgb(0x6E82E6, 200);
    constexpr UI::UIStraightSrgba8Color fromArgb = UI::argb(0xC86E82E6);
    EXPECT_EQ(fromChannels, fromRgb);
    EXPECT_EQ(fromChannels, fromArgb);
    EXPECT_EQ(UI::rgb(0xFF0000).red, 255);
    EXPECT_EQ(UI::rgb(0x00FF00).green, 255);
    EXPECT_EQ(UI::rgb(0x0000FF).blue, 255);
    EXPECT_EQ(UI::rgb(0x112233).alpha, 255);
    EXPECT_EQ(UI::argb(0x80112233).alpha, 0x80);
}

void expectOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] const UI::UICommittedPaintEntry* findPaintEntry(
    UI::UICommittedPaintView view,
    UI::UINodeId node) noexcept
{
    const auto found = std::ranges::find_if(
        view.entries(),
        [node](const UI::UICommittedPaintEntry& entry) {
            return entry.node == node;
        });
    return found == view.end() ? nullptr : &*found;
}

[[nodiscard]] const UI::UICommittedLayoutEntry* findLayoutEntry(
    UI::UICommittedLayoutView view,
    UI::UINodeId node) noexcept
{
    const auto found = std::ranges::find_if(
        view.entries(),
        [node](const UI::UICommittedLayoutEntry& entry) {
            return entry.node == node;
        });
    return found == view.end() ? nullptr : &*found;
}

class UIPaintSnapshotTest : public testing::Test {
protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(1);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto windowResult = windows->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());
        window = *windowResult;
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
};

TEST_F(UIPaintSnapshotTest, IntegerColorPremultiplicationAndDerivedCapacityAreDeterministic)
{
    constexpr UI::UIPremultipliedRgba8Color premultiplied = UI::premultiply({
        .red = 255,
        .green = 64,
        .blue = 1,
        .alpha = 128,
    });
    static_assert(premultiplied.red == 128);
    static_assert(premultiplied.green == 32);
    static_assert(premultiplied.blue == 1);
    static_assert(premultiplied.alpha == 128);

    auto context = createContext(window, {.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context->statistics().paintSnapshotCapacity, 4U);
    EXPECT_EQ(context->statistics().committedPaintNodeCount, 0U);
    EXPECT_EQ(context->statistics().paintRevision, 0U);
    const UI::UICommittedPaintView empty = context->committedPaint();
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(empty.viewportSize(), UI::UILogicalSize{});
    EXPECT_EQ(empty.structureRevision(), 0U);
    EXPECT_EQ(empty.layoutRevision(), 0U);
    EXPECT_EQ(empty.paintOrderRevision(), 0U);
    EXPECT_EQ(empty.paintRevision(), 0U);

    const auto expanded = UI::UIContext::Create(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 4,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 5,
        });
    ASSERT_TRUE(expanded.has_value()) << (expanded ? "" : expanded.error().message);
    EXPECT_EQ((*expanded)->statistics().paintSnapshotCapacity, 5U);
}

TEST_F(UIPaintSnapshotTest, PublishesOnlyVisibleNonTransparentSolidFillsInPaintOrder)
{
    auto context = createContext(window, {.nodeCapacity = 6, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId painted = createPanel(*context, root.rootNodeId());
    const UI::UINodeId transparent = createPanel(*context, root.rootNodeId());
    const UI::UINodeId hidden = createPanel(*context, root.rootNodeId());
    const UI::UINodeId collapsed = createPanel(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);

    expectOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 80.0F)));
    expectOk(updater.setLayoutStyle(painted, fixedSize(40.0F, 20.0F)));
    expectOk(updater.setLayoutStyle(transparent, fixedSize(30.0F, 10.0F)));
    UI::UILayoutStyle hiddenStyle = fixedSize(20.0F, 10.0F);
    hiddenStyle.visibility = UI::UIVisibility::Hidden;
    expectOk(updater.setLayoutStyle(hidden, hiddenStyle));
    UI::UILayoutStyle collapsedStyle = fixedSize(20.0F, 10.0F);
    collapsedStyle.visibility = UI::UIVisibility::Collapsed;
    expectOk(updater.setLayoutStyle(collapsed, collapsedStyle));
    expectOk(updater.setBoxPaint(painted, solidFill(200, 100, 50, 128)));
    expectOk(updater.setBoxPaint(transparent, solidFill(255, 1, 1, 0)));
    expectOk(updater.setBoxPaint(hidden, solidFill(1, 2, 3)));
    expectOk(updater.setBoxPaint(collapsed, solidFill(4, 5, 6)));
    expectOk(context->commitLayout({.width = 100.0F, .height = 80.0F}));

    const UI::UICommittedPaintView paint = context->committedPaint();
    ASSERT_EQ(paint.size(), 1U);
    const UI::UICommittedPaintEntry& entry = paint.entries().front();
    EXPECT_EQ(entry.node, painted);
    EXPECT_EQ(entry.worldRect, context->committedLayout().entries()[1].worldRect);
    EXPECT_EQ(entry.effectiveClip, context->committedLayout().entries()[1].effectiveClip);
    // Paint ordinals are unique within the paint snapshot. They track paint
    // emission order (box fill then optional text fallback quads) and no longer
    // need to equal layout paint ordinals once a node can emit multiple quads.
    EXPECT_EQ(entry.paintOrdinal, 1U);
    EXPECT_EQ(
        entry.solidFill,
        (UI::UIPremultipliedRgba8Color{
            .red = 100,
            .green = 50,
            .blue = 25,
            .alpha = 128,
        }));
    EXPECT_EQ(paint.viewportSize(), (UI::UILogicalSize{100.0F, 80.0F}));
    EXPECT_EQ(paint.structureRevision(), context->committedStructure().revision());
    EXPECT_EQ(paint.layoutRevision(), context->committedLayout().layoutRevision());
    EXPECT_EQ(paint.paintOrderRevision(), context->committedHit().paintOrderRevision());
    // The explicit transparent paint still rebuilds once: it is a local
    // override that must be able to suppress a stylesheet fill.
    EXPECT_EQ(context->statistics().lastPaintCacheRebuildCount, 4U);
    EXPECT_EQ(context->statistics().lastPaintSnapshotRebuildCount, 1U);
    EXPECT_EQ(findPaintEntry(paint, root.rootNodeId()), nullptr);
    EXPECT_EQ(findPaintEntry(paint, transparent), nullptr);
    EXPECT_EQ(findPaintEntry(paint, hidden), nullptr);
    EXPECT_EQ(findPaintEntry(paint, collapsed), nullptr);
}

TEST_F(UIPaintSnapshotTest, MultipleRootsAndSiblingsPublishUniqueStrictPaintOrdinals)
{
    auto context = createContext(window, {.nodeCapacity = 8, .rootCapacity = 2});
    ASSERT_NE(context, nullptr);
    auto firstRoot = createRoot(*context);
    auto secondRoot = createRoot(*context);
    ASSERT_TRUE(firstRoot);
    ASSERT_TRUE(secondRoot);

    const UI::UINodeId firstSibling =
        createPanel(*context, firstRoot.rootNodeId());
    const UI::UINodeId secondSibling =
        createPanel(*context, firstRoot.rootNodeId());
    const UI::UINodeId nested = createPanel(*context, firstSibling);
    const UI::UINodeId secondRootChild =
        createPanel(*context, secondRoot.rootNodeId());
    auto firstUpdater = createUpdater(*context, firstRoot);
    auto secondUpdater = createUpdater(*context, secondRoot);

    expectOk(firstUpdater.setLayoutStyle(firstSibling, fixedSize(40.0F, 20.0F)));
    expectOk(firstUpdater.setLayoutStyle(secondSibling, fixedSize(30.0F, 10.0F)));
    expectOk(firstUpdater.setLayoutStyle(nested, fixedSize(10.0F, 5.0F)));
    expectOk(secondUpdater.setLayoutStyle(secondRootChild, fixedSize(50.0F, 25.0F)));
    expectOk(firstUpdater.setBoxPaint(firstSibling, solidFill(10, 20, 30)));
    expectOk(firstUpdater.setBoxPaint(secondSibling, solidFill(40, 50, 60)));
    expectOk(firstUpdater.setBoxPaint(nested, solidFill(70, 80, 90)));
    expectOk(secondUpdater.setBoxPaint(secondRootChild, solidFill(100, 110, 120)));
    expectOk(context->commitLayout({.width = 100.0F, .height = 80.0F}));

    const UI::UICommittedPaintView paint = context->committedPaint();
    const UI::UICommittedLayoutView layout = context->committedLayout();
    ASSERT_EQ(paint.size(), 4U);
    EXPECT_EQ(paint.paintOrderRevision(), context->committedHit().paintOrderRevision());
    EXPECT_EQ(paint.structureRevision(), layout.structureRevision());
    EXPECT_EQ(paint.layoutRevision(), layout.layoutRevision());

    for (usize index = 0; index < paint.size(); ++index) {
        const UI::UICommittedPaintEntry& entry = paint.entries()[index];
        const UI::UICommittedLayoutEntry* const layoutEntry =
            findLayoutEntry(layout, entry.node);
        ASSERT_NE(layoutEntry, nullptr);
        EXPECT_EQ(entry.worldRect, layoutEntry->worldRect);
        if (index != 0) {
            EXPECT_LT(
                paint.entries()[index - 1].paintOrdinal,
                entry.paintOrdinal);
        }
    }
}

TEST_F(UIPaintSnapshotTest, PaintOnlyCommitDoesNotRelayoutOrRebuildHitAndSameValueIsNoOp)
{
    auto context = createContext(window, {.nodeCapacity = 3, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    expectOk(updater.setLayoutStyle(panel, fixedSize(40.0F, 20.0F)));
    expectOk(updater.setBoxPaint(panel, solidFill(10, 20, 30)));
    expectOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));

    const u64 structureRevision = context->committedStructure().revision();
    const u64 layoutRevision = context->committedLayout().layoutRevision();
    const u64 hitRevision = context->committedHit().hitRevision();
    const u64 paintRevision = context->committedPaint().paintRevision();
    const UI::UIBoxPaint changed = solidFill(80, 40, 20, 128);
    expectOk(updater.setBoxPaint(panel, changed));
    EXPECT_TRUE(context->statistics().paintDirty);
    expectOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));

    EXPECT_EQ(context->committedStructure().revision(), structureRevision);
    EXPECT_EQ(context->committedLayout().layoutRevision(), layoutRevision);
    EXPECT_EQ(context->committedHit().hitRevision(), hitRevision);
    EXPECT_EQ(context->committedPaint().paintRevision(), paintRevision + 1U);
    EXPECT_EQ(context->statistics().lastLayoutPassCount, 0U);
    EXPECT_EQ(context->statistics().lastHitRebuildCount, 0U);
    EXPECT_EQ(context->statistics().lastPaintCacheRebuildCount, 1U);
    EXPECT_EQ(context->statistics().lastPaintSnapshotRebuildCount, 1U);

    expectOk(updater.setBoxPaint(panel, changed));
    EXPECT_FALSE(context->statistics().paintDirty);
    const UI::UICommittedPaintEntry* const data =
        context->committedPaint().entries().data();
    expectOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));
    EXPECT_EQ(context->committedPaint().paintRevision(), paintRevision + 1U);
    EXPECT_EQ(context->committedPaint().entries().data(), data);
    EXPECT_EQ(context->statistics().lastPaintCacheRebuildCount, 0U);
    EXPECT_EQ(context->statistics().lastPaintSnapshotRebuildCount, 0U);

    expectOk(updater.setBoxPaint(panel, solidFill(255, 255, 255, 0)));
    expectOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));
    EXPECT_TRUE(context->committedPaint().empty());
    EXPECT_EQ(context->committedHit().hitRevision(), hitRevision);
}

TEST_F(UIPaintSnapshotTest, CommitStructureDoesNotPublishPendingPaintMutation)
{
    auto context = createContext(window, {.nodeCapacity = 2, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    expectOk(updater.setBoxPaint(panel, solidFill(10, 20, 30)));
    expectOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));

    const UI::UICommittedPaintView baseline = context->committedPaint();
    const auto* const baselineData = baseline.entries().data();
    ASSERT_EQ(baseline.size(), 1U);
    const UI::UIPremultipliedRgba8Color baselineColor =
        baseline.entries().front().solidFill;

    expectOk(updater.setBoxPaint(panel, solidFill(80, 40, 20, 128)));
    expectOk(context->commitStructure());
    EXPECT_EQ(context->committedPaint().entries().data(), baselineData);
    EXPECT_EQ(context->committedPaint().paintRevision(), baseline.paintRevision());
    ASSERT_EQ(context->committedPaint().size(), 1U);
    EXPECT_EQ(context->committedPaint().entries().front().solidFill, baselineColor);
    EXPECT_TRUE(context->statistics().paintDirty);

    expectOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));
    EXPECT_EQ(context->committedPaint().paintRevision(), baseline.paintRevision() + 1U);
    ASSERT_EQ(context->committedPaint().size(), 1U);
    EXPECT_EQ(
        context->committedPaint().entries().front().solidFill,
        (UI::UIPremultipliedRgba8Color{
            .red = 40,
            .green = 20,
            .blue = 10,
            .alpha = 128,
        }));
}

TEST_F(UIPaintSnapshotTest, ViewportRelayoutRebuildsCompositeSnapshotWithoutLocalPaint)
{
    auto context = createContext(window, {.nodeCapacity = 2, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle percentStyle;
    percentStyle.size.width = UI::UILayoutLength::Percent(50.0F);
    percentStyle.size.height = UI::UILayoutLength::Percent(50.0F);
    expectOk(updater.setLayoutStyle(panel, percentStyle));
    expectOk(updater.setBoxPaint(panel, solidFill(20, 30, 40)));
    expectOk(context->commitLayout({.width = 100.0F, .height = 60.0F}));
    const u64 paintRevision = context->committedPaint().paintRevision();

    expectOk(context->commitLayout({.width = 200.0F, .height = 80.0F}));
    const UI::UICommittedPaintView paint = context->committedPaint();
    ASSERT_EQ(paint.size(), 1U);
    EXPECT_EQ(paint.viewportSize(), (UI::UILogicalSize{200.0F, 80.0F}));
    EXPECT_EQ(
        paint.entries().front().worldRect,
        (UI::UILogicalRect{.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F}));
    EXPECT_EQ(paint.paintRevision(), paintRevision + 1U);
    EXPECT_EQ(context->statistics().lastLayoutPassCount, 1U);
    EXPECT_EQ(context->statistics().lastPaintCacheRebuildCount, 0U);
    EXPECT_EQ(context->statistics().lastPaintSnapshotRebuildCount, 1U);
}

TEST_F(UIPaintSnapshotTest, CapacityFailurePreservesAllFourPublishedSnapshotsAndPendingDirty)
{
    auto context = createContext(
        window,
        {
            .nodeCapacity = 3,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 1,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId first = createPanel(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    expectOk(updater.setBoxPaint(first, solidFill(1, 2, 3)));
    expectOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));

    const UI::UICommittedStructureView oldStructure = context->committedStructure();
    const UI::UICommittedLayoutView oldLayout = context->committedLayout();
    const UI::UICommittedHitView oldHit = context->committedHit();
    const UI::UICommittedPaintView oldPaint = context->committedPaint();
    const auto* const oldStructureData = oldStructure.entries().data();
    const auto* const oldLayoutData = oldLayout.entries().data();
    const auto* const oldHitData = oldHit.entries().data();
    const auto* const oldPaintData = oldPaint.entries().data();
    ASSERT_EQ(oldPaint.size(), 1U);

    const UI::UINodeId second = createPanel(*context, root.rootNodeId());
    expectOk(updater.setBoxPaint(second, solidFill(4, 5, 6)));
    const Core::Status rejected =
        context->commitLayout({.width = 100.0F, .height = 50.0F});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);

    EXPECT_EQ(context->committedStructure().entries().data(), oldStructureData);
    EXPECT_EQ(context->committedStructure().revision(), oldStructure.revision());
    EXPECT_EQ(context->committedLayout().entries().data(), oldLayoutData);
    EXPECT_EQ(context->committedLayout().layoutRevision(), oldLayout.layoutRevision());
    EXPECT_EQ(context->committedHit().entries().data(), oldHitData);
    EXPECT_EQ(context->committedHit().hitRevision(), oldHit.hitRevision());
    EXPECT_EQ(context->committedPaint().entries().data(), oldPaintData);
    EXPECT_EQ(context->committedPaint().paintRevision(), oldPaint.paintRevision());
    EXPECT_EQ(context->committedPaint().viewportSize(), oldPaint.viewportSize());
    EXPECT_EQ(oldPaint.entries().front().node, first);
    EXPECT_EQ(context->committedPaint().entries().front().node, first);
    EXPECT_TRUE(context->statistics().structureDirty);
    EXPECT_TRUE(context->statistics().layoutDirty);
    EXPECT_TRUE(context->statistics().hitDirty);
    EXPECT_TRUE(context->statistics().paintDirty);

    expectOk(updater.setBoxPaint(second, {}));
    expectOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));
    EXPECT_EQ(context->committedStructure().size(), 3U);
    ASSERT_EQ(context->committedPaint().size(), 1U);
    EXPECT_EQ(context->committedPaint().entries().front().node, first);
}

TEST_F(UIPaintSnapshotTest, RootScopeAndDirtyCapacityFailuresDoNotMutateRejectedPaint)
{
    auto context = createContext(
        window,
        {
            .nodeCapacity = 5,
            .rootCapacity = 2,
            .dirtyQueueCapacity = 1,
        });
    ASSERT_NE(context, nullptr);
    auto firstRoot = createRoot(*context);
    auto secondRoot = createRoot(*context);
    ASSERT_TRUE(firstRoot);
    ASSERT_TRUE(secondRoot);
    const UI::UINodeId first = createPanel(*context, firstRoot.rootNodeId());
    const UI::UINodeId second = createPanel(*context, firstRoot.rootNodeId());
    const UI::UINodeId foreign = createPanel(*context, secondRoot.rootNodeId());
    auto updater = createUpdater(*context, firstRoot);
    expectOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));

    const Core::Status wrongRoot = updater.setBoxPaint(foreign, solidFill(1, 1, 1));
    ASSERT_FALSE(wrongRoot.has_value());
    EXPECT_EQ(wrongRoot.error().code, UI::UIErrorCode::InvalidNode);
    expectOk(updater.setBoxPaint(first, solidFill(2, 2, 2)));
    const Core::Status exhausted = updater.setBoxPaint(second, solidFill(3, 3, 3));
    ASSERT_FALSE(exhausted.has_value());
    EXPECT_EQ(exhausted.error().code, UI::UIErrorCode::CapacityExceeded);
    expectOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));

    ASSERT_EQ(context->committedPaint().size(), 1U);
    EXPECT_EQ(context->committedPaint().entries().front().node, first);
    EXPECT_EQ(findPaintEntry(context->committedPaint(), second), nullptr);
    EXPECT_EQ(findPaintEntry(context->committedPaint(), foreign), nullptr);
}

TEST_F(UIPaintSnapshotTest, RejectsForeignContextAndStaleGenerationWithoutPaintPollution)
{
    auto context = createContext(window, {.nodeCapacity = 4, .rootCapacity = 1});
    auto foreignContext =
        createContext(window, {.nodeCapacity = 2, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);
    ASSERT_NE(foreignContext, nullptr);
    auto root = createRoot(*context);
    auto foreignRoot = createRoot(*foreignContext);
    ASSERT_TRUE(root);
    ASSERT_TRUE(foreignRoot);

    const UI::UINodeId stable = createPanel(*context, root.rootNodeId());
    const UI::UINodeId stale = createPanel(*context, root.rootNodeId());
    const UI::UINodeId foreign =
        createPanel(*foreignContext, foreignRoot.rootNodeId());
    auto updater = createUpdater(*context, root);
    expectOk(updater.setBoxPaint(stable, solidFill(1, 2, 3)));
    expectOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));

    const UI::UICommittedPaintView baseline = context->committedPaint();
    ASSERT_EQ(baseline.size(), 1U);
    const Core::Status wrongContext =
        updater.setBoxPaint(foreign, solidFill(4, 5, 6));
    ASSERT_FALSE(wrongContext.has_value());
    EXPECT_EQ(wrongContext.error().code, UI::UIErrorCode::WrongContext);

    expectOk(updater.destroy(stale));
    const Core::Status staleStatus =
        updater.setBoxPaint(stale, solidFill(7, 8, 9));
    ASSERT_FALSE(staleStatus.has_value());
    EXPECT_EQ(staleStatus.error().code, UI::UIErrorCode::InvalidNode);
    EXPECT_EQ(context->committedPaint().paintRevision(), baseline.paintRevision());
    ASSERT_EQ(context->committedPaint().size(), 1U);
    EXPECT_EQ(context->committedPaint().entries().front().node, stable);

    expectOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));
    ASSERT_EQ(context->committedPaint().size(), 1U);
    EXPECT_EQ(context->committedPaint().entries().front().node, stable);
    EXPECT_EQ(findPaintEntry(context->committedPaint(), foreign), nullptr);
    EXPECT_EQ(findPaintEntry(context->committedPaint(), stale), nullptr);
}

TEST_F(UIPaintSnapshotTest, NoChangeAndViewportCommitsUseFixedPmrStorage)
{
    ObservingMemoryResource resource;
    {
        auto context = createContext(
            window,
            {.nodeCapacity = 8, .rootCapacity = 1},
            resource);
        ASSERT_NE(context, nullptr);
        auto root = createRoot(*context);
        ASSERT_TRUE(root);
        const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
        auto updater = createUpdater(*context, root);
        expectOk(updater.setBoxPaint(panel, solidFill(40, 80, 120, 200)));
        expectOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));

        const u64 paintRevision = context->committedPaint().paintRevision();
        const usize allocationCount = resource.allocationCount();
        for (usize frame = 0; frame < 300; ++frame) {
            expectOk(context->commitLayout({.width = 100.0F, .height = 50.0F}));
            EXPECT_EQ(context->committedPaint().paintRevision(), paintRevision);
            EXPECT_EQ(context->statistics().lastPaintCacheRebuildCount, 0U);
            EXPECT_EQ(context->statistics().lastPaintSnapshotRebuildCount, 0U);
        }
        EXPECT_EQ(resource.allocationCount(), allocationCount);

        expectOk(context->commitLayout({.width = 200.0F, .height = 100.0F}));
        EXPECT_EQ(resource.allocationCount(), allocationCount);
        EXPECT_EQ(context->statistics().lastPaintCacheRebuildCount, 0U);
        EXPECT_EQ(context->statistics().lastPaintSnapshotRebuildCount, 1U);
        EXPECT_GT(resource.currentBytes(), 0U);
    }
    EXPECT_EQ(resource.currentBytes(), 0U);
    EXPECT_EQ(resource.allocationCount(), resource.deallocationCount());
}

} // namespace
} // namespace Tina::Tests
