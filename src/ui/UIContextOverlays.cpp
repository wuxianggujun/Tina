#include "detail/UIContextImpl.hpp"

namespace Tina::UI {

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolvePopup(UINodeId popup)
{
    auto nodeResult = resolveNode(popup);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::Popup || popup.index() >= popupStatesByNodeIndex.size())
    {
        return fail(UIErrorCode::InvalidControlValue, "UI Popup API requires a Popup node");
    }
    return *nodeResult;
}

[[nodiscard]] Core::Status UIContext::Impl::registerDialogFromBuild(const UIDialogParts& parts)
{
    const NodeRecord* record =
        contains(parts.modal) ? nodes.tryGet(parts.modal.storageId()) : nullptr;
    if (record == nullptr || record->kind != BuiltinElementKind::Modal)
    {
        return fail(Core::CoreErrorCode::Internal,
                    "UI Dialog registration requires a live Modal root");
    }
    if (!dialogStorage.initializeDialog(parts))
    {
        static_cast<void>(destroySubtree(parts.modal));
        return fail(UIErrorCode::CapacityExceeded,
                    "UI Dialog state capacity has been exhausted");
    }
    return Core::success();
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveDialog(UINodeId dialog)
{
    auto nodeResult = resolveNode(dialog);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::Modal ||
        !dialogStorage.containsDialog(dialog))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI Dialog API requires a Dialog built by buildDialog");
    }
    return *nodeResult;
}

void UIContext::Impl::closeActiveMenuForDialogNoFail(UINodeId menu) noexcept
{
    if (!menu.hasValue())
    {
        return;
    }
    const UINodeId anchor = menuPlacementAnchor(menu);
    const bool focusWasInMenu =
        isNodeWithinActiveMenuBranch(menu, defaultActionFocusButton);
    UINodeId nextFocus = focusRestoreByNodeIndex[menu.index()];
    static_cast<void>(menuStorage.close(menu));
    menuCommandPressLatch.clear();
    transientOverlayDismissPointerBarrierActive = false;
    if (!focusWasInMenu)
    {
        return;
    }
    if (!nextFocus.hasValue() || !contains(nextFocus) ||
        !isNodeEnabled(nextFocus))
    {
        nextFocus = isNodeEnabled(anchor) ? anchor : UINodeId{};
    }
    defaultActionPressState.clearAll();
    clearAllPointerArms();
    resetImeCompositionState();
    textInputFocus = {};
    defaultActionFocusButton = nextFocus;
}

[[nodiscard]] Core::Status UIContext::Impl::setDialogOpenState(UINodeId dialog, bool open)
{
    auto dialogResult = resolveDialog(dialog);
    if (!dialogResult)
    {
        return Core::failure(dialogResult.error());
    }
    const bool wasOpen = dialogStorage.isOpen(dialog);
    if (wasOpen == open)
    {
        return Core::success();
    }
    const UINodeId activeDialog = dialogStorage.activeDialog();
    if (open && activeDialog.hasValue() && activeDialog != dialog)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "Only one registered UI Dialog may be open per Window");
    }

    const UINodeId menuToClose = menuStorage.rootMenu();
    const Core::Status dirty =
        menuToClose.hasValue()
            ? markMenuMutationLayoutDirty({dialog}, {menuToClose})
            : markStylePropertyDirty(dialog,
                                     UIStylePropertyKind::LayoutStyle);
    if (!dirty)
    {
        return dirty;
    }

    if (open)
    {
        dialogStorage.openValidated(dialog);
    } else
    {
        static_cast<void>(dialogStorage.dismiss(dialog));
    }
    closeActiveMenuForDialogNoFail(menuToClose);
    hardDismissAllTooltipsNoFail(true);
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::openDialog(UINodeId dialog)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    return setDialogOpenState(dialog, true);
}

[[nodiscard]] Core::Status UIContext::Impl::dismissDialog(UINodeId dialog)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    return setDialogOpenState(dialog, false);
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isDialogOpen(UINodeId dialog) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    auto dialogResult = const_cast<Impl*>(this)->resolveDialog(dialog);
    if (!dialogResult)
    {
        return Core::failure(dialogResult.error());
    }
    return dialogStorage.isOpen(dialog);
}

[[nodiscard]] Core::Status UIContext::Impl::validateDialogUpdaterRoot(
    UINodeId updaterRoot, UINodeId dialog) const
{
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired,
                    "UI tree updater requires a live root owner");
    }
    auto dialogResult = const_cast<Impl*>(this)->resolveDialog(dialog);
    if (!dialogResult)
    {
        return Core::failure(dialogResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, dialog))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI Dialog is not owned by the updater root");
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::openDialogFromUpdater(
    UINodeId updaterRoot, UINodeId dialog)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateDialogUpdaterRoot(updaterRoot, dialog);
        !valid)
    {
        return valid;
    }
    return setDialogOpenState(dialog, true);
}

[[nodiscard]] Core::Status UIContext::Impl::dismissDialogFromUpdater(
    UINodeId updaterRoot, UINodeId dialog)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateDialogUpdaterRoot(updaterRoot, dialog);
        !valid)
    {
        return valid;
    }
    return setDialogOpenState(dialog, false);
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isDialogOpenFromUpdater(
    UINodeId updaterRoot, UINodeId dialog) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateDialogUpdaterRoot(updaterRoot, dialog);
        !valid)
    {
        return Core::failure(valid.error());
    }
    return dialogStorage.isOpen(dialog);
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveTooltip(UINodeId tooltip)
{
    auto nodeResult = resolveNode(tooltip);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::Tooltip ||
        !tooltipStorage.containsTooltip(tooltip))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI Tooltip API requires a Tooltip node");
    }
    return *nodeResult;
}

[[nodiscard]] bool UIContext::Impl::hasValidTooltipRelationship(UINodeId tooltip,
                                               UINodeId anchor) const noexcept
{
    if (!contains(tooltip) || !contains(anchor))
    {
        return false;
    }
    const NodeRecord* tooltipRecord = nodes.tryGet(tooltip.storageId());
    const NodeRecord* anchorRecord = nodes.tryGet(anchor.storageId());
    return tooltipRecord != nullptr && anchorRecord != nullptr &&
           tooltipRecord->kind == BuiltinElementKind::Tooltip &&
           tooltipRecord->rootIndex == anchorRecord->rootIndex &&
           tooltipStorage.hasRelationship(tooltip, anchor);
}

[[nodiscard]] UINodeId UIContext::Impl::tooltipForAnchor(UINodeId anchor) const noexcept
{
    if (!contains(anchor))
    {
        return {};
    }
    const UINodeId tooltip = tooltipStorage.tooltipForAnchor(anchor);
    return hasValidTooltipRelationship(tooltip, anchor) ? tooltip : UINodeId{};
}

[[nodiscard]] bool UIContext::Impl::isAuthoredTooltipNodeVisible(UINodeId node) const noexcept
{
    if (!contains(node))
    {
        return false;
    }
    u32 currentIndex = node.index();
    usize visited = 0;
    while (currentIndex != InvalidNodeIndex && visited++ < nodes.capacity())
    {
        const NodeRecord* record = recordByIndex(currentIndex);
        if (record == nullptr || currentIndex >= layoutStylesByIndex.size() ||
            layoutStylesByIndex[currentIndex].visibility != UIVisibility::Visible)
        {
            return false;
        }
        currentIndex = record->parentIndex;
    }
    return currentIndex == InvalidNodeIndex;
}

[[nodiscard]] bool UIContext::Impl::isTooltipAnchorEligible(UINodeId tooltip) const noexcept
{
    if (!contains(tooltip))
    {
        return false;
    }
    const TooltipState* state = tooltipStorage.tryState(tooltip);
    if (state == nullptr)
    {
        return false;
    }
    const UINodeId anchor = state->anchor;
    if (!hasValidTooltipRelationship(tooltip, anchor) || !isNodeEnabled(anchor) ||
        !isAuthoredTooltipNodeVisible(anchor) ||
        !isAuthoredTooltipNodeVisible(tooltip))
    {
        return false;
    }
    if (activeModalNode.hasValue() &&
        !isNodeWithinSubtree(activeModalNode, anchor))
    {
        return false;
    }
    const UICommittedLayoutEntry* anchorEntry = committedLayoutEntryFor(anchor);
    return anchorEntry != nullptr &&
           anchorEntry->effectiveVisibility == UIVisibility::Visible;
}

void UIContext::Impl::markTooltipPresentationDirty() noexcept
{
    // A Window has at most one active Tooltip, so a bounded full snapshot
    // rebuild avoids a second dirty ancestry path inside the frame coordinator.
    phaseDirty |= PhaseLayout | PhaseHit | PhasePaint | PhaseSemantics;
    layoutReuseCacheValid = false;
}

[[nodiscard]] Detail::UITooltipAdvanceCandidate
UIContext::Impl::tooltipAdvanceCandidate(UINodeId tooltip) const noexcept
{
    return {
        .tooltip = tooltip,
        .eligible = isTooltipAnchorEligible(tooltip),
    };
}

void UIContext::Impl::advanceTooltips(Core::MonotonicTimePoint now) noexcept
{
    const UINodeId active = tooltipStorage.activeTooltip();
    const UINodeId manual = tooltipStorage.manualTooltip();
    const UINodeId hover =
        tooltipForAnchor(tooltipStorage.hoveredAnchor());
    const UINodeId focus = tooltipForAnchor(defaultActionFocusButton);
    const Detail::UITooltipAdvanceInput input{
        .now = now,
        .focusedAnchor = defaultActionFocusButton,
        .active = tooltipAdvanceCandidate(active),
        .manual = tooltipAdvanceCandidate(manual),
        .hover = tooltipAdvanceCandidate(hover),
        .focus = tooltipAdvanceCandidate(focus),
    };
    if (tooltipStorage.advance(input))
    {
        markTooltipPresentationDirty();
    }
}

void UIContext::Impl::hardDismissAllTooltipsNoFail(bool suppress) noexcept
{
    if (tooltipStorage.hardDismiss(defaultActionFocusButton, suppress))
    {
        markTooltipPresentationDirty();
    }
}

void UIContext::Impl::dismissTooltipNoFail(UINodeId tooltip, bool suppress) noexcept
{
    if (tooltipStorage.dismiss(tooltip, suppress, motionNow()))
    {
        markTooltipPresentationDirty();
    }
}

void UIContext::Impl::reconcileTooltipAfterPublication(
    Core::MonotonicTimePoint now) noexcept
{
    const UINodeId active = tooltipStorage.activeTooltip();
    if (!active.hasValue())
    {
        return;
    }
    if (!isTooltipAnchorEligible(active))
    {
        if (tooltipStorage.dismiss(active, true, now))
        {
            markTooltipPresentationDirty();
        }
        return;
    }

    const TooltipState* state = tooltipStorage.tryState(active);
    if (state == nullptr)
    {
        return;
    }
    const UICommittedLayoutEntry* anchorEntry =
        committedLayoutEntryFor(state->anchor);
    if (anchorEntry == nullptr || !state->committedMetrics.open ||
        state->committedMetrics.anchorRect != anchorEntry->worldRect)
    {
        // Tooltip placement intentionally consumed the previous successful
        // Anchor geometry. Schedule one bounded follow-up publication when
        // the newly committed Anchor snapshot differs.
        markTooltipPresentationDirty();
    }
}

void UIContext::Impl::reconcileMenuAfterPublication() noexcept
{
    UINodeId active = menuStorage.rootMenu();
    usize visited = 0;
    while (active.hasValue() && visited++ < menuStorage.capacity())
    {
        const MenuState* state = menuStorage.tryMenu(active);
        const UINodeId anchor = menuPlacementAnchor(active);
        const UICommittedLayoutEntry* anchorEntry =
            committedLayoutEntryFor(anchor);
        if (state == nullptr ||
            !hasValidMenuPlacementRelationship(active, anchor) ||
            anchorEntry == nullptr ||
            anchorEntry->effectiveVisibility != UIVisibility::Visible ||
            !isNodeEnabled(anchor) || !isNodeEnabled(active) ||
            (activeModalNode.hasValue() &&
             !isNodeWithinSubtree(activeModalNode, anchor)))
        {
            static_cast<void>(menuStorage.close(active));
            menuCommandPressLatch.clear();
            transientOverlayDismissPointerBarrierActive = false;
            phaseDirty |=
                PhaseLayout | PhaseHit | PhasePaint | PhaseSemantics;
            layoutReuseCacheValid = false;
            return;
        }
        const UILogicalRect placementAnchorRect =
            menuStorage.hasInvocationAnchorRect(active)
                ? menuStorage.invocationAnchorRect(active)
                : anchorEntry->worldRect;
        if (!state->committedMetrics.open ||
            state->committedMetrics.anchorRect != placementAnchorRect)
        {
            phaseDirty |=
                PhaseLayout | PhaseHit | PhasePaint | PhaseSemantics;
            layoutReuseCacheValid = false;
        }
        active = menuStorage.activeChildMenu(active);
    }
}

[[nodiscard]] UINodeId UIContext::Impl::tooltipAnchorFromCommittedHit(
    const UIPointerHitTarget& target,
    std::span<const UICommittedHitEntry> entries) const noexcept
{
    if (!target.hasValue() || target.hitEntryIndex >= entries.size())
    {
        return {};
    }
    u32 entryIndex = target.hitEntryIndex;
    usize visited = 0;
    while (entryIndex < entries.size() && visited++ < entries.size())
    {
        const UICommittedHitEntry& entry = entries[entryIndex];
        const UINodeId tooltip = tooltipForAnchor(entry.node);
        const TooltipState* state = tooltipStorage.tryState(tooltip);
        if (state != nullptr &&
            hasTooltipTrigger(state->config.triggers,
                              UITooltipTrigger::PointerHover))
        {
            return entry.node;
        }
        if (entry.parentEntryIndex == InvalidUIHitEntryIndex)
        {
            break;
        }
        entryIndex = entry.parentEntryIndex;
    }
    return {};
}

[[nodiscard]] Core::Status UIContext::Impl::setTooltipAnchorRelation(UINodeId tooltip,
                                                    UINodeId anchor)
{
    auto tooltipResult = resolveTooltip(tooltip);
    if (!tooltipResult)
    {
        return Core::failure(tooltipResult.error());
    }
    auto anchorResult = resolveNode(anchor);
    if (!anchorResult)
    {
        return Core::failure(anchorResult.error());
    }
    NodeRecord* tooltipRecord = *tooltipResult;
    NodeRecord* anchorRecord = *anchorResult;
    if (tooltip == anchor)
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Tooltip cannot anchor to itself");
    }
    if (tooltipRecord->rootIndex != anchorRecord->rootIndex)
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Tooltip and Anchor must belong to the same root");
    }
    if (isNodeWithinSubtree(tooltip, anchor) ||
        isNodeWithinSubtree(anchor, tooltip))
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Tooltip and Anchor cannot form an ancestor cycle");
    }
    switch (anchorRecord->kind)
    {
    case BuiltinElementKind::Root:
    case BuiltinElementKind::Tooltip:
    case BuiltinElementKind::Popup:
    case BuiltinElementKind::Modal:
    case BuiltinElementKind::ListViewItem:
    case BuiltinElementKind::TreeViewItem:
        return fail(UIErrorCode::InvalidParent,
                    "UI node kind is not a stable Tooltip Anchor");
    default:
        break;
    }

    const UINodeId reverse = tooltipStorage.tooltipForAnchor(anchor);
    if (reverse.hasValue() && reverse != tooltip &&
        hasValidTooltipRelationship(reverse, anchor))
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Anchor already owns a Tooltip relationship");
    }
    const TooltipState* state = tooltipStorage.tryState(tooltip);
    if (state != nullptr && state->anchor == anchor && reverse == tooltip)
    {
        return Core::success();
    }
    dismissTooltipNoFail(tooltip, false);
    tooltipStorage.linkValidated(tooltip, anchor);
    markTooltipPresentationDirty();
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::clearTooltipAnchorRelation(UINodeId tooltip)
{
    auto tooltipResult = resolveTooltip(tooltip);
    if (!tooltipResult)
    {
        return Core::failure(tooltipResult.error());
    }
    const TooltipState* state = tooltipStorage.tryState(tooltip);
    if (state == nullptr || !state->anchor.hasValue())
    {
        return Core::success();
    }
    dismissTooltipNoFail(tooltip, false);
    static_cast<void>(tooltipStorage.unlinkTooltip(tooltip));
    markTooltipPresentationDirty();
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setTooltipAnchor(UINodeId tooltip,
                                            UINodeId anchor)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    return setTooltipAnchorRelation(tooltip, anchor);
}

[[nodiscard]] Core::Status UIContext::Impl::clearTooltipAnchor(UINodeId tooltip)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    return clearTooltipAnchorRelation(tooltip);
}

[[nodiscard]] Core::Result<UINodeId>
UIContext::Impl::tooltipAnchor(UINodeId tooltip) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    auto tooltipResult = const_cast<Impl*>(this)->resolveTooltip(tooltip);
    if (!tooltipResult)
    {
        return Core::failure(tooltipResult.error());
    }
    const UINodeId anchor = tooltipStorage.anchorForTooltip(tooltip);
    return hasValidTooltipRelationship(tooltip, anchor) ? anchor : UINodeId{};
}

[[nodiscard]] Core::Status UIContext::Impl::showTooltip(UINodeId tooltip)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    auto tooltipResult = resolveTooltip(tooltip);
    if (!tooltipResult)
    {
        return Core::failure(tooltipResult.error());
    }
    const TooltipState* state = tooltipStorage.tryState(tooltip);
    if (state == nullptr || !hasValidTooltipRelationship(tooltip, state->anchor))
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Tooltip requires a live Anchor before it can show");
    }
    if (!hasTooltipTrigger(state->config.triggers, UITooltipTrigger::Manual))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI Tooltip does not enable the Manual trigger");
    }
    if (tooltipStorage.requestManual(tooltip,
                                     isTooltipAnchorEligible(tooltip)))
    {
        markTooltipPresentationDirty();
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::dismissTooltip(UINodeId tooltip)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    auto tooltipResult = resolveTooltip(tooltip);
    if (!tooltipResult)
    {
        return Core::failure(tooltipResult.error());
    }
    dismissTooltipNoFail(tooltip, true);
    return Core::success();
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isTooltipOpen(UINodeId tooltip) const
{
    auto metrics = tooltipMetrics(tooltip);
    if (!metrics)
    {
        return Core::failure(metrics.error());
    }
    return metrics->open;
}

[[nodiscard]] Core::Result<UITooltipMetrics>
UIContext::Impl::tooltipMetrics(UINodeId tooltip) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    auto tooltipResult = const_cast<Impl*>(this)->resolveTooltip(tooltip);
    if (!tooltipResult)
    {
        return Core::failure(tooltipResult.error());
    }
    return tooltipStorage.committedMetrics(tooltip);
}

[[nodiscard]] Core::Status UIContext::Impl::validateTooltipUpdaterRoot(
    UINodeId updaterRoot, UINodeId tooltip) const
{
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired,
                    "UI tree updater requires a live root owner");
    }
    auto tooltipResult = const_cast<Impl*>(this)->resolveTooltip(tooltip);
    if (!tooltipResult)
    {
        return Core::failure(tooltipResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, tooltip))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI Tooltip is not owned by the updater root");
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setTooltipAnchorFromUpdater(
    UINodeId updaterRoot, UINodeId tooltip, UINodeId anchor)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateTooltipUpdaterRoot(updaterRoot, tooltip);
        !valid)
    {
        return valid;
    }
    if (!contains(anchor) || !isNodeWithinRoot(updaterRoot, anchor))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI Tooltip Anchor is not owned by the updater root");
    }
    return setTooltipAnchorRelation(tooltip, anchor);
}

[[nodiscard]] Core::Status UIContext::Impl::clearTooltipAnchorFromUpdater(
    UINodeId updaterRoot, UINodeId tooltip)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateTooltipUpdaterRoot(updaterRoot, tooltip);
        !valid)
    {
        return valid;
    }
    return clearTooltipAnchorRelation(tooltip);
}

[[nodiscard]] Core::Result<UINodeId> UIContext::Impl::tooltipAnchorFromUpdater(
    UINodeId updaterRoot, UINodeId tooltip) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateTooltipUpdaterRoot(updaterRoot, tooltip);
        !valid)
    {
        return Core::failure(valid.error());
    }
    return tooltipAnchor(tooltip);
}

[[nodiscard]] Core::Status UIContext::Impl::showTooltipFromUpdater(
    UINodeId updaterRoot, UINodeId tooltip)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateTooltipUpdaterRoot(updaterRoot, tooltip);
        !valid)
    {
        return valid;
    }
    return showTooltip(tooltip);
}

[[nodiscard]] Core::Status UIContext::Impl::dismissTooltipFromUpdater(
    UINodeId updaterRoot, UINodeId tooltip)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateTooltipUpdaterRoot(updaterRoot, tooltip);
        !valid)
    {
        return valid;
    }
    return dismissTooltip(tooltip);
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isTooltipOpenFromUpdater(
    UINodeId updaterRoot, UINodeId tooltip) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateTooltipUpdaterRoot(updaterRoot, tooltip);
        !valid)
    {
        return Core::failure(valid.error());
    }
    return isTooltipOpen(tooltip);
}

[[nodiscard]] Core::Result<UITooltipMetrics> UIContext::Impl::tooltipMetricsFromUpdater(
    UINodeId updaterRoot, UINodeId tooltip) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateTooltipUpdaterRoot(updaterRoot, tooltip);
        !valid)
    {
        return Core::failure(valid.error());
    }
    return tooltipMetrics(tooltip);
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveMenu(UINodeId menu)
{
    auto nodeResult = resolveNode(menu);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::Menu ||
        !menuStorage.containsMenu(menu))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI Menu API requires a Menu node");
    }
    return *nodeResult;
}

[[nodiscard]] Core::Result<NodeRecord*> UIContext::Impl::resolveMenuItem(UINodeId item)
{
    auto nodeResult = resolveNode(item);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if ((*nodeResult)->kind != BuiltinElementKind::MenuItem ||
        !menuStorage.containsMenuItem(item))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI MenuItem API requires a MenuItem node");
    }
    return *nodeResult;
}

[[nodiscard]] bool UIContext::Impl::hasValidSubmenuRelationship(
    UINodeId item, UINodeId submenu) const noexcept
{
    if (!contains(item) || !contains(submenu))
    {
        return false;
    }
    const NodeRecord* itemRecord = nodes.tryGet(item.storageId());
    const NodeRecord* submenuRecord = nodes.tryGet(submenu.storageId());
    const MenuItemState* itemState = menuStorage.tryItem(item);
    const UINodeId parentMenu = menuStorage.menuForItem(item);
    return itemRecord != nullptr && submenuRecord != nullptr &&
           itemState != nullptr &&
           itemRecord->kind == BuiltinElementKind::MenuItem &&
           submenuRecord->kind == BuiltinElementKind::Menu &&
           itemState->config.kind == UIMenuItemKind::Submenu &&
           parentMenu.hasValue() && parentMenu != submenu &&
           itemRecord->rootIndex == submenuRecord->rootIndex &&
           menuStorage.hasSubmenuRelationship(item, submenu);
}

[[nodiscard]] UINodeId UIContext::Impl::menuPlacementAnchor(UINodeId menu) const noexcept
{
    const UINodeId anchor = menuStorage.anchorForMenu(menu);
    if (hasValidMenuRelationship(menu, anchor))
    {
        return anchor;
    }
    const UINodeId parentItem = menuStorage.parentItemForMenu(menu);
    return hasValidSubmenuRelationship(parentItem, menu)
               ? parentItem
               : UINodeId{};
}

[[nodiscard]] bool UIContext::Impl::hasValidMenuPlacementRelationship(
    UINodeId menu, UINodeId anchor) const noexcept
{
    return hasValidMenuRelationship(menu, anchor) ||
           hasValidSubmenuRelationship(anchor, menu);
}

void UIContext::Impl::appendMenuMutationNode(UINodeId node)
{
    if (!node.hasValue() || !contains(node) ||
        std::find(menuMutationNodeScratch.begin(),
                  menuMutationNodeScratch.end(), node) !=
            menuMutationNodeScratch.end())
    {
        return;
    }
    menuMutationNodeScratch.push_back(node);
}

void UIContext::Impl::appendActiveMenuBranchMutationNodes(UINodeId menu)
{
    UINodeId current = menu;
    usize visited = 0;
    while (current.hasValue() && visited++ < menuStorage.capacity())
    {
        appendMenuMutationNode(current);
        appendMenuMutationNode(menuPlacementAnchor(current));
        current = menuStorage.activeChildMenu(current);
    }
}

[[nodiscard]] Core::Status UIContext::Impl::markMenuMutationLayoutDirty(
    std::initializer_list<UINodeId> nodesToDirty,
    std::initializer_list<UINodeId> activeBranchRoots)
{
    menuMutationNodeScratch.clear();
    for (const UINodeId node : nodesToDirty)
    {
        appendMenuMutationNode(node);
    }
    for (const UINodeId branchRoot : activeBranchRoots)
    {
        appendActiveMenuBranchMutationNodes(branchRoot);
    }
    return markLayoutDirtyBatch(std::span<const UINodeId>(
        menuMutationNodeScratch.data(), menuMutationNodeScratch.size()));
}

void UIContext::Impl::addActiveMenuBranchDirtyReservationCandidates(UINodeId menu)
{
    UINodeId current = menu;
    usize visited = 0;
    while (current.hasValue() && visited++ < menuStorage.capacity())
    {
        addRouteLayoutDirtyReservationCandidates(current);
        addRouteLayoutDirtyReservationCandidates(menuPlacementAnchor(current));
        current = menuStorage.activeChildMenu(current);
    }
}

[[nodiscard]] bool UIContext::Impl::isNodeWithinActiveMenuBranch(
    UINodeId menu, UINodeId node) const noexcept
{
    UINodeId current = menu;
    usize visited = 0;
    while (current.hasValue() && visited++ < menuStorage.capacity())
    {
        if (isNodeWithinSubtree(current, node))
        {
            return true;
        }
        current = menuStorage.activeChildMenu(current);
    }
    return false;
}

[[nodiscard]] UINodeId UIContext::Impl::firstActiveMenuBranchAffectedBy(
    UINodeId node, bool includeDescendants) const noexcept
{
    if (!contains(node))
    {
        return {};
    }
    UINodeId current = menuStorage.rootMenu();
    usize visited = 0;
    while (current.hasValue() && visited++ < menuStorage.capacity())
    {
        const UINodeId anchor = menuPlacementAnchor(current);
        const bool affectsMenu = includeDescendants
                                     ? isNodeWithinSubtree(node, current)
                                     : node == current;
        const bool affectsAnchor = includeDescendants
                                       ? isNodeWithinSubtree(node, anchor)
                                       : node == anchor;
        if (affectsMenu || affectsAnchor)
        {
            return current;
        }
        current = menuStorage.activeChildMenu(current);
    }
    return {};
}

[[nodiscard]] bool UIContext::Impl::hasValidMenuRelationship(UINodeId menu,
                                            UINodeId anchor) const noexcept
{
    if (!contains(menu) || !contains(anchor))
    {
        return false;
    }
    const NodeRecord* menuRecord = nodes.tryGet(menu.storageId());
    const NodeRecord* anchorRecord = nodes.tryGet(anchor.storageId());
    return menuRecord != nullptr && anchorRecord != nullptr &&
           menuRecord->kind == BuiltinElementKind::Menu &&
           menuRecord->rootIndex == anchorRecord->rootIndex &&
           menuStorage.hasRelationship(menu, anchor);
}

[[nodiscard]] std::pair<UINodeId, UINodeId>
UIContext::Impl::contextMenuForTarget(UINodeId target) const noexcept
{
    if (!contains(target))
    {
        return {};
    }
    const auto& entries = committedHitBuffers[publishedHitBufferIndex];
    u32 entryIndex = findHitEntryIndex(target, entries);
    usize visited = 0;
    while (entryIndex < entries.size() && visited++ < entries.size())
    {
        const UICommittedHitEntry& entry = entries[entryIndex];
        const UINodeId anchor = entry.node;
        const UINodeId menu = menuStorage.menuForAnchor(anchor);
        if (isContextMenuInvocationCandidate(menu, anchor))
        {
            return {menu, anchor};
        }
        if (entry.parentEntryIndex == InvalidUIHitEntryIndex)
        {
            break;
        }
        entryIndex = entry.parentEntryIndex;
    }
    return {};
}

[[nodiscard]] bool UIContext::Impl::isContextMenuInvocationCandidate(
    UINodeId menu, UINodeId anchor) const noexcept
{
    return hasValidMenuRelationship(menu, anchor) &&
           isNodeEnabled(menu) && isNodeEnabled(anchor) &&
           isAuthoredTooltipNodeVisible(menu) &&
           isAuthoredTooltipNodeVisible(anchor) &&
           (!activeModalNode.hasValue() ||
            isNodeWithinSubtree(activeModalNode, anchor));
}

[[nodiscard]] Core::Status UIContext::Impl::validateMenuAnchor(UINodeId menu,
                                              UINodeId anchor)
{
    auto menuResult = resolveMenu(menu);
    if (!menuResult)
    {
        return Core::failure(menuResult.error());
    }
    auto anchorResult = resolveNode(anchor);
    if (!anchorResult)
    {
        return Core::failure(anchorResult.error());
    }
    const NodeRecord* menuRecord = *menuResult;
    const NodeRecord* anchorRecord = *anchorResult;
    if (menu == anchor)
    {
        return fail(UIErrorCode::InvalidParent, "UI Menu cannot anchor to itself");
    }
    if (menuRecord->rootIndex != anchorRecord->rootIndex)
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Menu and Anchor must belong to the same root");
    }
    if (isNodeWithinSubtree(menu, anchor) ||
        isNodeWithinSubtree(anchor, menu))
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Menu and Anchor cannot form an ancestor cycle");
    }
    switch (anchorRecord->kind)
    {
    case BuiltinElementKind::Root:
    case BuiltinElementKind::Popup:
    case BuiltinElementKind::Tooltip:
    case BuiltinElementKind::Menu:
    case BuiltinElementKind::MenuItem:
    case BuiltinElementKind::DropdownItem:
    case BuiltinElementKind::Modal:
    case BuiltinElementKind::ListViewItem:
    case BuiltinElementKind::TreeViewItem:
        return fail(UIErrorCode::InvalidParent,
                    "UI node kind is not a stable Menu Anchor");
    default:
        break;
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setMenuAnchorRelation(UINodeId menu,
                                                 UINodeId anchor)
{
    if (Core::Status valid = validateMenuAnchor(menu, anchor); !valid)
    {
        return valid;
    }
    if (menuStorage.parentItemForMenu(menu).hasValue())
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Submenu must be detached from its parent MenuItem before assigning an Anchor");
    }
    const UINodeId reverse = menuStorage.menuForAnchor(anchor);
    if (reverse.hasValue() && reverse != menu &&
        hasValidMenuRelationship(reverse, anchor))
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Anchor already owns a Menu relationship");
    }
    if (menuStorage.anchorForMenu(menu) == anchor && reverse == menu)
    {
        return Core::success();
    }
    if (Core::Status dirty = markMenuMutationLayoutDirty(
            {menu, anchor}, {menu});
        !dirty)
    {
        return dirty;
    }
    static_cast<void>(menuStorage.close(menu));
    menuStorage.linkAnchorValidated(menu, anchor);
    menuCommandPressLatch.clear();
    transientOverlayDismissPointerBarrierActive = false;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::clearMenuAnchorRelation(UINodeId menu)
{
    auto menuResult = resolveMenu(menu);
    if (!menuResult)
    {
        return Core::failure(menuResult.error());
    }
    if (!menuStorage.anchorForMenu(menu).hasValue())
    {
        return Core::success();
    }
    if (Core::Status dirty = markMenuMutationLayoutDirty(
            {menu}, {menu});
        !dirty)
    {
        return dirty;
    }
    static_cast<void>(menuStorage.unlinkMenu(menu));
    menuCommandPressLatch.clear();
    transientOverlayDismissPointerBarrierActive = false;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::validateMenuItemSubmenu(
    UINodeId item, UINodeId submenu)
{
    auto itemResult = resolveMenuItem(item);
    if (!itemResult)
    {
        return Core::failure(itemResult.error());
    }
    auto submenuResult = resolveMenu(submenu);
    if (!submenuResult)
    {
        return Core::failure(submenuResult.error());
    }
    const MenuItemState* itemState = menuStorage.tryItem(item);
    if (itemState == nullptr ||
        itemState->config.kind != UIMenuItemKind::Submenu)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI submenu relationship requires a Submenu MenuItem");
    }
    const UINodeId parentMenu = menuStorage.menuForItem(item);
    if (!parentMenu.hasValue() || parentMenu == submenu)
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI submenu cannot reference its parent Menu");
    }
    if ((*itemResult)->rootIndex != (*submenuResult)->rootIndex)
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Submenu and parent MenuItem must belong to the same root");
    }
    if (menuStorage.anchorForMenu(submenu).hasValue())
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Menu with an ordinary Anchor cannot also be a Submenu");
    }
    const UINodeId currentParentItem =
        menuStorage.parentItemForMenu(submenu);
    if (currentParentItem.hasValue() && currentParentItem != item)
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Submenu is already owned by another MenuItem");
    }

    UINodeId ancestor = parentMenu;
    usize visited = 0;
    while (ancestor.hasValue() && visited++ < menuStorage.capacity())
    {
        if (ancestor == submenu)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI submenu relationships cannot form a cycle");
        }
        ancestor = menuStorage.parentMenu(ancestor);
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setMenuItemSubmenuRelation(
    UINodeId item, UINodeId submenu)
{
    if (Core::Status valid = validateMenuItemSubmenu(item, submenu); !valid)
    {
        return valid;
    }
    const UINodeId previousSubmenu = menuStorage.submenuForItem(item);
    if (previousSubmenu == submenu &&
        menuStorage.parentItemForMenu(submenu) == item)
    {
        return Core::success();
    }
    if (Core::Status dirty = markMenuMutationLayoutDirty(
            {item, submenu, previousSubmenu},
            {previousSubmenu, submenu});
        !dirty)
    {
        return dirty;
    }
    menuStorage.linkSubmenuValidated(item, submenu);
    menuCommandPressLatch.clear();
    transientOverlayDismissPointerBarrierActive = false;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::clearMenuItemSubmenuRelation(UINodeId item)
{
    auto itemResult = resolveMenuItem(item);
    if (!itemResult)
    {
        return Core::failure(itemResult.error());
    }
    const UINodeId submenu = menuStorage.submenuForItem(item);
    if (!submenu.hasValue())
    {
        return Core::success();
    }
    if (Core::Status dirty = markMenuMutationLayoutDirty(
            {item, submenu}, {submenu});
        !dirty)
    {
        return dirty;
    }
    static_cast<void>(menuStorage.unlinkSubmenuItem(item));
    menuCommandPressLatch.clear();
    transientOverlayDismissPointerBarrierActive = false;
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setMenuOpenState(
    UINodeId menu, bool open,
    const UILogicalPoint* invocationPoint)
{
    auto menuResult = resolveMenu(menu);
    if (!menuResult)
    {
        return Core::failure(menuResult.error());
    }
    MenuState* state = menuStorage.tryMenu(menu);
    const UINodeId anchor = menuPlacementAnchor(menu);
    const UINodeId parentItem = menuStorage.parentItemForMenu(menu);
    const UINodeId parentMenu = menuStorage.parentMenu(menu);
    const bool submenu = parentItem.hasValue();
    const bool useInvocationAnchor =
        open && !submenu && invocationPoint != nullptr;
    const UILogicalRect requestedInvocationAnchor =
        useInvocationAnchor
            ? UILogicalRect{
                  .x = invocationPoint->x,
                  .y = invocationPoint->y,
              }
            : UILogicalRect{};
    const bool invocationAnchorMatches =
        useInvocationAnchor
            ? menuStorage.hasInvocationAnchorRect(menu) &&
                  menuStorage.invocationAnchorRect(menu) ==
                      requestedInvocationAnchor
            : !menuStorage.hasInvocationAnchorRect(menu);
    const bool menuWasOpen = menuStorage.isOpen(menu);
    if (state == nullptr ||
        !hasValidMenuPlacementRelationship(menu, anchor))
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI Menu requires a live Anchor or parent MenuItem before it can open");
    }
    if (open && (!isNodeEnabled(menu) || !isNodeEnabled(anchor) ||
                 !isAuthoredTooltipNodeVisible(menu) ||
                 !isAuthoredTooltipNodeVisible(anchor) ||
                 (submenu && !menuStorage.isOpen(parentMenu)) ||
                 (activeModalNode.hasValue() &&
                  !isNodeWithinSubtree(activeModalNode, anchor))))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI Menu and Anchor must be enabled, visible, and inside the active Modal scope");
    }
    if ((open && menuWasOpen && invocationAnchorMatches) ||
        (!open && !state->open && !menuStorage.isInActiveChain(menu) &&
         !menuStorage.hasInvocationAnchorRect(menu)))
    {
        return Core::success();
    }

    UINodeId branchToClose{};
    if (open)
    {
        if (submenu)
        {
            branchToClose = menuStorage.activeChildMenu(parentMenu);
        } else
        {
            const UINodeId previousRoot = menuStorage.rootMenu();
            branchToClose =
                previousRoot != menu
                    ? previousRoot
                    : !invocationAnchorMatches
                          ? menuStorage.activeChildMenu(menu)
                          : UINodeId{};
        }
    } else if (menuStorage.isInActiveChain(menu))
    {
        branchToClose = menu;
    }
    const UINodeId previousPopup = open ? activePopup() : UINodeId{};
    const UINodeId previousDropdown = dropdownForPopup(previousPopup);
    if (Core::Status dirty = markMenuMutationLayoutDirty(
            {menu, anchor, previousPopup, previousDropdown},
            {branchToClose});
        !dirty)
    {
        return dirty;
    }

    const bool focusWasInClosingOverlay =
        (branchToClose.hasValue() &&
         isNodeWithinActiveMenuBranch(branchToClose,
                                      defaultActionFocusButton)) ||
        (previousPopup.hasValue() &&
         isNodeWithinSubtree(previousPopup, defaultActionFocusButton));
    if (open)
    {
        if (!menuWasOpen)
        {
            focusRestoreByNodeIndex[menu.index()] = defaultActionFocusButton;
        }
        if (submenu)
        {
            menuStorage.clearInvocationAnchorRect(menu);
            static_cast<void>(menuStorage.openSubmenuValidated(menu));
        } else
        {
            if (useInvocationAnchor)
            {
                menuStorage.setInvocationAnchorRectValidated(
                    menu, requestedInvocationAnchor);
            } else
            {
                menuStorage.clearInvocationAnchorRect(menu);
            }
            static_cast<void>(menuStorage.openRootValidated(menu));
        }
        if (previousPopup.hasValue())
        {
            popupStatesByNodeIndex[previousPopup.index()].open = false;
            activePopupNode = {};
            dropdownCommandPressLatch.clear();
        }
    } else
    {
        static_cast<void>(menuStorage.close(menu));
    }
    transientOverlayDismissPointerBarrierActive = false;
    menuCommandPressLatch.clear();

    if (focusWasInClosingOverlay)
    {
        UINodeId nextFocus = open ? anchor
                                  : submenu ? parentItem
                                            : focusRestoreByNodeIndex[menu.index()];
        if (!nextFocus.hasValue() || !contains(nextFocus) || !isNodeEnabled(nextFocus))
        {
            nextFocus = isNodeEnabled(anchor) ? anchor : UINodeId{};
        }
        defaultActionPressState.clearAll();
        clearAllPointerArms();
        resetImeCompositionState();
        textInputFocus = {};
        defaultActionFocusButton = nextFocus;
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setMenuItemCheckedState(UINodeId item,
                                                   bool checked)
{
    auto itemResult = resolveMenuItem(item);
    if (!itemResult)
    {
        return Core::failure(itemResult.error());
    }
    MenuItemState* itemState = menuStorage.tryItem(item);
    if (itemState == nullptr ||
        (itemState->config.kind != UIMenuItemKind::Check &&
         itemState->config.kind != UIMenuItemKind::Radio))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI MenuItem checked state requires a Check or Radio item");
    }
    if (itemState->config.kind == UIMenuItemKind::Radio && !checked)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI Radio MenuItem cannot be cleared without selecting a peer");
    }
    const UINodeId menu = menuStorage.menuForItem(item);
    if (!menu.hasValue())
    {
        return fail(UIErrorCode::InvalidParent,
                    "UI MenuItem is detached from its Menu");
    }
    if (itemState->checked == checked)
    {
        return Core::success();
    }
    if (Core::Status dirty = markPaintDirty(menu); !dirty)
    {
        return dirty;
    }
    if (itemState->config.kind == UIMenuItemKind::Radio)
    {
        const NodeRecord* menuRecord = nodes.tryGet(menu.storageId());
        u32 childIndex = menuRecord != nullptr ? menuRecord->firstChildIndex
                                              : InvalidNodeIndex;
        while (childIndex != InvalidNodeIndex)
        {
            const NodeRecord* child = recordByIndex(childIndex);
            if (child == nullptr)
            {
                break;
            }
            const u32 next = child->nextSiblingIndex;
            MenuItemState* peer = menuStorage.tryItem(idForIndex(childIndex));
            if (peer != nullptr && peer->config.kind == UIMenuItemKind::Radio &&
                peer->config.radioGroup == itemState->config.radioGroup)
            {
                static_cast<void>(menuStorage.setItemChecked(peer->node,
                                                            peer->node == item));
            }
            childIndex = next;
        }
        return Core::success();
    }
    static_cast<void>(menuStorage.setItemChecked(item, checked));
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setMenuAnchor(UINodeId menu, UINodeId anchor)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    return setMenuAnchorRelation(menu, anchor);
}

[[nodiscard]] Core::Status UIContext::Impl::clearMenuAnchor(UINodeId menu)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    return clearMenuAnchorRelation(menu);
}

[[nodiscard]] Core::Result<UINodeId> UIContext::Impl::menuAnchor(UINodeId menu) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    auto menuResult = const_cast<Impl*>(this)->resolveMenu(menu);
    if (!menuResult)
    {
        return Core::failure(menuResult.error());
    }
    const UINodeId anchor = menuStorage.anchorForMenu(menu);
    return hasValidMenuRelationship(menu, anchor) ? anchor : UINodeId{};
}

[[nodiscard]] Core::Status UIContext::Impl::setMenuOpen(UINodeId menu, bool open)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    return setMenuOpenState(menu, open);
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isMenuOpen(UINodeId menu) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    auto menuResult = const_cast<Impl*>(this)->resolveMenu(menu);
    if (!menuResult)
    {
        return Core::failure(menuResult.error());
    }
    return menuStorage.isOpen(menu);
}

[[nodiscard]] Core::Result<UIMenuMetrics> UIContext::Impl::menuMetrics(UINodeId menu) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    auto menuResult = const_cast<Impl*>(this)->resolveMenu(menu);
    if (!menuResult)
    {
        return Core::failure(menuResult.error());
    }
    return menuStorage.committedMetrics(menu);
}

[[nodiscard]] Core::Status UIContext::Impl::setMenuItemSubmenu(
    UINodeId item, UINodeId submenu)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    return setMenuItemSubmenuRelation(item, submenu);
}

[[nodiscard]] Core::Status UIContext::Impl::clearMenuItemSubmenu(UINodeId item)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    return clearMenuItemSubmenuRelation(item);
}

[[nodiscard]] Core::Result<UINodeId> UIContext::Impl::menuItemSubmenu(
    UINodeId item) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    auto itemResult = const_cast<Impl*>(this)->resolveMenuItem(item);
    if (!itemResult)
    {
        return Core::failure(itemResult.error());
    }
    const MenuItemState* state = menuStorage.tryItem(item);
    if (state == nullptr || state->config.kind != UIMenuItemKind::Submenu)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI submenu query requires a Submenu MenuItem");
    }
    const UINodeId submenu = menuStorage.submenuForItem(item);
    return hasValidSubmenuRelationship(item, submenu)
               ? submenu
               : UINodeId{};
}

[[nodiscard]] Core::Result<UINodeId> UIContext::Impl::menuParentItem(
    UINodeId menu) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    auto menuResult = const_cast<Impl*>(this)->resolveMenu(menu);
    if (!menuResult)
    {
        return Core::failure(menuResult.error());
    }
    const UINodeId item = menuStorage.parentItemForMenu(menu);
    return hasValidSubmenuRelationship(item, menu) ? item : UINodeId{};
}

[[nodiscard]] Core::Status UIContext::Impl::setMenuItemChecked(UINodeId item, bool checked)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    return setMenuItemCheckedState(item, checked);
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isMenuItemChecked(UINodeId item) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    auto itemResult = const_cast<Impl*>(this)->resolveMenuItem(item);
    if (!itemResult)
    {
        return Core::failure(itemResult.error());
    }
    const MenuItemState* state = menuStorage.tryItem(item);
    if (state == nullptr ||
        (state->config.kind != UIMenuItemKind::Check &&
         state->config.kind != UIMenuItemKind::Radio))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI MenuItem checked state requires a Check or Radio item");
    }
    return state->checked;
}

[[nodiscard]] Core::Status UIContext::Impl::validateMenuUpdaterRoot(UINodeId updaterRoot,
                                                   UINodeId menu) const
{
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired,
                    "UI tree updater requires a live root owner");
    }
    auto menuResult = const_cast<Impl*>(this)->resolveMenu(menu);
    if (!menuResult)
    {
        return Core::failure(menuResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, menu))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI Menu is not owned by the updater root");
    }
    return Core::success();
}

[[nodiscard]] Core::Status UIContext::Impl::setMenuAnchorFromUpdater(
    UINodeId updaterRoot, UINodeId menu, UINodeId anchor)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateMenuUpdaterRoot(updaterRoot, menu); !valid)
    {
        return valid;
    }
    if (!contains(anchor) || !isNodeWithinRoot(updaterRoot, anchor))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI Menu Anchor is not owned by the updater root");
    }
    return setMenuAnchorRelation(menu, anchor);
}

[[nodiscard]] Core::Status UIContext::Impl::clearMenuAnchorFromUpdater(
    UINodeId updaterRoot, UINodeId menu)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateMenuUpdaterRoot(updaterRoot, menu); !valid)
    {
        return valid;
    }
    return clearMenuAnchorRelation(menu);
}

[[nodiscard]] Core::Result<UINodeId> UIContext::Impl::menuAnchorFromUpdater(
    UINodeId updaterRoot, UINodeId menu) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateMenuUpdaterRoot(updaterRoot, menu); !valid)
    {
        return Core::failure(valid.error());
    }
    const UINodeId anchor = menuStorage.anchorForMenu(menu);
    return hasValidMenuRelationship(menu, anchor) ? anchor : UINodeId{};
}

[[nodiscard]] Core::Status UIContext::Impl::setMenuOpenFromUpdater(
    UINodeId updaterRoot, UINodeId menu, bool open)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (Core::Status valid = validateMenuUpdaterRoot(updaterRoot, menu); !valid)
    {
        return valid;
    }
    return setMenuOpenState(menu, open);
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isMenuOpenFromUpdater(
    UINodeId updaterRoot, UINodeId menu) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateMenuUpdaterRoot(updaterRoot, menu); !valid)
    {
        return Core::failure(valid.error());
    }
    return menuStorage.isOpen(menu);
}

[[nodiscard]] Core::Result<UIMenuMetrics> UIContext::Impl::menuMetricsFromUpdater(
    UINodeId updaterRoot, UINodeId menu) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateMenuUpdaterRoot(updaterRoot, menu); !valid)
    {
        return Core::failure(valid.error());
    }
    return menuStorage.committedMetrics(menu);
}

[[nodiscard]] Core::Status UIContext::Impl::setMenuItemSubmenuFromUpdater(
    UINodeId updaterRoot, UINodeId item, UINodeId submenu)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired,
                    "UI tree updater requires a live root owner");
    }
    if (!contains(item) || !contains(submenu) ||
        !isNodeWithinRoot(updaterRoot, item) ||
        !isNodeWithinRoot(updaterRoot, submenu))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI MenuItem and Submenu must be owned by the updater root");
    }
    return setMenuItemSubmenuRelation(item, submenu);
}

[[nodiscard]] Core::Status UIContext::Impl::clearMenuItemSubmenuFromUpdater(
    UINodeId updaterRoot, UINodeId item)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired,
                    "UI tree updater requires a live root owner");
    }
    if (!contains(item) || !isNodeWithinRoot(updaterRoot, item))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI MenuItem is not owned by the updater root");
    }
    return clearMenuItemSubmenuRelation(item);
}

[[nodiscard]] Core::Result<UINodeId> UIContext::Impl::menuItemSubmenuFromUpdater(
    UINodeId updaterRoot, UINodeId item) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired,
                    "UI tree updater requires a live root owner");
    }
    if (!contains(item) || !isNodeWithinRoot(updaterRoot, item))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI MenuItem is not owned by the updater root");
    }
    return menuItemSubmenu(item);
}

[[nodiscard]] Core::Result<UINodeId> UIContext::Impl::menuParentItemFromUpdater(
    UINodeId updaterRoot, UINodeId menu) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (Core::Status valid = validateMenuUpdaterRoot(updaterRoot, menu); !valid)
    {
        return Core::failure(valid.error());
    }
    return menuParentItem(menu);
}

[[nodiscard]] Core::Status UIContext::Impl::setMenuItemCheckedFromUpdater(
    UINodeId updaterRoot, UINodeId item, bool checked)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired,
                    "UI tree updater requires a live root owner");
    }
    if (!isNodeWithinRoot(updaterRoot, item))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI MenuItem is not owned by the updater root");
    }
    return setMenuItemCheckedState(item, checked);
}

[[nodiscard]] Core::Result<bool> UIContext::Impl::isMenuItemCheckedFromUpdater(
    UINodeId updaterRoot, UINodeId item) const
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (!updaterRoot.hasValue() || !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired,
                    "UI tree updater requires a live root owner");
    }
    auto itemResult = const_cast<Impl*>(this)->resolveMenuItem(item);
    if (!itemResult)
    {
        return Core::failure(itemResult.error());
    }
    if (!isNodeWithinRoot(updaterRoot, item))
    {
        return fail(UIErrorCode::InvalidNode,
                    "UI MenuItem is not owned by the updater root");
    }
    const MenuItemState* state = menuStorage.tryItem(item);
    if (state == nullptr ||
        (state->config.kind != UIMenuItemKind::Check &&
         state->config.kind != UIMenuItemKind::Radio))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI MenuItem checked state requires a Check or Radio item");
    }
    return state->checked;
}

} // namespace Tina::UI
