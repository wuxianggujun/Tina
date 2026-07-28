#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <limits>
#include <memory>
#include <memory_resource>
#include <string>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;
inline constexpr usize ContextNodeCapacity = 256;

class ObservingMemoryResource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] usize allocationCount() const noexcept
    {
        return m_allocationCount;
    }

  private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        void* storage = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_allocationCount;
        return storage;
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize m_allocationCount = 0;
};

struct ListDataSource final {
    u64 count = 0;
    u64 disabledIndex = (std::numeric_limits<u64>::max)();
    u64 failingIndex = (std::numeric_limits<u64>::max)();
    UI::UIListViewItemKey keyBase = 0;
    std::string label = "Virtual row";

    [[nodiscard]] UI::UIListViewDataSource view() const noexcept
    {
        return UI::UIListViewDataSource{
            .state = this,
            .itemCount = [](const void* state) noexcept {
                return static_cast<const ListDataSource*>(state)->count;
            },
            .resolveItem = [](const void* state, u64 logicalIndex,
                              UI::UIListViewItemDescriptor& output) noexcept {
                const auto& source = *static_cast<const ListDataSource*>(state);
                if (logicalIndex >= source.count || logicalIndex == source.failingIndex)
                {
                    return false;
                }
                output = UI::UIListViewItemDescriptor{
                    .key = source.keyBase + logicalIndex + 1,
                    .label = source.label,
                    .enabled = logicalIndex != source.disabledIndex,
                };
                return true;
            },
        };
    }
};

[[nodiscard]] std::unique_ptr<UI::UIContext>
createContext(Platform::WindowId window, usize nodeCapacity, std::pmr::memory_resource& resource)
{
    auto result = UI::UIContext::Create(window,
                                        {
                                            .nodeCapacity = nodeCapacity,
                                            .rootCapacity = 1,
                                            .routePathCapacity = nodeCapacity,
                                            .textByteCapacity = 4096,
                                            .applyDefaultProductChrome = false,
                                        },
                                        resource);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(Platform::WindowId window, usize nodeCapacity)
{
    return createContext(window, nodeCapacity, *std::pmr::get_default_resource());
}

[[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

[[nodiscard]] UI::UITreeUpdater createUpdater(UI::UIContext& context, UI::UIRootOwner& root)
{
    auto result = context.treeUpdater(root);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UITreeUpdater{};
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] UI::UIPointerInputEvent pointerInput(Platform::WindowId window, UI::UIRoutedPointerEventKind kind,
                                                   u64 sequence, UI::UILogicalPoint position,
                                                   UI::UILogicalPoint delta = {}) noexcept
{
    return UI::UIPointerInputEvent{
        .platformFrame = Platform::PlatformFrameId{sequence},
        .transitionOrdinal = 0,
        .sourceSequence = sequence,
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .kind = kind,
        .position = position,
        .delta = delta,
        .button = Platform::PointerButton::Primary,
    };
}

[[nodiscard]] const UI::UISemanticsEntry* findVirtualItem(UI::UICommittedSemanticsView view,
                                                          u64 logicalIndex) noexcept
{
    for (const UI::UISemanticsEntry& entry : view.entries())
    {
        if (entry.kind == UI::UIWidgetKind::ListViewItem && entry.virtualItemIndex == logicalIndex)
        {
            return &entry;
        }
    }
    return nullptr;
}

class UIListViewTest : public testing::Test {
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

TEST_F(UIListViewTest, VirtualizesOneHundredThousandItemsWithFixedNodePool)
{
    constexpr u32 RowCapacity = 12;
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    ListDataSource source{.count = 100'000, .keyBase = 1'000};

    auto listResult = updater.createListView(root.rootNodeId(), {.materializedItemCapacity = RowCapacity});
    ASSERT_TRUE(listResult.has_value()) << listResult.error().message;
    const UI::UINodeId listView = *listResult;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(listView, fixedSize(100.0F, 100.0F)));
    assertOk(updater.setListViewStyle(listView, {
                                                        .rowHeight = 20.0F,
                                                        .overscanRows = 2,
                                                        .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                                                        .wheelStep = 20.0F,
                                                    }));
    assertOk(updater.setListViewDataSource(listView, source.view()));
    ASSERT_EQ(context->liveNodeCount(), RowCapacity + 2U);

    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    UI::UIListViewMetrics metrics = updater.listViewMetrics(listView).value();
    EXPECT_EQ(metrics.logicalItemCount, 100'000U);
    EXPECT_EQ(metrics.firstVisibleIndex, 0U);
    EXPECT_EQ(metrics.visibleItemCount, 5U);
    EXPECT_EQ(metrics.firstMaterializedIndex, 0U);
    EXPECT_EQ(metrics.materializedItemCount, 7U);
    EXPECT_EQ(metrics.materializedItemCapacity, RowCapacity);
    EXPECT_EQ(metrics.viewportSize, (UI::UILogicalSize{.width = 90.0F, .height = 100.0F}));
    EXPECT_FLOAT_EQ(metrics.contentSize.height, 2'000'000.0F);
    EXPECT_FLOAT_EQ(metrics.maxScrollOffset, 1'999'900.0F);
    EXPECT_TRUE(metrics.verticalScrollBarVisible);

    assertOk(updater.scrollListViewToIndex(listView, 50'000, UI::UIListViewScrollAlignment::Start));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    metrics = updater.listViewMetrics(listView).value();
    EXPECT_EQ(metrics.firstVisibleIndex, 50'000U);
    EXPECT_EQ(metrics.firstMaterializedIndex, 49'998U);
    EXPECT_EQ(metrics.materializedItemCount, 9U);
    EXPECT_EQ(context->liveNodeCount(), RowCapacity + 2U);

    const UI::UISemanticsEntry* row = findVirtualItem(context->committedSemantics(), 50'000);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->virtualItemKey, 51'001U);
    EXPECT_EQ(row->role, UI::UISemanticsRole::ListItem);

    UI::UIAccessibilityTree accessibility;
    assertOk(accessibility.rebuildFrom(context->committedSemantics()));
    const UI::UIAccessibilityNode* accessibleRow = accessibility.findNode(row->node);
    ASSERT_NE(accessibleRow, nullptr);
    EXPECT_EQ(accessibleRow->virtualItemKey, row->virtualItemKey);
    EXPECT_EQ(accessibleRow->virtualItemIndex, row->virtualItemIndex);
}

TEST_F(UIListViewTest, PointerSelectionWheelAndScrollbarUseCommittedVirtualRows)
{
    constexpr u32 RowCapacity = 12;
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    ListDataSource source{.count = 100};

    auto listResult = updater.createListView(root.rootNodeId(), {.materializedItemCapacity = RowCapacity});
    ASSERT_TRUE(listResult.has_value()) << listResult.error().message;
    const UI::UINodeId listView = *listResult;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(listView, fixedSize(100.0F, 100.0F)));
    assertOk(updater.setListViewStyle(listView, {
                                                        .rowHeight = 20.0F,
                                                        .overscanRows = 2,
                                                        .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                                                        .wheelStep = 20.0F,
                                                    }));
    const UI::UIStraightSrgba8Color selectionColor{.red = 40, .green = 120, .blue = 220, .alpha = 192};
    assertOk(updater.setListViewPaint(listView, {
                                                        .scrollBar =
                                                            {
                                                                .trackColor = UI::rgb(0x202830),
                                                                .thumbColor = UI::rgb(0x8090A0),
                                                                .draggingThumbColor = UI::rgb(0xE0A030),
                                                                .thickness = 10.0F,
                                                                .minThumbExtent = 24.0F,
                                                            },
                                                        .selectedItemBackgroundColor = selectionColor,
                                                    }));
    assertOk(updater.setListViewDataSource(listView, source.view()));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const UI::UISemanticsEntry* thirdRow = findVirtualItem(context->committedSemantics(), 2);
    ASSERT_NE(thirdRow, nullptr);
    const UI::UILogicalPoint rowCenter{
        .x = thirdRow->worldRect.x + thirdRow->worldRect.width * 0.5F,
        .y = thirdRow->worldRect.y + thirdRow->worldRect.height * 0.5F,
    };
    auto down = context->routePointerInput(
        pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 1, rowCenter));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_EQ(context->pointerCapture(), thirdRow->node);
    auto up = context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 2, rowCenter));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_TRUE(up->consumed);
    EXPECT_EQ(context->defaultActionFocus(), listView);
    EXPECT_EQ(updater.listViewSelection(listView).value(),
              (UI::UIListViewSelection{.key = 3, .logicalIndex = 2}));

    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const UI::UISemanticsEntry* selectedRow = findVirtualItem(context->committedSemantics(), 2);
    ASSERT_NE(selectedRow, nullptr);
    EXPECT_TRUE(selectedRow->selected);
    EXPECT_TRUE(selectedRow->focused);
    bool foundSelectionPaint = false;
    for (const UI::UICommittedPaintEntry& entry : context->committedPaint().entries())
    {
        foundSelectionPaint = foundSelectionPaint || entry.solidFill == UI::premultiply(selectionColor);
    }
    EXPECT_TRUE(foundSelectionPaint);

    auto wheel = context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::Wheel, 3,
                                                         {.x = 50.0F, .y = 50.0F}, {.x = 0.0F, .y = -1.0F}));
    ASSERT_TRUE(wheel.has_value()) << (wheel ? "" : wheel.error().message);
    EXPECT_TRUE(wheel->consumed);
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    EXPECT_FLOAT_EQ(updater.listViewMetrics(listView).value().scrollOffset, 20.0F);

    assertOk(updater.scrollListViewToIndex(listView, 0, UI::UIListViewScrollAlignment::Start));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    auto thumbDown = context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 4,
                                                             {.x = 95.0F, .y = 12.0F}));
    ASSERT_TRUE(thumbDown.has_value()) << (thumbDown ? "" : thumbDown.error().message);
    EXPECT_TRUE(thumbDown->consumed);
    EXPECT_EQ(context->pointerCapture(), listView);
    auto thumbMove = context->routePointerInput(
        pointerInput(window, UI::UIRoutedPointerEventKind::Move, 5, {.x = 95.0F, .y = 80.0F}));
    ASSERT_TRUE(thumbMove.has_value()) << (thumbMove ? "" : thumbMove.error().message);
    auto thumbUp = context->routePointerInput(
        pointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 6, {.x = 95.0F, .y = 80.0F}));
    ASSERT_TRUE(thumbUp.has_value()) << (thumbUp ? "" : thumbUp.error().message);
    EXPECT_FALSE(context->pointerCapture().hasValue());
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    EXPECT_GT(updater.listViewMetrics(listView).value().scrollOffset, 0.0F);
}

TEST_F(UIListViewTest, KeyboardCommandsSkipDisabledRowsDebounceAndActivate)
{
    constexpr u32 RowCapacity = 10;
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    ListDataSource source{.count = 30, .disabledIndex = 1, .keyBase = 100};

    const UI::UINodeId listView = *updater.createListView(root.rootNodeId(), {.materializedItemCapacity = RowCapacity});
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(120.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(listView, fixedSize(120.0F, 100.0F)));
    assertOk(updater.setListViewStyle(listView, {.rowHeight = 20.0F, .overscanRows = 1}));
    assertOk(updater.setListViewDataSource(listView, source.view()));
    assertOk(context->commitLayout({.width = 120.0F, .height = 100.0F}));
    assertOk(context->requestFocus(listView));

    auto first = context->routeListViewCommand(UI::UIListViewCommand::NextItem, true);
    ASSERT_TRUE(first.has_value()) << first.error().message;
    EXPECT_TRUE(first->consumed);
    EXPECT_TRUE(first->changed);
    EXPECT_EQ(first->selection, (UI::UIListViewSelection{.key = 101, .logicalIndex = 0}));
    auto repeated = context->routeListViewCommand(UI::UIListViewCommand::NextItem, true);
    ASSERT_TRUE(repeated.has_value());
    EXPECT_TRUE(repeated->consumed);
    EXPECT_FALSE(repeated->changed);
    EXPECT_TRUE(context->routeListViewCommand(UI::UIListViewCommand::NextItem, false)->consumed);

    auto next = context->routeListViewCommand(UI::UIListViewCommand::NextItem, true);
    ASSERT_TRUE(next.has_value()) << next.error().message;
    EXPECT_EQ(next->selection, (UI::UIListViewSelection{.key = 103, .logicalIndex = 2}));
    EXPECT_TRUE(context->routeListViewCommand(UI::UIListViewCommand::NextItem, false)->consumed);

    auto page = context->routeListViewCommand(UI::UIListViewCommand::NextPage, true);
    ASSERT_TRUE(page.has_value()) << page.error().message;
    EXPECT_EQ(page->selection.logicalIndex, 7U);
    EXPECT_TRUE(context->routeListViewCommand(UI::UIListViewCommand::NextPage, false)->consumed);
    assertOk(context->commitLayout({.width = 120.0F, .height = 100.0F}));
    EXPECT_GE(updater.listViewMetrics(listView).value().firstVisibleIndex, 3U);

    auto activate = context->routeListViewCommand(UI::UIListViewCommand::Activate, true);
    ASSERT_TRUE(activate.has_value()) << activate.error().message;
    EXPECT_TRUE(activate->consumed);
    EXPECT_TRUE(activate->activated);
    EXPECT_EQ(activate->selection.logicalIndex, 7U);
    EXPECT_TRUE(context->routeListViewCommand(UI::UIListViewCommand::Activate, false)->consumed);
}

TEST_F(UIListViewTest, FailedSourceAndRowPoolOverflowPreserveCommittedSnapshots)
{
    constexpr u32 RowCapacity = 4;
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    ListDataSource source{.count = 20, .label = "Old"};

    const UI::UINodeId listView = *updater.createListView(root.rootNodeId(), {.materializedItemCapacity = RowCapacity});
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 60.0F)));
    assertOk(updater.setLayoutStyle(listView, fixedSize(100.0F, 60.0F)));
    assertOk(updater.setListViewStyle(listView, {
                                                        .rowHeight = 20.0F,
                                                        .overscanRows = 1,
                                                        .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                                                        .wheelStep = 20.0F,
                                                    }));
    assertOk(updater.setListViewDataSource(listView, source.view()));
    assertOk(context->commitLayout({.width = 100.0F, .height = 60.0F}));

    const UI::UIListViewMetrics committedMetrics = updater.listViewMetrics(listView).value();
    const u64 committedLayoutRevision = context->committedLayout().layoutRevision();
    const u64 committedSemanticsRevision = context->committedSemantics().semanticsRevision();
    const UI::UISemanticsEntry* committedFirst = findVirtualItem(context->committedSemantics(), 0);
    ASSERT_NE(committedFirst, nullptr);
    EXPECT_EQ(committedFirst->name, "Old");

    source.label = "New";
    source.failingIndex = 2;
    assertOk(updater.invalidateListViewItems(listView));
    const Core::Status sourceFailure = context->commitLayout({.width = 100.0F, .height = 60.0F});
    ASSERT_FALSE(sourceFailure.has_value());
    EXPECT_EQ(sourceFailure.error().code, UI::UIErrorCode::InvalidControlValue);
    EXPECT_EQ(context->committedLayout().layoutRevision(), committedLayoutRevision);
    EXPECT_EQ(context->committedSemantics().semanticsRevision(), committedSemanticsRevision);
    EXPECT_EQ(updater.listViewMetrics(listView).value(), committedMetrics);
    committedFirst = findVirtualItem(context->committedSemantics(), 0);
    ASSERT_NE(committedFirst, nullptr);
    EXPECT_EQ(committedFirst->name, "Old");

    source.failingIndex = (std::numeric_limits<u64>::max)();
    assertOk(context->commitLayout({.width = 100.0F, .height = 60.0F}));
    EXPECT_EQ(findVirtualItem(context->committedSemantics(), 0)->name, "New");
    const UI::UIListViewMetrics recoveredMetrics = updater.listViewMetrics(listView).value();
    const u64 recoveredRevision = context->committedLayout().layoutRevision();

    assertOk(updater.setListViewStyle(listView, {
                                                        .rowHeight = 20.0F,
                                                        .overscanRows = 2,
                                                        .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                                                        .wheelStep = 20.0F,
                                                    }));
    const Core::Status poolOverflow = context->commitLayout({.width = 100.0F, .height = 100.0F});
    ASSERT_FALSE(poolOverflow.has_value());
    EXPECT_EQ(poolOverflow.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->committedLayout().layoutRevision(), recoveredRevision);
    EXPECT_EQ(updater.listViewMetrics(listView).value(), recoveredMetrics);
}

TEST_F(UIListViewTest, InternalRowsCannotBeDestroyedIndependently)
{
    constexpr u32 RowCapacity = 6;
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    ListDataSource source{.count = 10};

    const UI::UINodeId listView = *updater.createListView(root.rootNodeId(), {.materializedItemCapacity = RowCapacity});
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 60.0F)));
    assertOk(updater.setLayoutStyle(listView, fixedSize(100.0F, 60.0F)));
    assertOk(updater.setListViewStyle(listView, {.rowHeight = 20.0F, .overscanRows = 1}));
    assertOk(updater.setListViewDataSource(listView, source.view()));
    assertOk(context->commitLayout({.width = 100.0F, .height = 60.0F}));
    const UI::UISemanticsEntry* row = findVirtualItem(context->committedSemantics(), 0);
    ASSERT_NE(row, nullptr);

    const Core::Status rejected = updater.destroy(row->node);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidControlValue);
    EXPECT_EQ(context->liveNodeCount(), RowCapacity + 2U);

    assertOk(updater.destroy(listView));
    EXPECT_EQ(context->liveNodeCount(), 1U);
}

TEST_F(UIListViewTest, WheelAndCommitRemainAllocationFreeAfterWarmup)
{
    constexpr u32 RowCapacity = 12;
    ObservingMemoryResource resource;
    auto context = createContext(window, ContextNodeCapacity, resource);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    ListDataSource source{.count = 1'000};

    const UI::UINodeId listView = *updater.createListView(root.rootNodeId(), {.materializedItemCapacity = RowCapacity});
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(listView, fixedSize(100.0F, 100.0F)));
    assertOk(updater.setListViewStyle(listView, {
                                                        .rowHeight = 20.0F,
                                                        .overscanRows = 2,
                                                        .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
                                                        .wheelStep = 20.0F,
                                                    }));
    assertOk(updater.setListViewDataSource(listView, source.view()));
    assertOk(updater.scrollListViewToIndex(listView, 10, UI::UIListViewScrollAlignment::Start));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const usize allocationCount = resource.allocationCount();

    for (u64 routeIndex = 0; routeIndex < 200; ++routeIndex)
    {
        const float wheelDelta = routeIndex % 2 == 0 ? -1.0F : 1.0F;
        auto routed = context->routePointerInput(
            pointerInput(window, UI::UIRoutedPointerEventKind::Wheel, routeIndex + 1, {.x = 50.0F, .y = 50.0F},
                         {.x = 0.0F, .y = wheelDelta}));
        ASSERT_TRUE(routed.has_value()) << (routed ? "" : routed.error().message);
        EXPECT_TRUE(routed->consumed);
        assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    }
    EXPECT_EQ(resource.allocationCount(), allocationCount);
}

} // namespace
} // namespace Tina::Tests
