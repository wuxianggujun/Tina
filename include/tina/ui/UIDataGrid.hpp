#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIScrollView.hpp>
#include <tina/ui/UIText.hpp>

#include <compare>
#include <string_view>

namespace Tina::UI {

using UIDataGridRowKey = u64;
using UIDataGridColumnKey = u64;
inline constexpr UIDataGridRowKey InvalidUIDataGridRowKey = 0;
inline constexpr UIDataGridColumnKey InvalidUIDataGridColumnKey = 0;

struct UIDataGridColumnDescriptor final {
    UIDataGridColumnKey key = InvalidUIDataGridColumnKey;
    std::string_view header{};
    // Exact logical width. Column resizing and automatic content measurement
    // are deliberately outside this first fixed-capacity contract.
    float width = 120.0F;

    auto operator<=>(const UIDataGridColumnDescriptor&) const = default;
};

struct UIDataGridRowDescriptor final {
    UIDataGridRowKey key = InvalidUIDataGridRowKey;
    bool enabled = true;

    auto operator<=>(const UIDataGridRowDescriptor&) const = default;
};

struct UIDataGridCellDescriptor final {
    std::string_view text{};

    auto operator<=>(const UIDataGridCellDescriptor&) const = default;
};

// Owner-thread data source borrowed by a DataGrid. Its row and column keys must
// be stable and non-zero. Returned headers and cell text must remain alive until
// the source is replaced or cleared. Callbacks must not mutate UIContext or
// throw. The column count must fit the fixed column capacity selected at
// creation; the logical row count is virtualized and is not limited by the
// materialized row capacity.
struct UIDataGridDataSource final {
    using RowCountOperation = u64 (*)(const void* state) noexcept;
    using ColumnCountOperation = u32 (*)(const void* state) noexcept;
    using ResolveRowOperation = bool (*)(
        const void* state, u64 logicalRow,
        UIDataGridRowDescriptor& output) noexcept;
    using ResolveColumnOperation = bool (*)(
        const void* state, u32 logicalColumn,
        UIDataGridColumnDescriptor& output) noexcept;
    using ResolveCellOperation = bool (*)(
        const void* state, u64 logicalRow, u32 logicalColumn,
        UIDataGridCellDescriptor& output) noexcept;

    const void* state = nullptr;
    RowCountOperation rowCount = nullptr;
    ColumnCountOperation columnCount = nullptr;
    ResolveRowOperation resolveRow = nullptr;
    ResolveColumnOperation resolveColumn = nullptr;
    ResolveCellOperation resolveCell = nullptr;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return state != nullptr && rowCount != nullptr &&
               columnCount != nullptr && resolveRow != nullptr &&
               resolveColumn != nullptr && resolveCell != nullptr;
    }
};

struct UIDataGridCreateConfig final {
    static constexpr u32 DefaultColumnCapacity = 16;
    static constexpr u32 MaximumColumnCapacity = 256;
    static constexpr u32 DefaultMaterializedRowCapacity = 64;
    static constexpr u32 MaximumMaterializedRowCapacity = 4096;

    // Columns and materialized rows use independent fixed-capacity pools.
    u32 columnCapacity = DefaultColumnCapacity;
    u32 materializedRowCapacity = DefaultMaterializedRowCapacity;

    auto operator<=>(const UIDataGridCreateConfig&) const = default;
};

struct UIDataGridStyle final {
    float columnHeaderHeight = 32.0F;
    float rowHeight = 28.0F;
    u32 overscanRows = 2;
    UIScrollBarVisibility scrollBarVisibility = UIScrollBarVisibility::Auto;
    float wheelStep = 48.0F;
    UITextOverflow headerTextOverflow = UITextOverflow::Ellipsis;
    UITextOverflow cellTextOverflow = UITextOverflow::Ellipsis;

    auto operator<=>(const UIDataGridStyle&) const = default;
};

struct UIDataGridPaint final {
    UIScrollViewPaint scrollBar{};
    UIStraightSrgba8Color columnHeaderBackgroundColor{};
    UIStraightSrgba8Color selectedRowBackgroundColor{};
    UIStraightSrgba8Color hoveredSelectedRowBackgroundColor{};
    UIStraightSrgba8Color focusedSelectedRowBackgroundColor{};
    UIStraightSrgba8Color gridLineColor{};

    auto operator<=>(const UIDataGridPaint&) const = default;
};

struct UIDataGridSelection final {
    UIDataGridRowKey rowKey = InvalidUIDataGridRowKey;
    UIDataGridColumnKey columnKey = InvalidUIDataGridColumnKey;
    u64 logicalRow = 0;
    u32 logicalColumn = 0;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return rowKey != InvalidUIDataGridRowKey &&
               columnKey != InvalidUIDataGridColumnKey;
    }

    auto operator<=>(const UIDataGridSelection&) const = default;
};

struct UIDataGridMetrics final {
    u64 logicalRowCount = 0;
    u32 logicalColumnCount = 0;
    u64 firstVisibleRow = 0;
    u32 visibleRowCount = 0;
    u64 firstMaterializedRow = 0;
    u32 materializedRowCount = 0;
    u32 materializedRowCapacity = 0;
    u32 columnCapacity = 0;
    UIScrollOffset scrollOffset{};
    UIScrollOffset maxScrollOffset{};
    UILogicalSize viewportSize{};
    UILogicalSize contentSize{};
    bool horizontalScrollBarVisible = false;
    bool verticalScrollBarVisible = false;

    auto operator<=>(const UIDataGridMetrics&) const = default;
};

enum class UIDataGridScrollAlignment : u8 {
    Nearest,
    Start,
    Center,
    End,
};

enum class UIDataGridCommand : u8 {
    PreviousColumn,
    NextColumn,
    PreviousRow,
    NextRow,
    PreviousPage,
    NextPage,
    FirstCell,
    LastCell,
    Activate,
};

struct UIDataGridCommandResult final {
    bool consumed = false;
    bool changed = false;
    bool activated = false;
    UIDataGridSelection selection{};
};

} // namespace Tina::UI
