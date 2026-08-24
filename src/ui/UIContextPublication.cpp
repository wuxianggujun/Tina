#include "detail/UIContextImpl.hpp"

namespace Tina::UI {

[[nodiscard]] Core::Status UIContext::Impl::commitStructure()
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI structure cannot be committed during pointer routing");
    }
    if (activeBuildTransactionCount != 0)
    {
        return fail(UIErrorCode::BuildTransactionInProgress,
                    "UI structure cannot be committed while a component build transaction is active");
    }
    drainDeferredRootDestroys();
    return publishStructureIfDirty();
}

[[nodiscard]] Core::Status UIContext::Impl::validateViewport(UILogicalSize viewportSize) const
{
    if (!isFiniteNonNegative(viewportSize.width) || !isFiniteNonNegative(viewportSize.height))
    {
        return fail(UIErrorCode::InvalidLayout, "UI layout viewport must be finite and non-negative");
    }
    return Core::success();
}

void UIContext::Impl::publishControlLayoutState(const std::pmr::vector<u32>& order) noexcept
{
    for (const u32 index : order)
    {
        const NodeRecord* record = recordByIndex(index);
        if (record == nullptr || record->kind != BuiltinElementKind::ScrollView ||
            index >= scrollViewLayoutScratchByNodeIndex.size())
        {
            continue;
        }
        UIScrollBehaviorState* state = behaviorStateStorage.tryScrollState(index);
        if (state == nullptr)
        {
            continue;
        }
        const ScrollViewLayoutScratch& layout = scrollViewLayoutScratchByNodeIndex[index];
        state->requestedOffset = layout.metrics.offset;
        state->committedMetrics = layout.metrics;
        state->committedViewportRect = layout.viewportRect;
    }
    for (const u32 index : order)
    {
        const NodeRecord* record = recordByIndex(index);
        if (record == nullptr || record->kind != BuiltinElementKind::Popup || index >= popupStatesByNodeIndex.size() ||
            index >= popupLayoutScratchByNodeIndex.size())
        {
            continue;
        }
        popupStatesByNodeIndex[index].committedMetrics = popupLayoutScratchByNodeIndex[index].metrics;
    }
    for (const u32 index : order)
    {
        const NodeRecord* record = recordByIndex(index);
        if (record == nullptr || record->kind != BuiltinElementKind::Tooltip ||
            !tooltipStorage.containsTooltip(idForIndex(index)))
        {
            continue;
        }
        tooltipStorage.publishMetrics(index);
    }
    for (const u32 index : order)
    {
        const NodeRecord* record = recordByIndex(index);
        if (record == nullptr || record->kind != BuiltinElementKind::SplitView ||
            !splitViewStorage.containsSplitView(idForIndex(index)))
        {
            continue;
        }
        splitViewStorage.publishMetrics(index);
    }
    for (const u32 index : order)
    {
        const NodeRecord* record = recordByIndex(index);
        if (record == nullptr || record->kind != BuiltinElementKind::TabView ||
            !tabViewStorage.containsTabView(idForIndex(index)))
        {
            continue;
        }
        tabViewStorage.publishMetrics(index);
    }
    for (const u32 index : order)
    {
        const NodeRecord* record = recordByIndex(index);
        if (record == nullptr || record->kind != BuiltinElementKind::Menu ||
            !menuStorage.containsMenu(idForIndex(index)))
        {
            continue;
        }
        menuStorage.publishMetrics(index);
    }
    for (const u32 index : order)
    {
        const NodeRecord* record = recordByIndex(index);
        if (record == nullptr)
        {
            continue;
        }
        if (record->kind == BuiltinElementKind::ListView && index < listViewStatesByNodeIndex.size() &&
            index < listViewLayoutScratchByNodeIndex.size())
        {
            ListViewState& state = listViewStatesByNodeIndex[index];
            state.requestedScrollOffset = listViewLayoutScratchByNodeIndex[index].metrics.scrollOffset;
            state.committedMetrics = listViewLayoutScratchByNodeIndex[index].metrics;
            state.committedViewportRect = listViewLayoutScratchByNodeIndex[index].viewportRect;
        } else if (record->kind == BuiltinElementKind::ListViewItem && index < listViewItemStatesByNodeIndex.size())
        {
            ListViewItemState& item = listViewItemStatesByNodeIndex[index];
            item.committedKey = item.key;
            item.committedLogicalIndex = item.logicalIndex;
            item.committedBound = item.bound;
            item.committedEnabled = item.enabled;
        } else if (record->kind == BuiltinElementKind::VirtualGridView)
        {
            VirtualGridViewState* state =
                virtualGridViewStorage.tryView(idForIndex(index));
            VirtualGridViewLayoutScratch* layout =
                virtualGridViewStorage.tryLayoutScratch(idForIndex(index));
            if (state != nullptr && layout != nullptr)
            {
                state->requestedScrollOffset =
                    layout->metrics.scrollOffset;
                virtualGridViewStorage.publishMetrics(idForIndex(index));
                virtualGridViewStorage.publishItemBindings(
                    idForIndex(index));
            }
        } else if (record->kind == BuiltinElementKind::DataGrid)
        {
            const UINodeId dataGrid = idForIndex(index);
            DataGridState* state = dataGridStorage.tryGrid(dataGrid);
            DataGridLayoutScratch* layout =
                dataGridStorage.tryLayoutScratch(dataGrid);
            if (state != nullptr && layout != nullptr)
            {
                state->requestedScrollOffset = layout->metrics.scrollOffset;
                dataGridStorage.publishMetrics(dataGrid);
                dataGridStorage.publishBindings(dataGrid);
            }
        } else if (record->kind == BuiltinElementKind::TreeView && index < treeViewStatesByNodeIndex.size() &&
                   index < treeViewLayoutScratchByNodeIndex.size())
        {
            TreeViewState& state = treeViewStatesByNodeIndex[index];
            state.requestedScrollOffset = treeViewLayoutScratchByNodeIndex[index].metrics.scrollOffset;
            state.committedMetrics = treeViewLayoutScratchByNodeIndex[index].metrics;
            state.committedViewportRect = treeViewLayoutScratchByNodeIndex[index].viewportRect;
        } else if (record->kind == BuiltinElementKind::TreeViewItem && index < treeViewItemStatesByNodeIndex.size())
        {
            TreeViewItemState& item = treeViewItemStatesByNodeIndex[index];
            item.committedKey = item.key;
            item.committedLogicalIndex = item.logicalIndex;
            item.committedLevel = item.level;
            item.committedBound = item.bound;
            item.committedEnabled = item.enabled;
            item.committedExpandable = item.expandable;
            item.committedExpanded = item.expanded;
        }
    }
}

[[nodiscard]] Core::Status UIContext::Impl::commitLayout(UILogicalSize viewportSize)
{
    if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
    {
        return ownerThread;
    }
    if (routeDispatchDepth != 0)
    {
        return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                    "UI layout cannot be committed during pointer routing");
    }
    if (activeBuildTransactionCount != 0)
    {
        return fail(UIErrorCode::BuildTransactionInProgress,
                    "UI layout cannot be committed while a component build transaction is active");
    }
    drainDeferredRootDestroys();
    if (Core::Status viewportStatus = validateViewport(viewportSize); !viewportStatus)
    {
        return viewportStatus;
    }
    // Timeline layout tracks remain a candidate until every Layout/Hit/Paint
    // builder below succeeds. M==0 remains a pure no-op.
    const Core::MonotonicTimePoint frameNow = motionNow();
    if (Core::Status motionStatus = sampleMotion(frameNow); !motionStatus)
    {
        return motionStatus;
    }

    tooltipStorage.beginCommitTransaction();
    auto tooltipAdvanceRollback = Core::makeScopeExit([&]() noexcept {
        tooltipStorage.rollbackCommitTransaction();
    });
    advanceTooltips(frameNow);
    const bool hasTransactionalTimelineSample = timelineStorage.hasCandidateSample();
    const bool hasTransactionalDirectMotionSample = motionTrackStorage.hasCandidateSample();
    const bool hasLayoutTimelineCandidate = !timelineStorage.candidateLayoutNodes().empty();
    auto timelineSampleRollback = Core::makeScopeExit([&]() noexcept {
        if (!timelineStorage.hasCandidateSample() &&
            !motionTrackStorage.hasCandidateSample())
        {
            return;
        }
        motionTrackStorage.rollbackCandidateSample();
        timelineStorage.discardCandidateSample();
        if (hasLayoutTimelineCandidate)
        {
            ++layoutTimelineCommitFailureCount;
        }
    });
    viewportSize.width = normalizeFloat(viewportSize.width);
    viewportSize.height = normalizeFloat(viewportSize.height);
    const bool viewportChanged = !hasCommittedViewport || viewportSize != committedViewportSize;
    const bool structureNeedsCommit = isPhaseDirty(PhaseStructure);
    const bool layoutNeedsCommit = structureNeedsCommit || isPhaseDirty(PhaseLayout) || viewportChanged;
    const bool hitNeedsCommit = isPhaseDirty(PhaseHit) || layoutNeedsCommit || committedHitRevision == 0;
    bool paintNeedsCommit = isPhaseDirty(PhasePaint) || layoutNeedsCommit || committedPaintRevision == 0;
    bool semanticsNeedsCommit =
        isPhaseDirty(PhaseSemantics) || layoutNeedsCommit || committedSemanticsRevision == 0;

    if (!layoutNeedsCommit && !hitNeedsCommit && !paintNeedsCommit && !semanticsNeedsCommit)
    {
        lastLayoutPass = {};
        lastHitRebuildCount = 0;
        lastPaintCacheRebuildCount = 0;
        lastPaintSnapshotRebuildCount = 0;
        lastStyleInspectedNodeCount = 0;
        lastStyleResolvedNodeCount = 0;
        lastStyleCandidateRuleCount = 0;
        tooltipAdvanceRollback.release();
        return Core::success();
    }
    if (layoutNeedsCommit && nodes.activeCount() > capacityConfig.layoutSnapshotCapacity)
    {
        return fail(UIErrorCode::CapacityExceeded, "UI committed layout snapshot capacity has been exhausted");
    }

    const usize writeStructureBufferIndex = 1 - publishedBufferIndex;
    if (structureNeedsCommit)
    {
        buildCommittedStructure(committedBuffers[writeStructureBufferIndex]);
    }

    usize writeLayoutBufferIndex = publishedLayoutBufferIndex;
    LayoutPassStatistics pass{};
    std::span<const UICommittedLayoutEntry> candidateLayoutEntries{};
    bool imageCandidateTransactionActive = false;
    auto imageCandidateRollback = Core::makeScopeExit([&]() noexcept {
        if (imageCandidateTransactionActive)
        {
            imageContentStorage.rollbackCandidateTransaction();
        }
    });
    if (layoutNeedsCommit)
    {
        imageContentStorage.beginCandidateTransaction();
        imageCandidateTransactionActive = true;
        const bool allowLayoutReuse = !structureNeedsCommit && !viewportChanged && layoutReuseCacheValid;
        layoutReuseCacheValid = false;
        layoutReuseInProgress = allowLayoutReuse;
        auto layoutReuseGuard = Core::makeScopeExit([this]() noexcept { layoutReuseInProgress = false; });
        writeLayoutBufferIndex = 1 - publishedLayoutBufferIndex;
        std::pmr::vector<UICommittedLayoutEntry>& writeLayout = committedLayoutBuffers[writeLayoutBufferIndex];
        writeLayout.clear();

        buildLayoutOrder(layoutOrderScratch);
        pass.passCount = 0U;
        prepareLayoutState(viewportSize, layoutOrderScratch, allowLayoutReuse);
        constexpr usize MaximumResolvedLayoutPasses = 3U;
        bool layoutStabilized = layoutOrderScratch.empty();
        for (usize resolvedPass = 0U;
             resolvedPass < MaximumResolvedLayoutPasses && !layoutStabilized;
             ++resolvedPass)
        {
            if (resolvedPass != 0U)
            {
                for (const u32 index : layoutOrderScratch)
                {
                    layoutWorkByIndex[index] =
                        LayoutWorkMeasure | LayoutWorkArrange;
                }
            }
            ++pass.passCount;
            measureLayout(viewportSize, layoutOrderScratch, pass);
            if (Core::Status arranged =
                    arrangeLayout(viewportSize, layoutOrderScratch, pass);
                !arranged)
            {
                return arranged;
            }
            auto changed = refreshResolvedLayoutAfterArrange(layoutOrderScratch);
            if (!changed)
            {
                return Core::failure(changed.error());
            }
            if (!*changed)
            {
                layoutStabilized = true;
            }
        }
        if (!layoutStabilized)
        {
            return fail(
                UIErrorCode::InvalidLayout,
                "UI responsive/text/flex layout did not stabilize within the bounded pass count");
        }
        if (Core::Status candidateStatus = validateLayoutCandidate(layoutOrderScratch); !candidateStatus)
        {
            return candidateStatus;
        }
        buildCommittedLayout(writeLayout, layoutOrderScratch);
        candidateLayoutEntries = std::span<const UICommittedLayoutEntry>(writeLayout.data(), writeLayout.size());
    } else
    {
        const std::pmr::vector<UICommittedLayoutEntry>& currentLayout =
            committedLayoutBuffers[publishedLayoutBufferIndex];
        candidateLayoutEntries =
            std::span<const UICommittedLayoutEntry>(currentLayout.data(), currentLayout.size());
    }

    if (Core::Status textVisualStatus = buildTextEditVisualState(candidateLayoutEntries); !textVisualStatus)
    {
        return textVisualStatus;
    }

    const u64 candidateStructureRevision = committedRevision + (structureNeedsCommit ? 1u : 0u);
    const u64 candidateLayoutRevision = committedLayoutRevision + (layoutNeedsCommit ? 1u : 0u);
    // C1c-a derives paint order solely from committed tree preorder. A
    // future independent Order mutation must advance this stamp without
    // conflating it with hit-only policy changes.
    const u64 candidatePaintOrderRevision = candidateStructureRevision;
    usize writeHitBufferIndex = publishedHitBufferIndex;
    usize candidateHitTargetCount = committedHitTargetCount;
    u32 candidateActiveModalEntryIndex = committedActiveModalEntryIndex;
    if (hitNeedsCommit)
    {
        writeHitBufferIndex = 1 - publishedHitBufferIndex;
        auto hitResult = buildCommittedHit(committedHitBuffers[writeHitBufferIndex], candidateLayoutEntries);
        if (!hitResult)
        {
            return Core::failure(hitResult.error());
        }
        candidateHitTargetCount = hitResult->targetCount;
        candidateActiveModalEntryIndex = hitResult->activeModalEntryIndex;
    }
    const std::pmr::vector<UICommittedHitEntry>& candidateHitBuffer = committedHitBuffers[writeHitBufferIndex];
    const std::span<const UICommittedHitEntry> candidateHitEntries(candidateHitBuffer.data(),
                                                                   candidateHitBuffer.size());
    const UINodeId candidateActiveModalNode = candidateActiveModalEntryIndex < candidateHitEntries.size()
                                                  ? candidateHitEntries[candidateActiveModalEntryIndex].node
                                                  : UINodeId{};

    const UINodeId previousDefaultActionFocus = defaultActionFocusButton;
    const UINodeId previousTextInputFocus = textInputFocus;
    const UINodeId previousArmedPrimaryButton = armedPrimaryButton;
    const bool previousArmedPrimaryButtonPressed = armedPrimaryButtonPressed;
    const UINodeId previousHoveredPrimaryControl = hoveredPrimaryControl;
    const Detail::UIDefaultActionPressState previousDefaultActionPressState =
        defaultActionPressState;
    const UINodeId previousArmedSlider = armedSlider;
    const UINodeId previousArmedScrollView = armedScrollView;
    const UIScrollAxes previousArmedScrollAxis = armedScrollAxis;
    const float previousScrollDragGrabOffset = scrollDragGrabOffset;
    const bool previousScrollThumbDragActive = scrollThumbDragActive;
    const UINodeId previousArmedTextEdit = armedTextEdit;
    const UINodeId previousCapturedPointer = capturedPointerNode;
    const usize previousPublishedHitBufferIndex = publishedHitBufferIndex;
    const Detail::UIImeCompositionState previousImeComposition = imeComposition;
    StyleInteractionNodeSet styleInteractionCandidates{};
    bool styleInteractionCachesMayNeedRollback = false;
    struct FocusRestoreRollbackEntry final {
        u32 index = InvalidNodeIndex;
        UINodeId value{};
    };
    std::array<FocusRestoreRollbackEntry, 2> focusRestoreRollbackEntries{};
    usize focusRestoreRollbackCount = 0;
    const auto setFocusRestore = [&](u32 index, UINodeId value) noexcept {
        if (index >= focusRestoreByNodeIndex.size())
        {
            return;
        }
        bool alreadySaved = false;
        for (usize saved = 0; saved < focusRestoreRollbackCount; ++saved)
        {
            alreadySaved = alreadySaved || focusRestoreRollbackEntries[saved].index == index;
        }
        if (!alreadySaved && focusRestoreRollbackCount < focusRestoreRollbackEntries.size())
        {
            focusRestoreRollbackEntries[focusRestoreRollbackCount++] = {
                .index = index,
                .value = focusRestoreByNodeIndex[index],
            };
        }
        focusRestoreByNodeIndex[index] = value;
    };
    auto focusRollback = Core::makeScopeExit([&]() noexcept {
        defaultActionFocusButton = previousDefaultActionFocus;
        textInputFocus = previousTextInputFocus;
        armedPrimaryButton = previousArmedPrimaryButton;
        armedPrimaryButtonPressed = previousArmedPrimaryButtonPressed;
        hoveredPrimaryControl = previousHoveredPrimaryControl;
        defaultActionPressState = previousDefaultActionPressState;
        armedSlider = previousArmedSlider;
        armedScrollView = previousArmedScrollView;
        armedScrollAxis = previousArmedScrollAxis;
        scrollDragGrabOffset = previousScrollDragGrabOffset;
        scrollThumbDragActive = previousScrollThumbDragActive;
        armedTextEdit = previousArmedTextEdit;
        capturedPointerNode = previousCapturedPointer;
        imeComposition = previousImeComposition;
        for (usize saved = 0; saved < focusRestoreRollbackCount; ++saved)
        {
            const FocusRestoreRollbackEntry& entry = focusRestoreRollbackEntries[saved];
            if (entry.index < focusRestoreByNodeIndex.size())
            {
                focusRestoreByNodeIndex[entry.index] = entry.value;
            }
        }
        if (styleInteractionCachesMayNeedRollback)
        {
            for (usize index = 0; index < styleInteractionCandidates.count; ++index)
            {
                const UINodeId node = styleInteractionCandidates.nodes[index];
                if (contains(node))
                {
                    static_cast<void>(refreshResolvedStyleCache(node.index()));
                }
            }
        }
    });

    const auto firstFocusCandidateWithin = [&](u32 scopeEntryIndex) noexcept {
        for (u32 entryIndex = 0; entryIndex < candidateHitEntries.size(); ++entryIndex)
        {
            const UICommittedHitEntry& entry = candidateHitEntries[entryIndex];
            if (entry.policy == UIPointerHitPolicy::Targetable && contains(entry.node) &&
                isNodeEnabled(entry.node) && hasBehavior(entry.behaviors, UIElementBehavior::Focusable) &&
                hitEntryAllowedByModal(entry, candidateActiveModalEntryIndex) &&
                hitEntryIsWithinScope(entryIndex, scopeEntryIndex, candidateHitEntries))
            {
                return entry.node;
            }
        }
        return UINodeId{};
    };

    UINodeId desiredFocus = defaultActionFocusButton;
    const bool modalChanged = candidateActiveModalNode != activeModalNode;
    if (modalChanged)
    {
        UINodeId restoreFocus = defaultActionFocusButton;
        if (activeModalNode.hasValue())
        {
            if (contains(activeModalNode) && activeModalNode.index() < focusRestoreByNodeIndex.size())
            {
                restoreFocus = focusRestoreByNodeIndex[activeModalNode.index()];
            } else if (hasPendingDestroyedModalRestoreFocus)
            {
                restoreFocus = pendingDestroyedModalRestoreFocus;
            }
        }

        const u32 previousModalCandidateEntry = findHitEntryIndex(activeModalNode, candidateHitEntries);
        const bool openingNestedModal =
            candidateActiveModalEntryIndex < candidateHitEntries.size() &&
            previousModalCandidateEntry < candidateHitEntries.size() &&
            hitEntryIsWithinScope(candidateActiveModalEntryIndex, previousModalCandidateEntry, candidateHitEntries);
        if (openingNestedModal)
        {
            restoreFocus = defaultActionFocusButton;
        }

        if (activeModalNode.hasValue() && contains(activeModalNode) && !openingNestedModal)
        {
            setFocusRestore(activeModalNode.index(), {});
        }

        if (candidateActiveModalNode.hasValue())
        {
            const bool returningToExistingModal =
                candidateActiveModalNode.index() < focusRestoreByNodeIndex.size() &&
                focusRestoreByNodeIndex[candidateActiveModalNode.index()].hasValue() && !openingNestedModal;
            if (!returningToExistingModal)
            {
                setFocusRestore(candidateActiveModalNode.index(), restoreFocus);
            }
            desiredFocus = returningToExistingModal ? restoreFocus : defaultActionFocusButton;
        } else
        {
            desiredFocus = restoreFocus;
        }
    }

    if (!isKeyboardFocusCandidate(desiredFocus, candidateHitEntries, candidateActiveModalEntryIndex))
    {
        desiredFocus = candidateActiveModalNode.hasValue()
                           ? firstFocusCandidateWithin(candidateActiveModalEntryIndex)
                           : UINodeId{};
    }

    const u32 desiredFocusEntryIndex = findHitEntryIndex(desiredFocus, candidateHitEntries);
    const bool desiredFocusIsTextEdit = desiredFocusEntryIndex < candidateHitEntries.size() &&
                                        hasBehavior(candidateHitEntries[desiredFocusEntryIndex].behaviors,
                                                    UIElementBehavior::TextInput);
    const UINodeId desiredTextFocus = desiredFocusIsTextEdit ? desiredFocus : UINodeId{};
    const bool focusChanged = desiredFocus != defaultActionFocusButton || desiredTextFocus != textInputFocus;
    if (focusChanged)
    {
        defaultActionFocusButton = desiredFocus;
        textInputFocus = desiredTextFocus;
        defaultActionPressState.clearAll();
        if (desiredTextFocus != previousTextInputFocus)
        {
            resetImeCompositionState();
        }
        paintNeedsCommit = true;
        semanticsNeedsCommit = true;
    }

    const bool clearTextEditArm =
        armedTextEdit.hasValue() &&
        (!isLiveTextEdit(armedTextEdit) ||
         !isKeyboardFocusCandidate(armedTextEdit, candidateHitEntries, candidateActiveModalEntryIndex) ||
         armedTextEdit != desiredFocus);
    const bool clearPrimaryButtonArm =
        armedPrimaryButton.hasValue() &&
        !isPointerInteractionCandidate(armedPrimaryButton, candidateHitEntries, candidateActiveModalEntryIndex);
    const bool clearSliderArm =
        armedSlider.hasValue() &&
        !isPointerInteractionCandidate(armedSlider, candidateHitEntries, candidateActiveModalEntryIndex);
    const bool clearScrollViewArm =
        armedScrollView.hasValue() &&
        !isPointerInteractionCandidate(armedScrollView, candidateHitEntries, candidateActiveModalEntryIndex);
    const bool clearControlHover =
        hoveredPrimaryControl.hasValue() &&
        !isPointerInteractionCandidate(hoveredPrimaryControl, candidateHitEntries, candidateActiveModalEntryIndex);
    const bool clearPointerCapture =
        capturedPointerNode.hasValue() &&
        !isPointerCaptureCandidate(capturedPointerNode, candidateHitEntries, candidateActiveModalEntryIndex);
    if (clearTextEditArm)
    {
        armedTextEdit = {};
        paintNeedsCommit = true;
    }
    if (clearPrimaryButtonArm || clearSliderArm || clearScrollViewArm || clearControlHover)
    {
        if (clearPrimaryButtonArm)
        {
            clearArmedPrimaryButton();
        }
        if (clearSliderArm)
        {
            clearArmedSlider();
        }
        if (clearScrollViewArm)
        {
            clearArmedScrollView();
        }
        if (clearControlHover)
        {
            clearHoveredPrimaryControl();
        }
        paintNeedsCommit = true;
    }

    usize writePaintBufferIndex = publishedPaintBufferIndex;
    PaintCacheRebuildStatistics candidatePaintCacheStatistics{};
    if (paintNeedsCommit)
    {
        styleInteractionCachesMayNeedRollback = true;
        candidatePaintCacheStatistics =
            rebuildDirtyPaintCaches(candidateLayoutEntries, styleInteractionCandidates);
        auto paintCapacity = validatePaintCandidateCapacity(candidateLayoutEntries, true);
        if (!paintCapacity)
        {
            return Core::failure(paintCapacity.error());
        }
        writePaintBufferIndex = 1 - publishedPaintBufferIndex;
        if (Core::Status status =
                buildCommittedPaint(
                    committedPaintBuffers[writePaintBufferIndex], candidateLayoutEntries, true);
            !status)
        {
            return status;
        }
    }

    usize writeSemanticsBufferIndex = publishedSemanticsBufferIndex;
    if (semanticsNeedsCommit)
    {
        writeSemanticsBufferIndex = 1 - publishedSemanticsBufferIndex;
        if (Core::Status status = buildCommittedSemantics(committedSemanticsBuffers[writeSemanticsBufferIndex],
                                                          committedSemanticsTextBuffers[writeSemanticsBufferIndex],
                                                          candidateLayoutEntries,
                                                          layoutNeedsCommit);
            !status)
        {
            return status;
        }
    }

    // Every fallible candidate builder has succeeded. Commit the sampled
    // presentation and any completed final targets immediately before the
    // corresponding Layout/Hit/Paint/Semantics snapshots are published.
    if (hasTransactionalDirectMotionSample)
    {
        motionTrackStorage.commitCandidateSample();
        for (const Detail::UIMotionTrackStorage::Completed& completed :
             motionTrackStorage.lastCompleted())
        {
            applyCompletedMotion(completed);
        }
    }
    if (hasTransactionalTimelineSample)
    {
        timelineStorage.commitCandidateSample();
        const auto timelineTargets = timelineStorage.lastTargets();
        for (const Detail::UIKeyframeTimelineStorage::Target& target : timelineTargets)
        {
            applyTimelineTarget(target);
        }
    }
    timelineSampleRollback.release();

    if (imageCandidateTransactionActive)
    {
        imageContentStorage.commitCandidateTransaction();
        imageCandidateTransactionActive = false;
    }

    if (previousTextInputFocus != textInputFocus)
    {
        resetTextEditPreferredX(previousTextInputFocus);
        resetTextEditPreferredX(textInputFocus);
    }

    // Publish visual rows atomically with the snapshots that were built
    // from them. Any earlier return leaves route-visible state untouched.
    textEditVisualLinesByNodeIndex.swap(candidateTextEditVisualLinesByNodeIndex);
    textEditVisualLayoutsByNodeIndex.swap(candidateTextEditVisualLayoutsByNodeIndex);
    textEditScrollYByNodeIndex.swap(candidateTextEditScrollYByNodeIndex);

    if (structureNeedsCommit)
    {
        publishedBufferIndex = writeStructureBufferIndex;
        ++committedRevision;
    }
    if (layoutNeedsCommit)
    {
        publishedLayoutBufferIndex = writeLayoutBufferIndex;
        ++committedLayoutRevision;
        committedLayoutStructureRevision = candidateStructureRevision;
        committedViewportSize = viewportSize;
        hasCommittedViewport = true;
    }
    if (hitNeedsCommit)
    {
        publishedHitBufferIndex = writeHitBufferIndex;
        ++committedHitRevision;
        committedHitStructureRevision = candidateStructureRevision;
        committedHitLayoutRevision = candidateLayoutRevision;
        committedHitPaintOrderRevision = candidatePaintOrderRevision;
        committedHitTargetCount = candidateHitTargetCount;
        committedActiveModalEntryIndex = candidateActiveModalEntryIndex;
        activeModalNode = candidateActiveModalNode;
    }
    if (paintNeedsCommit)
    {
        publishedPaintBufferIndex = writePaintBufferIndex;
        ++committedPaintRevision;
        committedPaintStructureRevision = candidateStructureRevision;
        committedPaintLayoutRevision = candidateLayoutRevision;
        committedPaintOrderRevision = candidatePaintOrderRevision;
        committedPaintViewportSize = viewportSize;
        committedTextInputCaretRect = candidateTextInputCaretRect;
        committedStyleInteractionNodes = currentStyleInteractionNodes();
    }
    if (semanticsNeedsCommit)
    {
        publishedSemanticsBufferIndex = writeSemanticsBufferIndex;
        ++committedSemanticsRevision;
        committedSemanticsStructureRevision = candidateStructureRevision;
        committedSemanticsLayoutRevision = candidateLayoutRevision;
        committedSemanticsViewportSize = viewportSize;
    }
    lastLayoutPass = layoutNeedsCommit ? pass : LayoutPassStatistics{};
    lastHitRebuildCount = hitNeedsCommit ? 1 : 0;
    lastPaintCacheRebuildCount = candidatePaintCacheStatistics.paintCacheRebuildCount;
    lastPaintSnapshotRebuildCount = paintNeedsCommit ? 1 : 0;
    lastStyleInspectedNodeCount = candidatePaintCacheStatistics.styleInspectedNodeCount;
    lastStyleResolvedNodeCount = candidatePaintCacheStatistics.styleResolvedNodeCount;
    lastStyleCandidateRuleCount = candidatePaintCacheStatistics.styleCandidateRuleCount;
    if (layoutNeedsCommit)
    {
        publishControlLayoutState(layoutOrderScratch);
        layoutReuseCacheValid = true;
    }
    clearDirtyState();
    tooltipAdvanceRollback.release();
    reconcileTooltipAfterPublication(frameNow);
    reconcileMenuAfterPublication();
    pendingDestroyedModalRestoreFocus = {};
    hasPendingDestroyedModalRestoreFocus = false;
    focusRollback.release();
    if (clearPointerCapture)
    {
        const auto& previousHitEntries = committedHitBuffers[previousPublishedHitBufferIndex];
        dispatchPointerCancelToCapture(previousHitEntries);
    }
    return Core::success();
}

[[nodiscard]] UICommittedStructureView UIContext::Impl::committedStructure() const noexcept
{
    const std::pmr::vector<UICommittedNodeEntry>& entries = committedBuffers[publishedBufferIndex];
    return UICommittedStructureView{
        std::span<const UICommittedNodeEntry>(entries.data(), entries.size()),
        committedRevision,
    };
}

[[nodiscard]] UICommittedLayoutView UIContext::Impl::committedLayout() const noexcept
{
    const std::pmr::vector<UICommittedLayoutEntry>& entries = committedLayoutBuffers[publishedLayoutBufferIndex];
    return UICommittedLayoutView{
        std::span<const UICommittedLayoutEntry>(entries.data(), entries.size()),
        committedLayoutStructureRevision,
        committedLayoutRevision,
    };
}

[[nodiscard]] UICommittedHitView UIContext::Impl::committedHit() const noexcept
{
    const std::pmr::vector<UICommittedHitEntry>& entries = committedHitBuffers[publishedHitBufferIndex];
    return UICommittedHitView{
        std::span<const UICommittedHitEntry>(entries.data(), entries.size()),
        committedHitStructureRevision,
        committedHitLayoutRevision,
        committedHitPaintOrderRevision,
        committedHitRevision,
        committedActiveModalEntryIndex,
    };
}

[[nodiscard]] UICommittedPaintView UIContext::Impl::committedPaint() const noexcept
{
    const std::pmr::vector<UICommittedPaintEntry>& entries = committedPaintBuffers[publishedPaintBufferIndex];
    return UICommittedPaintView{
        std::span<const UICommittedPaintEntry>(entries.data(), entries.size()),
        committedPaintViewportSize,
        committedPaintStructureRevision,
        committedPaintLayoutRevision,
        committedPaintOrderRevision,
        committedPaintRevision,
    };
}

[[nodiscard]] std::optional<UILogicalRect> UIContext::Impl::committedTextInputCaretRectValue() const noexcept
{
    return committedTextInputCaretRect;
}

[[nodiscard]] UICommittedSemanticsView UIContext::Impl::committedSemantics() const noexcept
{
    const std::pmr::vector<UISemanticsEntry>& entries = committedSemanticsBuffers[publishedSemanticsBufferIndex];
    return UICommittedSemanticsView{
        std::span<const UISemanticsEntry>(entries.data(), entries.size()),
        committedSemanticsViewportSize,
        committedSemanticsStructureRevision,
        committedSemanticsLayoutRevision,
        committedSemanticsRevision,
    };
}

[[nodiscard]] std::span<const u8> UIContext::Impl::glyphAtlasPixels() const noexcept
{
    if (!glyphAtlas)
    {
        return {};
    }
    return glyphAtlas->pagePixels();
}

[[nodiscard]] u32 UIContext::Impl::glyphAtlasWidth() const noexcept
{
    return glyphAtlas ? glyphAtlas->capacity().width : 0U;
}

[[nodiscard]] u32 UIContext::Impl::glyphAtlasHeight() const noexcept
{
    return glyphAtlas ? glyphAtlas->capacity().height : 0U;
}

} // namespace Tina::UI
