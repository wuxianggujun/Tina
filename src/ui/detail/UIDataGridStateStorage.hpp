#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UIDataGrid.hpp>
#include <tina/ui/UINodeId.hpp>

#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::UI::Detail {

struct DataGridState final {
    UINodeId node{};
    UIDataGridStyle style{};
    UIDataGridPaint paint{};
    UIDataGridDataSource dataSource{};
    UIDataGridSelection selection{};
    UIDataGridMetrics committedMetrics{};
    UILogicalRect committedHeaderViewportRect{};
    UILogicalRect committedBodyViewportRect{};
    UINodeId firstColumn{};
    UINodeId lastColumn{};
    UINodeId firstMaterializedRow{};
    UINodeId lastMaterializedRow{};
    UIScrollOffset requestedScrollOffset{};
    u32 columnCapacity = 0;
    u32 materializedRowCapacity = 0;
    u32 linkedColumnCount = 0;
    u32 linkedMaterializedRowCount = 0;
};

struct DataGridColumnState final {
    UINodeId node{};
    UINodeId dataGrid{};
    UINodeId previousColumn{};
    UINodeId nextColumn{};
    u32 poolOrdinal = 0;

    UIDataGridColumnKey key = InvalidUIDataGridColumnKey;
    float width = 0.0F;
    bool bound = false;

    UIDataGridColumnKey committedKey = InvalidUIDataGridColumnKey;
    float committedWidth = 0.0F;
    bool committedBound = false;
};

struct DataGridRowState final {
    UINodeId node{};
    UINodeId dataGrid{};
    UINodeId previousRow{};
    UINodeId nextRow{};
    UINodeId firstCell{};
    UINodeId lastCell{};
    u32 poolOrdinal = 0;

    UIDataGridRowKey key = InvalidUIDataGridRowKey;
    u64 logicalRow = 0;
    bool bound = false;
    bool enabled = true;

    UIDataGridRowKey committedKey = InvalidUIDataGridRowKey;
    u64 committedLogicalRow = 0;
    bool committedBound = false;
    bool committedEnabled = true;
};

struct DataGridCellState final {
    UINodeId node{};
    UINodeId dataGrid{};
    UINodeId row{};
    UINodeId column{};
    UINodeId previousCell{};
    UINodeId nextCell{};
    u32 rowPoolOrdinal = 0;
    u32 columnOrdinal = 0;

    u64 logicalRow = 0;
    u32 logicalColumn = 0;
    bool bound = false;

    u64 committedLogicalRow = 0;
    u32 committedLogicalColumn = 0;
    bool committedBound = false;
};

struct DataGridLayoutScratch final {
    UIDataGridMetrics metrics{};
    UILogicalRect headerViewportRect{};
    UILogicalRect bodyViewportRect{};
};

// Index-aligned state for DataGrid and its fixed column, row, and row-major
// cell pools. Construction performs all allocation; steady-state binding and
// publication do not grow storage. UIContext owns topology and text content.
class UIDataGridStateStorage final {
  public:
    UIDataGridStateStorage(
        usize nodeCapacity, std::pmr::memory_resource& resource);

    [[nodiscard]] usize capacity() const noexcept;
    [[nodiscard]] bool containsGrid(UINodeId dataGrid) const noexcept;
    [[nodiscard]] bool containsColumn(UINodeId column) const noexcept;
    [[nodiscard]] bool containsRow(UINodeId row) const noexcept;
    [[nodiscard]] bool containsCell(UINodeId cell) const noexcept;
    [[nodiscard]] DataGridState* tryGrid(UINodeId dataGrid) noexcept;
    [[nodiscard]] const DataGridState* tryGrid(UINodeId dataGrid) const noexcept;
    [[nodiscard]] DataGridColumnState* tryColumn(UINodeId column) noexcept;
    [[nodiscard]] const DataGridColumnState* tryColumn(UINodeId column) const noexcept;
    [[nodiscard]] DataGridRowState* tryRow(UINodeId row) noexcept;
    [[nodiscard]] const DataGridRowState* tryRow(UINodeId row) const noexcept;
    [[nodiscard]] DataGridCellState* tryCell(UINodeId cell) noexcept;
    [[nodiscard]] const DataGridCellState* tryCell(UINodeId cell) const noexcept;
    [[nodiscard]] DataGridLayoutScratch* tryLayoutScratch(UINodeId dataGrid) noexcept;
    [[nodiscard]] const DataGridLayoutScratch* tryLayoutScratch(
        UINodeId dataGrid) const noexcept;

    void initializeGrid(
        UINodeId dataGrid, const UIDataGridCreateConfig& config) noexcept;
    [[nodiscard]] bool linkFixedPools(
        UINodeId dataGrid, std::span<const UINodeId> columns,
        std::span<const UINodeId> rows,
        std::span<const UINodeId> rowMajorCells) noexcept;
    void unlinkFixedPools(UINodeId dataGrid) noexcept;
    void resetNode(u32 nodeIndex) noexcept;
    [[nodiscard]] bool releaseNode(UINodeId node) noexcept;

    [[nodiscard]] bool relationshipValid(UINodeId dataGrid) const noexcept;
    [[nodiscard]] UINodeId columnAt(
        UINodeId dataGrid, u32 columnOrdinal) const noexcept;
    [[nodiscard]] UINodeId rowAt(
        UINodeId dataGrid, u32 rowPoolOrdinal) const noexcept;
    [[nodiscard]] UINodeId cellAt(
        UINodeId dataGrid, u32 rowPoolOrdinal,
        u32 columnOrdinal) const noexcept;
    [[nodiscard]] UINodeId gridForPoolNode(UINodeId node) const noexcept;

    void setDataSource(
        UINodeId dataGrid, UIDataGridDataSource dataSource) noexcept;
    void clearDataSource(UINodeId dataGrid) noexcept;
    [[nodiscard]] bool setSelection(
        UINodeId dataGrid, UIDataGridSelection selection) noexcept;
    [[nodiscard]] bool clearSelection(UINodeId dataGrid) noexcept;

    [[nodiscard]] bool bindColumn(
        UINodeId column, UIDataGridColumnKey key, float width) noexcept;
    [[nodiscard]] bool bindRow(
        UINodeId row, UIDataGridRowKey key, u64 logicalRow,
        bool enabled) noexcept;
    [[nodiscard]] bool bindCell(
        UINodeId cell, u64 logicalRow, u32 logicalColumn) noexcept;
    void clearColumnBindings(UINodeId dataGrid) noexcept;
    void clearRowBindings(UINodeId dataGrid) noexcept;
    void publishBindings(UINodeId dataGrid) noexcept;
    void publishMetrics(UINodeId dataGrid) noexcept;

  private:
    [[nodiscard]] bool beginLinkValidation() noexcept;
    [[nodiscard]] bool markLinkNode(UINodeId node) noexcept;

    std::pmr::vector<DataGridState> gridsByNodeIndex_;
    std::pmr::vector<DataGridColumnState> columnsByNodeIndex_;
    std::pmr::vector<DataGridRowState> rowsByNodeIndex_;
    std::pmr::vector<DataGridCellState> cellsByNodeIndex_;
    std::pmr::vector<DataGridLayoutScratch> layoutScratchByNodeIndex_;
    std::pmr::vector<u32> linkValidationEpochByNodeIndex_;
    u32 linkValidationEpoch_ = 0;
};

} // namespace Tina::UI::Detail
