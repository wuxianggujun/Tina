#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <array>
#include <limits>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
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

struct FlatTreeDataSource final {
    u64 count = 0;
    u64 disabledIndex = (std::numeric_limits<u64>::max)();
    u64 failingIndex = (std::numeric_limits<u64>::max)();
    UI::UITreeViewItemKey keyBase = 0;
    std::string label = "Virtual tree row";

    [[nodiscard]] UI::UITreeViewDataSource view() noexcept
    {
        return UI::UITreeViewDataSource{
            .state = this,
            .itemCount =
                [](const void* state) noexcept { return static_cast<const FlatTreeDataSource*>(state)->count; },
            .resolveItem =
                [](const void* state, u64 logicalIndex, UI::UITreeViewItemDescriptor& output) noexcept {
                    const auto& source = *static_cast<const FlatTreeDataSource*>(state);
                    if (logicalIndex >= source.count || logicalIndex == source.failingIndex)
                    {
                        return false;
                    }
                    output = UI::UITreeViewItemDescriptor{
                        .key = source.keyBase + logicalIndex + 1,
                        .label = source.label,
                        .enabled = logicalIndex != source.disabledIndex,
                    };
                    return true;
                },
        };
    }
};

struct MutableTreeDataSource final {
    struct Node final {
        UI::UITreeViewItemKey key = UI::InvalidUITreeViewItemKey;
        std::string_view label{};
        i32 parent = -1;
        u32 level = 0;
        bool enabled = true;
        bool expandable = false;
        bool expanded = false;
    };

    std::array<Node, 7> nodes{
        Node{.key = 1, .label = "Workspace", .parent = -1, .level = 0, .expandable = true, .expanded = true},
        Node{.key = 2, .label = "Source", .parent = 0, .level = 1, .expandable = true, .expanded = true},
        Node{.key = 3, .label = "main.cpp", .parent = 1, .level = 2},
        Node{.key = 4, .label = "disabled.hpp", .parent = 1, .level = 2, .enabled = false},
        Node{.key = 5, .label = "Tests", .parent = 0, .level = 1, .expandable = true},
        Node{.key = 6, .label = "tree_tests.cpp", .parent = 4, .level = 2},
        Node{.key = 7, .label = "README.md", .parent = -1, .level = 0},
    };
    std::array<u32, 7> visibleIndices{};
    u64 visibleCount = 0;
    u32 expansionCallCount = 0;
    UI::UITreeViewItemKey lastExpansionKey = UI::InvalidUITreeViewItemKey;
    bool rejectExpansion = false;

    MutableTreeDataSource()
    {
        rebuildVisibleProjection();
    }

    void rebuildVisibleProjection() noexcept
    {
        visibleCount = 0;
        for (u32 nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
        {
            bool visible = true;
            i32 ancestor = nodes[nodeIndex].parent;
            while (ancestor >= 0)
            {
                if (!nodes[static_cast<usize>(ancestor)].expanded)
                {
                    visible = false;
                    break;
                }
                ancestor = nodes[static_cast<usize>(ancestor)].parent;
            }
            if (visible)
            {
                visibleIndices[visibleCount++] = nodeIndex;
            }
        }
    }

    [[nodiscard]] UI::UITreeViewDataSource view() noexcept
    {
        return UI::UITreeViewDataSource{
            .state = this,
            .itemCount =
                [](const void* state) noexcept {
                    return static_cast<const MutableTreeDataSource*>(state)->visibleCount;
                },
            .resolveItem =
                [](const void* state, u64 logicalIndex, UI::UITreeViewItemDescriptor& output) noexcept {
                    const auto& source = *static_cast<const MutableTreeDataSource*>(state);
                    if (logicalIndex >= source.visibleCount)
                    {
                        return false;
                    }
                    const Node& node = source.nodes[source.visibleIndices[logicalIndex]];
                    output = UI::UITreeViewItemDescriptor{
                        .key = node.key,
                        .label = node.label,
                        .level = node.level,
                        .enabled = node.enabled,
                        .expandable = node.expandable,
                        .expanded = node.expanded,
                    };
                    return true;
                },
            .setItemExpanded =
                [](void* state, UI::UITreeViewItemKey key, bool expanded) noexcept {
                    auto& source = *static_cast<MutableTreeDataSource*>(state);
                    ++source.expansionCallCount;
                    source.lastExpansionKey = key;
                    if (source.rejectExpansion)
                    {
                        return false;
                    }
                    for (Node& node : source.nodes)
                    {
                        if (node.key == key && node.expandable)
                        {
                            node.expanded = expanded;
                            source.rebuildVisibleProjection();
                            return true;
                        }
                    }
                    return false;
                },
        };
    }
};

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(Platform::WindowId window, usize nodeCapacity,
                                                           std::pmr::memory_resource& resource)
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

[[nodiscard]] const UI::UISemanticsEntry* findVirtualItem(UI::UICommittedSemanticsView view, u64 logicalIndex) noexcept
{
    for (const UI::UISemanticsEntry& entry : view.entries())
    {
        if (entry.kind == UI::UIWidgetKind::TreeViewItem && entry.virtualItemIndex == logicalIndex)
        {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] const UI::UISemanticsEntry* findSemanticsNode(UI::UICommittedSemanticsView view,
                                                            UI::UINodeId node) noexcept
{
    for (const UI::UISemanticsEntry& entry : view.entries())
    {
        if (entry.node == node)
        {
            return &entry;
        }
    }
    return nullptr;
}

class UITreeViewTest : public testing::Test {
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

TEST_F(UITreeViewTest, VirtualizesOneHundredThousandItemsWithFixedNodePool)
{
    constexpr u32 RowCapacity = 12;
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    FlatTreeDataSource source{.count = 100'000, .keyBase = 1'000};

    auto treeResult = updater.createTreeView(root.rootNodeId(), {.materializedItemCapacity = RowCapacity});
    ASSERT_TRUE(treeResult.has_value()) << treeResult.error().message;
    const UI::UINodeId treeView = *treeResult;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(treeView, fixedSize(100.0F, 100.0F)));
    assertOk(updater.setTreeViewStyle(treeView, {
                                                    .rowHeight = 20.0F,
                                                    .overscanRows = 2,
                                                    .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                                                    .wheelStep = 20.0F,
                                                }));
    assertOk(updater.setTreeViewDataSource(treeView, source.view()));
    EXPECT_EQ(context->liveNodeCount(), RowCapacity + 2U);

    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    UI::UITreeViewMetrics metrics = updater.treeViewMetrics(treeView).value();
    EXPECT_EQ(metrics.logicalItemCount, 100'000U);
    EXPECT_EQ(metrics.visibleItemCount, 5U);
    EXPECT_EQ(metrics.materializedItemCount, 7U);
    EXPECT_EQ(metrics.materializedItemCapacity, RowCapacity);
    EXPECT_FLOAT_EQ(metrics.contentSize.height, 2'000'000.0F);
    EXPECT_TRUE(metrics.verticalScrollBarVisible);

    assertOk(updater.scrollTreeViewToIndex(treeView, 50'000, UI::UITreeViewScrollAlignment::Start));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    metrics = updater.treeViewMetrics(treeView).value();
    EXPECT_EQ(metrics.firstVisibleIndex, 50'000U);
    EXPECT_EQ(metrics.firstMaterializedIndex, 49'998U);
    EXPECT_EQ(metrics.materializedItemCount, 9U);
    EXPECT_EQ(context->liveNodeCount(), RowCapacity + 2U);

    const UI::UISemanticsEntry* row = findVirtualItem(context->committedSemantics(), 50'000);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->virtualItemKey, 51'001U);
    EXPECT_EQ(row->role, UI::UISemanticsRole::TreeItem);
    EXPECT_EQ(row->level, 0U);
    EXPECT_FALSE(row->expandable);

    UI::UIAccessibilityTree accessibility;
    assertOk(accessibility.rebuildFrom(context->committedSemantics()));
    const UI::UIAccessibilityNode* accessibleRow = accessibility.findNode(row->node);
    ASSERT_NE(accessibleRow, nullptr);
    EXPECT_EQ(accessibleRow->virtualItemKey, row->virtualItemKey);
    EXPECT_EQ(accessibleRow->virtualItemIndex, row->virtualItemIndex);
    EXPECT_EQ(accessibleRow->level, row->level);
}

TEST_F(UITreeViewTest, PointerSelectsRowsAndDisclosureTogglesCommittedStableKey)
{
    constexpr u32 RowCapacity = 10;
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    MutableTreeDataSource source;

    const UI::UINodeId treeView = *updater.createTreeView(root.rootNodeId(), {.materializedItemCapacity = RowCapacity});
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(160.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(treeView, fixedSize(160.0F, 120.0F)));
    assertOk(updater.setTreeViewStyle(treeView, {
                                                    .rowHeight = 24.0F,
                                                    .overscanRows = 1,
                                                    .wheelStep = 24.0F,
                                                    .indentation = 18.0F,
                                                    .disclosureExtent = 12.0F,
                                                    .disclosureGap = 6.0F,
                                                }));
    const UI::UIStraightSrgba8Color selectionColor{.red = 40, .green = 120, .blue = 220, .alpha = 192};
    assertOk(updater.setTreeViewPaint(treeView, {
                                                    .selectedItemBackgroundColor = selectionColor,
                                                    .disclosureColor = UI::rgb(0xE0A030),
                                                }));
    assertOk(updater.setTreeViewDataSource(treeView, source.view()));
    assertOk(context->commitLayout({.width = 160.0F, .height = 120.0F}));

    const UI::UISemanticsEntry* workspace = findVirtualItem(context->committedSemantics(), 0);
    ASSERT_NE(workspace, nullptr);
    EXPECT_TRUE(workspace->expandable);
    EXPECT_TRUE(workspace->expanded);
    const UI::UILogicalPoint rowBody{
        .x = workspace->worldRect.x + 80.0F,
        .y = workspace->worldRect.y + workspace->worldRect.height * 0.5F,
    };
    auto rowDown =
        context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 1, rowBody));
    ASSERT_TRUE(rowDown.has_value()) << rowDown.error().message;
    EXPECT_TRUE(rowDown->consumed);
    assertOk(context->commitLayout({.width = 160.0F, .height = 120.0F}));
    const UI::UISemanticsEntry* focusedTree = findSemanticsNode(context->committedSemantics(), treeView);
    ASSERT_NE(focusedTree, nullptr);
    EXPECT_TRUE(focusedTree->focused);
    ASSERT_TRUE(
        context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 2, rowBody))->consumed);
    EXPECT_EQ(context->defaultActionFocus(), treeView);
    EXPECT_EQ(updater.treeViewSelection(treeView).value(),
              (UI::UITreeViewSelection{.key = 1, .logicalIndex = 0, .level = 0}));

    workspace = findVirtualItem(context->committedSemantics(), 0);
    ASSERT_NE(workspace, nullptr);
    const UI::UILogicalPoint disclosure{
        .x = workspace->worldRect.x + 14.0F,
        .y = workspace->worldRect.y + workspace->worldRect.height * 0.5F,
    };
    ASSERT_TRUE(
        context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 3, disclosure))
            ->consumed);
    ASSERT_TRUE(context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 4, disclosure))
                    ->consumed);
    EXPECT_EQ(source.expansionCallCount, 1U);
    EXPECT_EQ(source.lastExpansionKey, 1U);
    EXPECT_FALSE(source.nodes[0].expanded);
    EXPECT_EQ(updater.treeViewSelection(treeView).value().key, 1U);
    assertOk(context->commitLayout({.width = 160.0F, .height = 120.0F}));
    EXPECT_EQ(updater.treeViewMetrics(treeView).value().logicalItemCount, 2U);

    workspace = findVirtualItem(context->committedSemantics(), 0);
    ASSERT_NE(workspace, nullptr);
    ASSERT_TRUE(
        context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 5, disclosure))
            ->consumed);
    ASSERT_TRUE(context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 6, disclosure))
                    ->consumed);
    EXPECT_TRUE(source.nodes[0].expanded);
    assertOk(context->commitLayout({.width = 160.0F, .height = 120.0F}));
    EXPECT_EQ(updater.treeViewMetrics(treeView).value().logicalItemCount, 6U);

    const UI::UISemanticsEntry* selected = findVirtualItem(context->committedSemantics(), 0);
    ASSERT_NE(selected, nullptr);
    EXPECT_TRUE(selected->selected);
    EXPECT_TRUE(selected->focused);
    bool foundSelectionPaint = false;
    for (const UI::UICommittedPaintEntry& entry : context->committedPaint().entries())
    {
        foundSelectionPaint = foundSelectionPaint || entry.solidFill == UI::premultiply(selectionColor);
    }
    EXPECT_TRUE(foundSelectionPaint);

    auto wheel = context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::Wheel, 7,
                                                         {.x = 80.0F, .y = 60.0F}, {.x = 0.0F, .y = -1.0F}));
    ASSERT_TRUE(wheel.has_value()) << wheel.error().message;
    EXPECT_TRUE(wheel->consumed);
    assertOk(context->commitLayout({.width = 160.0F, .height = 120.0F}));
    EXPECT_FLOAT_EQ(updater.treeViewMetrics(treeView).value().scrollOffset, 24.0F);
}

TEST_F(UITreeViewTest, KeyboardCommandsNavigateHierarchySkipDisabledRowsAndDebounce)
{
    constexpr u32 RowCapacity = 10;
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    MutableTreeDataSource source;

    const UI::UINodeId treeView = *updater.createTreeView(root.rootNodeId(), {.materializedItemCapacity = RowCapacity});
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(160.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(treeView, fixedSize(160.0F, 120.0F)));
    assertOk(updater.setTreeViewStyle(treeView, {.rowHeight = 24.0F, .overscanRows = 1}));
    assertOk(updater.setTreeViewDataSource(treeView, source.view()));
    assertOk(context->commitLayout({.width = 160.0F, .height = 120.0F}));
    assertOk(context->requestFocus(treeView));

    auto first = context->routeTreeViewCommand(UI::UITreeViewCommand::NextItem, true);
    ASSERT_TRUE(first.has_value()) << first.error().message;
    EXPECT_TRUE(first->consumed);
    EXPECT_TRUE(first->changed);
    EXPECT_EQ(first->selection, (UI::UITreeViewSelection{.key = 1, .logicalIndex = 0, .level = 0}));
    auto repeated = context->routeTreeViewCommand(UI::UITreeViewCommand::NextItem, true);
    ASSERT_TRUE(repeated.has_value());
    EXPECT_TRUE(repeated->consumed);
    EXPECT_FALSE(repeated->changed);
    EXPECT_TRUE(context->routeTreeViewCommand(UI::UITreeViewCommand::NextItem, false)->consumed);

    auto sourceFolder = context->routeTreeViewCommand(UI::UITreeViewCommand::ExpandOrFirstChild, true);
    ASSERT_TRUE(sourceFolder.has_value()) << sourceFolder.error().message;
    EXPECT_EQ(sourceFolder->selection, (UI::UITreeViewSelection{.key = 2, .logicalIndex = 1, .level = 1}));
    EXPECT_TRUE(context->routeTreeViewCommand(UI::UITreeViewCommand::ExpandOrFirstChild, false)->consumed);

    auto mainFile = context->routeTreeViewCommand(UI::UITreeViewCommand::ExpandOrFirstChild, true);
    ASSERT_TRUE(mainFile.has_value()) << mainFile.error().message;
    EXPECT_EQ(mainFile->selection, (UI::UITreeViewSelection{.key = 3, .logicalIndex = 2, .level = 2}));
    EXPECT_TRUE(context->routeTreeViewCommand(UI::UITreeViewCommand::ExpandOrFirstChild, false)->consumed);

    auto testsFolder = context->routeTreeViewCommand(UI::UITreeViewCommand::NextItem, true);
    ASSERT_TRUE(testsFolder.has_value()) << testsFolder.error().message;
    EXPECT_EQ(testsFolder->selection, (UI::UITreeViewSelection{.key = 5, .logicalIndex = 4, .level = 1}));
    EXPECT_TRUE(context->routeTreeViewCommand(UI::UITreeViewCommand::NextItem, false)->consumed);

    auto expanded = context->routeTreeViewCommand(UI::UITreeViewCommand::ExpandOrFirstChild, true);
    ASSERT_TRUE(expanded.has_value()) << expanded.error().message;
    EXPECT_TRUE(expanded->consumed);
    EXPECT_TRUE(expanded->changed);
    EXPECT_TRUE(expanded->expansionChanged);
    EXPECT_TRUE(source.nodes[4].expanded);
    EXPECT_TRUE(context->routeTreeViewCommand(UI::UITreeViewCommand::ExpandOrFirstChild, false)->consumed);

    auto firstChild = context->routeTreeViewCommand(UI::UITreeViewCommand::ExpandOrFirstChild, true);
    ASSERT_TRUE(firstChild.has_value()) << firstChild.error().message;
    EXPECT_EQ(firstChild->selection, (UI::UITreeViewSelection{.key = 6, .logicalIndex = 5, .level = 2}));
    EXPECT_TRUE(context->routeTreeViewCommand(UI::UITreeViewCommand::ExpandOrFirstChild, false)->consumed);

    auto parent = context->routeTreeViewCommand(UI::UITreeViewCommand::CollapseOrParent, true);
    ASSERT_TRUE(parent.has_value()) << parent.error().message;
    EXPECT_EQ(parent->selection, (UI::UITreeViewSelection{.key = 5, .logicalIndex = 4, .level = 1}));
    EXPECT_TRUE(context->routeTreeViewCommand(UI::UITreeViewCommand::CollapseOrParent, false)->consumed);

    auto collapsed = context->routeTreeViewCommand(UI::UITreeViewCommand::ToggleExpanded, true);
    ASSERT_TRUE(collapsed.has_value()) << collapsed.error().message;
    EXPECT_TRUE(collapsed->expansionChanged);
    EXPECT_FALSE(source.nodes[4].expanded);
    EXPECT_TRUE(context->routeTreeViewCommand(UI::UITreeViewCommand::ToggleExpanded, false)->consumed);

    auto activated = context->routeTreeViewCommand(UI::UITreeViewCommand::Activate, true);
    ASSERT_TRUE(activated.has_value()) << activated.error().message;
    EXPECT_TRUE(activated->consumed);
    EXPECT_TRUE(activated->activated);
    EXPECT_EQ(activated->selection.key, 5U);
    EXPECT_TRUE(context->routeTreeViewCommand(UI::UITreeViewCommand::Activate, false)->consumed);
}

TEST_F(UITreeViewTest, RejectedExpansionAndFailedCommitPreservePublishedState)
{
    constexpr u32 RowCapacity = 4;
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    FlatTreeDataSource source{.count = 20, .label = "Old"};

    const UI::UINodeId treeView = *updater.createTreeView(root.rootNodeId(), {.materializedItemCapacity = RowCapacity});
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 60.0F)));
    assertOk(updater.setLayoutStyle(treeView, fixedSize(100.0F, 60.0F)));
    assertOk(updater.setTreeViewStyle(treeView, {
                                                    .rowHeight = 20.0F,
                                                    .overscanRows = 1,
                                                    .wheelStep = 20.0F,
                                                }));
    assertOk(updater.setTreeViewDataSource(treeView, source.view()));
    assertOk(context->commitLayout({.width = 100.0F, .height = 60.0F}));

    const UI::UITreeViewMetrics committedMetrics = updater.treeViewMetrics(treeView).value();
    const u64 committedLayoutRevision = context->committedLayout().layoutRevision();
    const u64 committedSemanticsRevision = context->committedSemantics().semanticsRevision();
    const UI::UISemanticsEntry* committedFirst = findVirtualItem(context->committedSemantics(), 0);
    ASSERT_NE(committedFirst, nullptr);
    EXPECT_EQ(committedFirst->name, "Old");

    source.label = "New";
    source.failingIndex = 2;
    assertOk(updater.invalidateTreeViewItems(treeView));
    const Core::Status sourceFailure = context->commitLayout({.width = 100.0F, .height = 60.0F});
    ASSERT_FALSE(sourceFailure.has_value());
    EXPECT_EQ(sourceFailure.error().code, UI::UIErrorCode::InvalidControlValue);
    EXPECT_EQ(context->committedLayout().layoutRevision(), committedLayoutRevision);
    EXPECT_EQ(context->committedSemantics().semanticsRevision(), committedSemanticsRevision);
    EXPECT_EQ(updater.treeViewMetrics(treeView).value(), committedMetrics);
    committedFirst = findVirtualItem(context->committedSemantics(), 0);
    ASSERT_NE(committedFirst, nullptr);
    EXPECT_EQ(committedFirst->name, "Old");

    source.failingIndex = (std::numeric_limits<u64>::max)();
    assertOk(context->commitLayout({.width = 100.0F, .height = 60.0F}));
    EXPECT_EQ(findVirtualItem(context->committedSemantics(), 0)->name, "New");
    const UI::UITreeViewMetrics recoveredMetrics = updater.treeViewMetrics(treeView).value();
    const u64 recoveredRevision = context->committedLayout().layoutRevision();

    assertOk(updater.setTreeViewStyle(treeView, {
                                                    .rowHeight = 20.0F,
                                                    .overscanRows = 2,
                                                    .wheelStep = 20.0F,
                                                }));
    const Core::Status poolOverflow = context->commitLayout({.width = 100.0F, .height = 100.0F});
    ASSERT_FALSE(poolOverflow.has_value());
    EXPECT_EQ(poolOverflow.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->committedLayout().layoutRevision(), recoveredRevision);
    EXPECT_EQ(updater.treeViewMetrics(treeView).value(), recoveredMetrics);
}

TEST_F(UITreeViewTest, ExpansionRejectionLeavesVisibleProjectionUnchanged)
{
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    MutableTreeDataSource source;

    const UI::UINodeId treeView = *updater.createTreeView(root.rootNodeId(), {.materializedItemCapacity = 10});
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(160.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(treeView, fixedSize(160.0F, 120.0F)));
    assertOk(updater.setTreeViewStyle(treeView, {.rowHeight = 24.0F, .overscanRows = 1}));
    assertOk(updater.setTreeViewDataSource(treeView, source.view()));
    assertOk(context->commitLayout({.width = 160.0F, .height = 120.0F}));
    assertOk(updater.setTreeViewSelectedIndex(treeView, 0));
    assertOk(context->requestFocus(treeView));
    source.rejectExpansion = true;

    auto rejected = context->routeTreeViewCommand(UI::UITreeViewCommand::ToggleExpanded, true);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidControlValue);
    EXPECT_TRUE(source.nodes[0].expanded);
    EXPECT_EQ(source.visibleCount, 6U);
    assertOk(context->commitLayout({.width = 160.0F, .height = 120.0F}));
    EXPECT_EQ(updater.treeViewMetrics(treeView).value().logicalItemCount, 6U);
    EXPECT_EQ(updater.treeViewSelection(treeView).value().key, 1U);
}

TEST_F(UITreeViewTest, InternalRowsCannotBeDestroyedIndependently)
{
    constexpr u32 RowCapacity = 6;
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    FlatTreeDataSource source{.count = 10};

    const UI::UINodeId treeView = *updater.createTreeView(root.rootNodeId(), {.materializedItemCapacity = RowCapacity});
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 60.0F)));
    assertOk(updater.setLayoutStyle(treeView, fixedSize(100.0F, 60.0F)));
    assertOk(updater.setTreeViewStyle(treeView, {.rowHeight = 20.0F, .overscanRows = 1}));
    assertOk(updater.setTreeViewDataSource(treeView, source.view()));
    assertOk(context->commitLayout({.width = 100.0F, .height = 60.0F}));
    const UI::UISemanticsEntry* row = findVirtualItem(context->committedSemantics(), 0);
    ASSERT_NE(row, nullptr);

    const Core::Status rejected = updater.destroy(row->node);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidControlValue);
    EXPECT_EQ(context->liveNodeCount(), RowCapacity + 2U);

    assertOk(updater.destroy(treeView));
    EXPECT_EQ(context->liveNodeCount(), 1U);
}

TEST_F(UITreeViewTest, WheelAndCommitRemainAllocationFreeAfterWarmup)
{
    constexpr u32 RowCapacity = 12;
    ObservingMemoryResource resource;
    auto context = createContext(window, ContextNodeCapacity, resource);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    FlatTreeDataSource source{.count = 1'000};

    const UI::UINodeId treeView = *updater.createTreeView(root.rootNodeId(), {.materializedItemCapacity = RowCapacity});
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(treeView, fixedSize(100.0F, 100.0F)));
    assertOk(updater.setTreeViewStyle(treeView, {
                                                    .rowHeight = 20.0F,
                                                    .overscanRows = 2,
                                                    .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
                                                    .wheelStep = 20.0F,
                                                }));
    assertOk(updater.setTreeViewDataSource(treeView, source.view()));
    assertOk(updater.scrollTreeViewToIndex(treeView, 10, UI::UITreeViewScrollAlignment::Start));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const usize allocationCount = resource.allocationCount();

    for (u64 routeIndex = 0; routeIndex < 200; ++routeIndex)
    {
        const float wheelDelta = routeIndex % 2 == 0 ? -1.0F : 1.0F;
        auto routed =
            context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::Wheel, routeIndex + 1,
                                                    {.x = 50.0F, .y = 50.0F}, {.x = 0.0F, .y = wheelDelta}));
        ASSERT_TRUE(routed.has_value()) << (routed ? "" : routed.error().message);
        EXPECT_TRUE(routed->consumed);
        assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    }
    EXPECT_EQ(resource.allocationCount(), allocationCount);
}

} // namespace
} // namespace Tina::Tests
