#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;
inline constexpr usize ContextNodeCapacity = 256;

class ObservingMemoryResource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] usize allocationCount() const noexcept
    {
        return allocationCount_;
    }

  private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        void* storage = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++allocationCount_;
        return storage;
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(
        const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize allocationCount_ = 0;
};

struct VirtualGridDataSource final {
    u64 count = 0;
    u64 failingIndex = (std::numeric_limits<u64>::max)();
    UI::UIVirtualGridViewItemKey keyBase = 0;
    std::string label = "Grid item";
    std::string secondary = "Texture2D";
    std::string status = "Ready";
    std::optional<UI::UIImageSource> preview{};
    std::optional<UI::UIImageSource> icon{};
    std::array<u64, 4> disabledIndices{
        (std::numeric_limits<u64>::max)(),
        (std::numeric_limits<u64>::max)(),
        (std::numeric_limits<u64>::max)(),
        (std::numeric_limits<u64>::max)(),
    };

    [[nodiscard]] bool isDisabled(u64 logicalIndex) const noexcept
    {
        return std::ranges::find(disabledIndices, logicalIndex) !=
               disabledIndices.end();
    }

    [[nodiscard]] UI::UIVirtualGridViewDataSource view() const noexcept
    {
        return {
            .state = this,
            .itemCount = [](const void* state) noexcept {
                return static_cast<const VirtualGridDataSource*>(state)->count;
            },
            .resolveItem = [](const void* state, u64 logicalIndex,
                              UI::UIVirtualGridViewItemDescriptor& output) noexcept {
                const auto& source =
                    *static_cast<const VirtualGridDataSource*>(state);
                if (logicalIndex >= source.count ||
                    logicalIndex == source.failingIndex)
                {
                    return false;
                }
                output = UI::UIVirtualGridViewItemDescriptor{
                    .key = source.keyBase + logicalIndex + 1,
                    .label = source.label,
                    .enabled = !source.isDisabled(logicalIndex),
                    .presentation = {
                        .secondaryLabel = source.secondary,
                        .statusLabel = source.status,
                        .status = UI::UIVirtualGridViewItemStatus::Ready,
                        .preview = source.preview,
                        .icon = source.icon,
                    },
                };
                return true;
            },
        };
    }
};

[[nodiscard]] Core::AssetId imageAssetId(u8 discriminator) noexcept
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(discriminator);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] UI::UIImageSource imageSource(u8 discriminator) noexcept
{
    return {
        .texture = imageAssetId(discriminator),
        .sourcePixels = {.width = 16, .height = 16},
        .texturePixelExtent = {.width = 16, .height = 16},
        .intrinsicLogicalSize = {.width = 16.0F, .height = 16.0F},
    };
}

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window, usize nodeCapacity,
    std::pmr::memory_resource& resource)
{
    auto result = UI::UIContext::Create(
        window,
        {
            .nodeCapacity = nodeCapacity,
            .rootCapacity = 1,
            .paintSnapshotCapacity = nodeCapacity * 3,
            .routePathCapacity = nodeCapacity,
            .textByteCapacity = 16'384,
        },
        resource);
    EXPECT_TRUE(result.has_value())
        << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window, usize nodeCapacity)
{
    return createContext(
        window, nodeCapacity, *std::pmr::get_default_resource());
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
    ASSERT_TRUE(status.has_value())
        << (status ? "" : status.error().message);
}

[[nodiscard]] UI::UIPointerInputEvent pointerInput(
    Platform::WindowId window, UI::UIRoutedPointerEventKind kind, u64 sequence,
    UI::UILogicalPoint position, UI::UILogicalPoint delta = {}) noexcept
{
    return {
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

[[nodiscard]] const UI::UISemanticsEntry* findVirtualItem(
    UI::UICommittedSemanticsView view, u64 logicalIndex) noexcept
{
    for (const UI::UISemanticsEntry& entry : view.entries())
    {
        if (entry.role == UI::UISemanticsRole::ListItem &&
            entry.virtualItemIndex == logicalIndex)
        {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] const UI::UICommittedPaintEntry* findImagePaint(
    UI::UICommittedPaintView view, UI::UINodeId node) noexcept
{
    for (const UI::UICommittedPaintEntry& entry : view.entries())
    {
        if (entry.node == node &&
            entry.kind == UI::UICommittedPaintKind::Image)
        {
            return &entry;
        }
    }
    return nullptr;
}

class UIVirtualGridViewTest : public testing::Test {
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

TEST_F(UIVirtualGridViewTest,
       VirtualizesLargeDataSetWithFixedPoolAndOwnedInternalItems)
{
    constexpr u32 ItemCapacity = 10;
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = context->rootBuilder().createRoot();
    ASSERT_TRUE(root.has_value()) << root.error().message;
    auto updater = context->treeUpdater(*root);
    ASSERT_TRUE(updater.has_value()) << updater.error().message;
    VirtualGridDataSource source{.count = 100'000, .keyBase = 1'000};

    auto gridResult = updater->createElement(
        root->rootNodeId(), UI::makeVirtualGridViewElement(
                                {.materializedItemCapacity = ItemCapacity}));
    ASSERT_TRUE(gridResult.has_value()) << gridResult.error().message;
    const UI::UINodeId grid = *gridResult;
    assertOk(updater->setLayoutStyle(root->rootNodeId(), fixedSize(250.0F, 130.0F)));
    assertOk(updater->setLayoutStyle(grid, fixedSize(250.0F, 130.0F)));
    assertOk(updater->setVirtualGridViewStyle(
        grid,
        {
            .minimumItemWidth = 100.0F,
            .itemHeight = 40.0F,
            .columnGap = 10.0F,
            .rowGap = 10.0F,
            .overscanRows = 1,
            .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
        }));
    assertOk(updater->setVirtualGridViewDataSource(grid, source.view()));
    EXPECT_EQ(context->liveNodeCount(), ItemCapacity + 2U);

    assertOk(context->commitLayout({.width = 250.0F, .height = 130.0F}));
    UI::UIVirtualGridViewMetrics metrics =
        updater->virtualGridViewMetrics(grid).value();
    EXPECT_EQ(metrics.logicalItemCount, 100'000U);
    EXPECT_EQ(metrics.logicalRowCount, 50'000U);
    EXPECT_EQ(metrics.logicalColumnCount, 2U);
    EXPECT_EQ(metrics.visibleRowCount, 3U);
    EXPECT_EQ(metrics.materializedRowCount, 4U);
    EXPECT_EQ(metrics.materializedItemCount, 8U);
    EXPECT_EQ(metrics.materializedItemCapacity, ItemCapacity);
    EXPECT_FLOAT_EQ(metrics.itemWidth, 120.0F);
    EXPECT_FLOAT_EQ(metrics.contentSize.height, 2'499'990.0F);

    assertOk(updater->scrollVirtualGridViewToIndex(
        grid, 50'000, UI::UIVirtualGridViewScrollAlignment::Start));
    assertOk(context->commitLayout({.width = 250.0F, .height = 130.0F}));
    metrics = updater->virtualGridViewMetrics(grid).value();
    EXPECT_EQ(metrics.firstVisibleRow, 25'000U);
    EXPECT_EQ(metrics.firstMaterializedRow, 24'999U);
    EXPECT_EQ(metrics.firstMaterializedIndex, 49'998U);
    EXPECT_EQ(metrics.materializedItemCount, ItemCapacity);
    EXPECT_EQ(context->liveNodeCount(), ItemCapacity + 2U);

    const UI::UISemanticsEntry* item =
        findVirtualItem(context->committedSemantics(), 50'000);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->virtualItemKey, 51'001U);
    EXPECT_EQ(item->name, "Grid item");
    EXPECT_EQ(item->valueText, "Texture2D");
    EXPECT_EQ(item->description, "Ready");

    const Core::Status rejected = updater->destroy(item->node);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidControlValue);
    EXPECT_EQ(context->liveNodeCount(), ItemCapacity + 2U);
    assertOk(updater->destroy(grid));
    EXPECT_EQ(context->liveNodeCount(), 1U);
}

TEST_F(UIVirtualGridViewTest,
       PointerWheelThumbAndTrackUseCommittedGridGeometry)
{
    constexpr u32 ItemCapacity = 12;
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = context->rootBuilder().createRoot().value();
    auto updater = context->treeUpdater(root).value();
    VirtualGridDataSource source{.count = 100};
    const UI::UINodeId grid = *updater.createElement(
        root.rootNodeId(), UI::makeVirtualGridViewElement(
                               {.materializedItemCapacity = ItemCapacity}));
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(220.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(grid, fixedSize(220.0F, 120.0F)));
    assertOk(updater.setVirtualGridViewStyle(
        grid,
        {
            .minimumItemWidth = 100.0F,
            .itemHeight = 40.0F,
            .columnGap = 10.0F,
            .rowGap = 0.0F,
            .overscanRows = 1,
            .wheelStep = 40.0F,
        }));
    const UI::UIStraightSrgba8Color selectionColor{
        .red = 30, .green = 90, .blue = 150, .alpha = 255};
    assertOk(updater.setVirtualGridViewPaint(
        grid,
        {
            .scrollBar =
                {
                    .trackColor = UI::rgb(0x202830),
                    .thumbColor = UI::rgb(0x8090A0),
                    .draggingThumbColor = UI::rgb(0xE0A030),
                    .thickness = 10.0F,
                    .minThumbExtent = 24.0F,
                },
            .selectedItemBackgroundColor = selectionColor,
            .hoveredSelectedItemBackgroundColor = selectionColor,
            .focusedSelectedItemBackgroundColor = selectionColor,
            .pressedSelectedItemBackgroundColor = selectionColor,
        }));
    assertOk(updater.setVirtualGridViewDataSource(grid, source.view()));
    assertOk(context->commitLayout({.width = 220.0F, .height = 120.0F}));

    const UI::UISemanticsEntry* fourth =
        findVirtualItem(context->committedSemantics(), 3);
    ASSERT_NE(fourth, nullptr);
    const UI::UILogicalPoint center{
        .x = fourth->worldRect.x + fourth->worldRect.width * 0.5F,
        .y = fourth->worldRect.y + fourth->worldRect.height * 0.5F,
    };
    auto down = context->routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonDown, 1, center));
    ASSERT_TRUE(down.has_value()) << down.error().message;
    EXPECT_TRUE(down->consumed);
    auto up = context->routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonUp, 2, center));
    ASSERT_TRUE(up.has_value()) << up.error().message;
    EXPECT_TRUE(up->consumed);
    EXPECT_EQ(context->defaultActionFocus(), grid);
    EXPECT_EQ(updater.virtualGridViewSelection(grid).value(),
              (UI::UIVirtualGridViewSelection{
                  .key = 4,
                  .logicalIndex = 3,
                  .logicalRow = 1,
                  .logicalColumn = 1,
              }));

    assertOk(context->commitLayout({.width = 220.0F, .height = 120.0F}));
    const UI::UISemanticsEntry* selected =
        findVirtualItem(context->committedSemantics(), 3);
    ASSERT_NE(selected, nullptr);
    EXPECT_TRUE(selected->selected);
    EXPECT_TRUE(selected->focused);
    bool foundSelectionPaint = false;
    for (const UI::UICommittedPaintEntry& entry :
         context->committedPaint().entries())
    {
        foundSelectionPaint = foundSelectionPaint ||
                              (entry.node == selected->node &&
                               entry.solidFill == UI::premultiply(selectionColor));
    }
    EXPECT_TRUE(foundSelectionPaint);

    auto wheel = context->routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::Wheel, 3,
        {.x = 100.0F, .y = 60.0F}, {.x = 0.0F, .y = -1.0F}));
    ASSERT_TRUE(wheel.has_value()) << wheel.error().message;
    EXPECT_TRUE(wheel->consumed);
    assertOk(context->commitLayout({.width = 220.0F, .height = 120.0F}));
    EXPECT_FLOAT_EQ(
        updater.virtualGridViewMetrics(grid).value().scrollOffset, 40.0F);

    assertOk(updater.scrollVirtualGridViewToIndex(
        grid, 0, UI::UIVirtualGridViewScrollAlignment::Start));
    assertOk(context->commitLayout({.width = 220.0F, .height = 120.0F}));
    auto trackDown = context->routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonDown, 4,
        {.x = 215.0F, .y = 80.0F}));
    ASSERT_TRUE(trackDown.has_value()) << trackDown.error().message;
    EXPECT_TRUE(trackDown->consumed);
    static_cast<void>(context->routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonUp, 5,
        {.x = 215.0F, .y = 80.0F})));
    assertOk(context->commitLayout({.width = 220.0F, .height = 120.0F}));
    EXPECT_GT(updater.virtualGridViewMetrics(grid).value().scrollOffset, 0.0F);

    assertOk(updater.scrollVirtualGridViewToIndex(
        grid, 0, UI::UIVirtualGridViewScrollAlignment::Start));
    assertOk(context->commitLayout({.width = 220.0F, .height = 120.0F}));
    auto thumbDown = context->routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonDown, 6,
        {.x = 215.0F, .y = 12.0F}));
    ASSERT_TRUE(thumbDown.has_value()) << thumbDown.error().message;
    EXPECT_TRUE(thumbDown->consumed);
    EXPECT_EQ(context->pointerCapture(), grid);
    ASSERT_TRUE(context->routePointerInput(pointerInput(
                    window, UI::UIRoutedPointerEventKind::Move, 7,
                    {.x = 215.0F, .y = 90.0F}))
                    .has_value());
    ASSERT_TRUE(context->routePointerInput(pointerInput(
                    window, UI::UIRoutedPointerEventKind::ButtonUp, 8,
                    {.x = 215.0F, .y = 90.0F}))
                    .has_value());
    EXPECT_FALSE(context->pointerCapture().hasValue());
    assertOk(context->commitLayout({.width = 220.0F, .height = 120.0F}));
    EXPECT_GT(updater.virtualGridViewMetrics(grid).value().scrollOffset, 0.0F);
}

TEST_F(UIVirtualGridViewTest,
       CommandsPreserveNavigationAxisSkipDisabledItemsAndValidateActivation)
{
    constexpr u32 ItemCapacity = 15;
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = context->rootBuilder().createRoot().value();
    auto updater = context->treeUpdater(root).value();
    VirtualGridDataSource source{.count = 30, .keyBase = 100};
    source.disabledIndices[0] = 1;
    source.disabledIndices[1] = 5;
    const UI::UINodeId grid = *updater.createElement(
        root.rootNodeId(), UI::makeVirtualGridViewElement(
                               {.materializedItemCapacity = ItemCapacity}));
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(320.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(grid, fixedSize(320.0F, 100.0F)));
    assertOk(updater.setVirtualGridViewStyle(
        grid,
        {
            .minimumItemWidth = 100.0F,
            .itemHeight = 20.0F,
            .columnGap = 10.0F,
            .rowGap = 0.0F,
            .overscanRows = 0,
            .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
        }));
    assertOk(updater.setVirtualGridViewDataSource(grid, source.view()));
    assertOk(context->commitLayout({.width = 320.0F, .height = 100.0F}));
    assertOk(context->requestFocus(grid));

    auto first = context->routeVirtualGridViewCommand(
        UI::UIVirtualGridViewCommand::NextItem, true);
    ASSERT_TRUE(first.has_value()) << first.error().message;
    EXPECT_TRUE(first->changed);
    EXPECT_EQ(first->selection.logicalIndex, 0U);
    auto repeated = context->routeVirtualGridViewCommand(
        UI::UIVirtualGridViewCommand::NextItem, true);
    ASSERT_TRUE(repeated.has_value());
    EXPECT_TRUE(repeated->consumed);
    EXPECT_FALSE(repeated->changed);
    EXPECT_TRUE(context->routeVirtualGridViewCommand(
                    UI::UIVirtualGridViewCommand::NextItem, false)
                    ->consumed);

    auto next = context->routeVirtualGridViewCommand(
        UI::UIVirtualGridViewCommand::NextItem, true);
    ASSERT_TRUE(next.has_value()) << next.error().message;
    EXPECT_EQ(next->selection.logicalIndex, 2U);
    EXPECT_TRUE(context->routeVirtualGridViewCommand(
                    UI::UIVirtualGridViewCommand::NextItem, false)
                    ->consumed);

    auto nextRow = context->routeVirtualGridViewCommand(
        UI::UIVirtualGridViewCommand::NextRow, true);
    ASSERT_TRUE(nextRow.has_value()) << nextRow.error().message;
    EXPECT_EQ(nextRow->selection.logicalIndex, 8U);
    EXPECT_EQ(nextRow->selection.logicalRow, 2U);
    EXPECT_EQ(nextRow->selection.logicalColumn, 2U);
    EXPECT_TRUE(context->routeVirtualGridViewCommand(
                    UI::UIVirtualGridViewCommand::NextRow, false)
                    ->consumed);

    auto activate = context->routeVirtualGridViewCommand(
        UI::UIVirtualGridViewCommand::Activate, true);
    ASSERT_TRUE(activate.has_value()) << activate.error().message;
    EXPECT_TRUE(activate->activated);
    EXPECT_TRUE(context->routeVirtualGridViewCommand(
                    UI::UIVirtualGridViewCommand::Activate, false)
                    ->consumed);

    source.disabledIndices[2] = 8;
    assertOk(updater.invalidateVirtualGridViewItems(grid));
    activate = context->routeVirtualGridViewCommand(
        UI::UIVirtualGridViewCommand::Activate, true);
    ASSERT_TRUE(activate.has_value()) << activate.error().message;
    EXPECT_FALSE(activate->activated);
    EXPECT_TRUE(context->routeVirtualGridViewCommand(
                    UI::UIVirtualGridViewCommand::Activate, false)
                    ->consumed);
}

TEST_F(UIVirtualGridViewTest,
       FailedBindingAndPoolOverflowPreserveCommittedSnapshots)
{
    constexpr u32 ItemCapacity = 12;
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = context->rootBuilder().createRoot().value();
    auto updater = context->treeUpdater(root).value();
    VirtualGridDataSource source{.count = 40, .label = "Old"};
    const UI::UINodeId grid = *updater.createElement(
        root.rootNodeId(), UI::makeVirtualGridViewElement(
                               {.materializedItemCapacity = ItemCapacity}));
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(220.0F, 60.0F)));
    assertOk(updater.setLayoutStyle(grid, fixedSize(220.0F, 60.0F)));
    assertOk(updater.setVirtualGridViewStyle(
        grid,
        {
            .minimumItemWidth = 100.0F,
            .itemHeight = 20.0F,
            .columnGap = 10.0F,
            .rowGap = 0.0F,
            .overscanRows = 1,
            .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
        }));
    assertOk(updater.setVirtualGridViewDataSource(grid, source.view()));
    assertOk(context->commitLayout({.width = 220.0F, .height = 60.0F}));

    const UI::UIVirtualGridViewMetrics committedMetrics =
        updater.virtualGridViewMetrics(grid).value();
    const u64 committedLayoutRevision =
        context->committedLayout().layoutRevision();
    const u64 committedSemanticsRevision =
        context->committedSemantics().semanticsRevision();
    ASSERT_NE(findVirtualItem(context->committedSemantics(), 0), nullptr);
    EXPECT_EQ(findVirtualItem(context->committedSemantics(), 0)->name, "Old");

    source.label = "New";
    source.failingIndex = 2;
    assertOk(updater.invalidateVirtualGridViewItems(grid));
    const Core::Status sourceFailure =
        context->commitLayout({.width = 220.0F, .height = 60.0F});
    ASSERT_FALSE(sourceFailure.has_value());
    EXPECT_EQ(sourceFailure.error().code, UI::UIErrorCode::InvalidControlValue);
    EXPECT_EQ(context->committedLayout().layoutRevision(),
              committedLayoutRevision);
    EXPECT_EQ(context->committedSemantics().semanticsRevision(),
              committedSemanticsRevision);
    EXPECT_EQ(updater.virtualGridViewMetrics(grid).value(), committedMetrics);
    EXPECT_EQ(findVirtualItem(context->committedSemantics(), 0)->name, "Old");

    source.failingIndex = (std::numeric_limits<u64>::max)();
    assertOk(context->commitLayout({.width = 220.0F, .height = 60.0F}));
    EXPECT_EQ(findVirtualItem(context->committedSemantics(), 0)->name, "New");
    const UI::UIVirtualGridViewMetrics recoveredMetrics =
        updater.virtualGridViewMetrics(grid).value();
    const u64 recoveredRevision = context->committedLayout().layoutRevision();

    UI::UIVirtualGridViewStyle overflowStyle =
        updater.virtualGridViewStyle(grid).value();
    overflowStyle.overscanRows = 4;
    assertOk(updater.setVirtualGridViewStyle(grid, overflowStyle));
    const Core::Status poolOverflow =
        context->commitLayout({.width = 220.0F, .height = 60.0F});
    ASSERT_FALSE(poolOverflow.has_value());
    EXPECT_EQ(poolOverflow.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->committedLayout().layoutRevision(), recoveredRevision);
    EXPECT_EQ(updater.virtualGridViewMetrics(grid).value(), recoveredMetrics);
}

TEST_F(UIVirtualGridViewTest,
       PreviewWinsIconAndFailedCandidatePreservesCommittedPresentation)
{
    VirtualGridDataSource source{
        .count = 4,
        .label = "Old item",
        .secondary = "Old type",
        .status = "Old status",
        .preview = imageSource(0x11),
        .icon = imageSource(0x22),
    };
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = context->rootBuilder().createRoot().value();
    auto updater = context->treeUpdater(root).value();
    const UI::UINodeId grid = *updater.createElement(
        root.rootNodeId(), UI::makeVirtualGridViewElement(
                               {.materializedItemCapacity = 4}));
    assertOk(updater.setLayoutStyle(root.rootNodeId(),
                                    fixedSize(220.0F, 60.0F)));
    assertOk(updater.setLayoutStyle(grid, fixedSize(220.0F, 60.0F)));
    assertOk(updater.setVirtualGridViewStyle(
        grid,
        {
            .minimumItemWidth = 100.0F,
            .itemHeight = 20.0F,
            .columnGap = 10.0F,
            .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
        }));
    assertOk(updater.setVirtualGridViewDataSource(grid, source.view()));
    assertOk(context->commitLayout({.width = 220.0F, .height = 60.0F}));

    const UI::UISemanticsEntry* first =
        findVirtualItem(context->committedSemantics(), 0);
    ASSERT_NE(first, nullptr);
    const UI::UINodeId firstNode = first->node;
    const UI::UICommittedPaintEntry* firstImage =
        findImagePaint(context->committedPaint(), firstNode);
    ASSERT_NE(firstImage, nullptr);
    EXPECT_EQ(firstImage->imageSource.texture, imageAssetId(0x11));
    EXPECT_EQ(first->name, "Old item");
    EXPECT_EQ(first->valueText, "Old type");
    EXPECT_EQ(first->description, "Old status");
    const u64 committedLayoutRevision =
        context->committedLayout().layoutRevision();
    const u64 committedPaintRevision =
        context->committedPaint().paintRevision();
    const u64 committedSemanticsRevision =
        context->committedSemantics().semanticsRevision();

    source.label = "Candidate item";
    source.secondary = "Candidate type";
    source.status = "Candidate status";
    source.preview = imageSource(0x33);
    source.failingIndex = 1;
    assertOk(updater.invalidateVirtualGridViewItems(grid));
    const Core::Status rejected =
        context->commitLayout({.width = 220.0F, .height = 60.0F});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidControlValue);
    EXPECT_EQ(context->committedLayout().layoutRevision(),
              committedLayoutRevision);
    EXPECT_EQ(context->committedPaint().paintRevision(),
              committedPaintRevision);
    EXPECT_EQ(context->committedSemantics().semanticsRevision(),
              committedSemanticsRevision);
    first = findVirtualItem(context->committedSemantics(), 0);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->node, firstNode);
    EXPECT_EQ(first->name, "Old item");
    EXPECT_EQ(first->valueText, "Old type");
    EXPECT_EQ(first->description, "Old status");
    firstImage = findImagePaint(context->committedPaint(), firstNode);
    ASSERT_NE(firstImage, nullptr);
    EXPECT_EQ(firstImage->imageSource.texture, imageAssetId(0x11));

    source.failingIndex = (std::numeric_limits<u64>::max)();
    source.preview.reset();
    assertOk(context->commitLayout({.width = 220.0F, .height = 60.0F}));
    first = findVirtualItem(context->committedSemantics(), 0);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->name, "Candidate item");
    EXPECT_EQ(first->valueText, "Candidate type");
    EXPECT_EQ(first->description, "Candidate status");
    firstImage = findImagePaint(context->committedPaint(), first->node);
    ASSERT_NE(firstImage, nullptr);
    EXPECT_EQ(firstImage->imageSource.texture, imageAssetId(0x22));

    source.preview = imageSource(0x44);
    assertOk(updater.invalidateVirtualGridViewItems(grid));
    assertOk(context->commitLayout({.width = 220.0F, .height = 60.0F}));
    first = findVirtualItem(context->committedSemantics(), 0);
    ASSERT_NE(first, nullptr);
    firstImage = findImagePaint(context->committedPaint(), first->node);
    ASSERT_NE(firstImage, nullptr);
    EXPECT_EQ(firstImage->imageSource.texture, imageAssetId(0x44));
}

TEST_F(UIVirtualGridViewTest, PresentationRejectsInvalidUtf8WithoutPublishing)
{
    VirtualGridDataSource source{.count = 4};
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = context->rootBuilder().createRoot().value();
    auto updater = context->treeUpdater(root).value();
    const UI::UINodeId grid = *updater.createElement(
        root.rootNodeId(), UI::makeVirtualGridViewElement({.materializedItemCapacity = 4}));
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(220.0F, 60.0F)));
    assertOk(updater.setLayoutStyle(grid, fixedSize(220.0F, 60.0F)));
    assertOk(updater.setVirtualGridViewStyle(
        grid, {.minimumItemWidth = 100.0F, .itemHeight = 20.0F,
               .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden}));
    assertOk(updater.setVirtualGridViewDataSource(grid, source.view()));
    assertOk(context->commitLayout({.width = 220.0F, .height = 60.0F}));
    const u64 revision = context->committedSemantics().semanticsRevision();

    source.secondary.assign("\xFF", 1);
    assertOk(updater.invalidateVirtualGridViewItems(grid));
    const Core::Status rejected = context->commitLayout({.width = 220.0F, .height = 60.0F});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidText);
    EXPECT_EQ(context->committedSemantics().semanticsRevision(), revision);
}

TEST_F(UIVirtualGridViewTest,
       WheelAndCommitDoNotGrowContextStorageAfterWarmup)
{
    constexpr u32 ItemCapacity = 10;
    ObservingMemoryResource resource;
    auto context = createContext(window, ContextNodeCapacity, resource);
    ASSERT_NE(context, nullptr);
    auto root = context->rootBuilder().createRoot().value();
    auto updater = context->treeUpdater(root).value();
    VirtualGridDataSource source{.count = 1'000};
    const UI::UINodeId grid = *updater.createElement(
        root.rootNodeId(), UI::makeVirtualGridViewElement(
                               {.materializedItemCapacity = ItemCapacity}));
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(220.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(grid, fixedSize(220.0F, 100.0F)));
    assertOk(updater.setVirtualGridViewStyle(
        grid,
        {
            .minimumItemWidth = 100.0F,
            .itemHeight = 40.0F,
            .columnGap = 10.0F,
            .rowGap = 0.0F,
            .overscanRows = 1,
            .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
            .wheelStep = 40.0F,
    }));
    assertOk(updater.setVirtualGridViewDataSource(grid, source.view()));
    assertOk(context->commitLayout({.width = 220.0F, .height = 100.0F}));
    assertOk(updater.scrollVirtualGridViewToIndex(
        grid, 20, UI::UIVirtualGridViewScrollAlignment::Start));
    assertOk(context->commitLayout({.width = 220.0F, .height = 100.0F}));
    const usize allocationCount = resource.allocationCount();

    for (u64 routeIndex = 0; routeIndex < 100; ++routeIndex)
    {
        const float wheelDelta = routeIndex % 2 == 0 ? -1.0F : 1.0F;
        auto routed = context->routePointerInput(pointerInput(
            window, UI::UIRoutedPointerEventKind::Wheel, routeIndex + 1,
            {.x = 100.0F, .y = 50.0F}, {.x = 0.0F, .y = wheelDelta}));
        ASSERT_TRUE(routed.has_value()) << routed.error().message;
        EXPECT_TRUE(routed->consumed);
        assertOk(context->commitLayout({.width = 220.0F, .height = 100.0F}));
    }
    EXPECT_EQ(resource.allocationCount(), allocationCount);
}

} // namespace
} // namespace Tina::Tests
