#include "detail/UIContextImpl.hpp"

namespace Tina::UI {

UIContext::Impl::Impl(Platform::WindowId owner, UIContextCapacityConfig capacities, std::thread::id threadId,
     std::shared_ptr<Detail::UIContextLifetimeControl> lifetimeControl,
     std::unique_ptr<UIAllocationLedger> ownedAllocationLedger, NodePool&& nodePool)
    : allocationLedger(std::move(ownedAllocationLedger)), ownerWindow(owner), capacityConfig(capacities),
      ownerThreadId(threadId), lifetime(std::move(lifetimeControl)), nodes(std::move(nodePool)),
      idsByIndex(&allocationLedger->resource()), layoutStylesByIndex(&allocationLedger->resource()),
      pointerHitPoliciesByIndex(&allocationLedger->resource()), enabledByNodeIndex(&allocationLedger->resource()),
      focusScopeModesByNodeIndex(&allocationLedger->resource()), focusRestoreByNodeIndex(&allocationLedger->resource()),
      flowStatesByNodeIndex(&allocationLedger->resource()), styleRolesByNodeIndex(&allocationLedger->resource()),
      styleSheetStorage({
                            .classCapacity = capacities.styleClassCapacity,
                            .tokenCapacity = capacities.styleTokenCapacity,
                            .ruleCapacity = capacities.styleRuleCapacity,
                            .bucketCapacity = capacities.styleBucketCapacity,
                            .maxRulesPerBucket = capacities.styleRulesPerBucketCapacity,
                        },
                        allocationLedger->resource()),
      motionTrackStorage(capacities.motionTrackCapacity, allocationLedger->resource()),
      timelineStorage(capacities.nodeCapacity, capacities.timelineCapacity,
                      capacities.timelineTrackCapacity, capacities.timelineKeyframeCapacity,
                      capacities.activeTimelineCapacity, allocationLedger->resource()),
      timelineLayoutNodeScratch(&allocationLedger->resource()),
      timelinePaintNodeScratch(&allocationLedger->resource()),
      presentationOpacityByNodeIndex(&allocationLedger->resource()),
      presentationOpacityValidByNodeIndex(&allocationLedger->resource()),
      presentationOffsetXByNodeIndex(&allocationLedger->resource()),
      presentationOffsetYByNodeIndex(&allocationLedger->resource()),
      presentationOffsetValidByNodeIndex(&allocationLedger->resource()),
      styleClassesByNodeIndex(&allocationLedger->resource()), styleClassCountsByNodeIndex(&allocationLedger->resource()),
      styleStatesByNodeIndex(&allocationLedger->resource()),
      resolvedStyleInitializedByNodeIndex(&allocationLedger->resource()), resolvedBoxFillCacheByNodeIndex(&allocationLedger->resource()),
      resolvedStyleColorTokenByNodeIndex(&allocationLedger->resource()),
      styleTokenDependencyNextByNodeIndex(&allocationLedger->resource()),
      styleTokenDependencyPrevByNodeIndex(&allocationLedger->resource()),
      styleTokenDependencyHeadByTokenIndex(&allocationLedger->resource()),
      resolvedImageTintTokenByNodeIndex(&allocationLedger->resource()),
      imageTintTokenDependencyNextByNodeIndex(&allocationLedger->resource()),
      imageTintTokenDependencyPrevByNodeIndex(&allocationLedger->resource()),
      imageTintTokenDependencyHeadByTokenIndex(&allocationLedger->resource()),
      resolvedImageTintCacheByNodeIndex(&allocationLedger->resource()),
      resolvedImageTintValidByNodeIndex(&allocationLedger->resource()),
      boxPaintsByIndex(&allocationLedger->resource()),
      buttonPaintsByNodeIndex(&allocationLedger->resource()),
      themeBindingsByNodeIndex(&allocationLedger->resource()), styleOverridesByNodeIndex(&allocationLedger->resource()),
      themeDirtyScratchByNodeIndex(&allocationLedger->resource()),
      themeTextMetricsScratchByNodeIndex(&allocationLedger->resource()), localSolidFillCacheByIndex(&allocationLedger->resource()),
      localTextColorCacheByIndex(&allocationLedger->resource()), textStatesByIndex(&allocationLedger->resource()),
      semanticsStatesByNodeIndex(&allocationLedger->resource()),
      paintSnapshotBuilder(capacities.paintSnapshotCapacity),
      semanticsSnapshotBuilder(capacities.nodeCapacity, capacities.nodeCapacity, allocationLedger->resource()),
      canvasCommandStorage(capacities.nodeCapacity, capacities.canvasCommandCapacity, allocationLedger->resource()),
      imageContentStorage(capacities.nodeCapacity, capacities.imageContentCapacity, allocationLedger->resource()),
       textEditPaintsByNodeIndex(&allocationLedger->resource()), textEditMultilineByNodeIndex(&allocationLedger->resource()),
       textEditVisualLinesByNodeIndex(&allocationLedger->resource()), textEditVisualLayoutsByNodeIndex(&allocationLedger->resource()),
       textEditScrollYByNodeIndex(&allocationLedger->resource()), candidateTextEditVisualLinesByNodeIndex(&allocationLedger->resource()),
       candidateTextEditVisualLayoutsByNodeIndex(&allocationLedger->resource()), candidateTextEditScrollYByNodeIndex(&allocationLedger->resource()),
       textEditPreferredXByNodeIndex(&allocationLedger->resource()), textEditCaretAffinityByNodeIndex(&allocationLedger->resource()),
      progressBarStatesByNodeIndex(&allocationLedger->resource()), radioButtonStatesByNodeIndex(&allocationLedger->resource()),
      scrollViewPaintsByNodeIndex(&allocationLedger->resource()), scrollViewLayoutScratchByNodeIndex(&allocationLedger->resource()),
      dropdownStatesByNodeIndex(&allocationLedger->resource()), popupStatesByNodeIndex(&allocationLedger->resource()),
      popupLayoutScratchByNodeIndex(&allocationLedger->resource()),
      tooltipStorage(capacities.componentStates.tooltipCapacity,
                     allocationLedger->resource()),
      dialogStorage(capacities.componentStates.dialogCapacity,
                    allocationLedger->resource()),
      splitViewStorage(capacities.componentStates.splitViewCapacity,
                       capacities.componentStates.splitterCapacity,
                       allocationLedger->resource()),
      tabViewStorage(capacities.componentStates.tabViewCapacity,
                     capacities.componentStates.tabCapacity,
                     allocationLedger->resource()),
      menuStorage(capacities.componentStates.menuCapacity,
                  capacities.componentStates.menuItemCapacity,
                  allocationLedger->resource()),
      menuMutationNodeScratch(&allocationLedger->resource()),
      listViewStatesByNodeIndex(&allocationLedger->resource()),
      listViewLayoutScratchByNodeIndex(&allocationLedger->resource()), listViewItemStatesByNodeIndex(&allocationLedger->resource()),
      virtualGridViewStorage(
          capacities.componentStates.virtualGridViewCapacity,
          capacities.componentStates.virtualGridItemCapacity,
          allocationLedger->resource()),
      virtualGridItemLinkScratch(&allocationLedger->resource()),
      dataGridStorage(
          capacities.componentStates.dataGridCapacity,
          capacities.componentStates.dataGridColumnCapacity,
          capacities.componentStates.dataGridRowCapacity,
          capacities.componentStates.dataGridCellCapacity,
          capacities.nodeCapacity, allocationLedger->resource()),
      dataGridColumnLinkScratch(&allocationLedger->resource()),
      dataGridRowLinkScratch(&allocationLedger->resource()),
      dataGridCellLinkScratch(&allocationLedger->resource()),
      dataGridColumnWidthScratch(&allocationLedger->resource()),
      treeViewStatesByNodeIndex(&allocationLedger->resource()), treeViewLayoutScratchByNodeIndex(&allocationLedger->resource()),
      treeViewItemStatesByNodeIndex(&allocationLedger->resource()),
      textStorage(capacities.textByteCapacity, capacities.nodeCapacity * 2U, allocationLedger->resource()),
      dirtyQueueStorage(capacities.nodeCapacity, capacities.dirtyQueueCapacity, allocationLedger->resource()),
      layoutScratchByIndex(&allocationLedger->resource()),
      layoutWorkByIndex(&allocationLedger->resource()), layoutOrderScratch(&allocationLedger->resource()),
      hitEntryIndexByNodeIndex(&allocationLedger->resource()),
      routedPointerListenerRegistry(capacities.nodeCapacity, capacities.routedPointerListenerCapacity,
                                    allocationLedger->resource()),
      routePathScratch(&allocationLedger->resource()), pointerCancelRoutePathScratch(&allocationLedger->resource()),
      buttonActionRegistry(capacities.nodeCapacity, capacities.buttonActionCapacity, allocationLedger->resource()),
      behaviorStateStorage(capacities.nodeCapacity, capacities.nodeCapacity,
                           capacities.nodeCapacity, capacities.nodeCapacity, capacities.nodeCapacity,
                           capacities.nodeCapacity, capacities.nodeCapacity, allocationLedger->resource()),
      defaultActionPressState(owner), flowActionPressState(owner), rangeInputPressLatch(owner),
      checkboxPaintsByNodeIndex(&allocationLedger->resource()),
      sliderPaintsByNodeIndex(&allocationLedger->resource()),
      sliderChangeCallbackRegistry(capacities.nodeCapacity, allocationLedger->resource()),
      committedBuffers{std::pmr::vector<UICommittedNodeEntry>(&allocationLedger->resource()),
                       std::pmr::vector<UICommittedNodeEntry>(&allocationLedger->resource())},
      committedLayoutBuffers{std::pmr::vector<UICommittedLayoutEntry>(&allocationLedger->resource()),
                             std::pmr::vector<UICommittedLayoutEntry>(&allocationLedger->resource())},
      committedLayoutDebugBuffers{std::pmr::vector<UILayoutDebugEntry>(&allocationLedger->resource()),
                                  std::pmr::vector<UILayoutDebugEntry>(&allocationLedger->resource())},
      committedHitBuffers{std::pmr::vector<UICommittedHitEntry>(&allocationLedger->resource()),
                          std::pmr::vector<UICommittedHitEntry>(&allocationLedger->resource())},
      committedPaintBuffers{std::pmr::vector<UICommittedPaintEntry>(&allocationLedger->resource()),
                            std::pmr::vector<UICommittedPaintEntry>(&allocationLedger->resource())},
      committedSemanticsBuffers{std::pmr::vector<UISemanticsEntry>(&allocationLedger->resource()),
                                std::pmr::vector<UISemanticsEntry>(&allocationLedger->resource())},
      committedSemanticsTextBuffers{std::pmr::vector<char>(&allocationLedger->resource()),
                                    std::pmr::vector<char>(&allocationLedger->resource())},
      componentBuildReservationsByNodeIndex(&allocationLedger->resource())
{
    for (usize index = 0; index < observedFlowInputDevices.size(); ++index)
    {
        observedFlowInputDevices[index].localUser =
            UIFlowLocalUserId{static_cast<u32>(index + 1U)};
    }
}

[[nodiscard]] std::pmr::memory_resource& UIContext::Impl::allocationMemoryResource() noexcept
{
    return allocationLedger->resource();
}

[[nodiscard]] Core::Result<std::unique_ptr<UIContext::Impl>>
UIContext::Impl::Create(Platform::WindowId ownerWindow, NormalizedUIContextCapacityConfig normalized,
       std::shared_ptr<Detail::UIContextLifetimeControl> lifetimeControl, std::pmr::memory_resource& resource)
{
    auto allocationLedger = std::make_unique<UIAllocationLedger>(resource);
    auto poolResult = NodePool::Create(normalized.nodeCapacity, allocationLedger->resource());
    if (!poolResult)
    {
        const Core::Error& error = poolResult.error();
        if (error.code == Core::CoreErrorCode::CapacityExceeded)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI node pool capacity could not be reserved");
        }
        return Core::failure(error);
    }
    const usize nodePoolBytes = allocationLedger->statistics().currentBytes;

    UIContextCapacityConfig capacities{
        .nodeCapacity = normalized.nodeCapacity,
        .rootCapacity = normalized.rootCapacity,
        .dirtyQueueCapacity = normalized.dirtyQueueCapacity,
        .layoutSnapshotCapacity = normalized.layoutSnapshotCapacity,
        .hitSnapshotCapacity = normalized.hitSnapshotCapacity,
        .paintSnapshotCapacity = normalized.paintSnapshotCapacity,
        .canvasCommandCapacity = normalized.canvasCommandCapacity,
        .imageContentCapacity = normalized.imageContentCapacity,
        .routePathCapacity = normalized.routePathCapacity,
        .layoutDebuggerSnapshotCapacity = normalized.layoutDebuggerSnapshotCapacity,
        .routedPointerListenerCapacity = normalized.routedPointerListenerCapacity,
        .buttonActionCapacity = normalized.buttonActionCapacity,
        .textByteCapacity = normalized.textByteCapacity,
        .textEditVisualLineCapacity = normalized.textEditVisualLineCapacity,
        .styleClassCapacity = normalized.styleClassCapacity,
        .styleTokenCapacity = normalized.styleTokenCapacity,
        .styleRuleCapacity = normalized.styleRuleCapacity,
        .styleBucketCapacity = normalized.styleBucketCapacity,
        .styleRulesPerBucketCapacity = normalized.styleRulesPerBucketCapacity,
        .nodeStyleClassLinkCapacity = normalized.nodeStyleClassLinkCapacity,
        .motionTrackCapacity = normalized.motionTrackCapacity,
        .timelineCapacity = normalized.timelineCapacity,
        .timelineTrackCapacity = normalized.timelineTrackCapacity,
        .timelineKeyframeCapacity = normalized.timelineKeyframeCapacity,
        .activeTimelineCapacity = normalized.activeTimelineCapacity,
        .flowLayerCapacity = normalized.flowLayerCapacity,
        .flowScreenCapacity = normalized.flowScreenCapacity,
        .componentStates = normalized.componentStates,
        .applyDefaultProductChrome = normalized.applyDefaultProductChrome,
    };

    auto impl = std::unique_ptr<Impl>(new Impl(ownerWindow, capacities, std::this_thread::get_id(),
                                               std::move(lifetimeControl), std::move(allocationLedger),
                                               std::move(*poolResult)));
    impl->pmrNodePoolBytes = nodePoolBytes;
    usize allocationCheckpoint = impl->allocationLedger->statistics().currentBytes;
    impl->pmrStateStorageBytes = allocationIncrease(nodePoolBytes, allocationCheckpoint);
    impl->motionClock = &impl->motionDefaultClock;
    impl->timelineLayoutNodeScratch.reserve(normalized.timelineTrackCapacity);
    impl->timelinePaintNodeScratch.reserve(normalized.timelineTrackCapacity);
    impl->menuMutationNodeScratch.reserve(normalized.nodeCapacity);
    impl->virtualGridItemLinkScratch.reserve(
        normalized.componentStates.virtualGridItemCapacity);
    impl->dataGridColumnLinkScratch.reserve(
        normalized.componentStates.dataGridColumnCapacity);
    impl->dataGridRowLinkScratch.reserve(
        normalized.componentStates.dataGridRowCapacity);
    impl->dataGridCellLinkScratch.reserve(
        normalized.componentStates.dataGridCellCapacity);
    impl->dataGridColumnWidthScratch.reserve(
        normalized.componentStates.dataGridColumnCapacity);
    usize nextAllocationCheckpoint = impl->allocationLedger->statistics().currentBytes;
    impl->pmrScratchReserveBytes =
        allocationIncrease(allocationCheckpoint, nextAllocationCheckpoint);
    allocationCheckpoint = nextAllocationCheckpoint;
    impl->presentationOpacityByNodeIndex.resize(normalized.nodeCapacity, 1.0F);
    impl->presentationOpacityValidByNodeIndex.resize(normalized.nodeCapacity, 0);
    impl->presentationOffsetXByNodeIndex.resize(normalized.nodeCapacity, 0.0F);
    impl->presentationOffsetYByNodeIndex.resize(normalized.nodeCapacity, 0.0F);
    impl->presentationOffsetValidByNodeIndex.resize(normalized.nodeCapacity, 0);
    impl->idsByIndex.resize(normalized.nodeCapacity);
    impl->layoutStylesByIndex.resize(normalized.nodeCapacity);
    impl->pointerHitPoliciesByIndex.resize(normalized.nodeCapacity, UIPointerHitPolicy::Ignore);
    impl->enabledByNodeIndex.resize(normalized.nodeCapacity, 1);
    impl->focusScopeModesByNodeIndex.resize(normalized.nodeCapacity, UIFocusScopeMode::None);
    impl->focusRestoreByNodeIndex.resize(normalized.nodeCapacity);
    impl->flowStatesByNodeIndex.resize(normalized.nodeCapacity);
    impl->styleRolesByNodeIndex.resize(normalized.nodeCapacity, UIStyleRoleId::None);
    impl->styleClassesByNodeIndex.resize(normalized.nodeCapacity);
    impl->styleClassCountsByNodeIndex.resize(normalized.nodeCapacity, 0);
    impl->styleStatesByNodeIndex.resize(normalized.nodeCapacity, UIStyleState::None);
    impl->resolvedStyleInitializedByNodeIndex.resize(normalized.nodeCapacity, 0);
    impl->resolvedBoxFillCacheByNodeIndex.resize(normalized.nodeCapacity);
    impl->resolvedStyleColorTokenByNodeIndex.resize(normalized.nodeCapacity);
    impl->styleTokenDependencyNextByNodeIndex.resize(normalized.nodeCapacity, 0);
    impl->styleTokenDependencyPrevByNodeIndex.resize(normalized.nodeCapacity, 0);
    impl->styleTokenDependencyHeadByTokenIndex.resize(normalized.styleTokenCapacity, 0);
    impl->resolvedImageTintTokenByNodeIndex.resize(normalized.nodeCapacity);
    impl->imageTintTokenDependencyNextByNodeIndex.resize(normalized.nodeCapacity, 0);
    impl->imageTintTokenDependencyPrevByNodeIndex.resize(normalized.nodeCapacity, 0);
    impl->imageTintTokenDependencyHeadByTokenIndex.resize(normalized.styleTokenCapacity, 0);
    impl->resolvedImageTintCacheByNodeIndex.resize(normalized.nodeCapacity);
    impl->resolvedImageTintValidByNodeIndex.resize(normalized.nodeCapacity, 0);
    impl->boxPaintsByIndex.resize(normalized.nodeCapacity);
    impl->buttonPaintsByNodeIndex.resize(normalized.nodeCapacity);
    impl->themeBindingsByNodeIndex.resize(normalized.nodeCapacity, 0);
    impl->styleOverridesByNodeIndex.resize(normalized.nodeCapacity, 0);
    impl->themeDirtyScratchByNodeIndex.resize(normalized.nodeCapacity, 0);
    impl->themeTextMetricsScratchByNodeIndex.resize(normalized.nodeCapacity);
    impl->localSolidFillCacheByIndex.resize(normalized.nodeCapacity);
    impl->localTextColorCacheByIndex.resize(normalized.nodeCapacity);
    impl->textStatesByIndex.resize(normalized.nodeCapacity);
    impl->semanticsStatesByNodeIndex.resize(normalized.nodeCapacity);
    impl->textEditPaintsByNodeIndex.resize(normalized.nodeCapacity);
    impl->textEditMultilineByNodeIndex.resize(normalized.nodeCapacity);
    impl->textEditVisualLinesByNodeIndex.resize(normalized.nodeCapacity);
    impl->textEditVisualLayoutsByNodeIndex.resize(normalized.nodeCapacity);
    impl->textEditScrollYByNodeIndex.resize(normalized.nodeCapacity, 0.0F);
    impl->candidateTextEditVisualLinesByNodeIndex.resize(normalized.nodeCapacity);
    impl->candidateTextEditVisualLayoutsByNodeIndex.resize(normalized.nodeCapacity);
    impl->candidateTextEditScrollYByNodeIndex.resize(normalized.nodeCapacity, 0.0F);
    impl->textEditPreferredXByNodeIndex.resize(normalized.nodeCapacity);
    impl->textEditCaretAffinityByNodeIndex.resize(
        normalized.nodeCapacity, Detail::UITextEditCaretAffinity::Downstream);
    impl->progressBarStatesByNodeIndex.resize(normalized.nodeCapacity);
    impl->radioButtonStatesByNodeIndex.resize(normalized.nodeCapacity);
    impl->scrollViewPaintsByNodeIndex.resize(normalized.nodeCapacity);
    impl->scrollViewLayoutScratchByNodeIndex.resize(normalized.nodeCapacity);
    impl->dropdownStatesByNodeIndex.resize(normalized.nodeCapacity);
    impl->popupStatesByNodeIndex.resize(normalized.nodeCapacity);
    impl->popupLayoutScratchByNodeIndex.resize(normalized.nodeCapacity);
    impl->listViewStatesByNodeIndex.resize(normalized.nodeCapacity);
    impl->listViewLayoutScratchByNodeIndex.resize(normalized.nodeCapacity);
    impl->listViewItemStatesByNodeIndex.resize(normalized.nodeCapacity);
    impl->treeViewStatesByNodeIndex.resize(normalized.nodeCapacity);
    impl->treeViewLayoutScratchByNodeIndex.resize(normalized.nodeCapacity);
    impl->treeViewItemStatesByNodeIndex.resize(normalized.nodeCapacity);
    impl->componentBuildReservationsByNodeIndex.resize(normalized.nodeCapacity);
    impl->layoutScratchByIndex.resize(normalized.nodeCapacity);
    impl->layoutWorkByIndex.resize(normalized.nodeCapacity, 0);
    impl->layoutOrderScratch.reserve(normalized.nodeCapacity);
    impl->hitEntryIndexByNodeIndex.resize(normalized.nodeCapacity, InvalidUIHitEntryIndex);
    impl->routePathScratch.reserve(normalized.routePathCapacity);
    impl->pointerCancelRoutePathScratch.reserve(normalized.routePathCapacity);
    impl->checkboxPaintsByNodeIndex.resize(normalized.nodeCapacity);
    impl->sliderPaintsByNodeIndex.resize(normalized.nodeCapacity);
    nextAllocationCheckpoint = impl->allocationLedger->statistics().currentBytes;
    impl->pmrIndexAlignedStorageBytes =
        allocationIncrease(allocationCheckpoint, nextAllocationCheckpoint);
    allocationCheckpoint = nextAllocationCheckpoint;
    impl->committedBuffers[0].reserve(normalized.nodeCapacity);
    impl->committedBuffers[1].reserve(normalized.nodeCapacity);
    impl->committedLayoutBuffers[0].reserve(normalized.layoutSnapshotCapacity);
    impl->committedLayoutBuffers[1].reserve(normalized.layoutSnapshotCapacity);
    impl->committedLayoutDebugBuffers[0].reserve(normalized.layoutDebuggerSnapshotCapacity);
    impl->committedLayoutDebugBuffers[1].reserve(normalized.layoutDebuggerSnapshotCapacity);
    impl->committedHitBuffers[0].reserve(normalized.hitSnapshotCapacity);
    impl->committedHitBuffers[1].reserve(normalized.hitSnapshotCapacity);
    impl->committedPaintBuffers[0].reserve(normalized.paintSnapshotCapacity);
    impl->committedPaintBuffers[1].reserve(normalized.paintSnapshotCapacity);
    impl->committedSemanticsBuffers[0].reserve(normalized.nodeCapacity);
    impl->committedSemanticsBuffers[1].reserve(normalized.nodeCapacity);
    impl->committedSemanticsTextBuffers[0].resize(normalized.textByteCapacity, '\0');
    impl->committedSemanticsTextBuffers[1].resize(normalized.textByteCapacity, '\0');
    nextAllocationCheckpoint = impl->allocationLedger->statistics().currentBytes;
    impl->pmrSnapshotBufferBytes =
        allocationIncrease(allocationCheckpoint, nextAllocationCheckpoint);
    impl->deferredRootDestroyBuffer.reserve(normalized.rootCapacity);
    impl->deferredRoutedPointerListenerReleaseBuffer.reserve(normalized.routedPointerListenerCapacity);
    return impl;
}

void UIContext::Impl::detachLifetime(UIContext* context) noexcept
{
    if (lifetime && context != nullptr)
    {
        lifetime->detach(*context);
    }
}

[[nodiscard]] UIContextStatistics UIContext::Impl::statistics() const noexcept
{
    const Detail::UIBehaviorStateStorageCounters behaviorCounters =
        behaviorStateStorage.counters();
    const Detail::UIStyleSheetStorageStatistics styleStatistics =
        styleSheetStorage.statistics();
    const Core::MemoryStatistics memoryStatistics = allocationLedger->statistics();
    return UIContextStatistics{
        .pmrCurrentBytes = memoryStatistics.currentBytes,
        .pmrPeakBytes = memoryStatistics.peakBytes,
        .pmrAllocationCount = memoryStatistics.allocationCount,
        .pmrDeallocationCount = memoryStatistics.deallocationCount,
        .pmrFailedAllocationCount = memoryStatistics.failedAllocationCount,
        .pmrInvalidDeallocationCount = memoryStatistics.invalidDeallocationCount,
        .pmrNodePoolBytes = pmrNodePoolBytes,
        .pmrStateStorageBytes = pmrStateStorageBytes,
        .pmrScratchReserveBytes = pmrScratchReserveBytes,
        .pmrIndexAlignedStorageBytes = pmrIndexAlignedStorageBytes,
        .pmrSnapshotBufferBytes = pmrSnapshotBufferBytes,
        .pmrGlyphAtlasBytes = pmrGlyphAtlasBytes,
        .nodeCapacity = capacityConfig.nodeCapacity,
        .rootCapacity = capacityConfig.rootCapacity,
        .dirtyQueueCapacity = dirtyQueueStorage.queueCapacity(),
        .layoutSnapshotCapacity = capacityConfig.layoutSnapshotCapacity,
        .hitSnapshotCapacity = capacityConfig.hitSnapshotCapacity,
        .paintSnapshotCapacity = capacityConfig.paintSnapshotCapacity,
        .canvasCommandCapacity = capacityConfig.canvasCommandCapacity,
        .activeCanvasCommandCount = canvasCommandStorage.activeCount(),
        .canvasCommandHighWater = canvasCommandStorage.highWater(),
        .imageContentCapacity = imageContentStorage.capacity(),
        .activeImageContentCount = imageContentStorage.activeCount(),
        .imageContentHighWater = imageContentStorage.highWater(),
        .routePathCapacity = capacityConfig.routePathCapacity,
        .layoutDebuggerSnapshotCapacity = capacityConfig.layoutDebuggerSnapshotCapacity,
        .routedPointerListenerCapacity = capacityConfig.routedPointerListenerCapacity,
        .activeRoutedPointerListenerCount = routedPointerListenerRegistry.activeCount(),
        .routedPointerListenerHighWater = routedPointerListenerRegistry.highWater(),
        .buttonActionCapacity = capacityConfig.buttonActionCapacity,
        .activeButtonActionCount = buttonActionRegistry.activeCount(),
        .buttonActionHighWater = buttonActionRegistry.highWater(),
        .activateBehaviorCapacity = behaviorStateStorage.activateCapacity(),
        .activeActivateBehaviorCount = behaviorStateStorage.activeActivateCount(),
        .activateBehaviorHighWater = behaviorStateStorage.activateHighWater(),
        .toggleBehaviorCapacity = behaviorStateStorage.toggleCapacity(),
        .activeToggleBehaviorCount = behaviorStateStorage.activeToggleCount(),
        .toggleBehaviorHighWater = behaviorStateStorage.toggleHighWater(),
        .rangeInputBehaviorCapacity = behaviorStateStorage.rangeInputCapacity(),
        .activeRangeInputBehaviorCount = behaviorStateStorage.activeRangeInputCount(),
        .rangeInputBehaviorHighWater = behaviorStateStorage.rangeInputHighWater(),
        .textInputBehaviorCapacity = behaviorStateStorage.textInputCapacity(),
        .activeTextInputBehaviorCount = behaviorStateStorage.activeTextInputCount(),
        .textInputBehaviorHighWater = behaviorStateStorage.textInputHighWater(),
        .scrollBehaviorCapacity = behaviorStateStorage.scrollCapacity(),
        .activeScrollBehaviorCount = behaviorStateStorage.activeScrollCount(),
        .scrollBehaviorHighWater = behaviorStateStorage.scrollHighWater(),
        .selectBehaviorCapacity = behaviorStateStorage.selectCapacity(),
        .activeSelectBehaviorCount = behaviorStateStorage.activeSelectCount(),
        .selectBehaviorHighWater = behaviorStateStorage.selectHighWater(),
        .textByteCapacity = textStorage.capacity(),
        .textByteUsed = textStorage.used(),
        .textByteHighWater = textStorage.highWater(),
        .liveNodeCount = nodes.activeCount(),
        .liveRootCount = liveRootCount,
        .committedNodeCount = committedBuffers[publishedBufferIndex].size(),
        .committedRevision = committedRevision,
        .committedLayoutNodeCount = committedLayoutBuffers[publishedLayoutBufferIndex].size(),
        .committedLayoutDebugNodeCount =
            committedLayoutDebugBuffers[publishedLayoutDebugBufferIndex].size(),
        .layoutRevision = committedLayoutRevision,
        .committedHitNodeCount = committedHitBuffers[publishedHitBufferIndex].size(),
        .committedHitTargetCount = committedHitTargetCount,
        .hitRevision = committedHitRevision,
        .paintOrderRevision = committedHitPaintOrderRevision,
        .committedPaintNodeCount = committedPaintBuffers[publishedPaintBufferIndex].size(),
        .paintRevision = committedPaintRevision,
        .committedSemanticsNodeCount = committedSemanticsBuffers[publishedSemanticsBufferIndex].size(),
        .semanticsRevision = committedSemanticsRevision,
        .structureDirty = isPhaseDirty(PhaseStructure),
        .layoutDirty = isPhaseDirty(PhaseLayout),
        .hitDirty = isPhaseDirty(PhaseHit),
        .paintDirty = isPhaseDirty(PhasePaint),
        .semanticsDirty = isPhaseDirty(PhaseSemantics),
        .lastLayoutPassCount = lastLayoutPass.passCount,
        .lastLayoutMeasuredNodeCount = lastLayoutPass.measuredNodeCount,
        .lastLayoutArrangedNodeCount = lastLayoutPass.arrangedNodeCount,
        .lastLayoutPercentMeasureFallbackCount = lastLayoutPass.percentMeasureFallbackCount,
        .lastHitRebuildCount = lastHitRebuildCount,
        .lastPaintCacheRebuildCount = lastPaintCacheRebuildCount,
        .lastPaintSnapshotRebuildCount = lastPaintSnapshotRebuildCount,
        .lastStyleInspectedNodeCount = lastStyleInspectedNodeCount,
        .lastStyleResolvedNodeCount = lastStyleResolvedNodeCount,
        .lastStyleCandidateRuleCount = lastStyleCandidateRuleCount,
        .lastStyleTokenUpdateInspectedNodeCount =
            lastStyleTokenUpdateInspectedNodeCount,
        .lastStyleTokenUpdateResolvedNodeCount =
            lastStyleTokenUpdateResolvedNodeCount,
        .lastStyleTokenUpdateAffectedNodeCount =
            lastStyleTokenUpdateAffectedNodeCount,
        .lastStyleTokenUpdateCandidateRuleCount =
            lastStyleTokenUpdateCandidateRuleCount,
        .dirtyQueuePendingCount = validDirtyQueueCount(),
        .dirtyQueueHighWater = dirtyQueueStorage.highWater(),
        .style = {
            .classCapacity = styleStatistics.classCapacity,
            .registeredClassCount = styleStatistics.registeredClassCount,
            .classHighWater = styleStatistics.classHighWater,
            .tokenCapacity = styleStatistics.tokenCapacity,
            .registeredTokenCount = styleStatistics.registeredTokenCount,
            .tokenHighWater = styleStatistics.tokenHighWater,
            .ruleCapacity = styleStatistics.ruleCapacity,
            .activeRuleCount = styleStatistics.activeRuleCount,
            .ruleHighWater = styleStatistics.ruleHighWater,
            .bucketCapacity = styleStatistics.bucketCapacity,
            .activeBucketCount = styleStatistics.activeBucketCount,
            .bucketHighWater = styleStatistics.bucketHighWater,
            .rulesPerBucketCapacity = styleStatistics.maxRulesPerBucket,
            .bucketCandidateHighWater = styleStatistics.bucketCandidateHighWater,
            .nodeClassLinkCapacity = capacityConfig.nodeStyleClassLinkCapacity,
            .activeNodeClassLinkCount = activeNodeStyleClassLinkCount,
            .nodeClassLinkHighWater = nodeStyleClassLinkHighWater,
            .compileFailureCount = styleStatistics.compileFailureCount,
            .capacityFailureCount = styleStatistics.capacityFailureCount +
                                    nodeStyleClassLinkCapacityFailureCount,
            .revision = styleStatistics.revision,
        },
        .componentBuild = {
            .nodes = componentBuildNodeStatistics,
            .textBytes = makePoolStatistics(
                textStorage.reservationRequestedBytes(),
                textStorage.reservationReservedBytes(),
                textStorage.reservationPublishedBytes(),
                textStorage.reservationCapacityFailureCount(),
                textStorage.outstandingReservedBytes()),
            .canvasCommands = makePoolStatistics(
                canvasCommandStorage.reservationRequestedCount(),
                canvasCommandStorage.reservationReservedCount(),
                canvasCommandStorage.reservationPublishedCount(),
                canvasCommandStorage.reservationCapacityFailureCount(),
                canvasCommandStorage.outstandingReservedCount()),
            .behaviors = {
                .activate = makePoolStatistics(
                    behaviorCounters.requested.activate,
                    behaviorCounters.reserved.activate,
                    behaviorCounters.published.activate,
                    behaviorCounters.capacityFailures.activate,
                    behaviorCounters.outstandingReservations.activate),
                .toggle = makePoolStatistics(
                    behaviorCounters.requested.toggle,
                    behaviorCounters.reserved.toggle,
                    behaviorCounters.published.toggle,
                    behaviorCounters.capacityFailures.toggle,
                    behaviorCounters.outstandingReservations.toggle),
                .range = makePoolStatistics(
                    behaviorCounters.requested.range,
                    behaviorCounters.reserved.range,
                    behaviorCounters.published.range,
                    behaviorCounters.capacityFailures.range,
                    behaviorCounters.outstandingReservations.range),
                .textInput = makePoolStatistics(
                    behaviorCounters.requested.textInput,
                    behaviorCounters.reserved.textInput,
                    behaviorCounters.published.textInput,
                    behaviorCounters.capacityFailures.textInput,
                    behaviorCounters.outstandingReservations.textInput),
                .scroll = makePoolStatistics(
                    behaviorCounters.requested.scroll,
                    behaviorCounters.reserved.scroll,
                    behaviorCounters.published.scroll,
                    behaviorCounters.capacityFailures.scroll,
                    behaviorCounters.outstandingReservations.scroll),
                .selection = makePoolStatistics(
                    behaviorCounters.requested.selection,
                    behaviorCounters.reserved.selection,
                    behaviorCounters.published.selection,
                    behaviorCounters.capacityFailures.selection,
                    behaviorCounters.outstandingReservations.selection),
            },
            .activeTransactionCount = activeBuildTransactionCount,
            .transactionFailureCount = componentBuildTransactionFailureCount,
        },
        .motion =
            {
                .trackCapacity = motionTrackStorage.capacity(),
                .reservedTrackCount = motionTrackStorage.reservedCount(),
                .reservedTrackHighWater = motionTrackStorage.reservedHighWater(),
                .activeTrackCount = motionTrackStorage.activeCount(),
                .trackHighWater = motionTrackStorage.highWater(),
                .lastSampledTrackCount = motionTrackStorage.lastSampledCount(),
                .timelineCapacity = timelineStorage.timelineCapacity(),
                .timelineCount = timelineStorage.timelineCount(),
                .timelineHighWater = timelineStorage.timelineHighWater(),
                .timelineTrackCapacity = timelineStorage.trackCapacity(),
                .timelineTrackCount = timelineStorage.trackCount(),
                .timelineTrackHighWater = timelineStorage.trackHighWater(),
                .keyframeCapacity = timelineStorage.keyframeCapacity(),
                .keyframeCount = timelineStorage.keyframeCount(),
                .keyframeHighWater = timelineStorage.keyframeHighWater(),
                .activeTimelineCapacity = timelineStorage.activeCapacity(),
                .activeTimelineCount = timelineStorage.activeCount(),
                .activeTimelineHighWater = timelineStorage.activeHighWater(),
                .lastSampledTimelineCount = timelineStorage.lastSampledTimelineCount(),
                .lastSampledTimelineTrackCount = timelineStorage.lastSampledTrackCount(),
                .lastSampledTimelineLayoutTrackCount = timelineStorage.lastSampledLayoutTrackCount(),
                .lastSampledKeyframeSegmentCount = timelineStorage.lastSampledSegmentCount(),
                .timelineCancelCount = timelineStorage.cancelCount(),
                .timelineRetargetCount = timelineStorage.retargetCount(),
                .layoutTimelineCommitFailureCount = layoutTimelineCommitFailureCount,
                .reducedMotion = reducedMotionEnabled,
            },
        .flow =
            {
                .layerCapacity = capacityConfig.flowLayerCapacity,
                .registeredLayerCount = registeredFlowLayerCount,
                .layerHighWater = flowLayerHighWater,
                .screenCapacity = capacityConfig.flowScreenCapacity,
                .registeredScreenCount = registeredFlowScreenCount,
                .screenHighWater = flowScreenHighWater,
                .stackedScreenCount = stackedFlowScreenCount,
                .stackHighWater = flowStackHighWater,
                .registeredActionCount = registeredFlowActionCount,
                .actionHighWater = flowActionHighWater,
                .actionInvocationCount = flowActionInvocationCount,
                .capacityFailureCount = flowCapacityFailureCount,
            },
    };
}

} // namespace Tina::UI
