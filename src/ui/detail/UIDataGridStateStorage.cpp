#include "UIDataGridStateStorage.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace Tina::UI::Detail {

UIDataGridStateStorage::UIDataGridStateStorage(
    usize gridCapacity, usize columnCapacity, usize rowCapacity,
    usize cellCapacity, usize linkValidationCapacity,
    std::pmr::memory_resource& resource)
    : grids_(gridCapacity, resource), columns_(columnCapacity, resource),
      rows_(rowCapacity, resource), cells_(cellCapacity, resource),
      linkValidationNodeIndices_(&resource)
{
    linkValidationNodeIndices_.reserve(linkValidationCapacity);
}

usize UIDataGridStateStorage::capacity() const noexcept
{
    return grids_.capacity();
}

usize UIDataGridStateStorage::availableGridCount() const noexcept
{
    return grids_.availableCount();
}

usize UIDataGridStateStorage::availableColumnCount() const noexcept
{
    return columns_.availableCount();
}

usize UIDataGridStateStorage::availableRowCount() const noexcept
{
    return rows_.availableCount();
}

usize UIDataGridStateStorage::availableCellCount() const noexcept
{
    return cells_.availableCount();
}

bool UIDataGridStateStorage::containsGrid(UINodeId dataGrid) const noexcept
{
    return grids_.contains(dataGrid);
}

bool UIDataGridStateStorage::containsColumn(UINodeId column) const noexcept
{
    return columns_.contains(column);
}

bool UIDataGridStateStorage::containsRow(UINodeId row) const noexcept
{
    return rows_.contains(row);
}

bool UIDataGridStateStorage::containsCell(UINodeId cell) const noexcept
{
    return cells_.contains(cell);
}

DataGridState* UIDataGridStateStorage::tryGrid(UINodeId dataGrid) noexcept
{
    return const_cast<DataGridState*>(std::as_const(*this).tryGrid(dataGrid));
}

const DataGridState* UIDataGridStateStorage::tryGrid(
    UINodeId dataGrid) const noexcept
{
    return grids_.tryGet(dataGrid);
}

DataGridColumnState* UIDataGridStateStorage::tryColumn(UINodeId column) noexcept
{
    return const_cast<DataGridColumnState*>(
        std::as_const(*this).tryColumn(column));
}

const DataGridColumnState* UIDataGridStateStorage::tryColumn(
    UINodeId column) const noexcept
{
    return columns_.tryGet(column);
}

DataGridRowState* UIDataGridStateStorage::tryRow(UINodeId row) noexcept
{
    return const_cast<DataGridRowState*>(std::as_const(*this).tryRow(row));
}

const DataGridRowState* UIDataGridStateStorage::tryRow(UINodeId row) const noexcept
{
    return rows_.tryGet(row);
}

DataGridCellState* UIDataGridStateStorage::tryCell(UINodeId cell) noexcept
{
    return const_cast<DataGridCellState*>(std::as_const(*this).tryCell(cell));
}

const DataGridCellState* UIDataGridStateStorage::tryCell(
    UINodeId cell) const noexcept
{
    return cells_.tryGet(cell);
}

DataGridLayoutScratch* UIDataGridStateStorage::tryLayoutScratch(
    UINodeId dataGrid) noexcept
{
    return const_cast<DataGridLayoutScratch*>(
        std::as_const(*this).tryLayoutScratch(dataGrid));
}

const DataGridLayoutScratch* UIDataGridStateStorage::tryLayoutScratch(
    UINodeId dataGrid) const noexcept
{
    const DataGridState* state = tryGrid(dataGrid);
    return state != nullptr ? &state->layoutScratch : nullptr;
}

bool UIDataGridStateStorage::initializeGrid(
    UINodeId dataGrid, const UIDataGridCreateConfig& config) noexcept
{
    if (!dataGrid.hasValue())
    {
        return false;
    }
    resetNode(dataGrid.index());
    return grids_.insertOrAssign(DataGridState{
        .node = dataGrid,
        .columnCapacity = config.columnCapacity,
        .materializedRowCapacity = config.materializedRowCapacity,
    });
}

bool UIDataGridStateStorage::beginLinkValidation() noexcept
{
    linkValidationNodeIndices_.clear();
    return true;
}

bool UIDataGridStateStorage::markLinkNode(UINodeId node) noexcept
{
    if (!node.hasValue() || grids_.tryGetByIndex(node.index()) != nullptr ||
        columns_.tryGetByIndex(node.index()) != nullptr ||
        rows_.tryGetByIndex(node.index()) != nullptr ||
        cells_.tryGetByIndex(node.index()) != nullptr)
    {
        return false;
    }
    const auto found = std::lower_bound(
        linkValidationNodeIndices_.begin(), linkValidationNodeIndices_.end(),
        node.index());
    if (found != linkValidationNodeIndices_.end() && *found == node.index())
    {
        return false;
    }
    linkValidationNodeIndices_.insert(found, node.index());
    return true;
}

bool UIDataGridStateStorage::linkFixedPools(
    UINodeId dataGrid, std::span<const UINodeId> columns,
    std::span<const UINodeId> rows,
    std::span<const UINodeId> rowMajorCells) noexcept
{
    DataGridState* grid = tryGrid(dataGrid);
    if (grid == nullptr || grid->linkedColumnCount != 0 ||
        grid->linkedMaterializedRowCount != 0 ||
        columns.size() != grid->columnCapacity ||
        rows.size() != grid->materializedRowCapacity ||
        (grid->columnCapacity != 0 &&
         grid->materializedRowCapacity >
             (std::numeric_limits<usize>::max)() / grid->columnCapacity))
    {
        return false;
    }
    const usize expectedCellCount =
        static_cast<usize>(grid->columnCapacity) *
        static_cast<usize>(grid->materializedRowCapacity);
    if (rowMajorCells.size() != expectedCellCount ||
        columns.size() > columns_.availableCount() ||
        rows.size() > rows_.availableCount() ||
        rowMajorCells.size() > cells_.availableCount() ||
        !beginLinkValidation())
    {
        return false;
    }
    for (const UINodeId column : columns)
    {
        if (!markLinkNode(column))
        {
            return false;
        }
    }
    for (const UINodeId row : rows)
    {
        if (!markLinkNode(row))
        {
            return false;
        }
    }
    for (const UINodeId cell : rowMajorCells)
    {
        if (!markLinkNode(cell))
        {
            return false;
        }
    }

    grid->firstColumn = columns.empty() ? UINodeId{} : columns.front();
    grid->lastColumn = columns.empty() ? UINodeId{} : columns.back();
    grid->firstMaterializedRow = rows.empty() ? UINodeId{} : rows.front();
    grid->lastMaterializedRow = rows.empty() ? UINodeId{} : rows.back();
    grid->linkedColumnCount = static_cast<u32>(columns.size());
    grid->linkedMaterializedRowCount = static_cast<u32>(rows.size());

    for (u32 ordinal = 0; ordinal < grid->linkedColumnCount; ++ordinal)
    {
        const UINodeId column = columns[ordinal];
        const bool inserted = columns_.insertOrAssign(DataGridColumnState{
            .node = column,
            .dataGrid = dataGrid,
            .previousColumn = ordinal == 0 ? UINodeId{} : columns[ordinal - 1],
            .nextColumn = ordinal + 1 == grid->linkedColumnCount
                              ? UINodeId{}
                              : columns[ordinal + 1],
            .poolOrdinal = ordinal,
        });
        if (!inserted)
        {
            std::terminate();
        }
    }
    for (u32 rowOrdinal = 0;
         rowOrdinal < grid->linkedMaterializedRowCount; ++rowOrdinal)
    {
        const UINodeId row = rows[rowOrdinal];
        const usize firstCellIndex =
            static_cast<usize>(rowOrdinal) * grid->linkedColumnCount;
        const bool rowInserted = rows_.insertOrAssign(DataGridRowState{
            .node = row,
            .dataGrid = dataGrid,
            .previousRow = rowOrdinal == 0 ? UINodeId{} : rows[rowOrdinal - 1],
            .nextRow = rowOrdinal + 1 == grid->linkedMaterializedRowCount
                           ? UINodeId{}
                           : rows[rowOrdinal + 1],
            .firstCell = grid->linkedColumnCount == 0
                             ? UINodeId{}
                             : rowMajorCells[firstCellIndex],
            .lastCell = grid->linkedColumnCount == 0
                            ? UINodeId{}
                            : rowMajorCells[firstCellIndex +
                                            grid->linkedColumnCount - 1U],
            .poolOrdinal = rowOrdinal,
        });
        if (!rowInserted)
        {
            std::terminate();
        }
        for (u32 columnOrdinal = 0;
             columnOrdinal < grid->linkedColumnCount; ++columnOrdinal)
        {
            const usize cellIndex = firstCellIndex + columnOrdinal;
            const UINodeId cell = rowMajorCells[cellIndex];
            const bool cellInserted = cells_.insertOrAssign(DataGridCellState{
                .node = cell,
                .dataGrid = dataGrid,
                .row = row,
                .column = columns[columnOrdinal],
                .previousCell = columnOrdinal == 0
                                    ? UINodeId{}
                                    : rowMajorCells[cellIndex - 1U],
                .nextCell = columnOrdinal + 1 == grid->linkedColumnCount
                                ? UINodeId{}
                                : rowMajorCells[cellIndex + 1U],
                .rowPoolOrdinal = rowOrdinal,
                .columnOrdinal = columnOrdinal,
                .logicalColumn = columnOrdinal,
                .committedLogicalColumn = columnOrdinal,
            });
            if (!cellInserted)
            {
                std::terminate();
            }
        }
    }
    return true;
}

void UIDataGridStateStorage::unlinkFixedPools(UINodeId dataGrid) noexcept
{
    DataGridState* grid = tryGrid(dataGrid);
    if (grid == nullptr)
    {
        return;
    }
    UINodeId column = grid->firstColumn;
    for (u32 visited = 0;
         column.hasValue() && visited < grid->linkedColumnCount; ++visited)
    {
        DataGridColumnState* state = tryColumn(column);
        if (state == nullptr || state->dataGrid != dataGrid)
        {
            break;
        }
        const UINodeId next = state->nextColumn;
        static_cast<void>(columns_.erase(column));
        column = next;
    }

    UINodeId row = grid->firstMaterializedRow;
    for (u32 visited = 0;
         row.hasValue() && visited < grid->linkedMaterializedRowCount; ++visited)
    {
        DataGridRowState* rowState = tryRow(row);
        if (rowState == nullptr || rowState->dataGrid != dataGrid)
        {
            break;
        }
        const UINodeId nextRow = rowState->nextRow;
        UINodeId cell = rowState->firstCell;
        for (u32 cellOrdinal = 0;
             cell.hasValue() && cellOrdinal < grid->linkedColumnCount;
             ++cellOrdinal)
        {
            DataGridCellState* cellState = tryCell(cell);
            if (cellState == nullptr || cellState->dataGrid != dataGrid ||
                cellState->row != row)
            {
                break;
            }
            const UINodeId nextCell = cellState->nextCell;
            static_cast<void>(cells_.erase(cell));
            cell = nextCell;
        }
        static_cast<void>(rows_.erase(row));
        row = nextRow;
    }

    grid->firstColumn = {};
    grid->lastColumn = {};
    grid->firstMaterializedRow = {};
    grid->lastMaterializedRow = {};
    grid->linkedColumnCount = 0;
    grid->linkedMaterializedRowCount = 0;
}

void UIDataGridStateStorage::resetNode(u32 nodeIndex) noexcept
{
    UINodeId owner{};
    if (const DataGridState* grid = grids_.tryGetByIndex(nodeIndex);
        grid != nullptr)
    {
        owner = grid->node;
    }
    else if (const DataGridColumnState* column =
                 columns_.tryGetByIndex(nodeIndex);
             column != nullptr)
    {
        owner = column->dataGrid;
    }
    else if (const DataGridRowState* row = rows_.tryGetByIndex(nodeIndex);
             row != nullptr)
    {
        owner = row->dataGrid;
    }
    else if (const DataGridCellState* cell = cells_.tryGetByIndex(nodeIndex);
             cell != nullptr)
    {
        owner = cell->dataGrid;
    }
    if (containsGrid(owner))
    {
        unlinkFixedPools(owner);
    }
    static_cast<void>(grids_.eraseByIndex(nodeIndex));
    static_cast<void>(columns_.eraseByIndex(nodeIndex));
    static_cast<void>(rows_.eraseByIndex(nodeIndex));
    static_cast<void>(cells_.eraseByIndex(nodeIndex));
}

bool UIDataGridStateStorage::releaseNode(UINodeId node) noexcept
{
    if (!node.hasValue())
    {
        return false;
    }
    const bool related = containsGrid(node) || containsColumn(node) ||
                         containsRow(node) || containsCell(node);
    resetNode(node.index());
    return related;
}

bool UIDataGridStateStorage::relationshipValid(UINodeId dataGrid) const noexcept
{
    const DataGridState* grid = tryGrid(dataGrid);
    if (grid == nullptr || grid->linkedColumnCount != grid->columnCapacity ||
        grid->linkedMaterializedRowCount != grid->materializedRowCapacity)
    {
        return false;
    }

    UINodeId previousColumn{};
    UINodeId column = grid->firstColumn;
    for (u32 ordinal = 0; ordinal < grid->linkedColumnCount; ++ordinal)
    {
        const DataGridColumnState* state = tryColumn(column);
        if (state == nullptr || state->dataGrid != dataGrid ||
            state->previousColumn != previousColumn ||
            state->poolOrdinal != ordinal)
        {
            return false;
        }
        previousColumn = column;
        column = state->nextColumn;
    }
    if (previousColumn != grid->lastColumn || column.hasValue())
    {
        return false;
    }

    UINodeId previousRow{};
    UINodeId row = grid->firstMaterializedRow;
    for (u32 rowOrdinal = 0;
         rowOrdinal < grid->linkedMaterializedRowCount; ++rowOrdinal)
    {
        const DataGridRowState* rowState = tryRow(row);
        if (rowState == nullptr || rowState->dataGrid != dataGrid ||
            rowState->previousRow != previousRow ||
            rowState->poolOrdinal != rowOrdinal)
        {
            return false;
        }
        UINodeId previousCell{};
        UINodeId cell = rowState->firstCell;
        UINodeId expectedColumn = grid->firstColumn;
        for (u32 columnOrdinal = 0;
             columnOrdinal < grid->linkedColumnCount; ++columnOrdinal)
        {
            const DataGridCellState* cellState = tryCell(cell);
            const DataGridColumnState* columnState = tryColumn(expectedColumn);
            if (cellState == nullptr || cellState->dataGrid != dataGrid ||
                cellState->row != row || cellState->column != expectedColumn ||
                cellState->previousCell != previousCell ||
                cellState->rowPoolOrdinal != rowOrdinal ||
                cellState->columnOrdinal != columnOrdinal ||
                columnState == nullptr || columnState->dataGrid != dataGrid)
            {
                return false;
            }
            previousCell = cell;
            cell = cellState->nextCell;
            expectedColumn = columnState->nextColumn;
        }
        if (previousCell != rowState->lastCell || cell.hasValue() ||
            expectedColumn.hasValue())
        {
            return false;
        }
        previousRow = row;
        row = rowState->nextRow;
    }
    return previousRow == grid->lastMaterializedRow && !row.hasValue();
}

UINodeId UIDataGridStateStorage::columnAt(
    UINodeId dataGrid, u32 columnOrdinal) const noexcept
{
    const DataGridState* grid = tryGrid(dataGrid);
    if (grid == nullptr || columnOrdinal >= grid->linkedColumnCount)
    {
        return {};
    }
    UINodeId column = grid->firstColumn;
    for (u32 ordinal = 0; ordinal < columnOrdinal && column.hasValue(); ++ordinal)
    {
        const DataGridColumnState* state = tryColumn(column);
        column = state != nullptr ? state->nextColumn : UINodeId{};
    }
    const DataGridColumnState* state = tryColumn(column);
    return state != nullptr && state->dataGrid == dataGrid &&
                   state->poolOrdinal == columnOrdinal
               ? column
               : UINodeId{};
}

UINodeId UIDataGridStateStorage::rowAt(
    UINodeId dataGrid, u32 rowPoolOrdinal) const noexcept
{
    const DataGridState* grid = tryGrid(dataGrid);
    if (grid == nullptr || rowPoolOrdinal >= grid->linkedMaterializedRowCount)
    {
        return {};
    }
    UINodeId row = grid->firstMaterializedRow;
    for (u32 ordinal = 0; ordinal < rowPoolOrdinal && row.hasValue(); ++ordinal)
    {
        const DataGridRowState* state = tryRow(row);
        row = state != nullptr ? state->nextRow : UINodeId{};
    }
    const DataGridRowState* state = tryRow(row);
    return state != nullptr && state->dataGrid == dataGrid &&
                   state->poolOrdinal == rowPoolOrdinal
               ? row
               : UINodeId{};
}

UINodeId UIDataGridStateStorage::cellAt(
    UINodeId dataGrid, u32 rowPoolOrdinal,
    u32 columnOrdinal) const noexcept
{
    const UINodeId row = rowAt(dataGrid, rowPoolOrdinal);
    const DataGridRowState* rowState = tryRow(row);
    const DataGridState* grid = tryGrid(dataGrid);
    if (rowState == nullptr || grid == nullptr ||
        columnOrdinal >= grid->linkedColumnCount)
    {
        return {};
    }
    UINodeId cell = rowState->firstCell;
    for (u32 ordinal = 0; ordinal < columnOrdinal && cell.hasValue(); ++ordinal)
    {
        const DataGridCellState* state = tryCell(cell);
        cell = state != nullptr ? state->nextCell : UINodeId{};
    }
    const DataGridCellState* state = tryCell(cell);
    return state != nullptr && state->dataGrid == dataGrid &&
                   state->rowPoolOrdinal == rowPoolOrdinal &&
                   state->columnOrdinal == columnOrdinal
               ? cell
               : UINodeId{};
}

UINodeId UIDataGridStateStorage::gridForPoolNode(UINodeId node) const noexcept
{
    if (const DataGridColumnState* column = tryColumn(node); column != nullptr)
    {
        return containsGrid(column->dataGrid) ? column->dataGrid : UINodeId{};
    }
    if (const DataGridRowState* row = tryRow(node); row != nullptr)
    {
        return containsGrid(row->dataGrid) ? row->dataGrid : UINodeId{};
    }
    if (const DataGridCellState* cell = tryCell(node); cell != nullptr)
    {
        return containsGrid(cell->dataGrid) ? cell->dataGrid : UINodeId{};
    }
    return {};
}

void UIDataGridStateStorage::setDataSource(
    UINodeId dataGrid, UIDataGridDataSource dataSource) noexcept
{
    DataGridState* grid = tryGrid(dataGrid);
    if (grid == nullptr)
    {
        return;
    }
    grid->dataSource = dataSource;
    grid->selection = {};
    grid->requestedScrollOffset = {};
    clearColumnBindings(dataGrid);
    clearRowBindings(dataGrid);
}

void UIDataGridStateStorage::clearDataSource(UINodeId dataGrid) noexcept
{
    setDataSource(dataGrid, {});
}

bool UIDataGridStateStorage::setSelection(
    UINodeId dataGrid, UIDataGridSelection selection) noexcept
{
    DataGridState* grid = tryGrid(dataGrid);
    if (grid == nullptr || grid->selection == selection)
    {
        return false;
    }
    grid->selection = selection;
    return true;
}

bool UIDataGridStateStorage::clearSelection(UINodeId dataGrid) noexcept
{
    return setSelection(dataGrid, {});
}

bool UIDataGridStateStorage::bindColumn(
    UINodeId column, UIDataGridColumnKey key, float width) noexcept
{
    DataGridColumnState* state = tryColumn(column);
    if (state == nullptr || key == InvalidUIDataGridColumnKey ||
        !std::isfinite(width) || width <= 0.0F)
    {
        return false;
    }
    state->key = key;
    state->width = width;
    state->bound = true;
    return true;
}

bool UIDataGridStateStorage::bindRow(
    UINodeId row, UIDataGridRowKey key, u64 logicalRow,
    bool enabled) noexcept
{
    DataGridRowState* state = tryRow(row);
    if (state == nullptr || key == InvalidUIDataGridRowKey)
    {
        return false;
    }
    state->key = key;
    state->logicalRow = logicalRow;
    state->bound = true;
    state->enabled = enabled;
    return true;
}

bool UIDataGridStateStorage::bindCell(
    UINodeId cell, u64 logicalRow, u32 logicalColumn) noexcept
{
    DataGridCellState* state = tryCell(cell);
    if (state == nullptr || state->columnOrdinal != logicalColumn)
    {
        return false;
    }
    const DataGridRowState* row = tryRow(state->row);
    if (row == nullptr || !row->bound || row->logicalRow != logicalRow)
    {
        return false;
    }
    state->logicalRow = logicalRow;
    state->logicalColumn = logicalColumn;
    state->bound = true;
    return true;
}

void UIDataGridStateStorage::clearColumnBindings(UINodeId dataGrid) noexcept
{
    DataGridState* grid = tryGrid(dataGrid);
    if (grid == nullptr)
    {
        return;
    }
    UINodeId column = grid->firstColumn;
    for (u32 visited = 0;
         column.hasValue() && visited < grid->linkedColumnCount; ++visited)
    {
        DataGridColumnState* state = tryColumn(column);
        if (state == nullptr || state->dataGrid != dataGrid)
        {
            break;
        }
        state->key = InvalidUIDataGridColumnKey;
        state->width = 0.0F;
        state->bound = false;
        column = state->nextColumn;
    }
}

void UIDataGridStateStorage::clearRowBindings(UINodeId dataGrid) noexcept
{
    DataGridState* grid = tryGrid(dataGrid);
    if (grid == nullptr)
    {
        return;
    }
    UINodeId row = grid->firstMaterializedRow;
    for (u32 visited = 0;
         row.hasValue() && visited < grid->linkedMaterializedRowCount; ++visited)
    {
        DataGridRowState* rowState = tryRow(row);
        if (rowState == nullptr || rowState->dataGrid != dataGrid)
        {
            break;
        }
        rowState->key = InvalidUIDataGridRowKey;
        rowState->logicalRow = 0;
        rowState->bound = false;
        rowState->enabled = true;

        UINodeId cell = rowState->firstCell;
        for (u32 columnOrdinal = 0;
             cell.hasValue() && columnOrdinal < grid->linkedColumnCount;
             ++columnOrdinal)
        {
            DataGridCellState* cellState = tryCell(cell);
            if (cellState == nullptr || cellState->row != row)
            {
                break;
            }
            cellState->logicalRow = 0;
            cellState->logicalColumn = cellState->columnOrdinal;
            cellState->bound = false;
            cell = cellState->nextCell;
        }
        row = rowState->nextRow;
    }
}

void UIDataGridStateStorage::publishBindings(UINodeId dataGrid) noexcept
{
    DataGridState* grid = tryGrid(dataGrid);
    if (grid == nullptr)
    {
        return;
    }
    UINodeId column = grid->firstColumn;
    for (u32 visited = 0;
         column.hasValue() && visited < grid->linkedColumnCount; ++visited)
    {
        DataGridColumnState* state = tryColumn(column);
        if (state == nullptr || state->dataGrid != dataGrid)
        {
            break;
        }
        state->committedKey = state->key;
        state->committedWidth = state->width;
        state->committedBound = state->bound;
        column = state->nextColumn;
    }

    UINodeId row = grid->firstMaterializedRow;
    for (u32 visited = 0;
         row.hasValue() && visited < grid->linkedMaterializedRowCount; ++visited)
    {
        DataGridRowState* rowState = tryRow(row);
        if (rowState == nullptr || rowState->dataGrid != dataGrid)
        {
            break;
        }
        rowState->committedKey = rowState->key;
        rowState->committedLogicalRow = rowState->logicalRow;
        rowState->committedBound = rowState->bound;
        rowState->committedEnabled = rowState->enabled;
        UINodeId cell = rowState->firstCell;
        for (u32 columnOrdinal = 0;
             cell.hasValue() && columnOrdinal < grid->linkedColumnCount;
             ++columnOrdinal)
        {
            DataGridCellState* cellState = tryCell(cell);
            if (cellState == nullptr || cellState->row != row)
            {
                break;
            }
            cellState->committedLogicalRow = cellState->logicalRow;
            cellState->committedLogicalColumn = cellState->logicalColumn;
            cellState->committedBound = cellState->bound;
            cell = cellState->nextCell;
        }
        row = rowState->nextRow;
    }
}

void UIDataGridStateStorage::publishMetrics(UINodeId dataGrid) noexcept
{
    DataGridState* grid = tryGrid(dataGrid);
    DataGridLayoutScratch* scratch = tryLayoutScratch(dataGrid);
    if (grid == nullptr || scratch == nullptr)
    {
        return;
    }
    grid->committedMetrics = scratch->metrics;
    grid->committedHeaderViewportRect = scratch->headerViewportRect;
    grid->committedBodyViewportRect = scratch->bodyViewportRect;
}

} // namespace Tina::UI::Detail
