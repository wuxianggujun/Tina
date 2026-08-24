#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;
inline constexpr usize ContextNodeCapacity = 384;

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

struct DataGridSource final {
    u64 rows = 0;
    u32 columns = 3;
    u64 rowKeyBase = 0;
    u64 columnKeyBase = 10'000;
    u64 failingRow = (std::numeric_limits<u64>::max)();
    u64 failingCellRow = (std::numeric_limits<u64>::max)();
    u32 failingCellColumn = (std::numeric_limits<u32>::max)();
    std::array<u64, 4> disabledRows{
        (std::numeric_limits<u64>::max)(),
        (std::numeric_limits<u64>::max)(),
        (std::numeric_limits<u64>::max)(),
        (std::numeric_limits<u64>::max)(),
    };
    std::array<std::string_view, 3> headers{"Kind", "Source", "Status"};
    std::array<float, 3> widths{80.0F, 120.0F, 100.0F};
    std::array<std::string, 3> cells{"Catalog", "Asset path", "Ready"};

    [[nodiscard]] bool isDisabled(u64 logicalRow) const noexcept
    {
        return std::ranges::find(disabledRows, logicalRow) !=
               disabledRows.end();
    }

    [[nodiscard]] UI::UIDataGridDataSource view() const noexcept
    {
        return {
            .state = this,
            .rowCount = [](const void* state) noexcept {
                return static_cast<const DataGridSource*>(state)->rows;
            },
            .columnCount = [](const void* state) noexcept {
                return static_cast<const DataGridSource*>(state)->columns;
            },
            .resolveRow = [](const void* state, u64 logicalRow,
                             UI::UIDataGridRowDescriptor& output) noexcept {
                const auto& source = *static_cast<const DataGridSource*>(state);
                if (logicalRow >= source.rows || logicalRow == source.failingRow)
                {
                    return false;
                }
                output = UI::UIDataGridRowDescriptor{
                    .key = source.rowKeyBase + logicalRow + 1,
                    .enabled = !source.isDisabled(logicalRow),
                };
                return true;
            },
            .resolveColumn = [](const void* state, u32 logicalColumn,
                                UI::UIDataGridColumnDescriptor& output) noexcept {
                const auto& source = *static_cast<const DataGridSource*>(state);
                if (logicalColumn >= source.columns ||
                    logicalColumn >= source.headers.size())
                {
                    return false;
                }
                output = UI::UIDataGridColumnDescriptor{
                    .key = source.columnKeyBase + logicalColumn + 1,
                    .header = source.headers[logicalColumn],
                    .width = source.widths[logicalColumn],
                };
                return true;
            },
            .resolveCell = [](const void* state, u64 logicalRow,
                              u32 logicalColumn,
                              UI::UIDataGridCellDescriptor& output) noexcept {
                const auto& source = *static_cast<const DataGridSource*>(state);
                if (logicalRow >= source.rows || logicalColumn >= source.columns ||
                    logicalColumn >= source.cells.size() ||
                    (logicalRow == source.failingCellRow &&
                     logicalColumn == source.failingCellColumn))
                {
                    return false;
                }
                output = UI::UIDataGridCellDescriptor{
                    .text = source.cells[logicalColumn]};
                return true;
            },
        };
    }
};

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window, usize nodeCapacity,
    std::pmr::memory_resource& resource)
{
    auto result = UI::UIContext::Create(
        window,
        {
            .nodeCapacity = nodeCapacity,
            .rootCapacity = 1,
            .paintSnapshotCapacity = nodeCapacity * 4,
            .routePathCapacity = nodeCapacity,
            .textByteCapacity = 32'768,
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

[[nodiscard]] const UI::UISemanticsEntry* findCell(
    UI::UICommittedSemanticsView view, u64 logicalRow,
    std::string_view text) noexcept
{
    for (const UI::UISemanticsEntry& entry : view.entries())
    {
        if (entry.role == UI::UISemanticsRole::ListItem &&
            entry.virtualItemIndex == logicalRow && entry.name == text)
        {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] const UI::UISemanticsEntry* findHeader(
    UI::UICommittedSemanticsView view, std::string_view text) noexcept
{
    for (const UI::UISemanticsEntry& entry : view.entries())
    {
        if (entry.role == UI::UISemanticsRole::Label && entry.name == text)
        {
            return &entry;
        }
    }
    return nullptr;
}

class UIDataGridTest : public testing::Test {
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

TEST_F(UIDataGridTest,
       VirtualizesRowsAndOwnsIndependentColumnRowAndCellPools)
{
    constexpr u32 ColumnCapacity = 3;
    constexpr u32 RowCapacity = 8;
    constexpr usize FixedPoolNodeCount =
        1U + ColumnCapacity + RowCapacity + ColumnCapacity * RowCapacity;
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = context->authoring().rootBuilder().createRoot().value();
    auto updater = context->authoring().treeUpdater(root).value();
    DataGridSource source{.rows = 100'000, .rowKeyBase = 1'000};
    const UI::UINodeId grid = *updater.createElement(
        root.rootNodeId(),
        UI::makeDataGridElement({
            .columnCapacity = ColumnCapacity,
            .materializedRowCapacity = RowCapacity,
        }));
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(180.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(grid, fixedSize(180.0F, 100.0F)));
    assertOk(updater.setDataGridStyle(
        grid,
        {
            .columnHeaderHeight = 20.0F,
            .rowHeight = 20.0F,
            .overscanRows = 1,
            .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
        }));
    assertOk(updater.setDataGridDataSource(grid, source.view()));
    EXPECT_EQ(context->liveNodeCount(), FixedPoolNodeCount + 1U);

    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 100.0F}));
    UI::UIDataGridMetrics metrics = updater.dataGridMetrics(grid).value();
    EXPECT_EQ(metrics.logicalRowCount, 100'000U);
    EXPECT_EQ(metrics.logicalColumnCount, 3U);
    EXPECT_EQ(metrics.visibleRowCount, 4U);
    EXPECT_EQ(metrics.materializedRowCount, 5U);
    EXPECT_EQ(metrics.materializedRowCapacity, RowCapacity);
    EXPECT_EQ(metrics.columnCapacity, ColumnCapacity);
    EXPECT_EQ(metrics.viewportSize,
              (UI::UILogicalSize{.width = 180.0F, .height = 80.0F}));
    EXPECT_EQ(metrics.contentSize,
              (UI::UILogicalSize{.width = 300.0F, .height = 2'000'000.0F}));

    assertOk(updater.scrollDataGridToCell(
        grid, 50'000, 2, UI::UIDataGridScrollAlignment::Start));
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 100.0F}));
    metrics = updater.dataGridMetrics(grid).value();
    EXPECT_EQ(metrics.firstVisibleRow, 50'000U);
    EXPECT_EQ(metrics.firstMaterializedRow, 49'999U);
    EXPECT_EQ(metrics.materializedRowCount, 6U);
    EXPECT_FLOAT_EQ(metrics.scrollOffset.x, 120.0F);
    EXPECT_FLOAT_EQ(metrics.scrollOffset.y, 1'000'000.0F);
    EXPECT_EQ(context->liveNodeCount(), FixedPoolNodeCount + 1U);

    const UI::UISemanticsEntry* cell =
        findCell(context->publication().committedSemantics(), 50'000, "Ready");
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->virtualItemKey, 51'001U);
    EXPECT_NE(findHeader(context->publication().committedSemantics(), "Status"), nullptr);

    const Core::Status rejected = updater.destroy(cell->node);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidControlValue);
    EXPECT_EQ(context->liveNodeCount(), FixedPoolNodeCount + 1U);
    assertOk(updater.destroy(grid));
    EXPECT_EQ(context->liveNodeCount(), 1U);
}

TEST_F(UIDataGridTest,
       PointerSelectionPaintAndDualAxisScrollbarsUseCommittedCells)
{
    constexpr u32 ColumnCapacity = 3;
    constexpr u32 RowCapacity = 8;
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = context->authoring().rootBuilder().createRoot().value();
    auto updater = context->authoring().treeUpdater(root).value();
    DataGridSource source{.rows = 100};
    const UI::UINodeId grid = *updater.createElement(
        root.rootNodeId(),
        UI::makeDataGridElement({
            .columnCapacity = ColumnCapacity,
            .materializedRowCapacity = RowCapacity,
        }));
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(180.0F, 120.0F)));
    assertOk(updater.setLayoutStyle(grid, fixedSize(180.0F, 120.0F)));
    assertOk(updater.setDataGridStyle(
        grid,
        {
            .columnHeaderHeight = 20.0F,
            .rowHeight = 20.0F,
            .overscanRows = 1,
            .wheelStep = 20.0F,
        }));
    const UI::UIStraightSrgba8Color selectionColor{
        .red = 24, .green = 96, .blue = 144, .alpha = 255};
    const UI::UIStraightSrgba8Color headerColor{
        .red = 32, .green = 40, .blue = 48, .alpha = 255};
    const UI::UIStraightSrgba8Color gridLineColor{
        .red = 80, .green = 88, .blue = 96, .alpha = 255};
    assertOk(updater.setDataGridPaint(
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
            .columnHeaderBackgroundColor = headerColor,
            .selectedRowBackgroundColor = selectionColor,
            .hoveredSelectedRowBackgroundColor = selectionColor,
            .focusedSelectedRowBackgroundColor = selectionColor,
            .gridLineColor = gridLineColor,
        }));
    assertOk(updater.setDataGridDataSource(grid, source.view()));
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 120.0F}));

    const UI::UISemanticsEntry* cell =
        findCell(context->publication().committedSemantics(), 1, "Asset path");
    ASSERT_NE(cell, nullptr);
    const UI::UILogicalPoint center{
        .x = cell->worldRect.x + cell->worldRect.width * 0.5F,
        .y = cell->worldRect.y + cell->worldRect.height * 0.5F,
    };
    auto down = context->input().routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonDown, 1, center));
    ASSERT_TRUE(down.has_value()) << down.error().message;
    EXPECT_TRUE(down->consumed);
    auto up = context->input().routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonUp, 2, center));
    ASSERT_TRUE(up.has_value()) << up.error().message;
    EXPECT_TRUE(up->consumed);
    EXPECT_EQ(context->input().defaultActionFocus(), grid);
    EXPECT_EQ(updater.dataGridSelection(grid).value(),
              (UI::UIDataGridSelection{
                  .rowKey = 2,
                  .columnKey = 10'002,
                  .logicalRow = 1,
                  .logicalColumn = 1,
              }));

    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 120.0F}));
    cell = findCell(context->publication().committedSemantics(), 1, "Asset path");
    ASSERT_NE(cell, nullptr);
    EXPECT_TRUE(cell->selected);
    EXPECT_TRUE(cell->focused);
    bool foundSelectionPaint = false;
    bool foundHeaderPaint = false;
    bool foundGridLinePaint = false;
    const UI::UISemanticsEntry* header =
        findHeader(context->publication().committedSemantics(), "Kind");
    ASSERT_NE(header, nullptr);
    for (const UI::UICommittedPaintEntry& entry :
         context->publication().committedPaint().entries())
    {
        foundSelectionPaint = foundSelectionPaint ||
                              (entry.node == cell->node &&
                               entry.solidFill == UI::premultiply(selectionColor));
        foundHeaderPaint = foundHeaderPaint ||
                           (entry.node == header->node &&
                            entry.solidFill == UI::premultiply(headerColor));
        foundGridLinePaint = foundGridLinePaint ||
                             (entry.node == cell->node &&
                              entry.solidFill == UI::premultiply(gridLineColor));
    }
    EXPECT_TRUE(foundSelectionPaint);
    EXPECT_TRUE(foundHeaderPaint);
    EXPECT_TRUE(foundGridLinePaint);

    auto horizontalWheel = context->input().routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::Wheel, 3,
        {.x = 100.0F, .y = 60.0F}, {.x = -1.0F, .y = 0.0F}));
    ASSERT_TRUE(horizontalWheel.has_value()) << horizontalWheel.error().message;
    EXPECT_TRUE(horizontalWheel->consumed);
    auto verticalWheel = context->input().routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::Wheel, 4,
        {.x = 100.0F, .y = 60.0F}, {.x = 0.0F, .y = -1.0F}));
    ASSERT_TRUE(verticalWheel.has_value()) << verticalWheel.error().message;
    EXPECT_TRUE(verticalWheel->consumed);
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 120.0F}));
    UI::UIDataGridMetrics metrics = updater.dataGridMetrics(grid).value();
    EXPECT_FLOAT_EQ(metrics.scrollOffset.x, 20.0F);
    EXPECT_FLOAT_EQ(metrics.scrollOffset.y, 20.0F);

    assertOk(updater.scrollDataGridToCell(
        grid, 0, 0, UI::UIDataGridScrollAlignment::Start));
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 120.0F}));
    auto horizontalTrack = context->input().routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonDown, 5,
        {.x = 140.0F, .y = 115.0F}));
    ASSERT_TRUE(horizontalTrack.has_value()) << horizontalTrack.error().message;
    EXPECT_TRUE(horizontalTrack->consumed);
    static_cast<void>(context->input().routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonUp, 6,
        {.x = 140.0F, .y = 115.0F})));
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 120.0F}));
    EXPECT_GT(updater.dataGridMetrics(grid).value().scrollOffset.x, 0.0F);

    assertOk(updater.scrollDataGridToCell(
        grid, 0, 0, UI::UIDataGridScrollAlignment::Start));
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 120.0F}));
    auto verticalThumb = context->input().routePointerInput(pointerInput(
        window, UI::UIRoutedPointerEventKind::ButtonDown, 7,
        {.x = 175.0F, .y = 32.0F}));
    ASSERT_TRUE(verticalThumb.has_value()) << verticalThumb.error().message;
    EXPECT_TRUE(verticalThumb->consumed);
    EXPECT_EQ(context->input().pointerCapture(), grid);
    ASSERT_TRUE(context->input().routePointerInput(pointerInput(
                    window, UI::UIRoutedPointerEventKind::Move, 8,
                    {.x = 175.0F, .y = 90.0F}))
                    .has_value());
    ASSERT_TRUE(context->input().routePointerInput(pointerInput(
                    window, UI::UIRoutedPointerEventKind::ButtonUp, 9,
                    {.x = 175.0F, .y = 90.0F}))
                    .has_value());
    EXPECT_FALSE(context->input().pointerCapture().hasValue());
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 120.0F}));
    EXPECT_GT(updater.dataGridMetrics(grid).value().scrollOffset.y, 0.0F);
}

TEST_F(UIDataGridTest,
       CommandsSkipDisabledRowsPreserveColumnsDebounceAndValidateActivation)
{
    constexpr u32 ColumnCapacity = 3;
    constexpr u32 RowCapacity = 6;
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = context->authoring().rootBuilder().createRoot().value();
    auto updater = context->authoring().treeUpdater(root).value();
    DataGridSource source{.rows = 30, .rowKeyBase = 100};
    source.disabledRows[0] = 1;
    source.disabledRows[1] = 4;
    const UI::UINodeId grid = *updater.createElement(
        root.rootNodeId(),
        UI::makeDataGridElement({
            .columnCapacity = ColumnCapacity,
            .materializedRowCapacity = RowCapacity,
        }));
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(300.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(grid, fixedSize(300.0F, 100.0F)));
    assertOk(updater.setDataGridStyle(
        grid,
        {
            .columnHeaderHeight = 20.0F,
            .rowHeight = 20.0F,
            .overscanRows = 1,
            .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
        }));
    assertOk(updater.setDataGridDataSource(grid, source.view()));
    assertOk(context->publication().commitLayout({.width = 300.0F, .height = 100.0F}));
    assertOk(context->input().requestFocus(grid));

    auto first = context->input().routeDataGridCommand(
        UI::UIDataGridCommand::NextRow, true);
    ASSERT_TRUE(first.has_value()) << first.error().message;
    EXPECT_TRUE(first->changed);
    EXPECT_EQ(first->selection.logicalRow, 0U);
    auto repeated = context->input().routeDataGridCommand(
        UI::UIDataGridCommand::NextRow, true);
    ASSERT_TRUE(repeated.has_value());
    EXPECT_TRUE(repeated->consumed);
    EXPECT_FALSE(repeated->changed);
    EXPECT_TRUE(context->input().routeDataGridCommand(
                    UI::UIDataGridCommand::NextRow, false)
                    ->consumed);

    auto nextRow = context->input().routeDataGridCommand(
        UI::UIDataGridCommand::NextRow, true);
    ASSERT_TRUE(nextRow.has_value()) << nextRow.error().message;
    EXPECT_EQ(nextRow->selection.logicalRow, 2U);
    EXPECT_EQ(nextRow->selection.logicalColumn, 0U);
    EXPECT_TRUE(context->input().routeDataGridCommand(
                    UI::UIDataGridCommand::NextRow, false)
                    ->consumed);

    auto nextColumn = context->input().routeDataGridCommand(
        UI::UIDataGridCommand::NextColumn, true);
    ASSERT_TRUE(nextColumn.has_value()) << nextColumn.error().message;
    EXPECT_EQ(nextColumn->selection.logicalRow, 2U);
    EXPECT_EQ(nextColumn->selection.logicalColumn, 1U);
    EXPECT_TRUE(context->input().routeDataGridCommand(
                    UI::UIDataGridCommand::NextColumn, false)
                    ->consumed);

    auto nextPage = context->input().routeDataGridCommand(
        UI::UIDataGridCommand::NextPage, true);
    ASSERT_TRUE(nextPage.has_value()) << nextPage.error().message;
    EXPECT_EQ(nextPage->selection.logicalRow, 6U);
    EXPECT_EQ(nextPage->selection.logicalColumn, 1U);
    EXPECT_TRUE(context->input().routeDataGridCommand(
                    UI::UIDataGridCommand::NextPage, false)
                    ->consumed);

    auto activate = context->input().routeDataGridCommand(
        UI::UIDataGridCommand::Activate, true);
    ASSERT_TRUE(activate.has_value()) << activate.error().message;
    EXPECT_TRUE(activate->activated);
    EXPECT_TRUE(context->input().routeDataGridCommand(
                    UI::UIDataGridCommand::Activate, false)
                    ->consumed);

    source.disabledRows[2] = 6;
    assertOk(updater.invalidateDataGridItems(grid));
    activate = context->input().routeDataGridCommand(
        UI::UIDataGridCommand::Activate, true);
    ASSERT_TRUE(activate.has_value()) << activate.error().message;
    EXPECT_FALSE(activate->activated);
    EXPECT_TRUE(context->input().routeDataGridCommand(
                    UI::UIDataGridCommand::Activate, false)
                    ->consumed);
}

TEST_F(UIDataGridTest,
       FailedCellBindingAndColumnOverflowPreserveCommittedSnapshots)
{
    constexpr u32 ColumnCapacity = 2;
    constexpr u32 RowCapacity = 6;
    auto context = createContext(window, ContextNodeCapacity);
    ASSERT_NE(context, nullptr);
    auto root = context->authoring().rootBuilder().createRoot().value();
    auto updater = context->authoring().treeUpdater(root).value();
    DataGridSource source{.rows = 40, .columns = 2};
    source.cells[0] = "Old";
    const UI::UINodeId grid = *updater.createElement(
        root.rootNodeId(),
        UI::makeDataGridElement({
            .columnCapacity = ColumnCapacity,
            .materializedRowCapacity = RowCapacity,
        }));
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(180.0F, 80.0F)));
    assertOk(updater.setLayoutStyle(grid, fixedSize(180.0F, 80.0F)));
    assertOk(updater.setDataGridStyle(
        grid,
        {
            .columnHeaderHeight = 20.0F,
            .rowHeight = 20.0F,
            .overscanRows = 1,
            .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
        }));
    assertOk(updater.setDataGridDataSource(grid, source.view()));
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 80.0F}));

    const UI::UIDataGridMetrics committedMetrics =
        updater.dataGridMetrics(grid).value();
    const u64 committedLayoutRevision =
        context->publication().committedLayout().layoutRevision();
    const u64 committedSemanticsRevision =
        context->publication().committedSemantics().semanticsRevision();
    ASSERT_NE(findCell(context->publication().committedSemantics(), 0, "Old"), nullptr);

    source.cells[0] = "New";
    source.failingCellRow = 1;
    source.failingCellColumn = 1;
    assertOk(updater.invalidateDataGridItems(grid));
    const Core::Status sourceFailure =
        context->publication().commitLayout({.width = 180.0F, .height = 80.0F});
    ASSERT_FALSE(sourceFailure.has_value());
    EXPECT_EQ(sourceFailure.error().code, UI::UIErrorCode::InvalidControlValue);
    EXPECT_EQ(context->publication().committedLayout().layoutRevision(),
              committedLayoutRevision);
    EXPECT_EQ(context->publication().committedSemantics().semanticsRevision(),
              committedSemanticsRevision);
    EXPECT_EQ(updater.dataGridMetrics(grid).value(), committedMetrics);
    EXPECT_NE(findCell(context->publication().committedSemantics(), 0, "Old"), nullptr);

    source.failingCellRow = (std::numeric_limits<u64>::max)();
    source.failingCellColumn = (std::numeric_limits<u32>::max)();
    assertOk(context->publication().commitLayout({.width = 180.0F, .height = 80.0F}));
    EXPECT_NE(findCell(context->publication().committedSemantics(), 0, "New"), nullptr);
    const UI::UIDataGridMetrics recoveredMetrics =
        updater.dataGridMetrics(grid).value();
    const u64 recoveredRevision = context->publication().committedLayout().layoutRevision();

    source.columns = 3;
    assertOk(updater.invalidateDataGridItems(grid));
    const Core::Status columnOverflow =
        context->publication().commitLayout({.width = 180.0F, .height = 80.0F});
    ASSERT_FALSE(columnOverflow.has_value());
    EXPECT_EQ(columnOverflow.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->publication().committedLayout().layoutRevision(), recoveredRevision);
    EXPECT_EQ(updater.dataGridMetrics(grid).value(), recoveredMetrics);
}

TEST_F(UIDataGridTest, WheelAndCommitDoNotGrowContextStorageAfterWarmup)
{
    constexpr u32 ColumnCapacity = 2;
    constexpr u32 RowCapacity = 6;
    ObservingMemoryResource resource;
    auto context = createContext(window, ContextNodeCapacity, resource);
    ASSERT_NE(context, nullptr);
    auto root = context->authoring().rootBuilder().createRoot().value();
    auto updater = context->authoring().treeUpdater(root).value();
    DataGridSource source{.rows = 1'000, .columns = 2};
    source.widths[0] = 140.0F;
    source.widths[1] = 140.0F;
    const UI::UINodeId grid = *updater.createElement(
        root.rootNodeId(),
        UI::makeDataGridElement({
            .columnCapacity = ColumnCapacity,
            .materializedRowCapacity = RowCapacity,
        }));
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(grid, fixedSize(200.0F, 100.0F)));
    assertOk(updater.setDataGridStyle(
        grid,
        {
            .columnHeaderHeight = 20.0F,
            .rowHeight = 20.0F,
            .overscanRows = 1,
            .scrollBarVisibility = UI::UIScrollBarVisibility::Hidden,
            .wheelStep = 20.0F,
        }));
    assertOk(updater.setDataGridDataSource(grid, source.view()));
    assertOk(updater.scrollDataGridToCell(
        grid, 10, 1, UI::UIDataGridScrollAlignment::Start));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));
    const UI::UIDataGridMetrics warmupMetrics =
        updater.dataGridMetrics(grid).value();
    EXPECT_EQ(warmupMetrics.firstVisibleRow, 10U);
    EXPECT_FLOAT_EQ(warmupMetrics.scrollOffset.x, 80.0F);
    EXPECT_FLOAT_EQ(warmupMetrics.scrollOffset.y, 200.0F);
    const usize allocationCount = resource.allocationCount();

    for (u64 routeIndex = 0; routeIndex < 100; ++routeIndex)
    {
        const float wheelDelta = routeIndex % 2 == 0 ? -1.0F : 1.0F;
        auto routed = context->input().routePointerInput(pointerInput(
            window, UI::UIRoutedPointerEventKind::Wheel, routeIndex + 1,
            {.x = 100.0F, .y = 60.0F}, {.x = 0.0F, .y = wheelDelta}));
        ASSERT_TRUE(routed.has_value()) << routed.error().message;
        EXPECT_TRUE(routed->consumed);
        assertOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));
    }
    EXPECT_EQ(resource.allocationCount(), allocationCount);
}

} // namespace
} // namespace Tina::Tests
