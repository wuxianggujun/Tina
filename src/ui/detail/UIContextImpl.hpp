#pragma once

#include "UIContextPrivateTypes.hpp"
#include "UITextWrapping.hpp"

namespace Tina::UI {

using namespace Detail::UIContextPrivate;

struct UIContext::Impl final {
    static constexpr usize StyleInteractionNodeCapacity =
        2U * (7U + Detail::UIDefaultActionPressState::MaximumPressedTargetCount);

    struct StyleInteractionNodeSet final {
        std::array<UINodeId, StyleInteractionNodeCapacity> nodes{};
        usize count = 0;

        void add(UINodeId node) noexcept
        {
            if (!node.hasValue() ||
                std::find(nodes.begin(), nodes.begin() + count, node) != nodes.begin() + count)
            {
                return;
            }
            if (count < nodes.size())
            {
                nodes[count++] = node;
            }
        }

        void merge(const StyleInteractionNodeSet& other) noexcept
        {
            for (usize index = 0; index < other.count; ++index)
            {
                add(other.nodes[index]);
            }
        }
    };

    std::unique_ptr<UIAllocationLedger> allocationLedger;
    usize pmrNodePoolBytes = 0;
    usize pmrStateStorageBytes = 0;
    usize pmrScratchReserveBytes = 0;
    usize pmrIndexAlignedStorageBytes = 0;
    usize pmrSnapshotBufferBytes = 0;
    usize pmrGlyphAtlasBytes = 0;
    Platform::WindowId ownerWindow{};
    UIContextCapacityConfig capacityConfig{};
    std::thread::id ownerThreadId{};
    // Active product theme used when create* installs default control chrome.
    // Not a global singleton; one copy per UIContext. Local set*Paint overrides
    // remain on the node after create.
    UITheme productTheme = makeModernDesktopTheme();
    std::shared_ptr<Detail::UIContextLifetimeControl> lifetime;
    NodePool nodes;
    std::pmr::vector<UINodeId> idsByIndex;
    std::pmr::vector<UILayoutStyle> layoutStylesByIndex;
    std::pmr::vector<UIPointerHitPolicy> pointerHitPoliciesByIndex;
    // Enabled state is index-aligned with the node pool. A byte keeps the
    // side-array deterministic and avoids introducing a packed-bit mutation
    // path for a frequently queried interaction property.
    std::pmr::vector<u8> enabledByNodeIndex;
    std::pmr::vector<UIFocusScopeMode> focusScopeModesByNodeIndex;
    // A Modal stores the focus that should be restored when that committed
    // scope stops being topmost. Storage is fixed and index-aligned.
    std::pmr::vector<UINodeId> focusRestoreByNodeIndex;
    std::pmr::vector<UIFlowNodeState> flowStatesByNodeIndex;
    usize registeredFlowLayerCount = 0;
    usize flowLayerHighWater = 0;
    usize registeredFlowScreenCount = 0;
    usize flowScreenHighWater = 0;
    usize stackedFlowScreenCount = 0;
    usize flowStackHighWater = 0;
    usize registeredFlowActionCount = 0;
    usize flowActionHighWater = 0;
    usize flowActionInvocationCount = 0;
    usize flowActionCallbackOperationDepth = 0;
    usize flowCapacityFailureCount = 0;
    std::array<UIFlowInputDeviceState, UIFlowLocalUserCapacity>
        observedFlowInputDevices{};
    std::array<UIFlowGamepadAssignment,
               Platform::PlatformFrameBuilder::MaximumGamepadSlots>
        flowGamepadAssignments{};
    std::pmr::vector<UIStyleRoleId> styleRolesByNodeIndex;
    Detail::UIStyleSheetStorage styleSheetStorage;
    Detail::UIMotionTrackStorage motionTrackStorage;
    Detail::UIKeyframeTimelineStorage timelineStorage;
    std::pmr::vector<UINodeId> timelineLayoutNodeScratch;
    std::pmr::vector<UINodeId> timelinePaintNodeScratch;
    const Core::IMonotonicClock* motionClock = nullptr;
    Core::SteadyMonotonicClock motionDefaultClock{};
    bool reducedMotionEnabled = false;
    usize layoutTimelineCommitFailureCount = 0;
    // Default duration 0 resolves stylesheet BoxFill immediately.
    UITransitionSpec styleBackgroundColorTransitionSpec{
        .property = UIAnimatableProperty::BackgroundColor,
        .duration = Core::Duration{0.0},
        .delay = Core::Duration{0.0},
        .easing = UIEasing::EaseOut,
    };
    // Residual presentation after tracks complete (still paint-only; not hit).
    std::pmr::vector<float> presentationOpacityByNodeIndex;
    std::pmr::vector<u8> presentationOpacityValidByNodeIndex;
    std::pmr::vector<float> presentationOffsetXByNodeIndex;
    std::pmr::vector<float> presentationOffsetYByNodeIndex;
    std::pmr::vector<u8> presentationOffsetValidByNodeIndex;
    std::pmr::vector<std::array<UIStyleClassId, Detail::MaxStyleClassesPerNode>>
        styleClassesByNodeIndex;
    std::pmr::vector<u8> styleClassCountsByNodeIndex;
    std::pmr::vector<UIStyleState> styleStatesByNodeIndex;
    std::pmr::vector<u8> resolvedStyleInitializedByNodeIndex;
    std::pmr::vector<UIPremultipliedRgba8Color> resolvedBoxFillCacheByNodeIndex;
    // Winning ColorToken dependency for reverse invalidation. Zero means the
    // node is not linked. Heads/next/prev use 1-based node indices so 0 is NIL.
    // Box fill and image tint keep independent reverse lists so one node can
    // depend on two tokens (or the same token once per channel).
    std::pmr::vector<UIStyleTokenId> resolvedStyleColorTokenByNodeIndex;
    std::pmr::vector<u32> styleTokenDependencyNextByNodeIndex;
    std::pmr::vector<u32> styleTokenDependencyPrevByNodeIndex;
    std::pmr::vector<u32> styleTokenDependencyHeadByTokenIndex;
    std::pmr::vector<UIStyleTokenId> resolvedImageTintTokenByNodeIndex;
    std::pmr::vector<u32> imageTintTokenDependencyNextByNodeIndex;
    std::pmr::vector<u32> imageTintTokenDependencyPrevByNodeIndex;
    std::pmr::vector<u32> imageTintTokenDependencyHeadByTokenIndex;
    std::pmr::vector<UIStraightSrgba8Color> resolvedImageTintCacheByNodeIndex;
    std::pmr::vector<u8> resolvedImageTintValidByNodeIndex;
    usize activeNodeStyleClassLinkCount = 0;
    usize nodeStyleClassLinkHighWater = 0;
    usize nodeStyleClassLinkCapacityFailureCount = 0;
    bool styleRegistrationClosed = false;
    std::pmr::vector<UIBoxPaint> boxPaintsByIndex;
    std::pmr::vector<UIButtonPaint> buttonPaintsByNodeIndex;
    // Each bit tracks one property that still inherits product chrome. Theme
    // staging scratch is index-aligned and allocated once during Create().
    std::pmr::vector<u16> themeBindingsByNodeIndex;
    std::pmr::vector<u16> styleOverridesByNodeIndex;
    std::pmr::vector<u8> themeDirtyScratchByNodeIndex;
    std::pmr::vector<UITextMetrics> themeTextMetricsScratchByNodeIndex;
    std::pmr::vector<UIPremultipliedRgba8Color> localSolidFillCacheByIndex;
    std::pmr::vector<UIPremultipliedRgba8Color> localTextColorCacheByIndex;
    std::pmr::vector<WidgetTextState> textStatesByIndex;
    std::pmr::vector<SemanticsState> semanticsStatesByNodeIndex;
    Detail::UIPaintSnapshotBuilder paintSnapshotBuilder;
    Detail::UISemanticsSnapshotBuilder semanticsSnapshotBuilder;
    Detail::UICanvasCommandStorage canvasCommandStorage;
    Detail::UIImageContentStorage imageContentStorage;
    // TextInput selection lives in the behavior side store; TextEdit chrome remains kind-owned.
    std::pmr::vector<UITextEditPaint> textEditPaintsByNodeIndex;
    std::pmr::vector<UITextEditMultilineConfig> textEditMultilineByNodeIndex;
    // Unprefixed visual state is route-visible committed data. CommitLayout
    // builds the parallel candidate state and swaps it only after every
    // fallible snapshot builder succeeds.
    std::pmr::vector<std::pmr::vector<Detail::UITextEditVisualLine>> textEditVisualLinesByNodeIndex;
    std::pmr::vector<Detail::UITextEditVisualLayout> textEditVisualLayoutsByNodeIndex;
    std::pmr::vector<float> textEditScrollYByNodeIndex;
    std::pmr::vector<std::pmr::vector<Detail::UITextEditVisualLine>> candidateTextEditVisualLinesByNodeIndex;
    std::pmr::vector<Detail::UITextEditVisualLayout> candidateTextEditVisualLayoutsByNodeIndex;
    std::pmr::vector<float> candidateTextEditScrollYByNodeIndex;
    std::pmr::vector<std::optional<float>> textEditPreferredXByNodeIndex;
    std::pmr::vector<Detail::UITextEditCaretAffinity> textEditCaretAffinityByNodeIndex;
    std::pmr::vector<ProgressBarState> progressBarStatesByNodeIndex;
    std::pmr::vector<RadioButtonState> radioButtonStatesByNodeIndex;
    // Scroll style/offset/metrics live in the behavior side store; chrome remains kind-owned.
    std::pmr::vector<UIScrollViewPaint> scrollViewPaintsByNodeIndex;
    std::pmr::vector<ScrollViewLayoutScratch> scrollViewLayoutScratchByNodeIndex;
    // Select ownership lives in the behavior side store; popup linkage and chrome remain kind-owned.
    std::pmr::vector<DropdownState> dropdownStatesByNodeIndex;
    std::pmr::vector<PopupState> popupStatesByNodeIndex;
    std::pmr::vector<PopupLayoutScratch> popupLayoutScratchByNodeIndex;
    Detail::UITooltipStateStorage tooltipStorage;
    Detail::UIDialogStateStorage dialogStorage;
    UISplitViewStateStorage splitViewStorage;
    UITabViewStateStorage tabViewStorage;
    UIMenuStateStorage menuStorage;
    std::pmr::vector<UINodeId> menuMutationNodeScratch;
    std::pmr::vector<ListViewState> listViewStatesByNodeIndex;
    std::pmr::vector<ListViewLayoutScratch> listViewLayoutScratchByNodeIndex;
    std::pmr::vector<ListViewItemState> listViewItemStatesByNodeIndex;
    Detail::UIVirtualGridViewStateStorage virtualGridViewStorage;
    std::pmr::vector<UINodeId> virtualGridItemLinkScratch;
    Detail::UIDataGridStateStorage dataGridStorage;
    std::pmr::vector<UINodeId> dataGridColumnLinkScratch;
    std::pmr::vector<UINodeId> dataGridRowLinkScratch;
    std::pmr::vector<UINodeId> dataGridCellLinkScratch;
    std::pmr::vector<float> dataGridColumnWidthScratch;
    std::pmr::vector<TreeViewState> treeViewStatesByNodeIndex;
    std::pmr::vector<TreeViewLayoutScratch> treeViewLayoutScratchByNodeIndex;
    std::pmr::vector<TreeViewItemState> treeViewItemStatesByNodeIndex;
    Detail::UITextStorage textStorage;
    std::unique_ptr<IUITextRasterizer> textRasterizer;
    UIFontFaceId textFace{};
    std::unique_ptr<UIGlyphAtlas> glyphAtlas;
    // Pointer routes reserve the queue entries needed by their post-dispatch
    // state transition before invoking user listeners. Storage owns the fixed
    // queue, node flags, reservation bits, and route candidate scratch.
    Detail::UIDirtyQueueStorage dirtyQueueStorage;
    std::pmr::vector<LayoutScratchState> layoutScratchByIndex;
    std::pmr::vector<u8> layoutWorkByIndex;
    std::pmr::vector<u32> layoutOrderScratch;
    std::pmr::vector<u32> hitEntryIndexByNodeIndex;
    Detail::UIRoutedPointerListenerRegistry routedPointerListenerRegistry;
    std::pmr::vector<u32> routePathScratch;
    // PointerCancel may be synthesized by a mutation performed from inside a
    // routed callback, so it cannot reuse the outer dispatch ancestry scratch.
    std::pmr::vector<u32> pointerCancelRoutePathScratch;
    Detail::UIButtonActionRegistry buttonActionRegistry;
    Detail::UIBehaviorStateStorage behaviorStateStorage;
    Detail::UIDefaultActionPressState defaultActionPressState;
    UIFlowActionPressState flowActionPressState;
    StyleInteractionNodeSet committedStyleInteractionNodes{};
    Detail::UIRangeInputPressLatch rangeInputPressLatch;
    std::pmr::vector<UICheckboxPaint> checkboxPaintsByNodeIndex;
    // Slider-specific visuals remain kind-owned; RangeInput values live in the behavior side store.
    std::pmr::vector<UISliderPaint> sliderPaintsByNodeIndex;
    Detail::UISliderChangeCallbackRegistry sliderChangeCallbackRegistry;
    std::array<std::pmr::vector<UICommittedNodeEntry>, 2> committedBuffers;
    std::array<std::pmr::vector<UICommittedLayoutEntry>, 2> committedLayoutBuffers;
    std::array<std::pmr::vector<UICommittedHitEntry>, 2> committedHitBuffers;
    std::array<std::pmr::vector<UICommittedPaintEntry>, 2> committedPaintBuffers;
    std::array<std::pmr::vector<UISemanticsEntry>, 2> committedSemanticsBuffers;
    // Semantics entries borrow from the published buffer's private text copy;
    // mutating the live widget text arena must not rewrite an older snapshot.
    std::array<std::pmr::vector<char>, 2> committedSemanticsTextBuffers;
    std::vector<UINodeId> deferredRootDestroyBuffer;
    std::vector<Detail::DeferredRoutedPointerListenerRelease> deferredRoutedPointerListenerReleaseBuffer;
    usize publishedBufferIndex = 0;
    usize publishedLayoutBufferIndex = 0;
    usize publishedHitBufferIndex = 0;
    usize publishedPaintBufferIndex = 0;
    u64 committedRevision = 0;
    u64 committedLayoutRevision = 0;
    u64 committedLayoutStructureRevision = 0;
    u64 committedHitRevision = 0;
    u64 committedHitStructureRevision = 0;
    u64 committedHitLayoutRevision = 0;
    u64 committedHitPaintOrderRevision = 0;
    u64 committedPaintRevision = 0;
    u64 committedPaintStructureRevision = 0;
    u64 committedPaintLayoutRevision = 0;
    u64 committedPaintOrderRevision = 0;
    UILogicalSize committedPaintViewportSize{};
    std::optional<UILogicalRect> committedTextInputCaretRect{};
    std::optional<UILogicalRect> candidateTextInputCaretRect{};
    u64 committedSemanticsRevision = 0;
    u64 committedSemanticsStructureRevision = 0;
    u64 committedSemanticsLayoutRevision = 0;
    UILogicalSize committedSemanticsViewportSize{};
    usize publishedSemanticsBufferIndex = 0;
    UILogicalSize committedViewportSize{};
    bool hasCommittedViewport = false;
    usize liveRootCount = 0;
    usize activeBuildTransactionCount = 0;
    std::pmr::vector<UIComponentBuildReservation> componentBuildReservationsByNodeIndex;
    UIComponentBuildReservation* activeComponentBuildReservation = nullptr;
    UIComponentBuildPoolStatistics componentBuildNodeStatistics{};
    usize componentBuildTransactionFailureCount = 0;
    u32 firstRootIndex = InvalidNodeIndex;
    u32 lastRootIndex = InvalidNodeIndex;
    // Single phase-level dirty truth. Node-level dirty flags remain the
    // incremental work queue; these bits only say which published snapshots
    // still need a successful commit.
    static constexpr UIDirty PhaseStructure = UIDirty::Structure;
    static constexpr UIDirty PhaseLayout = UIDirty::Measure;
    static constexpr UIDirty PhaseHit = UIDirty::HitTest;
    static constexpr UIDirty PhasePaint = UIDirty::Paint;
    static constexpr UIDirty PhaseSemantics = UIDirty::Semantics;
    UIDirty phaseDirty = UIDirty::None;
    // A failed candidate may have partially mutated layout scratch. The next
    // layout attempt must rebuild from scratch before reuse is enabled again.
    bool layoutReuseCacheValid = false;
    bool layoutReuseInProgress = false;
    LayoutPassStatistics lastLayoutPass{};
    usize committedHitTargetCount = 0;
    u32 committedActiveModalEntryIndex = InvalidUIHitEntryIndex;
    usize lastHitRebuildCount = 0;
    usize lastPaintCacheRebuildCount = 0;
    usize lastPaintSnapshotRebuildCount = 0;
    usize lastStyleInspectedNodeCount = 0;
    usize lastStyleResolvedNodeCount = 0;
    usize lastStyleCandidateRuleCount = 0;
    usize lastStyleTokenUpdateInspectedNodeCount = 0;
    usize lastStyleTokenUpdateResolvedNodeCount = 0;
    usize lastStyleTokenUpdateAffectedNodeCount = 0;
    usize lastStyleTokenUpdateCandidateRuleCount = 0;
    usize routeDispatchDepth = 0;
    u64 buttonRouteSerial = 0;
    u64 accessibilityActionSequence = 0;
    UINodeId armedPrimaryButton{};
    bool armedPrimaryButtonPressed = false;
    UINodeId hoveredPrimaryControl{};
    // M11-C1: exclusive Primary drag capture for Slider (clears Button arm).
    UINodeId armedSlider{};
    float splitterDragGrabOffset = 0.0F;
    UINodeId armedScrollView{};
    UIScrollAxes armedScrollAxis = UIScrollAxes::None;
    float scrollDragGrabOffset = 0.0F;
    bool scrollThumbDragActive = false;
    UINodeId armedTextEdit{};
    UINodeId capturedPointerNode{};
    UIPointerInputEvent lastPointerInput{};
    bool hasLastPointerInput = false;
    UIInputModality inputModality = UIInputModality::Pointer;
    bool pointerCancelDispatchInProgress = false;
    UINodeId activeModalNode{};
    UINodeId activePopupNode{};
    Detail::UICommandPressLatch<UIDropdownCommand, UIDropdownCommand::ExitNext>
        dropdownCommandPressLatch;
    Detail::UICommandPressLatch<UIListViewCommand, UIListViewCommand::Activate>
        listViewCommandPressLatch;
    Detail::UICommandPressLatch<UIVirtualGridViewCommand,
                                UIVirtualGridViewCommand::Activate>
        virtualGridViewCommandPressLatch;
    Detail::UICommandPressLatch<UIDataGridCommand, UIDataGridCommand::Activate>
        dataGridCommandPressLatch;
    Detail::UICommandPressLatch<UITreeViewCommand, UITreeViewCommand::Activate>
        treeViewCommandPressLatch;
    Detail::UICommandPressLatch<UIFocusNavigationDirection, UIFocusNavigationDirection::Down>
        focusNavigationPressLatch;
    Detail::UICommandPressLatch<UITabViewCommand, UITabViewCommand::Last>
        tabViewCommandPressLatch;
    Detail::UICommandPressLatch<UIMenuCommand, UIMenuCommand::CloseSubmenu>
        menuCommandPressLatch;
    Detail::UICommandPressLatch<UIMenuInvocationCommand,
                                UIMenuInvocationCommand::ShiftF10>
        menuInvocationPressLatch;
    Detail::UICommandPressLatch<UIFocusNavigationDirection, UIFocusNavigationDirection::Down>
        tabViewDirectionPressLatch;
    bool armedTreeDisclosure = false;
    bool transientOverlayDismissPointerBarrierActive = false;
    bool secondaryMenuInvocationPressLatched = false;
    UINodeId pendingDestroyedModalRestoreFocus{};
    bool hasPendingDestroyedModalRestoreFocus = false;
    // Last Button that received Primary Pointer arm. Keyboard/Gamepad Accept
    // activates this node without requiring a live pointer press.
    UINodeId defaultActionFocusButton{};
    // Focused single-line editor that receives keyboard and IME input.
    UINodeId textInputFocus{};
    Detail::UIImeCompositionState imeComposition;

    Impl(Platform::WindowId owner, UIContextCapacityConfig capacities, std::thread::id threadId,
         std::shared_ptr<Detail::UIContextLifetimeControl> lifetimeControl,
         std::unique_ptr<UIAllocationLedger> ownedAllocationLedger, NodePool&& nodePool);


    [[nodiscard]] std::pmr::memory_resource& allocationMemoryResource() noexcept;


    [[nodiscard]] static Core::Result<std::unique_ptr<Impl>>
    Create(Platform::WindowId ownerWindow, NormalizedUIContextCapacityConfig normalized,
           std::shared_ptr<Detail::UIContextLifetimeControl> lifetimeControl, std::pmr::memory_resource& resource);


    void detachLifetime(UIContext* context) noexcept;


    void buildCommittedStructure(std::pmr::vector<UICommittedNodeEntry>& output) const noexcept;


    void appendLayoutOrderTree(u32 index, std::pmr::vector<u32>& output) const noexcept;


    void buildLayoutOrder(std::pmr::vector<u32>& output) const noexcept;


    void markLayoutSubtreeWork(u32 rootIndex, u8 work) noexcept;


    void ensureLayoutSubtreeWork(u32 rootIndex, u8 work) noexcept;


    void markLayoutAncestorsWork(u32 nodeIndex, u8 work) noexcept;


    void initializeLayoutWork(const std::pmr::vector<u32>& order, bool allowReuse) noexcept;


    [[nodiscard]] bool isActiveFlowScreenIndex(u32 index) const noexcept;


    void prepareLayoutState(UILogicalSize viewportSize, const std::pmr::vector<u32>& order, bool allowReuse) noexcept;

    [[nodiscard]] const UILayoutStyle& resolvedLayoutStyle(u32 nodeIndex) const noexcept;

    [[nodiscard]] Core::Result<bool> refreshResolvedLayoutAfterArrange(
        const std::pmr::vector<u32>& order);


    void measureLayout(UILogicalSize viewportSize, const std::pmr::vector<u32>& order,
                       LayoutPassStatistics& statistics) noexcept;


    void assignLayoutRect(u32 index, UILogicalRect worldRect, UILogicalRect parentWorldRect,
                          UILogicalRect descendantClip) noexcept;


    void refreshMeasuredSizeForParentContent(u32 childIndex, UILogicalRect parentContentRect,
                                             LayoutPassStatistics& statistics) noexcept;


    void arrangeOverlayChild(u32 childIndex, UILogicalRect parentContentRect, UILogicalRect parentWorldRect,
                             UILogicalRect descendantClip, LayoutPassStatistics& statistics) noexcept;


    void arrangePopupChild(u32 popupIndex, UILogicalRect anchorRect, UILogicalRect viewportRect,
                           LayoutPassStatistics& statistics) noexcept;


    [[nodiscard]] const UICommittedLayoutEntry*
    committedLayoutEntryFor(UINodeId node) const noexcept;


    void arrangeTooltipChild(u32 tooltipIndex, UILogicalRect parentWorldRect,
                             UILogicalRect viewportRect,
                             LayoutPassStatistics& statistics) noexcept;


    void arrangeMenuChild(u32 menuIndex, UILogicalRect parentWorldRect,
                          UILogicalRect viewportRect,
                          LayoutPassStatistics& statistics) noexcept;


    [[nodiscard]] Core::Status bindListViewItem(u32 itemIndex, u64 logicalIndex,
                                                const UIListViewItemDescriptor& descriptor);


    void collapseListViewItems(u32 listViewIndex, UILogicalRect contentRect, UILogicalRect parentWorldRect,
                               UILogicalRect descendantClip) noexcept;


    [[nodiscard]] Core::Status arrangeListViewItems(u32 listViewIndex, UILogicalRect unscrolledContentRect,
                                                    UILogicalRect parentWorldRect, UILogicalRect descendantClip);


    [[nodiscard]] Core::Status bindVirtualGridViewItem(
        UINodeId item, u64 logicalIndex,
        const UIVirtualGridViewItemDescriptor& descriptor);


    void collapseVirtualGridViewItems(
        UINodeId virtualGridView, UILogicalRect contentRect,
        UILogicalRect parentWorldRect, UILogicalRect descendantClip) noexcept;


    [[nodiscard]] Core::Status arrangeVirtualGridViewItems(
        UINodeId virtualGridView, UILogicalRect unscrolledContentRect,
        UILogicalRect parentWorldRect, UILogicalRect descendantClip);


    [[nodiscard]] Core::Status bindDataGridText(
        UINodeId node, std::string_view value);


    void collapseDataGrid(
        UINodeId dataGrid, UILogicalRect contentRect,
        UILogicalRect parentWorldRect, UILogicalRect descendantClip) noexcept;


    [[nodiscard]] Core::Status arrangeDataGrid(
        UINodeId dataGrid, UILogicalRect unscrolledContentRect,
        UILogicalRect parentWorldRect, UILogicalRect descendantClip);


    [[nodiscard]] Core::Status bindTreeViewItem(u32 itemIndex, u64 logicalIndex,
                                                const UITreeViewItemDescriptor& descriptor);


    void collapseTreeViewItems(u32 treeViewIndex, UILogicalRect contentRect, UILogicalRect parentWorldRect,
                               UILogicalRect descendantClip) noexcept;


    [[nodiscard]] Core::Status arrangeTreeViewItems(u32 treeViewIndex, UILogicalRect unscrolledContentRect,
                                                    UILogicalRect parentWorldRect, UILogicalRect descendantClip);


    [[nodiscard]] Core::Status arrangeChildren(u32 parentIndex, UILogicalRect viewportRect,
                                               LayoutPassStatistics& statistics);


    [[nodiscard]] Core::Status arrangeLayout(UILogicalSize viewportSize, const std::pmr::vector<u32>& order,
                                             LayoutPassStatistics& statistics);


    [[nodiscard]] UICommittedContentPlacement contentPlacementFor(u32 index) const noexcept;


    [[nodiscard]] bool hasVirtualGridPreview(u32 index) const noexcept;


    [[nodiscard]] UICommittedContentPlacement virtualGridImagePlacement(
        const UICommittedLayoutEntry& layoutEntry) const noexcept;


    [[nodiscard]] UICommittedContentPlacement virtualGridTextPlacement(
        const UICommittedLayoutEntry& layoutEntry) const noexcept;


    void buildCommittedLayout(std::pmr::vector<UICommittedLayoutEntry>& output,
                              const std::pmr::vector<u32>& order) const noexcept;


    [[nodiscard]] Core::Result<CommittedHitBuildResult>
    buildCommittedHit(std::pmr::vector<UICommittedHitEntry>& output,
                      std::span<const UICommittedLayoutEntry> layoutEntries);


    [[nodiscard]] UIPremultipliedRgba8Color widgetPaintColor(UINodeId node,
                                                             UIPremultipliedRgba8Color color) const noexcept;


    [[nodiscard]] bool isFocusVisible(UINodeId node) const noexcept;


    [[nodiscard]] UISplitViewConfig resolvedSplitViewConfig(
        const UISplitViewConfig& authored) const noexcept;


    [[nodiscard]] UIPremultipliedRgba8Color resolveBuiltinBoxFillColor(
        UINodeId node, u32 nodeIndex,
        UIPremultipliedRgba8Color normalColor) const noexcept;


    [[nodiscard]] static u16 boxFillOverrideMask(const NodeRecord& record) noexcept;


    [[nodiscard]] bool hasLocalBoxFillOverride(u32 nodeIndex,
                                                const NodeRecord& record) const noexcept;


    [[nodiscard]] UIStyleState deriveStyleState(UINodeId node,
                                                 u32 nodeIndex) const noexcept;


    [[nodiscard]] StyleInteractionNodeSet currentStyleInteractionNodes() const noexcept;


    [[nodiscard]] Detail::UIStyleBoxFillResolution resolveStyleBoxFill(
        u32 nodeIndex, UIStyleState states) const noexcept;


    [[nodiscard]] std::span<const UIStyleClassId> styleClassesFor(u32 nodeIndex) const noexcept;


    [[nodiscard]] bool styleBackgroundTransitionEnabled() const noexcept;


    [[nodiscard]] bool needsStyleBackgroundMotionReservation(UIStyleRoleId role,
                                                             std::span<const UIStyleClassId> classes) const noexcept;


    void unlinkTokenDependencyList(u32 nodeIndex, std::pmr::vector<UIStyleTokenId>& tokenByNode,
                                   std::pmr::vector<u32>& nextByNode, std::pmr::vector<u32>& prevByNode,
                                   std::pmr::vector<u32>& headByToken) noexcept;


    void linkTokenDependencyList(u32 nodeIndex, UIStyleTokenId token,
                                 std::pmr::vector<UIStyleTokenId>& tokenByNode,
                                 std::pmr::vector<u32>& nextByNode, std::pmr::vector<u32>& prevByNode,
                                 std::pmr::vector<u32>& headByToken) noexcept;


    void unlinkStyleTokenDependency(u32 nodeIndex) noexcept;


    void linkStyleTokenDependency(u32 nodeIndex, UIStyleTokenId token) noexcept;


    void setResolvedStyleColorTokenDependency(u32 nodeIndex, UIStyleTokenId token) noexcept;


    void unlinkImageTintTokenDependency(u32 nodeIndex) noexcept;


    void linkImageTintTokenDependency(u32 nodeIndex, UIStyleTokenId token) noexcept;


    void setResolvedImageTintTokenDependency(u32 nodeIndex, UIStyleTokenId token) noexcept;


    [[nodiscard]] bool hasLocalImageTintOverride(u32 nodeIndex) const noexcept;


    [[nodiscard]] static bool isStatefulControlImageTintRole(
        UIStyleRoleId role) noexcept;


    [[nodiscard]] UIStraightSrgba8Color resolveBuiltinControlImageTint(
        UINodeId node, u32 nodeIndex, UIStraightSrgba8Color authoredTint) const noexcept;


    [[nodiscard]] UIStraightSrgba8Color resolvedImageTintColor(u32 nodeIndex,
                                                               const UIImageContent& image) const noexcept;


    [[nodiscard]] usize refreshResolvedStyleCache(u32 nodeIndex,
                                                   UIStyleState states) noexcept;


    [[nodiscard]] usize refreshResolvedStyleCache(u32 nodeIndex) noexcept;


    [[nodiscard]] UIBoxPaint resolvedBoxChrome(UINodeId node, u32 nodeIndex) const noexcept;


    [[nodiscard]] UIPremultipliedRgba8Color resolvedRadioIndicatorColor(UINodeId node, u32 nodeIndex) const noexcept;


    [[nodiscard]] UIPremultipliedRgba8Color resolvedDropdownSelectionColor(UINodeId item) const noexcept;


    [[nodiscard]] UIPremultipliedRgba8Color resolvedCollectionSelectionColor(
        UINodeId item, UINodeId collection, UIStraightSrgba8Color normalColor,
        UIStraightSrgba8Color hoveredColor, UIStraightSrgba8Color focusedColor,
        UIStraightSrgba8Color pressedColor) const noexcept;


    [[nodiscard]] UIPremultipliedRgba8Color resolvedListViewSelectionColor(UINodeId item) const noexcept;


    [[nodiscard]] UIPremultipliedRgba8Color
    resolvedVirtualGridViewSelectionColor(UINodeId item) const noexcept;


    [[nodiscard]] UIPremultipliedRgba8Color
    resolvedDataGridRowSelectionColor(UINodeId cell) const noexcept;


    [[nodiscard]] UIPremultipliedRgba8Color resolvedTreeViewSelectionColor(UINodeId item) const noexcept;


    [[nodiscard]] UIPremultipliedRgba8Color resolvedTreeViewDisclosureColor(UINodeId item) const noexcept;


    [[nodiscard]] usize countCanvasPaintEntries(const UICommittedLayoutEntry& layoutEntry) const noexcept;


    void appendCanvasPaints(std::pmr::vector<UICommittedPaintEntry>& output,
                            const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal) noexcept;


    [[nodiscard]] Core::Result<Detail::UIControlPaintBatch>
    resolveControlPaintBatch(const UICommittedLayoutEntry& layoutEntry, bool applyDisabledOpacity) const;


    [[nodiscard]] Detail::UITextEditPaintState resolveTextEditPaintState(
        UINodeId node, bool applyDisabledOpacity, bool useCandidateVisualState) const noexcept;


    // Both the counting and the appending paint pass must observe the same
    // available width, so the content box is injected here instead of at each
    // call site. ADR 0022: truncation reads the one committed content
    // placement and never re-derives geometry from worldRect + padding.
    [[nodiscard]] Detail::UITextEditPaintState resolveTextEditPaintStateFor(
        const UICommittedLayoutEntry& layoutEntry, bool applyDisabledOpacity,
        bool useCandidateVisualState) const noexcept;


    [[nodiscard]] Core::Status buildTextEditVisualState(
        std::span<const UICommittedLayoutEntry> layoutEntries) noexcept;

    [[nodiscard]] Core::Result<usize> countPaintEntries(
        const UICommittedLayoutEntry& layoutEntry, bool useCandidateTextEditVisualState) const;


    void refreshLocalPaintCache(u32 nodeIndex) noexcept;


    struct PaintCacheRebuildStatistics final {
        usize paintCacheRebuildCount = 0;
        usize styleInspectedNodeCount = 0;
        usize styleResolvedNodeCount = 0;
        usize styleCandidateRuleCount = 0;
    };

    [[nodiscard]] PaintCacheRebuildStatistics
    rebuildDirtyPaintCaches(
        std::span<const UICommittedLayoutEntry> layoutEntries,
        StyleInteractionNodeSet& interactionCandidates) noexcept;


    void appendTextGlyphPaints(std::pmr::vector<UICommittedPaintEntry>& output,
                               const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal,
                               bool useCandidateTextEditVisualState) noexcept;


    [[nodiscard]] static UILogicalRect resolveImageDestination(
        const UICommittedContentPlacement& placement, const UIImageContent& image) noexcept;


    void appendImagePaint(std::pmr::vector<UICommittedPaintEntry>& output,
                          const UICommittedLayoutEntry& layoutEntry,
                          u32& nextPaintOrdinal) const noexcept;


    [[nodiscard]] Core::Status appendPaintEntries(std::pmr::vector<UICommittedPaintEntry>& output,
                                                  const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal,
                                                  bool useCandidateTextEditVisualState);


    struct PaintSnapshotSourceContext final {
        Impl* impl = nullptr;
        bool useCandidateTextEditVisualState = false;
    };

    [[nodiscard]] static constexpr Detail::UIPaintSnapshotSourceAdapter paintSnapshotSourceAdapter() noexcept;


    [[nodiscard]] Core::Result<usize>
    validatePaintCandidateCapacity(std::span<const UICommittedLayoutEntry> layoutEntries,
                                   bool useCandidateTextEditVisualState);


    [[nodiscard]] Core::Status buildCommittedPaint(std::pmr::vector<UICommittedPaintEntry>& output,
                                                   std::span<const UICommittedLayoutEntry> layoutEntries,
                                                   bool useCandidateTextEditVisualState);


    [[nodiscard]] bool resolveSemanticsSnapshotSource(
        UINodeId node, Detail::UISemanticsSnapshotSource& source,
        bool useCandidateVirtualGridPresentation) noexcept;


    [[nodiscard]] Core::Status buildCommittedSemantics(std::pmr::vector<UISemanticsEntry>& output,
                                                       std::pmr::vector<char>& textOutput,
                                                       std::span<const UICommittedLayoutEntry> layoutEntries,
                                                       bool useCandidateVirtualGridPresentation);


    [[nodiscard]] Core::Status validateLayoutCandidate(const std::pmr::vector<u32>& order) const;


    [[nodiscard]] bool isPhaseDirty(UIDirty flags) const noexcept;


    [[nodiscard]] Core::Status publishStructureIfDirty();


    [[nodiscard]] bool isOwnerThread() const noexcept;


    [[nodiscard]] Core::Status ensureOwnerThread() const;


    void publishRoutedPointerListenerTokenState(u32 slot, u32 generation, bool active) noexcept;


    static void publishRoutedPointerListenerTokenStateFromRegistry(void* context, u32 slot,
                                                                   u32 generation, bool active) noexcept;


    [[nodiscard]] Detail::UIRoutedPointerListenerStatePublisher
    routedPointerListenerStatePublisher() noexcept;


    void reclaimInactiveRoutedPointerListeners() noexcept;


    void deactivateRoutedPointerListener(u32 listenerIndex, u32 generation,
                                         bool publishTokenState) noexcept;


    void deactivateAllRoutedPointerListenersForNode(u32 nodeIndex) noexcept;


    void drainDeferredRoutedPointerListenerReleases() noexcept;


    void drainDeferredRootDestroys() noexcept;


    [[nodiscard]] UINodeId idForIndex(u32 index) const noexcept;


    [[nodiscard]] NodeRecord* recordByIndex(u32 index) noexcept;


    [[nodiscard]] const NodeRecord* recordByIndex(u32 index) const noexcept;


    [[nodiscard]] Core::Result<NodeRecord*> resolveNode(UINodeId node);


    [[nodiscard]] Core::Result<NodeRecord*> resolveParent(UINodeId parent);


    [[nodiscard]] bool contains(UINodeId node) const noexcept;


    [[nodiscard]] bool isNodeEnabled(UINodeId node) const noexcept;


    [[nodiscard]] bool isCandidateNodeEnabled(UINodeId node) const noexcept;


    [[nodiscard]] Core::Result<NodeRecord*> resolveButton(UINodeId button);


    [[nodiscard]] bool isButtonPressed(UINodeId node) const noexcept;


    void clearArmedPrimaryButton() noexcept;


    void clearHoveredPrimaryControl() noexcept;


    [[nodiscard]] UINodeId resolvedHoveredPrimaryControl(UINodeId candidate) const noexcept;


    [[nodiscard]] Core::Status updateHoveredPrimaryControl(UINodeId candidate);


    void clearArmedSlider() noexcept;


    void clearArmedScrollView() noexcept;


    void clearArmedTextEdit() noexcept;


    void clearDefaultActionFocus() noexcept;


    void resetImeCompositionState() noexcept;


    [[nodiscard]] Core::Status clearImeComposition();


    void resetTextEditPreferredX(UINodeId node) noexcept;


    void clearImeFocus() noexcept;


    [[nodiscard]] bool isLiveTextEdit(UINodeId node) const noexcept;


    [[nodiscard]] bool isPointerInteractionCandidate(UINodeId node, std::span<const UICommittedHitEntry> entries,
                                                     u32 activeModalEntryIndex) const noexcept;


    [[nodiscard]] bool isPointerCaptureCandidate(UINodeId node, std::span<const UICommittedHitEntry> entries,
                                                 u32 activeModalEntryIndex) const noexcept;


    [[nodiscard]] bool isKeyboardFocusCandidate(UINodeId node, std::span<const UICommittedHitEntry> entries,
                                                u32 activeModalEntryIndex) const noexcept;


    [[nodiscard]] bool isCommittedKeyboardFocusCandidate(UINodeId node) const noexcept;


    [[nodiscard]] bool isCommittedTextEditFocusCandidate(UINodeId node) const noexcept;


    void deactivateButtonActionForNode(u32 nodeIndex) noexcept;


    [[nodiscard]] Detail::UIButtonActionInvocation
    captureButtonAction(UINodeId button, u64 registrationSerialBoundary) const noexcept;


    void invokeButtonAction(Detail::UIButtonActionInvocation candidate, const UIButtonActionEvent& event,
                            u64 routeSerial) noexcept;


    [[nodiscard]] Detail::UISliderChangeCallbackInvocation
    captureSliderChangeCallback(UINodeId slider) const noexcept;


    void invokeSliderChangeCallback(Detail::UISliderChangeCallbackInvocation candidate,
                                    const UISliderChangeEvent& event) noexcept;


    void clearTextState(u32 index) noexcept;


    void clearSemanticsState(u32 index) noexcept;


    [[nodiscard]] std::string_view semanticsNameViewFor(u32 index) const noexcept;


    [[nodiscard]] std::string_view semanticsDescriptionViewFor(u32 index) const noexcept;


    [[nodiscard]] std::string_view semanticsNameSourceFor(u32 index) const noexcept;


    [[nodiscard]] std::string_view textViewFor(u32 index) const noexcept;


    [[nodiscard]] UINodeId dropdownForPopup(UINodeId popup) const noexcept;


    [[nodiscard]] UINodeId popupForDropdown(UINodeId dropdown) const noexcept;


    [[nodiscard]] UINodeId dropdownForItem(UINodeId item) const noexcept;


    [[nodiscard]] bool isSelectedDropdownItem(UINodeId item) const noexcept;


    [[nodiscard]] std::string_view presentationTextViewFor(u32 index) const noexcept;


    [[nodiscard]] const UITextMetrics* presentationTextMetricsFor(u32 index) const noexcept;


    [[nodiscard]] Core::Result<UITextMetrics> measureWidgetText(std::string_view utf8, const UITextStyle& style);

    [[nodiscard]] Core::Result<UITextMetrics> measureWrappedWidgetText(
        u32 index, float maximumWidth,
        Detail::UITextIntrinsicWidths* intrinsicWidths = nullptr);


    void releaseFlowNode(u32 index) noexcept;


    void resetNodeSideData(u32 index) noexcept;


    void markStructureChanged() noexcept;


    [[nodiscard]] Detail::UIDirtyQueueEntryDisposition classifyDirtyQueueEntry(UINodeId queued) const noexcept;


    [[nodiscard]] Detail::UIDirtyQueueEntryClassifier dirtyQueueEntryClassifier() const noexcept;


    void compactDirtyQueue() noexcept;


    [[nodiscard]] usize validDirtyQueueCount() const noexcept;


    [[nodiscard]] usize occupiedDirtyQueueSlotCount() const noexcept;


    void addRouteDirtyReservationCandidate(UINodeId node);


    void addRouteLayoutDirtyReservationCandidates(UINodeId node);


    [[nodiscard]] Core::Status reserveRouteDirtyQueueSlots();


    void releaseRouteDirtyQueueReservations() noexcept;


    [[nodiscard]] Core::Status markLayoutAndPaintDirtyBatch(
        std::span<const UINodeId> requestedLayoutNodes,
        std::span<const UINodeId> requestedPaintNodes);


    [[nodiscard]] Core::Status markLayoutDirtyBatch(std::span<const UINodeId> requestedNodes);


    [[nodiscard]] Core::Status markLayoutDirtyBatch(
        std::initializer_list<UINodeId> requestedNodes);


    [[nodiscard]] Core::Status markHitTestDirty(UINodeId node);


    [[nodiscard]] Core::Status preflightPaintDirtyBatch(std::initializer_list<UINodeId> requestedNodes) const;


    [[nodiscard]] Core::Status markPaintDirtyBatch(std::initializer_list<UINodeId> requestedNodes);


    [[nodiscard]] Core::Status markPaintDirty(UINodeId node);


    [[nodiscard]] Core::Status setInputModality(UIInputModality modality);


    // Dispatches through UIStylePropertyKind static dirty metadata (UI-STYLE-001).
    // Keeps capacity/atomic dirty-queue helpers as the only mutation path.
    [[nodiscard]] Core::Status markStylePropertyDirty(UINodeId node, UIStylePropertyKind kind);


    void clearDirtyState() noexcept;


    [[nodiscard]] bool isNodeWithinRoot(UINodeId root, UINodeId node) const noexcept;


    [[nodiscard]] bool isNodeWithinSubtree(UINodeId subtreeRoot, UINodeId node) const noexcept;


    [[nodiscard]] ProductChromeStorage productChromeStorageFor(u32 index) noexcept;


    void applyProductChromeTransition(u32 index, UIStyleRoleId role, const UITheme& theme,
                                      u16 affectedBindings, u16 targetBindings) noexcept;


    void applyDefaultProductChrome(u32 index, UIStyleRoleId role) noexcept;


    void stageThemePaintChange(u32 index) noexcept;


    void detachThemeBinding(u32 index, u16 binding) noexcept;


    [[nodiscard]] Core::Status stageThemeTextStyle(u32 index, const UITextStyle& nextStyle);


    [[nodiscard]] Core::Status stageProductChromeTransition(u32 index, UIStyleRoleId role,
                                                            const UITheme& theme, u16 affectedBindings,
                                                            u16 targetBindings);


    void applyStagedProductChromeTransition(u32 index, UIStyleRoleId role, const UITheme& theme,
                                            u16 affectedBindings, u16 targetBindings) noexcept;


    void propagateThemeLayoutDirtyToAncestors() noexcept;


    [[nodiscard]] Core::Status preflightThemeDirtyQueue();


    void publishThemeDirtyState() noexcept;


    [[nodiscard]] Core::Status setProductTheme(const UITheme& theme);


    [[nodiscard]] Core::Result<UIStyleClassId> registerStyleClass();


    [[nodiscard]] Core::Result<UIStyleTokenId>
    registerStyleColorToken(UIStraightSrgba8Color value);


    [[nodiscard]] Core::Result<UIStraightSrgba8Color>
    styleColorToken(UIStyleTokenId token) const;


    struct StyleTokenUpdateStatistics final {
        usize inspectedNodeCount = 0;
        usize resolvedNodeCount = 0;
        usize affectedNodeCount = 0;
        usize candidateRuleCount = 0;
    };

    [[nodiscard]] u32 tokenDependencyHead(UIStyleTokenId token,
                                          const std::pmr::vector<u32>& headByToken) const noexcept;


    [[nodiscard]] Core::Status preflightStyleColorTokenDirtyQueue(
        UIStyleTokenId token, StyleTokenUpdateStatistics& statistics);


    void publishStyleColorTokenDirtyState(UIStyleTokenId token) noexcept;


    [[nodiscard]] Core::Status setStyleColorToken(
        UIStyleTokenId token, UIStraightSrgba8Color value);


    [[nodiscard]] Core::MonotonicTimePoint motionNow() const noexcept;


    [[nodiscard]] Core::Status setMotionClock(const Core::IMonotonicClock* clock);


    [[nodiscard]] Core::Status setReducedMotion(bool enabled);


    [[nodiscard]] bool reducedMotion() const noexcept;


    [[nodiscard]] Core::Status setStyleBackgroundColorTransition(const UITransitionSpec& spec);


    [[nodiscard]] UITransitionSpec styleBackgroundColorTransition() const noexcept;


    void commitMotionProperty(const Detail::UIMotionTrackStorage::Completed& completed) noexcept;


    void applyCompletedMotion(const Detail::UIMotionTrackStorage::Completed& completed) noexcept;


    void applyTimelineTarget(const Detail::UIKeyframeTimelineStorage::Target& target) noexcept;


    [[nodiscard]] UIStraightSrgba8Color unpremultiplyColor(UIPremultipliedRgba8Color premul) const noexcept;


    [[nodiscard]] Detail::UIMotionTrackStorage::NodePresentation
    motionPresentationFor(UINodeId node) const noexcept;


    [[nodiscard]] UILayoutStyle presentationLayoutStyle(u32 nodeIndex) const noexcept;


    [[nodiscard]] UIStraightSrgba8Color currentBackgroundColor(UINodeId node, u32 nodeIndex) const noexcept;


    [[nodiscard]] UIStraightSrgba8Color currentBorderColor(UINodeId node, u32 nodeIndex) const noexcept;


    [[nodiscard]] UIStraightSrgba8Color currentTextColor(UINodeId node, u32 nodeIndex) const noexcept;


    [[nodiscard]] float currentOpacity(UINodeId node, u32 nodeIndex) const noexcept;


    [[nodiscard]] float currentCornerRadius(UINodeId node, u32 nodeIndex) const noexcept;


    [[nodiscard]] Detail::UIMotionTrackStorage::Scalar2 currentVisualOffset(UINodeId node,
                                                                            u32 nodeIndex) const noexcept;


    [[nodiscard]] Core::Status beginColorPropertyTransition(
        UINodeId node, UIAnimatableProperty property, UIStraightSrgba8Color target,
        const UITransitionSpec& spec);


    [[nodiscard]] Core::Status beginBackgroundColorTransition(
        UINodeId node, UIStraightSrgba8Color target, const UITransitionSpec& spec);


    [[nodiscard]] Core::Status beginBorderColorTransition(
        UINodeId node, UIStraightSrgba8Color target, const UITransitionSpec& spec);


    [[nodiscard]] Core::Status beginTextColorTransition(
        UINodeId node, UIStraightSrgba8Color target, const UITransitionSpec& spec);


    [[nodiscard]] Core::Status beginOpacityTransition(
        UINodeId node, float targetOpacity, const UITransitionSpec& spec);


    [[nodiscard]] Core::Status beginCornerRadiusTransition(
        UINodeId node, float targetRadius, const UITransitionSpec& spec);


    [[nodiscard]] Core::Status beginVisualOffsetTransition(
        UINodeId node, float targetOffsetX, float targetOffsetY, const UITransitionSpec& spec);


    struct TimelineValidationContext final {
        Impl* impl = nullptr;
        UITimelineId timeline{};
    };

    struct TimelineDirtyNodeCollectionContext final {
        std::pmr::vector<UINodeId>* layoutNodes = nullptr;
        std::pmr::vector<UINodeId>* paintNodes = nullptr;
    };

    [[nodiscard]] static Core::Status collectTimelineDirtyNodeVisitor(
        void* rawContext, const Detail::UIKeyframeTimelineStorage::TrackView& track);


    [[nodiscard]] static Core::Status validateTimelineTrackVisitor(
        void* rawContext, const Detail::UIKeyframeTimelineStorage::TrackView& track);


    [[nodiscard]] Core::Status validateTimelineNodes(UITimelineId timeline);


    [[nodiscard]] Core::Status collectTimelineDirtyNodes(UITimelineId timeline);


    [[nodiscard]] Core::Status collectActiveTimelineDirtyNodes();


    [[nodiscard]] Core::Status markCollectedTimelineDirtyNodes();


    [[nodiscard]] Core::Status validateTimelineDefinitionPropertyCapability(
        UINodeId node, UIAnimatableProperty property) const;


    [[nodiscard]] Core::Status validateTimelinePlaybackPropertyCapability(
        UINodeId node, UIAnimatableProperty property) const;


    [[nodiscard]] Core::Result<UITimelineId> createTimeline(const UITimelineDesc& desc);


    [[nodiscard]] Core::Status replaceTimeline(
        UITimelineId timeline, const UITimelineDesc& desc);


    [[nodiscard]] Core::Status playTimeline(UITimelineId timeline);


    [[nodiscard]] Core::Status cancelTimeline(UITimelineId timeline);


    [[nodiscard]] Core::Status destroyTimeline(UITimelineId timeline);


    [[nodiscard]] Core::Result<bool> isTimelineActive(UITimelineId timeline) const;


    [[nodiscard]] Core::Status sampleMotion(Core::MonotonicTimePoint now);


    [[nodiscard]] UIBoxPaint presentationBoxPaint(UINodeId node, u32 nodeIndex) const noexcept;


    [[nodiscard]] UILogicalRect presentationPaintWorldRect(UINodeId node, u32 nodeIndex,
                                                           UILogicalRect worldRect) const noexcept;


    [[nodiscard]] UIPremultipliedRgba8Color presentationBoxFill(UINodeId node,
                                                                u32 nodeIndex) const noexcept;


    [[nodiscard]] Core::Status installStyleSheet(
        std::span<const UIStyleBoxFillRule> rules);


    [[nodiscard]] Core::Status reserveComponentBuildStorage(
        UIComponentBuildBudget budget, UIComponentBuildReservation& reservation);


    void releaseComponentBuildStorage(UIComponentBuildReservation& reservation) noexcept;


    [[nodiscard]] UIComponentBuildReservation*
    findComponentBuildReservation(UINodeId componentRoot) noexcept;


    [[nodiscard]] const UIComponentBuildReservation*
    findComponentBuildReservation(UINodeId componentRoot) const noexcept;


    [[nodiscard]] bool isBuildTransactionActive(UINodeId componentRoot) const noexcept;


    [[nodiscard]] Core::Result<TextByteAllocation> allocateRetainedText(u32 byteCount);


    [[nodiscard]] Core::Status assignRetainedCanvas(
        u32 nodeIndex, std::span<const UICanvasCommand> commands);


    [[nodiscard]] Core::Result<UINodeId> createNode(
        BuiltinElementKind kind, UIElementBehavior behaviors,
        std::optional<UIStyleRoleId> authoredStyleRole = std::nullopt,
        std::span<const UIStyleClassId> authoredStyleClasses = {});


    [[nodiscard]] usize availableNodeCountForCurrentCreation() const noexcept;


    [[nodiscard]] Core::Status initializeSemantics(
        u32 index, BuiltinElementKind kind, const UISemanticsDescriptor& descriptor,
        UIElementBehavior behaviors);


    [[nodiscard]] Core::Status initializeElement(UINodeId node, BuiltinElementKind kind,
                                                 const UIElementDescriptor& descriptor,
                                                 const UILayoutStyle& normalizedLayout);


    [[nodiscard]] Core::Result<UINodeId> createElement(UINodeId parent,
                                                       const UIElementDescriptor& descriptor);


    [[nodiscard]] Core::Result<UIRootOwner> createRoot(UIContext& context);


    [[nodiscard]] Core::Result<UINodeId>
    createChild(UINodeId parent, BuiltinElementKind kind,
                std::optional<UIElementBehavior> authoredBehaviors = std::nullopt,
                std::optional<UIStyleRoleId> authoredStyleRole = std::nullopt,
                std::span<const UIStyleClassId> authoredStyleClasses = {});


    [[nodiscard]] Core::Result<UINodeId>
    createListViewComposite(UINodeId parent, UIListViewCreateConfig config,
                            UIStyleRoleId authoredStyleRole,
                            std::span<const UIStyleClassId> authoredStyleClasses);


    [[nodiscard]] Core::Result<UINodeId>
    createVirtualGridViewComposite(
        UINodeId parent, UIVirtualGridViewCreateConfig config,
        UIStyleRoleId authoredStyleRole,
        std::span<const UIStyleClassId> authoredStyleClasses);


    [[nodiscard]] Core::Result<UINodeId>
    createDataGridComposite(
        UINodeId parent, UIDataGridCreateConfig config,
        UIStyleRoleId authoredStyleRole,
        std::span<const UIStyleClassId> authoredStyleClasses);


    [[nodiscard]] Core::Result<UINodeId>
    createTreeViewComposite(UINodeId parent, UITreeViewCreateConfig config,
                            UIStyleRoleId authoredStyleRole,
                            std::span<const UIStyleClassId> authoredStyleClasses);


    [[nodiscard]] Core::Result<UINodeId> createElementFromUpdater(UINodeId updaterRoot, UINodeId parent,
                                                                  const UIElementDescriptor& descriptor);


    [[nodiscard]] Core::Status validateFlowUpdaterRoot(UINodeId updaterRoot) const;


    [[nodiscard]] Core::Status markFlowVisibilityDirty(std::initializer_list<UINodeId> screens);


    [[nodiscard]] Core::Result<UIFlowLayerId> registerFlowLayerFromUpdater(UINodeId updaterRoot,
                                                                           UINodeId layer);


    [[nodiscard]] Core::Result<UIFlowScreenId>
    registerFlowScreenFromUpdater(UINodeId updaterRoot, UIFlowLayerId layerId, UINodeId screen);


    [[nodiscard]] Core::Status pushFlowScreenFromUpdater(UINodeId updaterRoot, UIFlowScreenId screenId);


    [[nodiscard]] Core::Result<UIFlowScreenId> popFlowScreenFromUpdater(UINodeId updaterRoot,
                                                                        UIFlowLayerId layerId);


    [[nodiscard]] Core::Result<UIFlowScreenId> replaceFlowScreenFromUpdater(UINodeId updaterRoot,
                                                                            UIFlowScreenId replacementId);


    [[nodiscard]] Core::Result<UIFlowScreenId> activeFlowScreenFromUpdater(UINodeId updaterRoot,
                                                                           UIFlowLayerId layerId) const;


    [[nodiscard]] Core::Result<bool> isFlowScreenActiveFromUpdater(UINodeId updaterRoot,
                                                                   UIFlowScreenId screenId) const;


    [[nodiscard]] Core::Status
    setFlowScreenActionFromUpdater(UINodeId updaterRoot, UIFlowScreenId screenId,
                                   UIFlowAction action, UIFlowActionCallback&& callback);


    [[nodiscard]] Core::Status
    clearFlowScreenActionFromUpdater(UINodeId updaterRoot, UIFlowScreenId screenId,
                                     UIFlowAction action);


    [[nodiscard]] Core::Result<UIComponentBuildBudget>
    requiredBuildBudgetForElement(const UIElementDescriptor& descriptor) const;


    [[nodiscard]] Core::Result<UIElementBuildTransaction>
    beginBuildTransaction(UIContext& context, UINodeId parent,
                          const UIElementDescriptor& rootDescriptor,
                          UIComponentBuildBudget budget);


    [[nodiscard]] Core::Result<UIElementBuildTransaction>
    beginBuildTransaction(UIContext& context, UINodeId updaterRoot, UINodeId parent,
                          const UIElementDescriptor& rootDescriptor, UIComponentBuildBudget budget);


    [[nodiscard]] Core::Result<UINodeId>
    createElementFromBuildTransaction(UINodeId updaterRoot, UINodeId componentRoot, UINodeId parent,
                                      const UIElementDescriptor& descriptor,
                                      UIComponentBuildBudget& remainingBudget);


    [[nodiscard]] Core::Status commitBuildTransaction(UINodeId updaterRoot, UINodeId componentRoot,
                                                      UIComponentBuildBudget& remainingBudget);


    void rollbackBuildTransaction(UINodeId updaterRoot, UINodeId componentRoot,
                                  UIComponentBuildBudget& remainingBudget) noexcept;


    void unlinkFromTree(u32 index, NodeRecord& record) noexcept;


    void eraseDetachedSubtree(u32 index) noexcept;


    void releaseComponentBuildReservationsInSubtree(UINodeId subtreeRoot) noexcept;


    [[nodiscard]] Core::Status destroySubtree(UINodeId node);


    void destroyRootImmediately(UINodeId root) noexcept;

    void destroyRootFromOwner(UINodeId root) noexcept;
    [[nodiscard]] bool isAliveInRoot(UINodeId updaterRoot,
                                     UINodeId node) const noexcept;
    void releaseRoutedPointerListenerFromToken(u32 slot,
                                               u32 generation) noexcept;


    [[nodiscard]] Core::Status destroyFromUpdater(UINodeId updaterRoot, UINodeId node);


    [[nodiscard]] Core::Status setLayoutStyleFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                         const UILayoutStyle& style);


    [[nodiscard]] Core::Status setPointerHitPolicyFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                              UIPointerHitPolicy policy);


    [[nodiscard]] Core::Status setEnabledFromUpdater(UINodeId updaterRoot, UINodeId node, bool enabled);


    [[nodiscard]] Core::Result<bool> isEnabledFromUpdater(UINodeId updaterRoot, UINodeId node) const;


    [[nodiscard]] Core::Status setFocusScopeModeFromUpdater(UINodeId updaterRoot, UINodeId node, UIFocusScopeMode mode);


    [[nodiscard]] Core::Result<UIFocusScopeMode> focusScopeModeFromUpdater(UINodeId updaterRoot, UINodeId node) const;


    [[nodiscard]] Core::Status setStyleRoleFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                       UIStyleRoleId role);


    [[nodiscard]] Core::Result<UIStyleRoleId> styleRoleFromUpdater(UINodeId updaterRoot,
                                                                   UINodeId node) const;


    [[nodiscard]] Core::Status clearOverrideFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                        UIStyleOverride properties);


    [[nodiscard]] Core::Status setBoxPaintFromUpdater(UINodeId updaterRoot, UINodeId node, const UIBoxPaint& paint);


    [[nodiscard]] Core::Status setImageTintFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                       UIStraightSrgba8Color tint);


    [[nodiscard]] Core::Result<UIStraightSrgba8Color> imageTintFromUpdater(UINodeId updaterRoot,
                                                                           UINodeId node) const;


    [[nodiscard]] Core::Status setButtonPaintFromUpdater(UINodeId updaterRoot, UINodeId button,
                                                         const UIButtonPaint& paint);


    [[nodiscard]] Core::Result<UIButtonPaint> buttonPaintFromUpdater(UINodeId updaterRoot, UINodeId button) const;


    [[nodiscard]] Core::Status setTextFromUpdater(UINodeId updaterRoot, UINodeId node, std::string_view utf8);


    [[nodiscard]] Core::Status setTextStyleFromUpdater(UINodeId updaterRoot, UINodeId node, const UITextStyle& style);


    [[nodiscard]] Core::Status setContentAlignmentFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                               UIContentAlignment alignment);


    [[nodiscard]] Core::Status setTextOverflowFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                          UITextOverflow overflow);


    [[nodiscard]] Core::Result<UITextOverflow> textOverflowFromUpdater(
        UINodeId updaterRoot, UINodeId node);

    [[nodiscard]] Core::Status setTextWrapModeFromUpdater(
        UINodeId updaterRoot, UINodeId node, UITextWrapMode wrapMode);

    [[nodiscard]] Core::Result<UITextWrapMode> textWrapModeFromUpdater(
        UINodeId updaterRoot, UINodeId node);

    [[nodiscard]] Core::Status setTextLineClampFromUpdater(
        UINodeId updaterRoot, UINodeId node, UITextLineClamp lineClamp);

    [[nodiscard]] Core::Result<UITextLineClamp> textLineClampFromUpdater(
        UINodeId updaterRoot, UINodeId node);


    [[nodiscard]] Core::Result<std::string_view> textFromUpdater(UINodeId updaterRoot, UINodeId node);


    [[nodiscard]] Core::Result<UITextStyle> textStyleFromUpdater(UINodeId updaterRoot, UINodeId node);


    [[nodiscard]] Core::Result<UIContentAlignment> contentAlignmentFromUpdater(UINodeId updaterRoot,
                                                                                UINodeId node);


    [[nodiscard]] Core::Status setTextSelectionFromUpdater(UINodeId updaterRoot, UINodeId textEdit,
                                                           UITextSelection selection);


    [[nodiscard]] Core::Result<UITextSelection> textSelectionFromUpdater(UINodeId updaterRoot, UINodeId textEdit) const;


    [[nodiscard]] Core::Status setTextEditPaintFromUpdater(UINodeId updaterRoot, UINodeId textEdit,
                                                           const UITextEditPaint& paint);


    [[nodiscard]] Core::Result<UITextEditPaint> textEditPaintFromUpdater(UINodeId updaterRoot,
                                                                         UINodeId textEdit) const;


    [[nodiscard]] Core::Status setButtonActionFromUpdater(UINodeId updaterRoot, UINodeId button,
                                                          UIButtonActionCallback&& callback);


    [[nodiscard]] Core::Status clearButtonActionFromUpdater(UINodeId updaterRoot, UINodeId button);


    [[nodiscard]] Core::Result<bool> isButtonPressedFromUpdater(UINodeId updaterRoot, UINodeId button);


    [[nodiscard]] Core::Result<NodeRecord*> resolveCheckbox(UINodeId checkbox);


    [[nodiscard]] Core::Result<NodeRecord*> resolveToggle(UINodeId node);


    [[nodiscard]] Core::Status setCheckboxActionFromUpdater(UINodeId updaterRoot, UINodeId checkbox,
                                                            UIButtonActionCallback&& callback);


    [[nodiscard]] Core::Status clearCheckboxActionFromUpdater(UINodeId updaterRoot, UINodeId checkbox);


    [[nodiscard]] Core::Status setCheckboxPaintFromUpdater(UINodeId updaterRoot, UINodeId checkbox,
                                                           const UICheckboxPaint& paint);


    [[nodiscard]] Core::Result<UICheckboxPaint> checkboxPaintFromUpdater(UINodeId updaterRoot, UINodeId checkbox) const;


    [[nodiscard]] Core::Status setCheckedFromUpdater(UINodeId updaterRoot, UINodeId checkbox, bool checked);


    [[nodiscard]] Core::Result<bool> isCheckedFromUpdater(UINodeId updaterRoot, UINodeId checkbox) const;


    [[nodiscard]] Core::Result<bool> isCheckboxPressedFromUpdater(UINodeId updaterRoot, UINodeId checkbox);


    [[nodiscard]] Core::Result<NodeRecord*> resolveSlider(UINodeId slider);


    [[nodiscard]] Core::Result<NodeRecord*> resolveRangeInput(UINodeId node);


    [[nodiscard]] bool isInteractiveRangeInput(UINodeId node) const noexcept;


    [[nodiscard]] bool isPointerAdjustableRangeInput(UINodeId node) const noexcept;


    [[nodiscard]] Core::Result<bool> applySliderValue(UINodeId slider, double requestedValue,
                                                       Platform::PlatformFrameId platformFrame,
                                                       u64 sourceSequence, bool requireInteractive,
                                                       bool quantizeToStep = true);


    // Map pointer X into [min,max] using last committed hit worldRect for the slider.
    [[nodiscard]] Core::Result<bool> applySliderValueFromPointer(UINodeId slider, UILogicalPoint position,
                                                                 Platform::PlatformFrameId platformFrame,
                                                                 u64 sourceSequence);


    [[nodiscard]] Core::Result<bool> applySplitterValueFromPointer(
        UINodeId splitter, UILogicalPoint position,
        Platform::PlatformFrameId platformFrame, u64 sourceSequence);


    [[nodiscard]] Core::Result<bool> applyRangeInputValueFromPointer(
        UINodeId node, UILogicalPoint position,
        Platform::PlatformFrameId platformFrame, u64 sourceSequence);


    [[nodiscard]] Detail::UITextEditVisualHit textEditHitFromPointer(
        UINodeId textEdit, UILogicalPoint position) const noexcept;


    [[nodiscard]] Core::Status updateTextEditSelectionFromPointer(UINodeId textEdit, UILogicalPoint position,
                                                                  bool extendSelection);


    [[nodiscard]] Core::Status setSliderRangeFromUpdater(UINodeId updaterRoot, UINodeId slider, float minValue,
                                                         float maxValue, float step);


    [[nodiscard]] Core::Status setSliderValueFromUpdater(UINodeId updaterRoot, UINodeId slider, float value);


    [[nodiscard]] Core::Result<float> sliderValueFromUpdater(UINodeId updaterRoot, UINodeId slider) const;


    [[nodiscard]] Core::Status setSliderPaintFromUpdater(UINodeId updaterRoot, UINodeId slider,
                                                         const UISliderPaint& paint);


    [[nodiscard]] Core::Result<UISliderPaint> sliderPaintFromUpdater(UINodeId updaterRoot, UINodeId slider) const;


    [[nodiscard]] Core::Status setSliderChangeCallbackFromUpdater(UINodeId updaterRoot, UINodeId slider,
                                                                  UISliderChangeCallback&& callback);


    [[nodiscard]] Core::Status clearSliderChangeCallbackFromUpdater(UINodeId updaterRoot, UINodeId slider);


    [[nodiscard]] Core::Result<bool> isSliderDraggingFromUpdater(UINodeId updaterRoot, UINodeId slider) const;


    [[nodiscard]] Core::Result<NodeRecord*> resolveSplitView(UINodeId splitView);


    [[nodiscard]] Core::Result<NodeRecord*> resolveSplitter(UINodeId splitter);


    [[nodiscard]] Core::Status validateSplitViewUpdaterRoot(
        UINodeId updaterRoot, UINodeId splitView) const;


    [[nodiscard]] Core::Status setSplitViewPartsFromUpdater(
        UINodeId updaterRoot, UINodeId splitView, UINodeId primaryPane,
        UINodeId splitter, UINodeId secondaryPane);


    [[nodiscard]] Core::Status clearSplitViewPartsFromUpdater(
        UINodeId updaterRoot, UINodeId splitView);


    [[nodiscard]] Core::Result<UISplitViewParts> splitViewPartsFromUpdater(
        UINodeId updaterRoot, UINodeId splitView) const;


    [[nodiscard]] Core::Status setSplitViewFractionFromUpdater(
        UINodeId updaterRoot, UINodeId splitView, float fraction);


    [[nodiscard]] Core::Result<float> splitViewFractionFromUpdater(
        UINodeId updaterRoot, UINodeId splitView) const;


    [[nodiscard]] Core::Result<UISplitViewMetrics> splitViewMetricsFromUpdater(
        UINodeId updaterRoot, UINodeId splitView) const;


    [[nodiscard]] Core::Result<bool> isSplitterDraggingFromUpdater(
        UINodeId updaterRoot, UINodeId splitter) const;


    [[nodiscard]] Core::Status setSplitterPaintFromUpdater(
        UINodeId updaterRoot, UINodeId splitter, const UISplitterPaint& paint);


    [[nodiscard]] Core::Result<UISplitterPaint> splitterPaintFromUpdater(
        UINodeId updaterRoot, UINodeId splitter) const;


    [[nodiscard]] UINodeId rootForSplitView(UINodeId splitView) const noexcept;


    [[nodiscard]] Core::Status setSplitViewParts(
        UINodeId splitView, UINodeId primaryPane, UINodeId splitter,
        UINodeId secondaryPane);


    [[nodiscard]] Core::Status clearSplitViewParts(UINodeId splitView);


    [[nodiscard]] Core::Result<UISplitViewParts> splitViewParts(UINodeId splitView) const;


    [[nodiscard]] Core::Status setSplitViewFraction(UINodeId splitView, float fraction);


    [[nodiscard]] Core::Result<float> splitViewFraction(UINodeId splitView) const;


    [[nodiscard]] Core::Result<UISplitViewMetrics> splitViewMetrics(UINodeId splitView) const;


    [[nodiscard]] Core::Result<bool> isSplitterDragging(UINodeId splitter) const;


    [[nodiscard]] Core::Status setSplitterPaint(
        UINodeId splitter, const UISplitterPaint& paint);


    [[nodiscard]] Core::Result<UISplitterPaint> splitterPaint(
        UINodeId splitter) const;


    [[nodiscard]] Core::Result<NodeRecord*> resolveTabView(UINodeId tabView);


    [[nodiscard]] Core::Result<NodeRecord*> resolveTab(UINodeId tab);


    [[nodiscard]] Core::Status validateTabViewUpdaterRoot(
        UINodeId updaterRoot, UINodeId tabView) const;


    [[nodiscard]] Core::Status setTabPaintFromUpdater(
        UINodeId updaterRoot, UINodeId tab, const UITabPaint& paint);


    [[nodiscard]] Core::Result<UITabPaint> tabPaintFromUpdater(
        UINodeId updaterRoot, UINodeId tab) const;


    [[nodiscard]] Core::Status setTabViewItemsFromUpdater(
        UINodeId updaterRoot, UINodeId tabView,
        std::span<const UITabViewItem> items, u32 activeIndex);


    [[nodiscard]] Core::Status clearTabViewItemsFromUpdater(
        UINodeId updaterRoot, UINodeId tabView);


    [[nodiscard]] Core::Result<u32> tabViewItemCountFromUpdater(
        UINodeId updaterRoot, UINodeId tabView) const;


    [[nodiscard]] Core::Result<UITabViewItem> tabViewItemAtFromUpdater(
        UINodeId updaterRoot, UINodeId tabView, u32 index) const;


    [[nodiscard]] Core::Status setTabViewActiveTabFromUpdater(
        UINodeId updaterRoot, UINodeId tabView, UINodeId tab);


    [[nodiscard]] Core::Result<UINodeId> tabViewActiveTabFromUpdater(
        UINodeId updaterRoot, UINodeId tabView) const;


    [[nodiscard]] Core::Result<UINodeId> tabViewActivePanelFromUpdater(
        UINodeId updaterRoot, UINodeId tabView) const;


    [[nodiscard]] Core::Result<UITabViewMetrics> tabViewMetricsFromUpdater(
        UINodeId updaterRoot, UINodeId tabView) const;


    [[nodiscard]] Core::Result<UITabViewCommandResult> routeTabViewCommandFromUpdater(
        UINodeId updaterRoot, UINodeId tabView, UITabViewCommand command);


    [[nodiscard]] Core::Result<UITabViewCommandResult> routeFocusedTabViewCommand(
        UITabViewCommand command, bool pressed);


    [[nodiscard]] Core::Result<UITabViewCommandResult> routeFocusedTabViewDirection(
        UIFocusNavigationDirection direction, bool pressed);


    [[nodiscard]] UINodeId rootForTabView(UINodeId tabView) const noexcept;


    [[nodiscard]] Core::Status setTabViewItems(
        UINodeId tabView, std::span<const UITabViewItem> items, u32 activeIndex);


    [[nodiscard]] Core::Status clearTabViewItems(UINodeId tabView);


    [[nodiscard]] Core::Result<u32> tabViewItemCount(UINodeId tabView) const;


    [[nodiscard]] Core::Result<UITabViewItem> tabViewItemAt(UINodeId tabView, u32 index) const;


    [[nodiscard]] Core::Status setTabViewActiveTab(UINodeId tabView, UINodeId tab);


    [[nodiscard]] Core::Result<UINodeId> tabViewActiveTab(UINodeId tabView) const;


    [[nodiscard]] Core::Result<UINodeId> tabViewActivePanel(UINodeId tabView) const;


    [[nodiscard]] Core::Result<UITabViewMetrics> tabViewMetrics(UINodeId tabView) const;


    [[nodiscard]] Core::Result<UITabViewCommandResult> routeTabViewCommand(
        UINodeId tabView, UITabViewCommand command);


    [[nodiscard]] Core::Result<UIMenuCommandResult> routeMenuCommandFromUpdater(
        UINodeId updaterRoot, UINodeId menu, UIMenuCommand command);


    [[nodiscard]] Core::Result<UIMenuCommandResult> routeMenuCommand(
        UINodeId menu, UIMenuCommand command);


    [[nodiscard]] Core::Result<UIMenuCommandResult> routeMenuCommand(
        UIMenuCommand command, bool pressed);


    [[nodiscard]] Core::Result<UIMenuInvocationResult> routeMenuInvocation(
        UIMenuInvocationCommand command, bool pressed);


    [[nodiscard]] Core::Status setTabPaint(UINodeId tab, const UITabPaint& paint);


    [[nodiscard]] Core::Result<UITabPaint> tabPaint(UINodeId tab) const;


    [[nodiscard]] Core::Result<NodeRecord*> resolveScrollView(UINodeId scrollView);


    [[nodiscard]] Core::Result<NodeRecord*> resolveScroll(UINodeId node);


    [[nodiscard]] Core::Status markScrollOffsetDirty(UINodeId scrollView);


    [[nodiscard]] Core::Status markLayoutStyleDirty(UINodeId node);


    [[nodiscard]] bool isLiveScrollView(UINodeId scrollView) const noexcept;


    [[nodiscard]] bool isLiveListView(UINodeId listView) const noexcept;


    [[nodiscard]] bool isLiveTreeView(UINodeId treeView) const noexcept;


    [[nodiscard]] bool isLiveVirtualGridView(
        UINodeId virtualGridView) const noexcept;


    [[nodiscard]] bool isLiveDataGrid(UINodeId dataGrid) const noexcept;


    [[nodiscard]] bool isLiveVirtualView(UINodeId node) const noexcept;


    [[nodiscard]] bool isLiveVerticalVirtualView(UINodeId node) const noexcept;


    [[nodiscard]] bool isLiveScrollable(UINodeId node) const noexcept;


    [[nodiscard]] bool isLiveMultilineTextEdit(UINodeId node) const noexcept;


    [[nodiscard]] bool textEditWheelWouldChange(UINodeId textEdit, UILogicalPoint delta) const noexcept;


    [[nodiscard]] Core::Result<bool> applyTextEditScrollWheel(UINodeId textEdit, UILogicalPoint delta);



    [[nodiscard]] ScrollBarGeometry committedScrollBarGeometry(UINodeId scrollView, UIScrollAxes axis) const noexcept;


    [[nodiscard]] Core::Result<bool> applyScrollOffsetFromInput(UINodeId scrollView, UIScrollOffset requested);


    [[nodiscard]] UIScrollOffset resolvedScrollWheelOffset(UINodeId scrollView, UILogicalPoint delta) const noexcept;


    [[nodiscard]] bool scrollWheelWouldChange(UINodeId scrollView, UILogicalPoint delta) const noexcept;


    [[nodiscard]] Core::Result<bool> applyScrollWheel(UINodeId scrollView, UILogicalPoint delta);


    [[nodiscard]] Core::Result<bool> applyScrollThumbFromPointer(UINodeId scrollView, UIScrollAxes axis,
                                                                UILogicalPoint position, float grabOffset);


    [[nodiscard]] Core::Result<bool> applyScrollTrackPage(UINodeId scrollView, UIScrollAxes axis,
                                                         UILogicalPoint position);


    [[nodiscard]] ScrollBarPointerHit scrollBarPointerHit(std::span<const u32> routePath,
                                                         std::span<const UICommittedHitEntry> entries,
                                                         UILogicalPoint position) const noexcept;


    [[nodiscard]] Core::Status setScrollViewStyleFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                            const UIScrollViewStyle& style);


    [[nodiscard]] Core::Result<UIScrollViewStyle> scrollViewStyleFromUpdater(UINodeId updaterRoot,
                                                                             UINodeId scrollView) const;


    [[nodiscard]] Core::Status setScrollViewOffsetFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                             UIScrollOffset offset);


    [[nodiscard]] Core::Result<UIScrollOffset> scrollViewOffsetFromUpdater(UINodeId updaterRoot,
                                                                           UINodeId scrollView) const;


    [[nodiscard]] Core::Result<UIScrollViewMetrics> scrollViewMetricsFromUpdater(UINodeId updaterRoot,
                                                                                 UINodeId scrollView) const;


    [[nodiscard]] Core::Status setScrollViewPaintFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                            const UIScrollViewPaint& paint);


    [[nodiscard]] Core::Result<UIScrollViewPaint> scrollViewPaintFromUpdater(UINodeId updaterRoot,
                                                                             UINodeId scrollView) const;


    [[nodiscard]] Core::Result<bool> isScrollViewDraggingFromUpdater(UINodeId updaterRoot,
                                                                     UINodeId scrollView) const;


    [[nodiscard]] Core::Result<NodeRecord*> resolvePopup(UINodeId popup);


    [[nodiscard]] Core::Status registerDialogFromBuild(const UIDialogParts& parts);


    [[nodiscard]] Core::Result<NodeRecord*> resolveDialog(UINodeId dialog);


    void closeActiveMenuForDialogNoFail(UINodeId menu) noexcept;


    [[nodiscard]] Core::Status setDialogOpenState(UINodeId dialog, bool open);


    [[nodiscard]] Core::Status openDialog(UINodeId dialog);


    [[nodiscard]] Core::Status dismissDialog(UINodeId dialog);


    [[nodiscard]] Core::Result<bool> isDialogOpen(UINodeId dialog) const;


    [[nodiscard]] Core::Status validateDialogUpdaterRoot(
        UINodeId updaterRoot, UINodeId dialog) const;


    [[nodiscard]] Core::Status openDialogFromUpdater(
        UINodeId updaterRoot, UINodeId dialog);


    [[nodiscard]] Core::Status dismissDialogFromUpdater(
        UINodeId updaterRoot, UINodeId dialog);


    [[nodiscard]] Core::Result<bool> isDialogOpenFromUpdater(
        UINodeId updaterRoot, UINodeId dialog) const;


    [[nodiscard]] Core::Result<NodeRecord*> resolveTooltip(UINodeId tooltip);


    [[nodiscard]] bool hasValidTooltipRelationship(UINodeId tooltip,
                                                   UINodeId anchor) const noexcept;


    [[nodiscard]] UINodeId tooltipForAnchor(UINodeId anchor) const noexcept;


    [[nodiscard]] bool isAuthoredTooltipNodeVisible(UINodeId node) const noexcept;


    [[nodiscard]] bool isTooltipAnchorEligible(UINodeId tooltip) const noexcept;


    void markTooltipPresentationDirty() noexcept;


    [[nodiscard]] Detail::UITooltipAdvanceCandidate
    tooltipAdvanceCandidate(UINodeId tooltip) const noexcept;


    void advanceTooltips(Core::MonotonicTimePoint now) noexcept;


    void hardDismissAllTooltipsNoFail(bool suppress) noexcept;


    void dismissTooltipNoFail(UINodeId tooltip, bool suppress) noexcept;



    void reconcileTooltipAfterPublication(
        Core::MonotonicTimePoint now) noexcept;


    void reconcileMenuAfterPublication() noexcept;


    [[nodiscard]] UINodeId tooltipAnchorFromCommittedHit(
        const UIPointerHitTarget& target,
        std::span<const UICommittedHitEntry> entries) const noexcept;


    [[nodiscard]] Core::Status setTooltipAnchorRelation(UINodeId tooltip,
                                                        UINodeId anchor);


    [[nodiscard]] Core::Status clearTooltipAnchorRelation(UINodeId tooltip);


    [[nodiscard]] Core::Status setTooltipAnchor(UINodeId tooltip,
                                                UINodeId anchor);


    [[nodiscard]] Core::Status clearTooltipAnchor(UINodeId tooltip);


    [[nodiscard]] Core::Result<UINodeId>
    tooltipAnchor(UINodeId tooltip) const;


    [[nodiscard]] Core::Status showTooltip(UINodeId tooltip);


    [[nodiscard]] Core::Status dismissTooltip(UINodeId tooltip);


    [[nodiscard]] Core::Result<bool> isTooltipOpen(UINodeId tooltip) const;


    [[nodiscard]] Core::Result<UITooltipMetrics>
    tooltipMetrics(UINodeId tooltip) const;


    [[nodiscard]] Core::Status validateTooltipUpdaterRoot(
        UINodeId updaterRoot, UINodeId tooltip) const;


    [[nodiscard]] Core::Status setTooltipAnchorFromUpdater(
        UINodeId updaterRoot, UINodeId tooltip, UINodeId anchor);


    [[nodiscard]] Core::Status clearTooltipAnchorFromUpdater(
        UINodeId updaterRoot, UINodeId tooltip);


    [[nodiscard]] Core::Result<UINodeId> tooltipAnchorFromUpdater(
        UINodeId updaterRoot, UINodeId tooltip) const;


    [[nodiscard]] Core::Status showTooltipFromUpdater(
        UINodeId updaterRoot, UINodeId tooltip);


    [[nodiscard]] Core::Status dismissTooltipFromUpdater(
        UINodeId updaterRoot, UINodeId tooltip);


    [[nodiscard]] Core::Result<bool> isTooltipOpenFromUpdater(
        UINodeId updaterRoot, UINodeId tooltip) const;


    [[nodiscard]] Core::Result<UITooltipMetrics> tooltipMetricsFromUpdater(
        UINodeId updaterRoot, UINodeId tooltip) const;


    [[nodiscard]] Core::Result<NodeRecord*> resolveMenu(UINodeId menu);


    [[nodiscard]] Core::Result<NodeRecord*> resolveMenuItem(UINodeId item);


    [[nodiscard]] bool hasValidSubmenuRelationship(
        UINodeId item, UINodeId submenu) const noexcept;


    [[nodiscard]] UINodeId menuPlacementAnchor(UINodeId menu) const noexcept;


    [[nodiscard]] bool hasValidMenuPlacementRelationship(
        UINodeId menu, UINodeId anchor) const noexcept;


    void appendMenuMutationNode(UINodeId node);


    void appendActiveMenuBranchMutationNodes(UINodeId menu);


    [[nodiscard]] Core::Status markMenuMutationLayoutDirty(
        std::initializer_list<UINodeId> nodesToDirty,
        std::initializer_list<UINodeId> activeBranchRoots = {});


    void addActiveMenuBranchDirtyReservationCandidates(UINodeId menu);


    [[nodiscard]] bool isNodeWithinActiveMenuBranch(
        UINodeId menu, UINodeId node) const noexcept;


    [[nodiscard]] UINodeId firstActiveMenuBranchAffectedBy(
        UINodeId node, bool includeDescendants) const noexcept;


    [[nodiscard]] bool hasValidMenuRelationship(UINodeId menu,
                                                UINodeId anchor) const noexcept;


    [[nodiscard]] std::pair<UINodeId, UINodeId>
    contextMenuForTarget(UINodeId target) const noexcept;


    [[nodiscard]] bool isContextMenuInvocationCandidate(
        UINodeId menu, UINodeId anchor) const noexcept;


    [[nodiscard]] Core::Status validateMenuAnchor(UINodeId menu,
                                                  UINodeId anchor);


    [[nodiscard]] Core::Status setMenuAnchorRelation(UINodeId menu,
                                                     UINodeId anchor);


    [[nodiscard]] Core::Status clearMenuAnchorRelation(UINodeId menu);


    [[nodiscard]] Core::Status validateMenuItemSubmenu(
        UINodeId item, UINodeId submenu);


    [[nodiscard]] Core::Status setMenuItemSubmenuRelation(
        UINodeId item, UINodeId submenu);


    [[nodiscard]] Core::Status clearMenuItemSubmenuRelation(UINodeId item);


    [[nodiscard]] Core::Status setMenuOpenState(
        UINodeId menu, bool open,
        const UILogicalPoint* invocationPoint = nullptr);


    [[nodiscard]] Core::Status setMenuItemCheckedState(UINodeId item,
                                                       bool checked);


    [[nodiscard]] Core::Status setMenuAnchor(UINodeId menu, UINodeId anchor);


    [[nodiscard]] Core::Status clearMenuAnchor(UINodeId menu);


    [[nodiscard]] Core::Result<UINodeId> menuAnchor(UINodeId menu) const;


    [[nodiscard]] Core::Status setMenuOpen(UINodeId menu, bool open);


    [[nodiscard]] Core::Result<bool> isMenuOpen(UINodeId menu) const;


    [[nodiscard]] Core::Result<UIMenuMetrics> menuMetrics(UINodeId menu) const;


    [[nodiscard]] Core::Status setMenuItemSubmenu(
        UINodeId item, UINodeId submenu);


    [[nodiscard]] Core::Status clearMenuItemSubmenu(UINodeId item);


    [[nodiscard]] Core::Result<UINodeId> menuItemSubmenu(
        UINodeId item) const;


    [[nodiscard]] Core::Result<UINodeId> menuParentItem(
        UINodeId menu) const;


    [[nodiscard]] Core::Status setMenuItemChecked(UINodeId item, bool checked);


    [[nodiscard]] Core::Result<bool> isMenuItemChecked(UINodeId item) const;


    [[nodiscard]] Core::Status validateMenuUpdaterRoot(UINodeId updaterRoot,
                                                       UINodeId menu) const;


    [[nodiscard]] Core::Status setMenuAnchorFromUpdater(
        UINodeId updaterRoot, UINodeId menu, UINodeId anchor);


    [[nodiscard]] Core::Status clearMenuAnchorFromUpdater(
        UINodeId updaterRoot, UINodeId menu);


    [[nodiscard]] Core::Result<UINodeId> menuAnchorFromUpdater(
        UINodeId updaterRoot, UINodeId menu) const;


    [[nodiscard]] Core::Status setMenuOpenFromUpdater(
        UINodeId updaterRoot, UINodeId menu, bool open);


    [[nodiscard]] Core::Result<bool> isMenuOpenFromUpdater(
        UINodeId updaterRoot, UINodeId menu) const;


    [[nodiscard]] Core::Result<UIMenuMetrics> menuMetricsFromUpdater(
        UINodeId updaterRoot, UINodeId menu) const;


    [[nodiscard]] Core::Status setMenuItemSubmenuFromUpdater(
        UINodeId updaterRoot, UINodeId item, UINodeId submenu);


    [[nodiscard]] Core::Status clearMenuItemSubmenuFromUpdater(
        UINodeId updaterRoot, UINodeId item);


    [[nodiscard]] Core::Result<UINodeId> menuItemSubmenuFromUpdater(
        UINodeId updaterRoot, UINodeId item) const;


    [[nodiscard]] Core::Result<UINodeId> menuParentItemFromUpdater(
        UINodeId updaterRoot, UINodeId menu) const;


    [[nodiscard]] Core::Status setMenuItemCheckedFromUpdater(
        UINodeId updaterRoot, UINodeId item, bool checked);


    [[nodiscard]] Core::Result<bool> isMenuItemCheckedFromUpdater(
        UINodeId updaterRoot, UINodeId item) const;


    [[nodiscard]] Core::Result<NodeRecord*> resolveDropdown(UINodeId dropdown);


    [[nodiscard]] Core::Result<NodeRecord*> resolveDropdownItem(UINodeId item);


    [[nodiscard]] Core::Result<NodeRecord*> resolveListView(UINodeId listView);


    [[nodiscard]] UINodeId listViewForItem(UINodeId item) const noexcept;


    [[nodiscard]] bool isSelectedListViewItem(UINodeId item) const noexcept;


    [[nodiscard]] Core::Status selectCommittedListViewItem(UINodeId item);


    [[nodiscard]] Core::Result<NodeRecord*> resolveVirtualGridView(
        UINodeId virtualGridView);


    [[nodiscard]] Core::Result<NodeRecord*> resolveDataGrid(UINodeId dataGrid);


    [[nodiscard]] Core::Status validateDataGridUpdater(
        UINodeId updaterRoot, UINodeId dataGrid) const;


    [[nodiscard]] UINodeId virtualGridViewForItem(
        UINodeId item) const noexcept;


    [[nodiscard]] bool isSelectedVirtualGridViewItem(
        UINodeId item) const noexcept;


    [[nodiscard]] Core::Status selectCommittedVirtualGridViewItem(
        UINodeId item);


    [[nodiscard]] UINodeId dataGridForCell(UINodeId cell) const noexcept;


    [[nodiscard]] bool isSelectedDataGridCell(UINodeId cell) const noexcept;


    [[nodiscard]] bool isSelectedDataGridRowCell(UINodeId cell) const noexcept;


    [[nodiscard]] bool isHoveredDataGridRowCell(UINodeId cell) const noexcept;


    [[nodiscard]] Core::Status selectCommittedDataGridCell(UINodeId cell);


    [[nodiscard]] Core::Result<NodeRecord*> resolveTreeView(UINodeId treeView);


    [[nodiscard]] UINodeId treeViewForItem(UINodeId item) const noexcept;


    [[nodiscard]] bool pointWithinCommittedTreeDisclosure(UINodeId item, UILogicalPoint point,
                                                          std::span<const UICommittedHitEntry> entries) const noexcept;


    [[nodiscard]] bool isSelectedTreeViewItem(UINodeId item) const noexcept;


    [[nodiscard]] Core::Status selectCommittedTreeViewItem(UINodeId item);


    [[nodiscard]] Core::Status toggleCommittedTreeViewItem(UINodeId item);


    [[nodiscard]] Core::Status setPopupOpenState(UINodeId popup, bool open);


    [[nodiscard]] Core::Status setPopupStyleFromUpdater(UINodeId updaterRoot, UINodeId popup,
                                                       const UIPopupStyle& style);


    [[nodiscard]] Core::Result<UIPopupStyle> popupStyleFromUpdater(UINodeId updaterRoot, UINodeId popup) const;


    [[nodiscard]] Core::Status setPopupOpenFromUpdater(UINodeId updaterRoot, UINodeId popup, bool open);


    [[nodiscard]] Core::Result<bool> isPopupOpenFromUpdater(UINodeId updaterRoot, UINodeId popup) const;


    [[nodiscard]] Core::Result<UIPopupMetrics> popupMetricsFromUpdater(UINodeId updaterRoot, UINodeId popup) const;


    [[nodiscard]] Core::Status setDropdownOpenFromUpdater(UINodeId updaterRoot, UINodeId dropdown, bool open);


    [[nodiscard]] Core::Result<bool> isDropdownOpenFromUpdater(UINodeId updaterRoot, UINodeId dropdown) const;


    [[nodiscard]] Core::Status setDropdownSelectedItemFromUpdater(UINodeId updaterRoot, UINodeId dropdown,
                                                                 UINodeId item);


    [[nodiscard]] Core::Result<UINodeId> dropdownSelectedItemFromUpdater(UINodeId updaterRoot,
                                                                         UINodeId dropdown) const;


    [[nodiscard]] Core::Result<bool> isDropdownItemSelectedFromUpdater(UINodeId updaterRoot,
                                                                       UINodeId item) const;


    [[nodiscard]] Core::Status setDropdownPaintFromUpdater(UINodeId updaterRoot, UINodeId dropdown,
                                                          const UIDropdownPaint& paint);


    [[nodiscard]] Core::Result<UIDropdownPaint> dropdownPaintFromUpdater(UINodeId updaterRoot,
                                                                        UINodeId dropdown) const;


    [[nodiscard]] Core::Status validateListViewUpdater(UINodeId updaterRoot, UINodeId listView) const;


    [[nodiscard]] Core::Result<UIListViewItemDescriptor> resolveListViewLogicalItem(UINodeId listView,
                                                                                   u64 logicalIndex) const;


    [[nodiscard]] Core::Status setListViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                               UIListViewDataSource source);


    [[nodiscard]] Core::Status clearListViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId listView);


    [[nodiscard]] Core::Status invalidateListViewItemsFromUpdater(UINodeId updaterRoot, UINodeId listView);


    [[nodiscard]] Core::Status setListViewStyleFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                          const UIListViewStyle& style);


    [[nodiscard]] Core::Result<UIListViewStyle> listViewStyleFromUpdater(UINodeId updaterRoot,
                                                                        UINodeId listView) const;


    [[nodiscard]] Core::Status setListViewPaintFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                          const UIListViewPaint& paint);


    [[nodiscard]] Core::Result<UIListViewPaint> listViewPaintFromUpdater(UINodeId updaterRoot,
                                                                        UINodeId listView) const;


    [[nodiscard]] Core::Result<UIListViewMetrics> listViewMetricsFromUpdater(UINodeId updaterRoot,
                                                                            UINodeId listView) const;


    [[nodiscard]] Core::Status setListViewSelectedIndexFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                                  u64 logicalIndex);


    [[nodiscard]] Core::Status clearListViewSelectionFromUpdater(UINodeId updaterRoot, UINodeId listView);


    [[nodiscard]] Core::Result<UIListViewSelection> listViewSelectionFromUpdater(UINodeId updaterRoot,
                                                                                UINodeId listView) const;


    [[nodiscard]] Core::Status scrollListViewToIndexFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                               u64 logicalIndex,
                                                               UIListViewScrollAlignment alignment);


    [[nodiscard]] Core::Status validateVirtualGridViewUpdater(
        UINodeId updaterRoot, UINodeId virtualGridView) const;


    [[nodiscard]] Core::Result<UIVirtualGridViewItemDescriptor>
    resolveVirtualGridViewLogicalItem(
        UINodeId virtualGridView, u64 logicalIndex) const;


    [[nodiscard]] Core::Status setVirtualGridViewDataSourceFromUpdater(
        UINodeId updaterRoot, UINodeId virtualGridView,
        UIVirtualGridViewDataSource source);


    [[nodiscard]] Core::Status clearVirtualGridViewDataSourceFromUpdater(
        UINodeId updaterRoot, UINodeId virtualGridView);


    [[nodiscard]] Core::Status invalidateVirtualGridViewItemsFromUpdater(
        UINodeId updaterRoot, UINodeId virtualGridView);


    [[nodiscard]] Core::Status setVirtualGridViewStyleFromUpdater(
        UINodeId updaterRoot, UINodeId virtualGridView,
        const UIVirtualGridViewStyle& style);


    [[nodiscard]] Core::Result<UIVirtualGridViewStyle>
    virtualGridViewStyleFromUpdater(
        UINodeId updaterRoot, UINodeId virtualGridView) const;


    [[nodiscard]] Core::Status setVirtualGridViewPaintFromUpdater(
        UINodeId updaterRoot, UINodeId virtualGridView,
        const UIVirtualGridViewPaint& paint);


    [[nodiscard]] Core::Result<UIVirtualGridViewPaint>
    virtualGridViewPaintFromUpdater(
        UINodeId updaterRoot, UINodeId virtualGridView) const;


    [[nodiscard]] Core::Result<UIVirtualGridViewMetrics>
    virtualGridViewMetricsFromUpdater(
        UINodeId updaterRoot, UINodeId virtualGridView) const;


    [[nodiscard]] Core::Result<UINodeId>
    virtualGridViewMaterializedItemNodeFromUpdater(
        UINodeId updaterRoot, UINodeId virtualGridView,
        u64 logicalIndex) const;


    [[nodiscard]] Core::Status setVirtualGridViewSelectedIndexFromUpdater(
        UINodeId updaterRoot, UINodeId virtualGridView, u64 logicalIndex);


    [[nodiscard]] Core::Status clearVirtualGridViewSelectionFromUpdater(
        UINodeId updaterRoot, UINodeId virtualGridView);


    [[nodiscard]] Core::Result<UIVirtualGridViewSelection>
    virtualGridViewSelectionFromUpdater(
        UINodeId updaterRoot, UINodeId virtualGridView) const;


    [[nodiscard]] Core::Status scrollVirtualGridViewToIndexFromUpdater(
        UINodeId updaterRoot, UINodeId virtualGridView, u64 logicalIndex,
        UIVirtualGridViewScrollAlignment alignment);


    [[nodiscard]] Core::Result<UIDataGridSelection> resolveDataGridSelection(
        UINodeId dataGrid, u64 logicalRow, u32 logicalColumn,
        bool* rowEnabled = nullptr) const;


    [[nodiscard]] Core::Status setDataGridDataSourceFromUpdater(
        UINodeId updaterRoot, UINodeId dataGrid, UIDataGridDataSource source);


    [[nodiscard]] Core::Status clearDataGridDataSourceFromUpdater(
        UINodeId updaterRoot, UINodeId dataGrid);


    [[nodiscard]] Core::Status invalidateDataGridItemsFromUpdater(
        UINodeId updaterRoot, UINodeId dataGrid);


    [[nodiscard]] Core::Status setDataGridStyleFromUpdater(
        UINodeId updaterRoot, UINodeId dataGrid,
        const UIDataGridStyle& style);


    [[nodiscard]] Core::Result<UIDataGridStyle> dataGridStyleFromUpdater(
        UINodeId updaterRoot, UINodeId dataGrid) const;


    [[nodiscard]] Core::Status setDataGridPaintFromUpdater(
        UINodeId updaterRoot, UINodeId dataGrid,
        const UIDataGridPaint& paint);


    [[nodiscard]] Core::Result<UIDataGridPaint> dataGridPaintFromUpdater(
        UINodeId updaterRoot, UINodeId dataGrid) const;


    [[nodiscard]] Core::Result<UIDataGridMetrics> dataGridMetricsFromUpdater(
        UINodeId updaterRoot, UINodeId dataGrid) const;


    [[nodiscard]] Core::Status setDataGridSelectedCellFromUpdater(
        UINodeId updaterRoot, UINodeId dataGrid, u64 logicalRow,
        u32 logicalColumn);


    [[nodiscard]] Core::Status clearDataGridSelectionFromUpdater(
        UINodeId updaterRoot, UINodeId dataGrid);


    [[nodiscard]] Core::Result<UIDataGridSelection>
    dataGridSelectionFromUpdater(
        UINodeId updaterRoot, UINodeId dataGrid) const;


    [[nodiscard]] Core::Status scrollDataGridToCellFromUpdater(
        UINodeId updaterRoot, UINodeId dataGrid, u64 logicalRow,
        u32 logicalColumn, UIDataGridScrollAlignment alignment);


    [[nodiscard]] Core::Status validateTreeViewUpdater(UINodeId updaterRoot, UINodeId treeView) const;


    [[nodiscard]] Core::Result<UITreeViewItemDescriptor> resolveTreeViewLogicalItem(UINodeId treeView,
                                                                                    u64 logicalIndex) const;


    [[nodiscard]] Core::Status setTreeViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                                UITreeViewDataSource source);


    [[nodiscard]] Core::Status clearTreeViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId treeView);


    [[nodiscard]] Core::Status invalidateTreeViewItemsFromUpdater(UINodeId updaterRoot, UINodeId treeView);


    [[nodiscard]] Core::Status setTreeViewStyleFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                           const UITreeViewStyle& style);


    [[nodiscard]] Core::Result<UITreeViewStyle> treeViewStyleFromUpdater(UINodeId updaterRoot, UINodeId treeView) const;


    [[nodiscard]] Core::Status setTreeViewPaintFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                           const UITreeViewPaint& paint);


    [[nodiscard]] Core::Result<UITreeViewPaint> treeViewPaintFromUpdater(UINodeId updaterRoot, UINodeId treeView) const;


    [[nodiscard]] Core::Result<UITreeViewMetrics> treeViewMetricsFromUpdater(UINodeId updaterRoot,
                                                                             UINodeId treeView) const;


    [[nodiscard]] Core::Result<UINodeId> treeViewMaterializedItemNodeFromUpdater(
        UINodeId updaterRoot, UINodeId treeView, u64 logicalIndex) const;


    [[nodiscard]] Core::Status setTreeViewSelectedIndexFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                                   u64 logicalIndex);


    [[nodiscard]] Core::Status clearTreeViewSelectionFromUpdater(UINodeId updaterRoot, UINodeId treeView);


    [[nodiscard]] Core::Result<UITreeViewSelection> treeViewSelectionFromUpdater(UINodeId updaterRoot,
                                                                                 UINodeId treeView) const;


    [[nodiscard]] Core::Status setTreeViewItemExpandedFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                                  u64 logicalIndex, bool expanded);


    [[nodiscard]] Core::Status scrollTreeViewToIndexFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                                u64 logicalIndex, UITreeViewScrollAlignment alignment);


    void addDropdownActivationDirtyReservationCandidates(UINodeId control);


    void addMenuActivationDirtyReservationCandidates(UINodeId control);


    [[nodiscard]] bool isSubmenuMenuItem(UINodeId item) const noexcept;


    [[nodiscard]] Core::Result<bool> activateMenuItem(UINodeId item);


    void addTabActivationDirtyReservationCandidates(UINodeId tab);


    [[nodiscard]] Core::Result<bool> activateTabControl(UINodeId tab);


    [[nodiscard]] Core::Result<bool> activateDropdownControl(UINodeId control);


    [[nodiscard]] Core::Result<NodeRecord*> resolveProgressBar(UINodeId progressBar);


    [[nodiscard]] Core::Result<NodeRecord*> resolvePlainButton(UINodeId button);


    [[nodiscard]] Core::Status setProgressBarRangeFromUpdater(UINodeId updaterRoot, UINodeId progressBar,
                                                              float minValue, float maxValue);


    [[nodiscard]] Core::Status setProgressBarValueFromUpdater(UINodeId updaterRoot, UINodeId progressBar, float value);


    [[nodiscard]] Core::Result<float> progressBarValueFromUpdater(UINodeId updaterRoot, UINodeId progressBar) const;


    [[nodiscard]] Core::Status setProgressBarPaintFromUpdater(UINodeId updaterRoot, UINodeId progressBar,
                                                              const UIProgressBarPaint& paint);


    [[nodiscard]] Core::Result<UIProgressBarPaint> progressBarPaintFromUpdater(UINodeId updaterRoot,
                                                                               UINodeId progressBar) const;


    [[nodiscard]] Core::Result<NodeRecord*> resolveRadioButton(UINodeId radioButton);


    [[nodiscard]] Core::Status preflightDefaultActionActivationDirty(UINodeId target, bool pressedStateChanges) const;


    [[nodiscard]] Core::Status applyRadioButtonSelection(UINodeId radioButton, bool selected);


    [[nodiscard]] Core::Status setRadioButtonPaintFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                              const UIRadioButtonPaint& paint);


    [[nodiscard]] Core::Result<UIRadioButtonPaint> radioButtonPaintFromUpdater(UINodeId updaterRoot,
                                                                               UINodeId radioButton) const;


    [[nodiscard]] Core::Status setRadioButtonActionFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                               UIButtonActionCallback&& callback);


    [[nodiscard]] Core::Status clearRadioButtonActionFromUpdater(UINodeId updaterRoot, UINodeId radioButton);


    [[nodiscard]] Core::Status setRadioButtonSelectedFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                                 bool selected);


    [[nodiscard]] Core::Result<bool> isRadioButtonSelectedFromUpdater(UINodeId updaterRoot, UINodeId radioButton) const;


    [[nodiscard]] Core::Result<bool> isRadioButtonPressedFromUpdater(UINodeId updaterRoot, UINodeId radioButton);


    void appendCommittedTree(u32 index, u32& ordinal, std::pmr::vector<UICommittedNodeEntry>& output) const noexcept;


    [[nodiscard]] Core::Status commitStructure();


    [[nodiscard]] Core::Status validateViewport(UILogicalSize viewportSize) const;


    void publishControlLayoutState(const std::pmr::vector<u32>& order) noexcept;


    [[nodiscard]] Core::Status commitLayout(UILogicalSize viewportSize);


    [[nodiscard]] UICommittedStructureView committedStructure() const noexcept;


    [[nodiscard]] UICommittedLayoutView committedLayout() const noexcept;


    [[nodiscard]] UICommittedHitView committedHit() const noexcept;


    [[nodiscard]] UICommittedPaintView committedPaint() const noexcept;


    [[nodiscard]] std::optional<UILogicalRect> committedTextInputCaretRectValue() const noexcept;


    [[nodiscard]] UICommittedSemanticsView committedSemantics() const noexcept;


    [[nodiscard]] std::span<const u8> glyphAtlasPixels() const noexcept;


    [[nodiscard]] u32 glyphAtlasWidth() const noexcept;


    [[nodiscard]] u32 glyphAtlasHeight() const noexcept;


    [[nodiscard]] Core::Status openTextFont(std::span<const std::byte> fontBytes, i32 faceIndex);


    [[nodiscard]] UIPointerHitQueryResult queryPointerHit(UILogicalPoint point) const noexcept;


    [[nodiscard]] Core::Result<std::pair<u32, u32>> addRoutedPointerListener(UIRoutedPointerListenerDesc descriptor,
                                                                             UIRoutedPointerCallback&& callback,
                                                                             UINodeId updaterRoot);


    [[nodiscard]] Core::Result<std::pair<u32, u32>>
    addRoutedPointerListenerFromUpdater(UINodeId updaterRoot, UIRoutedPointerListenerDesc descriptor,
                                        UIRoutedPointerCallback&& callback);


    [[nodiscard]] Core::Status validatePointerInput(const UIPointerInputEvent& input) const;


    void dispatchRoutedPointerListeners(UINodeId node, UIEventPhase phase, UIRoutedPointerEventKind kind,
                                        u64 registrationSerialBoundary, UIRoutedPointerEvent& event,
                                        UIPointerRouteResult& result) noexcept;


    void finishRoutedPointerDispatch() noexcept;


    void dispatchPointerCancelToCapture(std::span<const UICommittedHitEntry> entries) noexcept;


    void dispatchPointerCancelForCurrentCapture() noexcept;


    void dispatchPointerCancelForSubtree(UINodeId subtreeRoot) noexcept;


    [[nodiscard]] Core::Result<UIPointerRouteResult> routePointerInput(const UIPointerInputEvent& input);


    [[nodiscard]] Core::Status cancelPointerInteraction(Platform::WindowId routedWindow);


    [[nodiscard]] static bool isValidFlowLocalUser(UIFlowLocalUserId localUser) noexcept;


    [[nodiscard]] static usize flowLocalUserIndex(UIFlowLocalUserId localUser) noexcept;


    [[nodiscard]] Core::Status validateFlowLocalUser(UIFlowLocalUserId localUser) const;


    [[nodiscard]] Core::Status validateFlowGamepad(Platform::GamepadId gamepad) const;


    [[nodiscard]] UIFlowLocalUserId
    flowLocalUserForGamepadUnchecked(Platform::GamepadId gamepad) const noexcept;


    [[nodiscard]] Core::Status fallbackFlowInputDevicesForGamepads(
        Platform::GamepadId first,
        std::optional<Platform::GamepadId> second = std::nullopt);


    [[nodiscard]] Core::Status fallbackAllFlowInputDevices();


    [[nodiscard]] Core::Status assignFlowGamepad(
        Platform::GamepadId gamepad, UIFlowLocalUserId localUser);


    [[nodiscard]] Core::Status
    clearFlowGamepadAssignment(Platform::GamepadId gamepad);


    [[nodiscard]] Core::Result<UIFlowLocalUserId>
    flowLocalUserForGamepad(Platform::GamepadId gamepad) const;


    [[nodiscard]] Core::Result<UIFlowInputDeviceState>
    flowInputDeviceState(UIFlowLocalUserId localUser) const;


    [[nodiscard]] Core::Status cancelDefaultActionInteraction(Platform::WindowId routedWindow,
                                                              std::optional<Platform::GamepadId> gamepad);


    [[nodiscard]] Core::Status observeFlowInputDevice(
        Platform::PlatformFrameId platformFrame, u64 sourceSequence,
        UIFlowLocalUserId localUser, UIFlowInputDevice device,
        std::optional<Platform::GamepadId> gamepad);


    [[nodiscard]] Core::Result<UIFlowActionRouteResult>
    routeFlowAction(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                    UIFlowLocalUserId localUser, UIFlowAction action,
                    UIFlowActionSource source, bool pressed,
                    const Platform::DigitalControlIdentity& control);


    [[nodiscard]] Core::Result<UIDefaultActionResult>
    routeDefaultActionActivate(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                               UIButtonActivationSource source,
                               std::optional<Platform::DigitalControlIdentity> control,
                               UINodeId explicitAccessibilityTarget = {});


    [[nodiscard]] Core::Status performAccessibilityAction(const UIAccessibilityAction& action);


    [[nodiscard]] Core::Result<UIDefaultActionResult>
    routeDefaultActionRelease(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                              UIButtonActivationSource source, const Platform::DigitalControlIdentity& control);


    [[nodiscard]] Core::Status applyExplicitFocus(UINodeId nextFocus);


    [[nodiscard]] Core::Status requestFocus(UINodeId node);


    [[nodiscard]] Core::Status clearFocus();


    [[nodiscard]] Core::Status requestFocusFromUpdater(UINodeId updaterRoot, UINodeId node);


    [[nodiscard]] Core::Status clearFocusFromUpdater(UINodeId updaterRoot);


    [[nodiscard]] UINodeId activeFocusScope() const noexcept;


    [[nodiscard]] UINodeId activeModal() const noexcept;


    [[nodiscard]] UINodeId pointerCapture() const noexcept;


    [[nodiscard]] UINodeId activePopup() const noexcept;


    [[nodiscard]] UINodeId activeMenu() const noexcept;


    [[nodiscard]] Core::Result<UIDropdownCommandResult> routeDropdownCommand(UIDropdownCommand command,
                                                                             bool pressed);


    [[nodiscard]] Core::Result<UIListViewCommandResult> routeListViewCommand(UIListViewCommand command,
                                                                             bool pressed);


    [[nodiscard]] Core::Result<UIVirtualGridViewCommandResult>
    routeVirtualGridViewCommand(UIVirtualGridViewCommand command, bool pressed);


    [[nodiscard]] Core::Result<UIDataGridCommandResult>
    routeDataGridCommand(UIDataGridCommand command, bool pressed);


    [[nodiscard]] UINodeId rootForVirtualGridView(UINodeId virtualGridView) const noexcept;


    [[nodiscard]] Core::Status setVirtualGridViewDataSource(
        UINodeId virtualGridView, UIVirtualGridViewDataSource source);


    [[nodiscard]] Core::Status clearVirtualGridViewDataSource(UINodeId virtualGridView);


    [[nodiscard]] Core::Status invalidateVirtualGridViewItems(UINodeId virtualGridView);


    [[nodiscard]] Core::Status setVirtualGridViewStyle(
        UINodeId virtualGridView, const UIVirtualGridViewStyle& style);


    [[nodiscard]] Core::Result<UIVirtualGridViewStyle>
    virtualGridViewStyle(UINodeId virtualGridView) const;


    [[nodiscard]] Core::Status setVirtualGridViewPaint(
        UINodeId virtualGridView, const UIVirtualGridViewPaint& paint);


    [[nodiscard]] Core::Result<UIVirtualGridViewPaint>
    virtualGridViewPaint(UINodeId virtualGridView) const;


    [[nodiscard]] Core::Result<UIVirtualGridViewMetrics>
    virtualGridViewMetrics(UINodeId virtualGridView) const;


    [[nodiscard]] Core::Status setVirtualGridViewSelectedIndex(
        UINodeId virtualGridView, u64 logicalIndex);


    [[nodiscard]] Core::Status clearVirtualGridViewSelection(UINodeId virtualGridView);


    [[nodiscard]] Core::Result<UIVirtualGridViewSelection>
    virtualGridViewSelection(UINodeId virtualGridView) const;


    [[nodiscard]] Core::Status scrollVirtualGridViewToIndex(
        UINodeId virtualGridView, u64 logicalIndex,
        UIVirtualGridViewScrollAlignment alignment);


    [[nodiscard]] UINodeId rootForDataGrid(UINodeId dataGrid) const noexcept;


    [[nodiscard]] Core::Status setDataGridDataSource(
        UINodeId dataGrid, UIDataGridDataSource source);


    [[nodiscard]] Core::Status clearDataGridDataSource(UINodeId dataGrid);


    [[nodiscard]] Core::Status invalidateDataGridItems(UINodeId dataGrid);


    [[nodiscard]] Core::Status setDataGridStyle(
        UINodeId dataGrid, const UIDataGridStyle& style);


    [[nodiscard]] Core::Result<UIDataGridStyle>
    dataGridStyle(UINodeId dataGrid) const;


    [[nodiscard]] Core::Status setDataGridPaint(
        UINodeId dataGrid, const UIDataGridPaint& paint);


    [[nodiscard]] Core::Result<UIDataGridPaint>
    dataGridPaint(UINodeId dataGrid) const;


    [[nodiscard]] Core::Result<UIDataGridMetrics>
    dataGridMetrics(UINodeId dataGrid) const;


    [[nodiscard]] Core::Status setDataGridSelectedCell(
        UINodeId dataGrid, u64 logicalRow, u32 logicalColumn);


    [[nodiscard]] Core::Status clearDataGridSelection(UINodeId dataGrid);


    [[nodiscard]] Core::Result<UIDataGridSelection>
    dataGridSelection(UINodeId dataGrid) const;


    [[nodiscard]] Core::Status scrollDataGridToCell(
        UINodeId dataGrid, u64 logicalRow, u32 logicalColumn,
        UIDataGridScrollAlignment alignment);


    [[nodiscard]] Core::Result<UITreeViewCommandResult> routeTreeViewCommand(UITreeViewCommand command, bool pressed);


    [[nodiscard]] Core::Status applyNavigationFocus(UINodeId nextFocus);


    [[nodiscard]] Core::Result<UIDefaultFocusStepResult> routeDefaultActionFocusStep(bool reverse);


    [[nodiscard]] Core::Result<UIRangeInputCommandResult>
    routeRangeInputCommand(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                           UIRangeInputCommand command, bool pressed,
                           const Platform::DigitalControlIdentity& control);


    [[nodiscard]] Core::Result<UIDefaultFocusStepResult>
    routeFocusNavigation(UIFocusNavigationDirection direction, bool pressed,
                         UIInputModality modality);


    [[nodiscard]] UINodeId defaultActionFocus() const noexcept;


    [[nodiscard]] UINodeId imeFocus() const noexcept;


    [[nodiscard]] bool imeCompositionActive() const noexcept;


    [[nodiscard]] std::string_view imePreeditUtf8() const noexcept;


    [[nodiscard]] u32 imePreeditCursorCodepoint() const noexcept;


    [[nodiscard]] Core::Result<UITextInputRouteResult>
    routeTextComposition(Platform::WindowId window, Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                         std::string_view preeditUtf8, u32 cursorCodepoint, Platform::TextCompositionStage stage);


    [[nodiscard]] Core::Result<UITextInputRouteResult> routeTextInput(Platform::WindowId window,
                                                                      Platform::PlatformFrameId platformFrame,
                                                                      u64 sourceSequence,
                                                                      std::string_view committedUtf8);


    [[nodiscard]] Core::Result<UITextInputRouteResult>
    routeTextEditCommand(Platform::WindowId window, Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                         UITextEditCommand command, bool extendSelection);


    [[nodiscard]] UIContextStatistics statistics() const noexcept;

};

} // namespace Tina::UI
