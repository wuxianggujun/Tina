#include "detail/UIContextImpl.hpp"

namespace Tina::UI {

[[nodiscard]] UIPointerHitQueryResult UIContext::Impl::queryPointerHit(UILogicalPoint point) const noexcept
{
    return Detail::queryCommittedPointerHit(committedHit(), point);
}

[[nodiscard]] Core::Result<std::pair<u32, u32>> UIContext::Impl::addRoutedPointerListener(UIRoutedPointerListenerDesc descriptor,
                                                                         UIRoutedPointerCallback&& callback,
                                                                         UINodeId updaterRoot)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    drainDeferredRootDestroys();
    if (updaterRoot.hasValue() && !contains(updaterRoot))
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
    }
    auto nodeResult = resolveNode(descriptor.node);
    if (!nodeResult)
    {
        return Core::failure(nodeResult.error());
    }
    if (updaterRoot.hasValue() && !isNodeWithinRoot(updaterRoot, descriptor.node))
    {
        return fail(UIErrorCode::InvalidNode, "UI routed pointer listener node is not owned by the updater root");
    }
    if (!isValidRoutedPointerEventKind(descriptor.kind) || !isValidEventPhaseMask(descriptor.phases) ||
        !callback.hasValue())
    {
        return fail(UIErrorCode::InvalidRoutedPointerListener,
                    "UI routed pointer listener descriptor or callback is invalid");
    }
    auto registrationResult = routedPointerListenerRegistry.stage(
        descriptor, std::move(callback), routeDispatchDepth != 0);
    if (!registrationResult)
    {
        return Core::failure(registrationResult.error());
    }
    const Detail::UIRoutedPointerListenerRegistration registration = *registrationResult;
    auto rollbackRegistration = [this, &registration](Core::Error error)
        -> Core::Result<std::pair<u32, u32>> {
        routedPointerListenerRegistry.rollback(registration, routeDispatchDepth != 0);
        return Core::failure(std::move(error));
    };

    // Moving a valid fixed-inline callable may execute user move/destructor
    // code. Revalidate generation ownership before publishing the slot.
    if (updaterRoot.hasValue() && !contains(updaterRoot))
    {
        return rollbackRegistration(
            makeError(UIErrorCode::RootRequired,
                      "UI tree updater root was released while registering a routed pointer listener"));
    }
    auto liveNodeResult = resolveNode(descriptor.node);
    if (!liveNodeResult)
    {
        return rollbackRegistration(liveNodeResult.error());
    }
    if (updaterRoot.hasValue() && !isNodeWithinRoot(updaterRoot, descriptor.node))
    {
        return rollbackRegistration(
            makeError(UIErrorCode::InvalidNode,
                      "UI routed pointer listener node left the updater root during registration"));
    }
    Core::Status commitStatus = routedPointerListenerRegistry.commit(
        registration, routedPointerListenerStatePublisher(), routeDispatchDepth != 0);
    if (!commitStatus)
    {
        return rollbackRegistration(commitStatus.error());
    }
    return std::pair<u32, u32>{registration.listenerIndex, registration.generation};
}

[[nodiscard]] Core::Result<std::pair<u32, u32>>
UIContext::Impl::addRoutedPointerListenerFromUpdater(UINodeId updaterRoot, UIRoutedPointerListenerDesc descriptor,
                                    UIRoutedPointerCallback&& callback)
{
    if (!updaterRoot.hasValue())
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
    }
    return addRoutedPointerListener(descriptor, std::move(callback), updaterRoot);
}

[[nodiscard]] Core::Status UIContext::Impl::validatePointerInput(const UIPointerInputEvent& input) const
{
    if (!input.platformFrame.hasValue() || input.sourceSequence == 0)
    {
        return fail(UIErrorCode::InvalidPointerInput,
                    "UI pointer input requires a platform frame and source sequence");
    }
    if (!input.window.hasValue())
    {
        return fail(UIErrorCode::InvalidPointerInput, "UI pointer input owner window is empty");
    }
    if (input.window != ownerWindow)
    {
        return fail(UIErrorCode::WrongOwnerWindow, "UI pointer input belongs to another owner window");
    }
    if (input.pointer >= Platform::PointerCapacity || !isValidRoutedPointerEventKind(input.kind) ||
        input.kind == UIRoutedPointerEventKind::PointerCancel || !std::isfinite(input.position.x) ||
        !std::isfinite(input.position.y) || !std::isfinite(input.delta.x) || !std::isfinite(input.delta.y))
    {
        return fail(UIErrorCode::InvalidPointerInput,
                    "UI pointer input kind, identity, position, or delta is invalid or synthetic-only");
    }
    if ((input.kind == UIRoutedPointerEventKind::ButtonDown || input.kind == UIRoutedPointerEventKind::ButtonUp) &&
        input.button >= Platform::PointerButton::Count)
    {
        return fail(UIErrorCode::InvalidPointerInput, "UI pointer button is invalid");
    }
    return Core::success();
}

void UIContext::Impl::dispatchRoutedPointerListeners(UINodeId node, UIEventPhase phase, UIRoutedPointerEventKind kind,
                                    u64 registrationSerialBoundary, UIRoutedPointerEvent& event,
                                    UIPointerRouteResult& result) noexcept
{
    if (!contains(node))
    {
        return;
    }
    Detail::UIRoutedPointerEventAccess::setRouteState(event, phase, node, result.routedTarget.node,
                                                      result.routedTarget.rootNode);
    result.listenerInvocationCount += routedPointerListenerRegistry.dispatch(
        node, kind, phaseMaskFor(phase), registrationSerialBoundary, event);
}

void UIContext::Impl::finishRoutedPointerDispatch() noexcept
{
    if (routeDispatchDepth == 0)
    {
        return;
    }
    --routeDispatchDepth;
    if (routeDispatchDepth != 0)
    {
        return;
    }
    drainDeferredRoutedPointerListenerReleases();
    reclaimInactiveRoutedPointerListeners();
    buttonActionRegistry.reclaim(false);
    sliderChangeCallbackRegistry.reclaim(false);
}

void UIContext::Impl::dispatchPointerCancelToCapture(std::span<const UICommittedHitEntry> entries) noexcept
{
    if (pointerCancelDispatchInProgress || !capturedPointerNode.hasValue() || !hasLastPointerInput)
    {
        return;
    }

    const UINodeId targetNode = capturedPointerNode;
    const u32 targetEntryIndex = findHitEntryIndex(targetNode, entries);
    if (!contains(targetNode) || targetEntryIndex >= entries.size() ||
        entries[targetEntryIndex].rootEntryIndex >= entries.size())
    {
        capturedPointerNode = {};
        return;
    }

    pointerCancelRoutePathScratch.clear();
    u32 entryIndex = targetEntryIndex;
    while (entryIndex != InvalidUIHitEntryIndex)
    {
        if (entryIndex >= entries.size() ||
            pointerCancelRoutePathScratch.size() >= capacityConfig.routePathCapacity)
        {
            pointerCancelRoutePathScratch.clear();
            capturedPointerNode = {};
            return;
        }
        pointerCancelRoutePathScratch.push_back(entryIndex);
        if (entryIndex == entries[targetEntryIndex].rootEntryIndex)
        {
            break;
        }
        entryIndex = entries[entryIndex].parentEntryIndex;
    }
    if (pointerCancelRoutePathScratch.empty() ||
        pointerCancelRoutePathScratch.back() != entries[targetEntryIndex].rootEntryIndex)
    {
        pointerCancelRoutePathScratch.clear();
        capturedPointerNode = {};
        return;
    }

    const UICommittedHitEntry& targetEntry = entries[targetEntryIndex];
    const UICommittedHitEntry& rootEntry = entries[targetEntry.rootEntryIndex];
    UIPointerRouteResult result{
        .routedTarget =
            {
                .node = targetEntry.node,
                .rootNode = rootEntry.node,
                .hitEntryIndex = targetEntryIndex,
                .rootEntryIndex = targetEntry.rootEntryIndex,
                .worldRect = targetEntry.worldRect,
                .effectiveClip = targetEntry.effectiveClip,
                .paintOrdinal = targetEntry.paintOrdinal,
            },
        .routeDepth = pointerCancelRoutePathScratch.size(),
        .routedThroughPointerCapture = true,
    };
    UIPointerInputEvent cancelInput = lastPointerInput;
    cancelInput.kind = UIRoutedPointerEventKind::PointerCancel;
    cancelInput.delta = {};
    UIRoutedPointerEvent routedEvent = Detail::UIRoutedPointerEventAccess::Create(cancelInput);
    Detail::UIRoutedPointerEventAccess::setPointerCaptureRoute(routedEvent, true);
    const u64 registrationSerialBoundary = routedPointerListenerRegistry.registrationSerial();

    pointerCancelDispatchInProgress = true;
    ++routeDispatchDepth;
    auto dispatchCleanup = Core::makeScopeExit([this]() noexcept {
        capturedPointerNode = {};
        pointerCancelDispatchInProgress = false;
        pointerCancelRoutePathScratch.clear();
        finishRoutedPointerDispatch();
    });

    for (usize reversePathIndex = pointerCancelRoutePathScratch.size(); reversePathIndex > 1; --reversePathIndex)
    {
        if (!contains(targetNode))
        {
            return;
        }
        const UINodeId currentNode = entries[pointerCancelRoutePathScratch[reversePathIndex - 1]].node;
        if (contains(currentNode))
        {
            dispatchRoutedPointerListeners(currentNode, UIEventPhase::Capture,
                                           UIRoutedPointerEventKind::PointerCancel, registrationSerialBoundary,
                                           routedEvent, result);
        }
        if (routedEvent.isPropagationStopped())
        {
            return;
        }
    }

    if (!contains(targetNode))
    {
        return;
    }
    dispatchRoutedPointerListeners(targetNode, UIEventPhase::Target, UIRoutedPointerEventKind::PointerCancel,
                                   registrationSerialBoundary, routedEvent, result);
    if (routedEvent.isPropagationStopped() || !contains(targetNode))
    {
        return;
    }

    for (usize pathIndex = 1; pathIndex < pointerCancelRoutePathScratch.size(); ++pathIndex)
    {
        if (!contains(targetNode))
        {
            return;
        }
        const UINodeId currentNode = entries[pointerCancelRoutePathScratch[pathIndex]].node;
        if (contains(currentNode))
        {
            dispatchRoutedPointerListeners(currentNode, UIEventPhase::Bubble,
                                           UIRoutedPointerEventKind::PointerCancel, registrationSerialBoundary,
                                           routedEvent, result);
        }
        if (routedEvent.isPropagationStopped())
        {
            return;
        }
    }
}

void UIContext::Impl::dispatchPointerCancelForCurrentCapture() noexcept
{
    const auto& entries = committedHitBuffers[publishedHitBufferIndex];
    dispatchPointerCancelToCapture(entries);
}

void UIContext::Impl::dispatchPointerCancelForSubtree(UINodeId subtreeRoot) noexcept
{
    if (pointerCancelDispatchInProgress)
    {
        return;
    }

    savePointerInteractionState(activePointerState);
    const Platform::PointerId originalPointer = activePointerState;
    for (Platform::PointerId pointer = 0; pointer < Platform::PointerCapacity; ++pointer)
    {
        loadPointerInteractionState(pointer);
        if (isNodeWithinSubtree(subtreeRoot, capturedPointerNode))
        {
            dispatchPointerCancelForCurrentCapture();
        }
        savePointerInteractionState(pointer);
    }
    restorePointerInteractionState(pointerInteractionStates[originalPointer]);
    activePointerState = originalPointer;
}

[[nodiscard]] Core::Result<UIPointerRouteResult> UIContext::Impl::routePointerInput(const UIPointerInputEvent& input)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress, "UI pointer routing is already in progress");
    }
    drainDeferredRootDestroys();
    if (Core::Status inputStatus = validatePointerInput(input); !inputStatus)
    {
        return Core::failure(inputStatus.error());
    }
    if (Core::Status modality = setInputModality(UIInputModality::Pointer); !modality)
    {
        return Core::failure(modality.error());
    }
    loadPointerInteractionState(input.pointer);
    auto pointerStateCleanup = Core::makeScopeExit([this, pointer = input.pointer]() noexcept {
        savePointerInteractionState(pointer);
        if (pointer != Platform::PrimaryPointerId)
        {
            loadPointerInteractionState(Platform::PrimaryPointerId);
        }
    });
    lastPointerInput = input;
    hasLastPointerInput = true;

    UIPointerRouteResult result{
        .pointQuery = queryPointerHit(input.position),
    };
    const UICommittedHitView hit = committedHit();
    const std::span<const UICommittedHitEntry> entries = hit.entries();
    const UINodeId physicalTarget = result.pointQuery.target.node;
    const UINodeId menuAtRouteStart = menuStorage.rootMenu();
    const UINodeId menuAnchorAtRouteStart = menuPlacementAnchor(menuAtRouteStart);
    bool pointTargetsActiveMenu = false;
    UINodeId activeMenuSurfaceAtPointer{};
    UINodeId activeChainMenuAtRouteStart = menuAtRouteStart;
    usize activeChainVisitCount = 0;
    while (activeChainMenuAtRouteStart.hasValue() &&
           activeChainVisitCount++ < menuStorage.capacity())
    {
        const u32 entryIndex =
            findHitEntryIndex(activeChainMenuAtRouteStart, entries);
        if (entryIndex < entries.size() &&
            containsPointHalfOpen(entries[entryIndex].worldRect,
                                  input.position) &&
            containsPointHalfOpen(entries[entryIndex].effectiveClip,
                                  input.position))
        {
            activeMenuSurfaceAtPointer = activeChainMenuAtRouteStart;
        }
        pointTargetsActiveMenu = pointTargetsActiveMenu ||
            isNodeWithinSubtree(activeChainMenuAtRouteStart,
                                physicalTarget);
        activeChainMenuAtRouteStart =
            menuStorage.activeChildMenu(activeChainMenuAtRouteStart);
    }
    const bool pointInsideActiveMenu =
        activeMenuSurfaceAtPointer.hasValue();
    const bool activeMenuChromeOccludesPhysicalTarget =
        pointInsideActiveMenu &&
        !isNodeWithinSubtree(activeMenuSurfaceAtPointer, physicalTarget);
    const UINodeId captureAtRouteStart = capturedPointerNode;
    const u32 capturedEntryIndex = findHitEntryIndex(capturedPointerNode, entries);
    if (capturedEntryIndex < entries.size() &&
        isPointerCaptureCandidate(capturedPointerNode, entries, committedActiveModalEntryIndex))
    {
        result.routedTarget = pointerHitTargetForEntry(entries, capturedEntryIndex);
        result.routedThroughPointerCapture = result.routedTarget.hasValue();
    } else
    {
        if (capturedPointerNode.hasValue())
        {
            dispatchPointerCancelToCapture(entries);
        }
        if (activeMenuChromeOccludesPhysicalTarget)
        {
            result.routedTarget = pointerHitTargetForEntry(
                entries,
                findHitEntryIndex(activeMenuSurfaceAtPointer, entries));
        }
        else
        {
            result.routedTarget = result.pointQuery.target;
        }
    }
    result.blockedByModal = result.pointQuery.modalBarrierActive && !result.pointQuery.hasTarget();
    routePathScratch.clear();
    if (result.routedTarget.hasValue())
    {
        if (!contains(result.routedTarget.node))
        {
            result.targetInvalidated = true;
        } else
        {
            const auto routePath = buildPointerRoutePath(
                result.routedTarget, entries,
                capacityConfig.routePathCapacity, routePathScratch);
            switch (routePath.error)
            {
            case UIPointerRoutePathError::None:
                result.routeDepth = routePath.depth;
                break;
            case UIPointerRoutePathError::InvalidEntryIndex:
                return fail(Core::CoreErrorCode::Internal,
                            "UI committed pointer route entry index is invalid");
            case UIPointerRoutePathError::CapacityExceeded:
                return fail(UIErrorCode::CapacityExceeded,
                            "UI pointer route path capacity has been exhausted");
            case UIPointerRoutePathError::AncestryCycle:
                return fail(Core::CoreErrorCode::Internal,
                            "UI committed pointer route ancestry contains a cycle");
            case UIPointerRoutePathError::InvalidRoot:
                return fail(Core::CoreErrorCode::Internal,
                            "UI committed pointer route root is invalid");
            }
        }
    }

    if (buttonRouteSerial == (std::numeric_limits<u64>::max)())
    {
        routePathScratch.clear();
        return fail(UIErrorCode::CapacityExceeded, "UI Button route serial is exhausted");
    }

    const auto routeInspection = inspectPointerRouteTargets(
        result.pointQuery.target, routePathScratch, entries,
        armedPrimaryButton,
        [this](UINodeId node) noexcept { return isNodeEnabled(node); });
    if (routeInspection.error ==
        UIPointerRouteInspectionError::PhysicalAncestryCycle)
    {
        return fail(Core::CoreErrorCode::Internal,
                    "UI committed physical pointer ancestry contains a cycle");
    }
    if (routeInspection.error ==
        UIPointerRouteInspectionError::InvalidRouteEntryIndex)
    {
        routePathScratch.clear();
        return fail(Core::CoreErrorCode::Internal,
                    "UI committed Button route entry index is invalid");
    }
    const UINodeId nearestButton = routeInspection.routedNearestActivatable;
    const UINodeId nearestSlider = routeInspection.routedNearestRangeInput;
    const bool pointWithinArmedButton =
        routeInspection.pointWithinArmedActivatable;
    const UINodeId physicalNearestButton =
        routeInspection.physicalNearestActivatable;

    const NodeRecord* nearestButtonRecord =
        nearestButton.hasValue() && contains(nearestButton) ? nodes.tryGet(nearestButton.storageId()) : nullptr;
    const UINodeId nearestListView =
        nearestButtonRecord != nullptr && nearestButtonRecord->kind == BuiltinElementKind::ListViewItem
            ? listViewForItem(nearestButton)
            : UINodeId{};
    const UINodeId nearestTreeView =
        nearestButtonRecord != nullptr && nearestButtonRecord->kind == BuiltinElementKind::TreeViewItem
            ? treeViewForItem(nearestButton)
            : UINodeId{};
    const UINodeId nearestVirtualGridView =
        nearestButtonRecord != nullptr &&
                nearestButtonRecord->kind ==
                    BuiltinElementKind::VirtualGridViewItem
            ? virtualGridViewForItem(nearestButton)
            : UINodeId{};
    const UINodeId nearestDataGrid =
        nearestButtonRecord != nullptr &&
                nearestButtonRecord->kind == BuiltinElementKind::DataGridCell
            ? dataGridForCell(nearestButton)
            : UINodeId{};
    const bool nearestTreeDisclosureAtRouteStart =
        nearestTreeView.hasValue() && pointWithinCommittedTreeDisclosure(nearestButton, input.position, entries);

    const UINodeId targetNode = result.routedTarget.node;
    const bool targetNodeEnabledAtRouteStart = isNodeEnabled(targetNode);
    const bool primaryButtonDown =
        input.kind == UIRoutedPointerEventKind::ButtonDown && input.button == Platform::PointerButton::Primary;
    const bool primaryButtonUp =
        input.kind == UIRoutedPointerEventKind::ButtonUp && input.button == Platform::PointerButton::Primary;
    const bool secondaryButtonDown =
        input.kind == UIRoutedPointerEventKind::ButtonDown &&
        input.button == Platform::PointerButton::Secondary;
    const bool secondaryButtonUp =
        input.kind == UIRoutedPointerEventKind::ButtonUp &&
        input.button == Platform::PointerButton::Secondary;
    const UINodeId popupAtRouteStart =
        activePopupNode.hasValue() && contains(activePopupNode) &&
                activePopupNode.index() < popupStatesByNodeIndex.size() &&
                popupStatesByNodeIndex[activePopupNode.index()].open
            ? activePopupNode
            : UINodeId{};
    const UINodeId popupDropdownAtRouteStart = dropdownForPopup(popupAtRouteStart);
    const u32 popupEntryIndexAtRouteStart = findHitEntryIndex(popupAtRouteStart, entries);
    const bool pointInsideActivePopup =
        popupEntryIndexAtRouteStart < entries.size() &&
        containsPointHalfOpen(entries[popupEntryIndexAtRouteStart].worldRect, input.position) &&
        containsPointHalfOpen(entries[popupEntryIndexAtRouteStart].effectiveClip, input.position);
    const auto [contextMenuAtRouteStart, contextMenuAnchorAtRouteStart] =
        secondaryButtonDown ? contextMenuForTarget(physicalTarget)
                            : std::pair<UINodeId, UINodeId>{};
    const bool pointTargetsActivePopup = isNodeWithinSubtree(popupAtRouteStart, physicalTarget);
    const bool pointTargetsPopupDropdown =
        isNodeWithinSubtree(popupDropdownAtRouteStart, physicalTarget) && !pointTargetsActivePopup;
    const bool dismissActivePopupOnPrimaryDown = primaryButtonDown && popupAtRouteStart.hasValue() &&
                                                 !pointInsideActivePopup && !pointTargetsPopupDropdown;
    const bool blockPopupChromeClickThrough = primaryButtonDown && popupAtRouteStart.hasValue() &&
                                              pointInsideActivePopup && !pointTargetsActivePopup;
    const bool pointTargetsMenuAnchor =
        isNodeWithinSubtree(menuAnchorAtRouteStart, physicalTarget) && !pointTargetsActiveMenu;
    const bool dismissActiveMenuOnPrimaryDown = primaryButtonDown && menuAtRouteStart.hasValue() &&
                                                !pointInsideActiveMenu && !pointTargetsMenuAnchor;
    const bool blockMenuChromeClickThrough = primaryButtonDown && menuAtRouteStart.hasValue() &&
                                             pointInsideActiveMenu && !pointTargetsActiveMenu;
    const bool dismissActiveMenuOnWheel = input.kind == UIRoutedPointerEventKind::Wheel &&
                                          menuAtRouteStart.hasValue();
    const MenuItemState* hoveredMenuItemAtRouteStart =
        input.kind == UIRoutedPointerEventKind::Move
            ? menuStorage.tryItem(physicalNearestButton)
            : nullptr;
    const UINodeId hoveredMenuAtRouteStart =
        hoveredMenuItemAtRouteStart != nullptr
            ? menuStorage.menuForItem(physicalNearestButton)
            : UINodeId{};
    const UINodeId hoverSubmenuToOpen =
        hoveredMenuItemAtRouteStart != nullptr &&
                hoveredMenuItemAtRouteStart->config.kind ==
                    UIMenuItemKind::Submenu &&
                menuStorage.isOpen(hoveredMenuAtRouteStart)
            ? menuStorage.submenuForItem(physicalNearestButton)
            : UINodeId{};
    const UINodeId hoverSubmenuToClose =
        hoveredMenuItemAtRouteStart != nullptr &&
                hoveredMenuItemAtRouteStart->config.kind !=
                    UIMenuItemKind::Submenu &&
                menuStorage.isOpen(hoveredMenuAtRouteStart)
            ? menuStorage.activeChildMenu(hoveredMenuAtRouteStart)
            : UINodeId{};
    const bool transientOverlayBarrierAtRouteStart = transientOverlayDismissPointerBarrierActive;
    const ScrollBarPointerHit scrollBarHitAtRouteStart =
        primaryButtonDown ? scrollBarPointerHit(routePathScratch, entries, input.position) : ScrollBarPointerHit{};
    const UINodeId armedButtonAtRouteStart = armedPrimaryButton;
    const UINodeId armedSliderAtRouteStart = armedSlider;
    const UINodeId armedScrollViewAtRouteStart = armedScrollView;
    const UIScrollAxes armedScrollAxisAtRouteStart = armedScrollAxis;
    const float scrollDragGrabOffsetAtRouteStart = scrollDragGrabOffset;
    const bool scrollThumbDragAtRouteStart = scrollThumbDragActive;
    const UINodeId armedTextEditAtRouteStart = armedTextEdit;
    const bool armedTreeDisclosureAtRouteStart = armedTreeDisclosure;
    const bool hadArmedInteraction = armedButtonAtRouteStart.hasValue();
    const bool hadArmedSlider = armedSliderAtRouteStart.hasValue();
    const bool hadArmedScrollView = armedScrollViewAtRouteStart.hasValue();
    const bool hadArmedTextEdit = armedTextEditAtRouteStart.hasValue();
    const bool pointWithinArmedTreeDisclosure =
        armedTreeDisclosureAtRouteStart &&
        pointWithinCommittedTreeDisclosure(armedButtonAtRouteStart, input.position, entries);
    releaseRouteDirtyQueueReservations();
    if (dismissActivePopupOnPrimaryDown)
    {
        addRouteLayoutDirtyReservationCandidates(popupAtRouteStart);
        addRouteLayoutDirtyReservationCandidates(popupDropdownAtRouteStart);
    }
    if (dismissActiveMenuOnPrimaryDown || dismissActiveMenuOnWheel)
    {
        addActiveMenuBranchDirtyReservationCandidates(menuAtRouteStart);
    }
    if (contextMenuAtRouteStart.hasValue())
    {
        addRouteLayoutDirtyReservationCandidates(contextMenuAtRouteStart);
        addRouteLayoutDirtyReservationCandidates(contextMenuAnchorAtRouteStart);
        addActiveMenuBranchDirtyReservationCandidates(menuAtRouteStart);
        addRouteLayoutDirtyReservationCandidates(popupAtRouteStart);
        addRouteLayoutDirtyReservationCandidates(popupDropdownAtRouteStart);
    }
    if (hoverSubmenuToOpen.hasValue())
    {
        addRouteLayoutDirtyReservationCandidates(hoverSubmenuToOpen);
        addRouteLayoutDirtyReservationCandidates(
            menuPlacementAnchor(hoverSubmenuToOpen));
        addActiveMenuBranchDirtyReservationCandidates(
            menuStorage.activeChildMenu(hoveredMenuAtRouteStart));
    }
    if (hoverSubmenuToClose.hasValue())
    {
        addActiveMenuBranchDirtyReservationCandidates(
            hoverSubmenuToClose);
    }
    const UINodeId hoverCandidate = activeMenuChromeOccludesPhysicalTarget
                                        ? UINodeId{}
                                        : physicalNearestButton.hasValue()
                                              ? physicalNearestButton
                                              : physicalTarget;
    const UINodeId nextHoveredControl = resolvedHoveredPrimaryControl(hoverCandidate);
    if (nextHoveredControl != hoveredPrimaryControl)
    {
        const UINodeId previousHover =
            hoveredPrimaryControl.hasValue() && contains(hoveredPrimaryControl) ? hoveredPrimaryControl : UINodeId{};
        addRouteDirtyReservationCandidate(previousHover);
        addRouteDirtyReservationCandidate(nextHoveredControl);
    }

    if (primaryButtonDown)
    {
        addRouteDirtyReservationCandidate(defaultActionFocusButton);
        addRouteDirtyReservationCandidate(textInputFocus);
        addRouteDirtyReservationCandidate(armedSliderAtRouteStart);
        addRouteDirtyReservationCandidate(armedScrollViewAtRouteStart);
        const NodeRecord* targetRecord =
            targetNode.hasValue() && contains(targetNode) ? nodes.tryGet(targetNode.storageId()) : nullptr;
        const NodeRecord* nearestSliderRecord =
            nearestSlider.hasValue() && contains(nearestSlider) ? nodes.tryGet(nearestSlider.storageId()) : nullptr;
        if (scrollBarHitAtRouteStart.hasValue())
        {
            addRouteDirtyReservationCandidate(scrollBarHitAtRouteStart.scrollView);
        } else if (targetRecord != nullptr && targetRecord->kind == BuiltinElementKind::TextEdit &&
                   targetNodeEnabledAtRouteStart)
        {
            addRouteDirtyReservationCandidate(targetNode);
        } else if (nearestSliderRecord != nullptr &&
                   isPointerAdjustableRangeInput(nearestSlider))
        {
            addRouteDirtyReservationCandidate(nearestSlider);
        } else if (nearestButton.hasValue() && isNodeEnabled(nearestButton))
        {
            addRouteDirtyReservationCandidate(nearestButton);
            addRouteDirtyReservationCandidate(nearestListView);
            addRouteDirtyReservationCandidate(nearestTreeView);
            addRouteDirtyReservationCandidate(nearestVirtualGridView);
            addRouteDirtyReservationCandidate(nearestDataGrid);
        }
    } else if (input.kind == UIRoutedPointerEventKind::Move)
    {
        if (hadArmedSlider)
        {
            addRouteDirtyReservationCandidate(armedSliderAtRouteStart);
        }
        if (hadArmedScrollView)
        {
            addRouteDirtyReservationCandidate(armedScrollViewAtRouteStart);
        }
        if (hadArmedTextEdit)
        {
            addRouteDirtyReservationCandidate(armedTextEditAtRouteStart);
        }
        if (hadArmedInteraction && armedPrimaryButtonPressed != pointWithinArmedButton)
        {
            addRouteDirtyReservationCandidate(armedButtonAtRouteStart);
        }
    } else if (primaryButtonUp)
    {
        if (hadArmedSlider)
        {
            addRouteDirtyReservationCandidate(armedSliderAtRouteStart);
        }
        if (hadArmedScrollView)
        {
            addRouteDirtyReservationCandidate(armedScrollViewAtRouteStart);
        }
        if (hadArmedTextEdit)
        {
            addRouteDirtyReservationCandidate(armedTextEditAtRouteStart);
        }
        if (hadArmedInteraction)
        {
            addRouteDirtyReservationCandidate(armedButtonAtRouteStart);
            const NodeRecord* armedRecord =
                contains(armedButtonAtRouteStart) ? nodes.tryGet(armedButtonAtRouteStart.storageId()) : nullptr;
            if (pointWithinArmedButton && isNodeEnabled(armedButtonAtRouteStart) && armedRecord != nullptr &&
                armedRecord->kind == BuiltinElementKind::RadioButton)
            {
                const NodeRecord* parent = recordByIndex(armedRecord->parentIndex);
                if (parent != nullptr)
                {
                    for (u32 childIndex = parent->firstChildIndex; childIndex != InvalidNodeIndex;)
                    {
                        const NodeRecord* child = recordByIndex(childIndex);
                        if (child == nullptr)
                        {
                            break;
                        }
                        const u32 nextSiblingIndex = child->nextSiblingIndex;
                        if (child->kind == BuiltinElementKind::RadioButton &&
                            childIndex < radioButtonStatesByNodeIndex.size() &&
                            radioButtonStatesByNodeIndex[childIndex].selected !=
                                (childIndex == armedButtonAtRouteStart.index()))
                        {
                            addRouteDirtyReservationCandidate(idForIndex(childIndex));
                        }
                        childIndex = nextSiblingIndex;
                    }
                }
            }
            if (pointWithinArmedButton && isNodeEnabled(armedButtonAtRouteStart))
            {
                addDropdownActivationDirtyReservationCandidates(armedButtonAtRouteStart);
                addMenuActivationDirtyReservationCandidates(armedButtonAtRouteStart);
                addTabActivationDirtyReservationCandidates(armedButtonAtRouteStart);
                addRouteDirtyReservationCandidate(listViewForItem(armedButtonAtRouteStart));
                addRouteDirtyReservationCandidate(
                    virtualGridViewForItem(armedButtonAtRouteStart));
                addRouteDirtyReservationCandidate(
                    dataGridForCell(armedButtonAtRouteStart));
                const UINodeId armedTreeView = treeViewForItem(armedButtonAtRouteStart);
                if (pointWithinArmedTreeDisclosure)
                {
                    addRouteLayoutDirtyReservationCandidates(armedTreeView);
                } else
                {
                    addRouteDirtyReservationCandidate(armedTreeView);
                }
            }
        }
    } else if (input.kind == UIRoutedPointerEventKind::Wheel)
    {
        for (const u32 routeEntryIndex : routePathScratch)
        {
            if (routeEntryIndex < entries.size())
            {
                const UINodeId routeNode = entries[routeEntryIndex].node;
                if (textEditWheelWouldChange(routeNode, input.delta) ||
                    scrollWheelWouldChange(routeNode, input.delta))
                {
                    addRouteDirtyReservationCandidate(routeNode);
                }
            }
        }
    }
    if (Core::Status reservation = reserveRouteDirtyQueueSlots(); !reservation)
    {
        if (primaryButtonUp && reservation.error().code == UIErrorCode::CapacityExceeded)
        {
            const UINodeId releasedButton = armedPrimaryButton;
            const UINodeId releasedSlider = armedSlider;
            const UINodeId releasedScrollView = armedScrollView;
            const UINodeId releasedTextEdit = armedTextEdit;
            clearArmedPrimaryButton();
            clearArmedSlider();
            clearArmedScrollView();
            clearArmedTextEdit();
            capturedPointerNode = {};
            // Primary Up is a release barrier even when queue capacity is
            // exhausted. Existing dirty work will rebuild paint globally;
            // otherwise this best-effort mark publishes the released state.
            static_cast<void>(markPaintDirtyBatch({
                releasedButton,
                releasedSlider,
                releasedScrollView,
                releasedTextEdit,
            }));
        }
        releaseRouteDirtyQueueReservations();
        return Core::failure(reservation.error());
    }
    auto reservationCleanup = Core::makeScopeExit([this]() noexcept { releaseRouteDirtyQueueReservations(); });
    const u64 actionRegistrationSerialBoundary = buttonActionRegistry.registrationSerial();
    const Detail::UIButtonActionInvocation actionCandidate =
        primaryButtonUp && hadArmedInteraction && pointWithinArmedButton &&
                isNodeEnabled(armedButtonAtRouteStart) &&
                !isSubmenuMenuItem(armedButtonAtRouteStart)
            ? captureButtonAction(armedButtonAtRouteStart, actionRegistrationSerialBoundary)
            : Detail::UIButtonActionInvocation{};
    const u64 currentButtonRouteSerial = ++buttonRouteSerial;
    const u64 registrationSerialBoundary = routedPointerListenerRegistry.registrationSerial();
    UIRoutedPointerEvent routedEvent = Detail::UIRoutedPointerEventAccess::Create(input);
    Detail::UIRoutedPointerEventAccess::setPointerCaptureRoute(routedEvent, result.routedThroughPointerCapture);
    ++routeDispatchDepth;
    auto dispatchCleanup = Core::makeScopeExit([this]() noexcept { finishRoutedPointerDispatch(); });

    if (!routePathScratch.empty() && !result.targetInvalidated)
    {
        for (usize reversePathIndex = routePathScratch.size(); reversePathIndex > 1; --reversePathIndex)
        {
            if (!contains(targetNode))
            {
                result.targetInvalidated = true;
                break;
            }
            const UINodeId currentNode = entries[routePathScratch[reversePathIndex - 1]].node;
            if (contains(currentNode))
            {
                dispatchRoutedPointerListeners(currentNode, UIEventPhase::Capture, input.kind,
                                               registrationSerialBoundary, routedEvent, result);
            }
            if (routedEvent.isPropagationStopped())
            {
                break;
            }
        }

        if (!routedEvent.isPropagationStopped() && !result.targetInvalidated)
        {
            if (!contains(targetNode))
            {
                result.targetInvalidated = true;
            } else
            {
                dispatchRoutedPointerListeners(targetNode, UIEventPhase::Target, input.kind,
                                               registrationSerialBoundary, routedEvent, result);
            }
        }

        if (!routedEvent.isPropagationStopped() && !result.targetInvalidated)
        {
            for (usize pathIndex = 1; pathIndex < routePathScratch.size(); ++pathIndex)
            {
                if (!contains(targetNode))
                {
                    result.targetInvalidated = true;
                    break;
                }
                const UINodeId currentNode = entries[routePathScratch[pathIndex]].node;
                if (contains(currentNode))
                {
                    dispatchRoutedPointerListeners(currentNode, UIEventPhase::Bubble, input.kind,
                                                   registrationSerialBoundary, routedEvent, result);
                }
                if (routedEvent.isPropagationStopped())
                {
                    break;
                }
            }
        }
    }

    if (targetNode.hasValue() && !contains(targetNode))
    {
        result.targetInvalidated = true;
    }
    if (primaryButtonUp)
    {
        // Physical Up is an unconditional capture release barrier, even if
        // a later repaint or callback-side mutation makes the route fail.
        capturedPointerNode = {};
    }

    bool preserveFocusForTransientOverlayBarrier = false;
    if (primaryButtonDown &&
        (dismissActivePopupOnPrimaryDown || blockPopupChromeClickThrough ||
         dismissActiveMenuOnPrimaryDown || blockMenuChromeClickThrough))
    {
        if (dismissActivePopupOnPrimaryDown && !routedEvent.isDefaultActionPrevented() &&
            contains(popupAtRouteStart) && popupAtRouteStart.index() < popupStatesByNodeIndex.size() &&
            popupStatesByNodeIndex[popupAtRouteStart.index()].open)
        {
            if (Core::Status closed = setPopupOpenState(popupAtRouteStart, false); !closed)
            {
                return Core::failure(closed.error());
            }
        }
        if (dismissActiveMenuOnPrimaryDown && !routedEvent.isDefaultActionPrevented() &&
            menuStorage.rootMenu() == menuAtRouteStart)
        {
            if (Core::Status closed = setMenuOpenState(menuAtRouteStart, false); !closed)
            {
                return Core::failure(closed.error());
            }
        }
        transientOverlayDismissPointerBarrierActive = true;
        routedEvent.preventDefaultAction();
        routedEvent.consumeInputTransition();
        static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
        preserveFocusForTransientOverlayBarrier = true;
    } else if (primaryButtonUp && transientOverlayBarrierAtRouteStart)
    {
        transientOverlayDismissPointerBarrierActive = false;
        routedEvent.preventDefaultAction();
        routedEvent.consumeInputTransition();
        static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
        preserveFocusForTransientOverlayBarrier = true;
    } else if (dismissActiveMenuOnWheel && !routedEvent.isDefaultActionPrevented() &&
               menuStorage.rootMenu() == menuAtRouteStart)
    {
        if (Core::Status closed = setMenuOpenState(menuAtRouteStart, false); !closed)
        {
            return Core::failure(closed.error());
        }
        routedEvent.preventDefaultAction();
        routedEvent.consumeInputTransition();
    }

    if (secondaryButtonDown)
    {
        // A new physical press supersedes a missing prior release. The
        // listener route gets first refusal through preventDefaultAction().
        secondaryMenuInvocationPressLatched = false;
        if (!routedEvent.isDefaultActionPrevented() &&
            isContextMenuInvocationCandidate(contextMenuAtRouteStart,
                                             contextMenuAnchorAtRouteStart))
        {
            if (Core::Status opened = setMenuOpenState(
                    contextMenuAtRouteStart, true, &input.position);
                !opened)
            {
                return Core::failure(opened.error());
            }
            secondaryMenuInvocationPressLatched = true;
            routedEvent.preventDefaultAction();
            routedEvent.consumeInputTransition();
            static_cast<void>(routedEvent.claimPointerButton(
                Platform::PointerButton::Secondary));
        }
    } else if (secondaryButtonUp && secondaryMenuInvocationPressLatched)
    {
        secondaryMenuInvocationPressLatched = false;
        routedEvent.preventDefaultAction();
        routedEvent.consumeInputTransition();
        static_cast<void>(routedEvent.claimPointerButton(
            Platform::PointerButton::Secondary));
    }

    if (input.kind == UIRoutedPointerEventKind::Move &&
        !routedEvent.isDefaultActionPrevented())
    {
        if (hasValidSubmenuRelationship(physicalNearestButton,
                                        hoverSubmenuToOpen) &&
            menuStorage.isOpen(hoveredMenuAtRouteStart))
        {
            if (Core::Status opened =
                    setMenuOpenState(hoverSubmenuToOpen, true);
                !opened)
            {
                return Core::failure(opened.error());
            }
        } else if (hoverSubmenuToClose.hasValue() &&
                   menuStorage.isInActiveChain(hoverSubmenuToClose))
        {
            if (Core::Status closed =
                    setMenuOpenState(hoverSubmenuToClose, false);
                !closed)
            {
                return Core::failure(closed.error());
            }
        }
    }

    Core::Status hoverPaintStatus = updateHoveredPrimaryControl(hoverCandidate);
    const bool deferHoverFailureForRelease =
        primaryButtonUp && (hadArmedInteraction || hadArmedSlider || hadArmedScrollView || hadArmedTextEdit);
    if (!hoverPaintStatus && !deferHoverFailureForRelease)
    {
        return Core::failure(hoverPaintStatus.error());
    }

    if (primaryButtonDown)
    {
        clearArmedPrimaryButton();
        clearArmedSlider();
        clearArmedScrollView();
        clearArmedTextEdit();
        capturedPointerNode = {};
        const NodeRecord* targetRecord =
            targetNode.hasValue() && contains(targetNode) ? nodes.tryGet(targetNode.storageId()) : nullptr;
        const bool targetsTextEdit = targetRecord != nullptr && targetRecord->kind == BuiltinElementKind::TextEdit &&
                                     targetNodeEnabledAtRouteStart;
        const UINodeId previousKeyboardFocus = defaultActionFocusButton;
        const UINodeId previousTextFocus = textInputFocus;
        const bool preserveFocusForModalBarrier = result.blockedByModal && !targetNode.hasValue();
        const bool allowsDefaultAction = !routedEvent.isDefaultActionPrevented();
        const bool willUseScrollBar = allowsDefaultAction && scrollBarHitAtRouteStart.hasValue() &&
                                      isLiveScrollable(scrollBarHitAtRouteStart.scrollView) &&
                                      isNodeEnabled(scrollBarHitAtRouteStart.scrollView);
        const bool willFocusTextEdit = allowsDefaultAction && !willUseScrollBar && targetsTextEdit;
        const NodeRecord* nearestSliderRecord =
            nearestSlider.hasValue() && contains(nearestSlider) ? nodes.tryGet(nearestSlider.storageId()) : nullptr;
        const bool willArmSlider = allowsDefaultAction && !willUseScrollBar && !willFocusTextEdit &&
                                   nearestSlider.hasValue() && nearestSliderRecord != nullptr &&
                                   isPointerAdjustableRangeInput(nearestSlider);
        UINodeId nextKeyboardFocus =
            preserveFocusForModalBarrier || preserveFocusForTransientOverlayBarrier
                ? previousKeyboardFocus
                : UINodeId{};
        UINodeId nextActivationTarget{};
        UINodeId interactionPaintNode{};
        if (willUseScrollBar)
        {
            interactionPaintNode = scrollBarHitAtRouteStart.thumb ? scrollBarHitAtRouteStart.scrollView
                                                                 : UINodeId{};
        } else if (willFocusTextEdit)
        {
            nextKeyboardFocus = targetNode;
            interactionPaintNode = targetNode;
        } else if (willArmSlider)
        {
            nextKeyboardFocus = nearestSlider;
            interactionPaintNode = nearestSlider;
        } else if (allowsDefaultAction && nearestButton.hasValue() && isNodeEnabled(nearestButton))
        {
            const NodeRecord* nextButtonRecord = nodes.tryGet(nearestButton.storageId());
            if (nextButtonRecord != nullptr &&
                behaviorStateStorage.hasActivate(nearestButton.index()))
            {
                nextActivationTarget = nearestButton;
                if (hasBehavior(nextButtonRecord->behaviors,
                                UIElementBehavior::Focusable))
                {
                    nextKeyboardFocus =
                        nextButtonRecord->kind == BuiltinElementKind::ListViewItem ? nearestListView
                        : nextButtonRecord->kind == BuiltinElementKind::TreeViewItem ? nearestTreeView
                        : nextButtonRecord->kind == BuiltinElementKind::VirtualGridViewItem
                            ? nearestVirtualGridView
                        : nextButtonRecord->kind == BuiltinElementKind::DataGridCell
                            ? nearestDataGrid
                            : nearestButton;
                }
                interactionPaintNode = nearestButton;
            }
        }
        const UINodeId dirtyPreviousKeyboard = previousKeyboardFocus.hasValue() &&
                                                       previousKeyboardFocus != nextKeyboardFocus &&
                                                       contains(previousKeyboardFocus)
                                                   ? previousKeyboardFocus
                                                   : UINodeId{};
        const UINodeId dirtyPreviousText =
            previousTextFocus.hasValue() && previousTextFocus != nextKeyboardFocus && contains(previousTextFocus)
                ? previousTextFocus
                : UINodeId{};
        const UINodeId dirtyPreviousSlider = armedSliderAtRouteStart.hasValue() &&
                                                     armedSliderAtRouteStart != interactionPaintNode &&
                                                     contains(armedSliderAtRouteStart)
                                                 ? armedSliderAtRouteStart
                                                 : UINodeId{};
        const UINodeId dirtyPreviousScrollView =
            scrollThumbDragAtRouteStart && armedScrollViewAtRouteStart.hasValue() &&
                    armedScrollViewAtRouteStart != interactionPaintNode && contains(armedScrollViewAtRouteStart)
                ? armedScrollViewAtRouteStart
                : UINodeId{};
        const UINodeId interactionFocusNode =
            nextKeyboardFocus.hasValue() && nextKeyboardFocus != interactionPaintNode ? nextKeyboardFocus
                                                                                      : UINodeId{};
        if (Core::Status dirty = markPaintDirtyBatch({
                dirtyPreviousKeyboard,
                dirtyPreviousText,
                dirtyPreviousSlider,
                dirtyPreviousScrollView,
                interactionPaintNode,
                interactionFocusNode,
            });
            !dirty)
        {
            return Core::failure(dirty.error());
        }
        if (!preserveFocusForModalBarrier && !preserveFocusForTransientOverlayBarrier)
        {
            clearDefaultActionFocus();
            if (!willFocusTextEdit && textInputFocus.hasValue())
            {
                clearImeFocus();
            }
        }
        // Only one Primary interaction may be armed. Scrollbar chrome wins
        // at its committed track; otherwise an exact TextEdit target wins,
        // then Slider drag, then Button/Checkbox.
        if (preserveFocusForModalBarrier)
        {
            // A Modal backdrop owns the input but is not a focus target.
        } else if (preserveFocusForTransientOverlayBarrier)
        {
            // Transient overlay chrome and outside-dismiss clicks are
            // click-through barriers and preserve restored focus.
        } else if (willUseScrollBar)
        {
            armedScrollView = scrollBarHitAtRouteStart.scrollView;
            armedScrollAxis = scrollBarHitAtRouteStart.axis;
            scrollThumbDragActive = scrollBarHitAtRouteStart.thumb;
            if (scrollThumbDragActive)
            {
                const float pointer = armedScrollAxis == UIScrollAxes::Horizontal ? input.position.x
                                                                                 : input.position.y;
                const float thumbStart = armedScrollAxis == UIScrollAxes::Horizontal
                                             ? scrollBarHitAtRouteStart.geometry.thumb.x
                                             : scrollBarHitAtRouteStart.geometry.thumb.y;
                scrollDragGrabOffset = normalizeFloat(pointer - thumbStart);
            }
            capturedPointerNode = armedScrollView;
            routedEvent.consumeInputTransition();
            static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
            if (!scrollThumbDragActive)
            {
                if (auto applied = applyScrollTrackPage(armedScrollView, armedScrollAxis, input.position); !applied)
                {
                    return Core::failure(applied.error());
                }
            }
        } else if (willFocusTextEdit)
        {
            const UINodeId previousFocus = textInputFocus;
            if (previousFocus != targetNode)
            {
                if (Core::Status composition = clearImeComposition(); !composition)
                {
                    return Core::failure(composition.error());
                }
                resetTextEditPreferredX(previousFocus);
                resetTextEditPreferredX(targetNode);
            }
            textInputFocus = targetNode;
            defaultActionFocusButton = targetNode;
            armedTextEdit = targetNode;
            capturedPointerNode = targetNode;
            if (Core::Status selection = updateTextEditSelectionFromPointer(targetNode, input.position, false);
                !selection)
            {
                return Core::failure(selection.error());
            }
            routedEvent.consumeInputTransition();
            static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
        } else if (willArmSlider)
        {
            armedSlider = nearestSlider;
            defaultActionFocusButton = nextKeyboardFocus;
            capturedPointerNode = nearestSlider;
            if (nearestSliderRecord->kind == BuiltinElementKind::Splitter)
            {
                const UINodeId splitView = splitViewStorage.splitViewForSplitter(nearestSlider);
                const UISplitViewMetrics metrics = splitViewStorage.committedMetrics(splitView);
                const float extent = metrics.orientation == UISplitViewOrientation::Horizontal
                                         ? metrics.splitterRect.width
                                         : metrics.splitterRect.height;
                splitterDragGrabOffset = std::clamp(
                    splitterGrabOffset(metrics, input.position), 0.0F,
                    (std::max)(0.0F, extent));
            }
            routedEvent.consumeInputTransition();
            static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
            if (auto applied = applyRangeInputValueFromPointer(nearestSlider, input.position, input.platformFrame,
                                                               input.sourceSequence);
                !applied)
            {
                return Core::failure(applied.error());
            }
        } else if (nextActivationTarget.hasValue())
        {
            const NodeRecord* buttonRecord = nodes.tryGet(nextActivationTarget.storageId());
            if (buttonRecord != nullptr &&
                behaviorStateStorage.hasActivate(nextActivationTarget.index()))
            {
                armedPrimaryButton = nextActivationTarget;
                armedPrimaryButtonPressed = true;
                armedTreeDisclosure = nearestTreeDisclosureAtRouteStart;
                defaultActionFocusButton = nextKeyboardFocus;
                capturedPointerNode = nextActivationTarget;
                clearImeFocus();
                routedEvent.consumeInputTransition();
                static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
            }
        }
    } else if (input.kind == UIRoutedPointerEventKind::Wheel && !routedEvent.isDefaultActionPrevented())
    {
        for (const u32 routeEntryIndex : routePathScratch)
        {
            if (routeEntryIndex >= entries.size())
            {
                continue;
            }
            const UINodeId routeNode = entries[routeEntryIndex].node;
            // Multiline TextEdit takes wheel before ScrollView.
            if (isLiveMultilineTextEdit(routeNode) && isNodeEnabled(routeNode))
            {
                auto applied = applyTextEditScrollWheel(routeNode, input.delta);
                if (!applied)
                {
                    return Core::failure(applied.error());
                }
                if (*applied)
                {
                    routedEvent.consumeInputTransition();
                    break;
                }
                continue;
            }
            if (!isLiveScrollable(routeNode) || !isNodeEnabled(routeNode))
            {
                continue;
            }
            auto applied = applyScrollWheel(routeNode, input.delta);
            if (!applied)
            {
                return Core::failure(applied.error());
            }
            if (*applied)
            {
                routedEvent.consumeInputTransition();
                break;
            }
        }
    } else if (input.kind == UIRoutedPointerEventKind::Move && hadArmedScrollView &&
               armedScrollView == armedScrollViewAtRouteStart)
    {
        if (!isLiveScrollable(armedScrollViewAtRouteStart) || !isNodeEnabled(armedScrollViewAtRouteStart))
        {
            clearArmedScrollView();
            if (capturedPointerNode == armedScrollViewAtRouteStart)
            {
                capturedPointerNode = {};
            }
        } else
        {
            static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
            if (scrollThumbDragAtRouteStart)
            {
                auto applied = applyScrollThumbFromPointer(armedScrollViewAtRouteStart,
                                                           armedScrollAxisAtRouteStart, input.position,
                                                           scrollDragGrabOffsetAtRouteStart);
                if (!applied)
                {
                    return Core::failure(applied.error());
                }
            }
        }
    } else if (input.kind == UIRoutedPointerEventKind::Move && hadArmedSlider &&
               armedSlider == armedSliderAtRouteStart)
    {
        if (!isNodeEnabled(armedSliderAtRouteStart))
        {
            clearArmedSlider();
        } else
        {
            static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
            if (auto applied = applyRangeInputValueFromPointer(armedSliderAtRouteStart, input.position,
                                                               input.platformFrame, input.sourceSequence);
                !applied)
            {
                return Core::failure(applied.error());
            }
        }
    } else if (input.kind == UIRoutedPointerEventKind::Move && hadArmedTextEdit &&
               armedTextEdit == armedTextEditAtRouteStart)
    {
        if (!isNodeEnabled(armedTextEditAtRouteStart))
        {
            clearArmedTextEdit();
        } else
        {
            if (Core::Status selection =
                    updateTextEditSelectionFromPointer(armedTextEditAtRouteStart, input.position, true);
                !selection)
            {
                return Core::failure(selection.error());
            }
            static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
        }
    } else if (input.kind == UIRoutedPointerEventKind::Move && hadArmedInteraction &&
               armedPrimaryButton == armedButtonAtRouteStart)
    {
        if (!isNodeEnabled(armedButtonAtRouteStart))
        {
            clearArmedPrimaryButton();
        } else
        {
            if (armedPrimaryButtonPressed != pointWithinArmedButton)
            {
                if (Core::Status dirty = markPaintDirty(armedButtonAtRouteStart); !dirty)
                {
                    return Core::failure(dirty.error());
                }
                armedPrimaryButtonPressed = pointWithinArmedButton;
            }
            static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
        }
    } else if (primaryButtonUp && hadArmedScrollView)
    {
        const bool scrollViewStillArmed = armedScrollView == armedScrollViewAtRouteStart;
        Core::Status releasePaint = Core::success();
        if (scrollViewStillArmed && scrollThumbDragAtRouteStart && contains(armedScrollViewAtRouteStart))
        {
            releasePaint = markPaintDirty(armedScrollViewAtRouteStart);
        }
        clearArmedScrollView();
        if (!hoverPaintStatus)
        {
            return Core::failure(hoverPaintStatus.error());
        }
        if (!releasePaint)
        {
            return Core::failure(releasePaint.error());
        }
        routedEvent.consumeInputTransition();
        static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
        if (scrollViewStillArmed && scrollThumbDragAtRouteStart &&
            isLiveScrollable(armedScrollViewAtRouteStart) && isNodeEnabled(armedScrollViewAtRouteStart) &&
            !routedEvent.isDefaultActionPrevented())
        {
            auto applied = applyScrollThumbFromPointer(armedScrollViewAtRouteStart,
                                                       armedScrollAxisAtRouteStart, input.position,
                                                       scrollDragGrabOffsetAtRouteStart);
            if (!applied)
            {
                return Core::failure(applied.error());
            }
        }
    } else if (primaryButtonUp && hadArmedSlider)
    {
        const bool sliderStillArmed = armedSlider == armedSliderAtRouteStart;
        Core::Status releasePaint = Core::success();
        if (sliderStillArmed && contains(armedSliderAtRouteStart))
        {
            releasePaint = markPaintDirty(armedSliderAtRouteStart);
        }
        clearArmedSlider();
        if (!hoverPaintStatus)
        {
            return Core::failure(hoverPaintStatus.error());
        }
        if (!releasePaint)
        {
            return Core::failure(releasePaint.error());
        }
        routedEvent.consumeInputTransition();
        if (sliderStillArmed && isNodeEnabled(armedSliderAtRouteStart) && !routedEvent.isDefaultActionPrevented())
        {
            if (auto applied = applyRangeInputValueFromPointer(armedSliderAtRouteStart, input.position,
                                                               input.platformFrame, input.sourceSequence);
                !applied)
            {
                return Core::failure(applied.error());
            }
        }
    } else if (primaryButtonUp && hadArmedTextEdit)
    {
        const bool textEditStillArmed = armedTextEdit == armedTextEditAtRouteStart;
        Core::Status releasePaint = Core::success();
        if (textEditStillArmed && contains(armedTextEditAtRouteStart))
        {
            releasePaint = markPaintDirty(armedTextEditAtRouteStart);
        }
        clearArmedTextEdit();
        if (!hoverPaintStatus)
        {
            return Core::failure(hoverPaintStatus.error());
        }
        if (!releasePaint)
        {
            return Core::failure(releasePaint.error());
        }
        routedEvent.consumeInputTransition();
        if (textEditStillArmed && isNodeEnabled(armedTextEditAtRouteStart))
        {
            if (Core::Status selection =
                    updateTextEditSelectionFromPointer(armedTextEditAtRouteStart, input.position, true);
                !selection)
            {
                return Core::failure(selection.error());
            }
        }
    } else if (primaryButtonUp && hadArmedInteraction)
    {
        const bool interactionStillArmed = armedPrimaryButton == armedButtonAtRouteStart;
        Core::Status releasePaint = Core::success();
        if (interactionStillArmed && contains(armedButtonAtRouteStart))
        {
            releasePaint = markPaintDirty(armedButtonAtRouteStart);
        }
        clearArmedPrimaryButton();
        if (!hoverPaintStatus)
        {
            return Core::failure(hoverPaintStatus.error());
        }
        if (!releasePaint)
        {
            return Core::failure(releasePaint.error());
        }
        routedEvent.consumeInputTransition();
        if (!routedEvent.isDefaultActionPrevented() && interactionStillArmed && pointWithinArmedButton &&
            isNodeEnabled(armedButtonAtRouteStart))
        {
            if (u8* toggleValue =
                    behaviorStateStorage.tryToggleValue(armedButtonAtRouteStart.index());
                toggleValue != nullptr)
            {
                if (Core::Status dirty = markPaintDirty(armedButtonAtRouteStart); !dirty)
                {
                    return Core::failure(dirty.error());
                }
                *toggleValue = *toggleValue == 0 ? 1 : 0;
            }
            if (const NodeRecord* armedRecord = nodes.tryGet(armedButtonAtRouteStart.storageId());
                armedRecord != nullptr && armedRecord->kind == BuiltinElementKind::RadioButton)
            {
                if (Core::Status selected = applyRadioButtonSelection(armedButtonAtRouteStart, true); !selected)
                {
                    return Core::failure(selected.error());
                }
            }
            if (const NodeRecord* armedRecord = nodes.tryGet(armedButtonAtRouteStart.storageId());
                armedRecord != nullptr && (armedRecord->kind == BuiltinElementKind::Dropdown ||
                                           armedRecord->kind == BuiltinElementKind::DropdownItem))
            {
                if (auto activated = activateDropdownControl(armedButtonAtRouteStart); !activated)
                {
                    return Core::failure(activated.error());
                }
            }
            if (const NodeRecord* armedRecord = nodes.tryGet(armedButtonAtRouteStart.storageId());
                armedRecord != nullptr && armedRecord->kind == BuiltinElementKind::MenuItem)
            {
                if (auto activated = activateMenuItem(armedButtonAtRouteStart); !activated)
                {
                    return Core::failure(activated.error());
                }
            }
            if (const NodeRecord* armedRecord = nodes.tryGet(armedButtonAtRouteStart.storageId());
                armedRecord != nullptr && armedRecord->kind == BuiltinElementKind::Tab)
            {
                if (auto activated = activateTabControl(armedButtonAtRouteStart); !activated)
                {
                    return Core::failure(activated.error());
                }
            }
            if (const NodeRecord* armedRecord = nodes.tryGet(armedButtonAtRouteStart.storageId());
                armedRecord != nullptr && armedRecord->kind == BuiltinElementKind::ListViewItem)
            {
                if (Core::Status selected = selectCommittedListViewItem(armedButtonAtRouteStart); !selected)
                {
                    return Core::failure(selected.error());
                }
            }
            if (const NodeRecord* armedRecord = nodes.tryGet(armedButtonAtRouteStart.storageId());
                armedRecord != nullptr &&
                armedRecord->kind == BuiltinElementKind::VirtualGridViewItem)
            {
                if (Core::Status selected =
                        selectCommittedVirtualGridViewItem(
                            armedButtonAtRouteStart);
                    !selected)
                {
                    return Core::failure(selected.error());
                }
            }
            if (const NodeRecord* armedRecord = nodes.tryGet(armedButtonAtRouteStart.storageId());
                armedRecord != nullptr &&
                armedRecord->kind == BuiltinElementKind::DataGridCell)
            {
                if (Core::Status selected = selectCommittedDataGridCell(
                        armedButtonAtRouteStart);
                    !selected)
                {
                    return Core::failure(selected.error());
                }
            }
            if (const NodeRecord* armedRecord = nodes.tryGet(armedButtonAtRouteStart.storageId());
                armedRecord != nullptr && armedRecord->kind == BuiltinElementKind::TreeViewItem)
            {
                Core::Status treeAction = armedTreeDisclosureAtRouteStart
                                              ? pointWithinArmedTreeDisclosure
                                                    ? toggleCommittedTreeViewItem(armedButtonAtRouteStart)
                                                    : Core::success()
                                              : selectCommittedTreeViewItem(armedButtonAtRouteStart);
                if (!treeAction)
                {
                    return Core::failure(treeAction.error());
                }
            }
            invokeButtonAction(actionCandidate,
                               UIButtonActionEvent{
                                   .buttonNode = armedButtonAtRouteStart,
                                   .source = UIButtonActivationSource::PrimaryPointer,
                                   .platformFrame = input.platformFrame,
                                   .sourceSequence = input.sourceSequence,
                               },
                               currentButtonRouteSerial);
        }
    }

    const bool releaseCaptureRequested =
        Detail::UIRoutedPointerEventAccess::pointerCaptureReleaseRequested(routedEvent);
    const UINodeId requestedCapture = Detail::UIRoutedPointerEventAccess::pointerCaptureRequest(routedEvent);
    if (primaryButtonUp || releaseCaptureRequested)
    {
        capturedPointerNode = {};
    } else if (requestedCapture.hasValue() &&
               isPointerCaptureCandidate(requestedCapture, entries, committedActiveModalEntryIndex))
    {
        capturedPointerNode = requestedCapture;
    }

    if (result.pointQuery.modalBarrierActive)
    {
        routedEvent.consumeInputTransition();
        static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
        if ((input.kind == UIRoutedPointerEventKind::ButtonDown ||
             input.kind == UIRoutedPointerEventKind::ButtonUp) &&
            input.button < Platform::PointerButton::Count)
        {
            static_cast<void>(routedEvent.claimPointerButton(input.button));
        }
    }

    result.claimedPointerButtons = Detail::UIRoutedPointerEventAccess::claimedPointerButtons(routedEvent);
    result.consumed = routedEvent.isInputTransitionConsumed();
    result.stopped = routedEvent.isPropagationStopped();
    result.pointerCaptureChanged = capturedPointerNode != captureAtRouteStart;
    if (input.kind == UIRoutedPointerEventKind::Move)
    {
        // Hover intent always follows the physical committed hit rather
        // than Pointer Capture routing. Active Menu chrome is the exception:
        // its Ignore surface still occludes lower tooltip anchors.
        tooltipStorage.setHoveredAnchor(
            tooltipAnchorFromCommittedHit(
                activeMenuChromeOccludesPhysicalTarget
                    ? result.routedTarget
                    : result.pointQuery.target,
                entries));
    } else if (input.kind == UIRoutedPointerEventKind::ButtonDown ||
               input.kind == UIRoutedPointerEventKind::Wheel)
    {
        hardDismissAllTooltipsNoFail(true);
    }
    return result;
}

[[nodiscard]] Core::Status UIContext::Impl::cancelPointerInteraction(Platform::WindowId routedWindow)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    drainDeferredRootDestroys();
    if (!routedWindow.hasValue())
    {
        return fail(UIErrorCode::InvalidPointerInput, "UI Pointer interaction cancellation requires a Window");
    }
    if (routedWindow != ownerWindow)
    {
        return fail(UIErrorCode::WrongOwnerWindow, "UI Pointer interaction cancellation belongs to another Window");
    }
    std::array<UINodeId, Platform::PointerCapacity * 5 + 2> cancelledNodes{};
    usize cancelledNodeCount = 0;
    const auto remember = [&cancelledNodes, &cancelledNodeCount](UINodeId node) noexcept {
        if (!node.hasValue() || cancelledNodeCount >= cancelledNodes.size())
        {
            return;
        }
        for (usize index = 0; index < cancelledNodeCount; ++index)
        {
            if (cancelledNodes[index] == node)
            {
                return;
            }
        }
        cancelledNodes[cancelledNodeCount++] = node;
    };
    for (const PointerInteractionState& state : pointerInteractionStates)
    {
        remember(state.armedPrimaryButton);
        remember(state.armedSlider);
        remember(state.armedScrollView);
        remember(state.armedTextEdit);
        remember(state.hoveredPrimaryControl);
    }
    const UINodeId cancelledFocus = defaultActionFocusButton;
    remember(cancelledFocus);
    const UINodeId cancelledHover = hoveredPrimaryControl;
    remember(cancelledHover);
    for (PointerInteractionState& state : pointerInteractionStates)
    {
        state = {};
    }
    restorePointerInteractionState(pointerInteractionStates[Platform::PrimaryPointerId]);
    activePointerState = Platform::PrimaryPointerId;
    clearArmedPrimaryButton();
    clearArmedSlider();
    clearArmedScrollView();
    clearArmedTextEdit();
    capturedPointerNode = {};
    transientOverlayDismissPointerBarrierActive = false;
    secondaryMenuInvocationPressLatched = false;
    dropdownCommandPressLatch.clear();
    listViewCommandPressLatch.clear();
    virtualGridViewCommandPressLatch.clear();
    dataGridCommandPressLatch.clear();
    treeViewCommandPressLatch.clear();
    focusNavigationPressLatch.clear();
    tabViewCommandPressLatch.clear();
    menuCommandPressLatch.clear();
    tabViewDirectionPressLatch.clear();
    menuInvocationPressLatch.clear();
    rangeInputPressLatch.clear();
    defaultActionPressState.clearAll();
    clearImeFocus();
    clearDefaultActionFocus();
    clearHoveredPrimaryControl();
    tooltipStorage.setHoveredAnchor({});
    hardDismissAllTooltipsNoFail(true);
    // Cancellation is a state barrier: a full dirty queue must not leave
    // any pointer interaction armed. Existing dirty work will rebuild the
    // paint/semantics snapshot; otherwise this best-effort mark schedules
    // the cleared control state for the next commit.
    for (usize index = 0; index < cancelledNodeCount; ++index)
    {
        static_cast<void>(markPaintDirty(cancelledNodes[index]));
    }
    return Core::success();
}

} // namespace Tina::UI
