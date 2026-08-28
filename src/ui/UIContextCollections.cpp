#include "detail/UIContextImpl.hpp"

namespace Tina::UI {

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveDropdown(UINodeId dropdown)
{
    auto nodeResult = resolveNode(dropdown);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::Dropdown || dropdown.index() >= dropdownStatesByNodeIndex.size())
    {
        return fail(UIErrorCode::InvalidControlValue, "UI Dropdown API requires a Dropdown node");
    }
    return *nodeResult;
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveDropdownItem(UINodeId item)
{
    auto nodeResult = resolveNode(item);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::DropdownItem)
    {
        return fail(UIErrorCode::InvalidControlValue, "UI Dropdown item API requires a DropdownItem node");
    }
    return *nodeResult;
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveListView(UINodeId listView)
{
    auto nodeResult = resolveNode(listView);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::ListView || listView.index() >= listViewStatesByNodeIndex.size())
    {
        return fail(UIErrorCode::InvalidControlValue, "UI ListView API requires a ListView node");
    }
    return *nodeResult;
}

[[nodiscard]] UINodeId UIContext::Impl::listViewForItem(UINodeId item) const noexcept
{
    if (!contains(item))
    {
        return {};
    }
    const NodeRecord* itemRecord = nodes.tryGet(item.storageId());
    if (itemRecord == nullptr || itemRecord->kind != BuiltinElementKind::ListViewItem ||
        itemRecord->parentIndex == InvalidNodeIndex)
    {
        return {};
    }
    const UINodeId parent = idForIndex(itemRecord->parentIndex);
    const NodeRecord* parentRecord = contains(parent) ? nodes.tryGet(parent.storageId()) : nullptr;
    return parentRecord != nullptr && parentRecord->kind == BuiltinElementKind::ListView ? parent : UINodeId{};
}

[[nodiscard]] bool UIContext::Impl::isSelectedListViewItem(UINodeId item) const noexcept
{
    const UINodeId listView = listViewForItem(item);
    if (!listView.hasValue() || item.index() >= listViewItemStatesByNodeIndex.size())
    {
        return false;
    }
    const ListViewItemState& itemState = listViewItemStatesByNodeIndex[item.index()];
    const ListViewState& listState = listViewStatesByNodeIndex[listView.index()];
    return itemState.bound && listState.selection.hasValue() && itemState.key == listState.selection.key;
}

[[nodiscard]] Core::Status UIContext::Impl::selectCommittedListViewItem(UINodeId item)
{
    const UINodeId listView = listViewForItem(item);
    if (!listView.hasValue() || item.index() >= listViewItemStatesByNodeIndex.size() ||
        listView.index() >= listViewStatesByNodeIndex.size())
    {
        return fail(UIErrorCode::InvalidControlValue, "UI ListView selection requires a bound item row");
    }
    const ListViewItemState& itemState = listViewItemStatesByNodeIndex[item.index()];
    if (!itemState.committedBound || !itemState.committedEnabled)
    {
        return Core::success();
    }
    ListViewState& listState = listViewStatesByNodeIndex[listView.index()];
    const UIListViewSelection selection{
        .key = itemState.committedKey,
        .logicalIndex = itemState.committedLogicalIndex,
    };
    if (selection == listState.selection)
    {
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(listView); !dirty)
    {
        return dirty;
    }
    listState.selection = selection;
    return Core::success();
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveVirtualGridView(
    UINodeId virtualGridView)
{
    auto nodeResult = resolveNode(virtualGridView);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::VirtualGridView ||
        virtualGridViewStorage.tryView(virtualGridView) == nullptr)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI VirtualGridView API requires a VirtualGridView node");
    }
    return *nodeResult;
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveDataGrid(UINodeId dataGrid)
{
    auto nodeResult = resolveNode(dataGrid);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::DataGrid ||
        dataGridStorage.tryGrid(dataGrid) == nullptr)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI DataGrid API requires a DataGrid node");
    }
    return *nodeResult;
}

[[nodiscard]] Core::Status UIContext::Impl::validateDataGridUpdater(
    UINodeId updaterRoot, UINodeId dataGrid) const
{
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired,
                    "UI tree updater requires a live root owner");
    }
    auto result = const_cast<Impl*>(this)->resolveDataGrid(dataGrid);
    if (!result)
    {
        return Core::failure(result.error());
    }
    if (!isNodeWithinRoot(updaterRoot, dataGrid))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI DataGrid is not owned by the updater root");
    }
    return Core::success();
}

[[nodiscard]] UINodeId UIContext::Impl::virtualGridViewForItem(
    UINodeId item) const noexcept
{
    if (!contains(item))
    {
        return {};
    }
    const NodeRecord* itemRecord = nodes.tryGet(item.storageId());
    if (itemRecord == nullptr ||
        itemRecord->kind != BuiltinElementKind::VirtualGridViewItem ||
        itemRecord->parentIndex == InvalidNodeIndex)
    {
        return {};
    }
    const UINodeId owner = virtualGridViewStorage.viewForItem(item);
    const UINodeId parent = idForIndex(itemRecord->parentIndex);
    return owner == parent &&
                   virtualGridViewStorage.tryView(owner) != nullptr
               ? owner
               : UINodeId{};
}

[[nodiscard]] bool UIContext::Impl::isSelectedVirtualGridViewItem(
    UINodeId item) const noexcept
{
    const UINodeId virtualGridView = virtualGridViewForItem(item);
    const VirtualGridViewState* view =
        virtualGridViewStorage.tryView(virtualGridView);
    const VirtualGridViewItemState* itemState =
        virtualGridViewStorage.tryItem(item);
    return view != nullptr && itemState != nullptr && itemState->bound &&
           view->selection.hasValue() &&
           itemState->key == view->selection.key;
}

[[nodiscard]] Core::Status UIContext::Impl::selectCommittedVirtualGridViewItem(
    UINodeId item)
{
    const UINodeId virtualGridView = virtualGridViewForItem(item);
    VirtualGridViewState* view =
        virtualGridViewStorage.tryView(virtualGridView);
    const VirtualGridViewItemState* itemState =
        virtualGridViewStorage.tryItem(item);
    if (view == nullptr || itemState == nullptr)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI VirtualGridView selection requires a bound item");
    }
    if (!itemState->committedBound || !itemState->committedEnabled)
    {
        return Core::success();
    }
    const u32 columnCount = view->committedMetrics.logicalColumnCount;
    if (columnCount == 0)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI VirtualGridView selection requires a committed layout shape");
    }
    const UIVirtualGridViewSelection selection{
        .key = itemState->committedKey,
        .logicalIndex = itemState->committedLogicalIndex,
        .logicalRow = itemState->committedLogicalIndex / columnCount,
        .logicalColumn = static_cast<u32>(
            itemState->committedLogicalIndex % columnCount),
    };
    if (view->selection == selection)
    {
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(virtualGridView); !dirty)
    {
        return dirty;
    }
    static_cast<void>(virtualGridViewStorage.setSelection(
        virtualGridView, selection));
    return Core::success();
}

[[nodiscard]] UINodeId UIContext::Impl::dataGridForCell(UINodeId cell) const noexcept
{
    if (!contains(cell))
    {
        return {};
    }
    const NodeRecord* record = nodes.tryGet(cell.storageId());
    const DataGridCellState* cellState = dataGridStorage.tryCell(cell);
    if (record == nullptr || record->kind != BuiltinElementKind::DataGridCell ||
        cellState == nullptr || record->parentIndex != cellState->row.index())
    {
        return {};
    }
    const DataGridRowState* row = dataGridStorage.tryRow(cellState->row);
    const DataGridColumnState* column =
        dataGridStorage.tryColumn(cellState->column);
    const DataGridState* grid = dataGridStorage.tryGrid(cellState->dataGrid);
    return row != nullptr && column != nullptr && grid != nullptr &&
                   row->dataGrid == cellState->dataGrid &&
                   column->dataGrid == cellState->dataGrid
               ? cellState->dataGrid
               : UINodeId{};
}

[[nodiscard]] bool UIContext::Impl::isSelectedDataGridCell(UINodeId cell) const noexcept
{
    const UINodeId dataGrid = dataGridForCell(cell);
    const DataGridState* grid = dataGridStorage.tryGrid(dataGrid);
    const DataGridCellState* cellState = dataGridStorage.tryCell(cell);
    const DataGridRowState* row =
        cellState != nullptr ? dataGridStorage.tryRow(cellState->row) : nullptr;
    const DataGridColumnState* column =
        cellState != nullptr ? dataGridStorage.tryColumn(cellState->column)
                             : nullptr;
    return grid != nullptr && grid->selection.hasValue() &&
           cellState != nullptr && cellState->bound && row != nullptr &&
           row->bound && column != nullptr && column->bound &&
           row->key == grid->selection.rowKey &&
           column->key == grid->selection.columnKey &&
           cellState->logicalRow == grid->selection.logicalRow &&
           cellState->logicalColumn == grid->selection.logicalColumn;
}

[[nodiscard]] bool UIContext::Impl::isSelectedDataGridRowCell(UINodeId cell) const noexcept
{
    const UINodeId dataGrid = dataGridForCell(cell);
    const DataGridState* grid = dataGridStorage.tryGrid(dataGrid);
    const DataGridCellState* cellState = dataGridStorage.tryCell(cell);
    const DataGridRowState* row =
        cellState != nullptr ? dataGridStorage.tryRow(cellState->row) : nullptr;
    return grid != nullptr && grid->selection.hasValue() &&
           cellState != nullptr && cellState->bound && row != nullptr &&
           row->bound && row->key == grid->selection.rowKey &&
           cellState->logicalRow == grid->selection.logicalRow;
}

[[nodiscard]] bool UIContext::Impl::isHoveredDataGridRowCell(UINodeId cell) const noexcept
{
    const DataGridCellState* cellState = dataGridStorage.tryCell(cell);
    const DataGridCellState* hoveredState =
        dataGridStorage.tryCell(hoveredPrimaryControl);
    const DataGridRowState* row =
        cellState != nullptr ? dataGridStorage.tryRow(cellState->row) : nullptr;
    const DataGridRowState* hoveredRow = hoveredState != nullptr
                                             ? dataGridStorage.tryRow(
                                                   hoveredState->row)
                                             : nullptr;
    return cellState != nullptr && hoveredState != nullptr && row != nullptr &&
           hoveredRow != nullptr && cellState->dataGrid == hoveredState->dataGrid &&
           row->bound && hoveredRow->bound && row->key == hoveredRow->key &&
           row->logicalRow == hoveredRow->logicalRow;
}

[[nodiscard]] Core::Status UIContext::Impl::selectCommittedDataGridCell(UINodeId cell)
{
    const UINodeId dataGrid = dataGridForCell(cell);
    DataGridState* grid = dataGridStorage.tryGrid(dataGrid);
    const DataGridCellState* cellState = dataGridStorage.tryCell(cell);
    const DataGridRowState* row =
        cellState != nullptr ? dataGridStorage.tryRow(cellState->row) : nullptr;
    const DataGridColumnState* column =
        cellState != nullptr ? dataGridStorage.tryColumn(cellState->column)
                             : nullptr;
    if (grid == nullptr || cellState == nullptr || row == nullptr ||
        column == nullptr)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI DataGrid selection requires a bound cell");
    }
    if (!cellState->committedBound || !row->committedBound ||
        !row->committedEnabled || !column->committedBound)
    {
        return Core::success();
    }
    const UIDataGridSelection selection{
        .rowKey = row->committedKey,
        .columnKey = column->committedKey,
        .logicalRow = cellState->committedLogicalRow,
        .logicalColumn = cellState->committedLogicalColumn,
    };
    if (grid->selection == selection)
    {
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(dataGrid); !dirty)
    {
        return dirty;
    }
    static_cast<void>(dataGridStorage.setSelection(dataGrid, selection));
    return Core::success();
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveTreeView(UINodeId treeView)
{
    auto nodeResult = resolveNode(treeView);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::TreeView || treeView.index() >= treeViewStatesByNodeIndex.size())
    {
        return fail(UIErrorCode::InvalidControlValue, "UI TreeView API requires a TreeView node");
    }
    return *nodeResult;
}

[[nodiscard]] UINodeId UIContext::Impl::treeViewForItem(UINodeId item) const noexcept
{
    if (!contains(item))
    {
        return {};
    }
    const NodeRecord* itemRecord = nodes.tryGet(item.storageId());
    if (itemRecord == nullptr || itemRecord->kind != BuiltinElementKind::TreeViewItem ||
        itemRecord->parentIndex == InvalidNodeIndex)
    {
        return {};
    }
    const UINodeId parent = idForIndex(itemRecord->parentIndex);
    const NodeRecord* parentRecord = contains(parent) ? nodes.tryGet(parent.storageId()) : nullptr;
    return parentRecord != nullptr && parentRecord->kind == BuiltinElementKind::TreeView ? parent : UINodeId{};
}

[[nodiscard]] bool UIContext::Impl::pointWithinCommittedTreeDisclosure(UINodeId item, UILogicalPoint point,
                                                      std::span<const UICommittedHitEntry> entries) const noexcept
{
    const UINodeId treeView = treeViewForItem(item);
    if (!treeView.hasValue() || item.index() >= treeViewItemStatesByNodeIndex.size() ||
        treeView.index() >= treeViewStatesByNodeIndex.size())
    {
        return false;
    }
    const TreeViewItemState& itemState = treeViewItemStatesByNodeIndex[item.index()];
    if (!itemState.committedBound || !itemState.committedEnabled || !itemState.committedExpandable)
    {
        return false;
    }
    const u32 entryIndex = findHitEntryIndex(item, entries);
    if (entryIndex >= entries.size())
    {
        return false;
    }
    const UICommittedHitEntry& entry = entries[entryIndex];
    const UILogicalRect disclosure = makeTreeViewDisclosureRect(
        entry.worldRect, treeViewStatesByNodeIndex[treeView.index()].style, itemState.committedLevel);
    return containsPointHalfOpen(entry.worldRect, point) && containsPointHalfOpen(disclosure, point) &&
           containsPointHalfOpen(entry.effectiveClip, point);
}

[[nodiscard]] bool UIContext::Impl::isSelectedTreeViewItem(UINodeId item) const noexcept
{
    const UINodeId treeView = treeViewForItem(item);
    if (!treeView.hasValue() || item.index() >= treeViewItemStatesByNodeIndex.size())
    {
        return false;
    }
    const TreeViewItemState& itemState = treeViewItemStatesByNodeIndex[item.index()];
    const TreeViewState& treeState = treeViewStatesByNodeIndex[treeView.index()];
    return itemState.bound && treeState.selection.hasValue() && itemState.key == treeState.selection.key;
}

[[nodiscard]] Core::Status UIContext::Impl::selectCommittedTreeViewItem(UINodeId item)
{
    const UINodeId treeView = treeViewForItem(item);
    if (!treeView.hasValue() || item.index() >= treeViewItemStatesByNodeIndex.size() ||
        treeView.index() >= treeViewStatesByNodeIndex.size())
    {
        return fail(UIErrorCode::InvalidControlValue, "UI TreeView selection requires a bound item row");
    }
    const TreeViewItemState& itemState = treeViewItemStatesByNodeIndex[item.index()];
    if (!itemState.committedBound || !itemState.committedEnabled)
    {
        return Core::success();
    }
    TreeViewState& treeState = treeViewStatesByNodeIndex[treeView.index()];
    const UITreeViewSelection selection{
        .key = itemState.committedKey,
        .logicalIndex = itemState.committedLogicalIndex,
        .level = itemState.committedLevel,
    };
    if (selection == treeState.selection)
    {
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(treeView); !dirty)
    {
        return dirty;
    }
    treeState.selection = selection;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::toggleCommittedTreeViewItem(UINodeId item)
{
    const UINodeId treeView = treeViewForItem(item);
    if (!treeView.hasValue() || item.index() >= treeViewItemStatesByNodeIndex.size() ||
        treeView.index() >= treeViewStatesByNodeIndex.size())
    {
        return fail(UIErrorCode::InvalidControlValue, "UI TreeView expansion requires a bound item row");
    }
    const TreeViewItemState& itemState = treeViewItemStatesByNodeIndex[item.index()];
    if (!itemState.committedBound || !itemState.committedEnabled || !itemState.committedExpandable)
    {
        return Core::success();
    }
    TreeViewState& treeState = treeViewStatesByNodeIndex[treeView.index()];
    if (!treeState.dataSource.canSetItemExpanded())
    {
        return fail(UIErrorCode::InvalidControlValue, "UI TreeView data source does not support expansion");
    }
    if (Core::Status dirty = markLayoutStyleDirty(treeView); !dirty)
    {
        return dirty;
    }
    if (!treeState.dataSource.setItemExpanded(treeState.dataSource.state, itemState.committedKey,
                                              !itemState.committedExpanded))
    {
        return fail(UIErrorCode::InvalidControlValue, "UI TreeView data source rejected expansion");
    }
    if (treeState.selection.key == itemState.committedKey)
    {
        treeState.selection.logicalIndex = itemState.committedLogicalIndex;
        treeState.selection.level = itemState.committedLevel;
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setPopupOpenState(UINodeId popup, bool open)
{
    PopupState& popupState = popupStatesByNodeIndex[popup.index()];
    if (popupState.open == open && (!open || activePopupNode == popup))
    {
        return Core::success();
    }

    const UINodeId dropdown = dropdownForPopup(popup);
    if (!dropdown.hasValue())
    {
        return fail(UIErrorCode::InvalidParent, "UI Popup is detached from its Dropdown anchor");
    }
    if (open && (!isNodeEnabled(dropdown) || !isNodeEnabled(popup)))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI Dropdown and Popup must be enabled before the Popup can open");
    }

    UINodeId previousPopup{};
    UINodeId previousDropdown{};
    const UINodeId previousMenu = open ? menuStorage.rootMenu() : UINodeId{};
    const UINodeId previousMenuAnchor = menuPlacementAnchor(previousMenu);
    if (open && activePopupNode.hasValue() && activePopupNode != popup && contains(activePopupNode) &&
        activePopupNode.index() < popupStatesByNodeIndex.size() &&
        popupStatesByNodeIndex[activePopupNode.index()].open)
    {
        previousPopup = activePopupNode;
        previousDropdown = dropdownForPopup(previousPopup);
    }
    if (Core::Status dirty = markMenuMutationLayoutDirty(
            {popup, dropdown, previousPopup, previousDropdown,
             previousMenu, previousMenuAnchor},
            {previousMenu});
        !dirty)
    {
        return dirty;
    }

    if (previousPopup.hasValue())
    {
        popupStatesByNodeIndex[previousPopup.index()].open = false;
    }
    const bool focusWasInClosingPopup =
        (!open && isNodeWithinSubtree(popup, defaultActionFocusButton)) ||
        (previousPopup.hasValue() && isNodeWithinSubtree(previousPopup, defaultActionFocusButton)) ||
        (previousMenu.hasValue() &&
         isNodeWithinSubtree(previousMenu, defaultActionFocusButton));
    if (open)
    {
        focusRestoreByNodeIndex[popup.index()] = defaultActionFocusButton;
        popupState.open = true;
        activePopupNode = popup;
        if (previousMenu.hasValue())
        {
            static_cast<void>(menuStorage.close(previousMenu));
            menuCommandPressLatch.clear();
        }
    } else
    {
        popupState.open = false;
        if (activePopupNode == popup)
        {
            activePopupNode = {};
        }
    }
    transientOverlayDismissPointerBarrierActive = false;
    dropdownCommandPressLatch.clear();
    menuCommandPressLatch.clear();

    if (focusWasInClosingPopup)
    {
        UINodeId nextFocus = open ? dropdown : focusRestoreByNodeIndex[popup.index()];
        if (!nextFocus.hasValue() || !contains(nextFocus) || !isNodeEnabled(nextFocus))
        {
            nextFocus = isNodeEnabled(dropdown) ? dropdown : UINodeId{};
        }
        defaultActionPressState.clearAll();
        clearAllPointerArms();
        resetImeCompositionState();
        textInputFocus = {};
        defaultActionFocusButton = nextFocus;
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setPopupStyleFromUpdater(UINodeId updaterRoot, UINodeId popup,
                                                   const UIPopupStyle& style)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto popupResult = resolvePopup(popup);
    if (!popupResult)
    {
        return Core::failure(popupResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, popup))
    {
        return fail(UIErrorCode::InvalidNode, "UI Popup is not owned by the updater root");
    }
    auto normalized = Detail::normalizePopupStyle(style);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }
    PopupState& state = popupStatesByNodeIndex[popup.index()];
    if (state.style == *normalized)
    {
        return Core::success();
    }
    if (Core::Status dirty = markLayoutStyleDirty(popup); !dirty)
    {
        return dirty;
    }
    state.style = *normalized;
    return Core::success();
}

[[nodiscard]] Core::Result<UIPopupStyle> UIContext::Impl::popupStyleFromUpdater(UINodeId updaterRoot, UINodeId popup) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto popupResult = const_cast<Impl*>(this)->resolvePopup(popup);
    if (!popupResult)
    {
        return Core::failure(popupResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, popup))
    {
        return fail(UIErrorCode::InvalidNode, "UI Popup is not owned by the updater root");
    }
    return popupStatesByNodeIndex[popup.index()].style;
}

[[nodiscard]] Core::Status UIContext::Impl::setPopupOpenFromUpdater(UINodeId updaterRoot, UINodeId popup, bool open)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto popupResult = resolvePopup(popup);
    if (!popupResult)
    {
        return Core::failure(popupResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, popup))
    {
        return fail(UIErrorCode::InvalidNode, "UI Popup is not owned by the updater root");
    }
    return setPopupOpenState(popup, open);
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isPopupOpenFromUpdater(UINodeId updaterRoot, UINodeId popup) const
{
    auto styleResult = popupStyleFromUpdater(updaterRoot, popup);
    if (!styleResult)
    {
        return Core::failure(styleResult.error());
    }
    return popupStatesByNodeIndex[popup.index()].open;
}

[[nodiscard]] Core::Result<UIPopupMetrics> UIContext::Impl::popupMetricsFromUpdater(UINodeId updaterRoot, UINodeId popup) const
{
    auto styleResult = popupStyleFromUpdater(updaterRoot, popup);
    if (!styleResult)
    {
        return Core::failure(styleResult.error());
    }
    return popupStatesByNodeIndex[popup.index()].committedMetrics;
}

[[nodiscard]] Core::Status UIContext::Impl::setDropdownOpenFromUpdater(UINodeId updaterRoot, UINodeId dropdown, bool open)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto dropdownResult = resolveDropdown(dropdown);
    if (!dropdownResult)
    {
        return Core::failure(dropdownResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, dropdown))
    {
        return fail(UIErrorCode::InvalidNode, "UI Dropdown is not owned by the updater root");
    }
    const UINodeId popup = popupForDropdown(dropdown);
    if (!popup.hasValue())
    {
        return fail(UIErrorCode::InvalidControlValue, "UI Dropdown has no live Popup");
    }
    return setPopupOpenState(popup, open);
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isDropdownOpenFromUpdater(UINodeId updaterRoot, UINodeId dropdown) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto dropdownResult = const_cast<Impl*>(this)->resolveDropdown(dropdown);
    if (!dropdownResult)
    {
        return Core::failure(dropdownResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, dropdown))
    {
        return fail(UIErrorCode::InvalidNode, "UI Dropdown is not owned by the updater root");
    }
    const UINodeId popup = popupForDropdown(dropdown);
    return popup.hasValue() && popupStatesByNodeIndex[popup.index()].open;
}

[[nodiscard]] Core::Status UIContext::Impl::setDropdownSelectedItemFromUpdater(UINodeId updaterRoot, UINodeId dropdown,
                                                             UINodeId item)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto dropdownResult = resolveDropdown(dropdown);
    if (!dropdownResult)
    {
        return Core::failure(dropdownResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, dropdown))
    {
        return fail(UIErrorCode::InvalidNode, "UI Dropdown is not owned by the updater root");
    }
    if (item.hasValue())
    {
        auto itemResult = resolveDropdownItem(item);
        if (!itemResult)
        {
            return Core::failure(itemResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, item) || dropdownForItem(item) != dropdown)
        {
            return fail(UIErrorCode::InvalidParent, "UI DropdownItem belongs to another Dropdown");
        }
    }

    Detail::UISelectBehaviorState* select = behaviorStateStorage.trySelectState(dropdown.index());
    if (select == nullptr)
    {
        return fail(Core::CoreErrorCode::Internal, "UI Dropdown is missing Select behavior state");
    }
    if (select->selectedOption == item)
    {
        return Core::success();
    }
    const UINodeId previousItem = select->selectedOption;
    if (Core::Status dirty = markLayoutDirtyBatch({dropdown, previousItem, item}); !dirty)
    {
        return dirty;
    }
    select->selectedOption = item;
    return Core::success();
}

[[nodiscard]] Core::Result<UINodeId> UIContext::Impl::dropdownSelectedItemFromUpdater(UINodeId updaterRoot,
                                                                     UINodeId dropdown) const
{
    auto openResult = isDropdownOpenFromUpdater(updaterRoot, dropdown);
    if (!openResult)
    {
        return Core::failure(openResult.error());
    }
    const Detail::UISelectBehaviorState* select = behaviorStateStorage.trySelectState(dropdown.index());
    if (select == nullptr)
    {
        return fail(Core::CoreErrorCode::Internal, "UI Dropdown is missing Select behavior state");
    }
    const UINodeId selected = select->selectedOption;
    return contains(selected) ? selected : UINodeId{};
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isDropdownItemSelectedFromUpdater(UINodeId updaterRoot,
                                                                   UINodeId item) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto itemResult = const_cast<Impl*>(this)->resolveDropdownItem(item);
    if (!itemResult)
    {
        return Core::failure(itemResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, item))
    {
        return fail(UIErrorCode::InvalidNode, "UI DropdownItem is not owned by the updater root");
    }
    const UINodeId dropdown = dropdownForItem(item);
    const Detail::UISelectBehaviorState* select =
        dropdown.hasValue() ? behaviorStateStorage.trySelectState(dropdown.index()) : nullptr;
    if (dropdown.hasValue() && select == nullptr)
    {
        return fail(Core::CoreErrorCode::Internal, "UI Dropdown is missing Select behavior state");
    }
    return select != nullptr && select->selectedOption == item;
}

[[nodiscard]] Core::Status UIContext::Impl::setDropdownPaintFromUpdater(UINodeId updaterRoot, UINodeId dropdown,
                                                      const UIDropdownPaint& paint)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto dropdownResult = resolveDropdown(dropdown);
    if (!dropdownResult)
    {
        return Core::failure(dropdownResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, dropdown))
    {
        return fail(UIErrorCode::InvalidNode, "UI Dropdown is not owned by the updater root");
    }
    auto normalized = Detail::normalizeDropdownPaint(paint);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }
    UIDropdownPaint& current = dropdownStatesByNodeIndex[dropdown.index()].paint;
    if (current == *normalized)
    {
        detachThemeBinding(dropdown.index(), ThemeBindingDropdownPaint);
        return Core::success();
    }
    const bool layoutChanged = current.indicatorWidth != normalized->indicatorWidth ||
                               current.indicatorHeight != normalized->indicatorHeight ||
                               current.indicatorInset != normalized->indicatorInset;
    Core::Status dirty = layoutChanged ? markLayoutStyleDirty(dropdown) : markPaintDirty(dropdown);
    if (!dirty)
    {
        return dirty;
    }
    current = *normalized;
    detachThemeBinding(dropdown.index(), ThemeBindingDropdownPaint);
    return Core::success();
}

[[nodiscard]] Core::Result<UIDropdownPaint> UIContext::Impl::dropdownPaintFromUpdater(UINodeId updaterRoot,
                                                                    UINodeId dropdown) const
{
    auto openResult = isDropdownOpenFromUpdater(updaterRoot, dropdown);
    if (!openResult)
    {
        return Core::failure(openResult.error());
    }
    return dropdownStatesByNodeIndex[dropdown.index()].paint;
}

[[nodiscard]] Core::Status UIContext::Impl::validateListViewUpdater(UINodeId updaterRoot, UINodeId listView) const
{
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto listResult = const_cast<Impl*>(this)->resolveListView(listView);
    if (!listResult)
    {
        return Core::failure(listResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, listView))
    {
        return fail(UIErrorCode::InvalidNode, "UI ListView is not owned by the updater root");
    }
    return Core::success();
}

[[nodiscard]] Core::Result<UIListViewItemDescriptor> UIContext::Impl::resolveListViewLogicalItem(UINodeId listView,
                                                                               u64 logicalIndex) const
{
    const ListViewState& state = listViewStatesByNodeIndex[listView.index()];
    if (!state.dataSource.hasValue())
    {
        return fail(UIErrorCode::InvalidControlValue, "UI ListView has no data source");
    }
    const u64 itemCount = state.dataSource.itemCount(state.dataSource.state);
    if (logicalIndex >= itemCount)
    {
        return fail(UIErrorCode::InvalidControlValue, "UI ListView logical item index is out of range");
    }
    UIListViewItemDescriptor descriptor{};
    if (!state.dataSource.resolveItem(state.dataSource.state, logicalIndex, descriptor))
    {
        return fail(UIErrorCode::InvalidControlValue, "UI ListView data source failed to resolve an item");
    }
    if (descriptor.key == InvalidUIListViewItemKey || containsLineBreak(descriptor.label))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI ListView item requires a non-zero key and a single-line label");
    }
    return descriptor;
}

[[nodiscard]] Core::Status UIContext::Impl::setListViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                           UIListViewDataSource source)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
    {
        return valid;
    }
    if (!source.hasValue())
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI ListView data source requires state, itemCount, and resolveItem");
    }
    if (Core::Status dirty = markLayoutStyleDirty(listView); !dirty)
    {
        return dirty;
    }
    ListViewState& state = listViewStatesByNodeIndex[listView.index()];
    state.dataSource = source;
    state.selection = {};
    state.requestedScrollOffset = 0.0F;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::clearListViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId listView)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
    {
        return valid;
    }
    ListViewState& state = listViewStatesByNodeIndex[listView.index()];
    if (!state.dataSource.hasValue())
    {
        return Core::success();
    }
    if (Core::Status dirty = markLayoutStyleDirty(listView); !dirty)
    {
        return dirty;
    }
    state.dataSource = {};
    state.selection = {};
    state.requestedScrollOffset = 0.0F;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::invalidateListViewItemsFromUpdater(UINodeId updaterRoot, UINodeId listView)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
    {
        return valid;
    }
    return markLayoutStyleDirty(listView);
}

[[nodiscard]] Core::Status UIContext::Impl::setListViewStyleFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                      const UIListViewStyle& style)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
    {
        return valid;
    }
    auto normalized = Detail::normalizeListViewStyle(style);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }
    ListViewState& state = listViewStatesByNodeIndex[listView.index()];
    if (state.style == *normalized)
    {
        return Core::success();
    }
    if (Core::Status dirty = markLayoutStyleDirty(listView); !dirty)
    {
        return dirty;
    }
    state.style = *normalized;
    const NodeRecord* listRecord = nodes.tryGet(listView.storageId());
    u32 child = listRecord == nullptr ? InvalidNodeIndex : listRecord->firstChildIndex;
    while (child != InvalidNodeIndex)
    {
        NodeRecord* childRecord = recordByIndex(child);
        if (childRecord == nullptr)
        {
            break;
        }
        configureCollectionRowLayout(layoutStylesByIndex[child], state.style.rowHeight);
        textStatesByIndex[child].overflow = state.style.rowTextOverflow;
        child = childRecord->nextSiblingIndex;
    }
    return Core::success();
}

[[nodiscard]] Core::Result<UIListViewStyle> UIContext::Impl::listViewStyleFromUpdater(UINodeId updaterRoot,
                                                                    UINodeId listView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
    {
        return Core::failure(valid.error());
    }
    return listViewStatesByNodeIndex[listView.index()].style;
}

[[nodiscard]] Core::Status UIContext::Impl::setListViewPaintFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                      const UIListViewPaint& paint)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
    {
        return valid;
    }
    auto normalized = Detail::normalizeListViewPaint(paint);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }
    ListViewState& state = listViewStatesByNodeIndex[listView.index()];
    if (state.paint == *normalized)
    {
        detachThemeBinding(listView.index(), ThemeBindingListViewPaint);
        return Core::success();
    }
    const bool layoutChanged = state.paint.scrollBar.thickness != normalized->scrollBar.thickness ||
                               state.paint.scrollBar.minThumbExtent != normalized->scrollBar.minThumbExtent;
    Core::Status dirty = layoutChanged ? markLayoutStyleDirty(listView) : markPaintDirty(listView);
    if (!dirty)
    {
        return dirty;
    }
    state.paint = *normalized;
    detachThemeBinding(listView.index(), ThemeBindingListViewPaint);
    return Core::success();
}

[[nodiscard]] Core::Result<UIListViewPaint> UIContext::Impl::listViewPaintFromUpdater(UINodeId updaterRoot,
                                                                    UINodeId listView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
    {
        return Core::failure(valid.error());
    }
    return listViewStatesByNodeIndex[listView.index()].paint;
}

[[nodiscard]] Core::Result<UIListViewMetrics> UIContext::Impl::listViewMetricsFromUpdater(UINodeId updaterRoot,
                                                                        UINodeId listView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
    {
        return Core::failure(valid.error());
    }
    return listViewStatesByNodeIndex[listView.index()].committedMetrics;
}

[[nodiscard]] Core::Status UIContext::Impl::setListViewSelectedIndexFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                              u64 logicalIndex)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
    {
        return valid;
    }
    auto descriptor = resolveListViewLogicalItem(listView, logicalIndex);
    if (!descriptor)
    {
        return Core::failure(descriptor.error());
    }
    ListViewState& state = listViewStatesByNodeIndex[listView.index()];
    const UIListViewSelection next{.key = descriptor->key, .logicalIndex = logicalIndex};
    if (state.selection == next)
    {
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(listView); !dirty)
    {
        return dirty;
    }
    state.selection = next;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::clearListViewSelectionFromUpdater(UINodeId updaterRoot, UINodeId listView)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
    {
        return valid;
    }
    ListViewState& state = listViewStatesByNodeIndex[listView.index()];
    if (!state.selection.hasValue())
    {
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(listView); !dirty)
    {
        return dirty;
    }
    state.selection = {};
    return Core::success();
}

[[nodiscard]] Core::Result<UIListViewSelection> UIContext::Impl::listViewSelectionFromUpdater(UINodeId updaterRoot,
                                                                            UINodeId listView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
    {
        return Core::failure(valid.error());
    }
    return listViewStatesByNodeIndex[listView.index()].selection;
}

[[nodiscard]] Core::Status UIContext::Impl::scrollListViewToIndexFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                           u64 logicalIndex,
                                                           UIListViewScrollAlignment alignment)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
    {
        return valid;
    }
    if (!Detail::isValidListViewScrollAlignment(alignment))
    {
        return fail(UIErrorCode::InvalidControlValue, "UI ListView scroll alignment is not recognized");
    }
    auto descriptor = resolveListViewLogicalItem(listView, logicalIndex);
    if (!descriptor)
    {
        return Core::failure(descriptor.error());
    }
    ListViewState& state = listViewStatesByNodeIndex[listView.index()];
    const u64 logicalItemCount = state.dataSource.itemCount(state.dataSource.state);
    const auto nextOffset = resolveVirtualRowScrollOffset(
        logicalIndex, logicalItemCount, state.style.rowHeight,
        state.committedMetrics.viewportSize.height,
        state.requestedScrollOffset,
        Detail::toVirtualRowScrollAlignment(alignment));
    if (!nextOffset)
    {
        return fail(UIErrorCode::InvalidControlValue, "UI ListView logical content height is not representable");
    }
    if (*nextOffset == state.requestedScrollOffset)
    {
        return Core::success();
    }
    if (Core::Status dirty = markLayoutStyleDirty(listView); !dirty)
    {
        return dirty;
    }
    state.requestedScrollOffset = *nextOffset;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::validateVirtualGridViewUpdater(
    UINodeId updaterRoot, UINodeId virtualGridView) const
{
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired,
                    "UI tree updater requires a live root owner");
    }
    auto viewResult =
        const_cast<Impl*>(this)->resolveVirtualGridView(virtualGridView);
    if (!viewResult)
    {
        return Core::failure(viewResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, virtualGridView))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI VirtualGridView is not owned by the updater root");
    }
    return Core::success();
}

[[nodiscard]] Core::Result<UIVirtualGridViewItemDescriptor>
UIContext::Impl::resolveVirtualGridViewLogicalItem(
    UINodeId virtualGridView, u64 logicalIndex) const
{
    const VirtualGridViewState* state =
        virtualGridViewStorage.tryView(virtualGridView);
    if (state == nullptr || !state->dataSource.hasValue())
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI VirtualGridView has no data source");
    }
    const u64 itemCount =
        state->dataSource.itemCount(state->dataSource.state);
    if (logicalIndex >= itemCount)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI VirtualGridView logical item index is out of range");
    }
    UIVirtualGridViewItemDescriptor descriptor{};
    if (!state->dataSource.resolveItem(
            state->dataSource.state, logicalIndex, descriptor))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI VirtualGridView data source failed to resolve an item");
    }
    if (descriptor.key == InvalidUIVirtualGridViewItemKey ||
        containsLineBreak(descriptor.label) ||
        containsLineBreak(descriptor.presentation.secondaryLabel) ||
        containsLineBreak(descriptor.presentation.statusLabel))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI VirtualGridView item requires non-zero key and single-line labels");
    }
    if (!Core::isStrictUtf8WithoutNul(descriptor.label) ||
        !Core::isStrictUtf8WithoutNul(descriptor.presentation.secondaryLabel) ||
        !Core::isStrictUtf8WithoutNul(descriptor.presentation.statusLabel))
    {
        return fail(UIErrorCode::InvalidText,
                    "UI VirtualGridView item labels must be strict UTF-8 without NUL");
    }
    return descriptor;
}

[[nodiscard]] Core::Status UIContext::Impl::setVirtualGridViewDataSourceFromUpdater(
    UINodeId updaterRoot, UINodeId virtualGridView,
    UIVirtualGridViewDataSource source)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateVirtualGridViewUpdater(
            updaterRoot, virtualGridView);
        !valid)
    {
        return valid;
    }
    if (!source.hasValue())
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI VirtualGridView data source requires state, itemCount, and resolveItem");
    }
    if (Core::Status dirty = markLayoutStyleDirty(virtualGridView); !dirty)
    {
        return dirty;
    }
    virtualGridViewStorage.setDataSource(virtualGridView, source);
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::clearVirtualGridViewDataSourceFromUpdater(
    UINodeId updaterRoot, UINodeId virtualGridView)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateVirtualGridViewUpdater(
            updaterRoot, virtualGridView);
        !valid)
    {
        return valid;
    }
    VirtualGridViewState* state =
        virtualGridViewStorage.tryView(virtualGridView);
    if (state == nullptr || !state->dataSource.hasValue())
    {
        return Core::success();
    }
    if (Core::Status dirty = markLayoutStyleDirty(virtualGridView); !dirty)
    {
        return dirty;
    }
    virtualGridViewStorage.clearDataSource(virtualGridView);
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::invalidateVirtualGridViewItemsFromUpdater(
    UINodeId updaterRoot, UINodeId virtualGridView)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateVirtualGridViewUpdater(
            updaterRoot, virtualGridView);
        !valid)
    {
        return valid;
    }
    return markLayoutStyleDirty(virtualGridView);
}

[[nodiscard]] Core::Status UIContext::Impl::setVirtualGridViewStyleFromUpdater(
    UINodeId updaterRoot, UINodeId virtualGridView,
    const UIVirtualGridViewStyle& style)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateVirtualGridViewUpdater(
            updaterRoot, virtualGridView);
        !valid)
    {
        return valid;
    }
    auto normalized = normalizeVirtualGridViewStyle(style);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }
    VirtualGridViewState* state =
        virtualGridViewStorage.tryView(virtualGridView);
    if (state == nullptr)
    {
        return fail(Core::CoreErrorCode::Internal,
                    "UI VirtualGridView state is unavailable");
    }
    if (state->style == *normalized)
    {
        return Core::success();
    }
    if (Core::Status dirty = markLayoutStyleDirty(virtualGridView); !dirty)
    {
        return dirty;
    }
    state->style = *normalized;
    UINodeId item = state->firstMaterializedItem;
    for (u32 visited = 0;
         item.hasValue() && visited < state->linkedMaterializedItemCount;
         ++visited)
    {
        VirtualGridViewItemState* itemState =
            virtualGridViewStorage.tryItem(item);
        if (itemState == nullptr ||
            itemState->virtualGridView != virtualGridView)
        {
            break;
        }
        const UINodeId next = itemState->nextItem;
        configureCollectionRowLayout(
            layoutStylesByIndex[item.index()], state->style.itemHeight);
        textStatesByIndex[item.index()].overflow =
            state->style.itemTextOverflow;
        item = next;
    }
    return Core::success();
}

[[nodiscard]] Core::Result<UIVirtualGridViewStyle>
UIContext::Impl::virtualGridViewStyleFromUpdater(
    UINodeId updaterRoot, UINodeId virtualGridView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateVirtualGridViewUpdater(
            updaterRoot, virtualGridView);
        !valid)
    {
        return Core::failure(valid.error());
    }
    return virtualGridViewStorage.tryView(virtualGridView)->style;
}

[[nodiscard]] Core::Status UIContext::Impl::setVirtualGridViewPaintFromUpdater(
    UINodeId updaterRoot, UINodeId virtualGridView,
    const UIVirtualGridViewPaint& paint)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateVirtualGridViewUpdater(
            updaterRoot, virtualGridView);
        !valid)
    {
        return valid;
    }
    auto normalized = normalizeVirtualGridViewPaint(paint);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }
    VirtualGridViewState* state =
        virtualGridViewStorage.tryView(virtualGridView);
    if (state == nullptr)
    {
        return fail(Core::CoreErrorCode::Internal,
                    "UI VirtualGridView state is unavailable");
    }
    if (state->paint == *normalized)
    {
        detachThemeBinding(virtualGridView.index(), ThemeBindingGridPaint);
        return Core::success();
    }
    const bool layoutChanged =
        state->paint.scrollBar.thickness !=
            normalized->scrollBar.thickness ||
        state->paint.scrollBar.minThumbExtent !=
            normalized->scrollBar.minThumbExtent;
    Core::Status dirty = layoutChanged
                             ? markLayoutStyleDirty(virtualGridView)
                             : markPaintDirty(virtualGridView);
    if (!dirty)
    {
        return dirty;
    }
    state->paint = *normalized;
    detachThemeBinding(virtualGridView.index(), ThemeBindingGridPaint);
    return Core::success();
}

[[nodiscard]] Core::Result<UIVirtualGridViewPaint>
UIContext::Impl::virtualGridViewPaintFromUpdater(
    UINodeId updaterRoot, UINodeId virtualGridView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateVirtualGridViewUpdater(
            updaterRoot, virtualGridView);
        !valid)
    {
        return Core::failure(valid.error());
    }
    return virtualGridViewStorage.tryView(virtualGridView)->paint;
}

[[nodiscard]] Core::Result<UIVirtualGridViewMetrics>
UIContext::Impl::virtualGridViewMetricsFromUpdater(
    UINodeId updaterRoot, UINodeId virtualGridView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateVirtualGridViewUpdater(
            updaterRoot, virtualGridView);
        !valid)
    {
        return Core::failure(valid.error());
    }
    return virtualGridViewStorage.tryView(virtualGridView)
        ->committedMetrics;
}

[[nodiscard]] Core::Result<UINodeId>
UIContext::Impl::virtualGridViewMaterializedItemNodeFromUpdater(
    UINodeId updaterRoot, UINodeId virtualGridView,
    u64 logicalIndex) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateVirtualGridViewUpdater(
            updaterRoot, virtualGridView); !valid)
    {
        return Core::failure(valid.error());
    }
    const Detail::VirtualGridViewState* state =
        virtualGridViewStorage.tryView(virtualGridView);
    if (state == nullptr)
    {
        return UINodeId{};
    }
    const UIVirtualGridViewMetrics& metrics = state->committedMetrics;
    const bool isInCommittedWindow =
        logicalIndex >= metrics.firstMaterializedIndex &&
        logicalIndex - metrics.firstMaterializedIndex <
            static_cast<u64>(metrics.materializedItemCount);
    if (!isInCommittedWindow)
    {
        return UINodeId{};
    }
    for (u32 ordinal = 0; ordinal < state->linkedMaterializedItemCount;
         ++ordinal)
    {
        const UINodeId itemNode =
            virtualGridViewStorage.itemAt(virtualGridView, ordinal);
        const Detail::VirtualGridViewItemState* item =
            virtualGridViewStorage.tryItem(itemNode);
        if (item != nullptr && item->committedBound &&
            item->committedLogicalIndex == logicalIndex)
        {
            return itemNode;
        }
    }
    return UINodeId{};
}

[[nodiscard]] Core::Status UIContext::Impl::setVirtualGridViewSelectedIndexFromUpdater(
    UINodeId updaterRoot, UINodeId virtualGridView, u64 logicalIndex)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateVirtualGridViewUpdater(
            updaterRoot, virtualGridView);
        !valid)
    {
        return valid;
    }
    auto descriptor = resolveVirtualGridViewLogicalItem(
        virtualGridView, logicalIndex);
    if (!descriptor)
    {
        return Core::failure(descriptor.error());
    }
    VirtualGridViewState* state =
        virtualGridViewStorage.tryView(virtualGridView);
    const u32 columnCount = state->committedMetrics.logicalColumnCount;
    if (columnCount == 0)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI VirtualGridView selection requires a committed layout shape");
    }
    const UIVirtualGridViewSelection selection{
        .key = descriptor->key,
        .logicalIndex = logicalIndex,
        .logicalRow = logicalIndex / columnCount,
        .logicalColumn =
            static_cast<u32>(logicalIndex % columnCount),
    };
    if (state->selection == selection)
    {
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(virtualGridView); !dirty)
    {
        return dirty;
    }
    static_cast<void>(virtualGridViewStorage.setSelection(
        virtualGridView, selection));
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::clearVirtualGridViewSelectionFromUpdater(
    UINodeId updaterRoot, UINodeId virtualGridView)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateVirtualGridViewUpdater(
            updaterRoot, virtualGridView);
        !valid)
    {
        return valid;
    }
    const VirtualGridViewState* state =
        virtualGridViewStorage.tryView(virtualGridView);
    if (state == nullptr || !state->selection.hasValue())
    {
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(virtualGridView); !dirty)
    {
        return dirty;
    }
    static_cast<void>(
        virtualGridViewStorage.clearSelection(virtualGridView));
    return Core::success();
}

[[nodiscard]] Core::Result<UIVirtualGridViewSelection>
UIContext::Impl::virtualGridViewSelectionFromUpdater(
    UINodeId updaterRoot, UINodeId virtualGridView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateVirtualGridViewUpdater(
            updaterRoot, virtualGridView);
        !valid)
    {
        return Core::failure(valid.error());
    }
    return virtualGridViewStorage.tryView(virtualGridView)->selection;
}

[[nodiscard]] Core::Status UIContext::Impl::scrollVirtualGridViewToIndexFromUpdater(
    UINodeId updaterRoot, UINodeId virtualGridView, u64 logicalIndex,
    UIVirtualGridViewScrollAlignment alignment)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateVirtualGridViewUpdater(
            updaterRoot, virtualGridView);
        !valid)
    {
        return valid;
    }
    if (!Detail::isValidVirtualGridViewScrollAlignment(alignment))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI VirtualGridView scroll alignment is not recognized");
    }
    auto descriptor = resolveVirtualGridViewLogicalItem(
        virtualGridView, logicalIndex);
    if (!descriptor)
    {
        return Core::failure(descriptor.error());
    }
    VirtualGridViewState* state =
        virtualGridViewStorage.tryView(virtualGridView);
    const u32 columnCount = state->committedMetrics.logicalColumnCount;
    if (columnCount == 0)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI VirtualGridView scrolling requires a committed layout shape");
    }
    const u64 logicalRow = logicalIndex / columnCount;
    const double rowStride =
        static_cast<double>(state->style.itemHeight) +
        static_cast<double>(state->style.rowGap);
    const double itemStart =
        static_cast<double>(logicalRow) * rowStride;
    const double itemEnd =
        itemStart + static_cast<double>(state->style.itemHeight);
    const double viewportExtent =
        state->committedMetrics.viewportSize.height;
    const double current = state->requestedScrollOffset;
    double requested = current;
    switch (alignment)
    {
    case UIVirtualGridViewScrollAlignment::Nearest:
        if (itemStart < current)
        {
            requested = itemStart;
        }
        else if (itemEnd > current + viewportExtent)
        {
            requested = itemEnd - viewportExtent;
        }
        break;
    case UIVirtualGridViewScrollAlignment::Start:
        requested = itemStart;
        break;
    case UIVirtualGridViewScrollAlignment::Center:
        requested = itemStart -
                    (viewportExtent - state->style.itemHeight) * 0.5;
        break;
    case UIVirtualGridViewScrollAlignment::End:
        requested = itemEnd - viewportExtent;
        break;
    }
    if (!std::isfinite(requested))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI VirtualGridView logical scroll offset is not representable");
    }
    const float nextOffset = normalizeFloat(static_cast<float>(
        (std::clamp)(requested, 0.0,
                     static_cast<double>(
                         state->committedMetrics.maxScrollOffset))));
    if (nextOffset == state->requestedScrollOffset)
    {
        return Core::success();
    }
    if (Core::Status dirty = markLayoutStyleDirty(virtualGridView); !dirty)
    {
        return dirty;
    }
    state->requestedScrollOffset = nextOffset;
    return Core::success();
}

[[nodiscard]] Core::Result<UIDataGridSelection> UIContext::Impl::resolveDataGridSelection(
    UINodeId dataGrid, u64 logicalRow, u32 logicalColumn,
    bool* rowEnabled) const
{
    const DataGridState* state = dataGridStorage.tryGrid(dataGrid);
    if (state == nullptr || !state->dataSource.hasValue())
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI DataGrid has no data source");
    }
    const u64 rowCount = state->dataSource.rowCount(state->dataSource.state);
    const u32 columnCount =
        state->dataSource.columnCount(state->dataSource.state);
    if (logicalRow >= rowCount || logicalColumn >= columnCount ||
        columnCount > state->columnCapacity)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI DataGrid logical cell is out of range");
    }
    UIDataGridRowDescriptor row{};
    UIDataGridColumnDescriptor column{};
    if (!state->dataSource.resolveRow(
            state->dataSource.state, logicalRow, row) ||
        !state->dataSource.resolveColumn(
            state->dataSource.state, logicalColumn, column) ||
        row.key == InvalidUIDataGridRowKey ||
        column.key == InvalidUIDataGridColumnKey ||
        !std::isfinite(column.width) || column.width <= 0.0F)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI DataGrid failed to resolve a stable logical cell");
    }
    if (rowEnabled != nullptr)
    {
        *rowEnabled = row.enabled;
    }
    return UIDataGridSelection{
        .rowKey = row.key,
        .columnKey = column.key,
        .logicalRow = logicalRow,
        .logicalColumn = logicalColumn,
    };
}

[[nodiscard]] Core::Status UIContext::Impl::setDataGridDataSourceFromUpdater(
    UINodeId updaterRoot, UINodeId dataGrid, UIDataGridDataSource source)
{
    if (Core::Status owner = ensureOwnerThread(); !owner)
    {
        return owner;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateDataGridUpdater(updaterRoot, dataGrid);
        !valid)
    {
        return valid;
    }
    if (!source.hasValue())
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI DataGrid data source is incomplete");
    }
    const DataGridState* state = dataGridStorage.tryGrid(dataGrid);
    if (source.columnCount(source.state) > state->columnCapacity)
    {
        return fail(UIErrorCode::CapacityExceeded,
                    "UI DataGrid data source exceeds the fixed column pool");
    }
    if (Core::Status dirty = markLayoutStyleDirty(dataGrid); !dirty)
    {
        return dirty;
    }
    dataGridStorage.setDataSource(dataGrid, source);
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::clearDataGridDataSourceFromUpdater(
    UINodeId updaterRoot, UINodeId dataGrid)
{
    if (Core::Status owner = ensureOwnerThread(); !owner)
    {
        return owner;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateDataGridUpdater(updaterRoot, dataGrid);
        !valid)
    {
        return valid;
    }
    DataGridState* state = dataGridStorage.tryGrid(dataGrid);
    if (!state->dataSource.hasValue())
    {
        return Core::success();
    }
    if (Core::Status dirty = markLayoutStyleDirty(dataGrid); !dirty)
    {
        return dirty;
    }
    dataGridStorage.clearDataSource(dataGrid);
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::invalidateDataGridItemsFromUpdater(
    UINodeId updaterRoot, UINodeId dataGrid)
{
    if (Core::Status owner = ensureOwnerThread(); !owner)
    {
        return owner;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateDataGridUpdater(updaterRoot, dataGrid);
        !valid)
    {
        return valid;
    }
    return markLayoutStyleDirty(dataGrid);
}

[[nodiscard]] Core::Status UIContext::Impl::setDataGridStyleFromUpdater(
    UINodeId updaterRoot, UINodeId dataGrid,
    const UIDataGridStyle& style)
{
    if (Core::Status owner = ensureOwnerThread(); !owner)
    {
        return owner;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateDataGridUpdater(updaterRoot, dataGrid);
        !valid)
    {
        return valid;
    }
    auto normalized = normalizeDataGridStyle(style);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }
    DataGridState* state = dataGridStorage.tryGrid(dataGrid);
    if (state->style == *normalized)
    {
        return Core::success();
    }
    if (Core::Status dirty = markLayoutStyleDirty(dataGrid); !dirty)
    {
        return dirty;
    }
    state->style = *normalized;
    for (u32 column = 0; column < state->columnCapacity; ++column)
    {
        const UINodeId header = dataGridStorage.columnAt(dataGrid, column);
        configureCollectionRowLayout(
            layoutStylesByIndex[header.index()], state->style.columnHeaderHeight);
        textStatesByIndex[header.index()].overflow =
            state->style.headerTextOverflow;
    }
    for (u32 rowOrdinal = 0; rowOrdinal < state->materializedRowCapacity;
         ++rowOrdinal)
    {
        const UINodeId row = dataGridStorage.rowAt(dataGrid, rowOrdinal);
        configureCollectionRowLayout(
            layoutStylesByIndex[row.index()], state->style.rowHeight);
        for (u32 column = 0; column < state->columnCapacity; ++column)
        {
            const UINodeId cell =
                dataGridStorage.cellAt(dataGrid, rowOrdinal, column);
            configureCollectionRowLayout(
                layoutStylesByIndex[cell.index()], state->style.rowHeight);
            textStatesByIndex[cell.index()].overflow =
                state->style.cellTextOverflow;
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Result<UIDataGridStyle> UIContext::Impl::dataGridStyleFromUpdater(
    UINodeId updaterRoot, UINodeId dataGrid) const
{
    if (Core::Status owner = ensureOwnerThread(); !owner)
    {
        return Core::failure(owner.error());
    }
    if (Core::Status valid = validateDataGridUpdater(updaterRoot, dataGrid);
        !valid)
    {
        return Core::failure(valid.error());
    }
    return dataGridStorage.tryGrid(dataGrid)->style;
}

[[nodiscard]] Core::Status UIContext::Impl::setDataGridPaintFromUpdater(
    UINodeId updaterRoot, UINodeId dataGrid,
    const UIDataGridPaint& paint)
{
    if (Core::Status owner = ensureOwnerThread(); !owner)
    {
        return owner;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateDataGridUpdater(updaterRoot, dataGrid);
        !valid)
    {
        return valid;
    }
    auto normalized = normalizeDataGridPaint(paint);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }
    DataGridState* state = dataGridStorage.tryGrid(dataGrid);
    if (state->paint == *normalized)
    {
        detachThemeBinding(dataGrid.index(), ThemeBindingGridPaint);
        return Core::success();
    }
    const bool layoutChanged =
        state->paint.scrollBar.thickness != normalized->scrollBar.thickness ||
        state->paint.scrollBar.minThumbExtent != normalized->scrollBar.minThumbExtent;
    Core::Status dirty = layoutChanged ? markLayoutStyleDirty(dataGrid)
                                       : markPaintDirty(dataGrid);
    if (!dirty)
    {
        return dirty;
    }
    state->paint = *normalized;
    detachThemeBinding(dataGrid.index(), ThemeBindingGridPaint);
    return Core::success();
}

[[nodiscard]] Core::Result<UIDataGridPaint> UIContext::Impl::dataGridPaintFromUpdater(
    UINodeId updaterRoot, UINodeId dataGrid) const
{
    if (Core::Status owner = ensureOwnerThread(); !owner)
    {
        return Core::failure(owner.error());
    }
    if (Core::Status valid = validateDataGridUpdater(updaterRoot, dataGrid);
        !valid)
    {
        return Core::failure(valid.error());
    }
    return dataGridStorage.tryGrid(dataGrid)->paint;
}

[[nodiscard]] Core::Result<UIDataGridMetrics> UIContext::Impl::dataGridMetricsFromUpdater(
    UINodeId updaterRoot, UINodeId dataGrid) const
{
    if (Core::Status owner = ensureOwnerThread(); !owner)
    {
        return Core::failure(owner.error());
    }
    if (Core::Status valid = validateDataGridUpdater(updaterRoot, dataGrid);
        !valid)
    {
        return Core::failure(valid.error());
    }
    return dataGridStorage.tryGrid(dataGrid)->committedMetrics;
}

[[nodiscard]] Core::Status UIContext::Impl::setDataGridSelectedCellFromUpdater(
    UINodeId updaterRoot, UINodeId dataGrid, u64 logicalRow,
    u32 logicalColumn)
{
    if (Core::Status owner = ensureOwnerThread(); !owner)
    {
        return owner;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateDataGridUpdater(updaterRoot, dataGrid);
        !valid)
    {
        return valid;
    }
    auto selection = resolveDataGridSelection(
        dataGrid, logicalRow, logicalColumn);
    if (!selection)
    {
        return Core::failure(selection.error());
    }
    DataGridState* state = dataGridStorage.tryGrid(dataGrid);
    if (state->selection == *selection)
    {
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(dataGrid); !dirty)
    {
        return dirty;
    }
    static_cast<void>(dataGridStorage.setSelection(dataGrid, *selection));
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::clearDataGridSelectionFromUpdater(
    UINodeId updaterRoot, UINodeId dataGrid)
{
    if (Core::Status owner = ensureOwnerThread(); !owner)
    {
        return owner;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateDataGridUpdater(updaterRoot, dataGrid);
        !valid)
    {
        return valid;
    }
    DataGridState* state = dataGridStorage.tryGrid(dataGrid);
    if (!state->selection.hasValue())
    {
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(dataGrid); !dirty)
    {
        return dirty;
    }
    static_cast<void>(dataGridStorage.clearSelection(dataGrid));
    return Core::success();
}

[[nodiscard]] Core::Result<UIDataGridSelection>
UIContext::Impl::dataGridSelectionFromUpdater(
    UINodeId updaterRoot, UINodeId dataGrid) const
{
    if (Core::Status owner = ensureOwnerThread(); !owner)
    {
        return Core::failure(owner.error());
    }
    if (Core::Status valid = validateDataGridUpdater(updaterRoot, dataGrid);
        !valid)
    {
        return Core::failure(valid.error());
    }
    return dataGridStorage.tryGrid(dataGrid)->selection;
}

[[nodiscard]] Core::Status UIContext::Impl::scrollDataGridToCellFromUpdater(
    UINodeId updaterRoot, UINodeId dataGrid, u64 logicalRow,
    u32 logicalColumn, UIDataGridScrollAlignment alignment)
{
    if (Core::Status owner = ensureOwnerThread(); !owner)
    {
        return owner;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateDataGridUpdater(updaterRoot, dataGrid);
        !valid)
    {
        return valid;
    }
    if (!Detail::isValidDataGridScrollAlignment(alignment))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI DataGrid scroll alignment is not recognized");
    }
    auto selection = resolveDataGridSelection(
        dataGrid, logicalRow, logicalColumn);
    if (!selection)
    {
        return Core::failure(selection.error());
    }
    DataGridState* state = dataGridStorage.tryGrid(dataGrid);
    dataGridColumnWidthScratch.clear();
    const u32 columnCount =
        state->dataSource.columnCount(state->dataSource.state);
    double logicalContentWidth = 0.0;
    for (u32 column = 0; column < columnCount; ++column)
    {
        UIDataGridColumnDescriptor descriptor{};
        if (!state->dataSource.resolveColumn(
                state->dataSource.state, column, descriptor) ||
            descriptor.key == InvalidUIDataGridColumnKey ||
            !std::isfinite(descriptor.width) || descriptor.width <= 0.0F)
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI DataGrid column descriptor is invalid");
        }
        dataGridColumnWidthScratch.push_back(descriptor.width);
        logicalContentWidth += static_cast<double>(descriptor.width);
    }
    const u64 rowCount =
        state->dataSource.rowCount(state->dataSource.state);
    const double logicalContentHeight =
        static_cast<double>(rowCount) * state->style.rowHeight;
    const double maximumFloat =
        static_cast<double>((std::numeric_limits<float>::max)());
    if (!std::isfinite(logicalContentWidth) ||
        !std::isfinite(logicalContentHeight) ||
        logicalContentWidth > maximumFloat ||
        logicalContentHeight > maximumFloat)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI DataGrid logical content extent is not representable");
    }
    const double cellStartX = Detail::resolveDataGridColumnOffset(
        dataGridColumnWidthScratch, logicalColumn);
    const double cellEndX =
        cellStartX + dataGridColumnWidthScratch[logicalColumn];
    const double cellStartY =
        static_cast<double>(logicalRow) * state->style.rowHeight;
    const double cellEndY = cellStartY + state->style.rowHeight;
    const double maximumScrollOffsetX = (std::max)(
        0.0, logicalContentWidth -
                 state->committedMetrics.viewportSize.width);
    const double maximumScrollOffsetY = (std::max)(
        0.0, logicalContentHeight -
                 state->committedMetrics.viewportSize.height);
    const auto alignAxis = [alignment](double start, double end,
                                       double viewport,
                                       double current) noexcept {
        switch (alignment)
        {
        case UIDataGridScrollAlignment::Nearest:
            if (start < current)
            {
                return start;
            }
            if (end > current + viewport)
            {
                return end - viewport;
            }
            return current;
        case UIDataGridScrollAlignment::Start:
            return start;
        case UIDataGridScrollAlignment::Center:
            return start - (viewport - (end - start)) * 0.5;
        case UIDataGridScrollAlignment::End:
            return end - viewport;
        }
        return current;
    };
    const UIScrollOffset requested{
        .x = normalizeFloat(static_cast<float>((std::clamp)(
            alignAxis(cellStartX, cellEndX,
                      state->committedMetrics.viewportSize.width,
                      state->requestedScrollOffset.x),
            0.0, maximumScrollOffsetX))),
        .y = normalizeFloat(static_cast<float>((std::clamp)(
            alignAxis(cellStartY, cellEndY,
                      state->committedMetrics.viewportSize.height,
                      state->requestedScrollOffset.y),
            0.0, maximumScrollOffsetY))),
    };
    if (requested == state->requestedScrollOffset)
    {
        return Core::success();
    }
    if (Core::Status dirty = markLayoutStyleDirty(dataGrid); !dirty)
    {
        return dirty;
    }
    state->requestedScrollOffset = requested;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::validateTreeViewUpdater(UINodeId updaterRoot, UINodeId treeView) const
{
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    auto treeResult = const_cast<Impl*>(this)->resolveTreeView(treeView);
    if (!treeResult)
    {
        return Core::failure(treeResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, treeView))
    {
        return fail(UIErrorCode::InvalidNode, "UI TreeView is not owned by the updater root");
    }
    return Core::success();
}

[[nodiscard]] Core::Result<UITreeViewItemDescriptor> UIContext::Impl::resolveTreeViewLogicalItem(UINodeId treeView,
                                                                                u64 logicalIndex) const
{
    const TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
    if (!state.dataSource.hasValue())
    {
        return fail(UIErrorCode::InvalidControlValue, "UI TreeView has no data source");
    }
    const u64 itemCount = state.dataSource.itemCount(state.dataSource.state);
    if (logicalIndex >= itemCount)
    {
        return fail(UIErrorCode::InvalidControlValue, "UI TreeView logical item index is out of range");
    }
    UITreeViewItemDescriptor descriptor{};
    if (!state.dataSource.resolveItem(state.dataSource.state, logicalIndex, descriptor))
    {
        return fail(UIErrorCode::InvalidControlValue, "UI TreeView data source failed to resolve an item");
    }
    if (descriptor.key == InvalidUITreeViewItemKey || containsLineBreak(descriptor.label) ||
        (descriptor.expanded && !descriptor.expandable))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI TreeView item requires a non-zero key, a single-line label, and valid expansion state");
    }
    return descriptor;
}

[[nodiscard]] Core::Status UIContext::Impl::setTreeViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                            UITreeViewDataSource source)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
    {
        return valid;
    }
    if (!source.hasValue())
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI TreeView data source requires state, itemCount, and resolveItem");
    }
    if (Core::Status dirty = markLayoutStyleDirty(treeView); !dirty)
    {
        return dirty;
    }
    TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
    state.dataSource = source;
    state.selection = {};
    state.requestedScrollOffset = 0.0F;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::clearTreeViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId treeView)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
    {
        return valid;
    }
    TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
    if (!state.dataSource.hasValue())
    {
        return Core::success();
    }
    if (Core::Status dirty = markLayoutStyleDirty(treeView); !dirty)
    {
        return dirty;
    }
    state.dataSource = {};
    state.selection = {};
    state.requestedScrollOffset = 0.0F;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::invalidateTreeViewItemsFromUpdater(UINodeId updaterRoot, UINodeId treeView)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
    {
        return valid;
    }
    return markLayoutStyleDirty(treeView);
}

[[nodiscard]] Core::Status UIContext::Impl::setTreeViewStyleFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                       const UITreeViewStyle& style)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
    {
        return valid;
    }
    auto normalized = Detail::normalizeTreeViewStyle(style);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }
    TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
    if (state.style == *normalized)
    {
        return Core::success();
    }
    if (Core::Status dirty = markLayoutStyleDirty(treeView); !dirty)
    {
        return dirty;
    }
    state.style = *normalized;
    const NodeRecord* treeRecord = nodes.tryGet(treeView.storageId());
    u32 child = treeRecord == nullptr ? InvalidNodeIndex : treeRecord->firstChildIndex;
    while (child != InvalidNodeIndex)
    {
        NodeRecord* childRecord = recordByIndex(child);
        if (childRecord == nullptr)
        {
            break;
        }
        configureCollectionRowLayout(layoutStylesByIndex[child], state.style.rowHeight,
                                     layoutStylesByIndex[child].padding.left);
        child = childRecord->nextSiblingIndex;
    }
    return Core::success();
}

[[nodiscard]] Core::Result<UITreeViewStyle> UIContext::Impl::treeViewStyleFromUpdater(UINodeId updaterRoot, UINodeId treeView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
    {
        return Core::failure(valid.error());
    }
    return treeViewStatesByNodeIndex[treeView.index()].style;
}

[[nodiscard]] Core::Status UIContext::Impl::setTreeViewPaintFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                       const UITreeViewPaint& paint)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
    {
        return valid;
    }
    auto normalized = Detail::normalizeTreeViewPaint(paint);
    if (!normalized)
    {
        return Core::failure(normalized.error());
    }
    TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
    if (state.paint == *normalized)
    {
        detachThemeBinding(treeView.index(), ThemeBindingTreeViewPaint);
        return Core::success();
    }
    const bool layoutChanged = state.paint.scrollBar.thickness != normalized->scrollBar.thickness ||
                               state.paint.scrollBar.minThumbExtent != normalized->scrollBar.minThumbExtent;
    Core::Status dirty = layoutChanged ? markLayoutStyleDirty(treeView) : markPaintDirty(treeView);
    if (!dirty)
    {
        return dirty;
    }
    state.paint = *normalized;
    detachThemeBinding(treeView.index(), ThemeBindingTreeViewPaint);
    return Core::success();
}

[[nodiscard]] Core::Result<UITreeViewPaint> UIContext::Impl::treeViewPaintFromUpdater(UINodeId updaterRoot, UINodeId treeView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
    {
        return Core::failure(valid.error());
    }
    return treeViewStatesByNodeIndex[treeView.index()].paint;
}

[[nodiscard]] Core::Result<UITreeViewMetrics> UIContext::Impl::treeViewMetricsFromUpdater(UINodeId updaterRoot,
                                                                         UINodeId treeView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
    {
        return Core::failure(valid.error());
    }
    return treeViewStatesByNodeIndex[treeView.index()].committedMetrics;
}

[[nodiscard]] Core::Result<UINodeId> UIContext::Impl::treeViewMaterializedItemNodeFromUpdater(
    UINodeId updaterRoot, UINodeId treeView, u64 logicalIndex) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
    {
        return Core::failure(valid.error());
    }
    const UITreeViewMetrics& metrics =
        treeViewStatesByNodeIndex[treeView.index()].committedMetrics;
    const bool isInCommittedWindow =
        logicalIndex >= metrics.firstMaterializedIndex &&
        logicalIndex - metrics.firstMaterializedIndex <
            static_cast<u64>(metrics.materializedItemCount);
    if (isInCommittedWindow)
    {
        const NodeRecord* treeRecord = nodes.tryGet(treeView.storageId());
        u32 childIndex = treeRecord == nullptr ? InvalidNodeIndex : treeRecord->firstChildIndex;
        while (childIndex != InvalidNodeIndex)
        {
            const NodeRecord* childRecord = recordByIndex(childIndex);
            if (childRecord == nullptr)
            {
                break;
            }
            if (childIndex >= treeViewItemStatesByNodeIndex.size())
            {
                childIndex = childRecord->nextSiblingIndex;
                continue;
            }
            const TreeViewItemState& item = treeViewItemStatesByNodeIndex[childIndex];
            if (item.committedBound && item.committedLogicalIndex == logicalIndex)
            {
                return idForIndex(childIndex);
            }
            childIndex = childRecord->nextSiblingIndex;
        }
    }
    return UINodeId{};
}

[[nodiscard]] Core::Status UIContext::Impl::setTreeViewSelectedIndexFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                               u64 logicalIndex)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
    {
        return valid;
    }
    auto descriptor = resolveTreeViewLogicalItem(treeView, logicalIndex);
    if (!descriptor)
    {
        return Core::failure(descriptor.error());
    }
    TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
    const UITreeViewSelection next{
        .key = descriptor->key,
        .logicalIndex = logicalIndex,
        .level = descriptor->level,
    };
    if (state.selection == next)
    {
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(treeView); !dirty)
    {
        return dirty;
    }
    state.selection = next;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::clearTreeViewSelectionFromUpdater(UINodeId updaterRoot, UINodeId treeView)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
    {
        return valid;
    }
    TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
    if (!state.selection.hasValue())
    {
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(treeView); !dirty)
    {
        return dirty;
    }
    state.selection = {};
    return Core::success();
}

[[nodiscard]] Core::Result<UITreeViewSelection> UIContext::Impl::treeViewSelectionFromUpdater(UINodeId updaterRoot,
                                                                             UINodeId treeView) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
    {
        return Core::failure(valid.error());
    }
    return treeViewStatesByNodeIndex[treeView.index()].selection;
}

[[nodiscard]] Core::Status UIContext::Impl::setTreeViewItemExpandedFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                              u64 logicalIndex, bool expanded)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
    {
        return valid;
    }
    auto descriptor = resolveTreeViewLogicalItem(treeView, logicalIndex);
    if (!descriptor)
    {
        return Core::failure(descriptor.error());
    }
    if (!descriptor->expandable)
    {
        return fail(UIErrorCode::InvalidControlValue, "UI TreeView item is not expandable");
    }
    if (descriptor->expanded == expanded)
    {
        return Core::success();
    }
    TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
    if (!state.dataSource.canSetItemExpanded())
    {
        return fail(UIErrorCode::InvalidControlValue, "UI TreeView data source does not support expansion");
    }
    if (Core::Status dirty = markLayoutStyleDirty(treeView); !dirty)
    {
        return dirty;
    }
    if (!state.dataSource.setItemExpanded(state.dataSource.state, descriptor->key, expanded))
    {
        return fail(UIErrorCode::InvalidControlValue, "UI TreeView data source rejected expansion");
    }
    if (state.selection.key == descriptor->key)
    {
        state.selection.logicalIndex = logicalIndex;
        state.selection.level = descriptor->level;
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::scrollTreeViewToIndexFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                            u64 logicalIndex, UITreeViewScrollAlignment alignment)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
    {
        return valid;
    }
    if (!Detail::isValidTreeViewScrollAlignment(alignment))
    {
        return fail(UIErrorCode::InvalidControlValue, "UI TreeView scroll alignment is not recognized");
    }
    auto descriptor = resolveTreeViewLogicalItem(treeView, logicalIndex);
    if (!descriptor)
    {
        return Core::failure(descriptor.error());
    }
    TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
    const u64 logicalItemCount = state.dataSource.itemCount(state.dataSource.state);
    const auto nextOffset = resolveVirtualRowScrollOffset(
        logicalIndex, logicalItemCount, state.style.rowHeight,
        state.committedMetrics.viewportSize.height,
        state.requestedScrollOffset,
        Detail::toVirtualRowScrollAlignment(alignment));
    if (!nextOffset)
    {
        return fail(UIErrorCode::InvalidControlValue, "UI TreeView logical content height is not representable");
    }
    if (*nextOffset == state.requestedScrollOffset)
    {
        return Core::success();
    }
    if (Core::Status dirty = markLayoutStyleDirty(treeView); !dirty)
    {
        return dirty;
    }
    state.requestedScrollOffset = *nextOffset;
    return Core::success();
}

void UIContext::Impl::addDropdownActivationDirtyReservationCandidates(UINodeId control)
{
    const NodeRecord* record = contains(control) ? nodes.tryGet(control.storageId()) : nullptr;
    if (record == nullptr)
    {
        return;
    }
    if (record->kind == BuiltinElementKind::Dropdown)
    {
        const UINodeId popup = popupForDropdown(control);
        addRouteLayoutDirtyReservationCandidates(control);
        addRouteLayoutDirtyReservationCandidates(popup);
        if (popup.hasValue() && !popupStatesByNodeIndex[popup.index()].open && activePopupNode != popup)
        {
            addRouteLayoutDirtyReservationCandidates(activePopupNode);
            addRouteLayoutDirtyReservationCandidates(dropdownForPopup(activePopupNode));
        }
        return;
    }
    if (record->kind != BuiltinElementKind::DropdownItem)
    {
        return;
    }

    const UINodeId dropdown = dropdownForItem(control);
    const UINodeId popup = dropdown.hasValue() ? popupForDropdown(dropdown) : UINodeId{};
    addRouteLayoutDirtyReservationCandidates(dropdown);
    addRouteLayoutDirtyReservationCandidates(popup);
    addRouteLayoutDirtyReservationCandidates(control);
    if (dropdown.hasValue())
    {
        const Detail::UISelectBehaviorState* select = behaviorStateStorage.trySelectState(dropdown.index());
        addRouteLayoutDirtyReservationCandidates(select != nullptr ? select->selectedOption : UINodeId{});
    }
}

void UIContext::Impl::addMenuActivationDirtyReservationCandidates(UINodeId control)
{
    const MenuItemState* item = menuStorage.tryItem(control);
    if (item == nullptr)
    {
        return;
    }
    const UINodeId menu = menuStorage.menuForItem(control);
    addRouteDirtyReservationCandidate(control);
    addActiveMenuBranchDirtyReservationCandidates(menuStorage.rootMenu());
    if (item->config.kind == UIMenuItemKind::Submenu)
    {
        const UINodeId submenu = menuStorage.submenuForItem(control);
        addRouteLayoutDirtyReservationCandidates(submenu);
        addRouteLayoutDirtyReservationCandidates(
            menuPlacementAnchor(submenu));
    }
}

[[nodiscard]] bool UIContext::Impl::isSubmenuMenuItem(UINodeId item) const noexcept
{
    const MenuItemState* state = menuStorage.tryItem(item);
    return state != nullptr &&
           state->config.kind == UIMenuItemKind::Submenu;
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::activateMenuItem(UINodeId item)
{
    const NodeRecord* record = contains(item) ? nodes.tryGet(item.storageId()) : nullptr;
    MenuItemState* itemState = menuStorage.tryItem(item);
    if (record == nullptr || record->kind != BuiltinElementKind::MenuItem ||
        itemState == nullptr || itemState->config.kind == UIMenuItemKind::Separator ||
        !isNodeEnabled(item))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI MenuItem activation requires a live enabled non-separator item");
    }
    const UINodeId menu = menuStorage.menuForItem(item);
    const MenuState* menuState = menuStorage.tryMenu(menu);
    if (menuState == nullptr || !menuStorage.isOpen(menu))
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI MenuItem activation requires its Menu to be active");
    }

    const UIMenuItemKind kind = itemState->config.kind;
    const bool checked = itemState->checked;
    const bool closeOnActivate = menuState->config.closeOnActivate;
    if (kind == UIMenuItemKind::Submenu)
    {
        const UINodeId submenu = menuStorage.submenuForItem(item);
        if (!hasValidSubmenuRelationship(item, submenu))
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI Submenu MenuItem requires a live child Menu");
        }
        if (Core::Status opened = setMenuOpenState(submenu, true); !opened)
        {
            return Core::failure(opened.error());
        }
        return true;
    }
    if (kind == UIMenuItemKind::Check)
    {
        if (Core::Status changed = setMenuItemCheckedState(item, !checked); !changed)
        {
            return Core::failure(changed.error());
        }
    } else if (kind == UIMenuItemKind::Radio && !checked)
    {
        if (Core::Status changed = setMenuItemCheckedState(item, true); !changed)
        {
            return Core::failure(changed.error());
        }
    }
    if (closeOnActivate)
    {
        const UINodeId rootMenu = menuStorage.rootMenu();
        if (Core::Status closed = setMenuOpenState(rootMenu, false); !closed)
        {
            return Core::failure(closed.error());
        }
    }
    return true;
}

void UIContext::Impl::addTabActivationDirtyReservationCandidates(UINodeId tab)
{
    const UINodeId tabView = tabViewStorage.tabViewForTab(tab);
    if (!tabView.hasValue())
    {
        return;
    }
    addRouteLayoutDirtyReservationCandidates(tabView);
    addRouteLayoutDirtyReservationCandidates(tabViewStorage.activeTab(tabView));
    addRouteLayoutDirtyReservationCandidates(tabViewStorage.activePanel(tabView));
    addRouteLayoutDirtyReservationCandidates(tab);
    const TabState* next = tabViewStorage.tryTab(tab);
    addRouteLayoutDirtyReservationCandidates(next != nullptr ? next->panel : UINodeId{});
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::activateTabControl(UINodeId tab)
{
    const NodeRecord* record = contains(tab) ? nodes.tryGet(tab.storageId()) : nullptr;
    if (record == nullptr || record->kind != BuiltinElementKind::Tab || !isNodeEnabled(tab))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI Tab activation requires a live enabled Tab");
    }
    const UINodeId tabView = tabViewStorage.tabViewForTab(tab);
    if (!tabView.hasValue())
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Tab activation requires a valid TabView relationship");
    }
    const UINodeId previousTab = tabViewStorage.activeTab(tabView);
    if (previousTab == tab)
    {
        return false;
    }
    const UINodeId previousPanel = tabViewStorage.activePanel(tabView);
    const TabState* next = tabViewStorage.tryTab(tab);
    const UINodeId nextPanel = next != nullptr ? next->panel : UINodeId{};
    if (Core::Status dirty = markLayoutDirtyBatch(
            {tabView, previousTab, tab, previousPanel, nextPanel});
        !dirty)
    {
        return Core::failure(dirty.error());
    }
    if (!tabViewStorage.setActiveTab(tabView, tab))
    {
        return fail(Core::CoreErrorCode::Internal,
                    "UI TabView relationship rejected a validated activation");
    }
    return true;
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::activateDropdownControl(UINodeId control)
{
    const NodeRecord* record = contains(control) ? nodes.tryGet(control.storageId()) : nullptr;
    if (record == nullptr)
    {
        return fail(UIErrorCode::InvalidNode, "UI Dropdown activation target is stale");
    }
    if (record->kind == BuiltinElementKind::Dropdown)
    {
        const UINodeId popup = popupForDropdown(control);
        if (!popup.hasValue())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI Dropdown has no live Popup");
        }
        const bool nextOpen = !popupStatesByNodeIndex[popup.index()].open;
        if (Core::Status status = setPopupOpenState(popup, nextOpen); !status)
        {
            return Core::failure(status.error());
        }
        return true;
    }
    if (record->kind != BuiltinElementKind::DropdownItem)
    {
        return false;
    }

    const UINodeId dropdown = dropdownForItem(control);
    const UINodeId popup = dropdown.hasValue() ? popupForDropdown(dropdown) : UINodeId{};
    if (!dropdown.hasValue() || !popup.hasValue())
    {
        return fail(UIErrorCode::InvalidParent, "UI DropdownItem is detached from its Dropdown");
    }
    Detail::UISelectBehaviorState* select = behaviorStateStorage.trySelectState(dropdown.index());
    if (select == nullptr)
    {
        return fail(Core::CoreErrorCode::Internal, "UI Dropdown is missing Select behavior state");
    }
    PopupState& popupState = popupStatesByNodeIndex[popup.index()];
    const UINodeId previousItem = select->selectedOption;
    const bool changed = previousItem != control || popupState.open;
    if (!changed)
    {
        return false;
    }
    if (Core::Status dirty = markLayoutDirtyBatch({dropdown, popup, previousItem, control}); !dirty)
    {
        return Core::failure(dirty.error());
    }

    select->selectedOption = control;
    popupState.open = false;
    if (activePopupNode == popup)
    {
        activePopupNode = {};
    }
    transientOverlayDismissPointerBarrierActive = false;
    defaultActionPressState.clearAll();
    resetTextEditPreferredX(textInputFocus);
    resetImeCompositionState();
    textInputFocus = {};
    defaultActionFocusButton = dropdown;
    return true;
}

} // namespace Tina::UI
