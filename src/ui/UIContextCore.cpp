#include "detail/UIContextImpl.hpp"

namespace Tina::UI {

[[nodiscard]] bool UIContext::Impl::isPhaseDirty(UIDirty flags) const noexcept
{
    return hasDirty(phaseDirty, flags);
}

[[nodiscard]] Core::Status UIContext::Impl::publishStructureIfDirty()
{
    if (!isPhaseDirty(PhaseStructure))
    {
        return Core::success();
    }

    const usize writeBufferIndex = 1 - publishedBufferIndex;
    buildCommittedStructure(committedBuffers[writeBufferIndex]);
    publishedBufferIndex = writeBufferIndex;
    ++committedRevision;
    phaseDirty = clearDirty(phaseDirty, PhaseStructure);
    return Core::success();
}

[[nodiscard]] bool UIContext::Impl::isOwnerThread() const noexcept
{
    return std::this_thread::get_id() == ownerThreadId;
}

[[nodiscard]] Core::Status UIContext::Impl::ensureOwnerThread() const
{
    if (!isOwnerThread())
    {
        return fail(UIErrorCode::WrongOwnerThread, "UI context was accessed from a non-owner thread");
    }
    return Core::success();
}

void UIContext::Impl::publishRoutedPointerListenerTokenState(u32 slot, u32 generation, bool active) noexcept
{
    if (lifetime)
    {
        lifetime->publishRoutedPointerListenerState(slot, generation, active);
    }
}

void UIContext::Impl::publishRoutedPointerListenerTokenStateFromRegistry(void* context, u32 slot,
                                                               u32 generation, bool active) noexcept
{
    if (context != nullptr)
    {
        static_cast<Impl*>(context)->publishRoutedPointerListenerTokenState(slot, generation, active);
    }
}

[[nodiscard]] Detail::UIRoutedPointerListenerStatePublisher
UIContext::Impl::routedPointerListenerStatePublisher() noexcept
{
    return Detail::UIRoutedPointerListenerStatePublisher{
        .context = this,
        .publish = &Impl::publishRoutedPointerListenerTokenStateFromRegistry,
    };
}

void UIContext::Impl::reclaimInactiveRoutedPointerListeners() noexcept
{
    routedPointerListenerRegistry.reclaim(routeDispatchDepth != 0);
}

void UIContext::Impl::deactivateRoutedPointerListener(u32 listenerIndex, u32 generation,
                                     bool publishTokenState) noexcept
{
    const auto statePublisher = publishTokenState ? routedPointerListenerStatePublisher()
                                                  : Detail::UIRoutedPointerListenerStatePublisher{};
    static_cast<void>(routedPointerListenerRegistry.deactivate(
        listenerIndex, generation, statePublisher, routeDispatchDepth != 0));
}

void UIContext::Impl::deactivateAllRoutedPointerListenersForNode(u32 nodeIndex) noexcept
{
    routedPointerListenerRegistry.clearNode(nodeIndex, routedPointerListenerStatePublisher());
}

void UIContext::Impl::drainDeferredRoutedPointerListenerReleases() noexcept
{
    if (!isOwnerThread() || !lifetime)
    {
        return;
    }
    lifetime->takeDeferredRoutedPointerListenerReleases(
        deferredRoutedPointerListenerReleaseBuffer);
    for (const Detail::DeferredRoutedPointerListenerRelease release : deferredRoutedPointerListenerReleaseBuffer)
    {
        deactivateRoutedPointerListener(release.slot, release.generation, false);
    }
    deferredRoutedPointerListenerReleaseBuffer.clear();
    reclaimInactiveRoutedPointerListeners();
}

void UIContext::Impl::drainDeferredRootDestroys() noexcept
{
    if (!isOwnerThread() || !lifetime)
    {
        return;
    }

    lifetime->takeDeferredRootDestroys(deferredRootDestroyBuffer);
    for (const UINodeId root : deferredRootDestroyBuffer)
    {
        destroyRootImmediately(root);
    }
    deferredRootDestroyBuffer.clear();
    drainDeferredRoutedPointerListenerReleases();
}

[[nodiscard]] UINodeId UIContext::Impl::idForIndex(u32 index) const noexcept
{
    if (index == InvalidNodeIndex || index >= idsByIndex.size())
    {
        return {};
    }
    return idsByIndex[index];
}

[[nodiscard]] NodeRecord* UIContext::Impl::recordByIndex(u32 index) noexcept
{
    return nodes.tryGet(idForIndex(index).storageId());
}

[[nodiscard]] const NodeRecord* UIContext::Impl::recordByIndex(u32 index) const noexcept
{
    return nodes.tryGet(idForIndex(index).storageId());
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveNode(UINodeId node)
{
    if (!node.hasValue())
    {
        return fail(UIErrorCode::InvalidNode, "UI node id is empty");
    }
    if (node.ownerWindow() != ownerWindow)
    {
        return fail(UIErrorCode::WrongOwnerWindow, "UI node belongs to another owner window");
    }
    if (node.storageId().owner() != nodes.owner())
    {
        return fail(UIErrorCode::WrongContext, "UI node belongs to another context");
    }
    NodeRecord* record = nodes.tryGet(node.storageId());
    if (record == nullptr)
    {
        return fail(UIErrorCode::InvalidNode, "UI node is stale or out of range");
    }
    return record;
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveParent(UINodeId parent)
{
    if (!parent.hasValue())
    {
        return fail(UIErrorCode::InvalidParent, "UI parent id is empty");
    }
    if (parent.ownerWindow() != ownerWindow)
    {
        return fail(UIErrorCode::WrongOwnerWindow, "UI parent belongs to another owner window");
    }
    if (parent.storageId().owner() != nodes.owner())
    {
        return fail(UIErrorCode::WrongContext, "UI parent belongs to another context");
    }
    NodeRecord* record = nodes.tryGet(parent.storageId());
    if (record == nullptr)
    {
        return fail(UIErrorCode::InvalidParent, "UI parent is stale or out of range");
    }
    return record;
}

[[nodiscard]] bool UIContext::Impl::contains(UINodeId node) const noexcept
{
    return node.hasValue() && node.ownerWindow() == ownerWindow && node.storageId().owner() == nodes.owner() &&
           nodes.contains(node.storageId());
}

[[nodiscard]] bool UIContext::Impl::isNodeEnabled(UINodeId node) const noexcept
{
    if (!contains(node) || node.index() >= enabledByNodeIndex.size() || enabledByNodeIndex[node.index()] == 0)
    {
        return false;
    }
    const NodeRecord* record = nodes.tryGet(node.storageId());
    if (record != nullptr && record->kind == BuiltinElementKind::ListViewItem &&
        node.index() < listViewItemStatesByNodeIndex.size())
    {
        const ListViewItemState& item = listViewItemStatesByNodeIndex[node.index()];
        return item.committedBound && item.committedEnabled &&
               record->parentIndex != InvalidNodeIndex && isNodeEnabled(idForIndex(record->parentIndex));
    }
    if (record != nullptr && record->kind == BuiltinElementKind::TreeViewItem &&
        node.index() < treeViewItemStatesByNodeIndex.size())
    {
        const TreeViewItemState& item = treeViewItemStatesByNodeIndex[node.index()];
        return item.committedBound && item.committedEnabled &&
               record->parentIndex != InvalidNodeIndex && isNodeEnabled(idForIndex(record->parentIndex));
    }
    if (record != nullptr &&
        record->kind == BuiltinElementKind::VirtualGridViewItem)
    {
        const VirtualGridViewItemState* item =
            virtualGridViewStorage.tryItem(node);
        return item != nullptr && item->committedBound &&
               item->committedEnabled &&
               record->parentIndex != InvalidNodeIndex &&
               isNodeEnabled(idForIndex(record->parentIndex));
    }
    if (record != nullptr && record->kind == BuiltinElementKind::DataGridCell)
    {
        const DataGridCellState* cell = dataGridStorage.tryCell(node);
        const DataGridRowState* row =
            cell != nullptr ? dataGridStorage.tryRow(cell->row) : nullptr;
        return cell != nullptr && cell->committedBound && row != nullptr &&
               row->committedBound && row->committedEnabled &&
               isNodeEnabled(cell->dataGrid);
    }
    return true;
}

[[nodiscard]] bool UIContext::Impl::isCandidateNodeEnabled(UINodeId node) const noexcept
{
    if (!contains(node) || node.index() >= enabledByNodeIndex.size() || enabledByNodeIndex[node.index()] == 0)
    {
        return false;
    }
    const NodeRecord* record = nodes.tryGet(node.storageId());
    if (record != nullptr && record->kind == BuiltinElementKind::ListViewItem &&
        node.index() < listViewItemStatesByNodeIndex.size())
    {
        const ListViewItemState& item = listViewItemStatesByNodeIndex[node.index()];
        return item.bound && item.enabled && record->parentIndex != InvalidNodeIndex &&
               isCandidateNodeEnabled(idForIndex(record->parentIndex));
    }
    if (record != nullptr && record->kind == BuiltinElementKind::TreeViewItem &&
        node.index() < treeViewItemStatesByNodeIndex.size())
    {
        const TreeViewItemState& item = treeViewItemStatesByNodeIndex[node.index()];
        return item.bound && item.enabled && record->parentIndex != InvalidNodeIndex &&
               isCandidateNodeEnabled(idForIndex(record->parentIndex));
    }
    if (record != nullptr &&
        record->kind == BuiltinElementKind::VirtualGridViewItem)
    {
        const VirtualGridViewItemState* item =
            virtualGridViewStorage.tryItem(node);
        return item != nullptr && item->bound && item->enabled &&
               record->parentIndex != InvalidNodeIndex &&
               isCandidateNodeEnabled(idForIndex(record->parentIndex));
    }
    if (record != nullptr && record->kind == BuiltinElementKind::DataGridCell)
    {
        const DataGridCellState* cell = dataGridStorage.tryCell(node);
        const DataGridRowState* row =
            cell != nullptr ? dataGridStorage.tryRow(cell->row) : nullptr;
        return cell != nullptr && cell->bound && row != nullptr &&
               row->bound && row->enabled &&
               isCandidateNodeEnabled(cell->dataGrid);
    }
    return true;
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveButton(UINodeId button)
{
    auto nodeResult = resolveNode(button);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (!behaviorStateStorage.hasActivate(button.index()) ||
        (*nodeResult)->kind == BuiltinElementKind::ListViewItem ||
        (*nodeResult)->kind == BuiltinElementKind::TreeViewItem ||
        (*nodeResult)->kind == BuiltinElementKind::VirtualGridViewItem ||
        (*nodeResult)->kind == BuiltinElementKind::DataGridCell)
    {
        return fail(UIErrorCode::InvalidButtonAction,
                    "UI action requires an Activate-capable non-virtual Element");
    }
    return *nodeResult;
}

[[nodiscard]] bool UIContext::Impl::isButtonPressed(UINodeId node) const noexcept
{
    return (armedPrimaryButton == node && armedPrimaryButtonPressed) ||
           defaultActionPressState.isPressed(node);
}

void UIContext::Impl::clearArmedPrimaryButton() noexcept
{
    armedPrimaryButton = {};
    armedPrimaryButtonPressed = false;
    armedTreeDisclosure = false;
}

void UIContext::Impl::clearHoveredPrimaryControl() noexcept
{
    hoveredPrimaryControl = {};
}

[[nodiscard]] UINodeId UIContext::Impl::resolvedHoveredPrimaryControl(UINodeId candidate) const noexcept
{
    if (candidate.hasValue() && isNodeEnabled(candidate))
    {
        const NodeRecord* record = nodes.tryGet(candidate.storageId());
        if (record != nullptr &&
            (isButtonChromeKind(record->kind) || record->kind == BuiltinElementKind::Checkbox ||
             record->kind == BuiltinElementKind::RadioButton || record->kind == BuiltinElementKind::TextEdit ||
             record->kind == BuiltinElementKind::Splitter))
        {
            return candidate;
        }
    }
    return {};
}

[[nodiscard]] Core::Status UIContext::Impl::updateHoveredPrimaryControl(UINodeId candidate)
{
    const UINodeId nextHover = resolvedHoveredPrimaryControl(candidate);
    if (nextHover == hoveredPrimaryControl)
    {
        return Core::success();
    }
    const UINodeId previousHover =
        hoveredPrimaryControl.hasValue() && contains(hoveredPrimaryControl) ? hoveredPrimaryControl : UINodeId{};
    if (Core::Status dirty = markPaintDirtyBatch({previousHover, nextHover}); !dirty)
    {
        return dirty;
    }
    hoveredPrimaryControl = nextHover;
    return Core::success();
}

void UIContext::Impl::clearArmedSlider() noexcept
{
    armedSlider = {};
    splitterDragGrabOffset = 0.0F;
}

void UIContext::Impl::clearArmedScrollView() noexcept
{
    armedScrollView = {};
    armedScrollAxis = UIScrollAxes::None;
    scrollDragGrabOffset = 0.0F;
    scrollThumbDragActive = false;
}

void UIContext::Impl::clearArmedTextEdit() noexcept
{
    armedTextEdit = {};
}

void UIContext::Impl::clearDefaultActionFocus() noexcept
{
    defaultActionFocusButton = {};
    defaultActionPressState.clearAll();
}

void UIContext::Impl::resetImeCompositionState() noexcept
{
    imeComposition.reset();
}

[[nodiscard]] Core::Status UIContext::Impl::clearImeComposition()
{
    const bool wasActive = imeComposition.active();
    const UINodeId focus = textInputFocus;
    if (wasActive && focus.hasValue() && contains(focus))
    {
        if (Core::Status paintStatus = markPaintDirty(focus); !paintStatus)
        {
            return paintStatus;
        }
    }
    resetImeCompositionState();
    return Core::success();
}

void UIContext::Impl::resetTextEditPreferredX(UINodeId node) noexcept
{
    if (node.hasValue() && node.index() < textEditPreferredXByNodeIndex.size())
    {
        textEditPreferredXByNodeIndex[node.index()].reset();
    }
}

void UIContext::Impl::clearImeFocus() noexcept
{
    const UINodeId previousFocus = textInputFocus;
    if (Core::Status status = clearImeComposition(); !status)
    {
        // Focus cancellation must win even when another dirty node has
        // exhausted the queue. A pending paint commit will observe this
        // reset state.
        resetImeCompositionState();
    }
    textInputFocus = {};
    resetTextEditPreferredX(previousFocus);
    if (defaultActionFocusButton == previousFocus)
    {
        clearDefaultActionFocus();
    }
    if (previousFocus.hasValue() && contains(previousFocus))
    {
        static_cast<void>(markPaintDirty(previousFocus));
    }
}

[[nodiscard]] bool UIContext::Impl::isLiveTextEdit(UINodeId node) const noexcept
{
    if (!node.hasValue() || !contains(node))
    {
        return false;
    }
    const NodeRecord* record = nodes.tryGet(node.storageId());
    return record != nullptr && record->kind == BuiltinElementKind::TextEdit;
}

[[nodiscard]] bool UIContext::Impl::isPointerInteractionCandidate(UINodeId node, std::span<const UICommittedHitEntry> entries,
                                                 u32 activeModalEntryIndex) const noexcept
{
    if (!node.hasValue() || !isNodeEnabled(node))
    {
        return false;
    }
    const u32 entryIndex = findHitEntryIndex(node, entries);
    return Detail::hitEntryAllowsPointerInteraction(
        entryIndex, entries, activeModalEntryIndex);
}

[[nodiscard]] bool UIContext::Impl::isPointerCaptureCandidate(UINodeId node, std::span<const UICommittedHitEntry> entries,
                                             u32 activeModalEntryIndex) const noexcept
{
    if (!node.hasValue() || !contains(node) || !isNodeEnabled(node))
    {
        return false;
    }
    const u32 entryIndex = findHitEntryIndex(node, entries);
    return Detail::hitEntryAllowsPointerCapture(
        entryIndex, entries, activeModalEntryIndex);
}

[[nodiscard]] bool UIContext::Impl::isKeyboardFocusCandidate(UINodeId node, std::span<const UICommittedHitEntry> entries,
                                            u32 activeModalEntryIndex) const noexcept
{
    if (!node.hasValue() || !isNodeEnabled(node))
    {
        return false;
    }
    const u32 entryIndex = findHitEntryIndex(node, entries);
    return Detail::hitEntryAllowsKeyboardFocus(
        entryIndex, entries, activeModalEntryIndex);
}

[[nodiscard]] bool UIContext::Impl::isCommittedKeyboardFocusCandidate(UINodeId node) const noexcept
{
    if (!node.hasValue() || !isNodeEnabled(node))
    {
        return false;
    }
    const NodeRecord* record = nodes.tryGet(node.storageId());
    if (record == nullptr ||
        !hasBehavior(record->behaviors, UIElementBehavior::Focusable))
    {
        return false;
    }
    const auto& hitEntries = committedHitBuffers[publishedHitBufferIndex];
    return isKeyboardFocusCandidate(node, hitEntries, committedActiveModalEntryIndex);
}

[[nodiscard]] bool UIContext::Impl::isCommittedTextEditFocusCandidate(UINodeId node) const noexcept
{
    return isLiveTextEdit(node) && isCommittedKeyboardFocusCandidate(node) && defaultActionFocusButton == node;
}

void UIContext::Impl::deactivateButtonActionForNode(u32 nodeIndex) noexcept
{
    if (nodeIndex >= idsByIndex.size())
    {
        return;
    }
    const UINodeId node = idForIndex(nodeIndex);
    // Node destruction makes every matching control identity stale; no
    // synthetic Up is emitted for the destroyed target.
    defaultActionPressState.clearNode(node);
    if (hoveredPrimaryControl == node)
    {
        hoveredPrimaryControl = {};
    }
    if (armedPrimaryButton == node)
    {
        clearArmedPrimaryButton();
    }
    if (armedSlider == node)
    {
        clearArmedSlider();
    }
    if (armedScrollView == node)
    {
        clearArmedScrollView();
    }
    if (armedTextEdit == node)
    {
        clearArmedTextEdit();
    }
    if (capturedPointerNode == node)
    {
        capturedPointerNode = {};
    }
    if (defaultActionFocusButton == node)
    {
        clearDefaultActionFocus();
    }
    if (textInputFocus == node)
    {
        clearImeFocus();
    }
    // Callback destruction is delayed until the node generation has been
    // erased, so a callable destructor cannot register against a dying node.
    buttonActionRegistry.clearNode(nodeIndex, true);
}

[[nodiscard]] Detail::UIButtonActionInvocation
UIContext::Impl::captureButtonAction(UINodeId button, u64 registrationSerialBoundary) const noexcept
{
    if (!contains(button))
    {
        return {};
    }
    return buttonActionRegistry.capture(button, registrationSerialBoundary);
}

void UIContext::Impl::invokeButtonAction(Detail::UIButtonActionInvocation candidate, const UIButtonActionEvent& event,
                        u64 routeSerial) noexcept
{
    if (!candidate.hasValue() || !contains(candidate.button))
    {
        return;
    }
    const NodeRecord* buttonRecord = nodes.tryGet(candidate.button.storageId());
    if (buttonRecord == nullptr ||
        !behaviorStateStorage.hasActivate(candidate.button.index()))
    {
        return;
    }
    buttonActionRegistry.invoke(candidate, event, routeSerial, routeDispatchDepth != 0);
}

[[nodiscard]] Detail::UISliderChangeCallbackInvocation
UIContext::Impl::captureSliderChangeCallback(UINodeId slider) const noexcept
{
    if (!contains(slider))
    {
        return {};
    }
    return sliderChangeCallbackRegistry.capture(slider);
}

void UIContext::Impl::invokeSliderChangeCallback(Detail::UISliderChangeCallbackInvocation candidate,
                                const UISliderChangeEvent& event) noexcept
{
    if (!candidate.hasValue() || !contains(candidate.slider))
    {
        return;
    }
    const NodeRecord* sliderRecord = nodes.tryGet(candidate.slider.storageId());
    if (sliderRecord == nullptr || sliderRecord->kind != BuiltinElementKind::Slider)
    {
        return;
    }
    sliderChangeCallbackRegistry.invoke(candidate, event, routeDispatchDepth != 0);
}

void UIContext::Impl::clearSemanticsState(u32 index) noexcept
{
    if (index >= semanticsStatesByNodeIndex.size())
    {
        return;
    }
    SemanticsState& state = semanticsStatesByNodeIndex[index];
    textStorage.release(state.textAllocation);
    state = {};
}

[[nodiscard]] UINodeId UIContext::Impl::dropdownForPopup(UINodeId popup) const noexcept
{
    if (!contains(popup))
    {
        return {};
    }
    const NodeRecord* popupRecord = nodes.tryGet(popup.storageId());
    if (popupRecord == nullptr || popupRecord->kind != BuiltinElementKind::Popup ||
        popupRecord->parentIndex == InvalidNodeIndex)
    {
        return {};
    }
    const NodeRecord* dropdownRecord = recordByIndex(popupRecord->parentIndex);
    return dropdownRecord != nullptr && dropdownRecord->kind == BuiltinElementKind::Dropdown
               ? idForIndex(popupRecord->parentIndex)
               : UINodeId{};
}

[[nodiscard]] UINodeId UIContext::Impl::popupForDropdown(UINodeId dropdown) const noexcept
{
    if (!contains(dropdown) || dropdown.index() >= dropdownStatesByNodeIndex.size())
    {
        return {};
    }
    const NodeRecord* record = nodes.tryGet(dropdown.storageId());
    const UINodeId popup = dropdownStatesByNodeIndex[dropdown.index()].popup;
    return record != nullptr && record->kind == BuiltinElementKind::Dropdown && contains(popup) ? popup : UINodeId{};
}

[[nodiscard]] UINodeId UIContext::Impl::dropdownForItem(UINodeId item) const noexcept
{
    if (!contains(item))
    {
        return {};
    }
    const NodeRecord* itemRecord = nodes.tryGet(item.storageId());
    if (itemRecord == nullptr || itemRecord->kind != BuiltinElementKind::DropdownItem ||
        itemRecord->parentIndex == InvalidNodeIndex)
    {
        return {};
    }
    return dropdownForPopup(idForIndex(itemRecord->parentIndex));
}

[[nodiscard]] bool UIContext::Impl::isSelectedDropdownItem(UINodeId item) const noexcept
{
    const UINodeId dropdown = dropdownForItem(item);
    const Detail::UISelectBehaviorState* select =
        dropdown.hasValue() ? behaviorStateStorage.trySelectState(dropdown.index()) : nullptr;
    return select != nullptr && select->selectedOption == item;
}

void UIContext::Impl::releaseFlowNode(u32 index) noexcept
{
    if (index >= flowStatesByNodeIndex.size())
    {
        return;
    }
    UIFlowNodeState& state = flowStatesByNodeIndex[index];
    if (state.kind == UIFlowNodeKind::None)
    {
        return;
    }

    const UINodeId node = idForIndex(index);
    if (state.kind == UIFlowNodeKind::Screen)
    {
        for (const UIFlowActionSlot& actionSlot : state.actions)
        {
            if (actionSlot.registered)
            {
                if (registeredFlowActionCount == 0)
                {
                    std::terminate();
                }
                --registeredFlowActionCount;
            }
        }
        if (state.stacked)
        {
            if (contains(state.previous))
            {
                flowStatesByNodeIndex[state.previous.index()].next = state.next;
            }
            if (contains(state.next))
            {
                flowStatesByNodeIndex[state.next.index()].previous = state.previous;
            }
            if (contains(state.layer))
            {
                UIFlowNodeState& layerState = flowStatesByNodeIndex[state.layer.index()];
                if (layerState.bottom == node)
                {
                    layerState.bottom = state.next;
                }
                if (layerState.top == node)
                {
                    layerState.top = state.previous;
                }
            }
            if (stackedFlowScreenCount == 0)
            {
                std::terminate();
            }
            --stackedFlowScreenCount;
        }
        if (registeredFlowScreenCount == 0)
        {
            std::terminate();
        }
        --registeredFlowScreenCount;
    }
    else
    {
        if (state.bottom.hasValue() || state.top.hasValue() || registeredFlowLayerCount == 0)
        {
            std::terminate();
        }
        --registeredFlowLayerCount;
    }
    state = {};
}

void UIContext::Impl::resetNodeSideData(u32 index) noexcept
{
    if (index >= layoutStylesByIndex.size())
    {
        return;
    }
    layoutStylesByIndex[index] = {};
    pointerHitPoliciesByIndex[index] = UIPointerHitPolicy::Ignore;
    if (index < enabledByNodeIndex.size())
    {
        enabledByNodeIndex[index] = 1;
    }
    if (index < focusScopeModesByNodeIndex.size())
    {
        focusScopeModesByNodeIndex[index] = UIFocusScopeMode::None;
    }
    if (index < focusRestoreByNodeIndex.size())
    {
        focusRestoreByNodeIndex[index] = {};
    }
    if (index < flowStatesByNodeIndex.size())
    {
        flowStatesByNodeIndex[index] = {};
    }
    if (index < styleRolesByNodeIndex.size())
    {
        styleRolesByNodeIndex[index] = UIStyleRoleId::None;
    }
    if (index < styleClassCountsByNodeIndex.size())
    {
        const usize releasedCount = styleClassCountsByNodeIndex[index];
        if (releasedCount > activeNodeStyleClassLinkCount)
        {
            std::terminate();
        }
        activeNodeStyleClassLinkCount -= releasedCount;
        styleClassCountsByNodeIndex[index] = 0;
        styleClassesByNodeIndex[index] = {};
    }
    if (index < styleStatesByNodeIndex.size())
    {
        styleStatesByNodeIndex[index] = UIStyleState::None;
        resolvedStyleInitializedByNodeIndex[index] = 0;
        resolvedBoxFillCacheByNodeIndex[index] = {};
        unlinkStyleTokenDependency(index);
        unlinkImageTintTokenDependency(index);
        if (index < resolvedImageTintValidByNodeIndex.size())
        {
            resolvedImageTintValidByNodeIndex[index] = 0;
            resolvedImageTintCacheByNodeIndex[index] = {};
        }
    }
    boxPaintsByIndex[index] = {};
    buttonPaintsByNodeIndex[index] = {};
    if (index < themeBindingsByNodeIndex.size())
    {
        themeBindingsByNodeIndex[index] = 0;
        styleOverridesByNodeIndex[index] = 0;
        themeDirtyScratchByNodeIndex[index] = 0;
        themeTextMetricsScratchByNodeIndex[index] = {};
    }
    localSolidFillCacheByIndex[index] = {};
    localTextColorCacheByIndex[index] = {};
    clearTextState(index);
    clearSemanticsState(index);
    canvasCommandStorage.release(index);
    imageContentStorage.release(index);
    behaviorStateStorage.release(index);
    if (index < presentationOpacityValidByNodeIndex.size())
    {
        presentationOpacityByNodeIndex[index] = 1.0F;
        presentationOpacityValidByNodeIndex[index] = 0;
        presentationOffsetXByNodeIndex[index] = 0.0F;
        presentationOffsetYByNodeIndex[index] = 0.0F;
        presentationOffsetValidByNodeIndex[index] = 0;
    }
    dirtyQueueStorage.resetNode(index);
    layoutScratchByIndex[index] = {};
    layoutWorkByIndex[index] = 0;
    routedPointerListenerRegistry.resetNodeSlot(index);
    if (index < checkboxPaintsByNodeIndex.size())
    {
        checkboxPaintsByNodeIndex[index] = {};
    }
    if (index < sliderPaintsByNodeIndex.size())
    {
        sliderPaintsByNodeIndex[index] = {};
    }
    if (index < textEditPaintsByNodeIndex.size())
    {
        textEditPaintsByNodeIndex[index] = {};
    }
    if (index < textEditMultilineByNodeIndex.size())
    {
        textEditMultilineByNodeIndex[index] = {};
    }
    if (index < textEditVisualLinesByNodeIndex.size())
    {
        textEditVisualLinesByNodeIndex[index].clear();
    }
    if (index < textEditVisualLayoutsByNodeIndex.size())
    {
        textEditVisualLayoutsByNodeIndex[index] = {};
    }
    if (index < textEditScrollYByNodeIndex.size())
    {
        textEditScrollYByNodeIndex[index] = 0.0F;
    }
    if (index < candidateTextEditVisualLinesByNodeIndex.size())
    {
        candidateTextEditVisualLinesByNodeIndex[index].clear();
    }
    if (index < candidateTextEditVisualLayoutsByNodeIndex.size())
    {
        candidateTextEditVisualLayoutsByNodeIndex[index] = {};
    }
    if (index < candidateTextEditScrollYByNodeIndex.size())
    {
        candidateTextEditScrollYByNodeIndex[index] = 0.0F;
    }
    if (index < textEditPreferredXByNodeIndex.size())
    {
        textEditPreferredXByNodeIndex[index].reset();
    }
    if (index < textEditCaretAffinityByNodeIndex.size())
    {
        textEditCaretAffinityByNodeIndex[index] = Detail::UITextEditCaretAffinity::Downstream;
    }
    if (index < progressBarStatesByNodeIndex.size())
    {
        progressBarStatesByNodeIndex[index] = {};
    }
    if (index < radioButtonStatesByNodeIndex.size())
    {
        radioButtonStatesByNodeIndex[index] = {};
    }
    if (index < scrollViewPaintsByNodeIndex.size())
    {
        scrollViewPaintsByNodeIndex[index] = {};
    }
    if (index < scrollViewLayoutScratchByNodeIndex.size())
    {
        scrollViewLayoutScratchByNodeIndex[index] = {};
    }
    if (index < dropdownStatesByNodeIndex.size())
    {
        dropdownStatesByNodeIndex[index] = {};
    }
    if (index < popupStatesByNodeIndex.size())
    {
        popupStatesByNodeIndex[index] = {};
    }
    if (index < popupLayoutScratchByNodeIndex.size())
    {
        popupLayoutScratchByNodeIndex[index] = {};
    }
    tooltipStorage.resetNode(index);
    dialogStorage.resetNode(index);
    splitViewStorage.resetNode(index);
    tabViewStorage.resetNode(index);
    menuStorage.resetNode(index);
    if (index < listViewStatesByNodeIndex.size())
    {
        listViewStatesByNodeIndex[index] = {};
    }
    if (index < listViewLayoutScratchByNodeIndex.size())
    {
        listViewLayoutScratchByNodeIndex[index] = {};
    }
    if (index < listViewItemStatesByNodeIndex.size())
    {
        listViewItemStatesByNodeIndex[index] = {};
    }
    virtualGridViewStorage.resetNode(index);
    dataGridStorage.resetNode(index);
    if (index < treeViewStatesByNodeIndex.size())
    {
        treeViewStatesByNodeIndex[index] = {};
    }
    if (index < treeViewLayoutScratchByNodeIndex.size())
    {
        treeViewLayoutScratchByNodeIndex[index] = {};
    }
    if (index < treeViewItemStatesByNodeIndex.size())
    {
        treeViewItemStatesByNodeIndex[index] = {};
    }
}

void UIContext::Impl::markStructureChanged() noexcept
{
    phaseDirty |= PhaseStructure | PhaseLayout | PhaseHit;
    layoutReuseCacheValid = false;
}

[[nodiscard]] Detail::UIDirtyQueueEntryDisposition UIContext::Impl::classifyDirtyQueueEntry(UINodeId queued) const noexcept
{
    const u32 index = queued.index();
    if (idForIndex(index) != queued)
    {
        // A stale generation may share its slot with a newly queued node.
        // Never clear the current generation's side-state from the stale entry.
        return Detail::UIDirtyQueueEntryDisposition::DiscardStale;
    }
    if (!contains(queued) || !dirtyQueueStorage.isQueued(index) || !anyDirty(dirtyQueueStorage.flags(index)))
    {
        return Detail::UIDirtyQueueEntryDisposition::DiscardCurrent;
    }
    return Detail::UIDirtyQueueEntryDisposition::Keep;
}

[[nodiscard]] Detail::UIDirtyQueueEntryClassifier UIContext::Impl::dirtyQueueEntryClassifier() const noexcept
{
    return Detail::UIDirtyQueueEntryClassifier{
        .context = this,
        .classify = [](const void* context, UINodeId queued) noexcept {
            return static_cast<const Impl*>(context)->classifyDirtyQueueEntry(queued);
        },
    };
}

void UIContext::Impl::compactDirtyQueue() noexcept
{
    dirtyQueueStorage.compact(dirtyQueueEntryClassifier());
}

[[nodiscard]] usize UIContext::Impl::validDirtyQueueCount() const noexcept
{
    return dirtyQueueStorage.validCount(dirtyQueueEntryClassifier());
}

[[nodiscard]] usize UIContext::Impl::occupiedDirtyQueueSlotCount() const noexcept
{
    return dirtyQueueStorage.occupiedSlotCount();
}

void UIContext::Impl::addRouteDirtyReservationCandidate(UINodeId node)
{
    if (!node.hasValue() || !contains(node))
    {
        return;
    }
    const u32 index = node.index();
    if (index >= dirtyQueueStorage.nodeCapacity() || dirtyQueueStorage.isRouteCandidate(index))
    {
        return;
    }
    dirtyQueueStorage.addRouteCandidate(node);
}

void UIContext::Impl::addRouteLayoutDirtyReservationCandidates(UINodeId node)
{
    if (!node.hasValue() || !contains(node))
    {
        return;
    }
    u32 index = node.index();
    usize visited = 0;
    while (index != InvalidNodeIndex && visited++ < nodes.capacity())
    {
        addRouteDirtyReservationCandidate(idForIndex(index));
        const NodeRecord* record = recordByIndex(index);
        if (record == nullptr)
        {
            return;
        }
        index = record->parentIndex;
    }
}

[[nodiscard]] Core::Status UIContext::Impl::reserveRouteDirtyQueueSlots()
{
    compactDirtyQueue();
    usize requiredQueueEntries = 0;
    for (const UINodeId node : dirtyQueueStorage.routeCandidates())
    {
        if (!contains(node) || node.index() >= dirtyQueueStorage.nodeCapacity())
        {
            return fail(UIErrorCode::InvalidNode, "UI pointer route dirty reservation node is invalid");
        }
        const u32 index = node.index();
        if (!dirtyQueueStorage.isQueued(index) && !dirtyQueueStorage.isReserved(index))
        {
            ++requiredQueueEntries;
        }
    }

    const usize occupiedQueueEntries = occupiedDirtyQueueSlotCount();
    if (occupiedQueueEntries > dirtyQueueStorage.queueCapacity() ||
        requiredQueueEntries > dirtyQueueStorage.queueCapacity() - occupiedQueueEntries)
    {
        return fail(UIErrorCode::CapacityExceeded, "UI dirty queue capacity has been exhausted");
    }

    for (const UINodeId node : dirtyQueueStorage.routeCandidates())
    {
        const u32 index = node.index();
        if (!dirtyQueueStorage.isQueued(index) && !dirtyQueueStorage.isReserved(index))
        {
            dirtyQueueStorage.reserve(index);
        }
    }
    return Core::success();
}

void UIContext::Impl::releaseRouteDirtyQueueReservations() noexcept
{
    dirtyQueueStorage.releaseRouteReservations();
}

[[nodiscard]] Core::Status UIContext::Impl::markLayoutAndPaintDirtyBatch(
    std::span<const UINodeId> requestedLayoutNodes,
    std::span<const UINodeId> requestedPaintNodes)
{
    layoutOrderScratch.clear();
    for (const UINodeId requested : requestedLayoutNodes)
    {
        if (!requested.hasValue())
        {
            continue;
        }
        if (!contains(requested) || requested.index() >= dirtyQueueStorage.nodeCapacity())
        {
            return fail(UIErrorCode::InvalidNode, "UI dirty node is invalid");
        }

        u32 index = requested.index();
        usize visited = 0;
        while (index != InvalidNodeIndex && visited++ < nodes.capacity())
        {
            if (std::find(layoutOrderScratch.begin(), layoutOrderScratch.end(), index) == layoutOrderScratch.end())
            {
                layoutOrderScratch.push_back(index);
            }
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr)
            {
                return fail(UIErrorCode::InvalidNode, "UI dirty ancestry is invalid");
            }
            index = record->parentIndex;
        }
    }
    const usize layoutDirtyNodeCount = layoutOrderScratch.size();
    for (const UINodeId requested : requestedPaintNodes)
    {
        if (!requested.hasValue())
        {
            continue;
        }
        if (!contains(requested) || requested.index() >= dirtyQueueStorage.nodeCapacity())
        {
            return fail(UIErrorCode::InvalidNode, "UI paint dirty node is invalid");
        }
        if (std::find(layoutOrderScratch.begin(), layoutOrderScratch.end(), requested.index()) ==
            layoutOrderScratch.end())
        {
            layoutOrderScratch.push_back(requested.index());
        }
    }
    if (layoutOrderScratch.empty())
    {
        return Core::success();
    }

    usize requiredQueueEntries = 0;
    for (const u32 dirtyIndex : layoutOrderScratch)
    {
        if (!dirtyQueueStorage.isQueued(dirtyIndex) && !dirtyQueueStorage.isReserved(dirtyIndex))
        {
            ++requiredQueueEntries;
        }
    }
    usize occupiedQueueEntries = occupiedDirtyQueueSlotCount();
    if (occupiedQueueEntries > dirtyQueueStorage.queueCapacity() ||
        requiredQueueEntries > dirtyQueueStorage.queueCapacity() - occupiedQueueEntries)
    {
        compactDirtyQueue();
        requiredQueueEntries = 0;
        for (const u32 dirtyIndex : layoutOrderScratch)
        {
            if (!dirtyQueueStorage.isQueued(dirtyIndex) && !dirtyQueueStorage.isReserved(dirtyIndex))
            {
                ++requiredQueueEntries;
            }
        }
        occupiedQueueEntries = occupiedDirtyQueueSlotCount();
    }
    if (occupiedQueueEntries > dirtyQueueStorage.queueCapacity() ||
        requiredQueueEntries > dirtyQueueStorage.queueCapacity() - occupiedQueueEntries)
    {
        return fail(UIErrorCode::CapacityExceeded, "UI dirty queue capacity has been exhausted");
    }
    constexpr UIDirty ChangedNodeDirty = UIDirty::Style | UIDirty::Measure | UIDirty::Arrange | UIDirty::Composite |
                                         UIDirty::HitTest | UIDirty::Semantics;
    constexpr UIDirty AncestorDirty = UIDirty::Measure | UIDirty::Arrange | UIDirty::Composite | UIDirty::HitTest;
    for (usize candidateIndex = 0; candidateIndex < layoutDirtyNodeCount; ++candidateIndex)
    {
        const u32 dirtyIndex = layoutOrderScratch[candidateIndex];
        if (!dirtyQueueStorage.isQueued(dirtyIndex))
        {
            dirtyQueueStorage.enqueue(idForIndex(dirtyIndex));
        }
        const bool directlyRequested = std::any_of(
            requestedLayoutNodes.begin(), requestedLayoutNodes.end(),
            [dirtyIndex](UINodeId requested) noexcept {
                return requested.hasValue() && requested.index() == dirtyIndex;
            });
        dirtyQueueStorage.flags(dirtyIndex) |= directlyRequested ? ChangedNodeDirty : AncestorDirty;
    }
    for (const UINodeId requested : requestedPaintNodes)
    {
        if (!requested.hasValue())
        {
            continue;
        }
        const u32 dirtyIndex = requested.index();
        if (!dirtyQueueStorage.isQueued(dirtyIndex))
        {
            dirtyQueueStorage.enqueue(requested);
        }
        dirtyQueueStorage.flags(dirtyIndex) |= UIDirty::Paint;
    }
    if (layoutDirtyNodeCount != 0)
    {
        phaseDirty |= PhaseLayout | PhaseHit;
    }
    if (!requestedPaintNodes.empty())
    {
        phaseDirty |= PhasePaint;
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::markLayoutDirtyBatch(std::span<const UINodeId> requestedNodes)
{
    return markLayoutAndPaintDirtyBatch(requestedNodes, {});
}

[[nodiscard]] Core::Status UIContext::Impl::markLayoutDirtyBatch(
    std::initializer_list<UINodeId> requestedNodes)
{
    return markLayoutDirtyBatch(
        std::span<const UINodeId>(requestedNodes.begin(), requestedNodes.size()));
}

[[nodiscard]] Core::Status UIContext::Impl::markHitTestDirty(UINodeId node)
{
    if (!contains(node) || node.index() >= dirtyQueueStorage.nodeCapacity())
    {
        return fail(UIErrorCode::InvalidNode, "UI hit-test dirty node is invalid");
    }

    const u32 index = node.index();
    if (!dirtyQueueStorage.isQueued(index))
    {
        if (occupiedDirtyQueueSlotCount() >= dirtyQueueStorage.queueCapacity())
        {
            compactDirtyQueue();
        }
        if (occupiedDirtyQueueSlotCount() >= dirtyQueueStorage.queueCapacity() &&
            !dirtyQueueStorage.isReserved(index))
        {
            return fail(UIErrorCode::CapacityExceeded, "UI dirty queue capacity has been exhausted");
        }
        dirtyQueueStorage.enqueue(node);
    }
    dirtyQueueStorage.flags(index) |= UIDirty::HitTest;
    phaseDirty |= PhaseHit;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::preflightPaintDirtyBatch(std::initializer_list<UINodeId> requestedNodes) const
{
    usize requiredQueueEntries = 0;
    for (auto current = requestedNodes.begin(); current != requestedNodes.end(); ++current)
    {
        if (!current->hasValue())
        {
            continue;
        }
        if (!contains(*current) || current->index() >= dirtyQueueStorage.nodeCapacity())
        {
            return fail(UIErrorCode::InvalidNode, "UI paint dirty node is invalid");
        }
        bool duplicate = false;
        for (auto prior = requestedNodes.begin(); prior != current; ++prior)
        {
            if (prior->hasValue() && *prior == *current)
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && !dirtyQueueStorage.isQueued(current->index()) && !dirtyQueueStorage.isReserved(current->index()))
        {
            ++requiredQueueEntries;
        }
    }

    const usize occupiedQueueEntries = validDirtyQueueCount() + dirtyQueueStorage.reservationCount();
    if (occupiedQueueEntries > dirtyQueueStorage.queueCapacity() ||
        requiredQueueEntries > dirtyQueueStorage.queueCapacity() - occupiedQueueEntries)
    {
        return fail(UIErrorCode::CapacityExceeded, "UI dirty queue capacity has been exhausted");
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::markPaintDirtyBatch(std::initializer_list<UINodeId> requestedNodes)
{
    usize uniqueNodeCount = 0;
    usize requiredQueueEntries = 0;
    for (auto current = requestedNodes.begin(); current != requestedNodes.end(); ++current)
    {
        if (!current->hasValue())
        {
            continue;
        }
        if (!contains(*current) || current->index() >= dirtyQueueStorage.nodeCapacity())
        {
            return fail(UIErrorCode::InvalidNode, "UI paint dirty node is invalid");
        }
        bool duplicate = false;
        for (auto prior = requestedNodes.begin(); prior != current; ++prior)
        {
            if (prior->hasValue() && *prior == *current)
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && !dirtyQueueStorage.isQueued(current->index()) && !dirtyQueueStorage.isReserved(current->index()))
        {
            ++requiredQueueEntries;
        }
        if (!duplicate)
        {
            ++uniqueNodeCount;
        }
    }
    if (uniqueNodeCount == 0)
    {
        return Core::success();
    }

    usize occupiedQueueEntries = occupiedDirtyQueueSlotCount();
    if (occupiedQueueEntries > dirtyQueueStorage.queueCapacity() ||
        requiredQueueEntries > dirtyQueueStorage.queueCapacity() - occupiedQueueEntries)
    {
        compactDirtyQueue();
        requiredQueueEntries = 0;
        for (auto current = requestedNodes.begin(); current != requestedNodes.end(); ++current)
        {
            if (!current->hasValue())
            {
                continue;
            }
            bool duplicate = false;
            for (auto prior = requestedNodes.begin(); prior != current; ++prior)
            {
                if (prior->hasValue() && *prior == *current)
                {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate && !dirtyQueueStorage.isQueued(current->index()) &&
                !dirtyQueueStorage.isReserved(current->index()))
            {
                ++requiredQueueEntries;
            }
        }
        occupiedQueueEntries = occupiedDirtyQueueSlotCount();
    }
    if (occupiedQueueEntries > dirtyQueueStorage.queueCapacity() ||
        requiredQueueEntries > dirtyQueueStorage.queueCapacity() - occupiedQueueEntries)
    {
        return fail(UIErrorCode::CapacityExceeded, "UI dirty queue capacity has been exhausted");
    }

    for (auto current = requestedNodes.begin(); current != requestedNodes.end(); ++current)
    {
        if (!current->hasValue())
        {
            continue;
        }
        bool duplicate = false;
        for (auto prior = requestedNodes.begin(); prior != current; ++prior)
        {
            if (prior->hasValue() && *prior == *current)
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
        {
            continue;
        }
        const u32 index = current->index();
        if (!dirtyQueueStorage.isQueued(index))
        {
            dirtyQueueStorage.enqueue(*current);
        }
        dirtyQueueStorage.flags(index) |= UIDirty::Paint | UIDirty::Semantics;
    }
    phaseDirty |= PhasePaint | PhaseSemantics;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::markPaintDirty(UINodeId node)
{
    return markPaintDirtyBatch({node});
}

[[nodiscard]] Core::Status UIContext::Impl::setInputModality(UIInputModality modality)
{
    if (modality != UIInputModality::Pointer &&
        modality != UIInputModality::Keyboard &&
        modality != UIInputModality::Gamepad &&
        modality != UIInputModality::Accessibility)
    {
        return fail(UIErrorCode::InvalidFocusTarget,
                    "UI input modality is not recognized");
    }
    if (inputModality == modality)
    {
        return Core::success();
    }
    const UINodeId focusedButton = defaultActionFocusButton;
    const UINodeId focusedTextEdit = textInputFocus;
    if (Core::Status dirty = markPaintDirtyBatch({focusedButton, focusedTextEdit}); !dirty)
    {
        return dirty;
    }
    inputModality = modality;
    return Core::success();
}

// Dispatches through UIStylePropertyKind dirty metadata (UI-STYLE-001).
// Keeps capacity/atomic dirty-queue helpers as the only mutation path.
[[nodiscard]] Core::Status UIContext::Impl::markStylePropertyDirty(UINodeId node, UIStylePropertyKind kind)
{
    static_assert(!stylePropertyDirtiesLayout(UIStylePropertyKind::ColorOrOpacity));
    static_assert(stylePropertyDirtiesPaint(UIStylePropertyKind::ColorOrOpacity));
    static_assert(!stylePropertyDirtiesLayout(UIStylePropertyKind::ColorToken));
    static_assert(stylePropertyDirtiesPaint(UIStylePropertyKind::ColorToken));
    static_assert(stylePropertyDirtiesLayout(UIStylePropertyKind::TextStyle));
    static_assert(stylePropertyDirtiesPaint(UIStylePropertyKind::TextStyle));
    static_assert(stylePropertyDirtiesLayout(UIStylePropertyKind::TextWrap));
    static_assert(stylePropertyDirtiesPaint(UIStylePropertyKind::TextWrap));
    static_assert(stylePropertyDirtiesHit(UIStylePropertyKind::PointerHitPolicy));
    static_assert(!stylePropertyDirtiesPaint(UIStylePropertyKind::PointerHitPolicy));
    static_assert(stylePropertyDirtiesLayout(UIStylePropertyKind::LayoutStyle));
    static_assert(!stylePropertyDirtiesPaint(UIStylePropertyKind::LayoutStyle));
    static_assert(!stylePropertyDirtiesLayout(UIStylePropertyKind::TextOverflow));
    static_assert(stylePropertyDirtiesPaint(UIStylePropertyKind::TextOverflow));
    static_assert(!stylePropertyDirtiesSemantics(UIStylePropertyKind::TextOverflow));

    switch (kind) {
    case UIStylePropertyKind::ColorOrOpacity:
        return markPaintDirty(node);
    // Paint-only kinds share one path: reserve a dirty-queue slot, then mark
    // Paint without touching the layout, hit, or semantics phases.
    case UIStylePropertyKind::ColorToken:
    case UIStylePropertyKind::TextOverflow: {
        if (!contains(node) || node.index() >= dirtyQueueStorage.nodeCapacity()) {
            return fail(UIErrorCode::InvalidNode, "UI style property dirty node is invalid");
        }
        const u32 index = node.index();
        if (!dirtyQueueStorage.isQueued(index) && !dirtyQueueStorage.isReserved(index)) {
            compactDirtyQueue();
            if (occupiedDirtyQueueSlotCount() >= dirtyQueueStorage.queueCapacity()) {
                return fail(UIErrorCode::CapacityExceeded,
                            "UI dirty queue capacity has been exhausted");
            }
        }
        if (!dirtyQueueStorage.isQueued(index)) {
            dirtyQueueStorage.enqueue(node);
        }
        dirtyQueueStorage.flags(index) |= UIDirty::Paint;
        phaseDirty |= PhasePaint;
        return Core::success();
    }
    case UIStylePropertyKind::TextStyle:
    case UIStylePropertyKind::TextWrap:
    case UIStylePropertyKind::ContentAlignment: {
        if (Core::Status layout = markLayoutStyleDirty(node); !layout) {
            return layout;
        }
        return markPaintDirty(node);
    }
    case UIStylePropertyKind::PointerHitPolicy:
        return markHitTestDirty(node);
    case UIStylePropertyKind::LayoutStyle:
        return markLayoutStyleDirty(node);
    }
    return Core::success();
}

void UIContext::Impl::clearDirtyState() noexcept
{
    dirtyQueueStorage.clearQueuedDirtyState();
    phaseDirty = UIDirty::None;
}

[[nodiscard]] bool UIContext::Impl::isNodeWithinRoot(UINodeId root, UINodeId node) const noexcept
{
    if (!contains(root) || !contains(node))
    {
        return false;
    }

    const NodeRecord* nodeRecord = nodes.tryGet(node.storageId());
    if (nodeRecord == nullptr)
    {
        return false;
    }
    return nodeRecord->rootIndex == root.index();
}

[[nodiscard]] bool UIContext::Impl::isNodeWithinSubtree(UINodeId subtreeRoot, UINodeId node) const noexcept
{
    if (!contains(subtreeRoot) || !contains(node))
    {
        return false;
    }

    u32 currentIndex = node.index();
    usize visitedCount = 0;
    while (currentIndex != InvalidNodeIndex && visitedCount++ < nodes.capacity())
    {
        if (currentIndex == subtreeRoot.index())
        {
            return true;
        }
        const NodeRecord* current = recordByIndex(currentIndex);
        if (current == nullptr)
        {
            return false;
        }
        currentIndex = current->parentIndex;
    }
    return false;
}

} // namespace Tina::UI
