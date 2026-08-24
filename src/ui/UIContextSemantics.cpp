#include "detail/UIContextImpl.hpp"

namespace Tina::UI {

[[nodiscard]] bool UIContext::Impl::resolveSemanticsSnapshotSource(
    UINodeId node, Detail::UISemanticsSnapshotSource& source,
    bool useCandidateVirtualGridPresentation) noexcept
{
    const u32 nodeIndex = node.index();
    const NodeRecord* record = recordByIndex(nodeIndex);
    if (record == nullptr || nodeIndex >= semanticsStatesByNodeIndex.size())
    {
        return false;
    }

    const SemanticsState& state = semanticsStatesByNodeIndex[nodeIndex];
    source = {};
    source.parentNodeIndex = record->parentIndex;
    source.mode = state.mode;
    source.entry = UISemanticsEntry{
        .node = node,
        .role = state.role,
        .actions = state.actions,
        .liveSetting = state.liveSetting,
        .enabled = isCandidateNodeEnabled(node),
        .readOnly = state.readOnly,
    };
    UISemanticsEntry& entry = source.entry;
    const bool enabled = entry.enabled;
    entry.focused = enabled && defaultActionFocusButton == node;
    if (const u8* toggleValue = behaviorStateStorage.tryToggleValue(nodeIndex);
        toggleValue != nullptr)
    {
        entry.checked = *toggleValue != 0;
    }
    if (const Detail::UIRangeInputState* range = behaviorStateStorage.tryRangeInputState(nodeIndex);
        range != nullptr)
    {
        entry.hasRange = true;
        entry.minValue = range->minValue;
        entry.maxValue = range->maxValue;
        entry.value = range->value;
    }
    if (record->kind == BuiltinElementKind::Dropdown && nodeIndex < dropdownStatesByNodeIndex.size())
    {
        entry.focused = enabled && defaultActionFocusButton == node;
    } else if (record->kind == BuiltinElementKind::Tab &&
               tabViewStorage.tabViewForTab(node).hasValue())
    {
        const UINodeId owner = tabViewStorage.tabViewForTab(node);
        entry.selected = tabViewStorage.activeTab(owner) == node;
        entry.focused = enabled && defaultActionFocusButton == node;
    } else if (tabViewStorage.tabViewForPanel(node).hasValue())
    {
        // Association promotes an ordinary retained content node to the
        // TabPanel semantic role without creating a second node/state.
        entry.role = UISemanticsRole::TabPanel;
    } else if (record->kind == BuiltinElementKind::DropdownItem)
    {
        entry.selected = isSelectedDropdownItem(node);
    } else if (record->kind == BuiltinElementKind::MenuItem)
    {
        const MenuItemState* item = menuStorage.tryItem(node);
        entry.checked = item != nullptr && item->checked;
        entry.focused = enabled && defaultActionFocusButton == node;
    } else if (record->kind == BuiltinElementKind::TextEdit)
    {
        entry.focused = enabled && textInputFocus == node;
    } else if (record->kind == BuiltinElementKind::ProgressBar &&
               nodeIndex < progressBarStatesByNodeIndex.size())
    {
        const ProgressBarState& progress = progressBarStatesByNodeIndex[nodeIndex];
        entry.hasRange = true;
        entry.minValue = progress.minValue;
        entry.maxValue = progress.maxValue;
        entry.value = progress.value;
    } else if (record->kind == BuiltinElementKind::RadioButton &&
               nodeIndex < radioButtonStatesByNodeIndex.size())
    {
        entry.checked = radioButtonStatesByNodeIndex[nodeIndex].selected;
    } else if (record->kind == BuiltinElementKind::ScrollView &&
               nodeIndex < scrollViewLayoutScratchByNodeIndex.size())
    {
        const UIScrollViewMetrics& metrics = scrollViewLayoutScratchByNodeIndex[nodeIndex].metrics;
        const UIScrollBehaviorState* state = behaviorStateStorage.tryScrollState(nodeIndex);
        if (state == nullptr)
        {
            return false;
        }
        const bool vertical = hasScrollAxis(state->style.axes, UIScrollAxes::Vertical);
        entry.focused = enabled && scrollThumbDragActive && armedScrollView == node;
        entry.hasRange = true;
        entry.maxValue = vertical ? metrics.maxOffsetY() : metrics.maxOffsetX();
        entry.value = vertical ? metrics.offset.y : metrics.offset.x;
    } else if (record->kind == BuiltinElementKind::ListView &&
               nodeIndex < listViewLayoutScratchByNodeIndex.size())
    {
        const UIListViewMetrics& metrics = listViewLayoutScratchByNodeIndex[nodeIndex].metrics;
        entry.hasRange = true;
        entry.maxValue = metrics.maxScrollOffset;
        entry.value = metrics.scrollOffset;
    } else if (record->kind == BuiltinElementKind::ListViewItem &&
               nodeIndex < listViewItemStatesByNodeIndex.size())
    {
        const ListViewItemState& item = listViewItemStatesByNodeIndex[nodeIndex];
        entry.virtualItemKey = item.key;
        entry.virtualItemIndex = item.logicalIndex;
        entry.selected = item.bound && isSelectedListViewItem(node);
        entry.focused = entry.enabled && entry.selected && defaultActionFocusButton == listViewForItem(node);
    } else if (record->kind == BuiltinElementKind::VirtualGridView)
    {
        const VirtualGridViewLayoutScratch* layout =
            virtualGridViewStorage.tryLayoutScratch(node);
        if (layout != nullptr)
        {
            entry.hasRange = true;
            entry.maxValue = layout->metrics.maxScrollOffset;
            entry.value = layout->metrics.scrollOffset;
        }
    } else if (record->kind == BuiltinElementKind::VirtualGridViewItem)
    {
        const VirtualGridViewItemState* item =
            virtualGridViewStorage.tryItem(node);
        if (item != nullptr)
        {
            entry.virtualItemKey = item->key;
            entry.virtualItemIndex = item->logicalIndex;
            entry.selected = item->bound &&
                             isSelectedVirtualGridViewItem(node);
            entry.focused = entry.enabled && entry.selected &&
                            defaultActionFocusButton ==
                                virtualGridViewForItem(node);
        }
    } else if (record->kind == BuiltinElementKind::DataGrid)
    {
        const DataGridLayoutScratch* layout =
            dataGridStorage.tryLayoutScratch(node);
        if (layout != nullptr)
        {
            const bool publishVertical =
                layout->metrics.maxScrollOffset.y > 0.0F ||
                layout->metrics.maxScrollOffset.x <= 0.0F;
            entry.hasRange = true;
            entry.maxValue = publishVertical
                                 ? layout->metrics.maxScrollOffset.y
                                 : layout->metrics.maxScrollOffset.x;
            entry.value = publishVertical
                              ? layout->metrics.scrollOffset.y
                              : layout->metrics.scrollOffset.x;
        }
    } else if (record->kind == BuiltinElementKind::DataGridCell)
    {
        const DataGridCellState* cell = dataGridStorage.tryCell(node);
        const DataGridRowState* row = cell != nullptr
                                          ? dataGridStorage.tryRow(cell->row)
                                          : nullptr;
        if (cell != nullptr && row != nullptr)
        {
            entry.virtualItemKey = row->key;
            entry.virtualItemIndex = row->logicalRow;
            entry.selected = cell->bound && row->bound &&
                             isSelectedDataGridCell(node);
            entry.focused = entry.enabled && entry.selected &&
                            defaultActionFocusButton ==
                                dataGridForCell(node);
        }
    } else if (record->kind == BuiltinElementKind::TreeView &&
               nodeIndex < treeViewLayoutScratchByNodeIndex.size())
    {
        const UITreeViewMetrics& metrics = treeViewLayoutScratchByNodeIndex[nodeIndex].metrics;
        entry.hasRange = true;
        entry.maxValue = metrics.maxScrollOffset;
        entry.value = metrics.scrollOffset;
    } else if (record->kind == BuiltinElementKind::TreeViewItem &&
               nodeIndex < treeViewItemStatesByNodeIndex.size())
    {
        const TreeViewItemState& item = treeViewItemStatesByNodeIndex[nodeIndex];
        entry.virtualItemKey = item.key;
        entry.virtualItemIndex = item.logicalIndex;
        entry.level = item.level;
        entry.expandable = item.bound && item.expandable;
        entry.expanded = item.bound && item.expanded;
        entry.selected = item.bound && isSelectedTreeViewItem(node);
        entry.focused = entry.enabled && entry.selected && defaultActionFocusButton == treeViewForItem(node);
    }

    source.name = semanticsNameSourceFor(nodeIndex);
    source.description = semanticsDescriptionViewFor(nodeIndex);
    if (!state.hasExplicitDescription)
    {
        const UINodeId tooltip = tooltipStorage.tooltipForAnchor(node);
        if (hasValidTooltipRelationship(tooltip, node))
        {
            source.description = textViewFor(tooltip.index());
        }
    }
    if (record->kind == BuiltinElementKind::VirtualGridViewItem)
    {
        const VirtualGridViewItemState* item =
            virtualGridViewStorage.tryItem(node);
        if (item != nullptr)
        {
            const UIVirtualGridViewItemPresentation& presentation =
                useCandidateVirtualGridPresentation
                    ? item->presentation
                    : item->committedPresentation;
            if (!presentation.statusLabel.empty())
            {
                source.description = presentation.statusLabel;
            }
            if (!presentation.secondaryLabel.empty())
            {
                source.valueText = presentation.secondaryLabel;
            }
        }
    }
    if (record->kind == BuiltinElementKind::Dropdown && nodeIndex < dropdownStatesByNodeIndex.size())
    {
        const Detail::UISelectBehaviorState* select = behaviorStateStorage.trySelectState(nodeIndex);
        const UINodeId selected = select != nullptr ? select->selectedOption : UINodeId{};
        source.valueText = contains(selected) ? textViewFor(selected.index()) : std::string_view{};
    } else if (record->kind == BuiltinElementKind::TextEdit)
    {
        source.valueText = textViewFor(nodeIndex);
    }
    return true;
}

[[nodiscard]] Core::Status UIContext::Impl::buildCommittedSemantics(std::pmr::vector<UISemanticsEntry>& output,
                                                   std::pmr::vector<char>& textOutput,
                                                   std::span<const UICommittedLayoutEntry> layoutEntries,
                                                   bool useCandidateVirtualGridPresentation)
{
    struct SourceContext final {
        Impl* impl = nullptr;
        bool useCandidateVirtualGridPresentation = false;
    } sourceContext{
        .impl = this,
        .useCandidateVirtualGridPresentation =
            useCandidateVirtualGridPresentation,
    };
    return semanticsSnapshotBuilder.build(
        output, textOutput, layoutEntries,
        Detail::UISemanticsSnapshotSourceAdapter{
            .context = &sourceContext,
            .resolve = [](void* context, UINodeId node,
                          Detail::UISemanticsSnapshotSource& source) noexcept {
                auto& snapshot = *static_cast<SourceContext*>(context);
                return snapshot.impl->resolveSemanticsSnapshotSource(
                    node, source,
                    snapshot.useCandidateVirtualGridPresentation);
            },
        });
}

[[nodiscard]] Core::Status UIContext::Impl::validateLayoutCandidate(const std::pmr::vector<u32>& order) const
{
    for (const u32 index : order)
    {
        const LayoutScratchState& scratch = layoutScratchByIndex[index];
        if (!isFiniteNonNegative(scratch.measuredSize.width) || !isFiniteNonNegative(scratch.measuredSize.height) ||
            !isFiniteLayoutRect(scratch.localRect) || !isFiniteLayoutRect(scratch.worldRect) ||
            !isFiniteLayoutRect(scratch.effectiveClip) || !isFiniteLayoutRect(scratch.descendantClip))
        {
            return fail(UIErrorCode::InvalidLayout, "UI layout arithmetic produced non-finite geometry");
        }
    }
    return Core::success();
}

} // namespace Tina::UI
