#include <tina/ui/UIContext.hpp>

#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/id/GenerationPool.hpp>
#include <tina/core/text/Utf8.hpp>
#include <tina/ui/UIDirty.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/UITheme.hpp>
#include <tina/ui/text/UIGlyphAtlas.hpp>
#include <tina/ui/text/UITextRasterizer.hpp>

#include "detail/UIButtonActionRegistry.hpp"
#include "detail/UIBehaviorStateStorage.hpp"
#include "detail/UICanvasCommandStorage.hpp"
#include "detail/UICommandPressLatch.hpp"
#include "detail/UIControlGeometry.hpp"
#include "detail/UIControlPaintEmitter.hpp"
#include "detail/UIControlValuePrimitives.hpp"
#include "detail/UIContextLifetimeControl.hpp"
#include "detail/UIElementContractResolver.hpp"
#include "detail/UIFlexLayout.hpp"
#include "detail/UIFocusNavigation.hpp"
#include "detail/UIDefaultActionPressState.hpp"
#include "detail/UIDirtyQueueStorage.hpp"
#include "detail/UIImeCompositionState.hpp"
#include "detail/UIImageContentStorage.hpp"
#include "detail/UINineSlicePaintEmitter.hpp"
#include "detail/UIInputPrimitives.hpp"
#include "detail/UILayoutMeasurement.hpp"
#include "detail/UILayoutPrimitives.hpp"
#include "detail/UIPaintPrimitives.hpp"
#include "detail/UIPaintSnapshotBuilder.hpp"
#include "detail/UIPointerRouteInspection.hpp"
#include "detail/UIPointerRoutePath.hpp"
#include "detail/UIPropertyNormalization.hpp"
#include "detail/UIRangeInputPressLatch.hpp"
#include "detail/UIRoutedPointerListenerRegistry.hpp"
#include "detail/UIScrollViewLayout.hpp"
#include "detail/UISemanticsSnapshotBuilder.hpp"
#include "detail/UISliderChangeCallbackRegistry.hpp"
#include "detail/UIStyleRoleResolver.hpp"
#include "detail/UIStyleSheetStorage.hpp"
#include "detail/UIThemeTransitionResolver.hpp"
#include "detail/UIVirtualCollectionLayout.hpp"
#include "detail/UITextEditModel.hpp"
#include "detail/UITextEditPaintEmitter.hpp"
#include "detail/UITextStorage.hpp"
#include "detail/UIWidgetStateModels.hpp"
#include "detail/UIWidgetTraits.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <expected>
#include <initializer_list>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Tina::UI {
namespace {

using NodeStorageId = Core::GenerationId<Detail::UINodeRegistryTag>;

inline constexpr u32 InvalidNodeIndex = NodeStorageId::InvalidIndex;

using TextByteAllocation = Detail::UITextStorage::Allocation;
using Detail::appendBoxChromePaints;
using Detail::appendFlexLineItem;
using Detail::appendFlowMeasuredChild;
using Detail::applyOpacity;
using Detail::buildPointerRoutePath;
using Detail::BuiltinElementKind;
using Detail::clampHeight;
using Detail::clampWidth;
using Detail::combineVisibility;
using Detail::containsLineBreak;
using Detail::containsPointHalfOpen;
using Detail::countBoxChromePaintEntries;
using Detail::defaultBehaviorsForKind;
using Detail::defaultContentAlignment;
using Detail::defaultSemanticsForKind;
using Detail::defaultStyleRoleForKind;
using Detail::DropdownState;
using Detail::FlexLineSummary;
using Detail::hasLayoutWork;
using Detail::horizontalMargin;
using Detail::intersectRects;
using Detail::inspectPointerRouteTargets;
using Detail::isButtonChromeKind;
using Detail::isFiniteLayoutRect;
using Detail::isFiniteNonNegative;
using Detail::isValidFlexLineSummary;
using Detail::isValidContentAlignment;
using Detail::isValidEventPhaseMask;
using Detail::isValidFocusScopeMode;
using Detail::isValidPointerHitPolicy;
using Detail::isValidRoutedPointerEventKind;
using Detail::layoutSubtreeCompletionMask;
using Detail::LayoutFlowMeasurement;
using Detail::LayoutNodeMeasureContent;
using Detail::LayoutPassStatistics;
using Detail::LayoutPreparedInputs;
using Detail::LayoutScratchState;
using Detail::LayoutWorkArrange;
using Detail::LayoutWorkArrangeComplete;
using Detail::LayoutWorkMeasure;
using Detail::LayoutWorkMeasureComplete;
using Detail::ListViewItemState;
using Detail::ListViewLayoutScratch;
using Detail::ListViewState;
using Detail::makeListViewScrollBarGeometry;
using Detail::makeScrollBarGeometry;
using Detail::makeTreeViewDisclosureRect;
using Detail::makeTreeViewScrollBarGeometry;
using Detail::normalizedRangeFraction;
using Detail::normalizeBoxPaint;
using Detail::normalizeDropdownPaint;
using Detail::normalizeFloat;
using Detail::normalizeLayoutStyle;
using Detail::normalizeImageContent;
using Detail::normalizeListViewCreateConfig;
using Detail::normalizeListViewPaint;
using Detail::normalizeListViewStyle;
using Detail::normalizePopupStyle;
using Detail::normalizeScrollOffset;
using Detail::normalizeScrollViewPaint;
using Detail::normalizeScrollViewStyle;
using Detail::normalizeTreeViewCreateConfig;
using Detail::normalizeTreeViewPaint;
using Detail::normalizeTreeViewStyle;
using Detail::NormalizedUIContextCapacityConfig;
using Detail::makeNineSlicePatches;
using Detail::planTextEditCommand;
using Detail::quantizeSliderValue;
using Detail::ResolvedLength;
using Detail::resolveInset;
using Detail::resolveContentPlacement;
using Detail::resolveElementBuiltinKind;
using Detail::resolveFlexItemRect;
using Detail::resolveFlexLinePlan;
using Detail::resolveLength;
using Detail::resolveLengthNoFallbackCount;
using Detail::resolveMeasuredLayoutSize;
using Detail::resolveOverlayRect;
using Detail::resolvePopupPlacement;
using Detail::resolveScrollViewLayout;
using Detail::resolveScrollThumbOffset;
using Detail::resolveScrollTrackPageOffset;
using Detail::resolveScrollWheelOffset;
using Detail::resolveSliderValueFromPointer;
using Detail::resolveVirtualCollectionLayout;
using Detail::resolveVirtualRowScrollOffset;
using Detail::resolveVirtualScrollWheelOffset;
using Detail::resolvedHeight;
using Detail::resolvedWidth;
using Detail::scrollAxisMaxOffset;
using Detail::scrollAxisOffset;
using Detail::ScrollBarGeometry;
using Detail::ScrollBarPointerHit;
using Detail::ScrollViewLayoutScratch;
using Detail::ScrollViewLayoutInput;
using Detail::UIScrollBehaviorState;
using Detail::SliderPaintGeometry;
using Detail::sliderPaintGeometry;
using Detail::setScrollAxisOffset;
using Detail::textEditCodepointFromHorizontalPosition;
using Detail::TreeViewItemState;
using Detail::TreeViewLayoutScratch;
using Detail::TreeViewState;
using Detail::utf8ByteOffsetForCodepoint;
using Detail::validateSemanticsContract;
using Detail::verticalMargin;
using Detail::VirtualCollectionLayoutError;
using Detail::WidgetTextState;
using Detail::findHitEntryIndex;
using Detail::findFocusNavigationCandidate;
using Detail::hitEntryAllowedByModal;
using Detail::hitEntryIsWithinScope;
using Detail::isValidFocusNavigationDirection;
using Detail::phaseMaskFor;
using Detail::pointerHitTargetForEntry;
using Detail::PopupLayoutScratch;
using Detail::PopupState;
using Detail::ProgressBarState;
using Detail::RadioButtonState;
using Detail::supportsWidgetText;
using Detail::defaultThemeBindingsFor;
using Detail::isValidStyleRole;
using Detail::ProductChromeStorage;
using Detail::ThemeBindingBoxPaint;
using Detail::ThemeBindingButtonPaint;
using Detail::ThemeBindingCheckboxPaint;
using Detail::ThemeBindingDropdownPaint;
using Detail::ThemeBindingListViewPaint;
using Detail::ThemeBindingProgressBarPaint;
using Detail::ThemeBindingRadioButtonPaint;
using Detail::ThemeBindingScrollViewPaint;
using Detail::ThemeBindingSliderPaint;
using Detail::ThemeBindingTextEditPaint;
using Detail::ThemeBindingTextStyle;
using Detail::ThemeBindingTreeViewPaint;
using Detail::UIPointerRouteInspectionError;
using Detail::UIPointerRoutePathError;
using Detail::UINineSlicePatch;
using Detail::UINineSlicePatchBatch;

inline constexpr u8 ThemeDirtyPaint = 1U << 0U;
inline constexpr u8 ThemeDirtyLayoutSelf = 1U << 1U;
inline constexpr u8 ThemeDirtyLayoutAncestor = 1U << 2U;

[[nodiscard]] constexpr bool ownsDirectionalNavigation(BuiltinElementKind kind) noexcept
{
    return kind == BuiltinElementKind::TextEdit || kind == BuiltinElementKind::Dropdown ||
           kind == BuiltinElementKind::ListView || kind == BuiltinElementKind::TreeView;
}

[[nodiscard]] constexpr bool isCompositeFocusItem(BuiltinElementKind kind) noexcept
{
    return kind == BuiltinElementKind::DropdownItem || kind == BuiltinElementKind::ListViewItem ||
           kind == BuiltinElementKind::TreeViewItem;
}

struct NodeRecord final {
    u32 parentIndex = InvalidNodeIndex;
    u32 firstChildIndex = InvalidNodeIndex;
    u32 lastChildIndex = InvalidNodeIndex;
    u32 previousSiblingIndex = InvalidNodeIndex;
    u32 nextSiblingIndex = InvalidNodeIndex;
    u32 rootIndex = InvalidNodeIndex;
    u32 depth = 0;
    BuiltinElementKind kind = BuiltinElementKind::Panel;
    UIElementBehavior behaviors = UIElementBehavior::None;
};

struct SemanticsState final {
    TextByteAllocation textAllocation{};
    u32 nameLength = 0;
    u32 descriptionLength = 0;
    UISemanticsMode mode = UISemanticsMode::Automatic;
    UISemanticsRole role = UISemanticsRole::Group;
    UISemanticsAction actions = UISemanticsAction::None;
    bool hasExplicitName = false;
    bool useContentAsName = false;
    bool readOnly = false;
};

static_assert(sizeof(NodeRecord) <= 48);
static_assert(std::is_nothrow_destructible_v<NodeRecord>);

using NodePool = Core::GenerationPool<NodeRecord, Detail::UINodeRegistryTag>;

struct CommittedHitBuildResult final {
    usize targetCount = 0;
    u32 activeModalEntryIndex = InvalidUIHitEntryIndex;
};

[[nodiscard]] Core::Error makeError(Core::ErrorCode code, std::string_view message,
                                    Core::SourceLocation location = Core::SourceLocation::current())
{
    return Core::Error{code, message, location};
}

[[nodiscard]] std::unexpected<Core::Error> fail(Core::ErrorCode code, std::string_view message,
                                                Core::SourceLocation location = Core::SourceLocation::current())
{
    return Core::failure(makeError(code, message, location));
}

[[nodiscard]] constexpr Detail::UIBehaviorStateSlotCounts
toBehaviorSlotCounts(UIBehaviorSlotBudget budget) noexcept
{
    return {
        .activate = budget.activate,
        .toggle = budget.toggle,
        .range = budget.range,
        .textInput = budget.textInput,
        .scroll = budget.scroll,
        .selection = budget.selection,
    };
}

[[nodiscard]] constexpr UIBehaviorSlotBudget
toBehaviorSlotBudget(Detail::UIBehaviorStateSlotCounts counts) noexcept
{
    return {
        .activate = counts.activate,
        .toggle = counts.toggle,
        .range = counts.range,
        .textInput = counts.textInput,
        .scroll = counts.scroll,
        .selection = counts.selection,
    };
}

[[nodiscard]] constexpr bool containsBudget(UIBehaviorSlotBudget available,
                                            UIBehaviorSlotBudget required) noexcept
{
    return required.activate <= available.activate && required.toggle <= available.toggle &&
           required.range <= available.range && required.textInput <= available.textInput &&
           required.scroll <= available.scroll && required.selection <= available.selection;
}

[[nodiscard]] constexpr bool containsBudget(UIComponentBuildBudget available,
                                            UIComponentBuildBudget required) noexcept
{
    return required.nodes <= available.nodes && required.textBytes <= available.textBytes &&
           required.canvasCommands <= available.canvasCommands &&
           containsBudget(available.behaviors, required.behaviors);
}

[[nodiscard]] constexpr UIComponentBuildPoolStatistics
makePoolStatistics(usize requested, usize reserved, usize published,
                   usize capacityFailures, usize outstandingReservations) noexcept
{
    return {
        .requested = requested,
        .reserved = reserved,
        .published = published,
        .capacityFailures = capacityFailures,
        .outstandingReservations = outstandingReservations,
    };
}

} // namespace

struct UIComponentBuildReservation final {
    UINodeId componentRoot{};
    UIComponentBuildBudget remaining{};
    Detail::UITextStorage::Reservation text{};
    Detail::UICanvasCommandStorage::Reservation canvas{};
    Detail::UIBehaviorStateSlotCounts behaviors{};
    bool active = false;
};

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

    Platform::WindowId ownerWindow{};
    UIContextCapacityConfig capacityConfig{};
    std::thread::id ownerThreadId{};
    // Active product theme used when create* installs default control chrome.
    // Not a global singleton; one copy per UIContext. Local set*Paint overrides
    // remain on the node after create.
    UITheme productTheme = makeDefaultProductTheme();
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
    std::pmr::vector<UIStyleRoleId> styleRolesByNodeIndex;
    Detail::UIStyleSheetStorage styleSheetStorage;
    std::pmr::vector<std::array<UIStyleClassId, Detail::MaxStyleClassesPerNode>>
        styleClassesByNodeIndex;
    std::pmr::vector<u8> styleClassCountsByNodeIndex;
    std::pmr::vector<UIStyleState> styleStatesByNodeIndex;
    std::pmr::vector<UIPremultipliedRgba8Color> resolvedBoxFillCacheByNodeIndex;
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
    std::pmr::vector<ProgressBarState> progressBarStatesByNodeIndex;
    std::pmr::vector<RadioButtonState> radioButtonStatesByNodeIndex;
    // Scroll style/offset/metrics live in the behavior side store; chrome remains kind-owned.
    std::pmr::vector<UIScrollViewPaint> scrollViewPaintsByNodeIndex;
    std::pmr::vector<ScrollViewLayoutScratch> scrollViewLayoutScratchByNodeIndex;
    // Select ownership lives in the behavior side store; popup linkage and chrome remain kind-owned.
    std::pmr::vector<DropdownState> dropdownStatesByNodeIndex;
    std::pmr::vector<PopupState> popupStatesByNodeIndex;
    std::pmr::vector<PopupLayoutScratch> popupLayoutScratchByNodeIndex;
    std::pmr::vector<ListViewState> listViewStatesByNodeIndex;
    std::pmr::vector<ListViewLayoutScratch> listViewLayoutScratchByNodeIndex;
    std::pmr::vector<ListViewItemState> listViewItemStatesByNodeIndex;
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
    usize routeDispatchDepth = 0;
    u64 buttonRouteSerial = 0;
    u64 accessibilityActionSequence = 0;
    UINodeId armedPrimaryButton{};
    bool armedPrimaryButtonPressed = false;
    UINodeId hoveredPrimaryControl{};
    // M11-C1: exclusive Primary drag capture for Slider (clears Button arm).
    UINodeId armedSlider{};
    UINodeId armedScrollView{};
    UIScrollAxes armedScrollAxis = UIScrollAxes::None;
    float scrollDragGrabOffset = 0.0F;
    bool scrollThumbDragActive = false;
    UINodeId armedTextEdit{};
    UINodeId capturedPointerNode{};
    UIPointerInputEvent lastPointerInput{};
    bool hasLastPointerInput = false;
    bool pointerCancelDispatchInProgress = false;
    UINodeId activeModalNode{};
    UINodeId activePopupNode{};
    Detail::UICommandPressLatch<UIDropdownCommand, UIDropdownCommand::ExitNext>
        dropdownCommandPressLatch;
    Detail::UICommandPressLatch<UIListViewCommand, UIListViewCommand::Activate>
        listViewCommandPressLatch;
    Detail::UICommandPressLatch<UITreeViewCommand, UITreeViewCommand::Activate>
        treeViewCommandPressLatch;
    Detail::UICommandPressLatch<UIFocusNavigationDirection, UIFocusNavigationDirection::Down>
        focusNavigationPressLatch;
    bool armedTreeDisclosure = false;
    bool popupDismissPointerBarrierActive = false;
    UINodeId pendingDestroyedModalRestoreFocus{};
    bool hasPendingDestroyedModalRestoreFocus = false;
    // Last Button that received Primary Pointer arm. Keyboard/Gamepad Accept
    // activates this node without requiring a live pointer press.
    UINodeId defaultActionFocusButton{};
    // Focused single-line editor that receives keyboard and IME input.
    UINodeId textInputFocus{};
    Detail::UIImeCompositionState imeComposition;

    Impl(Platform::WindowId owner, UIContextCapacityConfig capacities, std::thread::id threadId,
         std::shared_ptr<Detail::UIContextLifetimeControl> lifetimeControl, NodePool&& nodePool,
         std::pmr::memory_resource& resource)
        : ownerWindow(owner), capacityConfig(capacities), ownerThreadId(threadId), lifetime(std::move(lifetimeControl)),
          nodes(std::move(nodePool)), idsByIndex(&resource), layoutStylesByIndex(&resource),
          pointerHitPoliciesByIndex(&resource), enabledByNodeIndex(&resource), focusScopeModesByNodeIndex(&resource),
          focusRestoreByNodeIndex(&resource), styleRolesByNodeIndex(&resource),
          styleSheetStorage({
                                .classCapacity = capacities.styleClassCapacity,
                                .tokenCapacity = capacities.styleTokenCapacity,
                                .ruleCapacity = capacities.styleRuleCapacity,
                                .bucketCapacity = capacities.styleBucketCapacity,
                                .maxRulesPerBucket = capacities.styleRulesPerBucketCapacity,
                            },
                            resource),
          styleClassesByNodeIndex(&resource), styleClassCountsByNodeIndex(&resource),
          styleStatesByNodeIndex(&resource), resolvedBoxFillCacheByNodeIndex(&resource),
          boxPaintsByIndex(&resource),
          buttonPaintsByNodeIndex(&resource),
          themeBindingsByNodeIndex(&resource), styleOverridesByNodeIndex(&resource), themeDirtyScratchByNodeIndex(&resource),
          themeTextMetricsScratchByNodeIndex(&resource), localSolidFillCacheByIndex(&resource),
          localTextColorCacheByIndex(&resource), textStatesByIndex(&resource), semanticsStatesByNodeIndex(&resource),
          paintSnapshotBuilder(capacities.paintSnapshotCapacity),
          semanticsSnapshotBuilder(capacities.nodeCapacity, capacities.nodeCapacity, resource),
          canvasCommandStorage(capacities.nodeCapacity, capacities.canvasCommandCapacity, resource),
          imageContentStorage(capacities.nodeCapacity, capacities.imageContentCapacity, resource),
          textEditPaintsByNodeIndex(&resource),
          progressBarStatesByNodeIndex(&resource), radioButtonStatesByNodeIndex(&resource),
          scrollViewPaintsByNodeIndex(&resource), scrollViewLayoutScratchByNodeIndex(&resource),
          dropdownStatesByNodeIndex(&resource), popupStatesByNodeIndex(&resource),
          popupLayoutScratchByNodeIndex(&resource), listViewStatesByNodeIndex(&resource),
          listViewLayoutScratchByNodeIndex(&resource), listViewItemStatesByNodeIndex(&resource),
          treeViewStatesByNodeIndex(&resource), treeViewLayoutScratchByNodeIndex(&resource),
          treeViewItemStatesByNodeIndex(&resource),
          textStorage(capacities.textByteCapacity, capacities.nodeCapacity * 2U, resource),
          dirtyQueueStorage(capacities.nodeCapacity, capacities.dirtyQueueCapacity, resource),
          layoutScratchByIndex(&resource),
          layoutWorkByIndex(&resource), layoutOrderScratch(&resource), hitEntryIndexByNodeIndex(&resource),
          routedPointerListenerRegistry(capacities.nodeCapacity, capacities.routedPointerListenerCapacity, resource),
          routePathScratch(&resource), pointerCancelRoutePathScratch(&resource),
          buttonActionRegistry(capacities.nodeCapacity, capacities.buttonActionCapacity, resource),
          behaviorStateStorage(capacities.nodeCapacity, capacities.nodeCapacity,
                               capacities.nodeCapacity, capacities.nodeCapacity, capacities.nodeCapacity,
                               capacities.nodeCapacity, capacities.nodeCapacity, resource),
          defaultActionPressState(owner), rangeInputPressLatch(owner),
          checkboxPaintsByNodeIndex(&resource),
          sliderPaintsByNodeIndex(&resource), sliderChangeCallbackRegistry(capacities.nodeCapacity, resource),
          committedBuffers{std::pmr::vector<UICommittedNodeEntry>(&resource),
                                                                   std::pmr::vector<UICommittedNodeEntry>(&resource)},
          committedLayoutBuffers{std::pmr::vector<UICommittedLayoutEntry>(&resource),
                                 std::pmr::vector<UICommittedLayoutEntry>(&resource)},
          committedHitBuffers{std::pmr::vector<UICommittedHitEntry>(&resource),
                              std::pmr::vector<UICommittedHitEntry>(&resource)},
          committedPaintBuffers{std::pmr::vector<UICommittedPaintEntry>(&resource),
                                std::pmr::vector<UICommittedPaintEntry>(&resource)},
          committedSemanticsBuffers{std::pmr::vector<UISemanticsEntry>(&resource),
                                    std::pmr::vector<UISemanticsEntry>(&resource)},
          committedSemanticsTextBuffers{std::pmr::vector<char>(&resource), std::pmr::vector<char>(&resource)},
          componentBuildReservationsByNodeIndex(&resource)
    {
    }

    [[nodiscard]] static Core::Result<std::unique_ptr<Impl>>
    Create(Platform::WindowId ownerWindow, NormalizedUIContextCapacityConfig normalized,
           std::shared_ptr<Detail::UIContextLifetimeControl> lifetimeControl, std::pmr::memory_resource& resource)
    {
        auto poolResult = NodePool::Create(normalized.nodeCapacity, resource);
        if (!poolResult)
        {
            const Core::Error& error = poolResult.error();
            if (error.code == Core::CoreErrorCode::CapacityExceeded)
            {
                return fail(UIErrorCode::CapacityExceeded, "UI node pool capacity could not be reserved");
            }
            return Core::failure(error);
        }

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
            .routedPointerListenerCapacity = normalized.routedPointerListenerCapacity,
            .buttonActionCapacity = normalized.buttonActionCapacity,
            .textByteCapacity = normalized.textByteCapacity,
            .styleClassCapacity = normalized.styleClassCapacity,
            .styleTokenCapacity = normalized.styleTokenCapacity,
            .styleRuleCapacity = normalized.styleRuleCapacity,
            .styleBucketCapacity = normalized.styleBucketCapacity,
            .styleRulesPerBucketCapacity = normalized.styleRulesPerBucketCapacity,
            .nodeStyleClassLinkCapacity = normalized.nodeStyleClassLinkCapacity,
            .applyDefaultProductChrome = normalized.applyDefaultProductChrome,
        };

        auto impl = std::unique_ptr<Impl>(new Impl(ownerWindow, capacities, std::this_thread::get_id(),
                                                   std::move(lifetimeControl), std::move(*poolResult), resource));
        impl->idsByIndex.resize(normalized.nodeCapacity);
        impl->layoutStylesByIndex.resize(normalized.nodeCapacity);
        impl->pointerHitPoliciesByIndex.resize(normalized.nodeCapacity, UIPointerHitPolicy::Ignore);
        impl->enabledByNodeIndex.resize(normalized.nodeCapacity, 1);
        impl->focusScopeModesByNodeIndex.resize(normalized.nodeCapacity, UIFocusScopeMode::None);
        impl->focusRestoreByNodeIndex.resize(normalized.nodeCapacity);
        impl->styleRolesByNodeIndex.resize(normalized.nodeCapacity, UIStyleRoleId::None);
        impl->styleClassesByNodeIndex.resize(normalized.nodeCapacity);
        impl->styleClassCountsByNodeIndex.resize(normalized.nodeCapacity, 0);
        impl->styleStatesByNodeIndex.resize(normalized.nodeCapacity, UIStyleState::None);
        impl->resolvedBoxFillCacheByNodeIndex.resize(normalized.nodeCapacity);
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
        impl->committedBuffers[0].reserve(normalized.nodeCapacity);
        impl->committedBuffers[1].reserve(normalized.nodeCapacity);
        impl->committedLayoutBuffers[0].reserve(normalized.layoutSnapshotCapacity);
        impl->committedLayoutBuffers[1].reserve(normalized.layoutSnapshotCapacity);
        impl->committedHitBuffers[0].reserve(normalized.hitSnapshotCapacity);
        impl->committedHitBuffers[1].reserve(normalized.hitSnapshotCapacity);
        impl->committedPaintBuffers[0].reserve(normalized.paintSnapshotCapacity);
        impl->committedPaintBuffers[1].reserve(normalized.paintSnapshotCapacity);
        impl->committedSemanticsBuffers[0].reserve(normalized.nodeCapacity);
        impl->committedSemanticsBuffers[1].reserve(normalized.nodeCapacity);
        impl->committedSemanticsTextBuffers[0].resize(normalized.textByteCapacity, '\0');
        impl->committedSemanticsTextBuffers[1].resize(normalized.textByteCapacity, '\0');
        impl->deferredRootDestroyBuffer.reserve(normalized.rootCapacity);
        impl->deferredRoutedPointerListenerReleaseBuffer.reserve(normalized.routedPointerListenerCapacity);
        return impl;
    }

    void detachLifetime(UIContext* context) noexcept
    {
        if (lifetime && context != nullptr)
        {
            lifetime->detach(*context);
        }
    }

    void buildCommittedStructure(std::pmr::vector<UICommittedNodeEntry>& output) const noexcept
    {
        output.clear();
        u32 ordinal = 0;
        u32 rootIndex = firstRootIndex;
        while (rootIndex != InvalidNodeIndex)
        {
            const NodeRecord* root = recordByIndex(rootIndex);
            const u32 nextRootIndex = root == nullptr ? InvalidNodeIndex : root->nextSiblingIndex;
            appendCommittedTree(rootIndex, ordinal, output);
            rootIndex = nextRootIndex;
        }
    }

    void appendLayoutOrderTree(u32 index, std::pmr::vector<u32>& output) const noexcept
    {
        const u32 rootIndex = index;
        u32 currentIndex = rootIndex;
        while (currentIndex != InvalidNodeIndex)
        {
            const NodeRecord* record = recordByIndex(currentIndex);
            if (record == nullptr)
            {
                return;
            }

            output.push_back(currentIndex);

            if (record->firstChildIndex != InvalidNodeIndex)
            {
                currentIndex = record->firstChildIndex;
                continue;
            }

            while (currentIndex != rootIndex)
            {
                record = recordByIndex(currentIndex);
                if (record == nullptr)
                {
                    return;
                }
                if (record->nextSiblingIndex != InvalidNodeIndex)
                {
                    currentIndex = record->nextSiblingIndex;
                    break;
                }
                currentIndex = record->parentIndex;
            }
            if (currentIndex == rootIndex)
            {
                currentIndex = InvalidNodeIndex;
            }
        }
    }

    void buildLayoutOrder(std::pmr::vector<u32>& output) const noexcept
    {
        output.clear();
        u32 rootIndex = firstRootIndex;
        while (rootIndex != InvalidNodeIndex)
        {
            const NodeRecord* root = recordByIndex(rootIndex);
            const u32 nextRootIndex = root == nullptr ? InvalidNodeIndex : root->nextSiblingIndex;
            appendLayoutOrderTree(rootIndex, output);
            rootIndex = nextRootIndex;
        }
    }

    void markLayoutSubtreeWork(u32 rootIndex, u8 work) noexcept
    {
        const u8 completion = layoutSubtreeCompletionMask(work);
        u32 currentIndex = rootIndex;
        while (currentIndex != InvalidNodeIndex)
        {
            const NodeRecord* record = recordByIndex(currentIndex);
            if (record == nullptr)
            {
                return;
            }
            layoutWorkByIndex[currentIndex] |= work | completion;

            if (record->firstChildIndex != InvalidNodeIndex)
            {
                currentIndex = record->firstChildIndex;
                continue;
            }

            while (currentIndex != rootIndex)
            {
                record = recordByIndex(currentIndex);
                if (record == nullptr)
                {
                    return;
                }
                if (record->nextSiblingIndex != InvalidNodeIndex)
                {
                    currentIndex = record->nextSiblingIndex;
                    break;
                }
                currentIndex = record->parentIndex;
            }
            if (currentIndex == rootIndex)
            {
                currentIndex = InvalidNodeIndex;
            }
        }
    }

    void ensureLayoutSubtreeWork(u32 rootIndex, u8 work) noexcept
    {
        if (rootIndex >= layoutWorkByIndex.size())
        {
            return;
        }
        const u8 requiredCompletion = layoutSubtreeCompletionMask(work);
        if ((layoutWorkByIndex[rootIndex] & requiredCompletion) == requiredCompletion)
        {
            return;
        }
        markLayoutSubtreeWork(rootIndex, work);
    }

    void markLayoutAncestorsWork(u32 nodeIndex, u8 work) noexcept
    {
        u32 currentIndex = nodeIndex;
        while (currentIndex != InvalidNodeIndex)
        {
            layoutWorkByIndex[currentIndex] |= work;
            const NodeRecord* record = recordByIndex(currentIndex);
            if (record == nullptr)
            {
                return;
            }
            currentIndex = record->parentIndex;
        }
    }

    void initializeLayoutWork(const std::pmr::vector<u32>& order, bool allowReuse) noexcept
    {
        for (const u32 index : order)
        {
            layoutWorkByIndex[index] = 0;
        }

        if (!allowReuse)
        {
            for (const u32 index : order)
            {
                layoutWorkByIndex[index] = LayoutWorkMeasure | LayoutWorkArrange;
            }
            return;
        }

        for (const u32 index : order)
        {
            const UIDirty dirty = dirtyQueueStorage.flags(index);
            if (hasDirty(dirty, UIDirty::Measure))
            {
                layoutWorkByIndex[index] |= LayoutWorkMeasure | LayoutWorkArrange;
            } else if (hasDirty(dirty, UIDirty::Arrange))
            {
                layoutWorkByIndex[index] |= LayoutWorkArrange;
            }
            if (hasDirty(dirty, UIDirty::Style))
            {
                // A direct style change can alter the containing basis or
                // effective visibility of any descendant.
                ensureLayoutSubtreeWork(index, LayoutWorkMeasure | LayoutWorkArrange);
                markLayoutAncestorsWork(index, LayoutWorkArrange);
            }
        }
    }

    void prepareLayoutState(UILogicalSize viewportSize, const std::pmr::vector<u32>& order, bool allowReuse) noexcept
    {
        initializeLayoutWork(order, allowReuse);
        for (const u32 index : order)
        {
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr)
            {
                continue;
            }
            const UILayoutStyle& style = layoutStylesByIndex[index];
            UIVisibility ownVisibility = style.visibility;
            if (record->kind == BuiltinElementKind::Popup && index < popupStatesByNodeIndex.size() &&
                !popupStatesByNodeIndex[index].open)
            {
                ownVisibility = UIVisibility::Collapsed;
            }
            LayoutScratchState& scratch = layoutScratchByIndex[index];
            const LayoutPreparedInputs previous = scratch.preparedInputs;
            if (!allowReuse)
            {
                scratch = {};
            }

            if (record->parentIndex == InvalidNodeIndex)
            {
                scratch.effectiveVisibility = ownVisibility;
                scratch.inPopupSubtree = record->kind == BuiltinElementKind::Popup;
                scratch.parentContentWidthDefinite = true;
                scratch.parentContentHeightDefinite = true;
                scratch.parentContentWidth = viewportSize.width;
                scratch.parentContentHeight = viewportSize.height;
            } else
            {
                const LayoutScratchState& parentScratch = layoutScratchByIndex[record->parentIndex];
                scratch.effectiveVisibility = combineVisibility(parentScratch.effectiveVisibility, ownVisibility);
                scratch.inPopupSubtree =
                    record->kind == BuiltinElementKind::Popup || parentScratch.inPopupSubtree;
                scratch.parentContentWidthDefinite = parentScratch.contentWidthDefinite;
                scratch.parentContentHeightDefinite = parentScratch.contentHeightDefinite;
                scratch.parentContentWidth = parentScratch.contentWidth;
                scratch.parentContentHeight = parentScratch.contentHeight;
            }

            const ResolvedLength width = resolveLengthNoFallbackCount(
                style.size.width, scratch.parentContentWidthDefinite, scratch.parentContentWidth);
            const ResolvedLength height = resolveLengthNoFallbackCount(
                style.size.height, scratch.parentContentHeightDefinite, scratch.parentContentHeight);
            const bool isRoot = record->parentIndex == InvalidNodeIndex;
            scratch.contentWidthDefinite = width.hasValue || isRoot;
            scratch.contentHeightDefinite = height.hasValue || isRoot;
            const float outerWidth = width.hasValue ? width.value : viewportSize.width;
            const float outerHeight = height.hasValue ? height.value : viewportSize.height;
            scratch.contentWidth =
                scratch.contentWidthDefinite ? (std::max)(0.0F, outerWidth - horizontalMargin(style.padding)) : 0.0F;
            scratch.contentHeight =
                scratch.contentHeightDefinite ? (std::max)(0.0F, outerHeight - verticalMargin(style.padding)) : 0.0F;

            const LayoutPreparedInputs currentInputs{
                .effectiveVisibility = scratch.effectiveVisibility,
                .parentContentWidthDefinite = scratch.parentContentWidthDefinite,
                .parentContentHeightDefinite = scratch.parentContentHeightDefinite,
                .parentContentWidth = scratch.parentContentWidth,
                .parentContentHeight = scratch.parentContentHeight,
                .contentWidthDefinite = scratch.contentWidthDefinite,
                .contentHeightDefinite = scratch.contentHeightDefinite,
                .contentWidth = scratch.contentWidth,
                .contentHeight = scratch.contentHeight,
            };
            scratch.preparedInputs = currentInputs;

            if (allowReuse && previous != currentInputs)
            {
                // Parent constraint or effective visibility changes can
                // invalidate every descendant even when only an ancestor was
                // explicitly queued dirty.
                ensureLayoutSubtreeWork(index, LayoutWorkMeasure | LayoutWorkArrange);
                markLayoutAncestorsWork(index, LayoutWorkArrange);
            }
        }
    }

    void measureLayout(UILogicalSize viewportSize, const std::pmr::vector<u32>& order,
                       LayoutPassStatistics& statistics) noexcept
    {
        for (usize reverseIndex = order.size(); reverseIndex > 0; --reverseIndex)
        {
            const u32 index = order[reverseIndex - 1];
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr)
            {
                continue;
            }
            if (!hasLayoutWork(layoutWorkByIndex[index], LayoutWorkMeasure))
            {
                continue;
            }
            const UILayoutStyle& style = layoutStylesByIndex[index];
            LayoutScratchState& scratch = layoutScratchByIndex[index];
            const UILogicalSize previousMeasuredSize = scratch.measuredSize;
            ++statistics.measuredNodeCount;

            if (scratch.effectiveVisibility == UIVisibility::Collapsed)
            {
                scratch.measuredSize = {};
                if (scratch.measuredSize != previousMeasuredSize)
                {
                    ensureLayoutSubtreeWork(index, LayoutWorkArrange);
                }
                continue;
            }

            LayoutFlowMeasurement flowMeasurement{};

            u32 childIndex = record->firstChildIndex;
            while (childIndex != InvalidNodeIndex)
            {
                const NodeRecord* childRecord = recordByIndex(childIndex);
                if (childRecord == nullptr)
                {
                    break;
                }
                const UILayoutStyle& childStyle = layoutStylesByIndex[childIndex];
                const LayoutScratchState& childScratch = layoutScratchByIndex[childIndex];
                if (childScratch.effectiveVisibility != UIVisibility::Collapsed &&
                    childStyle.placement == UILayoutPlacement::Flow)
                {
                    const float gap = style.flexContainer.direction == UIFlexDirection::Row
                                          ? style.flexContainer.gap.column
                                          : style.flexContainer.gap.row;
                    appendFlowMeasuredChild(
                        flowMeasurement,
                        style.flexContainer.direction,
                        gap,
                        childStyle,
                        childScratch.measuredSize);
                }
                childIndex = childRecord->nextSiblingIndex;
            }

            LayoutNodeMeasureContent content{.size = flowMeasurement.contentSize};
            if (flowMeasurement.childCount == 0)
            {
                if (const UIImageContent* image = imageContentStorage.get(index); image != nullptr)
                {
                    content.size = image->source.intrinsicLogicalSize;
                }
            }
            if (flowMeasurement.childCount == 0 && supportsWidgetText(record->kind))
            {
                if (const UITextMetrics* metrics = presentationTextMetricsFor(index); metrics != nullptr)
                {
                    const UILogicalSize textSize = metrics->measuredSize;
                    content.size = textSize;
                    if (record->kind == BuiltinElementKind::RadioButton && index < radioButtonStatesByNodeIndex.size())
                    {
                        content.indicatorLabelWidth = textSize.width;
                        content.hasIndicatorLabel = true;
                    }
                }
            }
            if (flowMeasurement.childCount == 0 && record->kind == BuiltinElementKind::Dropdown &&
                index < dropdownStatesByNodeIndex.size())
            {
                const UIDropdownPaint& dropdownPaint = dropdownStatesByNodeIndex[index].paint;
                content.size.width += dropdownPaint.indicatorWidth + dropdownPaint.indicatorInset * 2.0F;
                content.size.height = (std::max)(content.size.height, dropdownPaint.indicatorHeight);
            }

            if (flowMeasurement.childCount == 0 && record->kind == BuiltinElementKind::RadioButton &&
                index < radioButtonStatesByNodeIndex.size())
            {
                content.squareLeadingIndicator = true;
                content.indicatorLabelGap = radioButtonStatesByNodeIndex[index].paint.labelGap;
                if (!content.hasIndicatorLabel && index < textStatesByIndex.size())
                {
                    const UITextStyle& textStyle = textStatesByIndex[index].style;
                    const float defaultIndicatorExtent = textStyle.logicalSize * textStyle.lineHeightScale;
                    if (std::isfinite(defaultIndicatorExtent) && defaultIndicatorExtent > 0.0F)
                    {
                        content.size.height = defaultIndicatorExtent;
                    }
                }
            }

            scratch.measuredSize = resolveMeasuredLayoutSize(
                style,
                scratch,
                viewportSize,
                record->parentIndex == InvalidNodeIndex,
                content,
                statistics);
            if (scratch.measuredSize != previousMeasuredSize)
            {
                ensureLayoutSubtreeWork(index, LayoutWorkArrange);
            }
        }
    }

    void assignLayoutRect(u32 index, UILogicalRect worldRect, UILogicalRect parentWorldRect,
                          UILogicalRect descendantClip) noexcept
    {
        LayoutScratchState& scratch = layoutScratchByIndex[index];
        const UILayoutStyle& style = layoutStylesByIndex[index];
        const UILogicalRect previousWorldRect = scratch.worldRect;
        const UILogicalRect previousLocalRect = scratch.localRect;
        const UILogicalRect previousEffectiveClip = scratch.effectiveClip;
        const UILogicalRect previousDescendantClip = scratch.descendantClip;
        const UIVisibility previousVisibility = scratch.effectiveVisibility;
        if (scratch.effectiveVisibility == UIVisibility::Collapsed)
        {
            worldRect.width = 0.0F;
            worldRect.height = 0.0F;
        }
        scratch.worldRect = worldRect;
        scratch.localRect = UILogicalRect{
            .x = normalizeFloat(worldRect.x - parentWorldRect.x),
            .y = normalizeFloat(worldRect.y - parentWorldRect.y),
            .width = normalizeFloat(worldRect.width),
            .height = normalizeFloat(worldRect.height),
        };
        scratch.descendantClip = descendantClip;
        scratch.effectiveClip = scratch.effectiveVisibility == UIVisibility::Collapsed
                                    ? UILogicalRect{}
                                    : intersectRects(descendantClip, worldRect);
        scratch.contentWidthDefinite = true;
        scratch.contentHeightDefinite = true;
        scratch.contentWidth = normalizeFloat((std::max)(0.0F, worldRect.width - horizontalMargin(style.padding)));
        scratch.contentHeight = normalizeFloat((std::max)(0.0F, worldRect.height - verticalMargin(style.padding)));
        if (layoutReuseInProgress &&
            (previousWorldRect != scratch.worldRect || previousLocalRect != scratch.localRect ||
             previousEffectiveClip != scratch.effectiveClip || previousDescendantClip != scratch.descendantClip ||
             previousVisibility != scratch.effectiveVisibility))
        {
            ensureLayoutSubtreeWork(index, LayoutWorkArrange);
        }
    }

    void refreshMeasuredSizeForParentContent(u32 childIndex, UILogicalRect parentContentRect,
                                             LayoutPassStatistics& statistics) noexcept
    {
        const UILayoutStyle& childStyle = layoutStylesByIndex[childIndex];
        LayoutScratchState& childScratch = layoutScratchByIndex[childIndex];
        const UILogicalSize previousMeasuredSize = childScratch.measuredSize;
        childScratch.parentContentWidthDefinite = true;
        childScratch.parentContentHeightDefinite = true;
        childScratch.parentContentWidth = parentContentRect.width;
        childScratch.parentContentHeight = parentContentRect.height;

        const float resolvedOuterWidth = resolvedWidth(childStyle, childScratch, statistics);
        const float resolvedOuterHeight = resolvedHeight(childStyle, childScratch, statistics);
        const float outerWidth = resolvedOuterWidth >= 0.0F ? resolvedOuterWidth : childScratch.measuredSize.width;
        const float outerHeight = resolvedOuterHeight >= 0.0F ? resolvedOuterHeight : childScratch.measuredSize.height;
        childScratch.measuredSize = UILogicalSize{
            .width = clampWidth(outerWidth, childStyle, childScratch, statistics),
            .height = clampHeight(outerHeight, childStyle, childScratch, statistics),
        };
        if (layoutReuseInProgress && childScratch.measuredSize != previousMeasuredSize)
        {
            ensureLayoutSubtreeWork(childIndex, LayoutWorkArrange);
        }
    }

    void arrangeOverlayChild(u32 childIndex, UILogicalRect parentContentRect, UILogicalRect parentWorldRect,
                             UILogicalRect descendantClip, LayoutPassStatistics& statistics) noexcept
    {
        refreshMeasuredSizeForParentContent(childIndex, parentContentRect, statistics);
        assignLayoutRect(childIndex,
                         resolveOverlayRect(layoutStylesByIndex[childIndex],
                                            layoutScratchByIndex[childIndex],
                                            parentContentRect, statistics),
                         parentWorldRect, descendantClip);
    }

    void arrangePopupChild(u32 popupIndex, UILogicalRect anchorRect, UILogicalRect viewportRect,
                           LayoutPassStatistics& statistics) noexcept
    {
        const UILayoutStyle& popupLayoutStyle = layoutStylesByIndex[popupIndex];
        LayoutScratchState& popupScratch = layoutScratchByIndex[popupIndex];
        PopupState& popup = popupStatesByNodeIndex[popupIndex];
        refreshMeasuredSizeForParentContent(popupIndex, viewportRect, statistics);
        const auto resolved = resolvePopupPlacement(
            popupLayoutStyle, popupScratch, popup.style, anchorRect,
            viewportRect, statistics);
        assignLayoutRect(popupIndex, resolved.rect, anchorRect, viewportRect);
        popupLayoutScratchByNodeIndex[popupIndex].metrics = UIPopupMetrics{
            .anchorRect = anchorRect,
            .popupRect = resolved.rect,
            .resolvedPlacement = resolved.placement,
            .open = popupScratch.effectiveVisibility == UIVisibility::Visible,
        };
    }

    [[nodiscard]] Core::Status bindListViewItem(u32 itemIndex, u64 logicalIndex,
                                                const UIListViewItemDescriptor& descriptor)
    {
        if (itemIndex >= textStatesByIndex.size() || itemIndex >= listViewItemStatesByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI ListView item side state is out of range");
        }
        if (descriptor.key == InvalidUIListViewItemKey || containsLineBreak(descriptor.label))
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI ListView item requires a non-zero key and a single-line label");
        }
        if (descriptor.label.size() > (std::numeric_limits<u32>::max)())
        {
            return fail(UIErrorCode::CapacityExceeded, "UI ListView item label is too large");
        }

        WidgetTextState& text = textStatesByIndex[itemIndex];
        auto metrics = measureWidgetText(descriptor.label, text.style);
        if (!metrics)
        {
            return Core::failure(metrics.error());
        }

        TextByteAllocation replacement{};
        bool replaceAllocation = false;
        if (!descriptor.label.empty() && text.allocation.capacity < descriptor.label.size())
        {
            auto allocation = textStorage.allocate(static_cast<u32>(descriptor.label.size()));
            if (!allocation)
            {
                return Core::failure(allocation.error());
            }
            replacement = *allocation;
            replaceAllocation = true;
        }
        if (descriptor.label.empty())
        {
            textStorage.release(text.allocation);
            text.allocation = {};
            text.length = 0;
            text.metrics = {};
            text.hasContent = false;
        } else
        {
            if (replaceAllocation)
            {
                textStorage.release(text.allocation);
                text.allocation = replacement;
            }
            textStorage.write(text.allocation, descriptor.label);
            text.length = static_cast<u32>(descriptor.label.size());
            text.metrics = *metrics;
            text.hasContent = true;
        }
        localTextColorCacheByIndex[itemIndex] = text.hasContent ? premultiply(text.style.color)
                                                               : UIPremultipliedRgba8Color{};
        ListViewItemState& item = listViewItemStatesByNodeIndex[itemIndex];
        item.key = descriptor.key;
        item.logicalIndex = logicalIndex;
        item.bound = true;
        item.enabled = descriptor.enabled;
        return Core::success();
    }

    void collapseListViewItems(u32 listViewIndex, UILogicalRect contentRect, UILogicalRect parentWorldRect,
                               UILogicalRect descendantClip) noexcept
    {
        const NodeRecord* listRecord = recordByIndex(listViewIndex);
        u32 childIndex = listRecord == nullptr ? InvalidNodeIndex : listRecord->firstChildIndex;
        while (childIndex != InvalidNodeIndex)
        {
            const NodeRecord* childRecord = recordByIndex(childIndex);
            if (childRecord == nullptr)
            {
                break;
            }
            ListViewItemState& item = listViewItemStatesByNodeIndex[childIndex];
            item.key = InvalidUIListViewItemKey;
            item.logicalIndex = 0;
            item.bound = false;
            item.enabled = true;
            layoutScratchByIndex[childIndex].effectiveVisibility = UIVisibility::Collapsed;
            assignLayoutRect(childIndex, contentRect, parentWorldRect, descendantClip);
            childIndex = childRecord->nextSiblingIndex;
        }
    }

    [[nodiscard]] Core::Status arrangeListViewItems(u32 listViewIndex, UILogicalRect unscrolledContentRect,
                                                    UILogicalRect parentWorldRect, UILogicalRect descendantClip)
    {
        ListViewState& state = listViewStatesByNodeIndex[listViewIndex];
        const u64 logicalItemCount = state.dataSource.hasValue() ? state.dataSource.itemCount(state.dataSource.state) : 0;
        const auto collectionLayout = resolveVirtualCollectionLayout({
            .logicalItemCount = logicalItemCount,
            .materializedItemCapacity = state.materializedItemCapacity,
            .rowHeight = state.style.rowHeight,
            .overscanRows = state.style.overscanRows,
            .scrollBarVisibility = state.style.scrollBarVisibility,
            .scrollBarThickness = state.paint.scrollBar.thickness,
            .requestedScrollOffset = state.requestedScrollOffset,
            .availableRect = unscrolledContentRect,
        });
        if (!collectionLayout)
        {
            return collectionLayout.error() == VirtualCollectionLayoutError::ContentHeightNotRepresentable
                       ? fail(UIErrorCode::InvalidControlValue,
                              "UI ListView logical content height is not representable")
                       : fail(UIErrorCode::CapacityExceeded,
                              "UI ListView row pool cannot cover the viewport and configured overscan");
        }
        const auto& plan = *collectionLayout;

        ListViewLayoutScratch& listLayout = listViewLayoutScratchByNodeIndex[listViewIndex];
        listLayout = ListViewLayoutScratch{
            .metrics =
                UIListViewMetrics{
                    .logicalItemCount = logicalItemCount,
                    .firstVisibleIndex = plan.firstVisibleIndex,
                    .visibleItemCount = static_cast<u32>(plan.visibleItemCount),
                    .firstMaterializedIndex = plan.firstMaterializedIndex,
                    .materializedItemCount = static_cast<u32>(plan.materializedItemCount),
                    .materializedItemCapacity = state.materializedItemCapacity,
                    .scrollOffset = plan.scrollOffset,
                    .maxScrollOffset = plan.maximumScrollOffset,
                    .viewportSize = plan.viewportRect.size(),
                    .contentSize = plan.contentSize,
                    .verticalScrollBarVisible = plan.verticalScrollBarVisible,
                },
            .viewportRect = plan.viewportRect,
        };

        const NodeRecord* listRecord = recordByIndex(listViewIndex);
        u32 childIndex = listRecord == nullptr ? InvalidNodeIndex : listRecord->firstChildIndex;
        u64 materializedOrdinal = 0;
        const UIVisibility rowVisibility = layoutScratchByIndex[listViewIndex].effectiveVisibility;
        const UILogicalRect rowClip = intersectRects(descendantClip, plan.viewportRect);
        while (childIndex != InvalidNodeIndex)
        {
            const NodeRecord* childRecord = recordByIndex(childIndex);
            if (childRecord == nullptr)
            {
                break;
            }
            const u32 currentChild = childIndex;
            childIndex = childRecord->nextSiblingIndex;
            if (materializedOrdinal >= plan.materializedItemCount)
            {
                ListViewItemState& item = listViewItemStatesByNodeIndex[currentChild];
                item.key = InvalidUIListViewItemKey;
                item.logicalIndex = 0;
                item.bound = false;
                item.enabled = true;
                layoutScratchByIndex[currentChild].effectiveVisibility = UIVisibility::Collapsed;
                assignLayoutRect(currentChild, plan.viewportRect, parentWorldRect, rowClip);
                continue;
            }

            const u64 logicalIndex = plan.firstMaterializedIndex + materializedOrdinal;
            auto descriptor = resolveListViewLogicalItem(idForIndex(listViewIndex), logicalIndex);
            if (!descriptor)
            {
                return Core::failure(descriptor.error());
            }
            if (Core::Status bound = bindListViewItem(currentChild, logicalIndex, *descriptor); !bound)
            {
                return bound;
            }
            layoutScratchByIndex[currentChild].effectiveVisibility = rowVisibility;
            const double logicalY = static_cast<double>(logicalIndex) * state.style.rowHeight;
            const float rowY = normalizeFloat(
                plan.viewportRect.y + static_cast<float>(logicalY) - plan.scrollOffset);
            assignLayoutRect(currentChild,
                             UILogicalRect{
                                 .x = plan.viewportRect.x,
                                 .y = rowY,
                                 .width = plan.viewportRect.width,
                                 .height = state.style.rowHeight,
                             },
                             parentWorldRect, rowClip);
            ++materializedOrdinal;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status bindTreeViewItem(u32 itemIndex, u64 logicalIndex,
                                                const UITreeViewItemDescriptor& descriptor)
    {
        if (itemIndex >= textStatesByIndex.size() || itemIndex >= treeViewItemStatesByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI TreeView item side state is out of range");
        }
        if (descriptor.key == InvalidUITreeViewItemKey || containsLineBreak(descriptor.label) ||
            (descriptor.expanded && !descriptor.expandable))
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI TreeView item requires a non-zero key, a single-line label, and valid expansion state");
        }
        if (descriptor.label.size() > (std::numeric_limits<u32>::max)())
        {
            return fail(UIErrorCode::CapacityExceeded, "UI TreeView item label is too large");
        }

        WidgetTextState& text = textStatesByIndex[itemIndex];
        auto metrics = measureWidgetText(descriptor.label, text.style);
        if (!metrics)
        {
            return Core::failure(metrics.error());
        }

        TextByteAllocation replacement{};
        bool replaceAllocation = false;
        if (!descriptor.label.empty() && text.allocation.capacity < descriptor.label.size())
        {
            auto allocation = textStorage.allocate(static_cast<u32>(descriptor.label.size()));
            if (!allocation)
            {
                return Core::failure(allocation.error());
            }
            replacement = *allocation;
            replaceAllocation = true;
        }
        if (descriptor.label.empty())
        {
            textStorage.release(text.allocation);
            text.allocation = {};
            text.length = 0;
            text.metrics = {};
            text.hasContent = false;
        } else
        {
            if (replaceAllocation)
            {
                textStorage.release(text.allocation);
                text.allocation = replacement;
            }
            textStorage.write(text.allocation, descriptor.label);
            text.length = static_cast<u32>(descriptor.label.size());
            text.metrics = *metrics;
            text.hasContent = true;
        }
        localTextColorCacheByIndex[itemIndex] =
            text.hasContent ? premultiply(text.style.color) : UIPremultipliedRgba8Color{};
        TreeViewItemState& item = treeViewItemStatesByNodeIndex[itemIndex];
        item.key = descriptor.key;
        item.logicalIndex = logicalIndex;
        item.level = descriptor.level;
        item.bound = true;
        item.enabled = descriptor.enabled;
        item.expandable = descriptor.expandable;
        item.expanded = descriptor.expanded;
        return Core::success();
    }

    void collapseTreeViewItems(u32 treeViewIndex, UILogicalRect contentRect, UILogicalRect parentWorldRect,
                               UILogicalRect descendantClip) noexcept
    {
        const NodeRecord* treeRecord = recordByIndex(treeViewIndex);
        u32 childIndex = treeRecord == nullptr ? InvalidNodeIndex : treeRecord->firstChildIndex;
        while (childIndex != InvalidNodeIndex)
        {
            const NodeRecord* childRecord = recordByIndex(childIndex);
            if (childRecord == nullptr)
            {
                break;
            }
            TreeViewItemState& item = treeViewItemStatesByNodeIndex[childIndex];
            item = {};
            layoutScratchByIndex[childIndex].effectiveVisibility = UIVisibility::Collapsed;
            assignLayoutRect(childIndex, contentRect, parentWorldRect, descendantClip);
            childIndex = childRecord->nextSiblingIndex;
        }
    }

    [[nodiscard]] Core::Status arrangeTreeViewItems(u32 treeViewIndex, UILogicalRect unscrolledContentRect,
                                                    UILogicalRect parentWorldRect, UILogicalRect descendantClip)
    {
        TreeViewState& state = treeViewStatesByNodeIndex[treeViewIndex];
        const u64 logicalItemCount =
            state.dataSource.hasValue() ? state.dataSource.itemCount(state.dataSource.state) : 0;
        const auto collectionLayout = resolveVirtualCollectionLayout({
            .logicalItemCount = logicalItemCount,
            .materializedItemCapacity = state.materializedItemCapacity,
            .rowHeight = state.style.rowHeight,
            .overscanRows = state.style.overscanRows,
            .scrollBarVisibility = state.style.scrollBarVisibility,
            .scrollBarThickness = state.paint.scrollBar.thickness,
            .requestedScrollOffset = state.requestedScrollOffset,
            .availableRect = unscrolledContentRect,
        });
        if (!collectionLayout)
        {
            return collectionLayout.error() == VirtualCollectionLayoutError::ContentHeightNotRepresentable
                       ? fail(UIErrorCode::InvalidControlValue,
                              "UI TreeView logical content height is not representable")
                       : fail(UIErrorCode::CapacityExceeded,
                              "UI TreeView row pool cannot cover the viewport and configured overscan");
        }
        const auto& plan = *collectionLayout;

        TreeViewLayoutScratch& treeLayout = treeViewLayoutScratchByNodeIndex[treeViewIndex];
        treeLayout = TreeViewLayoutScratch{
            .metrics =
                UITreeViewMetrics{
                    .logicalItemCount = logicalItemCount,
                    .firstVisibleIndex = plan.firstVisibleIndex,
                    .visibleItemCount = static_cast<u32>(plan.visibleItemCount),
                    .firstMaterializedIndex = plan.firstMaterializedIndex,
                    .materializedItemCount = static_cast<u32>(plan.materializedItemCount),
                    .materializedItemCapacity = state.materializedItemCapacity,
                    .scrollOffset = plan.scrollOffset,
                    .maxScrollOffset = plan.maximumScrollOffset,
                    .viewportSize = plan.viewportRect.size(),
                    .contentSize = plan.contentSize,
                    .verticalScrollBarVisible = plan.verticalScrollBarVisible,
                },
            .viewportRect = plan.viewportRect,
        };

        const NodeRecord* treeRecord = recordByIndex(treeViewIndex);
        u32 childIndex = treeRecord == nullptr ? InvalidNodeIndex : treeRecord->firstChildIndex;
        u64 materializedOrdinal = 0;
        const UIVisibility rowVisibility = layoutScratchByIndex[treeViewIndex].effectiveVisibility;
        const UILogicalRect rowClip = intersectRects(descendantClip, plan.viewportRect);
        while (childIndex != InvalidNodeIndex)
        {
            const NodeRecord* childRecord = recordByIndex(childIndex);
            if (childRecord == nullptr)
            {
                break;
            }
            const u32 currentChild = childIndex;
            childIndex = childRecord->nextSiblingIndex;
            if (materializedOrdinal >= plan.materializedItemCount)
            {
                treeViewItemStatesByNodeIndex[currentChild] = {};
                layoutScratchByIndex[currentChild].effectiveVisibility = UIVisibility::Collapsed;
                assignLayoutRect(currentChild, plan.viewportRect, parentWorldRect, rowClip);
                continue;
            }

            const u64 logicalIndex = plan.firstMaterializedIndex + materializedOrdinal;
            auto descriptor = resolveTreeViewLogicalItem(idForIndex(treeViewIndex), logicalIndex);
            if (!descriptor)
            {
                return Core::failure(descriptor.error());
            }
            const double leftPadding = 8.0 + static_cast<double>(descriptor->level) * state.style.indentation +
                                       state.style.disclosureExtent + state.style.disclosureGap;
            if (!std::isfinite(leftPadding) || leftPadding > (std::numeric_limits<float>::max)())
            {
                return fail(UIErrorCode::InvalidControlValue, "UI TreeView item indentation is not representable");
            }
            if (Core::Status bound = bindTreeViewItem(currentChild, logicalIndex, *descriptor); !bound)
            {
                return bound;
            }
            UILayoutStyle& rowStyle = layoutStylesByIndex[currentChild];
            rowStyle.size.height = UILayoutLength::Px(state.style.rowHeight);
            rowStyle.padding.left = normalizeFloat(static_cast<float>(leftPadding));
            rowStyle.padding.right = 8.0F;
            rowStyle.padding.top = 4.0F;
            rowStyle.padding.bottom = 4.0F;
            layoutScratchByIndex[currentChild].effectiveVisibility = rowVisibility;
            const double logicalY = static_cast<double>(logicalIndex) * state.style.rowHeight;
            const float rowY = normalizeFloat(
                plan.viewportRect.y + static_cast<float>(logicalY) - plan.scrollOffset);
            assignLayoutRect(currentChild,
                             UILogicalRect{
                                 .x = plan.viewportRect.x,
                                 .y = rowY,
                                 .width = plan.viewportRect.width,
                                 .height = state.style.rowHeight,
                             },
                             parentWorldRect, rowClip);
            ++materializedOrdinal;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status arrangeChildren(u32 parentIndex, UILogicalRect viewportRect,
                                               LayoutPassStatistics& statistics)
    {
        const NodeRecord* parentRecord = recordByIndex(parentIndex);
        if (parentRecord == nullptr)
        {
            return Core::success();
        }
        const UILayoutStyle& parentStyle = layoutStylesByIndex[parentIndex];
        const LayoutScratchState& parentScratch = layoutScratchByIndex[parentIndex];
        const UILogicalRect parentWorldRect = parentScratch.worldRect;
        const UILogicalRect unscrolledContentRect{
            .x = normalizeFloat(parentWorldRect.x + parentStyle.padding.left),
            .y = normalizeFloat(parentWorldRect.y + parentStyle.padding.top),
            .width = normalizeFloat((std::max)(0.0F, parentWorldRect.width - horizontalMargin(parentStyle.padding))),
            .height = normalizeFloat((std::max)(0.0F, parentWorldRect.height - verticalMargin(parentStyle.padding))),
        };
        UILogicalRect layoutContentRect = unscrolledContentRect;
        UILogicalRect descendantClip = parentScratch.descendantClip;
        if (parentRecord->kind == BuiltinElementKind::Popup)
        {
            descendantClip = intersectRects(descendantClip, parentScratch.worldRect);
        }

        if (parentScratch.effectiveVisibility == UIVisibility::Collapsed)
        {
            if (parentRecord->kind == BuiltinElementKind::ScrollView &&
                parentIndex < scrollViewLayoutScratchByNodeIndex.size())
            {
                scrollViewLayoutScratchByNodeIndex[parentIndex] = {};
            }
            if (parentRecord->kind == BuiltinElementKind::Popup && parentIndex < popupLayoutScratchByNodeIndex.size())
            {
                popupLayoutScratchByNodeIndex[parentIndex] = {};
            }
            if (parentRecord->kind == BuiltinElementKind::ListView && parentIndex < listViewLayoutScratchByNodeIndex.size())
            {
                listViewLayoutScratchByNodeIndex[parentIndex] = {};
                collapseListViewItems(parentIndex, unscrolledContentRect, parentWorldRect, descendantClip);
                return Core::success();
            }
            if (parentRecord->kind == BuiltinElementKind::TreeView && parentIndex < treeViewLayoutScratchByNodeIndex.size())
            {
                treeViewLayoutScratchByNodeIndex[parentIndex] = {};
                collapseTreeViewItems(parentIndex, unscrolledContentRect, parentWorldRect, descendantClip);
                return Core::success();
            }
            u32 collapsedChild = parentRecord->firstChildIndex;
            while (collapsedChild != InvalidNodeIndex)
            {
                const NodeRecord* childRecord = recordByIndex(collapsedChild);
                if (childRecord == nullptr)
                {
                    break;
                }
                assignLayoutRect(collapsedChild, unscrolledContentRect, parentWorldRect, descendantClip);
                collapsedChild = childRecord->nextSiblingIndex;
            }
            return Core::success();
        }

        if (parentRecord->kind == BuiltinElementKind::ListView && parentIndex < listViewStatesByNodeIndex.size() &&
            parentIndex < listViewLayoutScratchByNodeIndex.size())
        {
            return arrangeListViewItems(parentIndex, unscrolledContentRect, parentWorldRect,
                                        intersectRects(parentScratch.descendantClip, parentScratch.worldRect));
        }
        if (parentRecord->kind == BuiltinElementKind::TreeView && parentIndex < treeViewStatesByNodeIndex.size() &&
            parentIndex < treeViewLayoutScratchByNodeIndex.size())
        {
            return arrangeTreeViewItems(parentIndex, unscrolledContentRect, parentWorldRect,
                                        intersectRects(parentScratch.descendantClip, parentScratch.worldRect));
        }

        const bool row = parentStyle.flexContainer.direction == UIFlexDirection::Row;
        const float configuredGap = row ? parentStyle.flexContainer.gap.column : parentStyle.flexContainer.gap.row;

        FlexLineSummary flexSummary{};
        const float initialContentMain = row ? unscrolledContentRect.width : unscrolledContentRect.height;
        u32 childIndex = parentRecord->firstChildIndex;
        while (childIndex != InvalidNodeIndex)
        {
            const NodeRecord* childRecord = recordByIndex(childIndex);
            if (childRecord == nullptr)
            {
                break;
            }
            const UILayoutStyle& childStyle = layoutStylesByIndex[childIndex];
            LayoutScratchState& childScratch = layoutScratchByIndex[childIndex];
            if (childScratch.effectiveVisibility != UIVisibility::Collapsed &&
                childStyle.placement == UILayoutPlacement::Flow)
            {
                refreshMeasuredSizeForParentContent(childIndex, unscrolledContentRect, statistics);
                appendFlexLineItem(
                    flexSummary,
                    parentStyle.flexContainer.direction,
                    configuredGap,
                    initialContentMain,
                    childStyle,
                    childScratch,
                    statistics);
            }
            childIndex = childRecord->nextSiblingIndex;
        }

        if (!isValidFlexLineSummary(flexSummary))
        {
            return fail(UIErrorCode::InvalidLayout, "UI flex layout accumulation produced a non-finite value");
        }

        if (parentRecord->kind == BuiltinElementKind::ScrollView && parentIndex < scrollViewPaintsByNodeIndex.size() &&
            parentIndex < scrollViewLayoutScratchByNodeIndex.size())
        {
            const UIScrollBehaviorState* state = behaviorStateStorage.tryScrollState(parentIndex);
            if (state == nullptr)
            {
                return fail(Core::CoreErrorCode::Internal, "UI ScrollView is missing Scroll behavior state");
            }
            const UIScrollViewPaint& paint = scrollViewPaintsByNodeIndex[parentIndex];
            const auto plan = resolveScrollViewLayout(ScrollViewLayoutInput{
                .availableRect = unscrolledContentRect,
                .rawContentSize = flexSummary.contentSize(parentStyle.flexContainer.direction),
                .style = state->style,
                .scrollBarThickness = paint.thickness,
                .requestedOffset = state->requestedOffset,
            });
            scrollViewLayoutScratchByNodeIndex[parentIndex] = ScrollViewLayoutScratch{
                .metrics = plan.metrics,
                .viewportRect = plan.viewportRect,
            };
            layoutContentRect = plan.contentRect;
            descendantClip = intersectRects(parentScratch.descendantClip, plan.viewportRect);
        }

        auto flexPlan = resolveFlexLinePlan(parentStyle, layoutContentRect, flexSummary);

        childIndex = parentRecord->firstChildIndex;
        while (childIndex != InvalidNodeIndex)
        {
            const NodeRecord* childRecord = recordByIndex(childIndex);
            if (childRecord == nullptr)
            {
                break;
            }
            const u32 currentChild = childIndex;
            childIndex = childRecord->nextSiblingIndex;
            const UILayoutStyle& childStyle = layoutStylesByIndex[currentChild];
            LayoutScratchState& childScratch = layoutScratchByIndex[currentChild];

            if (childScratch.effectiveVisibility == UIVisibility::Collapsed)
            {
                assignLayoutRect(currentChild, layoutContentRect, parentWorldRect, descendantClip);
                continue;
            }
            if (childStyle.placement == UILayoutPlacement::Overlay)
            {
                if (childRecord->kind == BuiltinElementKind::Popup && currentChild < popupStatesByNodeIndex.size() &&
                    currentChild < popupLayoutScratchByNodeIndex.size())
                {
                    arrangePopupChild(currentChild, parentWorldRect, viewportRect, statistics);
                } else
                {
                    arrangeOverlayChild(currentChild, layoutContentRect, parentWorldRect, descendantClip, statistics);
                }
                continue;
            }

            assignLayoutRect(currentChild,
                             resolveFlexItemRect(flexPlan, childStyle, childScratch, statistics),
                             parentWorldRect, descendantClip);
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status arrangeLayout(UILogicalSize viewportSize, const std::pmr::vector<u32>& order,
                                             LayoutPassStatistics& statistics)
    {
        const UILogicalRect viewportRect{
            .x = 0.0F,
            .y = 0.0F,
            .width = viewportSize.width,
            .height = viewportSize.height,
        };

        u32 ordinal = 0;
        for (const u32 index : order)
        {
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr)
            {
                continue;
            }
            const u32 currentOrdinal = ordinal++;
            if (!hasLayoutWork(layoutWorkByIndex[index], LayoutWorkArrange))
            {
                continue;
            }
            ++statistics.arrangedNodeCount;
            LayoutScratchState& scratch = layoutScratchByIndex[index];
            if (record->parentIndex == InvalidNodeIndex)
            {
                assignLayoutRect(index,
                                 UILogicalRect{
                                     .x = 0.0F,
                                     .y = 0.0F,
                                     .width = scratch.measuredSize.width,
                                     .height = scratch.measuredSize.height,
                                 },
                                 viewportRect, viewportRect);
            }
            scratch.layoutOrdinal = currentOrdinal;
            scratch.paintOrdinal = currentOrdinal;
            if (Core::Status arranged = arrangeChildren(index, viewportRect, statistics); !arranged)
            {
                return arranged;
            }
        }
        return Core::success();
    }

    [[nodiscard]] UICommittedContentPlacement contentPlacementFor(u32 index) const noexcept
    {
        const LayoutScratchState& scratch = layoutScratchByIndex[index];
        const UILayoutStyle& layout = layoutStylesByIndex[index];
        const NodeRecord* record = recordByIndex(index);
        float leadingReservedWidth = 0.0F;
        float trailingReservedWidth = 0.0F;
        if (record != nullptr && record->kind == BuiltinElementKind::Dropdown &&
            index < dropdownStatesByNodeIndex.size())
        {
            const UIDropdownPaint& dropdown = dropdownStatesByNodeIndex[index].paint;
            trailingReservedWidth =
                dropdown.indicatorWidth + dropdown.indicatorInset * 2.0F;
        }
        if (record != nullptr && record->kind == BuiltinElementKind::RadioButton &&
            index < radioButtonStatesByNodeIndex.size())
        {
            const float indicatorExtent = (std::min)(scratch.worldRect.width, scratch.worldRect.height);
            leadingReservedWidth =
                indicatorExtent + radioButtonStatesByNodeIndex[index].paint.labelGap;
        }

        const UIImageContent* image = imageContentStorage.get(index);
        const UITextMetrics* metrics =
            index < textStatesByIndex.size() ? presentationTextMetricsFor(index)
                                             : nullptr;
        const UIContentAlignment alignment =
            metrics != nullptr ? textStatesByIndex[index].alignment
                               : image != nullptr ? image->alignment : UIContentAlignment{};
        const UILogicalSize* intrinsicSize =
            metrics != nullptr ? &metrics->measuredSize
                               : image != nullptr ? &image->source.intrinsicLogicalSize : nullptr;
        return resolveContentPlacement(
            scratch.worldRect, layout.padding, leadingReservedWidth,
            trailingReservedWidth, alignment, intrinsicSize);
    }

    void buildCommittedLayout(std::pmr::vector<UICommittedLayoutEntry>& output,
                              const std::pmr::vector<u32>& order) const noexcept
    {
        output.clear();
        u32 paintOrdinal = 0;
        const auto appendPass = [&](bool popupPass) noexcept {
            for (const u32 index : order)
            {
                if (layoutScratchByIndex[index].inPopupSubtree != popupPass)
                {
                    continue;
                }
                const LayoutScratchState& scratch = layoutScratchByIndex[index];
                output.push_back(UICommittedLayoutEntry{
                    .node = idForIndex(index),
                    .localRect = scratch.localRect,
                    .worldRect = scratch.worldRect,
                    .effectiveClip = scratch.effectiveClip,
                    .contentPlacement = contentPlacementFor(index),
                    .effectiveVisibility = scratch.effectiveVisibility,
                    .layoutOrdinal = scratch.layoutOrdinal,
                    .paintOrdinal = paintOrdinal,
                });
                ++paintOrdinal;
            }
        };
        appendPass(false);
        appendPass(true);
    }

    [[nodiscard]] Core::Result<CommittedHitBuildResult>
    buildCommittedHit(std::pmr::vector<UICommittedHitEntry>& output,
                      std::span<const UICommittedLayoutEntry> layoutEntries)
    {
        usize visibleEntryCount = 0;
        for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
        {
            if (layoutEntry.effectiveVisibility == UIVisibility::Visible)
            {
                ++visibleEntryCount;
            }
        }
        if (visibleEntryCount > capacityConfig.hitSnapshotCapacity)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI committed hit snapshot capacity has been exhausted");
        }

        output.clear();
        for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
        {
            hitEntryIndexByNodeIndex[layoutEntry.node.index()] = InvalidUIHitEntryIndex;
        }
        usize targetCount = 0;
        u32 activeModalEntryIndex = InvalidUIHitEntryIndex;

        for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
        {
            if (layoutEntry.effectiveVisibility != UIVisibility::Visible)
            {
                continue;
            }
            if (!contains(layoutEntry.node))
            {
                return fail(UIErrorCode::InvalidNode, "UI hit snapshot layout references a stale node");
            }

            const u32 nodeIndex = layoutEntry.node.index();
            const NodeRecord* record = recordByIndex(nodeIndex);
            if (record == nullptr)
            {
                return fail(UIErrorCode::InvalidNode, "UI hit snapshot node record is unavailable");
            }

            const UIPointerHitPolicy policy = pointerHitPoliciesByIndex[nodeIndex];
            if (!isValidPointerHitPolicy(policy))
            {
                return fail(UIErrorCode::InvalidPointerPolicy, "UI hit snapshot contains an invalid pointer policy");
            }
            const UIFocusScopeMode focusScopeMode = focusScopeModesByNodeIndex[nodeIndex];
            if (!isValidFocusScopeMode(focusScopeMode))
            {
                return fail(UIErrorCode::InvalidFocusScope, "UI hit snapshot contains an invalid focus-scope mode");
            }

            const u32 entryIndex = static_cast<u32>(output.size());
            u32 parentEntryIndex = InvalidUIHitEntryIndex;
            u32 rootEntryIndex = entryIndex;
            u32 focusScopeEntryIndex = InvalidUIHitEntryIndex;
            u32 modalScopeEntryIndex = InvalidUIHitEntryIndex;
            if (record->parentIndex != InvalidNodeIndex)
            {
                parentEntryIndex = hitEntryIndexByNodeIndex[record->parentIndex];
                rootEntryIndex = hitEntryIndexByNodeIndex[record->rootIndex];
                if (parentEntryIndex == InvalidUIHitEntryIndex || rootEntryIndex == InvalidUIHitEntryIndex)
                {
                    return fail(UIErrorCode::InvalidNode, "UI hit snapshot visible ancestry is incomplete");
                }
                focusScopeEntryIndex = output[parentEntryIndex].focusScopeEntryIndex;
                modalScopeEntryIndex = output[parentEntryIndex].modalScopeEntryIndex;
            }
            if (focusScopeMode == UIFocusScopeMode::Contain ||
                hasBehavior(record->behaviors, UIElementBehavior::ModalBarrier))
            {
                focusScopeEntryIndex = entryIndex;
            }
            if (hasBehavior(record->behaviors, UIElementBehavior::ModalBarrier))
            {
                modalScopeEntryIndex = entryIndex;
                activeModalEntryIndex = entryIndex;
            }

            output.push_back(UICommittedHitEntry{
                .node = layoutEntry.node,
                .parentEntryIndex = parentEntryIndex,
                .rootEntryIndex = rootEntryIndex,
                .focusScopeEntryIndex = focusScopeEntryIndex,
                .modalScopeEntryIndex = modalScopeEntryIndex,
                .worldRect = layoutEntry.worldRect,
                .effectiveClip = layoutEntry.effectiveClip,
                .paintOrdinal = layoutEntry.paintOrdinal,
                .policy = policy,
                .behaviors = record->behaviors,
            });
            hitEntryIndexByNodeIndex[nodeIndex] = entryIndex;
            if (policy == UIPointerHitPolicy::Targetable)
            {
                ++targetCount;
            }
        }
        return CommittedHitBuildResult{
            .targetCount = targetCount,
            .activeModalEntryIndex = activeModalEntryIndex,
        };
    }

    [[nodiscard]] UIPremultipliedRgba8Color widgetPaintColor(UINodeId node,
                                                             UIPremultipliedRgba8Color color) const noexcept
    {
        constexpr u8 DisabledOpacity = 140;
        return isCandidateNodeEnabled(node) ? color : applyOpacity(color, DisabledOpacity);
    }

    [[nodiscard]] UIPremultipliedRgba8Color resolveBuiltinBoxFillColor(
        UINodeId node, u32 nodeIndex,
        UIPremultipliedRgba8Color normalColor) const noexcept
    {
        UIPremultipliedRgba8Color color = normalColor;
        const NodeRecord* record = recordByIndex(nodeIndex);
        if (record != nullptr && record->kind == BuiltinElementKind::Checkbox &&
            nodeIndex < checkboxPaintsByNodeIndex.size())
        {
            const UICheckboxPaint& paint = checkboxPaintsByNodeIndex[nodeIndex];
            const auto applyOverride = [&color](UIStraightSrgba8Color overrideColor) noexcept {
                if (overrideColor.alpha != 0)
                {
                    color = premultiply(overrideColor);
                }
            };
            if (defaultActionFocusButton == node)
            {
                applyOverride(paint.focusedIndicatorColor);
            }
            if (hoveredPrimaryControl == node)
            {
                applyOverride(paint.hoveredIndicatorColor);
            }
            if (isButtonPressed(node))
            {
                applyOverride(paint.pressedIndicatorColor);
            }
            return widgetPaintColor(node, color);
        }
        if (record != nullptr && record->kind == BuiltinElementKind::TextEdit &&
            nodeIndex < textEditPaintsByNodeIndex.size())
        {
            const UITextEditPaint& paint = textEditPaintsByNodeIndex[nodeIndex];
            const auto applyOverride = [&color](UIStraightSrgba8Color overrideColor) noexcept {
                if (overrideColor.alpha != 0)
                {
                    color = premultiply(overrideColor);
                }
            };
            if (textInputFocus == node && isLiveTextEdit(node))
            {
                applyOverride(paint.focusedBackgroundColor);
            }
            if (hoveredPrimaryControl == node)
            {
                applyOverride(paint.hoveredBackgroundColor);
            }
            if (armedTextEdit == node)
            {
                applyOverride(paint.pressedBackgroundColor);
            }
            if (!isNodeEnabled(node))
            {
                applyOverride(paint.disabledBackgroundColor);
            }
            return widgetPaintColor(node, color);
        }
        if (record == nullptr || !isButtonChromeKind(record->kind) || nodeIndex >= buttonPaintsByNodeIndex.size())
        {
            return widgetPaintColor(node, color);
        }

        const UIButtonPaint& paint = buttonPaintsByNodeIndex[nodeIndex];
        const auto applyOverride = [&color](UIStraightSrgba8Color overrideColor) noexcept {
            if (overrideColor.alpha != 0)
            {
                color = premultiply(overrideColor);
            }
        };
        if (defaultActionFocusButton == node)
        {
            applyOverride(paint.focusedBackgroundColor);
        }
        if (hoveredPrimaryControl == node)
        {
            applyOverride(paint.hoveredBackgroundColor);
        }
        if (isButtonPressed(node))
        {
            applyOverride(paint.pressedBackgroundColor);
        }
        if (!isNodeEnabled(node))
        {
            applyOverride(paint.disabledBackgroundColor);
        }
        return widgetPaintColor(node, color);
    }

    [[nodiscard]] static u16 boxFillOverrideMask(const NodeRecord& record) noexcept
    {
        u16 relevantOverrides = static_cast<u16>(UIStyleOverride::BoxPaint);
        if (record.kind == BuiltinElementKind::Checkbox)
        {
            relevantOverrides |= static_cast<u16>(UIStyleOverride::CheckboxPaint);
        }
        else if (record.kind == BuiltinElementKind::TextEdit)
        {
            relevantOverrides |= static_cast<u16>(UIStyleOverride::TextEditPaint);
        }
        else if (isButtonChromeKind(record.kind))
        {
            relevantOverrides |= static_cast<u16>(UIStyleOverride::ButtonPaint);
        }
        return relevantOverrides;
    }

    [[nodiscard]] bool hasLocalBoxFillOverride(u32 nodeIndex,
                                                const NodeRecord& record) const noexcept
    {
        return nodeIndex < styleOverridesByNodeIndex.size() &&
               (styleOverridesByNodeIndex[nodeIndex] & boxFillOverrideMask(record)) != 0;
    }

    [[nodiscard]] UIStyleState deriveStyleState(UINodeId node,
                                                 u32 nodeIndex) const noexcept
    {
        UIStyleState states = UIStyleState::None;
        if (hoveredPrimaryControl == node)
        {
            states |= UIStyleState::Hovered;
        }
        if (isButtonPressed(node) || armedTextEdit == node)
        {
            states |= UIStyleState::Pressed;
        }
        if (defaultActionFocusButton == node || textInputFocus == node)
        {
            states |= UIStyleState::Focused;
        }
        if (!isCandidateNodeEnabled(node))
        {
            states |= UIStyleState::Disabled;
        }
        if (const u8* toggleValue = behaviorStateStorage.tryToggleValue(nodeIndex);
            toggleValue != nullptr && *toggleValue != 0)
        {
            states |= UIStyleState::Checked;
        }

        const NodeRecord* record = recordByIndex(nodeIndex);
        if (record == nullptr)
        {
            return states;
        }
        if ((record->kind == BuiltinElementKind::RadioButton &&
             nodeIndex < radioButtonStatesByNodeIndex.size() &&
             radioButtonStatesByNodeIndex[nodeIndex].selected) ||
            (record->kind == BuiltinElementKind::DropdownItem &&
             isSelectedDropdownItem(node)) ||
            (record->kind == BuiltinElementKind::ListViewItem &&
             isSelectedListViewItem(node)) ||
            (record->kind == BuiltinElementKind::TreeViewItem &&
             isSelectedTreeViewItem(node)))
        {
            states |= UIStyleState::Selected;
        }
        bool open = record->kind == BuiltinElementKind::Popup &&
                    nodeIndex < popupStatesByNodeIndex.size() &&
                    popupStatesByNodeIndex[nodeIndex].open;
        if (record->kind == BuiltinElementKind::Dropdown)
        {
            const UINodeId popup = popupForDropdown(node);
            open = popup.hasValue() && popup.index() < popupStatesByNodeIndex.size() &&
                   popupStatesByNodeIndex[popup.index()].open;
        }
        if (open)
        {
            states |= UIStyleState::Open;
        }
        if (armedSlider == node || armedScrollView == node)
        {
            states |= UIStyleState::Dragging;
        }
        return states;
    }

    [[nodiscard]] StyleInteractionNodeSet currentStyleInteractionNodes() const noexcept
    {
        StyleInteractionNodeSet result{};
        result.add(hoveredPrimaryControl);
        result.add(armedPrimaryButton);
        result.add(defaultActionFocusButton);
        result.add(textInputFocus);
        result.add(armedSlider);
        result.add(armedScrollView);
        result.add(armedTextEdit);
        for (const UINodeId target : defaultActionPressState.pressedTargets())
        {
            result.add(target);
        }
        return result;
    }

    [[nodiscard]] usize refreshResolvedStyleCache(u32 nodeIndex,
                                                   UIStyleState states) noexcept
    {
        const NodeRecord* record = recordByIndex(nodeIndex);
        if (record == nullptr || nodeIndex >= styleStatesByNodeIndex.size() ||
            nodeIndex >= resolvedBoxFillCacheByNodeIndex.size())
        {
            return 0;
        }

        const UINodeId node = idForIndex(nodeIndex);
        styleStatesByNodeIndex[nodeIndex] = states;
        UIPremultipliedRgba8Color resolvedFill = resolveBuiltinBoxFillColor(
            node, nodeIndex, localSolidFillCacheByIndex[nodeIndex]);
        if (hasLocalBoxFillOverride(nodeIndex, *record))
        {
            resolvedBoxFillCacheByNodeIndex[nodeIndex] = resolvedFill;
            return 0;
        }

        const usize classCount = styleClassCountsByNodeIndex[nodeIndex];
        const auto classes = std::span<const UIStyleClassId>(
            styleClassesByNodeIndex[nodeIndex].data(), classCount);
        const Detail::UIStyleBoxFillResolution resolution =
            styleSheetStorage.resolveValidated(styleRolesByNodeIndex[nodeIndex],
                                               classes, states);
        if (resolution.color.has_value())
        {
            resolvedFill = premultiply(*resolution.color);
        }
        resolvedBoxFillCacheByNodeIndex[nodeIndex] = resolvedFill;
        return resolution.candidateRuleCount;
    }

    [[nodiscard]] usize refreshResolvedStyleCache(u32 nodeIndex) noexcept
    {
        return refreshResolvedStyleCache(nodeIndex,
                                         deriveStyleState(idForIndex(nodeIndex), nodeIndex));
    }

    [[nodiscard]] UIBoxPaint resolvedBoxChrome(UINodeId node, u32 nodeIndex) const noexcept
    {
        UIBoxPaint chrome = boxPaintsByIndex[nodeIndex];
        const NodeRecord* record = recordByIndex(nodeIndex);
        if (record == nullptr || !isButtonChromeKind(record->kind) || nodeIndex >= buttonPaintsByNodeIndex.size() ||
            !isCandidateNodeEnabled(node))
        {
            return chrome;
        }

        if (isButtonPressed(node))
        {
            std::swap(chrome.borderLight, chrome.borderDark);
            chrome.shadowOffsetX = 0.0F;
            chrome.shadowOffsetY = 0.0F;
            return chrome;
        }

        const UIButtonPaint& states = buttonPaintsByNodeIndex[nodeIndex];
        if (defaultActionFocusButton == node && states.focusedBorderColor.alpha != 0)
        {
            chrome.borderLight = states.focusedBorderColor;
            chrome.borderDark = states.focusedBorderColor;
            if (!(chrome.borderWidth > 0.0F))
            {
                chrome.borderWidth = 1.0F;
            }
        }
        return chrome;
    }

    [[nodiscard]] UIPremultipliedRgba8Color resolvedRadioIndicatorColor(UINodeId node, u32 nodeIndex) const noexcept
    {
        if (nodeIndex >= radioButtonStatesByNodeIndex.size())
        {
            return {};
        }

        const UIRadioButtonPaint& paint = radioButtonStatesByNodeIndex[nodeIndex].paint;
        UIPremultipliedRgba8Color color = premultiply(paint.indicatorColor);
        const auto applyOverride = [&color](UIStraightSrgba8Color overrideColor) noexcept {
            if (overrideColor.alpha != 0)
            {
                color = premultiply(overrideColor);
            }
        };
        if (defaultActionFocusButton == node)
        {
            applyOverride(paint.focusedIndicatorColor);
        }
        if (hoveredPrimaryControl == node)
        {
            applyOverride(paint.hoveredIndicatorColor);
        }
        if (isButtonPressed(node))
        {
            applyOverride(paint.pressedIndicatorColor);
        }
        return widgetPaintColor(node, color);
    }

    [[nodiscard]] UIPremultipliedRgba8Color resolvedDropdownSelectionColor(UINodeId item) const noexcept
    {
        const UINodeId dropdown = dropdownForItem(item);
        const Detail::UISelectBehaviorState* select =
            dropdown.hasValue() ? behaviorStateStorage.trySelectState(dropdown.index()) : nullptr;
        if (!dropdown.hasValue() || dropdown.index() >= dropdownStatesByNodeIndex.size() ||
            select == nullptr || select->selectedOption != item)
        {
            return {};
        }
        return widgetPaintColor(item,
                                premultiply(dropdownStatesByNodeIndex[dropdown.index()].paint
                                                .selectedItemBackgroundColor));
    }

    [[nodiscard]] UIPremultipliedRgba8Color resolvedCollectionSelectionColor(
        UINodeId item, UINodeId collection, UIStraightSrgba8Color normalColor,
        UIStraightSrgba8Color hoveredColor, UIStraightSrgba8Color focusedColor,
        UIStraightSrgba8Color pressedColor) const noexcept
    {
        UIPremultipliedRgba8Color color = premultiply(normalColor);
        const auto applyOverride = [&color](UIStraightSrgba8Color overrideColor) noexcept {
            if (overrideColor.alpha != 0)
            {
                color = premultiply(overrideColor);
            }
        };
        if (isCandidateNodeEnabled(collection) && isCandidateNodeEnabled(item))
        {
            if (defaultActionFocusButton == collection)
            {
                applyOverride(focusedColor);
            }
            if (hoveredPrimaryControl == item)
            {
                applyOverride(hoveredColor);
            }
            if (isButtonPressed(item))
            {
                applyOverride(pressedColor);
            }
        }
        return widgetPaintColor(item, color);
    }

    [[nodiscard]] UIPremultipliedRgba8Color resolvedListViewSelectionColor(UINodeId item) const noexcept
    {
        const UINodeId listView = listViewForItem(item);
        if (!listView.hasValue() || listView.index() >= listViewStatesByNodeIndex.size() ||
            !isSelectedListViewItem(item))
        {
            return {};
        }
        const UIListViewPaint& paint = listViewStatesByNodeIndex[listView.index()].paint;
        return resolvedCollectionSelectionColor(
            item, listView, paint.selectedItemBackgroundColor, paint.hoveredSelectedItemBackgroundColor,
            paint.focusedSelectedItemBackgroundColor, paint.pressedSelectedItemBackgroundColor);
    }

    [[nodiscard]] UIPremultipliedRgba8Color resolvedTreeViewSelectionColor(UINodeId item) const noexcept
    {
        const UINodeId treeView = treeViewForItem(item);
        if (!treeView.hasValue() || treeView.index() >= treeViewStatesByNodeIndex.size() ||
            !isSelectedTreeViewItem(item))
        {
            return {};
        }
        const UITreeViewPaint& paint = treeViewStatesByNodeIndex[treeView.index()].paint;
        return resolvedCollectionSelectionColor(
            item, treeView, paint.selectedItemBackgroundColor, paint.hoveredSelectedItemBackgroundColor,
            paint.focusedSelectedItemBackgroundColor, paint.pressedSelectedItemBackgroundColor);
    }

    [[nodiscard]] UIPremultipliedRgba8Color resolvedTreeViewDisclosureColor(UINodeId item) const noexcept
    {
        const UINodeId treeView = treeViewForItem(item);
        if (!treeView.hasValue() || item.index() >= treeViewItemStatesByNodeIndex.size())
        {
            return {};
        }
        const TreeViewItemState& itemState = treeViewItemStatesByNodeIndex[item.index()];
        if (!itemState.bound || !itemState.expandable)
        {
            return {};
        }
        return widgetPaintColor(item, premultiply(treeViewStatesByNodeIndex[treeView.index()].paint.disclosureColor));
    }

    [[nodiscard]] usize countCanvasPaintEntries(const UICommittedLayoutEntry& layoutEntry) const noexcept
    {
        usize count = 0;
        canvasCommandStorage.forEach(layoutEntry.node.index(), [&](const UICanvasCommand& command) noexcept {
            if (command.color.alpha == 0 || command.bounds.width <= 0.0F || command.bounds.height <= 0.0F)
            {
                return;
            }
            if (command.kind == UICanvasCommandKind::NineSlice)
            {
                count += makeNineSlicePatches(layoutEntry.worldRect, command).count;
            } else
            {
                ++count;
            }
        });
        return count;
    }

    void appendCanvasPaints(std::pmr::vector<UICommittedPaintEntry>& output,
                            const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal) noexcept
    {
        const u32 nodeIndex = layoutEntry.node.index();
        const UILogicalRect canvasClip = intersectRects(layoutEntry.effectiveClip, layoutEntry.worldRect);
        const NodeRecord* record = recordByIndex(nodeIndex);
        const UINodeId root = record != nullptr ? idForIndex(record->rootIndex) : UINodeId{};
        const auto appendImage = [&](const UICanvasCommand& command, UILogicalRect worldRect,
                                     UIImagePixelRect sourcePixels,
                                     UICommittedImageBoundsProjection boundsProjection,
                                     UILogicalPoint projectionEnd) noexcept {
            UIImageSource source = command.imageSource;
            source.sourcePixels = sourcePixels;
            output.push_back(UICommittedPaintEntry{
                .node = layoutEntry.node,
                .root = root,
                .worldRect = worldRect,
                .effectiveClip = canvasClip,
                .paintOrdinal = nextPaintOrdinal,
                .solidFill = premultiply(command.color),
                .kind = UICommittedPaintKind::Image,
                .imageSource = source,
                .imageSampling = command.imageSampling,
                .imageBoundsProjection = boundsProjection,
                .imageProjectionEnd = projectionEnd,
            });
            ++nextPaintOrdinal;
        };
        canvasCommandStorage.forEach(nodeIndex, [&](const UICanvasCommand& command) noexcept {
            if (command.color.alpha == 0 || command.bounds.width <= 0.0F || command.bounds.height <= 0.0F)
            {
                return;
            }
            if (command.kind == UICanvasCommandKind::SolidRect)
            {
                output.push_back(UICommittedPaintEntry{
                    .node = layoutEntry.node,
                    .worldRect =
                        UILogicalRect{
                            .x = normalizeFloat(layoutEntry.worldRect.x + command.bounds.x),
                            .y = normalizeFloat(layoutEntry.worldRect.y + command.bounds.y),
                            .width = command.bounds.width,
                            .height = command.bounds.height,
                        },
                    .effectiveClip = canvasClip,
                    .paintOrdinal = nextPaintOrdinal,
                    .solidFill = premultiply(command.color),
                    .cornerRadius = command.cornerRadius,
                });
                ++nextPaintOrdinal;
            } else if (command.kind == UICanvasCommandKind::Image)
            {
                appendImage(
                    command,
                    UILogicalRect{
                        .x = normalizeFloat(layoutEntry.worldRect.x + command.bounds.x),
                        .y = normalizeFloat(layoutEntry.worldRect.y + command.bounds.y),
                        .width = command.bounds.width,
                        .height = command.bounds.height,
                    },
                    command.imageSource.sourcePixels,
                    UICommittedImageBoundsProjection::Cover,
                    {});
            } else if (command.kind == UICanvasCommandKind::NineSlice)
            {
                const UINineSlicePatchBatch patches = makeNineSlicePatches(layoutEntry.worldRect, command);
                for (usize patchIndex = 0; patchIndex < patches.count; ++patchIndex)
                {
                    const UINineSlicePatch& patch = patches.patches[patchIndex];
                    appendImage(command, patch.worldRect, patch.sourcePixels,
                                UICommittedImageBoundsProjection::SharedBoundary, patch.worldEnd);
                }
            }
        });
    }

    [[nodiscard]] Core::Result<Detail::UIControlPaintBatch>
    resolveControlPaintBatch(const UICommittedLayoutEntry& layoutEntry, bool applyDisabledOpacity) const
    {
        Detail::UIControlPaintBatch batch;
        bool batchCapacityExceeded = false;
        const auto add = [&](UILogicalRect worldRect, UIPremultipliedRgba8Color color) noexcept {
            batchCapacityExceeded = !batch.add(worldRect, color) || batchCapacityExceeded;
        };
        const auto controlColor = [&](UIStraightSrgba8Color color) noexcept {
            const UIPremultipliedRgba8Color premultiplied = premultiply(color);
            return applyDisabledOpacity ? widgetPaintColor(layoutEntry.node, premultiplied) : premultiplied;
        };

        const u32 nodeIndex = layoutEntry.node.index();
        const NodeRecord* record = recordByIndex(nodeIndex);
        const u8* toggleValue = behaviorStateStorage.tryToggleValue(nodeIndex);
        if (record != nullptr && record->kind == BuiltinElementKind::Checkbox &&
            nodeIndex < checkboxPaintsByNodeIndex.size() && toggleValue != nullptr &&
            *toggleValue != 0)
        {
            const UICheckboxPaint& paint = checkboxPaintsByNodeIndex[nodeIndex];
            const float extent = (std::min)(layoutEntry.worldRect.width, layoutEntry.worldRect.height);
            const float inset = paint.checkedIndicatorInset;
            add(
                UILogicalRect{
                    .x = normalizeFloat(layoutEntry.worldRect.x + inset),
                    .y = normalizeFloat(layoutEntry.worldRect.y + inset),
                    .width = normalizeFloat(extent - inset * 2.0F),
                    .height = normalizeFloat(extent - inset * 2.0F),
                },
                controlColor(paint.checkedIndicatorColor));
        } else if (record != nullptr && record->kind == BuiltinElementKind::Slider &&
                   nodeIndex < sliderPaintsByNodeIndex.size())
        {
            const Detail::UIRangeInputState* range = behaviorStateStorage.tryRangeInputState(nodeIndex);
            if (range == nullptr)
            {
                return fail(Core::CoreErrorCode::Internal, "UI Slider is missing RangeInput behavior state");
            }
            const UISliderPaint& paint = sliderPaintsByNodeIndex[nodeIndex];
            const SliderPaintGeometry geometry = sliderPaintGeometry(layoutEntry.worldRect, range->minValue,
                                                                     range->maxValue, range->value, paint);
            if (geometry.fraction > 0.0F)
            {
                add(geometry.filledTrack, controlColor(paint.filledTrackColor));
            }
            const UIStraightSrgba8Color thumbColor =
                armedSlider == layoutEntry.node && paint.draggingThumbColor.alpha != 0
                    ? paint.draggingThumbColor
                    : defaultActionFocusButton == layoutEntry.node && paint.focusedThumbColor.alpha != 0
                        ? paint.focusedThumbColor
                        : paint.thumbColor;
            add(geometry.thumb, controlColor(thumbColor));
        } else if (record != nullptr && record->kind == BuiltinElementKind::ScrollView &&
                   nodeIndex < scrollViewPaintsByNodeIndex.size() &&
                   nodeIndex < scrollViewLayoutScratchByNodeIndex.size())
        {
            const UIScrollViewPaint& scroll = scrollViewPaintsByNodeIndex[nodeIndex];
            const ScrollViewLayoutScratch& scrollLayout = scrollViewLayoutScratchByNodeIndex[nodeIndex];
            const auto addBar = [&](UIScrollAxes axis) noexcept {
                const ScrollBarGeometry geometry =
                    makeScrollBarGeometry(scrollLayout.metrics, scrollLayout.viewportRect, scroll, axis);
                if (!geometry.visible)
                {
                    return;
                }
                add(geometry.track, controlColor(scroll.trackColor));
                const UIStraightSrgba8Color thumbColor = scrollThumbDragActive && armedScrollView == layoutEntry.node &&
                                                                 armedScrollAxis == axis &&
                                                                 scroll.draggingThumbColor.alpha != 0
                                                             ? scroll.draggingThumbColor
                                                             : scroll.thumbColor;
                add(geometry.thumb, controlColor(thumbColor));
            };
            addBar(UIScrollAxes::Horizontal);
            addBar(UIScrollAxes::Vertical);
        } else if (record != nullptr && record->kind == BuiltinElementKind::ListView &&
                   nodeIndex < listViewStatesByNodeIndex.size() && nodeIndex < listViewLayoutScratchByNodeIndex.size())
        {
            const ListViewState& list = listViewStatesByNodeIndex[nodeIndex];
            const ListViewLayoutScratch& listLayout = listViewLayoutScratchByNodeIndex[nodeIndex];
            const ScrollBarGeometry geometry =
                makeListViewScrollBarGeometry(listLayout.metrics, listLayout.viewportRect, list.paint.scrollBar);
            if (geometry.visible)
            {
                add(geometry.track, controlColor(list.paint.scrollBar.trackColor));
                const UIStraightSrgba8Color thumbColor = scrollThumbDragActive && armedScrollView == layoutEntry.node &&
                                                                 list.paint.scrollBar.draggingThumbColor.alpha != 0
                                                             ? list.paint.scrollBar.draggingThumbColor
                                                             : list.paint.scrollBar.thumbColor;
                add(geometry.thumb, controlColor(thumbColor));
            }
        } else if (record != nullptr && record->kind == BuiltinElementKind::TreeView &&
                   nodeIndex < treeViewStatesByNodeIndex.size() && nodeIndex < treeViewLayoutScratchByNodeIndex.size())
        {
            const TreeViewState& tree = treeViewStatesByNodeIndex[nodeIndex];
            const TreeViewLayoutScratch& treeLayout = treeViewLayoutScratchByNodeIndex[nodeIndex];
            const ScrollBarGeometry geometry =
                makeTreeViewScrollBarGeometry(treeLayout.metrics, treeLayout.viewportRect, tree.paint.scrollBar);
            if (geometry.visible)
            {
                add(geometry.track, controlColor(tree.paint.scrollBar.trackColor));
                const UIStraightSrgba8Color thumbColor = scrollThumbDragActive && armedScrollView == layoutEntry.node &&
                                                                 tree.paint.scrollBar.draggingThumbColor.alpha != 0
                                                             ? tree.paint.scrollBar.draggingThumbColor
                                                             : tree.paint.scrollBar.thumbColor;
                add(geometry.thumb, controlColor(thumbColor));
            }
        } else if (record != nullptr && record->kind == BuiltinElementKind::Dropdown &&
                   nodeIndex < dropdownStatesByNodeIndex.size())
        {
            const UIDropdownPaint& paint = dropdownStatesByNodeIndex[nodeIndex].paint;
            if (paint.indicatorWidth > 0.0F && paint.indicatorHeight > 0.0F)
            {
                constexpr usize StripeCount = 3;
                const float stripeHeight = paint.indicatorHeight / static_cast<float>(StripeCount);
                const float centerX =
                    layoutEntry.worldRect.right() - paint.indicatorInset - paint.indicatorWidth * 0.5F;
                const float top =
                    layoutEntry.worldRect.y + (layoutEntry.worldRect.height - paint.indicatorHeight) * 0.5F;
                const UIPremultipliedRgba8Color color = controlColor(paint.indicatorColor);
                for (usize stripe = 0; stripe < StripeCount; ++stripe)
                {
                    const float width = paint.indicatorWidth * static_cast<float>(StripeCount - stripe) /
                                        static_cast<float>(StripeCount);
                    add(
                        UILogicalRect{
                            .x = normalizeFloat(centerX - width * 0.5F),
                            .y = normalizeFloat(top + stripeHeight * static_cast<float>(stripe)),
                            .width = normalizeFloat(width),
                            .height = normalizeFloat(stripeHeight),
                        },
                        color);
                }
            }
        } else if (record != nullptr && record->kind == BuiltinElementKind::DropdownItem)
        {
            add(layoutEntry.worldRect, resolvedDropdownSelectionColor(layoutEntry.node));
        } else if (record != nullptr && record->kind == BuiltinElementKind::ListViewItem)
        {
            add(layoutEntry.worldRect, resolvedListViewSelectionColor(layoutEntry.node));
        } else if (record != nullptr && record->kind == BuiltinElementKind::TreeViewItem &&
                   nodeIndex < treeViewItemStatesByNodeIndex.size())
        {
            add(layoutEntry.worldRect, resolvedTreeViewSelectionColor(layoutEntry.node));
            const TreeViewItemState& item = treeViewItemStatesByNodeIndex[nodeIndex];
            const UINodeId treeView = treeViewForItem(layoutEntry.node);
            const UIPremultipliedRgba8Color disclosure = resolvedTreeViewDisclosureColor(layoutEntry.node);
            if (treeView.hasValue() && !disclosure.isTransparent())
            {
                const UITreeViewStyle& style = treeViewStatesByNodeIndex[treeView.index()].style;
                const UILogicalRect disclosureRect =
                    makeTreeViewDisclosureRect(layoutEntry.worldRect, style, item.level);
                const float stroke = normalizeFloat((std::max)(1.0F, disclosureRect.height / 6.0F));
                add(
                    UILogicalRect{
                        .x = disclosureRect.x,
                        .y = normalizeFloat(disclosureRect.y + (disclosureRect.height - stroke) * 0.5F),
                        .width = disclosureRect.width,
                        .height = stroke,
                    },
                    disclosure);
                if (!item.expanded)
                {
                    add(
                        UILogicalRect{
                            .x = normalizeFloat(disclosureRect.x + (disclosureRect.width - stroke) * 0.5F),
                            .y = disclosureRect.y,
                            .width = stroke,
                            .height = disclosureRect.height,
                        },
                        disclosure);
                }
            }
        } else if (record != nullptr && record->kind == BuiltinElementKind::ProgressBar &&
                   nodeIndex < progressBarStatesByNodeIndex.size())
        {
            const ProgressBarState& progress = progressBarStatesByNodeIndex[nodeIndex];
            const float fraction = normalizedRangeFraction(progress.value, progress.minValue, progress.maxValue);
            if (fraction > 0.0F)
            {
                add(
                    UILogicalRect{
                        .x = layoutEntry.worldRect.x,
                        .y = layoutEntry.worldRect.y,
                        .width = normalizeFloat(layoutEntry.worldRect.width * fraction),
                        .height = layoutEntry.worldRect.height,
                    },
                    controlColor(progress.paint.fillColor));
            }
        } else if (record != nullptr && record->kind == BuiltinElementKind::RadioButton &&
                   nodeIndex < radioButtonStatesByNodeIndex.size())
        {
            const RadioButtonState& radio = radioButtonStatesByNodeIndex[nodeIndex];
            const float extent = (std::min)(layoutEntry.worldRect.width, layoutEntry.worldRect.height);
            add(
                UILogicalRect{
                    .x = layoutEntry.worldRect.x,
                    .y = layoutEntry.worldRect.y,
                    .width = normalizeFloat(extent),
                    .height = normalizeFloat(extent),
                },
                resolvedRadioIndicatorColor(layoutEntry.node, nodeIndex));
            if (radio.selected)
            {
                const float inset = radio.paint.selectedIndicatorInset;
                add(
                    UILogicalRect{
                        .x = normalizeFloat(layoutEntry.worldRect.x + inset),
                        .y = normalizeFloat(layoutEntry.worldRect.y + inset),
                        .width = normalizeFloat(extent - inset * 2.0F),
                        .height = normalizeFloat(extent - inset * 2.0F),
                    },
                    controlColor(radio.paint.selectedIndicatorColor));
            }
        }

        if (batchCapacityExceeded)
        {
            return fail(Core::CoreErrorCode::Internal, "UI control paint primitive batch capacity has been exhausted");
        }
        return batch;
    }

    [[nodiscard]] Detail::UITextEditPaintState resolveTextEditPaintState(UINodeId node,
                                                                         bool applyDisabledOpacity) const noexcept
    {
        const u32 nodeIndex = node.index();
        const NodeRecord* record = recordByIndex(nodeIndex);
        const bool focused = record != nullptr && record->kind == BuiltinElementKind::TextEdit &&
                             isNodeEnabled(node) && textInputFocus == node && isLiveTextEdit(textInputFocus);
        const WidgetTextState* textState =
            nodeIndex < textStatesByIndex.size() ? &textStatesByIndex[nodeIndex] : nullptr;
        const UITextStyle style = textState != nullptr ? textState->style : UITextStyle{};
        const UIPremultipliedRgba8Color textColor =
            applyDisabledOpacity && nodeIndex < localTextColorCacheByIndex.size()
                ? widgetPaintColor(node, localTextColorCacheByIndex[nodeIndex])
                : (textState != nullptr ? premultiply(style.color) : UIPremultipliedRgba8Color{});
        const bool preeditActive = focused && imeComposition.active();
        const UITextEditPaint paint =
            nodeIndex < textEditPaintsByNodeIndex.size() ? textEditPaintsByNodeIndex[nodeIndex]
                                                         : UITextEditPaint{};
        const Detail::UITextInputState* textInputState = behaviorStateStorage.tryTextInputState(nodeIndex);
        return Detail::UITextEditPaintState{
            .focused = focused,
            .preeditActive = preeditActive,
            .committedText = presentationTextViewFor(nodeIndex),
            .selection = focused && textInputState != nullptr ? textInputState->selection : UITextSelection{},
            .preeditText = preeditActive ? imeComposition.preeditUtf8() : std::string_view{},
            .preeditCursorCodepoint = preeditActive ? imeComposition.cursorCodepoint() : 0,
            .style = style,
            .textColor = textColor,
            .selectionColor = premultiply(paint.selectionBackgroundColor),
            .caretColor = premultiply(paint.caretColor),
            .rasterSource =
                Detail::UITextPaintRasterSource{
                    .rasterizer = textRasterizer.get(),
                    .face = textFace,
                    .atlas = glyphAtlas.get(),
                },
        };
    }

    [[nodiscard]] Core::Result<usize> countPaintEntries(const UICommittedLayoutEntry& layoutEntry) const
    {
        usize paintEntryCount = 0;
        if (layoutEntry.effectiveVisibility != UIVisibility::Visible)
        {
            return paintEntryCount;
        }
        if (!contains(layoutEntry.node))
        {
            return fail(UIErrorCode::InvalidNode, "UI paint snapshot layout references a stale node");
        }

        const u32 nodeIndex = layoutEntry.node.index();
        const UIBoxPaint boxPaint = resolvedBoxChrome(layoutEntry.node, nodeIndex);
        const UIPremultipliedRgba8Color resolvedFill =
            resolvedBoxFillCacheByNodeIndex[nodeIndex];
        paintEntryCount += countBoxChromePaintEntries(boxPaint, layoutEntry.worldRect, !resolvedFill.isTransparent());
        paintEntryCount += countCanvasPaintEntries(layoutEntry);
        if (const UIImageContent* image = imageContentStorage.get(nodeIndex);
            image != nullptr && image->tint.alpha != 0 &&
            layoutEntry.contentPlacement.contentBox.width > 0.0F &&
            layoutEntry.contentPlacement.contentBox.height > 0.0F)
        {
            ++paintEntryCount;
        }
        auto controlPaintBatch = resolveControlPaintBatch(layoutEntry, false);
        if (!controlPaintBatch)
        {
            return Core::failure(controlPaintBatch.error());
        }
        paintEntryCount += controlPaintBatch->size();
        paintEntryCount +=
            Detail::UITextEditPaintEmitter::countEntries(resolveTextEditPaintState(layoutEntry.node, false));
        return paintEntryCount;
    }

    void refreshLocalPaintCache(u32 nodeIndex) noexcept
    {
        const UIBoxPaint& paint = boxPaintsByIndex[nodeIndex];
        localSolidFillCacheByIndex[nodeIndex] =
            paint.solidFill.has_value() ? premultiply(paint.solidFill->color) : UIPremultipliedRgba8Color{};
        localTextColorCacheByIndex[nodeIndex] =
            nodeIndex < textStatesByIndex.size() && textStatesByIndex[nodeIndex].hasContent
                ? premultiply(textStatesByIndex[nodeIndex].style.color)
                : UIPremultipliedRgba8Color{};
    }

    struct PaintCacheRebuildStatistics final {
        usize paintCacheRebuildCount = 0;
        usize styleInspectedNodeCount = 0;
        usize styleResolvedNodeCount = 0;
        usize styleCandidateRuleCount = 0;
    };

    [[nodiscard]] PaintCacheRebuildStatistics
    rebuildDirtyPaintCaches(
        std::span<const UICommittedLayoutEntry> layoutEntries,
        StyleInteractionNodeSet& interactionCandidates) noexcept
    {
        PaintCacheRebuildStatistics statistics{};
        interactionCandidates = committedStyleInteractionNodes;
        interactionCandidates.merge(currentStyleInteractionNodes());
        const auto rebuildNode = [this, &statistics](u32 nodeIndex) noexcept {
            ++statistics.styleInspectedNodeCount;
            refreshLocalPaintCache(nodeIndex);
            statistics.styleCandidateRuleCount += refreshResolvedStyleCache(nodeIndex);
            ++statistics.paintCacheRebuildCount;
            ++statistics.styleResolvedNodeCount;
        };
        for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
        {
            const u32 nodeIndex = layoutEntry.node.index();
            if (nodeIndex >= dirtyQueueStorage.nodeCapacity() ||
                nodeIndex >= styleStatesByNodeIndex.size())
            {
                continue;
            }

            const NodeRecord* record = recordByIndex(nodeIndex);
            const UIDirty dirty = dirtyQueueStorage.flags(nodeIndex);
            const bool paintDirty = hasDirty(dirty, UIDirty::Paint);
            const bool virtualCollectionOwner =
                record != nullptr &&
                (record->kind == BuiltinElementKind::ListView ||
                 record->kind == BuiltinElementKind::TreeView);
            if (paintDirty)
            {
                rebuildNode(nodeIndex);
            }
            if (!virtualCollectionOwner ||
                !hasDirty(dirty, UIDirty::Style | UIDirty::Paint))
            {
                continue;
            }

            // Virtual collection selection and row rebinding dirty the owner.
            // Refresh its bounded materialized row pool without turning every
            // paint-only state update into a full-tree style scan.
            for (u32 childIndex = record->firstChildIndex;
                 childIndex != InvalidNodeIndex;)
            {
                const NodeRecord* child = recordByIndex(childIndex);
                if (child == nullptr)
                {
                    break;
                }
                const u32 nextSiblingIndex = child->nextSiblingIndex;
                const bool materializedRow =
                    child->kind == BuiltinElementKind::ListViewItem ||
                    child->kind == BuiltinElementKind::TreeViewItem;
                const bool childAlreadyDirty =
                    childIndex < dirtyQueueStorage.nodeCapacity() &&
                    hasDirty(dirtyQueueStorage.flags(childIndex), UIDirty::Paint);
                if (materializedRow && !childAlreadyDirty)
                {
                    rebuildNode(childIndex);
                }
                childIndex = nextSiblingIndex;
            }
        }

        for (usize index = 0; index < interactionCandidates.count; ++index)
        {
            const UINodeId node = interactionCandidates.nodes[index];
            if (!contains(node) || node.index() >= styleStatesByNodeIndex.size())
            {
                continue;
            }
            const UIStyleState states = deriveStyleState(node, node.index());
            if (styleStatesByNodeIndex[node.index()] != states)
            {
                rebuildNode(node.index());
            }
        }
        return statistics;
    }

    void appendTextGlyphPaints(std::pmr::vector<UICommittedPaintEntry>& output,
                               const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal) noexcept
    {
        Detail::UITextEditPaintEmitter::append(output, layoutEntry, nextPaintOrdinal,
                                               resolveTextEditPaintState(layoutEntry.node, true));
    }

    [[nodiscard]] static UILogicalRect resolveImageDestination(
        const UICommittedContentPlacement& placement, const UIImageContent& image) noexcept
    {
        const UILogicalRect box = placement.contentBox;
        const UILogicalSize intrinsic = image.source.intrinsicLogicalSize;
        if (box.width <= 0.0F || box.height <= 0.0F)
        {
            return {};
        }
        if (image.fit == UIImageFit::Fill)
        {
            return box;
        }

        float scale = 1.0F;
        if (image.fit == UIImageFit::Contain || image.fit == UIImageFit::Cover)
        {
            const float horizontalScale = box.width / intrinsic.width;
            const float verticalScale = box.height / intrinsic.height;
            scale = image.fit == UIImageFit::Contain
                        ? (std::min)(horizontalScale, verticalScale)
                        : (std::max)(horizontalScale, verticalScale);
        }
        const float width = normalizeFloat(intrinsic.width * scale);
        const float height = normalizeFloat(intrinsic.height * scale);
        const auto alignedOffset = [](UIAxisAlignment axis, float freeSpace) noexcept {
            switch (axis)
            {
            case UIAxisAlignment::Center:
                return freeSpace * 0.5F;
            case UIAxisAlignment::End:
                return freeSpace;
            case UIAxisAlignment::Start:
            case UIAxisAlignment::Stretch:
                return 0.0F;
            }
            return 0.0F;
        };
        return UILogicalRect{
            .x = normalizeFloat(box.x + alignedOffset(image.alignment.horizontal, box.width - width)),
            .y = normalizeFloat(box.y + alignedOffset(image.alignment.vertical, box.height - height)),
            .width = width,
            .height = height,
        };
    }

    void appendImagePaint(std::pmr::vector<UICommittedPaintEntry>& output,
                          const UICommittedLayoutEntry& layoutEntry,
                          u32& nextPaintOrdinal) const noexcept
    {
        const u32 nodeIndex = layoutEntry.node.index();
        const UIImageContent* image = imageContentStorage.get(nodeIndex);
        const NodeRecord* record = recordByIndex(nodeIndex);
        if (image == nullptr || record == nullptr || image->tint.alpha == 0)
        {
            return;
        }
        const UILogicalRect destination = resolveImageDestination(layoutEntry.contentPlacement, *image);
        if (destination.width <= 0.0F || destination.height <= 0.0F)
        {
            return;
        }
        output.push_back(UICommittedPaintEntry{
            .node = layoutEntry.node,
            .root = idForIndex(record->rootIndex),
            .worldRect = destination,
            .effectiveClip = intersectRects(layoutEntry.effectiveClip,
                                            layoutEntry.contentPlacement.contentBox),
            .paintOrdinal = nextPaintOrdinal,
            .solidFill = premultiply(image->tint),
            .kind = UICommittedPaintKind::Image,
            .imageSource = image->source,
            .imageSampling = image->sampling,
        });
        ++nextPaintOrdinal;
    }

    [[nodiscard]] Core::Status appendPaintEntries(std::pmr::vector<UICommittedPaintEntry>& output,
                                                  const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal)
    {
        const u32 nodeIndex = layoutEntry.node.index();
        const UIBoxPaint boxPaint = resolvedBoxChrome(layoutEntry.node, nodeIndex);
        const UIPremultipliedRgba8Color fill =
            resolvedBoxFillCacheByNodeIndex[nodeIndex];
        appendBoxChromePaints(output, layoutEntry.node, layoutEntry.worldRect, layoutEntry.effectiveClip,
                              nextPaintOrdinal, boxPaint, fill);
        appendCanvasPaints(output, layoutEntry, nextPaintOrdinal);
        appendImagePaint(output, layoutEntry, nextPaintOrdinal);
        auto controlPaintBatch = resolveControlPaintBatch(layoutEntry, true);
        if (!controlPaintBatch)
        {
            return Core::failure(controlPaintBatch.error());
        }
        controlPaintBatch->appendTo(output, layoutEntry.node, layoutEntry.effectiveClip, nextPaintOrdinal);
        appendTextGlyphPaints(output, layoutEntry, nextPaintOrdinal);
        return Core::success();
    }

    [[nodiscard]] static constexpr Detail::UIPaintSnapshotSourceAdapter paintSnapshotSourceAdapter() noexcept
    {
        return Detail::UIPaintSnapshotSourceAdapter{
            .countEntries = [](const void* context, const UICommittedLayoutEntry& layoutEntry) -> Core::Result<usize> {
                return static_cast<const Impl*>(context)->countPaintEntries(layoutEntry);
            },
            .appendEntries = [](void* context, std::pmr::vector<UICommittedPaintEntry>& output,
                                const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal) -> Core::Status {
                return static_cast<Impl*>(context)->appendPaintEntries(output, layoutEntry, nextPaintOrdinal);
            },
        };
    }

    [[nodiscard]] Core::Result<usize>
    validatePaintCandidateCapacity(std::span<const UICommittedLayoutEntry> layoutEntries) const
    {
        return paintSnapshotBuilder.validateCapacity(layoutEntries, this, paintSnapshotSourceAdapter());
    }

    [[nodiscard]] Core::Status buildCommittedPaint(std::pmr::vector<UICommittedPaintEntry>& output,
                                                   std::span<const UICommittedLayoutEntry> layoutEntries)
    {
        return paintSnapshotBuilder.build(output, layoutEntries, this, paintSnapshotSourceAdapter());
    }

    [[nodiscard]] bool resolveSemanticsSnapshotSource(UINodeId node, Detail::UISemanticsSnapshotSource& source) noexcept
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
        } else if (record->kind == BuiltinElementKind::DropdownItem)
        {
            entry.selected = isSelectedDropdownItem(node);
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

    [[nodiscard]] Core::Status buildCommittedSemantics(std::pmr::vector<UISemanticsEntry>& output,
                                                       std::pmr::vector<char>& textOutput,
                                                       std::span<const UICommittedLayoutEntry> layoutEntries)
    {
        return semanticsSnapshotBuilder.build(
            output, textOutput, layoutEntries,
            Detail::UISemanticsSnapshotSourceAdapter{
                .context = this,
                .resolve = [](void* context, UINodeId node,
                              Detail::UISemanticsSnapshotSource& source) noexcept {
                    return static_cast<Impl*>(context)->resolveSemanticsSnapshotSource(node, source);
                },
            });
    }

    [[nodiscard]] Core::Status validateLayoutCandidate(const std::pmr::vector<u32>& order) const
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

    [[nodiscard]] bool isPhaseDirty(UIDirty flags) const noexcept
    {
        return hasDirty(phaseDirty, flags);
    }

    [[nodiscard]] Core::Status publishStructureIfDirty()
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

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == ownerThreadId;
    }

    [[nodiscard]] Core::Status ensureOwnerThread() const
    {
        if (!isOwnerThread())
        {
            return fail(UIErrorCode::WrongOwnerThread, "UI context was accessed from a non-owner thread");
        }
        return Core::success();
    }

    void publishRoutedPointerListenerTokenState(u32 slot, u32 generation, bool active) noexcept
    {
        if (lifetime)
        {
            lifetime->publishRoutedPointerListenerState(slot, generation, active);
        }
    }

    static void publishRoutedPointerListenerTokenStateFromRegistry(void* context, u32 slot,
                                                                   u32 generation, bool active) noexcept
    {
        if (context != nullptr)
        {
            static_cast<Impl*>(context)->publishRoutedPointerListenerTokenState(slot, generation, active);
        }
    }

    [[nodiscard]] Detail::UIRoutedPointerListenerStatePublisher
    routedPointerListenerStatePublisher() noexcept
    {
        return Detail::UIRoutedPointerListenerStatePublisher{
            .context = this,
            .publish = &Impl::publishRoutedPointerListenerTokenStateFromRegistry,
        };
    }

    void reclaimInactiveRoutedPointerListeners() noexcept
    {
        routedPointerListenerRegistry.reclaim(routeDispatchDepth != 0);
    }

    void deactivateRoutedPointerListener(u32 listenerIndex, u32 generation,
                                         bool publishTokenState) noexcept
    {
        const auto statePublisher = publishTokenState ? routedPointerListenerStatePublisher()
                                                      : Detail::UIRoutedPointerListenerStatePublisher{};
        static_cast<void>(routedPointerListenerRegistry.deactivate(
            listenerIndex, generation, statePublisher, routeDispatchDepth != 0));
    }

    void deactivateAllRoutedPointerListenersForNode(u32 nodeIndex) noexcept
    {
        routedPointerListenerRegistry.clearNode(nodeIndex, routedPointerListenerStatePublisher());
    }

    void drainDeferredRoutedPointerListenerReleases() noexcept
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

    void drainDeferredRootDestroys() noexcept
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

    [[nodiscard]] UINodeId idForIndex(u32 index) const noexcept
    {
        if (index == InvalidNodeIndex || index >= idsByIndex.size())
        {
            return {};
        }
        return idsByIndex[index];
    }

    [[nodiscard]] NodeRecord* recordByIndex(u32 index) noexcept
    {
        return nodes.tryGet(idForIndex(index).storageId());
    }

    [[nodiscard]] const NodeRecord* recordByIndex(u32 index) const noexcept
    {
        return nodes.tryGet(idForIndex(index).storageId());
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveNode(UINodeId node)
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

    [[nodiscard]] Core::Result<NodeRecord*> resolveParent(UINodeId parent)
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

    [[nodiscard]] bool contains(UINodeId node) const noexcept
    {
        return node.hasValue() && node.ownerWindow() == ownerWindow && node.storageId().owner() == nodes.owner() &&
               nodes.contains(node.storageId());
    }

    [[nodiscard]] bool isNodeEnabled(UINodeId node) const noexcept
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
        return true;
    }

    [[nodiscard]] bool isCandidateNodeEnabled(UINodeId node) const noexcept
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
        return true;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveButton(UINodeId button)
    {
        auto nodeResult = resolveNode(button);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!behaviorStateStorage.hasActivate(button.index()) ||
            (*nodeResult)->kind == BuiltinElementKind::ListViewItem ||
            (*nodeResult)->kind == BuiltinElementKind::TreeViewItem)
        {
            return fail(UIErrorCode::InvalidButtonAction,
                        "UI action requires an Activate-capable non-virtual Element");
        }
        return *nodeResult;
    }

    [[nodiscard]] bool isButtonPressed(UINodeId node) const noexcept
    {
        return (armedPrimaryButton == node && armedPrimaryButtonPressed) ||
               defaultActionPressState.isPressed(node);
    }

    void clearArmedPrimaryButton() noexcept
    {
        armedPrimaryButton = {};
        armedPrimaryButtonPressed = false;
        armedTreeDisclosure = false;
    }

    void clearHoveredPrimaryControl() noexcept
    {
        hoveredPrimaryControl = {};
    }

    [[nodiscard]] UINodeId resolvedHoveredPrimaryControl(UINodeId candidate) const noexcept
    {
        if (candidate.hasValue() && isNodeEnabled(candidate))
        {
            const NodeRecord* record = nodes.tryGet(candidate.storageId());
            if (record != nullptr &&
                (isButtonChromeKind(record->kind) || record->kind == BuiltinElementKind::Checkbox ||
                 record->kind == BuiltinElementKind::RadioButton || record->kind == BuiltinElementKind::TextEdit))
            {
                return candidate;
            }
        }
        return {};
    }

    [[nodiscard]] Core::Status updateHoveredPrimaryControl(UINodeId candidate)
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

    void clearArmedSlider() noexcept
    {
        armedSlider = {};
    }

    void clearArmedScrollView() noexcept
    {
        armedScrollView = {};
        armedScrollAxis = UIScrollAxes::None;
        scrollDragGrabOffset = 0.0F;
        scrollThumbDragActive = false;
    }

    void clearArmedTextEdit() noexcept
    {
        armedTextEdit = {};
    }

    void clearDefaultActionFocus() noexcept
    {
        defaultActionFocusButton = {};
        defaultActionPressState.clearAll();
    }

    void resetImeCompositionState() noexcept
    {
        imeComposition.reset();
    }

    [[nodiscard]] Core::Status clearImeComposition()
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

    void clearImeFocus() noexcept
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
        if (defaultActionFocusButton == previousFocus)
        {
            clearDefaultActionFocus();
        }
        if (previousFocus.hasValue() && contains(previousFocus))
        {
            static_cast<void>(markPaintDirty(previousFocus));
        }
    }

    [[nodiscard]] bool isLiveTextEdit(UINodeId node) const noexcept
    {
        if (!node.hasValue() || !contains(node))
        {
            return false;
        }
        const NodeRecord* record = nodes.tryGet(node.storageId());
        return record != nullptr && record->kind == BuiltinElementKind::TextEdit;
    }

    [[nodiscard]] bool isPointerInteractionCandidate(UINodeId node, std::span<const UICommittedHitEntry> entries,
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

    [[nodiscard]] bool isPointerCaptureCandidate(UINodeId node, std::span<const UICommittedHitEntry> entries,
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

    [[nodiscard]] bool isKeyboardFocusCandidate(UINodeId node, std::span<const UICommittedHitEntry> entries,
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

    [[nodiscard]] bool isCommittedKeyboardFocusCandidate(UINodeId node) const noexcept
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

    [[nodiscard]] bool isCommittedTextEditFocusCandidate(UINodeId node) const noexcept
    {
        return isLiveTextEdit(node) && isCommittedKeyboardFocusCandidate(node) && defaultActionFocusButton == node;
    }

    void deactivateButtonActionForNode(u32 nodeIndex) noexcept
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
    captureButtonAction(UINodeId button, u64 registrationSerialBoundary) const noexcept
    {
        if (!contains(button))
        {
            return {};
        }
        return buttonActionRegistry.capture(button, registrationSerialBoundary);
    }

    void invokeButtonAction(Detail::UIButtonActionInvocation candidate, const UIButtonActionEvent& event,
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
    captureSliderChangeCallback(UINodeId slider) const noexcept
    {
        if (!contains(slider))
        {
            return {};
        }
        return sliderChangeCallbackRegistry.capture(slider);
    }

    void invokeSliderChangeCallback(Detail::UISliderChangeCallbackInvocation candidate,
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

    void clearTextState(u32 index) noexcept
    {
        if (index >= textStatesByIndex.size())
        {
            return;
        }
        WidgetTextState& state = textStatesByIndex[index];
        textStorage.release(state.allocation);
        state = {};
    }

    void clearSemanticsState(u32 index) noexcept
    {
        if (index >= semanticsStatesByNodeIndex.size())
        {
            return;
        }
        SemanticsState& state = semanticsStatesByNodeIndex[index];
        textStorage.release(state.textAllocation);
        state = {};
    }

    [[nodiscard]] std::string_view semanticsNameViewFor(u32 index) const noexcept
    {
        if (index >= semanticsStatesByNodeIndex.size())
        {
            return {};
        }
        const SemanticsState& state = semanticsStatesByNodeIndex[index];
        return textStorage.view(
            TextByteAllocation{
                .offset = state.textAllocation.offset,
                .capacity = state.nameLength,
            },
            state.nameLength);
    }

    [[nodiscard]] std::string_view semanticsDescriptionViewFor(u32 index) const noexcept
    {
        if (index >= semanticsStatesByNodeIndex.size())
        {
            return {};
        }
        const SemanticsState& state = semanticsStatesByNodeIndex[index];
        return textStorage.view(
            TextByteAllocation{
                .offset = state.textAllocation.offset + state.nameLength,
                .capacity = state.descriptionLength,
            },
            state.descriptionLength);
    }

    [[nodiscard]] std::string_view semanticsNameSourceFor(u32 index) const noexcept
    {
        if (index >= semanticsStatesByNodeIndex.size())
        {
            return {};
        }
        const SemanticsState& state = semanticsStatesByNodeIndex[index];
        if (state.hasExplicitName)
        {
            return semanticsNameViewFor(index);
        }
        return state.useContentAsName ? textViewFor(index) : std::string_view{};
    }

    [[nodiscard]] std::string_view textViewFor(u32 index) const noexcept
    {
        if (index >= textStatesByIndex.size())
        {
            return {};
        }
        const WidgetTextState& state = textStatesByIndex[index];
        if (!state.hasContent || state.length == 0 || state.allocation.capacity == 0)
        {
            return {};
        }
        return textStorage.view(state.allocation, state.length);
    }

    [[nodiscard]] UINodeId dropdownForPopup(UINodeId popup) const noexcept
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

    [[nodiscard]] UINodeId popupForDropdown(UINodeId dropdown) const noexcept
    {
        if (!contains(dropdown) || dropdown.index() >= dropdownStatesByNodeIndex.size())
        {
            return {};
        }
        const NodeRecord* record = nodes.tryGet(dropdown.storageId());
        const UINodeId popup = dropdownStatesByNodeIndex[dropdown.index()].popup;
        return record != nullptr && record->kind == BuiltinElementKind::Dropdown && contains(popup) ? popup : UINodeId{};
    }

    [[nodiscard]] UINodeId dropdownForItem(UINodeId item) const noexcept
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

    [[nodiscard]] bool isSelectedDropdownItem(UINodeId item) const noexcept
    {
        const UINodeId dropdown = dropdownForItem(item);
        const Detail::UISelectBehaviorState* select =
            dropdown.hasValue() ? behaviorStateStorage.trySelectState(dropdown.index()) : nullptr;
        return select != nullptr && select->selectedOption == item;
    }

    [[nodiscard]] std::string_view presentationTextViewFor(u32 index) const noexcept
    {
        const NodeRecord* record = recordByIndex(index);
        if (record != nullptr && record->kind == BuiltinElementKind::Dropdown && index < dropdownStatesByNodeIndex.size())
        {
            const Detail::UISelectBehaviorState* select = behaviorStateStorage.trySelectState(index);
            const UINodeId selected = select != nullptr ? select->selectedOption : UINodeId{};
            if (contains(selected) && selected.index() < textStatesByIndex.size() &&
                textStatesByIndex[selected.index()].hasContent)
            {
                return textViewFor(selected.index());
            }
        }
        return textViewFor(index);
    }

    [[nodiscard]] const UITextMetrics* presentationTextMetricsFor(u32 index) const noexcept
    {
        const NodeRecord* record = recordByIndex(index);
        if (record != nullptr && record->kind == BuiltinElementKind::Dropdown && index < dropdownStatesByNodeIndex.size())
        {
            const Detail::UISelectBehaviorState* select = behaviorStateStorage.trySelectState(index);
            const UINodeId selected = select != nullptr ? select->selectedOption : UINodeId{};
            if (contains(selected) && selected.index() < textStatesByIndex.size() &&
                textStatesByIndex[selected.index()].hasContent)
            {
                return &textStatesByIndex[selected.index()].metrics;
            }
        }
        return index < textStatesByIndex.size() && textStatesByIndex[index].hasContent
                   ? &textStatesByIndex[index].metrics
                   : nullptr;
    }

    [[nodiscard]] Core::Result<UITextMetrics> measureWidgetText(std::string_view utf8, const UITextStyle& style)
    {
        if (textRasterizer && textFace.hasValue())
        {
            return textRasterizer->measure(textFace, utf8, style);
        }
        // Fallback keeps measure available when a custom FreeType rasterizer is
        // injected before any face is opened.
        return measurePlaceholderText(utf8, style);
    }

    void resetNodeSideData(u32 index) noexcept
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
            resolvedBoxFillCacheByNodeIndex[index] = {};
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

    void markStructureChanged() noexcept
    {
        phaseDirty |= PhaseStructure | PhaseLayout | PhaseHit;
        layoutReuseCacheValid = false;
    }

    [[nodiscard]] Detail::UIDirtyQueueEntryDisposition classifyDirtyQueueEntry(UINodeId queued) const noexcept
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

    [[nodiscard]] Detail::UIDirtyQueueEntryClassifier dirtyQueueEntryClassifier() const noexcept
    {
        return Detail::UIDirtyQueueEntryClassifier{
            .context = this,
            .classify = [](const void* context, UINodeId queued) noexcept {
                return static_cast<const Impl*>(context)->classifyDirtyQueueEntry(queued);
            },
        };
    }

    void compactDirtyQueue() noexcept
    {
        dirtyQueueStorage.compact(dirtyQueueEntryClassifier());
    }

    [[nodiscard]] usize validDirtyQueueCount() const noexcept
    {
        return dirtyQueueStorage.validCount(dirtyQueueEntryClassifier());
    }

    [[nodiscard]] usize occupiedDirtyQueueSlotCount() const noexcept
    {
        return dirtyQueueStorage.occupiedSlotCount();
    }

    void addRouteDirtyReservationCandidate(UINodeId node)
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

    void addRouteLayoutDirtyReservationCandidates(UINodeId node)
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

    [[nodiscard]] Core::Status reserveRouteDirtyQueueSlots()
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

    void releaseRouteDirtyQueueReservations() noexcept
    {
        dirtyQueueStorage.releaseRouteReservations();
    }

    [[nodiscard]] Core::Status markLayoutDirtyBatch(std::initializer_list<UINodeId> requestedNodes)
    {
        layoutOrderScratch.clear();
        for (const UINodeId requested : requestedNodes)
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
        for (const u32 dirtyIndex : layoutOrderScratch)
        {
            if (!dirtyQueueStorage.isQueued(dirtyIndex))
            {
                dirtyQueueStorage.enqueue(idForIndex(dirtyIndex));
            }
            const bool directlyRequested = std::any_of(
                requestedNodes.begin(), requestedNodes.end(),
                [dirtyIndex](UINodeId requested) noexcept {
                    return requested.hasValue() && requested.index() == dirtyIndex;
                });
            dirtyQueueStorage.flags(dirtyIndex) |= directlyRequested ? ChangedNodeDirty : AncestorDirty;
        }
        phaseDirty |= PhaseLayout | PhaseHit;
        return Core::success();
    }

    [[nodiscard]] Core::Status markHitTestDirty(UINodeId node)
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

    [[nodiscard]] Core::Status preflightPaintDirtyBatch(std::initializer_list<UINodeId> requestedNodes) const
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

    [[nodiscard]] Core::Status markPaintDirtyBatch(std::initializer_list<UINodeId> requestedNodes)
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

    [[nodiscard]] Core::Status markPaintDirty(UINodeId node)
    {
        return markPaintDirtyBatch({node});
    }

    void clearDirtyState() noexcept
    {
        dirtyQueueStorage.clearQueuedDirtyState();
        phaseDirty = UIDirty::None;
    }

    [[nodiscard]] bool isNodeWithinRoot(UINodeId root, UINodeId node) const noexcept
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

    [[nodiscard]] bool isNodeWithinSubtree(UINodeId subtreeRoot, UINodeId node) const noexcept
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

    [[nodiscard]] ProductChromeStorage productChromeStorageFor(u32 index) noexcept
    {
        return {
            .box = boxPaintsByIndex[index],
            .text = textStatesByIndex[index].style,
            .button = buttonPaintsByNodeIndex[index],
            .checkbox = checkboxPaintsByNodeIndex[index],
            .slider = sliderPaintsByNodeIndex[index],
            .progressBar = progressBarStatesByNodeIndex[index].paint,
            .radioButton = radioButtonStatesByNodeIndex[index].paint,
            .scrollView = scrollViewPaintsByNodeIndex[index],
            .dropdown = dropdownStatesByNodeIndex[index].paint,
            .listView = listViewStatesByNodeIndex[index].paint,
            .treeView = treeViewStatesByNodeIndex[index].paint,
            .textEdit = textEditPaintsByNodeIndex[index],
        };
    }

    void applyProductChromeTransition(u32 index, UIStyleRoleId role, const UITheme& theme,
                                      u16 affectedBindings, u16 targetBindings) noexcept
    {
        if (index >= themeBindingsByNodeIndex.size())
        {
            return;
        }
        ProductChromeStorage storage = productChromeStorageFor(index);
        const Detail::ProductChromeTransition transition = Detail::resolveProductChromeTransition(
            storage, role, theme, affectedBindings, targetBindings);
        Detail::applyProductChromeTransition(storage, transition, affectedBindings);
    }

    void applyDefaultProductChrome(u32 index, UIStyleRoleId role) noexcept
    {
        if (index >= themeBindingsByNodeIndex.size())
        {
            return;
        }
        const u16 bindings = defaultThemeBindingsFor(role);
        themeBindingsByNodeIndex[index] = bindings;
        applyProductChromeTransition(index, role, productTheme, bindings, bindings);
    }

    void stageThemePaintChange(u32 index) noexcept
    {
        themeDirtyScratchByNodeIndex[index] |= ThemeDirtyPaint;
    }

    void detachThemeBinding(u32 index, u16 binding) noexcept
    {
        if (index < themeBindingsByNodeIndex.size())
        {
            themeBindingsByNodeIndex[index] &= static_cast<u16>(~binding);
            styleOverridesByNodeIndex[index] |= binding;
        }
    }

    [[nodiscard]] Core::Status stageThemeTextStyle(u32 index, const UITextStyle& nextStyle)
    {
        WidgetTextState& state = textStatesByIndex[index];
        if (state.style == nextStyle)
        {
            return Core::success();
        }
        stageThemePaintChange(index);
        if (!state.hasContent || !Detail::textMeasureInputsDiffer(state.style, nextStyle))
        {
            themeTextMetricsScratchByNodeIndex[index] = state.metrics;
            return Core::success();
        }
        auto measured = measureWidgetText(textViewFor(index), nextStyle);
        if (!measured)
        {
            return Core::failure(measured.error());
        }
        themeTextMetricsScratchByNodeIndex[index] = *measured;
        if (*measured != state.metrics)
        {
            themeDirtyScratchByNodeIndex[index] |= ThemeDirtyLayoutSelf;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status stageProductChromeTransition(u32 index, UIStyleRoleId role,
                                                            const UITheme& theme, u16 affectedBindings,
                                                            u16 targetBindings)
    {
        if (index >= themeBindingsByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI Theme node index is out of range");
        }

        ProductChromeStorage storage = productChromeStorageFor(index);
        const Detail::ProductChromeTransition transition = Detail::resolveProductChromeTransition(
            storage, role, theme, affectedBindings, targetBindings);
        if ((affectedBindings & ThemeBindingTextStyle) != 0)
        {
            if (Core::Status status = stageThemeTextStyle(index, transition.target.text); !status)
            {
                return status;
            }
        }

        constexpr u16 NonTextBindings = static_cast<u16>(~ThemeBindingTextStyle);
        if ((transition.changedBindings & NonTextBindings) != 0)
        {
            stageThemePaintChange(index);
        }
        if ((transition.layoutAffectingBindings & NonTextBindings) != 0)
        {
            themeDirtyScratchByNodeIndex[index] |= ThemeDirtyLayoutSelf;
        }
        return Core::success();
    }

    void applyStagedProductChromeTransition(u32 index, UIStyleRoleId role, const UITheme& theme,
                                            u16 affectedBindings, u16 targetBindings) noexcept
    {
        ProductChromeStorage storage = productChromeStorageFor(index);
        const Detail::ProductChromeTransition transition = Detail::resolveProductChromeTransition(
            storage, role, theme, affectedBindings, targetBindings);
        if ((affectedBindings & ThemeBindingTextStyle) != 0)
        {
            WidgetTextState& textState = textStatesByIndex[index];
            if (textState.hasContent && Detail::textMeasureInputsDiffer(textState.style, transition.target.text))
            {
                textState.metrics = themeTextMetricsScratchByNodeIndex[index];
            }
        }
        Detail::applyProductChromeTransition(storage, transition, affectedBindings);
    }

    void propagateThemeLayoutDirtyToAncestors() noexcept
    {
        for (u32 index = 0; index < themeDirtyScratchByNodeIndex.size(); ++index)
        {
            if ((themeDirtyScratchByNodeIndex[index] & ThemeDirtyLayoutSelf) == 0)
            {
                continue;
            }
            const NodeRecord* record = recordByIndex(index);
            u32 parentIndex = record == nullptr ? InvalidNodeIndex : record->parentIndex;
            while (parentIndex != InvalidNodeIndex)
            {
                themeDirtyScratchByNodeIndex[parentIndex] |= ThemeDirtyLayoutAncestor;
                record = recordByIndex(parentIndex);
                parentIndex = record == nullptr ? InvalidNodeIndex : record->parentIndex;
            }
        }
    }

    [[nodiscard]] Core::Status preflightThemeDirtyQueue()
    {
        compactDirtyQueue();
        usize requiredQueueEntries = 0;
        for (u32 index = 0; index < themeDirtyScratchByNodeIndex.size(); ++index)
        {
            if (themeDirtyScratchByNodeIndex[index] != 0 && !dirtyQueueStorage.isQueued(index) &&
                !dirtyQueueStorage.isReserved(index))
            {
                ++requiredQueueEntries;
            }
        }
        const usize occupiedQueueEntries = occupiedDirtyQueueSlotCount();
        if (occupiedQueueEntries > dirtyQueueStorage.queueCapacity() ||
            requiredQueueEntries > dirtyQueueStorage.queueCapacity() - occupiedQueueEntries)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI Theme update exceeds dirty queue capacity");
        }
        return Core::success();
    }

    void publishThemeDirtyState() noexcept
    {
        constexpr UIDirty ChangedNodeLayoutDirty = UIDirty::Style | UIDirty::Measure | UIDirty::Arrange |
                                                   UIDirty::Composite | UIDirty::HitTest | UIDirty::Semantics;
        constexpr UIDirty AncestorLayoutDirty =
            UIDirty::Measure | UIDirty::Arrange | UIDirty::Composite | UIDirty::HitTest;
        bool hasLayoutChange = false;
        bool hasPaintChange = false;
        for (u32 index = 0; index < themeDirtyScratchByNodeIndex.size(); ++index)
        {
            const u8 staged = themeDirtyScratchByNodeIndex[index];
            if (staged == 0)
            {
                continue;
            }
            if (!dirtyQueueStorage.isQueued(index))
            {
                dirtyQueueStorage.enqueue(idForIndex(index));
            }
            if ((staged & ThemeDirtyLayoutSelf) != 0)
            {
                dirtyQueueStorage.flags(index) |= ChangedNodeLayoutDirty;
                hasLayoutChange = true;
            } else if ((staged & ThemeDirtyLayoutAncestor) != 0)
            {
                dirtyQueueStorage.flags(index) |= AncestorLayoutDirty;
                hasLayoutChange = true;
            }
            if ((staged & ThemeDirtyPaint) != 0)
            {
                dirtyQueueStorage.flags(index) |= UIDirty::Paint | UIDirty::Semantics;
                hasPaintChange = true;
            }
        }
        if (hasLayoutChange)
        {
            phaseDirty |= PhaseLayout | PhaseHit;
        }
        if (hasPaintChange)
        {
            phaseDirty |= PhasePaint | PhaseSemantics;
        }
    }

    [[nodiscard]] Core::Status setProductTheme(const UITheme& theme)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status validation = Detail::validateProductTheme(theme); !validation)
        {
            return validation;
        }
        if (productTheme == theme)
        {
            return Core::success();
        }

        std::fill(themeDirtyScratchByNodeIndex.begin(), themeDirtyScratchByNodeIndex.end(), u8{0});
        for (u32 index = 0; index < themeBindingsByNodeIndex.size(); ++index)
        {
            const NodeRecord* record = recordByIndex(index);
            const u16 bindings = themeBindingsByNodeIndex[index];
            if (record == nullptr || bindings == 0)
            {
                continue;
            }
            if (Core::Status staged =
                    stageProductChromeTransition(index, styleRolesByNodeIndex[index], theme, bindings, bindings);
                !staged)
            {
                return staged;
            }
        }
        propagateThemeLayoutDirtyToAncestors();
        if (Core::Status capacity = preflightThemeDirtyQueue(); !capacity)
        {
            return capacity;
        }

        for (u32 index = 0; index < themeBindingsByNodeIndex.size(); ++index)
        {
            const NodeRecord* record = recordByIndex(index);
            const u16 bindings = themeBindingsByNodeIndex[index];
            if (record == nullptr || bindings == 0)
            {
                continue;
            }
            const UIStyleRoleId role = styleRolesByNodeIndex[index];
            applyStagedProductChromeTransition(index, role, theme, bindings, bindings);
        }
        productTheme = theme;
        publishThemeDirtyState();
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIStyleClassId> registerStyleClass()
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (styleRegistrationClosed)
        {
            return fail(UIErrorCode::InvalidStyle,
                        "UI style classes must be registered before creating retained nodes");
        }
        return styleSheetStorage.registerClass();
    }

    [[nodiscard]] Core::Result<UIStyleTokenId>
    registerStyleColorToken(UIStraightSrgba8Color value)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (styleRegistrationClosed)
        {
            return fail(UIErrorCode::InvalidStyle,
                        "UI style tokens must be registered before creating retained nodes");
        }
        return styleSheetStorage.registerColorToken(value);
    }

    [[nodiscard]] Core::Status installStyleSheet(
        std::span<const UIStyleBoxFillRule> rules)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (styleRegistrationClosed)
        {
            return fail(UIErrorCode::InvalidStyle,
                        "UI stylesheet must be installed before creating retained nodes");
        }
        return styleSheetStorage.compile(rules);
    }

    [[nodiscard]] Core::Status reserveComponentBuildStorage(
        UIComponentBuildBudget budget, UIComponentBuildReservation& reservation)
    {
        reservation = {};
        componentBuildNodeStatistics.requested += budget.nodes;
        if (componentBuildNodeStatistics.outstandingReservations > nodes.availableCount() ||
            budget.nodes > nodes.availableCount() - componentBuildNodeStatistics.outstandingReservations)
        {
            ++componentBuildNodeStatistics.capacityFailures;
            return fail(UIErrorCode::CapacityExceeded,
                        "UI component node reservation exceeds remaining capacity");
        }

        componentBuildNodeStatistics.reserved += budget.nodes;
        componentBuildNodeStatistics.outstandingReservations += budget.nodes;
        reservation.remaining.nodes = budget.nodes;
        reservation.active = true;
        auto rollback = Core::makeScopeExit([this, &reservation]() noexcept {
            releaseComponentBuildStorage(reservation);
        });

        if (budget.textBytes > (std::numeric_limits<u32>::max)())
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI component text reservation exceeds the 32-bit text arena range");
        }
        auto textReservation = textStorage.reserve(static_cast<u32>(budget.textBytes));
        if (!textReservation)
        {
            return Core::failure(textReservation.error());
        }
        reservation.text = *textReservation;
        reservation.remaining.textBytes = budget.textBytes;

        auto canvasReservation = canvasCommandStorage.reserve(budget.canvasCommands);
        if (!canvasReservation)
        {
            return Core::failure(canvasReservation.error());
        }
        reservation.canvas = *canvasReservation;
        reservation.remaining.canvasCommands = budget.canvasCommands;

        const Detail::UIBehaviorStateSlotCounts behaviorCounts =
            toBehaviorSlotCounts(budget.behaviors);
        if (Core::Status behaviorReservation = behaviorStateStorage.reserve(behaviorCounts);
            !behaviorReservation)
        {
            return behaviorReservation;
        }
        reservation.behaviors = behaviorCounts;
        reservation.remaining.behaviors = budget.behaviors;
        rollback.release();
        return Core::success();
    }

    void releaseComponentBuildStorage(UIComponentBuildReservation& reservation) noexcept
    {
        if (!reservation.active)
        {
            return;
        }
        if (reservation.remaining.nodes > componentBuildNodeStatistics.outstandingReservations)
        {
            std::terminate();
        }
        componentBuildNodeStatistics.outstandingReservations -= reservation.remaining.nodes;
        textStorage.releaseReservation(reservation.text);
        canvasCommandStorage.releaseReservation(reservation.canvas);
        behaviorStateStorage.releaseReservation(reservation.behaviors);
        reservation = {};
    }

    [[nodiscard]] UIComponentBuildReservation*
    findComponentBuildReservation(UINodeId componentRoot) noexcept
    {
        if (!componentRoot.hasValue() || componentRoot.index() >= componentBuildReservationsByNodeIndex.size())
        {
            return nullptr;
        }
        UIComponentBuildReservation& reservation =
            componentBuildReservationsByNodeIndex[componentRoot.index()];
        return reservation.active && reservation.componentRoot == componentRoot ? &reservation : nullptr;
    }

    [[nodiscard]] const UIComponentBuildReservation*
    findComponentBuildReservation(UINodeId componentRoot) const noexcept
    {
        if (!componentRoot.hasValue() || componentRoot.index() >= componentBuildReservationsByNodeIndex.size())
        {
            return nullptr;
        }
        const UIComponentBuildReservation& reservation =
            componentBuildReservationsByNodeIndex[componentRoot.index()];
        return reservation.active && reservation.componentRoot == componentRoot ? &reservation : nullptr;
    }

    [[nodiscard]] bool isBuildTransactionActive(UINodeId componentRoot) const noexcept
    {
        return findComponentBuildReservation(componentRoot) != nullptr && contains(componentRoot);
    }

    [[nodiscard]] Core::Result<TextByteAllocation> allocateRetainedText(u32 byteCount)
    {
        if (activeComponentBuildReservation == nullptr)
        {
            return textStorage.allocate(byteCount);
        }
        auto allocation = textStorage.allocateReserved(activeComponentBuildReservation->text, byteCount);
        if (allocation)
        {
            activeComponentBuildReservation->remaining.textBytes =
                activeComponentBuildReservation->text.remainingBytes();
        }
        return allocation;
    }

    [[nodiscard]] Core::Status assignRetainedCanvas(
        u32 nodeIndex, std::span<const UICanvasCommand> commands)
    {
        if (activeComponentBuildReservation == nullptr)
        {
            return canvasCommandStorage.assign(nodeIndex, commands);
        }
        Core::Status status = canvasCommandStorage.assignReserved(
            nodeIndex, commands, activeComponentBuildReservation->canvas);
        if (status)
        {
            activeComponentBuildReservation->remaining.canvasCommands =
                activeComponentBuildReservation->canvas.remaining;
        }
        return status;
    }

    [[nodiscard]] Core::Result<UINodeId> createNode(BuiltinElementKind kind,
                                                    UIElementBehavior behaviors)
    {
        if (activeComponentBuildReservation == nullptr)
        {
            const usize available = nodes.availableCount();
            if (available == 0)
            {
                return fail(UIErrorCode::CapacityExceeded,
                            "UI node capacity has been exhausted");
            }
            if (componentBuildNodeStatistics.outstandingReservations > available ||
                (componentBuildNodeStatistics.outstandingReservations != 0 &&
                 available == componentBuildNodeStatistics.outstandingReservations))
            {
                return fail(UIErrorCode::CapacityExceeded,
                            "UI node capacity is reserved by active component builds");
            }
        }
        else if (activeComponentBuildReservation->remaining.nodes == 0 ||
                 componentBuildNodeStatistics.outstandingReservations == 0)
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI component node reservation has been exhausted");
        }

        auto idResult = nodes.tryEmplace();
        if (!idResult)
        {
            const Core::Error& error = idResult.error();
            if (error.code == Core::CoreErrorCode::CapacityExceeded)
            {
                return fail(UIErrorCode::CapacityExceeded, "UI node capacity has been exhausted");
            }
            return Core::failure(error);
        }

        const UINodeId node = UINodeId::create(ownerWindow, *idResult);
        idsByIndex[node.index()] = node;
        resetNodeSideData(node.index());
        NodeRecord* record = nodes.tryGet(node.storageId());
        record->kind = kind;
        record->behaviors = behaviors;
        record->rootIndex = node.index();
        Core::Status published = activeComponentBuildReservation == nullptr
                                     ? behaviorStateStorage.publish(node.index(), behaviors)
                                     : behaviorStateStorage.publishReserved(
                                           node.index(), behaviors,
                                           activeComponentBuildReservation->behaviors);
        if (!published)
        {
            idsByIndex[node.index()] = {};
            static_cast<void>(nodes.erase(node.storageId()));
            resetNodeSideData(node.index());
            return Core::failure(published.error());
        }
        if (activeComponentBuildReservation != nullptr)
        {
            --activeComponentBuildReservation->remaining.nodes;
            --componentBuildNodeStatistics.outstandingReservations;
            ++componentBuildNodeStatistics.published;
            activeComponentBuildReservation->remaining.behaviors =
                toBehaviorSlotBudget(activeComponentBuildReservation->behaviors);
        }
        const UIStyleRoleId styleRole = defaultStyleRoleForKind(kind);
        styleRolesByNodeIndex[node.index()] = styleRole;
        const auto semanticsDefaults = defaultSemanticsForKind(kind);
        semanticsStatesByNodeIndex[node.index()] = SemanticsState{
            .mode = semanticsDefaults.mode,
            .role = semanticsDefaults.role,
            .actions = semanticsDefaults.actions,
            .useContentAsName = semanticsDefaults.useContentAsName,
            .readOnly = semanticsDefaults.readOnly,
        };
        textStatesByIndex[node.index()].alignment = defaultContentAlignment(kind);
        if (kind == BuiltinElementKind::Modal || kind == BuiltinElementKind::Popup)
        {
            focusScopeModesByNodeIndex[node.index()] = UIFocusScopeMode::Contain;
        }
        if (kind == BuiltinElementKind::Popup)
        {
            layoutStylesByIndex[node.index()].placement = UILayoutPlacement::Overlay;
        }
        styleRegistrationClosed = true;
        // Interactive controls are targetable. Label remains read-only and
        // decorative unless the caller explicitly changes its hit policy.
        pointerHitPoliciesByIndex[node.index()] =
            (hasBehavior(behaviors, UIElementBehavior::Focusable) ||
             hasBehavior(behaviors, UIElementBehavior::Activate) ||
             hasBehavior(behaviors, UIElementBehavior::RangeInput) ||
             hasBehavior(behaviors, UIElementBehavior::TextInput) ||
             hasBehavior(behaviors, UIElementBehavior::Scroll) ||
             hasBehavior(behaviors, UIElementBehavior::SelectOption))
                ? UIPointerHitPolicy::Targetable
                : UIPointerHitPolicy::Ignore;
        if (capacityConfig.applyDefaultProductChrome)
        {
            applyDefaultProductChrome(node.index(), styleRole);
        }
        return node;
    }

    [[nodiscard]] usize availableNodeCountForCurrentCreation() const noexcept
    {
        const usize available = nodes.availableCount();
        if (activeComponentBuildReservation != nullptr)
        {
            return (std::min)(available, activeComponentBuildReservation->remaining.nodes);
        }
        if (componentBuildNodeStatistics.outstandingReservations > available)
        {
            return 0;
        }
        return available - componentBuildNodeStatistics.outstandingReservations;
    }

    [[nodiscard]] Core::Status initializeSemantics(u32 index, const UISemanticsDescriptor& descriptor,
                                                   UIElementBehavior behaviors)
    {
        if (Core::Status contract =
                validateSemanticsContract(descriptor, behaviors);
            !contract)
        {
            return contract;
        }

        const std::string_view name = descriptor.name.value_or(std::string_view{});
        const std::string_view description = descriptor.description.value_or(std::string_view{});
        if (!Core::isStrictUtf8WithoutNul(name) || !Core::isStrictUtf8WithoutNul(description))
        {
            return fail(UIErrorCode::InvalidText, "UI semantics text must be strict UTF-8 without NUL");
        }
        if (name.size() > (std::numeric_limits<u32>::max)() ||
            description.size() > (std::numeric_limits<u32>::max)() - name.size())
        {
            return fail(UIErrorCode::CapacityExceeded, "UI semantics text payload is too large");
        }

        const u32 nameLength = static_cast<u32>(name.size());
        const u32 descriptionLength = static_cast<u32>(description.size());
        auto allocation = allocateRetainedText(nameLength + descriptionLength);
        if (!allocation)
        {
            return Core::failure(allocation.error());
        }
        textStorage.write(
            TextByteAllocation{
                .offset = allocation->offset,
                .capacity = nameLength,
            },
            name);
        textStorage.write(
            TextByteAllocation{
                .offset = allocation->offset + nameLength,
                .capacity = descriptionLength,
            },
            description);
        semanticsStatesByNodeIndex[index] = SemanticsState{
            .textAllocation = *allocation,
            .nameLength = nameLength,
            .descriptionLength = descriptionLength,
            .mode = descriptor.mode,
            .role = descriptor.role,
            .actions = descriptor.actions,
            .hasExplicitName = descriptor.name.has_value(),
            .useContentAsName = descriptor.useContentAsName,
            .readOnly = descriptor.readOnly,
        };
        return Core::success();
    }

    [[nodiscard]] Core::Status initializeElement(UINodeId node, BuiltinElementKind kind,
                                                 const UIElementDescriptor& descriptor,
                                                 const UILayoutStyle& normalizedLayout)
    {
        NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr)
        {
            return fail(UIErrorCode::InvalidNode, "UI element initialization references a stale node");
        }
        if (record->behaviors != descriptor.behaviors)
        {
            return fail(Core::CoreErrorCode::Internal,
                        "UI element behavior state does not match its descriptor");
        }
        layoutStylesByIndex[node.index()] = normalizedLayout;
        enabledByNodeIndex[node.index()] = descriptor.enabled ? 1 : 0;
        const u16 previousBindings = themeBindingsByNodeIndex[node.index()];
        const u16 nextBindings = capacityConfig.applyDefaultProductChrome
                                     ? defaultThemeBindingsFor(descriptor.visual.styleRole)
                                     : 0;
        styleRolesByNodeIndex[node.index()] = descriptor.visual.styleRole;
        const usize styleClassCount = descriptor.visual.styleClasses.size();
        std::copy(descriptor.visual.styleClasses.begin(), descriptor.visual.styleClasses.end(),
                  styleClassesByNodeIndex[node.index()].begin());
        styleClassCountsByNodeIndex[node.index()] = static_cast<u8>(styleClassCount);
        activeNodeStyleClassLinkCount += styleClassCount;
        nodeStyleClassLinkHighWater =
            (std::max)(nodeStyleClassLinkHighWater, activeNodeStyleClassLinkCount);
        themeBindingsByNodeIndex[node.index()] = nextBindings;
        applyProductChromeTransition(node.index(), descriptor.visual.styleRole, productTheme,
                                     previousBindings | nextBindings, nextBindings);
        if (Core::Status semantics = initializeSemantics(node.index(), descriptor.semantics, descriptor.behaviors);
            !semantics)
        {
            return semantics;
        }
        if (Core::Status canvas = assignRetainedCanvas(node.index(), descriptor.visual.canvas); !canvas)
        {
            return canvas;
        }
        if (descriptor.image.has_value())
        {
            if (Core::Status image = imageContentStorage.assign(node.index(), *descriptor.image); !image)
            {
                return image;
            }
        }
        if (descriptor.visual.boxPaint.has_value())
        {
            boxPaintsByIndex[node.index()] = normalizeBoxPaint(*descriptor.visual.boxPaint);
            detachThemeBinding(node.index(), ThemeBindingBoxPaint);
            styleOverridesByNodeIndex[node.index()] |= static_cast<u16>(UIStyleOverride::BoxPaint);
        }
        if (descriptor.pointerHitPolicy.has_value())
        {
            pointerHitPoliciesByIndex[node.index()] = *descriptor.pointerHitPolicy;
        }
        if (descriptor.focusScopeMode.has_value())
        {
            focusScopeModesByNodeIndex[node.index()] = *descriptor.focusScopeMode;
        }

        if (!descriptor.text.has_value())
        {
            refreshLocalPaintCache(node.index());
            static_cast<void>(refreshResolvedStyleCache(node.index()));
            return Core::success();
        }

        WidgetTextState& state = textStatesByIndex[node.index()];
        if (descriptor.textStyle.has_value())
        {
            state.style = *descriptor.textStyle;
            detachThemeBinding(node.index(), ThemeBindingTextStyle);
            styleOverridesByNodeIndex[node.index()] |= static_cast<u16>(UIStyleOverride::TextStyle);
        }
        state.alignment = descriptor.contentAlignment;

        const std::string_view text = *descriptor.text;
        auto metrics = measureWidgetText(text, state.style);
        if (!metrics)
        {
            return Core::failure(metrics.error());
        }
        if (text.empty())
        {
            state.metrics = {};
            refreshLocalPaintCache(node.index());
            static_cast<void>(refreshResolvedStyleCache(node.index()));
            return Core::success();
        }

        auto allocation = allocateRetainedText(static_cast<u32>(text.size()));
        if (!allocation)
        {
            return Core::failure(allocation.error());
        }
        textStorage.write(*allocation, text);
        state.allocation = *allocation;
        state.length = static_cast<u32>(text.size());
        state.metrics = *metrics;
        state.hasContent = true;
        refreshLocalPaintCache(node.index());
        static_cast<void>(refreshResolvedStyleCache(node.index()));
        return Core::success();
    }

    [[nodiscard]] Core::Result<UINodeId> createElement(UINodeId parent,
                                                       const UIElementDescriptor& descriptor)
    {
        auto kindResult = resolveElementBuiltinKind(descriptor);
        if (!kindResult)
        {
            return Core::failure(kindResult.error());
        }
        const BuiltinElementKind kind = *kindResult;

        auto normalizedLayout = normalizeLayoutStyle(descriptor.layout);
        if (!normalizedLayout)
        {
            return Core::failure(normalizedLayout.error());
        }
        if (descriptor.pointerHitPolicy.has_value() && !isValidPointerHitPolicy(*descriptor.pointerHitPolicy))
        {
            return fail(UIErrorCode::InvalidPointerPolicy, "UI element pointer hit policy is not recognized");
        }
        if (descriptor.focusScopeMode.has_value() && !isValidFocusScopeMode(*descriptor.focusScopeMode))
        {
            return fail(UIErrorCode::InvalidFocusScope, "UI element focus-scope mode is not recognized");
        }
        if ((kind == BuiltinElementKind::Modal || kind == BuiltinElementKind::Popup) &&
            descriptor.focusScopeMode.has_value() && *descriptor.focusScopeMode != UIFocusScopeMode::Contain)
        {
            return fail(UIErrorCode::InvalidFocusScope, "UI Modal and Popup elements always contain focus");
        }
        if (kind != BuiltinElementKind::ListView && descriptor.listView != UIListViewCreateConfig{})
        {
            return fail(UIErrorCode::InvalidElementDescriptor,
                        "UI ListView creation config requires the VirtualList behavior");
        }
        if (kind != BuiltinElementKind::TreeView && descriptor.treeView != UITreeViewCreateConfig{})
        {
            return fail(UIErrorCode::InvalidElementDescriptor,
                        "UI TreeView creation config requires the VirtualTree behavior");
        }
        if (!isValidStyleRole(descriptor.visual.styleRole))
        {
            return fail(UIErrorCode::InvalidElementDescriptor, "UI element style role is not recognized");
        }
        if (Core::Status classes = styleSheetStorage.validateClasses(
                descriptor.visual.styleClasses);
            !classes)
        {
            return Core::failure(classes.error());
        }
        if (activeNodeStyleClassLinkCount > capacityConfig.nodeStyleClassLinkCapacity ||
            descriptor.visual.styleClasses.size() >
                capacityConfig.nodeStyleClassLinkCapacity - activeNodeStyleClassLinkCount)
        {
            ++nodeStyleClassLinkCapacityFailureCount;
            return fail(UIErrorCode::CapacityExceeded,
                        "UI node style class link capacity has been exhausted");
        }
        if (descriptor.textStyle.has_value() && !descriptor.text.has_value())
        {
            return fail(UIErrorCode::InvalidElementDescriptor,
                        "UI element text style requires intrinsic text content");
        }
        if (descriptor.text.has_value() && descriptor.image.has_value())
        {
            return fail(UIErrorCode::InvalidElementDescriptor,
                        "UI element intrinsic text and image content are mutually exclusive");
        }
        UIElementDescriptor normalizedDescriptor = descriptor;
        if (descriptor.image.has_value())
        {
            auto image = normalizeImageContent(*descriptor.image);
            if (!image)
            {
                return Core::failure(image.error());
            }
            normalizedDescriptor.image = *image;
            normalizedDescriptor.contentAlignment = image->alignment;
            const bool publishesAccessibleImage = descriptor.semantics.mode != UISemanticsMode::Exclude;
            if (publishesAccessibleImage &&
                (descriptor.semantics.role != UISemanticsRole::Image ||
                 !descriptor.semantics.name.has_value() || descriptor.semantics.name->empty() ||
                 descriptor.semantics.useContentAsName || descriptor.semantics.actions != UISemanticsAction::None))
            {
                return fail(UIErrorCode::InvalidElementDescriptor,
                            "UI accessible images require the Image role, an explicit name, and no actions");
            }
        }
        if (descriptor.text.has_value())
        {
            if (!isValidContentAlignment(descriptor.contentAlignment))
            {
                return fail(UIErrorCode::InvalidLayout,
                            "UI intrinsic content alignment supports Start, Center, or End on each axis");
            }
            if (descriptor.text->size() > (std::numeric_limits<u32>::max)())
            {
                return fail(UIErrorCode::CapacityExceeded, "UI element text payload is too large");
            }
            if ((kind == BuiltinElementKind::TextEdit || kind == BuiltinElementKind::RadioButton) &&
                containsLineBreak(*descriptor.text))
            {
                return fail(UIErrorCode::InvalidText,
                            "UI TextEdit and RadioButton accept one logical line without CR or LF");
            }
        } else if (!descriptor.image.has_value() && descriptor.contentAlignment != UIContentAlignment{})
        {
            return fail(UIErrorCode::InvalidElementDescriptor,
                        "UI element content alignment requires intrinsic text content");
        }

        Core::Result<UINodeId> nodeResult =
            kind == BuiltinElementKind::ListView
                ? createListViewComposite(parent, descriptor.listView)
                : kind == BuiltinElementKind::TreeView ? createTreeViewComposite(parent, descriptor.treeView)
                                                 : createChild(parent, kind, descriptor.behaviors);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }

        const UINodeId node = *nodeResult;
        auto rollback = Core::makeScopeExit([this, node]() noexcept {
            if (contains(node))
            {
                static_cast<void>(destroySubtree(node));
            }
        });
        if (Core::Status initialized = initializeElement(node, kind, normalizedDescriptor, *normalizedLayout); !initialized)
        {
            return Core::failure(initialized.error());
        }
        rollback.release();
        return node;
    }

    [[nodiscard]] Core::Result<UIRootOwner> createRoot(UIContext& context)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (liveRootCount >= capacityConfig.rootCapacity)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI root capacity has been exhausted");
        }

        auto nodeResult = createNode(BuiltinElementKind::Root,
                                     defaultBehaviorsForKind(BuiltinElementKind::Root));
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }

        const UINodeId root = *nodeResult;
        NodeRecord* rootRecord = nodes.tryGet(root.storageId());
        rootRecord->parentIndex = InvalidNodeIndex;
        rootRecord->previousSiblingIndex = lastRootIndex;
        rootRecord->nextSiblingIndex = InvalidNodeIndex;
        rootRecord->depth = 0;

        if (lastRootIndex != InvalidNodeIndex)
        {
            recordByIndex(lastRootIndex)->nextSiblingIndex = root.index();
        } else
        {
            firstRootIndex = root.index();
        }
        lastRootIndex = root.index();
        ++liveRootCount;
        markStructureChanged();
        return UIRootOwner(context.m_impl->lifetime, root);
    }

    [[nodiscard]] Core::Result<UINodeId>
    createChild(UINodeId parent, BuiltinElementKind kind,
                std::optional<UIElementBehavior> authoredBehaviors = std::nullopt)
    {
        if (kind == BuiltinElementKind::Root)
        {
            return fail(UIErrorCode::InvalidParent, "Root nodes cannot be created as children");
        }
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();

        auto parentResult = resolveParent(parent);
        if (!parentResult)
        {
            return Core::failure(parentResult.error());
        }
        NodeRecord& parentRecord = **parentResult;
        if (kind == BuiltinElementKind::Popup)
        {
            if (parentRecord.kind != BuiltinElementKind::Dropdown)
            {
                return fail(UIErrorCode::InvalidParent, "UI Popup requires a Dropdown parent");
            }
            if (parent.index() >= dropdownStatesByNodeIndex.size())
            {
                return fail(Core::CoreErrorCode::Internal, "UI Dropdown state index is out of range");
            }
            if (contains(dropdownStatesByNodeIndex[parent.index()].popup))
            {
                return fail(UIErrorCode::InvalidParent, "UI Dropdown already owns a Popup");
            }
        } else if (kind == BuiltinElementKind::DropdownItem)
        {
            if (parentRecord.kind != BuiltinElementKind::Popup)
            {
                return fail(UIErrorCode::InvalidParent, "UI DropdownItem requires a Popup parent");
            }
        } else if (kind == BuiltinElementKind::ListViewItem)
        {
            if (parentRecord.kind != BuiltinElementKind::ListView)
            {
                return fail(UIErrorCode::InvalidParent, "UI ListViewItem requires a ListView parent");
            }
        } else if (kind == BuiltinElementKind::TreeViewItem)
        {
            if (parentRecord.kind != BuiltinElementKind::TreeView)
            {
                return fail(UIErrorCode::InvalidParent, "UI TreeViewItem requires a TreeView parent");
            }
        } else if (parentRecord.kind == BuiltinElementKind::Dropdown || parentRecord.kind == BuiltinElementKind::Popup)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI Dropdown composites only accept Popup and DropdownItem children");
        } else if (parentRecord.kind == BuiltinElementKind::ListView)
        {
            return fail(UIErrorCode::InvalidParent, "UI ListView only accepts its internal item rows");
        } else if (parentRecord.kind == BuiltinElementKind::TreeView)
        {
            return fail(UIErrorCode::InvalidParent, "UI TreeView only accepts its internal item rows");
        }
        if (parentRecord.depth == (std::numeric_limits<u32>::max)())
        {
            return fail(UIErrorCode::InvalidParent, "UI parent depth cannot be represented");
        }

        auto nodeResult = createNode(kind,
                                     authoredBehaviors.value_or(defaultBehaviorsForKind(kind)));
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }

        const UINodeId node = *nodeResult;
        NodeRecord* childRecord = nodes.tryGet(node.storageId());
        childRecord->parentIndex = parent.index();
        childRecord->previousSiblingIndex = parentRecord.lastChildIndex;
        childRecord->nextSiblingIndex = InvalidNodeIndex;
        childRecord->rootIndex = parentRecord.rootIndex;
        childRecord->depth = parentRecord.depth + 1;

        if (parentRecord.lastChildIndex != InvalidNodeIndex)
        {
            recordByIndex(parentRecord.lastChildIndex)->nextSiblingIndex = node.index();
        } else
        {
            parentRecord.firstChildIndex = node.index();
        }
        parentRecord.lastChildIndex = node.index();
        if (kind == BuiltinElementKind::Popup)
        {
            dropdownStatesByNodeIndex[parent.index()].popup = node;
        }
        markStructureChanged();
        return node;
    }

    [[nodiscard]] Core::Result<UINodeId> createListViewComposite(UINodeId parent, UIListViewCreateConfig config)
    {
        auto normalized = Detail::normalizeListViewCreateConfig(config);
        if (!normalized)
        {
            return Core::failure(normalized.error());
        }
        const usize requiredNodes = static_cast<usize>(normalized->materializedItemCapacity) + 1U;
        if (availableNodeCountForCurrentCreation() < requiredNodes)
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI ListView row pool exceeds the remaining node capacity");
        }

        auto listResult = createChild(parent, BuiltinElementKind::ListView);
        if (!listResult)
        {
            return Core::failure(listResult.error());
        }
        const UINodeId listView = *listResult;
        auto rollback = Core::makeScopeExit([this, listView]() noexcept {
            if (contains(listView))
            {
                static_cast<void>(destroySubtree(listView));
            }
        });
        ListViewState& state = listViewStatesByNodeIndex[listView.index()];
        state.materializedItemCapacity = normalized->materializedItemCapacity;

        for (u32 row = 0; row < normalized->materializedItemCapacity; ++row)
        {
            auto itemResult = createChild(listView, BuiltinElementKind::ListViewItem);
            if (!itemResult)
            {
                return Core::failure(itemResult.error());
            }
            const UINodeId item = *itemResult;
            UILayoutStyle& itemLayout = layoutStylesByIndex[item.index()];
            itemLayout.size.height = UILayoutLength::Px(state.style.rowHeight);
            itemLayout.padding = UIEdgeSpacing::HorizontalVertical(8.0F, 4.0F);
        }
        rollback.release();
        return listView;
    }

    [[nodiscard]] Core::Result<UINodeId> createTreeViewComposite(UINodeId parent, UITreeViewCreateConfig config)
    {
        auto normalized = Detail::normalizeTreeViewCreateConfig(config);
        if (!normalized)
        {
            return Core::failure(normalized.error());
        }
        const usize requiredNodes = static_cast<usize>(normalized->materializedItemCapacity) + 1U;
        if (availableNodeCountForCurrentCreation() < requiredNodes)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI TreeView row pool exceeds the remaining node capacity");
        }

        auto treeResult = createChild(parent, BuiltinElementKind::TreeView);
        if (!treeResult)
        {
            return Core::failure(treeResult.error());
        }
        const UINodeId treeView = *treeResult;
        auto rollback = Core::makeScopeExit([this, treeView]() noexcept {
            if (contains(treeView))
            {
                static_cast<void>(destroySubtree(treeView));
            }
        });
        TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
        state.materializedItemCapacity = normalized->materializedItemCapacity;

        for (u32 row = 0; row < normalized->materializedItemCapacity; ++row)
        {
            auto itemResult = createChild(treeView, BuiltinElementKind::TreeViewItem);
            if (!itemResult)
            {
                return Core::failure(itemResult.error());
            }
            const UINodeId item = *itemResult;
            UILayoutStyle& itemLayout = layoutStylesByIndex[item.index()];
            itemLayout.size.height = UILayoutLength::Px(state.style.rowHeight);
            itemLayout.padding = UIEdgeSpacing::HorizontalVertical(8.0F, 4.0F);
        }
        rollback.release();
        return treeView;
    }

    [[nodiscard]] Core::Result<UINodeId> createElementFromUpdater(UINodeId updaterRoot, UINodeId parent,
                                                                  const UIElementDescriptor& descriptor)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto parentResult = resolveParent(parent);
        if (!parentResult)
        {
            return Core::failure(parentResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, parent))
        {
            return fail(UIErrorCode::InvalidNode, "UI parent is not owned by the updater root");
        }

        return createElement(parent, descriptor);
    }

    [[nodiscard]] Core::Result<UIComponentBuildBudget>
    requiredBuildBudgetForElement(const UIElementDescriptor& descriptor) const
    {
        auto kind = resolveElementBuiltinKind(descriptor);
        if (!kind)
        {
            return Core::failure(kind.error());
        }
        UIComponentBuildBudget required{.nodes = 1};
        const auto addBehaviorSlots = [](UIBehaviorSlotBudget& slots,
                                         UIElementBehavior behaviors,
                                         usize count = 1U) noexcept {
            if (hasBehavior(behaviors, UIElementBehavior::Activate))
            {
                slots.activate += count;
            }
            if (hasBehavior(behaviors, UIElementBehavior::Toggle))
            {
                slots.toggle += count;
            }
            if (hasBehavior(behaviors, UIElementBehavior::RangeInput))
            {
                slots.range += count;
            }
            if (hasBehavior(behaviors, UIElementBehavior::TextInput))
            {
                slots.textInput += count;
            }
            if (hasBehavior(behaviors, UIElementBehavior::Scroll))
            {
                slots.scroll += count;
            }
            if (hasBehavior(behaviors, UIElementBehavior::Select))
            {
                slots.selection += count;
            }
        };
        addBehaviorSlots(required.behaviors, descriptor.behaviors);

        const usize semanticsNameBytes = descriptor.semantics.name.has_value()
                                             ? descriptor.semantics.name->size()
                                             : 0U;
        const usize semanticsDescriptionBytes = descriptor.semantics.description.has_value()
                                                    ? descriptor.semantics.description->size()
                                                    : 0U;
        const usize intrinsicTextBytes = descriptor.text.has_value() ? descriptor.text->size() : 0U;
        if (semanticsDescriptionBytes > (std::numeric_limits<usize>::max)() - semanticsNameBytes ||
            intrinsicTextBytes > (std::numeric_limits<usize>::max)() -
                                     semanticsNameBytes - semanticsDescriptionBytes)
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI component descriptor text budget overflowed");
        }
        required.textBytes = semanticsNameBytes + semanticsDescriptionBytes + intrinsicTextBytes;
        required.canvasCommands = descriptor.visual.canvas.size();

        if (*kind == BuiltinElementKind::ListView)
        {
            auto config = normalizeListViewCreateConfig(descriptor.listView);
            if (!config)
            {
                return Core::failure(config.error());
            }
            const usize rowCount = config->materializedItemCapacity;
            required.nodes += rowCount;
            addBehaviorSlots(required.behaviors,
                             defaultBehaviorsForKind(BuiltinElementKind::ListViewItem), rowCount);
        }
        else if (*kind == BuiltinElementKind::TreeView)
        {
            auto config = normalizeTreeViewCreateConfig(descriptor.treeView);
            if (!config)
            {
                return Core::failure(config.error());
            }
            const usize rowCount = config->materializedItemCapacity;
            required.nodes += rowCount;
            addBehaviorSlots(required.behaviors,
                             defaultBehaviorsForKind(BuiltinElementKind::TreeViewItem), rowCount);
        }
        return required;
    }

    [[nodiscard]] Core::Result<UIElementBuildTransaction>
    beginBuildTransaction(UIContext& context, UINodeId updaterRoot, UINodeId parent,
                          const UIElementDescriptor& rootDescriptor, UIComponentBuildBudget budget)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (budget.nodes == 0)
        {
            ++componentBuildTransactionFailureCount;
            return fail(UIErrorCode::InvalidElementDescriptor,
                        "UI component build transaction requires a positive node budget");
        }
        auto requiredBudget = requiredBuildBudgetForElement(rootDescriptor);
        if (!requiredBudget)
        {
            ++componentBuildTransactionFailureCount;
            return Core::failure(requiredBudget.error());
        }
        if (!containsBudget(budget, *requiredBudget))
        {
            ++componentBuildTransactionFailureCount;
            return fail(UIErrorCode::CapacityExceeded,
                        "UI component root exceeds its declared build budget");
        }

        UIComponentBuildReservation reservation;
        if (Core::Status reserved = reserveComponentBuildStorage(budget, reservation); !reserved)
        {
            ++componentBuildTransactionFailureCount;
            return Core::failure(reserved.error());
        }
        auto reservationRollback = Core::makeScopeExit([this, &reservation]() noexcept {
            releaseComponentBuildStorage(reservation);
        });

        if (activeComponentBuildReservation != nullptr)
        {
            ++componentBuildTransactionFailureCount;
            return fail(Core::CoreErrorCode::Internal,
                        "UI component build reservation scope is already active");
        }
        activeComponentBuildReservation = &reservation;
        auto activeScope = Core::makeScopeExit([this]() noexcept {
            activeComponentBuildReservation = nullptr;
        });
        auto componentRoot = createElementFromUpdater(updaterRoot, parent, rootDescriptor);
        if (!componentRoot)
        {
            ++componentBuildTransactionFailureCount;
            return Core::failure(componentRoot.error());
        }
        activeScope.release();
        activeComponentBuildReservation = nullptr;

        UIComponentBuildReservation& storedReservation =
            componentBuildReservationsByNodeIndex[componentRoot->index()];
        if (storedReservation.active)
        {
            static_cast<void>(destroySubtree(*componentRoot));
            ++componentBuildTransactionFailureCount;
            return fail(Core::CoreErrorCode::Internal,
                        "UI component root already owns a build reservation");
        }
        reservation.componentRoot = *componentRoot;
        storedReservation = std::move(reservation);
        reservation = {};
        reservationRollback.release();
        ++activeBuildTransactionCount;
        return UIElementBuildTransaction{
            context,
            updaterRoot,
            *componentRoot,
            storedReservation.remaining,
        };
    }

    [[nodiscard]] Core::Result<UINodeId>
    createElementFromBuildTransaction(UINodeId updaterRoot, UINodeId componentRoot, UINodeId parent,
                                      const UIElementDescriptor& descriptor,
                                      UIComponentBuildBudget& remainingBudget)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        UIComponentBuildReservation* reservation = findComponentBuildReservation(componentRoot);
        if (activeBuildTransactionCount == 0 || reservation == nullptr || !contains(updaterRoot) ||
            !contains(componentRoot) ||
            !isNodeWithinRoot(updaterRoot, componentRoot))
        {
            ++componentBuildTransactionFailureCount;
            return fail(UIErrorCode::InvalidNode, "UI component build transaction is no longer active");
        }
        if (!contains(parent) || !isNodeWithinSubtree(componentRoot, parent))
        {
            ++componentBuildTransactionFailureCount;
            return fail(UIErrorCode::InvalidParent,
                        "UI component build transaction parent must belong to its component subtree");
        }
        auto requiredBudget = requiredBuildBudgetForElement(descriptor);
        if (!requiredBudget)
        {
            ++componentBuildTransactionFailureCount;
            return Core::failure(requiredBudget.error());
        }
        if (!containsBudget(reservation->remaining, *requiredBudget))
        {
            ++componentBuildTransactionFailureCount;
            return fail(UIErrorCode::CapacityExceeded,
                        "UI component build transaction budget has been exhausted");
        }

        if (activeComponentBuildReservation != nullptr)
        {
            ++componentBuildTransactionFailureCount;
            return fail(Core::CoreErrorCode::Internal,
                        "UI component build reservation scope is already active");
        }
        activeComponentBuildReservation = reservation;
        auto activeScope = Core::makeScopeExit([this]() noexcept {
            activeComponentBuildReservation = nullptr;
        });
        auto node = createElement(parent, descriptor);
        remainingBudget = reservation->remaining;
        if (!node)
        {
            ++componentBuildTransactionFailureCount;
            return Core::failure(node.error());
        }
        return node;
    }

    [[nodiscard]] Core::Status commitBuildTransaction(UINodeId updaterRoot, UINodeId componentRoot,
                                                      UIComponentBuildBudget& remainingBudget)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        UIComponentBuildReservation* reservation = findComponentBuildReservation(componentRoot);
        if (activeBuildTransactionCount == 0 || reservation == nullptr)
        {
            ++componentBuildTransactionFailureCount;
            return fail(UIErrorCode::InvalidNode, "UI component build transaction is no longer active");
        }
        if (!contains(updaterRoot) || !contains(componentRoot) || !isNodeWithinRoot(updaterRoot, componentRoot))
        {
            releaseComponentBuildStorage(*reservation);
            --activeBuildTransactionCount;
            remainingBudget = {};
            ++componentBuildTransactionFailureCount;
            return fail(UIErrorCode::InvalidNode, "UI component build transaction root is no longer alive");
        }
        releaseComponentBuildStorage(*reservation);
        --activeBuildTransactionCount;
        remainingBudget = {};
        return Core::success();
    }

    void rollbackBuildTransaction(UINodeId updaterRoot, UINodeId componentRoot,
                                  UIComponentBuildBudget& remainingBudget) noexcept
    {
        if (!isOwnerThread())
        {
            std::terminate();
        }
        UIComponentBuildReservation* reservation = findComponentBuildReservation(componentRoot);
        if (contains(updaterRoot) && contains(componentRoot) && isNodeWithinRoot(updaterRoot, componentRoot))
        {
            static_cast<void>(destroySubtree(componentRoot));
        }
        if (reservation != nullptr && reservation->active)
        {
            releaseComponentBuildStorage(*reservation);
            if (activeBuildTransactionCount != 0)
            {
                --activeBuildTransactionCount;
            }
        }
        remainingBudget = {};
    }

    void unlinkFromTree(u32 index, NodeRecord& record) noexcept
    {
        if (record.parentIndex != InvalidNodeIndex)
        {
            NodeRecord* parent = recordByIndex(record.parentIndex);
            if (parent != nullptr)
            {
                if (parent->firstChildIndex == index)
                {
                    parent->firstChildIndex = record.nextSiblingIndex;
                }
                if (parent->lastChildIndex == index)
                {
                    parent->lastChildIndex = record.previousSiblingIndex;
                }
            }
        } else
        {
            if (firstRootIndex == index)
            {
                firstRootIndex = record.nextSiblingIndex;
            }
            if (lastRootIndex == index)
            {
                lastRootIndex = record.previousSiblingIndex;
            }
        }

        if (record.previousSiblingIndex != InvalidNodeIndex)
        {
            if (NodeRecord* previous = recordByIndex(record.previousSiblingIndex); previous != nullptr)
            {
                previous->nextSiblingIndex = record.nextSiblingIndex;
            }
        }
        if (record.nextSiblingIndex != InvalidNodeIndex)
        {
            if (NodeRecord* next = recordByIndex(record.nextSiblingIndex); next != nullptr)
            {
                next->previousSiblingIndex = record.previousSiblingIndex;
            }
        }
    }

    void eraseDetachedSubtree(u32 index) noexcept
    {
        u32 currentIndex = index;
        while (currentIndex != InvalidNodeIndex)
        {
            NodeRecord* record = recordByIndex(currentIndex);
            if (record == nullptr)
            {
                return;
            }

            if (record->firstChildIndex != InvalidNodeIndex)
            {
                currentIndex = record->firstChildIndex;
                continue;
            }

            const u32 parentIndex = record->parentIndex;
            const u32 nextSiblingIndex = record->nextSiblingIndex;
            if (currentIndex != index)
            {
                unlinkFromTree(currentIndex, *record);
            }

            const UINodeId node = idForIndex(currentIndex);
            if (record->kind == BuiltinElementKind::Modal && currentIndex < focusRestoreByNodeIndex.size())
            {
                const auto& committedEntries = committedHitBuffers[publishedHitBufferIndex];
                const u32 destroyedModalEntryIndex = findHitEntryIndex(node, committedEntries);
                if (committedActiveModalEntryIndex < committedEntries.size() &&
                    destroyedModalEntryIndex < committedEntries.size() &&
                    hitEntryIsWithinScope(committedActiveModalEntryIndex, destroyedModalEntryIndex, committedEntries))
                {
                    // Post-order destruction visits the active Modal before its
                    // committed Modal ancestors. The last stored value therefore
                    // restores through every removed layer to the surviving scope.
                    pendingDestroyedModalRestoreFocus = focusRestoreByNodeIndex[currentIndex];
                    hasPendingDestroyedModalRestoreFocus = true;
                }
            }
            if (record->kind == BuiltinElementKind::DropdownItem)
            {
                const UINodeId dropdown = dropdownForItem(node);
                Detail::UISelectBehaviorState* select =
                    dropdown.hasValue() ? behaviorStateStorage.trySelectState(dropdown.index()) : nullptr;
                if (select != nullptr && select->selectedOption == node)
                {
                    select->selectedOption = {};
                }
            }
            if (record->kind == BuiltinElementKind::Popup)
            {
                const UINodeId dropdown = dropdownForPopup(node);
                if (dropdown.hasValue() && dropdown.index() < dropdownStatesByNodeIndex.size())
                {
                    DropdownState& dropdownState = dropdownStatesByNodeIndex[dropdown.index()];
                    if (dropdownState.popup == node)
                    {
                        dropdownState.popup = {};
                        if (Detail::UISelectBehaviorState* select =
                                behaviorStateStorage.trySelectState(dropdown.index());
                            select != nullptr)
                        {
                            select->selectedOption = {};
                        }
                    }
                }
                if (activePopupNode == node)
                {
                    activePopupNode = {};
                    popupDismissPointerBarrierActive = false;
                    dropdownCommandPressLatch.clear();
                }
            }
            deactivateAllRoutedPointerListenersForNode(currentIndex);
            deactivateButtonActionForNode(currentIndex);
            sliderChangeCallbackRegistry.clearNode(currentIndex, true);
            idsByIndex[currentIndex] = {};
            static_cast<void>(nodes.erase(node.storageId()));
            resetNodeSideData(currentIndex);
            routedPointerListenerRegistry.reclaim(routeDispatchDepth != 0);
            buttonActionRegistry.reclaim(routeDispatchDepth != 0);
            sliderChangeCallbackRegistry.reclaim(routeDispatchDepth != 0);

            if (currentIndex == index)
            {
                return;
            }
            currentIndex = nextSiblingIndex != InvalidNodeIndex ? nextSiblingIndex : parentIndex;
        }
    }

    void releaseComponentBuildReservationsInSubtree(UINodeId subtreeRoot) noexcept
    {
        for (UIComponentBuildReservation& reservation : componentBuildReservationsByNodeIndex)
        {
            if (!reservation.active || !contains(reservation.componentRoot) ||
                !isNodeWithinSubtree(subtreeRoot, reservation.componentRoot))
            {
                continue;
            }
            releaseComponentBuildStorage(reservation);
            if (activeBuildTransactionCount == 0)
            {
                std::terminate();
            }
            --activeBuildTransactionCount;
        }
    }

    [[nodiscard]] Core::Status destroySubtree(UINodeId node)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }

        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }

        dispatchPointerCancelForSubtree(node);
        NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr)
        {
            // A PointerCancel listener may synchronously complete the requested
            // destruction. Generation validation makes that outcome idempotent.
            return Core::success();
        }
        releaseComponentBuildReservationsInSubtree(node);
        const bool wasRoot = record->kind == BuiltinElementKind::Root;
        unlinkFromTree(node.index(), *record);
        eraseDetachedSubtree(node.index());
        if (wasRoot && liveRootCount > 0)
        {
            --liveRootCount;
        }
        markStructureChanged();
        return Core::success();
    }

    void destroyRootImmediately(UINodeId root) noexcept
    {
        if (!isOwnerThread() || !contains(root))
        {
            return;
        }

        dispatchPointerCancelForSubtree(root);
        NodeRecord* rootRecord = nodes.tryGet(root.storageId());
        if (rootRecord == nullptr || rootRecord->kind != BuiltinElementKind::Root)
        {
            return;
        }

        releaseComponentBuildReservationsInSubtree(root);
        unlinkFromTree(root.index(), *rootRecord);
        eraseDetachedSubtree(root.index());
        if (liveRootCount > 0)
        {
            --liveRootCount;
        }
        markStructureChanged();
    }

    [[nodiscard]] Core::Status destroyFromUpdater(UINodeId updaterRoot, UINodeId node)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        if (!contains(node))
        {
            auto nodeResult = resolveNode(node);
            return nodeResult ? Core::success() : Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }

        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record != nullptr && record->kind == BuiltinElementKind::ListViewItem)
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI ListView item rows are internal and cannot be destroyed independently");
        }
        if (record != nullptr && record->kind == BuiltinElementKind::TreeViewItem)
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI TreeView item rows are internal and cannot be destroyed independently");
        }

        if (updaterRoot == node)
        {
            return fail(UIErrorCode::RootRequired, "Destroying a root node requires UIRootOwner::reset");
        }
        return destroySubtree(node);
    }

    [[nodiscard]] Core::Status setLayoutStyleFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                         const UILayoutStyle& style)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto normalizedStyle = Detail::normalizeLayoutStyle(style);
        if (!normalizedStyle)
        {
            return Core::failure(normalizedStyle.error());
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }

        UILayoutStyle& currentStyle = layoutStylesByIndex[node.index()];
        if (currentStyle == *normalizedStyle)
        {
            return Core::success();
        }

        if (Core::Status dirtyStatus = markLayoutStyleDirty(node); !dirtyStatus)
        {
            return dirtyStatus;
        }
        currentStyle = *normalizedStyle;
        return Core::success();
    }

    [[nodiscard]] Core::Status setPointerHitPolicyFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                              UIPointerHitPolicy policy)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }
        if (!isValidPointerHitPolicy(policy))
        {
            return fail(UIErrorCode::InvalidPointerPolicy, "UI pointer hit policy is not recognized");
        }

        UIPointerHitPolicy& currentPolicy = pointerHitPoliciesByIndex[node.index()];
        if (currentPolicy == policy)
        {
            return Core::success();
        }
        const bool clearHover = hoveredPrimaryControl == node && policy != UIPointerHitPolicy::Targetable;
        if (clearHover)
        {
            if (Core::Status dirtyStatus = markPaintDirty(node); !dirtyStatus)
            {
                return dirtyStatus;
            }
        }
        if (Core::Status dirtyStatus = markHitTestDirty(node); !dirtyStatus)
        {
            return dirtyStatus;
        }
        currentPolicy = policy;
        if (clearHover)
        {
            clearHoveredPrimaryControl();
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status setEnabledFromUpdater(UINodeId updaterRoot, UINodeId node, bool enabled)
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
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }
        const UISemanticsMode semanticsMode = semanticsStatesByNodeIndex[node.index()].mode;
        if (semanticsMode != UISemanticsMode::Publish && semanticsMode != UISemanticsMode::MergeDescendants)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI enabled state requires a published widget node");
        }
        if (node.index() >= enabledByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI enabled state index is out of range");
        }

        const u8 next = enabled ? 1 : 0;
        if (enabledByNodeIndex[node.index()] == next)
        {
            return Core::success();
        }

        UINodeId popupToClose{};
        if (!enabled)
        {
            if ((*nodeResult)->kind == BuiltinElementKind::Dropdown)
            {
                const UINodeId popup = popupForDropdown(node);
                if (popup.hasValue() && popupStatesByNodeIndex[popup.index()].open)
                {
                    popupToClose = popup;
                }
            } else if ((*nodeResult)->kind == BuiltinElementKind::Popup &&
                       popupStatesByNodeIndex[node.index()].open)
            {
                popupToClose = node;
            }
        }

        // Dirty capacity is reserved before interaction state changes so a
        // rejected setter leaves enabled/focus/arm state untouched.
        releaseRouteDirtyQueueReservations();
        addRouteDirtyReservationCandidate(node);
        if (popupToClose.hasValue())
        {
            addRouteLayoutDirtyReservationCandidates(popupToClose);
            addRouteLayoutDirtyReservationCandidates(dropdownForPopup(popupToClose));
        }
        if (Core::Status reservation = reserveRouteDirtyQueueSlots(); !reservation)
        {
            releaseRouteDirtyQueueReservations();
            return reservation;
        }
        auto reservationCleanup = Core::makeScopeExit([this]() noexcept { releaseRouteDirtyQueueReservations(); });
        if (Core::Status dirty = markPaintDirty(node); !dirty)
        {
            return dirty;
        }
        if (popupToClose.hasValue())
        {
            if (Core::Status closed = setPopupOpenState(popupToClose, false); !closed)
            {
                return closed;
            }
        }
        if (!enabled && capturedPointerNode == node)
        {
            dispatchPointerCancelForCurrentCapture();
            if (!contains(node))
            {
                return Core::success();
            }
        }
        enabledByNodeIndex[node.index()] = next;
        if (!enabled)
        {
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
                defaultActionFocusButton = {};
            }
            if (textInputFocus == node)
            {
                textInputFocus = {};
                resetImeCompositionState();
            }
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<bool> isEnabledFromUpdater(UINodeId updaterRoot, UINodeId node) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto nodeResult = const_cast<Impl*>(this)->resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }
        const UISemanticsMode semanticsMode = semanticsStatesByNodeIndex[node.index()].mode;
        if (semanticsMode != UISemanticsMode::Publish && semanticsMode != UISemanticsMode::MergeDescendants)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI enabled state requires a published widget node");
        }
        if (node.index() >= enabledByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI enabled state index is out of range");
        }
        return enabledByNodeIndex[node.index()] != 0;
    }

    [[nodiscard]] Core::Status setFocusScopeModeFromUpdater(UINodeId updaterRoot, UINodeId node, UIFocusScopeMode mode)
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
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI focus scope is not owned by the updater root");
        }
        if (!isValidFocusScopeMode(mode))
        {
            return fail(UIErrorCode::InvalidFocusScope, "UI focus-scope mode is not recognized");
        }
        if (((*nodeResult)->kind == BuiltinElementKind::Modal || (*nodeResult)->kind == BuiltinElementKind::Popup) &&
            mode != UIFocusScopeMode::Contain)
        {
            return fail(UIErrorCode::InvalidFocusScope, "UI Modal and Popup nodes always contain focus");
        }
        UIFocusScopeMode& current = focusScopeModesByNodeIndex[node.index()];
        if (current == mode)
        {
            return Core::success();
        }
        if (Core::Status dirty = markHitTestDirty(node); !dirty)
        {
            return dirty;
        }
        current = mode;
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIFocusScopeMode> focusScopeModeFromUpdater(UINodeId updaterRoot, UINodeId node) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto nodeResult = const_cast<Impl*>(this)->resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI focus scope is not owned by the updater root");
        }
        return focusScopeModesByNodeIndex[node.index()];
    }

    [[nodiscard]] Core::Status setStyleRoleFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                       UIStyleRoleId role)
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
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI style role is not owned by the updater root");
        }
        if (!isValidStyleRole(role))
        {
            return fail(UIErrorCode::InvalidTheme, "UI style role is not recognized");
        }

        const u32 index = node.index();
        if (styleRolesByNodeIndex[index] == role)
        {
            return Core::success();
        }
        const u16 previousBindings = themeBindingsByNodeIndex[index];
        const u16 supportedBindings =
            capacityConfig.applyDefaultProductChrome ? defaultThemeBindingsFor(role) : 0;
        const u16 nextBindings =
            supportedBindings & static_cast<u16>(~styleOverridesByNodeIndex[index]);
        const u16 affectedBindings = previousBindings | nextBindings;

        std::fill(themeDirtyScratchByNodeIndex.begin(), themeDirtyScratchByNodeIndex.end(), u8{0});
        if (Core::Status staged =
                stageProductChromeTransition(index, role, productTheme, affectedBindings, nextBindings);
            !staged)
        {
            return staged;
        }
        stageThemePaintChange(index);
        propagateThemeLayoutDirtyToAncestors();
        if (Core::Status capacity = preflightThemeDirtyQueue(); !capacity)
        {
            return capacity;
        }

        styleRolesByNodeIndex[index] = role;
        themeBindingsByNodeIndex[index] = nextBindings;
        applyStagedProductChromeTransition(index, role, productTheme, affectedBindings, nextBindings);
        publishThemeDirtyState();
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIStyleRoleId> styleRoleFromUpdater(UINodeId updaterRoot,
                                                                   UINodeId node) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto nodeResult = const_cast<Impl*>(this)->resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI style role is not owned by the updater root");
        }
        return styleRolesByNodeIndex[node.index()];
    }

    [[nodiscard]] Core::Status clearOverrideFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                        UIStyleOverride properties)
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
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI style override is not owned by the updater root");
        }

        constexpr u16 ValidOverrides = static_cast<u16>(UIStyleOverride::All);
        const u16 requested = static_cast<u16>(properties);
        if ((requested & static_cast<u16>(~ValidOverrides)) != 0)
        {
            return fail(UIErrorCode::InvalidTheme, "UI style override mask contains an unknown property");
        }
        const u32 index = node.index();
        const u16 overridesToClear = styleOverridesByNodeIndex[index] & requested;
        if (overridesToClear == 0)
        {
            return Core::success();
        }

        const UIStyleRoleId role = styleRolesByNodeIndex[index];
        const u16 supportedBindings =
            capacityConfig.applyDefaultProductChrome ? defaultThemeBindingsFor(role) : 0;
        const u16 restoredBindings = supportedBindings & overridesToClear;
        const u16 previousBindings = themeBindingsByNodeIndex[index];
        const u16 nextBindings = previousBindings | restoredBindings;
        const u16 affectedBindings = nextBindings & static_cast<u16>(~previousBindings);

        std::fill(themeDirtyScratchByNodeIndex.begin(), themeDirtyScratchByNodeIndex.end(), u8{0});
        if (Core::Status staged =
                stageProductChromeTransition(index, role, productTheme, affectedBindings, affectedBindings);
            !staged)
        {
            return staged;
        }
        if ((overridesToClear & boxFillOverrideMask(**nodeResult)) != 0)
        {
            stageThemePaintChange(index);
        }
        propagateThemeLayoutDirtyToAncestors();
        if (Core::Status capacity = preflightThemeDirtyQueue(); !capacity)
        {
            return capacity;
        }

        styleOverridesByNodeIndex[index] &= static_cast<u16>(~requested);
        themeBindingsByNodeIndex[index] = nextBindings;
        applyStagedProductChromeTransition(index, role, productTheme, affectedBindings, affectedBindings);
        publishThemeDirtyState();
        return Core::success();
    }

    [[nodiscard]] Core::Status setBoxPaintFromUpdater(UINodeId updaterRoot, UINodeId node, const UIBoxPaint& paint)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }

        const UIBoxPaint normalizedPaint = Detail::normalizeBoxPaint(paint);
        UIBoxPaint& currentPaint = boxPaintsByIndex[node.index()];
        if (currentPaint == normalizedPaint)
        {
            if ((styleOverridesByNodeIndex[node.index()] &
                 static_cast<u16>(UIStyleOverride::BoxPaint)) != 0)
            {
                return Core::success();
            }
            if (Core::Status dirtyStatus = markPaintDirty(node); !dirtyStatus)
            {
                return dirtyStatus;
            }
            detachThemeBinding(node.index(), ThemeBindingBoxPaint);
            return Core::success();
        }
        if (Core::Status dirtyStatus = markPaintDirty(node); !dirtyStatus)
        {
            return dirtyStatus;
        }
        currentPaint = normalizedPaint;
        detachThemeBinding(node.index(), ThemeBindingBoxPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Status setButtonPaintFromUpdater(UINodeId updaterRoot, UINodeId button,
                                                         const UIButtonPaint& paint)
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
        auto buttonResult = resolvePlainButton(button);
        if (!buttonResult)
        {
            return Core::failure(buttonResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, button))
        {
            return fail(UIErrorCode::InvalidNode, "UI Button is not owned by the updater root");
        }
        if (button.index() >= buttonPaintsByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI Button paint index is out of range");
        }
        UIButtonPaint& currentPaint = buttonPaintsByNodeIndex[button.index()];
        if (currentPaint == paint)
        {
            if ((styleOverridesByNodeIndex[button.index()] &
                 static_cast<u16>(UIStyleOverride::ButtonPaint)) != 0)
            {
                return Core::success();
            }
            if (Core::Status dirty = markPaintDirty(button); !dirty)
            {
                return dirty;
            }
            detachThemeBinding(button.index(), ThemeBindingButtonPaint);
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(button); !dirty)
        {
            return dirty;
        }
        currentPaint = paint;
        detachThemeBinding(button.index(), ThemeBindingButtonPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIButtonPaint> buttonPaintFromUpdater(UINodeId updaterRoot, UINodeId button) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto buttonResult = const_cast<Impl*>(this)->resolvePlainButton(button);
        if (!buttonResult)
        {
            return Core::failure(buttonResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, button))
        {
            return fail(UIErrorCode::InvalidNode, "UI Button is not owned by the updater root");
        }
        if (button.index() >= buttonPaintsByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI Button paint index is out of range");
        }
        return buttonPaintsByNodeIndex[button.index()];
    }

    [[nodiscard]] Core::Status setTextFromUpdater(UINodeId updaterRoot, UINodeId node, std::string_view utf8)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }
        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr || !supportsWidgetText(record->kind))
        {
            return fail(UIErrorCode::InvalidText, "UI text requires an element with intrinsic text content");
        }
        if ((record->kind == BuiltinElementKind::TextEdit || record->kind == BuiltinElementKind::RadioButton) &&
            containsLineBreak(utf8))
        {
            return fail(UIErrorCode::InvalidText,
                        "UI TextEdit and RadioButton accept one logical line without CR or LF");
        }
        if (utf8.size() > (std::numeric_limits<u32>::max)())
        {
            return fail(UIErrorCode::CapacityExceeded, "UI text payload is too large");
        }

        auto metrics = measureWidgetText(utf8, textStatesByIndex[node.index()].style);
        if (!metrics)
        {
            return Core::failure(metrics.error());
        }

        WidgetTextState& state = textStatesByIndex[node.index()];
        const std::string_view current = textViewFor(node.index());
        const bool clearActiveIme =
            record->kind == BuiltinElementKind::TextEdit && textInputFocus == node && imeComposition.active();
        if (state.hasContent == !utf8.empty() && current == utf8 && state.metrics == *metrics)
        {
            if (clearActiveIme)
            {
                return clearImeComposition();
            }
            return Core::success();
        }

        TextByteAllocation reservedAllocation{};
        bool reservedNewAllocation = false;
        if (!utf8.empty() && state.allocation.capacity < utf8.size())
        {
            auto allocation = textStorage.allocate(static_cast<u32>(utf8.size()));
            if (!allocation)
            {
                return Core::failure(allocation.error());
            }
            reservedAllocation = *allocation;
            reservedNewAllocation = true;
        }

        if (Core::Status dirtyStatus = markLayoutStyleDirty(node); !dirtyStatus)
        {
            if (reservedNewAllocation)
            {
                textStorage.release(reservedAllocation);
            }
            return dirtyStatus;
        }
        if (Core::Status paintStatus = markPaintDirty(node); !paintStatus)
        {
            if (reservedNewAllocation)
            {
                textStorage.release(reservedAllocation);
            }
            return paintStatus;
        }

        if (utf8.empty())
        {
            textStorage.release(state.allocation);
            state.allocation = {};
            state.length = 0;
            state.metrics = {};
            state.hasContent = false;
            if (Detail::UITextInputState* textInputState =
                    behaviorStateStorage.tryTextInputState(node.index());
                textInputState != nullptr)
            {
                textInputState->selection = {};
                if (clearActiveIme)
                {
                    resetImeCompositionState();
                }
            }
            return Core::success();
        }

        if (reservedNewAllocation)
        {
            textStorage.release(state.allocation);
            state.allocation = reservedAllocation;
        }
        textStorage.write(state.allocation, utf8);
        state.length = static_cast<u32>(utf8.size());
        state.metrics = *metrics;
        state.hasContent = true;
        if (Detail::UITextInputState* textInputState =
                behaviorStateStorage.tryTextInputState(node.index());
            textInputState != nullptr)
        {
            UITextSelection& selection = textInputState->selection;
            selection.anchorCodepoint = (std::min)(selection.anchorCodepoint, metrics->codepointCount);
            selection.caretCodepoint = (std::min)(selection.caretCodepoint, metrics->codepointCount);
            if (clearActiveIme)
            {
                resetImeCompositionState();
            }
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status setTextStyleFromUpdater(UINodeId updaterRoot, UINodeId node, const UITextStyle& style)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }
        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr || !supportsWidgetText(record->kind))
        {
            return fail(UIErrorCode::InvalidText, "UI text style requires an element with intrinsic text content");
        }

        WidgetTextState& state = textStatesByIndex[node.index()];
        if (state.style == style)
        {
            detachThemeBinding(node.index(), ThemeBindingTextStyle);
            return Core::success();
        }

        UITextMetrics metrics{};
        if (state.hasContent)
        {
            auto measured = measureWidgetText(textViewFor(node.index()), style);
            if (!measured)
            {
                return Core::failure(measured.error());
            }
            metrics = *measured;
        }

        if (Core::Status dirtyStatus = markLayoutStyleDirty(node); !dirtyStatus)
        {
            return dirtyStatus;
        }
        if (Core::Status paintStatus = markPaintDirty(node); !paintStatus)
        {
            return paintStatus;
        }
        state.style = style;
        state.metrics = metrics;
        detachThemeBinding(node.index(), ThemeBindingTextStyle);
        return Core::success();
    }

    [[nodiscard]] Core::Status setContentAlignmentFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                               UIContentAlignment alignment)
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
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }
        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr || !supportsWidgetText(record->kind))
        {
            return fail(UIErrorCode::InvalidText, "UI content alignment requires an element with intrinsic text");
        }
        if (!isValidContentAlignment(alignment))
        {
            return fail(UIErrorCode::InvalidLayout,
                        "UI intrinsic content alignment supports Start, Center, or End on each axis");
        }

        WidgetTextState& state = textStatesByIndex[node.index()];
        if (state.alignment == alignment)
        {
            return Core::success();
        }
        if (Core::Status dirtyStatus = markLayoutStyleDirty(node); !dirtyStatus)
        {
            return dirtyStatus;
        }
        if (Core::Status paintStatus = markPaintDirty(node); !paintStatus)
        {
            return paintStatus;
        }
        state.alignment = alignment;
        return Core::success();
    }

    [[nodiscard]] Core::Result<std::string_view> textFromUpdater(UINodeId updaterRoot, UINodeId node)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }
        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr || !supportsWidgetText(record->kind))
        {
            return fail(UIErrorCode::InvalidText, "UI text requires an element with intrinsic text content");
        }
        return textViewFor(node.index());
    }

    [[nodiscard]] Core::Result<UITextStyle> textStyleFromUpdater(UINodeId updaterRoot, UINodeId node)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }
        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr || !supportsWidgetText(record->kind))
        {
            return fail(UIErrorCode::InvalidText, "UI text style requires an element with intrinsic text content");
        }
        return textStatesByIndex[node.index()].style;
    }

    [[nodiscard]] Core::Result<UIContentAlignment> contentAlignmentFromUpdater(UINodeId updaterRoot,
                                                                                UINodeId node)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }
        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr || !supportsWidgetText(record->kind))
        {
            return fail(UIErrorCode::InvalidText, "UI content alignment requires an element with intrinsic text");
        }
        return textStatesByIndex[node.index()].alignment;
    }

    [[nodiscard]] Core::Status setTextSelectionFromUpdater(UINodeId updaterRoot, UINodeId textEdit,
                                                           UITextSelection selection)
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
        auto nodeResult = resolveNode(textEdit);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        Detail::UITextInputState* state = behaviorStateStorage.tryTextInputState(textEdit.index());
        if (state == nullptr)
        {
            return fail(UIErrorCode::InvalidText, "UI selection requires a TextInput behavior");
        }
        if (!isNodeWithinRoot(updaterRoot, textEdit))
        {
            return fail(UIErrorCode::InvalidNode, "UI TextEdit is not owned by the updater root");
        }
        const u32 codepointCount = textStatesByIndex[textEdit.index()].metrics.codepointCount;
        if (selection.anchorCodepoint > codepointCount || selection.caretCodepoint > codepointCount)
        {
            return fail(UIErrorCode::InvalidText, "UI TextEdit selection exceeds the text length");
        }
        if (state->selection == selection)
        {
            return Core::success();
        }
        if (Core::Status dirtyStatus = markPaintDirty(textEdit); !dirtyStatus)
        {
            return dirtyStatus;
        }
        if (textInputFocus == textEdit)
        {
            static_cast<void>(clearImeComposition());
        }
        state->selection = selection;
        return Core::success();
    }

    [[nodiscard]] Core::Result<UITextSelection> textSelectionFromUpdater(UINodeId updaterRoot, UINodeId textEdit) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto nodeResult = const_cast<Impl*>(this)->resolveNode(textEdit);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        const Detail::UITextInputState* state = behaviorStateStorage.tryTextInputState(textEdit.index());
        if (state == nullptr)
        {
            return fail(UIErrorCode::InvalidText, "UI selection requires a TextInput behavior");
        }
        if (!isNodeWithinRoot(updaterRoot, textEdit))
        {
            return fail(UIErrorCode::InvalidNode, "UI TextEdit is not owned by the updater root");
        }
        return state->selection;
    }

    [[nodiscard]] Core::Status setTextEditPaintFromUpdater(UINodeId updaterRoot, UINodeId textEdit,
                                                           const UITextEditPaint& paint)
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
        auto nodeResult = resolveNode(textEdit);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        const NodeRecord* record = nodes.tryGet(textEdit.storageId());
        if (record == nullptr || record->kind != BuiltinElementKind::TextEdit)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TextEdit paint requires a TextEdit node");
        }
        if (!isNodeWithinRoot(updaterRoot, textEdit))
        {
            return fail(UIErrorCode::InvalidNode, "UI TextEdit is not owned by the updater root");
        }
        UITextEditPaint& currentPaint = textEditPaintsByNodeIndex[textEdit.index()];
        if (currentPaint == paint)
        {
            if ((styleOverridesByNodeIndex[textEdit.index()] &
                 static_cast<u16>(UIStyleOverride::TextEditPaint)) != 0)
            {
                return Core::success();
            }
            if (Core::Status dirty = markPaintDirty(textEdit); !dirty)
            {
                return dirty;
            }
            detachThemeBinding(textEdit.index(), ThemeBindingTextEditPaint);
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(textEdit); !dirty)
        {
            return dirty;
        }
        currentPaint = paint;
        detachThemeBinding(textEdit.index(), ThemeBindingTextEditPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Result<UITextEditPaint> textEditPaintFromUpdater(UINodeId updaterRoot,
                                                                         UINodeId textEdit) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto nodeResult = const_cast<Impl*>(this)->resolveNode(textEdit);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        const NodeRecord* record = nodes.tryGet(textEdit.storageId());
        if (record == nullptr || record->kind != BuiltinElementKind::TextEdit)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TextEdit paint requires a TextEdit node");
        }
        if (!isNodeWithinRoot(updaterRoot, textEdit))
        {
            return fail(UIErrorCode::InvalidNode, "UI TextEdit is not owned by the updater root");
        }
        return textEditPaintsByNodeIndex[textEdit.index()];
    }

    [[nodiscard]] Core::Status setButtonActionFromUpdater(UINodeId updaterRoot, UINodeId button,
                                                          UIButtonActionCallback&& callback)
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
        auto buttonResult = resolveButton(button);
        if (!buttonResult)
        {
            return Core::failure(buttonResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, button))
        {
            return fail(UIErrorCode::InvalidNode, "UI Button is not owned by the updater root");
        }
        if (!callback.hasValue())
        {
            return fail(UIErrorCode::InvalidButtonAction, "UI Button action callback is empty");
        }

        auto registration = buttonActionRegistry.stage(
            button, std::move(callback), routeDispatchDepth != 0);
        if (!registration)
        {
            return Core::failure(registration.error());
        }

        const auto rollbackRegistration = [&](Core::Error error) {
            buttonActionRegistry.rollback(*registration, routeDispatchDepth != 0);
            return Core::failure(std::move(error));
        };

        if (!contains(updaterRoot))
        {
            return rollbackRegistration(makeError(
                UIErrorCode::RootRequired,
                "UI tree updater root was released while setting a Button action"));
        }
        auto liveButtonResult = resolveButton(button);
        if (!liveButtonResult)
        {
            return rollbackRegistration(liveButtonResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, button))
        {
            return rollbackRegistration(
                makeError(UIErrorCode::InvalidNode, "UI Button left the updater root while setting its action"));
        }
        if (!buttonActionRegistry.canCommit(*registration))
        {
            return rollbackRegistration(
                makeError(UIErrorCode::InvalidButtonAction, "UI Button action changed during callback transfer"));
        }
        Core::Status committed = buttonActionRegistry.commit(
            *registration, routeDispatchDepth != 0);
        if (!committed)
        {
            return rollbackRegistration(committed.error());
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status clearButtonActionFromUpdater(UINodeId updaterRoot, UINodeId button)
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
        auto buttonResult = resolveButton(button);
        if (!buttonResult)
        {
            return Core::failure(buttonResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, button))
        {
            return fail(UIErrorCode::InvalidNode, "UI Button is not owned by the updater root");
        }

        buttonActionRegistry.clear(
            button, routeDispatchDepth != 0 ? buttonRouteSerial : 0, routeDispatchDepth != 0);
        return Core::success();
    }

    [[nodiscard]] Core::Result<bool> isButtonPressedFromUpdater(UINodeId updaterRoot, UINodeId button)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto buttonResult = resolveButton(button);
        if (!buttonResult)
        {
            return Core::failure(buttonResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, button))
        {
            return fail(UIErrorCode::InvalidNode, "UI Button is not owned by the updater root");
        }
        return isButtonPressed(button);
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveCheckbox(UINodeId checkbox)
    {
        auto nodeResult = resolveNode(checkbox);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != BuiltinElementKind::Checkbox)
        {
            return fail(UIErrorCode::InvalidButtonAction, "UI Checkbox API requires a Checkbox node");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveToggle(UINodeId node)
    {
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!behaviorStateStorage.hasToggle(node.index()))
        {
            return fail(UIErrorCode::InvalidButtonAction,
                        "UI checked state requires a Toggle-capable Element");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Status setCheckboxActionFromUpdater(UINodeId updaterRoot, UINodeId checkbox,
                                                            UIButtonActionCallback&& callback)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        auto checkboxResult = resolveCheckbox(checkbox);
        if (!checkboxResult)
        {
            return Core::failure(checkboxResult.error());
        }
        return setButtonActionFromUpdater(updaterRoot, checkbox, std::move(callback));
    }

    [[nodiscard]] Core::Status clearCheckboxActionFromUpdater(UINodeId updaterRoot, UINodeId checkbox)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        auto checkboxResult = resolveCheckbox(checkbox);
        if (!checkboxResult)
        {
            return Core::failure(checkboxResult.error());
        }
        return clearButtonActionFromUpdater(updaterRoot, checkbox);
    }

    [[nodiscard]] Core::Status setCheckboxPaintFromUpdater(UINodeId updaterRoot, UINodeId checkbox,
                                                           const UICheckboxPaint& paint)
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
        auto checkboxResult = resolveCheckbox(checkbox);
        if (!checkboxResult)
        {
            return Core::failure(checkboxResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, checkbox))
        {
            return fail(UIErrorCode::InvalidNode, "UI Checkbox is not owned by the updater root");
        }
        if (checkbox.index() >= checkboxPaintsByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI Checkbox paint index out of range");
        }
        if (!std::isfinite(paint.checkedIndicatorInset) || paint.checkedIndicatorInset < 0.0F)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI Checkbox paint inset must be finite and non-negative");
        }
        UICheckboxPaint& currentPaint = checkboxPaintsByNodeIndex[checkbox.index()];
        if (currentPaint == paint)
        {
            if ((styleOverridesByNodeIndex[checkbox.index()] &
                 static_cast<u16>(UIStyleOverride::CheckboxPaint)) != 0)
            {
                return Core::success();
            }
            if (Core::Status dirty = markPaintDirty(checkbox); !dirty)
            {
                return dirty;
            }
            detachThemeBinding(checkbox.index(), ThemeBindingCheckboxPaint);
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(checkbox); !dirty)
        {
            return dirty;
        }
        currentPaint = paint;
        detachThemeBinding(checkbox.index(), ThemeBindingCheckboxPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Result<UICheckboxPaint> checkboxPaintFromUpdater(UINodeId updaterRoot, UINodeId checkbox) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto checkboxResult = const_cast<Impl*>(this)->resolveCheckbox(checkbox);
        if (!checkboxResult)
        {
            return Core::failure(checkboxResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, checkbox))
        {
            return fail(UIErrorCode::InvalidNode, "UI Checkbox is not owned by the updater root");
        }
        if (checkbox.index() >= checkboxPaintsByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI Checkbox paint index out of range");
        }
        return checkboxPaintsByNodeIndex[checkbox.index()];
    }

    [[nodiscard]] Core::Status setCheckedFromUpdater(UINodeId updaterRoot, UINodeId checkbox, bool checked)
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
        auto toggleResult = resolveToggle(checkbox);
        if (!toggleResult)
        {
            return Core::failure(toggleResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, checkbox))
        {
            return fail(UIErrorCode::InvalidNode, "UI Toggle element is not owned by the updater root");
        }
        u8* currentValue = behaviorStateStorage.tryToggleValue(checkbox.index());
        if (currentValue == nullptr)
        {
            return fail(Core::CoreErrorCode::Internal, "UI Toggle state index is out of range");
        }
        const u8 next = checked ? 1 : 0;
        if (*currentValue != next)
        {
            if (Core::Status dirty = markPaintDirty(checkbox); !dirty)
            {
                return dirty;
            }
            *currentValue = next;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<bool> isCheckedFromUpdater(UINodeId updaterRoot, UINodeId checkbox) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        // const_cast: resolveToggle is non-const only for API reuse of resolveNode.
        auto toggleResult = const_cast<Impl*>(this)->resolveToggle(checkbox);
        if (!toggleResult)
        {
            return Core::failure(toggleResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, checkbox))
        {
            return fail(UIErrorCode::InvalidNode, "UI Checkbox is not owned by the updater root");
        }
        const u8* currentValue = behaviorStateStorage.tryToggleValue(checkbox.index());
        if (currentValue == nullptr)
        {
            return fail(Core::CoreErrorCode::Internal, "UI Toggle state index is out of range");
        }
        return *currentValue != 0;
    }

    [[nodiscard]] Core::Result<bool> isCheckboxPressedFromUpdater(UINodeId updaterRoot, UINodeId checkbox)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        auto checkboxResult = resolveCheckbox(checkbox);
        if (!checkboxResult)
        {
            return Core::failure(checkboxResult.error());
        }
        return isButtonPressedFromUpdater(updaterRoot, checkbox);
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveSlider(UINodeId slider)
    {
        auto nodeResult = resolveNode(slider);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != BuiltinElementKind::Slider)
        {
            return fail(UIErrorCode::InvalidButtonAction, "UI Slider API requires a Slider node");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveRangeInput(UINodeId node)
    {
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!hasBehavior((*nodeResult)->behaviors, UIElementBehavior::RangeInput) ||
            behaviorStateStorage.tryRangeInputState(node.index()) == nullptr)
        {
            return fail(UIErrorCode::InvalidButtonAction, "UI RangeInput API requires a RangeInput-capable node");
        }
        return *nodeResult;
    }

    [[nodiscard]] bool isInteractiveRangeInput(UINodeId node) const noexcept
    {
        if (!node.hasValue() || node.index() >= semanticsStatesByNodeIndex.size() || !isNodeEnabled(node) ||
            behaviorStateStorage.tryRangeInputState(node.index()) == nullptr)
        {
            return false;
        }
        const NodeRecord* record = nodes.tryGet(node.storageId());
        return record != nullptr && hasBehavior(record->behaviors, UIElementBehavior::RangeInput) &&
               !semanticsStatesByNodeIndex[node.index()].readOnly;
    }

    [[nodiscard]] Core::Result<bool> applySliderValue(UINodeId slider, double requestedValue,
                                                       Platform::PlatformFrameId platformFrame,
                                                       u64 sourceSequence, bool requireInteractive)
    {
        if (!slider.hasValue())
        {
            return false;
        }
        const NodeRecord* record = nodes.tryGet(slider.storageId());
        Detail::UIRangeInputState* state = behaviorStateStorage.tryRangeInputState(slider.index());
        if (record == nullptr || !hasBehavior(record->behaviors, UIElementBehavior::RangeInput) || state == nullptr)
        {
            return false;
        }
        if (requireInteractive && !isInteractiveRangeInput(slider))
        {
            return false;
        }
        if (!std::isfinite(requestedValue) || !std::isfinite(state->minValue) || !std::isfinite(state->maxValue) ||
            !(state->maxValue > state->minValue))
        {
            return fail(UIErrorCode::InvalidButtonAction, "UI Slider value update requires a finite range and value");
        }
        const float next = quantizeSliderValue(requestedValue, state->minValue, state->maxValue, state->step);
        if (next == state->value)
        {
            return false;
        }
        if (Core::Status dirty = markPaintDirty(slider); !dirty)
        {
            return Core::failure(dirty.error());
        }
        state->value = next;
        invokeSliderChangeCallback(captureSliderChangeCallback(slider), UISliderChangeEvent{
                                                                         .sliderNode = slider,
                                                                         .value = next,
                                                                         .platformFrame = platformFrame,
                                                                         .sourceSequence = sourceSequence,
                                                                     });
        return true;
    }

    // Map pointer X into [min,max] using last committed hit worldRect for the slider.
    [[nodiscard]] Core::Result<bool> applySliderValueFromPointer(UINodeId slider, UILogicalPoint position,
                                                                 Platform::PlatformFrameId platformFrame,
                                                                 u64 sourceSequence)
    {
        if (!slider.hasValue() || slider.index() >= sliderPaintsByNodeIndex.size())
        {
            return false;
        }
        const NodeRecord* record = nodes.tryGet(slider.storageId());
        if (record == nullptr || record->kind != BuiltinElementKind::Slider || !isInteractiveRangeInput(slider))
        {
            return false;
        }
        Detail::UIRangeInputState* state = behaviorStateStorage.tryRangeInputState(slider.index());
        if (state == nullptr || !(state->maxValue > state->minValue) || !std::isfinite(state->minValue) ||
            !std::isfinite(state->maxValue))
        {
            return false;
        }

        UILogicalRect worldRect{};
        bool foundRect = false;
        const UICommittedHitView hit = committedHit();
        for (const UICommittedHitEntry& entry : hit.entries())
        {
            if (entry.node == slider)
            {
                worldRect = entry.worldRect;
                foundRect = true;
                break;
            }
        }
        if (!foundRect || !(worldRect.width > 0.0F))
        {
            return false;
        }

        const auto next = resolveSliderValueFromPointer(
            worldRect, sliderPaintsByNodeIndex[slider.index()], position.x, state->minValue, state->maxValue,
            state->step);
        if (!next)
        {
            return false;
        }
        return applySliderValue(slider, *next, platformFrame, sourceSequence, true);
    }

    [[nodiscard]] u32 textEditCodepointFromPointer(UINodeId textEdit, UILogicalPoint position) const noexcept
    {
        if (!isLiveTextEdit(textEdit))
        {
            return 0;
        }
        UICommittedContentPlacement placement{};
        bool foundPlacement = false;
        const UICommittedLayoutView layout = committedLayout();
        for (const UICommittedLayoutEntry& entry : layout.entries())
        {
            if (entry.node == textEdit)
            {
                placement = entry.contentPlacement;
                foundPlacement = true;
                break;
            }
        }
        const WidgetTextState& textState = textStatesByIndex[textEdit.index()];
        const u32 codepointCount = textState.metrics.codepointCount;
        if (!foundPlacement || codepointCount == 0)
        {
            return 0;
        }
        const float relativeX = position.x - placement.origin.x;
        const float fallbackAdvance =
            textState.style.logicalSize * textState.style.advanceScale;

        if (textRasterizer && textFace.hasValue())
        {
            auto raster = textRasterizer->raster(textFace, textViewFor(textEdit.index()), textState.style);
            if (raster)
            {
                return textEditCodepointFromHorizontalPosition(
                    relativeX, codepointCount, fallbackAdvance, raster->glyphs);
            }
        }
        return textEditCodepointFromHorizontalPosition(
            relativeX, codepointCount, fallbackAdvance);
    }

    [[nodiscard]] Core::Status updateTextEditSelectionFromPointer(UINodeId textEdit, UILogicalPoint position,
                                                                  bool extendSelection)
    {
        if (!isLiveTextEdit(textEdit))
        {
            return Core::success();
        }
        Detail::UITextInputState* state = behaviorStateStorage.tryTextInputState(textEdit.index());
        if (state == nullptr)
        {
            return fail(Core::CoreErrorCode::Internal, "UI TextEdit is missing TextInput behavior state");
        }
        UITextSelection next = state->selection;
        next.caretCodepoint = textEditCodepointFromPointer(textEdit, position);
        if (!extendSelection)
        {
            next.anchorCodepoint = next.caretCodepoint;
        }
        if (next == state->selection)
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(textEdit); !dirty)
        {
            return dirty;
        }
        if (Core::Status composition = clearImeComposition(); !composition)
        {
            return composition;
        }
        state->selection = next;
        return Core::success();
    }

    [[nodiscard]] Core::Status setSliderRangeFromUpdater(UINodeId updaterRoot, UINodeId slider, float minValue,
                                                         float maxValue, float step)
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
        auto sliderResult = resolveRangeInput(slider);
        if (!sliderResult)
        {
            return Core::failure(sliderResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, slider))
        {
            return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
        }
        if (!std::isfinite(minValue) || !std::isfinite(maxValue) || !(maxValue > minValue) || (step < 0.0F) ||
            !std::isfinite(step))
        {
            return fail(UIErrorCode::InvalidButtonAction, "UI Slider range/step is invalid");
        }
        Detail::UIRangeInputState& state = *behaviorStateStorage.tryRangeInputState(slider.index());
        const float nextValue = quantizeSliderValue(state.value, minValue, maxValue, step);
        if (state.minValue == minValue && state.maxValue == maxValue && state.step == step && state.value == nextValue)
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(slider); !dirty)
        {
            return dirty;
        }
        const float previousValue = state.value;
        state.minValue = minValue;
        state.maxValue = maxValue;
        state.step = step;
        state.value = nextValue;
        if (state.value != previousValue)
        {
            invokeSliderChangeCallback(captureSliderChangeCallback(slider), UISliderChangeEvent{
                                                                                .sliderNode = slider,
                                                                                .value = state.value,
                                                                                .platformFrame = {},
                                                                                .sourceSequence = 0,
                                                                            });
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status setSliderValueFromUpdater(UINodeId updaterRoot, UINodeId slider, float value)
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
        auto sliderResult = resolveRangeInput(slider);
        if (!sliderResult)
        {
            return Core::failure(sliderResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, slider))
        {
            return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
        }
        if (!std::isfinite(value))
        {
            return fail(UIErrorCode::InvalidButtonAction, "UI Slider value must be finite");
        }
        auto applied = applySliderValue(slider, value, {}, 0, false);
        if (!applied)
        {
            return Core::failure(applied.error());
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<float> sliderValueFromUpdater(UINodeId updaterRoot, UINodeId slider) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto sliderResult = const_cast<Impl*>(this)->resolveRangeInput(slider);
        if (!sliderResult)
        {
            return Core::failure(sliderResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, slider))
        {
            return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
        }
        return behaviorStateStorage.tryRangeInputState(slider.index())->value;
    }

    [[nodiscard]] Core::Status setSliderPaintFromUpdater(UINodeId updaterRoot, UINodeId slider,
                                                         const UISliderPaint& paint)
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
        auto sliderResult = resolveSlider(slider);
        if (!sliderResult)
        {
            return Core::failure(sliderResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, slider))
        {
            return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
        }
        if (!std::isfinite(paint.contentInset) || paint.contentInset < 0.0F || !std::isfinite(paint.thumbWidth) ||
            paint.thumbWidth < 0.0F)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI Slider paint metrics must be finite and non-negative");
        }
        UISliderPaint& state = sliderPaintsByNodeIndex[slider.index()];
        if (state == paint)
        {
            detachThemeBinding(slider.index(), ThemeBindingSliderPaint);
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(slider); !dirty)
        {
            return dirty;
        }
        state = paint;
        detachThemeBinding(slider.index(), ThemeBindingSliderPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Result<UISliderPaint> sliderPaintFromUpdater(UINodeId updaterRoot, UINodeId slider) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto sliderResult = const_cast<Impl*>(this)->resolveSlider(slider);
        if (!sliderResult)
        {
            return Core::failure(sliderResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, slider))
        {
            return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
        }
        return sliderPaintsByNodeIndex[slider.index()];
    }

    [[nodiscard]] Core::Status setSliderChangeCallbackFromUpdater(UINodeId updaterRoot, UINodeId slider,
                                                                  UISliderChangeCallback&& callback)
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
        auto sliderResult = resolveSlider(slider);
        if (!sliderResult)
        {
            return Core::failure(sliderResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, slider))
        {
            return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
        }
        if (!callback.hasValue())
        {
            return fail(UIErrorCode::InvalidButtonAction, "UI Slider change callback is empty");
        }

        auto registration = sliderChangeCallbackRegistry.stage(
            slider, std::move(callback), routeDispatchDepth != 0);
        if (!registration)
        {
            return Core::failure(registration.error());
        }

        const auto rollbackRegistration = [&](Core::Error error) {
            sliderChangeCallbackRegistry.rollback(*registration, routeDispatchDepth != 0);
            return Core::failure(std::move(error));
        };

        if (!contains(updaterRoot))
        {
            return rollbackRegistration(makeError(
                UIErrorCode::RootRequired,
                "UI tree updater root was released while setting a Slider callback"));
        }
        auto liveSliderResult = resolveSlider(slider);
        if (!liveSliderResult)
        {
            return rollbackRegistration(liveSliderResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, slider))
        {
            return rollbackRegistration(
                makeError(UIErrorCode::InvalidNode, "UI Slider left the updater root while setting its callback"));
        }
        if (!sliderChangeCallbackRegistry.canCommit(*registration))
        {
            return rollbackRegistration(makeError(
                UIErrorCode::InvalidButtonAction,
                "UI Slider change callback changed during callback transfer"));
        }

        sliderChangeCallbackRegistry.commit(*registration, routeDispatchDepth != 0);
        return Core::success();
    }

    [[nodiscard]] Core::Status clearSliderChangeCallbackFromUpdater(UINodeId updaterRoot, UINodeId slider)
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
        auto sliderResult = resolveSlider(slider);
        if (!sliderResult)
        {
            return Core::failure(sliderResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, slider))
        {
            return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
        }
        sliderChangeCallbackRegistry.clear(slider, routeDispatchDepth != 0);
        return Core::success();
    }

    [[nodiscard]] Core::Result<bool> isSliderDraggingFromUpdater(UINodeId updaterRoot, UINodeId slider) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto sliderResult = const_cast<Impl*>(this)->resolveSlider(slider);
        if (!sliderResult)
        {
            return Core::failure(sliderResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, slider))
        {
            return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
        }
        return armedSlider == slider;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveScrollView(UINodeId scrollView)
    {
        auto nodeResult = resolveNode(scrollView);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != BuiltinElementKind::ScrollView ||
            scrollView.index() >= scrollViewPaintsByNodeIndex.size())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI ScrollView API requires a ScrollView node");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveScroll(UINodeId node)
    {
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!hasBehavior((*nodeResult)->behaviors, UIElementBehavior::Scroll) ||
            behaviorStateStorage.tryScrollState(node.index()) == nullptr)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI Scroll API requires a Scroll-capable node");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Status markScrollOffsetDirty(UINodeId scrollView)
    {
        if (Core::Status dirty = markHitTestDirty(scrollView); !dirty)
        {
            return dirty;
        }
        const u32 index = scrollView.index();
        dirtyQueueStorage.flags(index) |=
            UIDirty::Arrange | UIDirty::Composite | UIDirty::Paint | UIDirty::Semantics;
        phaseDirty |= PhaseLayout | PhaseHit | PhasePaint | PhaseSemantics;
        return Core::success();
    }

    [[nodiscard]] Core::Status markLayoutStyleDirty(UINodeId node)
    {
        return markLayoutDirtyBatch({node});
    }

    [[nodiscard]] bool isLiveScrollView(UINodeId scrollView) const noexcept
    {
        if (!scrollView.hasValue() || !contains(scrollView) || scrollView.index() >= scrollViewPaintsByNodeIndex.size() ||
            behaviorStateStorage.tryScrollState(scrollView.index()) == nullptr)
        {
            return false;
        }
        const NodeRecord* record = nodes.tryGet(scrollView.storageId());
        return record != nullptr && record->kind == BuiltinElementKind::ScrollView;
    }

    [[nodiscard]] bool isLiveListView(UINodeId listView) const noexcept
    {
        if (!listView.hasValue() || !contains(listView) || listView.index() >= listViewStatesByNodeIndex.size())
        {
            return false;
        }
        const NodeRecord* record = nodes.tryGet(listView.storageId());
        return record != nullptr && record->kind == BuiltinElementKind::ListView;
    }

    [[nodiscard]] bool isLiveTreeView(UINodeId treeView) const noexcept
    {
        if (!treeView.hasValue() || !contains(treeView) || treeView.index() >= treeViewStatesByNodeIndex.size())
        {
            return false;
        }
        const NodeRecord* record = nodes.tryGet(treeView.storageId());
        return record != nullptr && record->kind == BuiltinElementKind::TreeView;
    }

    [[nodiscard]] bool isLiveVirtualView(UINodeId node) const noexcept
    {
        return isLiveListView(node) || isLiveTreeView(node);
    }

    [[nodiscard]] bool isLiveScrollable(UINodeId node) const noexcept
    {
        return isLiveScrollView(node) || isLiveVirtualView(node);
    }

    [[nodiscard]] ScrollBarGeometry committedScrollBarGeometry(UINodeId scrollView, UIScrollAxes axis) const noexcept
    {
        if (isLiveListView(scrollView))
        {
            if (axis != UIScrollAxes::Vertical)
            {
                return {};
            }
            const ListViewState& state = listViewStatesByNodeIndex[scrollView.index()];
            return makeListViewScrollBarGeometry(state.committedMetrics, state.committedViewportRect,
                                                 state.paint.scrollBar);
        }
        if (isLiveTreeView(scrollView))
        {
            if (axis != UIScrollAxes::Vertical)
            {
                return {};
            }
            const TreeViewState& state = treeViewStatesByNodeIndex[scrollView.index()];
            return makeTreeViewScrollBarGeometry(state.committedMetrics, state.committedViewportRect,
                                                 state.paint.scrollBar);
        }
        if (!isLiveScrollView(scrollView))
        {
            return {};
        }
        const UIScrollBehaviorState* state = behaviorStateStorage.tryScrollState(scrollView.index());
        if (state == nullptr)
        {
            return {};
        }
        return makeScrollBarGeometry(state->committedMetrics, state->committedViewportRect,
                                     scrollViewPaintsByNodeIndex[scrollView.index()], axis);
    }

    [[nodiscard]] Core::Result<bool> applyScrollOffsetFromInput(UINodeId scrollView, UIScrollOffset requested)
    {
        if (!isLiveScrollable(scrollView) || !isNodeEnabled(scrollView))
        {
            return false;
        }
        if (isLiveListView(scrollView))
        {
            ListViewState& state = listViewStatesByNodeIndex[scrollView.index()];
            requested.x = 0.0F;
            requested.y = normalizeFloat((std::clamp)(requested.y, 0.0F, state.committedMetrics.maxScrollOffset));
            if (state.requestedScrollOffset == requested.y)
            {
                return false;
            }
            if (Core::Status dirty = markScrollOffsetDirty(scrollView); !dirty)
            {
                return Core::failure(dirty.error());
            }
            state.requestedScrollOffset = requested.y;
            return true;
        }
        if (isLiveTreeView(scrollView))
        {
            TreeViewState& state = treeViewStatesByNodeIndex[scrollView.index()];
            requested.x = 0.0F;
            requested.y = normalizeFloat((std::clamp)(requested.y, 0.0F, state.committedMetrics.maxScrollOffset));
            if (state.requestedScrollOffset == requested.y)
            {
                return false;
            }
            if (Core::Status dirty = markScrollOffsetDirty(scrollView); !dirty)
            {
                return Core::failure(dirty.error());
            }
            state.requestedScrollOffset = requested.y;
            return true;
        }
        UIScrollBehaviorState* state = behaviorStateStorage.tryScrollState(scrollView.index());
        if (state == nullptr)
        {
            return Core::failure(Core::CoreErrorCode::Internal, "UI ScrollView is missing Scroll behavior state");
        }
        const UIScrollViewMetrics& metrics = state->committedMetrics;
        requested.x = hasScrollAxis(state->style.axes, UIScrollAxes::Horizontal)
                          ? normalizeFloat((std::clamp)(requested.x, 0.0F, metrics.maxOffsetX()))
                          : 0.0F;
        requested.y = hasScrollAxis(state->style.axes, UIScrollAxes::Vertical)
                          ? normalizeFloat((std::clamp)(requested.y, 0.0F, metrics.maxOffsetY()))
                          : 0.0F;
        if (state->requestedOffset == requested)
        {
            return false;
        }
        if (Core::Status dirty = markScrollOffsetDirty(scrollView); !dirty)
        {
            return Core::failure(dirty.error());
        }
        state->requestedOffset = requested;
        return true;
    }

    [[nodiscard]] UIScrollOffset resolvedScrollWheelOffset(UINodeId scrollView, UILogicalPoint delta) const noexcept
    {
        if (!isLiveScrollable(scrollView) || !isNodeEnabled(scrollView))
        {
            return {};
        }
        if (isLiveListView(scrollView))
        {
            const ListViewState& state = listViewStatesByNodeIndex[scrollView.index()];
            return UIScrollOffset{
                .x = 0.0F,
                .y = resolveVirtualScrollWheelOffset(
                    state.requestedScrollOffset,
                    state.committedMetrics.maxScrollOffset,
                    state.style.wheelStep, delta),
            };
        }
        if (isLiveTreeView(scrollView))
        {
            const TreeViewState& state = treeViewStatesByNodeIndex[scrollView.index()];
            return UIScrollOffset{
                .x = 0.0F,
                .y = resolveVirtualScrollWheelOffset(
                    state.requestedScrollOffset,
                    state.committedMetrics.maxScrollOffset,
                    state.style.wheelStep, delta),
            };
        }
        const UIScrollBehaviorState* state = behaviorStateStorage.tryScrollState(scrollView.index());
        if (state == nullptr)
        {
            return {};
        }
        return resolveScrollWheelOffset(
            state->requestedOffset, state->style, state->committedMetrics, delta);
    }

    [[nodiscard]] bool scrollWheelWouldChange(UINodeId scrollView, UILogicalPoint delta) const noexcept
    {
        if (!isLiveScrollable(scrollView) || !isNodeEnabled(scrollView))
        {
            return false;
        }
        const UIScrollOffset resolved = resolvedScrollWheelOffset(scrollView, delta);
        if (isLiveListView(scrollView))
        {
            return listViewStatesByNodeIndex[scrollView.index()].requestedScrollOffset != resolved.y;
        }
        if (isLiveTreeView(scrollView))
        {
            return treeViewStatesByNodeIndex[scrollView.index()].requestedScrollOffset != resolved.y;
        }
        const UIScrollBehaviorState* state = behaviorStateStorage.tryScrollState(scrollView.index());
        return state != nullptr && state->requestedOffset != resolved;
    }

    [[nodiscard]] Core::Result<bool> applyScrollWheel(UINodeId scrollView, UILogicalPoint delta)
    {
        if (!isLiveScrollable(scrollView) || !isNodeEnabled(scrollView))
        {
            return false;
        }
        const UIScrollOffset next = resolvedScrollWheelOffset(scrollView, delta);
        return applyScrollOffsetFromInput(scrollView, next);
    }

    [[nodiscard]] Core::Result<bool> applyScrollThumbFromPointer(UINodeId scrollView, UIScrollAxes axis,
                                                                UILogicalPoint position, float grabOffset)
    {
        if (!isLiveScrollable(scrollView) || !isNodeEnabled(scrollView) ||
            (isLiveVirtualView(scrollView) && axis != UIScrollAxes::Vertical))
        {
            return false;
        }
        const bool listView = isLiveListView(scrollView);
        const bool treeView = isLiveTreeView(scrollView);
        const UIScrollBehaviorState* scrollState =
            listView || treeView ? nullptr : behaviorStateStorage.tryScrollState(scrollView.index());
        if (!listView && !treeView && scrollState == nullptr)
        {
            return false;
        }
        const ScrollBarGeometry geometry = committedScrollBarGeometry(scrollView, axis);
        const float maxOffset =
            listView ? listViewStatesByNodeIndex[scrollView.index()].committedMetrics.maxScrollOffset
            : treeView
                ? treeViewStatesByNodeIndex[scrollView.index()].committedMetrics.maxScrollOffset
                : scrollAxisMaxOffset(scrollState->committedMetrics, axis);
        const auto axisOffset = resolveScrollThumbOffset(
            geometry, axis, position, grabOffset, maxOffset);
        if (!axisOffset)
        {
            return false;
        }
        UIScrollOffset next =
            listView
                ? UIScrollOffset{.x = 0.0F, .y = listViewStatesByNodeIndex[scrollView.index()].requestedScrollOffset}
            : treeView
                ? UIScrollOffset{.x = 0.0F, .y = treeViewStatesByNodeIndex[scrollView.index()].requestedScrollOffset}
                                  : scrollState->requestedOffset;
        setScrollAxisOffset(next, axis, *axisOffset);
        return applyScrollOffsetFromInput(scrollView, next);
    }

    [[nodiscard]] Core::Result<bool> applyScrollTrackPage(UINodeId scrollView, UIScrollAxes axis,
                                                         UILogicalPoint position)
    {
        if (!isLiveScrollable(scrollView) || !isNodeEnabled(scrollView) ||
            (isLiveVirtualView(scrollView) && axis != UIScrollAxes::Vertical))
        {
            return false;
        }
        const bool listView = isLiveListView(scrollView);
        const bool treeView = isLiveTreeView(scrollView);
        const UIScrollBehaviorState* scrollState =
            listView || treeView ? nullptr : behaviorStateStorage.tryScrollState(scrollView.index());
        if (!listView && !treeView && scrollState == nullptr)
        {
            return false;
        }
        const ScrollBarGeometry geometry = committedScrollBarGeometry(scrollView, axis);
        if (!geometry.visible)
        {
            return false;
        }
        const float pageExtent = listView
                                     ? listViewStatesByNodeIndex[scrollView.index()].committedMetrics.viewportSize.height
            : treeView
                ? treeViewStatesByNodeIndex[scrollView.index()].committedMetrics.viewportSize.height
                                     : (axis == UIScrollAxes::Horizontal
                                            ? scrollState->committedMetrics.viewportSize.width
                                            : scrollState->committedMetrics.viewportSize.height);
        UIScrollOffset next =
            listView
                ? UIScrollOffset{.x = 0.0F, .y = listViewStatesByNodeIndex[scrollView.index()].requestedScrollOffset}
            : treeView
                ? UIScrollOffset{.x = 0.0F, .y = treeViewStatesByNodeIndex[scrollView.index()].requestedScrollOffset}
                                  : scrollState->requestedOffset;
        const auto axisOffset = resolveScrollTrackPageOffset(
            geometry, axis, position, scrollAxisOffset(next, axis), pageExtent);
        if (!axisOffset)
        {
            return false;
        }
        setScrollAxisOffset(next, axis, *axisOffset);
        return applyScrollOffsetFromInput(scrollView, next);
    }

    [[nodiscard]] ScrollBarPointerHit scrollBarPointerHit(std::span<const u32> routePath,
                                                         std::span<const UICommittedHitEntry> entries,
                                                         UILogicalPoint position) const noexcept
    {
        for (const u32 routeEntryIndex : routePath)
        {
            if (routeEntryIndex >= entries.size())
            {
                continue;
            }
            const UINodeId node = entries[routeEntryIndex].node;
            if (!isLiveScrollable(node) || !isNodeEnabled(node))
            {
                continue;
            }
            if (isLiveVirtualView(node))
            {
                const float maxOffset = isLiveListView(node)
                                            ? listViewStatesByNodeIndex[node.index()].committedMetrics.maxScrollOffset
                                            : treeViewStatesByNodeIndex[node.index()].committedMetrics.maxScrollOffset;
                if (!(maxOffset > 0.0F))
                {
                    continue;
                }
                const ScrollBarGeometry geometry = committedScrollBarGeometry(node, UIScrollAxes::Vertical);
                if (geometry.visible && containsPointHalfOpen(geometry.track, position))
                {
                    return ScrollBarPointerHit{
                        .scrollView = node,
                        .axis = UIScrollAxes::Vertical,
                        .geometry = geometry,
                        .thumb = containsPointHalfOpen(geometry.thumb, position),
                    };
                }
                continue;
            }
            const UIScrollBehaviorState* state = behaviorStateStorage.tryScrollState(node.index());
            if (state == nullptr)
            {
                continue;
            }
            for (const UIScrollAxes axis : {UIScrollAxes::Horizontal, UIScrollAxes::Vertical})
            {
                if (!hasScrollAxis(state->style.axes, axis) ||
                    !(scrollAxisMaxOffset(state->committedMetrics, axis) > 0.0F))
                {
                    continue;
                }
                const ScrollBarGeometry geometry = committedScrollBarGeometry(node, axis);
                if (!geometry.visible || !containsPointHalfOpen(geometry.track, position))
                {
                    continue;
                }
                return ScrollBarPointerHit{
                    .scrollView = node,
                    .axis = axis,
                    .geometry = geometry,
                    .thumb = containsPointHalfOpen(geometry.thumb, position),
                };
            }
        }
        return {};
    }

    [[nodiscard]] Core::Status setScrollViewStyleFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                            const UIScrollViewStyle& style)
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
        auto scrollResult = resolveScroll(scrollView);
        if (!scrollResult)
        {
            return Core::failure(scrollResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, scrollView))
        {
            return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
        }
        auto normalized = Detail::normalizeScrollViewStyle(style);
        if (!normalized)
        {
            return Core::failure(normalized.error());
        }
        UIScrollBehaviorState& state = *behaviorStateStorage.tryScrollState(scrollView.index());
        if (state.style == *normalized)
        {
            return Core::success();
        }
        if (Core::Status dirty = markLayoutStyleDirty(scrollView); !dirty)
        {
            return dirty;
        }
        state.style = *normalized;
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIScrollViewStyle> scrollViewStyleFromUpdater(UINodeId updaterRoot,
                                                                             UINodeId scrollView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto scrollResult = const_cast<Impl*>(this)->resolveScroll(scrollView);
        if (!scrollResult)
        {
            return Core::failure(scrollResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, scrollView))
        {
            return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
        }
        return behaviorStateStorage.tryScrollState(scrollView.index())->style;
    }

    [[nodiscard]] Core::Status setScrollViewOffsetFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                             UIScrollOffset offset)
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
        auto scrollResult = resolveScroll(scrollView);
        if (!scrollResult)
        {
            return Core::failure(scrollResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, scrollView))
        {
            return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
        }
        auto normalized = Detail::normalizeScrollOffset(offset);
        if (!normalized)
        {
            return Core::failure(normalized.error());
        }
        UIScrollBehaviorState& state = *behaviorStateStorage.tryScrollState(scrollView.index());
        if (state.requestedOffset == *normalized)
        {
            return Core::success();
        }
        if (Core::Status dirty = markScrollOffsetDirty(scrollView); !dirty)
        {
            return dirty;
        }
        state.requestedOffset = *normalized;
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIScrollOffset> scrollViewOffsetFromUpdater(UINodeId updaterRoot,
                                                                           UINodeId scrollView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto scrollResult = const_cast<Impl*>(this)->resolveScroll(scrollView);
        if (!scrollResult)
        {
            return Core::failure(scrollResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, scrollView))
        {
            return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
        }
        return behaviorStateStorage.tryScrollState(scrollView.index())->requestedOffset;
    }

    [[nodiscard]] Core::Result<UIScrollViewMetrics> scrollViewMetricsFromUpdater(UINodeId updaterRoot,
                                                                                 UINodeId scrollView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto scrollResult = const_cast<Impl*>(this)->resolveScroll(scrollView);
        if (!scrollResult)
        {
            return Core::failure(scrollResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, scrollView))
        {
            return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
        }
        return behaviorStateStorage.tryScrollState(scrollView.index())->committedMetrics;
    }

    [[nodiscard]] Core::Status setScrollViewPaintFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                            const UIScrollViewPaint& paint)
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
        auto scrollResult = resolveScrollView(scrollView);
        if (!scrollResult)
        {
            return Core::failure(scrollResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, scrollView))
        {
            return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
        }
        auto normalized = Detail::normalizeScrollViewPaint(paint);
        if (!normalized)
        {
            return Core::failure(normalized.error());
        }
        UIScrollViewPaint& state = scrollViewPaintsByNodeIndex[scrollView.index()];
        if (state == *normalized)
        {
            detachThemeBinding(scrollView.index(), ThemeBindingScrollViewPaint);
            return Core::success();
        }
        const bool layoutChanged = state.thickness != normalized->thickness ||
                                   state.minThumbExtent != normalized->minThumbExtent;
        Core::Status dirty = layoutChanged ? markLayoutStyleDirty(scrollView) : markPaintDirty(scrollView);
        if (!dirty)
        {
            return dirty;
        }
        state = *normalized;
        detachThemeBinding(scrollView.index(), ThemeBindingScrollViewPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIScrollViewPaint> scrollViewPaintFromUpdater(UINodeId updaterRoot,
                                                                             UINodeId scrollView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto scrollResult = const_cast<Impl*>(this)->resolveScrollView(scrollView);
        if (!scrollResult)
        {
            return Core::failure(scrollResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, scrollView))
        {
            return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
        }
        return scrollViewPaintsByNodeIndex[scrollView.index()];
    }

    [[nodiscard]] Core::Result<bool> isScrollViewDraggingFromUpdater(UINodeId updaterRoot,
                                                                     UINodeId scrollView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto scrollResult = const_cast<Impl*>(this)->resolveScrollView(scrollView);
        if (!scrollResult)
        {
            return Core::failure(scrollResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, scrollView))
        {
            return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
        }
        return scrollThumbDragActive && armedScrollView == scrollView;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolvePopup(UINodeId popup)
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

    [[nodiscard]] Core::Result<NodeRecord*> resolveDropdown(UINodeId dropdown)
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

    [[nodiscard]] Core::Result<NodeRecord*> resolveDropdownItem(UINodeId item)
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

    [[nodiscard]] Core::Result<NodeRecord*> resolveListView(UINodeId listView)
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

    [[nodiscard]] UINodeId listViewForItem(UINodeId item) const noexcept
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

    [[nodiscard]] bool isSelectedListViewItem(UINodeId item) const noexcept
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

    [[nodiscard]] Core::Status selectCommittedListViewItem(UINodeId item)
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

    [[nodiscard]] Core::Result<NodeRecord*> resolveTreeView(UINodeId treeView)
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

    [[nodiscard]] UINodeId treeViewForItem(UINodeId item) const noexcept
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

    [[nodiscard]] bool pointWithinCommittedTreeDisclosure(UINodeId item, UILogicalPoint point,
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

    [[nodiscard]] bool isSelectedTreeViewItem(UINodeId item) const noexcept
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

    [[nodiscard]] Core::Status selectCommittedTreeViewItem(UINodeId item)
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

    [[nodiscard]] Core::Status toggleCommittedTreeViewItem(UINodeId item)
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

    [[nodiscard]] Core::Status setPopupOpenState(UINodeId popup, bool open)
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
        if (open && activePopupNode.hasValue() && activePopupNode != popup && contains(activePopupNode) &&
            activePopupNode.index() < popupStatesByNodeIndex.size() &&
            popupStatesByNodeIndex[activePopupNode.index()].open)
        {
            previousPopup = activePopupNode;
            previousDropdown = dropdownForPopup(previousPopup);
        }
        if (Core::Status dirty = markLayoutDirtyBatch({popup, dropdown, previousPopup, previousDropdown}); !dirty)
        {
            return dirty;
        }

        if (previousPopup.hasValue())
        {
            popupStatesByNodeIndex[previousPopup.index()].open = false;
        }
        const bool focusWasInClosingPopup =
            (!open && isNodeWithinSubtree(popup, defaultActionFocusButton)) ||
            (previousPopup.hasValue() && isNodeWithinSubtree(previousPopup, defaultActionFocusButton));
        if (open)
        {
            focusRestoreByNodeIndex[popup.index()] = defaultActionFocusButton;
            popupState.open = true;
            activePopupNode = popup;
        } else
        {
            popupState.open = false;
            if (activePopupNode == popup)
            {
                activePopupNode = {};
            }
        }
        popupDismissPointerBarrierActive = false;
        dropdownCommandPressLatch.clear();

        if (focusWasInClosingPopup)
        {
            UINodeId nextFocus = open ? dropdown : focusRestoreByNodeIndex[popup.index()];
            if (!nextFocus.hasValue() || !contains(nextFocus) || !isNodeEnabled(nextFocus))
            {
                nextFocus = isNodeEnabled(dropdown) ? dropdown : UINodeId{};
            }
            defaultActionPressState.clearAll();
            clearArmedPrimaryButton();
            clearArmedSlider();
            clearArmedTextEdit();
            capturedPointerNode = {};
            resetImeCompositionState();
            textInputFocus = {};
            defaultActionFocusButton = nextFocus;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status setPopupStyleFromUpdater(UINodeId updaterRoot, UINodeId popup,
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

    [[nodiscard]] Core::Result<UIPopupStyle> popupStyleFromUpdater(UINodeId updaterRoot, UINodeId popup) const
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

    [[nodiscard]] Core::Status setPopupOpenFromUpdater(UINodeId updaterRoot, UINodeId popup, bool open)
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

    [[nodiscard]] Core::Result<bool> isPopupOpenFromUpdater(UINodeId updaterRoot, UINodeId popup) const
    {
        auto styleResult = popupStyleFromUpdater(updaterRoot, popup);
        if (!styleResult)
        {
            return Core::failure(styleResult.error());
        }
        return popupStatesByNodeIndex[popup.index()].open;
    }

    [[nodiscard]] Core::Result<UIPopupMetrics> popupMetricsFromUpdater(UINodeId updaterRoot, UINodeId popup) const
    {
        auto styleResult = popupStyleFromUpdater(updaterRoot, popup);
        if (!styleResult)
        {
            return Core::failure(styleResult.error());
        }
        return popupStatesByNodeIndex[popup.index()].committedMetrics;
    }

    [[nodiscard]] Core::Status setDropdownOpenFromUpdater(UINodeId updaterRoot, UINodeId dropdown, bool open)
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

    [[nodiscard]] Core::Result<bool> isDropdownOpenFromUpdater(UINodeId updaterRoot, UINodeId dropdown) const
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

    [[nodiscard]] Core::Status setDropdownSelectedItemFromUpdater(UINodeId updaterRoot, UINodeId dropdown,
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

    [[nodiscard]] Core::Result<UINodeId> dropdownSelectedItemFromUpdater(UINodeId updaterRoot,
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

    [[nodiscard]] Core::Result<bool> isDropdownItemSelectedFromUpdater(UINodeId updaterRoot,
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

    [[nodiscard]] Core::Status setDropdownPaintFromUpdater(UINodeId updaterRoot, UINodeId dropdown,
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

    [[nodiscard]] Core::Result<UIDropdownPaint> dropdownPaintFromUpdater(UINodeId updaterRoot,
                                                                        UINodeId dropdown) const
    {
        auto openResult = isDropdownOpenFromUpdater(updaterRoot, dropdown);
        if (!openResult)
        {
            return Core::failure(openResult.error());
        }
        return dropdownStatesByNodeIndex[dropdown.index()].paint;
    }

    [[nodiscard]] Core::Status validateListViewUpdater(UINodeId updaterRoot, UINodeId listView) const
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

    [[nodiscard]] Core::Result<UIListViewItemDescriptor> resolveListViewLogicalItem(UINodeId listView,
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

    [[nodiscard]] Core::Status setListViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId listView,
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

    [[nodiscard]] Core::Status clearListViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId listView)
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

    [[nodiscard]] Core::Status invalidateListViewItemsFromUpdater(UINodeId updaterRoot, UINodeId listView)
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

    [[nodiscard]] Core::Status setListViewStyleFromUpdater(UINodeId updaterRoot, UINodeId listView,
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
            layoutStylesByIndex[child].size.height = UILayoutLength::Px(state.style.rowHeight);
            child = childRecord->nextSiblingIndex;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIListViewStyle> listViewStyleFromUpdater(UINodeId updaterRoot,
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

    [[nodiscard]] Core::Status setListViewPaintFromUpdater(UINodeId updaterRoot, UINodeId listView,
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

    [[nodiscard]] Core::Result<UIListViewPaint> listViewPaintFromUpdater(UINodeId updaterRoot,
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

    [[nodiscard]] Core::Result<UIListViewMetrics> listViewMetricsFromUpdater(UINodeId updaterRoot,
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

    [[nodiscard]] Core::Status setListViewSelectedIndexFromUpdater(UINodeId updaterRoot, UINodeId listView,
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

    [[nodiscard]] Core::Status clearListViewSelectionFromUpdater(UINodeId updaterRoot, UINodeId listView)
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

    [[nodiscard]] Core::Result<UIListViewSelection> listViewSelectionFromUpdater(UINodeId updaterRoot,
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

    [[nodiscard]] Core::Status scrollListViewToIndexFromUpdater(UINodeId updaterRoot, UINodeId listView,
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

    [[nodiscard]] Core::Status validateTreeViewUpdater(UINodeId updaterRoot, UINodeId treeView) const
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

    [[nodiscard]] Core::Result<UITreeViewItemDescriptor> resolveTreeViewLogicalItem(UINodeId treeView,
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

    [[nodiscard]] Core::Status setTreeViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId treeView,
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

    [[nodiscard]] Core::Status clearTreeViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId treeView)
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

    [[nodiscard]] Core::Status invalidateTreeViewItemsFromUpdater(UINodeId updaterRoot, UINodeId treeView)
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

    [[nodiscard]] Core::Status setTreeViewStyleFromUpdater(UINodeId updaterRoot, UINodeId treeView,
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
            layoutStylesByIndex[child].size.height = UILayoutLength::Px(state.style.rowHeight);
            child = childRecord->nextSiblingIndex;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<UITreeViewStyle> treeViewStyleFromUpdater(UINodeId updaterRoot, UINodeId treeView) const
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

    [[nodiscard]] Core::Status setTreeViewPaintFromUpdater(UINodeId updaterRoot, UINodeId treeView,
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

    [[nodiscard]] Core::Result<UITreeViewPaint> treeViewPaintFromUpdater(UINodeId updaterRoot, UINodeId treeView) const
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

    [[nodiscard]] Core::Result<UITreeViewMetrics> treeViewMetricsFromUpdater(UINodeId updaterRoot,
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

    [[nodiscard]] Core::Status setTreeViewSelectedIndexFromUpdater(UINodeId updaterRoot, UINodeId treeView,
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

    [[nodiscard]] Core::Status clearTreeViewSelectionFromUpdater(UINodeId updaterRoot, UINodeId treeView)
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

    [[nodiscard]] Core::Result<UITreeViewSelection> treeViewSelectionFromUpdater(UINodeId updaterRoot,
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

    [[nodiscard]] Core::Status setTreeViewItemExpandedFromUpdater(UINodeId updaterRoot, UINodeId treeView,
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

    [[nodiscard]] Core::Status scrollTreeViewToIndexFromUpdater(UINodeId updaterRoot, UINodeId treeView,
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

    void addDropdownActivationDirtyReservationCandidates(UINodeId control)
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

    [[nodiscard]] Core::Result<bool> activateDropdownControl(UINodeId control)
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
        popupDismissPointerBarrierActive = false;
        defaultActionPressState.clearAll();
        resetImeCompositionState();
        textInputFocus = {};
        defaultActionFocusButton = dropdown;
        return true;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveProgressBar(UINodeId progressBar)
    {
        auto nodeResult = resolveNode(progressBar);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != BuiltinElementKind::ProgressBar)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI ProgressBar API requires a ProgressBar node");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolvePlainButton(UINodeId button)
    {
        auto nodeResult = resolveNode(button);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isButtonChromeKind((*nodeResult)->kind))
        {
            return fail(UIErrorCode::InvalidButtonAction,
                        "UI Button paint API requires a Button, Dropdown, or DropdownItem node");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Status setProgressBarRangeFromUpdater(UINodeId updaterRoot, UINodeId progressBar,
                                                              float minValue, float maxValue)
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
        auto progressResult = resolveProgressBar(progressBar);
        if (!progressResult)
        {
            return Core::failure(progressResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, progressBar))
        {
            return fail(UIErrorCode::InvalidNode, "UI ProgressBar is not owned by the updater root");
        }
        if (!std::isfinite(minValue) || !std::isfinite(maxValue) || !(maxValue > minValue))
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI ProgressBar range must be finite with max greater than min");
        }
        ProgressBarState& state = progressBarStatesByNodeIndex[progressBar.index()];
        const float nextValue = std::clamp(state.value, minValue, maxValue);
        if (state.minValue == minValue && state.maxValue == maxValue && state.value == nextValue)
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(progressBar); !dirty)
        {
            return dirty;
        }
        state.minValue = minValue;
        state.maxValue = maxValue;
        state.value = nextValue;
        return Core::success();
    }

    [[nodiscard]] Core::Status setProgressBarValueFromUpdater(UINodeId updaterRoot, UINodeId progressBar, float value)
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
        auto progressResult = resolveProgressBar(progressBar);
        if (!progressResult)
        {
            return Core::failure(progressResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, progressBar))
        {
            return fail(UIErrorCode::InvalidNode, "UI ProgressBar is not owned by the updater root");
        }
        if (!std::isfinite(value))
        {
            return fail(UIErrorCode::InvalidControlValue, "UI ProgressBar value must be finite");
        }
        ProgressBarState& state = progressBarStatesByNodeIndex[progressBar.index()];
        const float nextValue = std::clamp(value, state.minValue, state.maxValue);
        if (state.value == nextValue)
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(progressBar); !dirty)
        {
            return dirty;
        }
        state.value = nextValue;
        return Core::success();
    }

    [[nodiscard]] Core::Result<float> progressBarValueFromUpdater(UINodeId updaterRoot, UINodeId progressBar) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto progressResult = const_cast<Impl*>(this)->resolveProgressBar(progressBar);
        if (!progressResult)
        {
            return Core::failure(progressResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, progressBar))
        {
            return fail(UIErrorCode::InvalidNode, "UI ProgressBar is not owned by the updater root");
        }
        return progressBarStatesByNodeIndex[progressBar.index()].value;
    }

    [[nodiscard]] Core::Status setProgressBarPaintFromUpdater(UINodeId updaterRoot, UINodeId progressBar,
                                                              const UIProgressBarPaint& paint)
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
        auto progressResult = resolveProgressBar(progressBar);
        if (!progressResult)
        {
            return Core::failure(progressResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, progressBar))
        {
            return fail(UIErrorCode::InvalidNode, "UI ProgressBar is not owned by the updater root");
        }
        ProgressBarState& state = progressBarStatesByNodeIndex[progressBar.index()];
        if (state.paint == paint)
        {
            detachThemeBinding(progressBar.index(), ThemeBindingProgressBarPaint);
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(progressBar); !dirty)
        {
            return dirty;
        }
        state.paint = paint;
        detachThemeBinding(progressBar.index(), ThemeBindingProgressBarPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIProgressBarPaint> progressBarPaintFromUpdater(UINodeId updaterRoot,
                                                                               UINodeId progressBar) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto progressResult = const_cast<Impl*>(this)->resolveProgressBar(progressBar);
        if (!progressResult)
        {
            return Core::failure(progressResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, progressBar))
        {
            return fail(UIErrorCode::InvalidNode, "UI ProgressBar is not owned by the updater root");
        }
        return progressBarStatesByNodeIndex[progressBar.index()].paint;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveRadioButton(UINodeId radioButton)
    {
        auto nodeResult = resolveNode(radioButton);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != BuiltinElementKind::RadioButton)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI RadioButton API requires a RadioButton node");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Status preflightDefaultActionActivationDirty(UINodeId target, bool pressedStateChanges) const
    {
        const NodeRecord* targetRecord = nodes.tryGet(target.storageId());
        if (targetRecord == nullptr || !behaviorStateStorage.hasActivate(target.index()))
        {
            return fail(UIErrorCode::InvalidNode, "UI default-action target is stale");
        }

        usize requiredQueueEntries = 0;
        const auto countNode = [this, &requiredQueueEntries](UINodeId node) {
            if (node.hasValue() && contains(node) && !dirtyQueueStorage.isQueued(node.index()) &&
                !dirtyQueueStorage.isReserved(node.index()))
            {
                ++requiredQueueEntries;
            }
        };
        const bool targetStateChanges =
            pressedStateChanges || behaviorStateStorage.hasToggle(target.index());
        if (targetStateChanges)
        {
            countNode(target);
        }

        if (targetRecord->kind == BuiltinElementKind::RadioButton)
        {
            const NodeRecord* parent = recordByIndex(targetRecord->parentIndex);
            if (parent == nullptr)
            {
                return fail(UIErrorCode::InvalidParent, "UI RadioButton requires a live parent group");
            }
            for (u32 childIndex = parent->firstChildIndex; childIndex != InvalidNodeIndex;)
            {
                const NodeRecord* child = recordByIndex(childIndex);
                if (child == nullptr)
                {
                    return fail(Core::CoreErrorCode::Internal, "UI RadioButton group is invalid");
                }
                const u32 nextSiblingIndex = child->nextSiblingIndex;
                if (child->kind == BuiltinElementKind::RadioButton && childIndex < radioButtonStatesByNodeIndex.size() &&
                    radioButtonStatesByNodeIndex[childIndex].selected != (childIndex == target.index()) &&
                    !(targetStateChanges && childIndex == target.index()) && !dirtyQueueStorage.isQueued(childIndex) &&
                    !dirtyQueueStorage.isReserved(childIndex))
                {
                    ++requiredQueueEntries;
                }
                childIndex = nextSiblingIndex;
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

    [[nodiscard]] Core::Status applyRadioButtonSelection(UINodeId radioButton, bool selected)
    {
        NodeRecord* radioRecord = nodes.tryGet(radioButton.storageId());
        if (radioRecord == nullptr || radioRecord->kind != BuiltinElementKind::RadioButton ||
            radioButton.index() >= radioButtonStatesByNodeIndex.size())
        {
            return fail(UIErrorCode::InvalidNode, "UI RadioButton is stale");
        }
        if (!selected)
        {
            RadioButtonState& state = radioButtonStatesByNodeIndex[radioButton.index()];
            if (!state.selected)
            {
                return Core::success();
            }
            if (Core::Status dirty = markPaintDirty(radioButton); !dirty)
            {
                return dirty;
            }
            state.selected = false;
            return Core::success();
        }

        const NodeRecord* parent = recordByIndex(radioRecord->parentIndex);
        if (parent == nullptr)
        {
            return fail(UIErrorCode::InvalidParent, "UI RadioButton requires a live parent group");
        }
        usize requiredQueueEntries = 0;
        bool selectionChanged = false;
        for (u32 childIndex = parent->firstChildIndex; childIndex != InvalidNodeIndex;)
        {
            const NodeRecord* child = recordByIndex(childIndex);
            if (child == nullptr)
            {
                return fail(Core::CoreErrorCode::Internal, "UI RadioButton group is invalid");
            }
            const u32 nextSiblingIndex = child->nextSiblingIndex;
            if (child->kind == BuiltinElementKind::RadioButton && childIndex < radioButtonStatesByNodeIndex.size())
            {
                const bool nextSelected = childIndex == radioButton.index();
                if (radioButtonStatesByNodeIndex[childIndex].selected != nextSelected)
                {
                    selectionChanged = true;
                    if (!dirtyQueueStorage.isQueued(childIndex) && !dirtyQueueStorage.isReserved(childIndex))
                    {
                        ++requiredQueueEntries;
                    }
                }
            }
            childIndex = nextSiblingIndex;
        }
        if (!selectionChanged)
        {
            return Core::success();
        }
        usize occupiedQueueEntries = occupiedDirtyQueueSlotCount();
        if (occupiedQueueEntries > dirtyQueueStorage.queueCapacity() ||
            requiredQueueEntries > dirtyQueueStorage.queueCapacity() - occupiedQueueEntries)
        {
            compactDirtyQueue();
            requiredQueueEntries = 0;
            for (u32 childIndex = parent->firstChildIndex; childIndex != InvalidNodeIndex;)
            {
                const NodeRecord* child = recordByIndex(childIndex);
                if (child == nullptr)
                {
                    return fail(Core::CoreErrorCode::Internal, "UI RadioButton group is invalid");
                }
                const u32 nextSiblingIndex = child->nextSiblingIndex;
                if (child->kind == BuiltinElementKind::RadioButton && childIndex < radioButtonStatesByNodeIndex.size() &&
                    radioButtonStatesByNodeIndex[childIndex].selected != (childIndex == radioButton.index()) &&
                    !dirtyQueueStorage.isQueued(childIndex) && !dirtyQueueStorage.isReserved(childIndex))
                {
                    ++requiredQueueEntries;
                }
                childIndex = nextSiblingIndex;
            }
            occupiedQueueEntries = occupiedDirtyQueueSlotCount();
        }
        if (occupiedQueueEntries > dirtyQueueStorage.queueCapacity() ||
            requiredQueueEntries > dirtyQueueStorage.queueCapacity() - occupiedQueueEntries)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI RadioButton group selection exceeds dirty queue capacity");
        }
        for (u32 childIndex = parent->firstChildIndex; childIndex != InvalidNodeIndex;)
        {
            const NodeRecord* child = recordByIndex(childIndex);
            if (child == nullptr)
            {
                return fail(Core::CoreErrorCode::Internal, "UI RadioButton group is invalid");
            }
            const u32 nextSiblingIndex = child->nextSiblingIndex;
            if (child->kind == BuiltinElementKind::RadioButton && childIndex < radioButtonStatesByNodeIndex.size())
            {
                RadioButtonState& state = radioButtonStatesByNodeIndex[childIndex];
                const bool nextSelected = childIndex == radioButton.index();
                if (state.selected != nextSelected)
                {
                    if (!dirtyQueueStorage.isQueued(childIndex))
                    {
                        dirtyQueueStorage.enqueue(idForIndex(childIndex));
                    }
                    dirtyQueueStorage.flags(childIndex) |= UIDirty::Paint | UIDirty::Semantics;
                    state.selected = nextSelected;
                }
            }
            childIndex = nextSiblingIndex;
        }
        phaseDirty |= PhasePaint | PhaseSemantics;
        return Core::success();
    }

    [[nodiscard]] Core::Status setRadioButtonPaintFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                              const UIRadioButtonPaint& paint)
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
        auto radioResult = resolveRadioButton(radioButton);
        if (!radioResult)
        {
            return Core::failure(radioResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, radioButton))
        {
            return fail(UIErrorCode::InvalidNode, "UI RadioButton is not owned by the updater root");
        }
        if (!std::isfinite(paint.selectedIndicatorInset) || paint.selectedIndicatorInset < 0.0F ||
            !std::isfinite(paint.labelGap) || paint.labelGap < 0.0F)
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI RadioButton paint metrics must be finite and non-negative");
        }
        RadioButtonState& state = radioButtonStatesByNodeIndex[radioButton.index()];
        if (state.paint == paint)
        {
            detachThemeBinding(radioButton.index(), ThemeBindingRadioButtonPaint);
            return Core::success();
        }
        const bool layoutChanged = state.paint.labelGap != paint.labelGap;
        Core::Status dirty = layoutChanged ? markLayoutStyleDirty(radioButton) : markPaintDirty(radioButton);
        if (!dirty)
        {
            return dirty;
        }
        state.paint = paint;
        detachThemeBinding(radioButton.index(), ThemeBindingRadioButtonPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIRadioButtonPaint> radioButtonPaintFromUpdater(UINodeId updaterRoot,
                                                                               UINodeId radioButton) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto radioResult = const_cast<Impl*>(this)->resolveRadioButton(radioButton);
        if (!radioResult)
        {
            return Core::failure(radioResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, radioButton))
        {
            return fail(UIErrorCode::InvalidNode, "UI RadioButton is not owned by the updater root");
        }
        return radioButtonStatesByNodeIndex[radioButton.index()].paint;
    }

    [[nodiscard]] Core::Status setRadioButtonActionFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                               UIButtonActionCallback&& callback)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        auto radioResult = resolveRadioButton(radioButton);
        if (!radioResult)
        {
            return Core::failure(radioResult.error());
        }
        return setButtonActionFromUpdater(updaterRoot, radioButton, std::move(callback));
    }

    [[nodiscard]] Core::Status clearRadioButtonActionFromUpdater(UINodeId updaterRoot, UINodeId radioButton)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        auto radioResult = resolveRadioButton(radioButton);
        if (!radioResult)
        {
            return Core::failure(radioResult.error());
        }
        return clearButtonActionFromUpdater(updaterRoot, radioButton);
    }

    [[nodiscard]] Core::Status setRadioButtonSelectedFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                                 bool selected)
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
        auto radioResult = resolveRadioButton(radioButton);
        if (!radioResult)
        {
            return Core::failure(radioResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, radioButton))
        {
            return fail(UIErrorCode::InvalidNode, "UI RadioButton is not owned by the updater root");
        }
        return applyRadioButtonSelection(radioButton, selected);
    }

    [[nodiscard]] Core::Result<bool> isRadioButtonSelectedFromUpdater(UINodeId updaterRoot, UINodeId radioButton) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto radioResult = const_cast<Impl*>(this)->resolveRadioButton(radioButton);
        if (!radioResult)
        {
            return Core::failure(radioResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, radioButton))
        {
            return fail(UIErrorCode::InvalidNode, "UI RadioButton is not owned by the updater root");
        }
        return radioButtonStatesByNodeIndex[radioButton.index()].selected;
    }

    [[nodiscard]] Core::Result<bool> isRadioButtonPressedFromUpdater(UINodeId updaterRoot, UINodeId radioButton)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        auto radioResult = resolveRadioButton(radioButton);
        if (!radioResult)
        {
            return Core::failure(radioResult.error());
        }
        return isButtonPressedFromUpdater(updaterRoot, radioButton);
    }

    void appendCommittedTree(u32 index, u32& ordinal, std::pmr::vector<UICommittedNodeEntry>& output) const noexcept
    {
        const u32 rootIndex = index;
        u32 currentIndex = rootIndex;
        while (currentIndex != InvalidNodeIndex)
        {
            const NodeRecord* record = recordByIndex(currentIndex);
            if (record == nullptr)
            {
                return;
            }

            const u32 currentOrdinal = ordinal++;
            output.push_back(UICommittedNodeEntry{
                .node = idForIndex(currentIndex),
                .parent = idForIndex(record->parentIndex),
                .depth = record->depth,
                .preorder = currentOrdinal,
                .paintOrdinal = currentOrdinal,
            });

            if (record->firstChildIndex != InvalidNodeIndex)
            {
                currentIndex = record->firstChildIndex;
                continue;
            }

            while (currentIndex != rootIndex)
            {
                record = recordByIndex(currentIndex);
                if (record == nullptr)
                {
                    return;
                }
                if (record->nextSiblingIndex != InvalidNodeIndex)
                {
                    currentIndex = record->nextSiblingIndex;
                    break;
                }
                currentIndex = record->parentIndex;
            }
            if (currentIndex == rootIndex)
            {
                currentIndex = InvalidNodeIndex;
            }
        }
    }

    [[nodiscard]] Core::Status commitStructure()
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

    [[nodiscard]] Core::Status validateViewport(UILogicalSize viewportSize) const
    {
        if (!isFiniteNonNegative(viewportSize.width) || !isFiniteNonNegative(viewportSize.height))
        {
            return fail(UIErrorCode::InvalidLayout, "UI layout viewport must be finite and non-negative");
        }
        return Core::success();
    }

    void publishControlLayoutState(const std::pmr::vector<u32>& order) noexcept
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

    [[nodiscard]] Core::Status commitLayout(UILogicalSize viewportSize)
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
        if (layoutNeedsCommit)
        {
            const bool allowLayoutReuse = !structureNeedsCommit && !viewportChanged && layoutReuseCacheValid;
            layoutReuseCacheValid = false;
            layoutReuseInProgress = allowLayoutReuse;
            auto layoutReuseGuard = Core::makeScopeExit([this]() noexcept { layoutReuseInProgress = false; });
            writeLayoutBufferIndex = 1 - publishedLayoutBufferIndex;
            std::pmr::vector<UICommittedLayoutEntry>& writeLayout = committedLayoutBuffers[writeLayoutBufferIndex];
            writeLayout.clear();

            buildLayoutOrder(layoutOrderScratch);
            pass.passCount = layoutOrderScratch.empty() ? 0 : 1;
            prepareLayoutState(viewportSize, layoutOrderScratch, allowLayoutReuse);
            measureLayout(viewportSize, layoutOrderScratch, pass);
            if (Core::Status arranged = arrangeLayout(viewportSize, layoutOrderScratch, pass); !arranged)
            {
                return arranged;
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
            auto paintCapacity = validatePaintCandidateCapacity(candidateLayoutEntries);
            if (!paintCapacity)
            {
                return Core::failure(paintCapacity.error());
            }
            writePaintBufferIndex = 1 - publishedPaintBufferIndex;
            if (Core::Status status =
                    buildCommittedPaint(committedPaintBuffers[writePaintBufferIndex], candidateLayoutEntries);
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
                                                              candidateLayoutEntries);
                !status)
            {
                return status;
            }
        }

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

    [[nodiscard]] UICommittedStructureView committedStructure() const noexcept
    {
        const std::pmr::vector<UICommittedNodeEntry>& entries = committedBuffers[publishedBufferIndex];
        return UICommittedStructureView{
            std::span<const UICommittedNodeEntry>(entries.data(), entries.size()),
            committedRevision,
        };
    }

    [[nodiscard]] UICommittedLayoutView committedLayout() const noexcept
    {
        const std::pmr::vector<UICommittedLayoutEntry>& entries = committedLayoutBuffers[publishedLayoutBufferIndex];
        return UICommittedLayoutView{
            std::span<const UICommittedLayoutEntry>(entries.data(), entries.size()),
            committedLayoutStructureRevision,
            committedLayoutRevision,
        };
    }

    [[nodiscard]] UICommittedHitView committedHit() const noexcept
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

    [[nodiscard]] UICommittedPaintView committedPaint() const noexcept
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

    [[nodiscard]] UICommittedSemanticsView committedSemantics() const noexcept
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

    [[nodiscard]] std::span<const u8> glyphAtlasPixels() const noexcept
    {
        if (!glyphAtlas)
        {
            return {};
        }
        return glyphAtlas->pagePixels();
    }

    [[nodiscard]] u32 glyphAtlasWidth() const noexcept
    {
        return glyphAtlas ? glyphAtlas->capacity().width : 0U;
    }

    [[nodiscard]] u32 glyphAtlasHeight() const noexcept
    {
        return glyphAtlas ? glyphAtlas->capacity().height : 0U;
    }

    [[nodiscard]] Core::Status openTextFont(std::span<const std::byte> fontBytes, i32 faceIndex)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!textRasterizer)
        {
            return fail(UIErrorCode::InvalidFont, "UI context has no text rasterizer");
        }

        auto newFace = textRasterizer->openFace(fontBytes, faceIndex);
        if (!newFace)
        {
            return Core::failure(newFace.error());
        }

        if (textFace.hasValue())
        {
            static_cast<void>(textRasterizer->closeFace(textFace));
            textFace = {};
        }
        textFace = *newFace;
        if (glyphAtlas)
        {
            glyphAtlas->clear();
        }

        // Remeasure retained text and dirty layout/paint for all text nodes.
        for (u32 index = 0; index < static_cast<u32>(textStatesByIndex.size()); ++index)
        {
            WidgetTextState& state = textStatesByIndex[index];
            if (!state.hasContent)
            {
                continue;
            }
            const UINodeId node = idForIndex(index);
            if (!node.hasValue() || !contains(node))
            {
                continue;
            }
            auto metrics = measureWidgetText(textViewFor(index), state.style);
            if (!metrics)
            {
                return Core::failure(metrics.error());
            }
            state.metrics = *metrics;
            if (Core::Status dirtyStatus = markLayoutStyleDirty(node); !dirtyStatus)
            {
                return dirtyStatus;
            }
            if (Core::Status paintStatus = markPaintDirty(node); !paintStatus)
            {
                return paintStatus;
            }
        }
        return Core::success();
    }

    [[nodiscard]] UIPointerHitQueryResult queryPointerHit(UILogicalPoint point) const noexcept
    {
        return Detail::queryCommittedPointerHit(committedHit(), point);
    }

    [[nodiscard]] Core::Result<std::pair<u32, u32>> addRoutedPointerListener(UIRoutedPointerListenerDesc descriptor,
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
    addRoutedPointerListenerFromUpdater(UINodeId updaterRoot, UIRoutedPointerListenerDesc descriptor,
                                        UIRoutedPointerCallback&& callback)
    {
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        return addRoutedPointerListener(descriptor, std::move(callback), updaterRoot);
    }

    [[nodiscard]] Core::Status validatePointerInput(const UIPointerInputEvent& input) const
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
        if (input.pointer != Platform::PrimaryPointerId || !isValidRoutedPointerEventKind(input.kind) ||
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

    void dispatchRoutedPointerListeners(UINodeId node, UIEventPhase phase, UIRoutedPointerEventKind kind,
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

    void finishRoutedPointerDispatch() noexcept
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

    void dispatchPointerCancelToCapture(std::span<const UICommittedHitEntry> entries) noexcept
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

    void dispatchPointerCancelForCurrentCapture() noexcept
    {
        const auto& entries = committedHitBuffers[publishedHitBufferIndex];
        dispatchPointerCancelToCapture(entries);
    }

    void dispatchPointerCancelForSubtree(UINodeId subtreeRoot) noexcept
    {
        if (!pointerCancelDispatchInProgress && isNodeWithinSubtree(subtreeRoot, capturedPointerNode))
        {
            dispatchPointerCancelForCurrentCapture();
        }
    }

    [[nodiscard]] Core::Result<UIPointerRouteResult> routePointerInput(const UIPointerInputEvent& input)
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
        lastPointerInput = input;
        hasLastPointerInput = true;

        UIPointerRouteResult result{
            .pointQuery = queryPointerHit(input.position),
        };
        const UICommittedHitView hit = committedHit();
        const std::span<const UICommittedHitEntry> entries = hit.entries();
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
            result.routedTarget = result.pointQuery.target;
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
        const bool nearestTreeDisclosureAtRouteStart =
            nearestTreeView.hasValue() && pointWithinCommittedTreeDisclosure(nearestButton, input.position, entries);

        const UINodeId targetNode = result.routedTarget.node;
        const bool targetNodeEnabledAtRouteStart = isNodeEnabled(targetNode);
        const bool primaryButtonDown =
            input.kind == UIRoutedPointerEventKind::ButtonDown && input.button == Platform::PointerButton::Primary;
        const bool primaryButtonUp =
            input.kind == UIRoutedPointerEventKind::ButtonUp && input.button == Platform::PointerButton::Primary;
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
        const UINodeId physicalTarget = result.pointQuery.target.node;
        const bool pointTargetsActivePopup = isNodeWithinSubtree(popupAtRouteStart, physicalTarget);
        const bool pointTargetsPopupDropdown =
            isNodeWithinSubtree(popupDropdownAtRouteStart, physicalTarget) && !pointTargetsActivePopup;
        const bool dismissActivePopupOnPrimaryDown = primaryButtonDown && popupAtRouteStart.hasValue() &&
                                                     !pointInsideActivePopup && !pointTargetsPopupDropdown;
        const bool blockPopupChromeClickThrough = primaryButtonDown && popupAtRouteStart.hasValue() &&
                                                  pointInsideActivePopup && !pointTargetsActivePopup;
        const bool popupBarrierAtRouteStart = popupDismissPointerBarrierActive;
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
        const UINodeId hoverCandidate = physicalNearestButton.hasValue() ? physicalNearestButton : physicalTarget;
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
            } else if (nearestSliderRecord != nullptr && nearestSliderRecord->kind == BuiltinElementKind::Slider &&
                       isInteractiveRangeInput(nearestSlider))
            {
                addRouteDirtyReservationCandidate(nearestSlider);
            } else if (nearestButton.hasValue() && isNodeEnabled(nearestButton))
            {
                addRouteDirtyReservationCandidate(nearestButton);
                addRouteDirtyReservationCandidate(nearestListView);
                addRouteDirtyReservationCandidate(nearestTreeView);
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
                    addRouteDirtyReservationCandidate(listViewForItem(armedButtonAtRouteStart));
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
                    if (scrollWheelWouldChange(routeNode, input.delta))
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
            primaryButtonUp && hadArmedInteraction && pointWithinArmedButton && isNodeEnabled(armedButtonAtRouteStart)
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

        bool preserveFocusForPopupBarrier = false;
        if (primaryButtonDown && (dismissActivePopupOnPrimaryDown || blockPopupChromeClickThrough))
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
            popupDismissPointerBarrierActive = true;
            routedEvent.preventDefaultAction();
            routedEvent.consumeInputTransition();
            static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
            preserveFocusForPopupBarrier = true;
        } else if (primaryButtonUp && popupBarrierAtRouteStart)
        {
            popupDismissPointerBarrierActive = false;
            routedEvent.preventDefaultAction();
            routedEvent.consumeInputTransition();
            static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
            preserveFocusForPopupBarrier = true;
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
                                       nearestSliderRecord->kind == BuiltinElementKind::Slider &&
                                       isInteractiveRangeInput(nearestSlider);
            UINodeId nextKeyboardFocus =
                preserveFocusForModalBarrier || preserveFocusForPopupBarrier ? previousKeyboardFocus : UINodeId{};
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
            if (!preserveFocusForModalBarrier && !preserveFocusForPopupBarrier)
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
            } else if (preserveFocusForPopupBarrier)
            {
                // Popup chrome and outside-dismiss clicks are click-through
                // barriers and preserve the focus restored by Popup close.
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
                routedEvent.consumeInputTransition();
                static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
                if (auto applied = applySliderValueFromPointer(nearestSlider, input.position, input.platformFrame,
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
                if (auto applied = applySliderValueFromPointer(armedSliderAtRouteStart, input.position,
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
                if (auto applied = applySliderValueFromPointer(armedSliderAtRouteStart, input.position,
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
                    armedRecord != nullptr && armedRecord->kind == BuiltinElementKind::ListViewItem)
                {
                    if (Core::Status selected = selectCommittedListViewItem(armedButtonAtRouteStart); !selected)
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
        return result;
    }

    [[nodiscard]] Core::Status cancelPointerInteraction(Platform::WindowId routedWindow)
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
        const UINodeId cancelledButton = armedPrimaryButton;
        const UINodeId cancelledSlider = armedSlider;
        const UINodeId cancelledScrollView = armedScrollView;
        const UINodeId cancelledTextEdit = armedTextEdit;
        const UINodeId cancelledFocus = defaultActionFocusButton;
        const UINodeId cancelledHover = hoveredPrimaryControl;
        clearArmedPrimaryButton();
        clearArmedSlider();
        clearArmedScrollView();
        clearArmedTextEdit();
        capturedPointerNode = {};
        popupDismissPointerBarrierActive = false;
        dropdownCommandPressLatch.clear();
        listViewCommandPressLatch.clear();
        treeViewCommandPressLatch.clear();
        focusNavigationPressLatch.clear();
        rangeInputPressLatch.clear();
        defaultActionPressState.clearAll();
        clearImeFocus();
        clearDefaultActionFocus();
        clearHoveredPrimaryControl();
        // Cancellation is a state barrier: a full dirty queue must not leave
        // any pointer interaction armed. Existing dirty work will rebuild the
        // paint/semantics snapshot; otherwise this best-effort mark schedules
        // the cleared control state for the next commit.
        static_cast<void>(markPaintDirtyBatch({
            cancelledButton,
            cancelledSlider,
            cancelledScrollView,
            cancelledTextEdit,
            cancelledFocus,
            cancelledHover,
        }));
        return Core::success();
    }

    [[nodiscard]] Core::Status cancelDefaultActionInteraction(Platform::WindowId routedWindow,
                                                              std::optional<Platform::GamepadId> gamepad)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!routedWindow.hasValue())
        {
            return fail(UIErrorCode::InvalidPointerInput, "UI default-action cancellation requires a Window");
        }
        if (routedWindow != ownerWindow)
        {
            return fail(UIErrorCode::WrongOwnerWindow, "UI default-action cancellation belongs to another Window");
        }

        dropdownCommandPressLatch.clear();
        listViewCommandPressLatch.clear();
        treeViewCommandPressLatch.clear();
        focusNavigationPressLatch.clear();

        if (gamepad.has_value())
        {
            rangeInputPressLatch.clearGamepad(*gamepad);
            const UINodeId cancelledTarget = defaultActionPressState.pressedTarget(*gamepad);
            defaultActionPressState.clearGamepad(*gamepad);
            if (cancelledTarget.hasValue() && contains(cancelledTarget) && !isButtonPressed(cancelledTarget))
            {
                static_cast<void>(markPaintDirty(cancelledTarget));
            }
            return Core::success();
        }

        const UINodeId cancelledTarget = defaultActionFocusButton;
        rangeInputPressLatch.clear();
        defaultActionPressState.clearAll();
        if (cancelledTarget.hasValue() && contains(cancelledTarget))
        {
            static_cast<void>(markPaintDirty(cancelledTarget));
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIDefaultActionResult>
    routeDefaultActionActivate(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                               UIButtonActivationSource source,
                               std::optional<Platform::DigitalControlIdentity> control,
                               UINodeId explicitAccessibilityTarget = {})
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI default-action activation cannot nest during routing");
        }
        drainDeferredRootDestroys();
        if (!platformFrame.hasValue() || sourceSequence == 0)
        {
            return fail(UIErrorCode::InvalidPointerInput,
                        "UI default-action activation requires a platform frame and sequence");
        }
        if (source != UIButtonActivationSource::Keyboard && source != UIButtonActivationSource::Gamepad &&
            source != UIButtonActivationSource::Accessibility)
        {
            return fail(UIErrorCode::InvalidButtonAction,
                        "UI default-action activation source must be Keyboard, Gamepad, or Accessibility");
        }
        if (explicitAccessibilityTarget.hasValue() &&
            (source != UIButtonActivationSource::Accessibility || control.has_value()))
        {
            return fail(UIErrorCode::InvalidButtonAction,
                        "UI explicit activation targets are reserved for accessibility actions");
        }
        if (control.has_value())
        {
            if (Core::Status validControl = defaultActionPressState.validateControl(source, *control);
                !validControl)
            {
                return Core::failure(validControl.error());
            }
            const UINodeId existingTarget = defaultActionPressState.pressedTarget(*control);
            if (existingTarget.hasValue())
            {
                const NodeRecord* existingRecord =
                    contains(existingTarget) ? nodes.tryGet(existingTarget.storageId()) : nullptr;
                if ((existingTarget == defaultActionFocusButton &&
                     isCommittedKeyboardFocusCandidate(existingTarget)) ||
                    (existingRecord != nullptr && existingRecord->kind == BuiltinElementKind::DropdownItem))
                {
                    // Native key repeat and duplicate gamepad Down remain owned
                    // by UI without re-running toggle/callback side effects.
                    return UIDefaultActionResult{
                        .consumed = true,
                        .activated = false,
                    };
                }
                defaultActionPressState.clearPressedTarget(*control);
            }
        }
        UINodeId activationTarget = explicitAccessibilityTarget;
        if (!activationTarget.hasValue() &&
            !isCommittedKeyboardFocusCandidate(defaultActionFocusButton))
        {
            if (textInputFocus == defaultActionFocusButton)
            {
                clearImeFocus();
            } else
            {
                clearDefaultActionFocus();
            }
            return UIDefaultActionResult{};
        }
        if (!activationTarget.hasValue())
        {
            activationTarget = defaultActionFocusButton;
        }
        const NodeRecord* record = contains(activationTarget)
                                       ? nodes.tryGet(activationTarget.storageId())
                                       : nullptr;
        if (!explicitAccessibilityTarget.hasValue() && record != nullptr &&
            record->kind == BuiltinElementKind::TextEdit)
        {
            if (textInputFocus != activationTarget)
            {
                clearDefaultActionFocus();
                return UIDefaultActionResult{};
            }
            return UIDefaultActionResult{.consumed = true, .activated = false};
        }
        if (record == nullptr || !isNodeEnabled(activationTarget) ||
            !behaviorStateStorage.hasActivate(activationTarget.index()))
        {
            if (explicitAccessibilityTarget.hasValue())
            {
                return fail(UIErrorCode::InvalidButtonAction,
                            "UI accessibility activation target is not Activate-capable");
            }
            clearDefaultActionFocus();
            return UIDefaultActionResult{};
        }

        if (buttonRouteSerial == (std::numeric_limits<u64>::max)())
        {
            return fail(UIErrorCode::CapacityExceeded, "UI Button route serial is exhausted");
        }
        const bool pressedStateChanges = control.has_value() && !isButtonPressed(activationTarget);
        releaseRouteDirtyQueueReservations();
        auto reservationCleanup = Core::makeScopeExit([this]() noexcept { releaseRouteDirtyQueueReservations(); });
        if (pressedStateChanges || behaviorStateStorage.hasToggle(activationTarget.index()))
        {
            addRouteDirtyReservationCandidate(activationTarget);
        }
        if (record->kind == BuiltinElementKind::RadioButton)
        {
            const NodeRecord* parent = recordByIndex(record->parentIndex);
            if (parent == nullptr)
            {
                return fail(UIErrorCode::InvalidParent, "UI RadioButton requires a live parent group");
            }
            for (u32 childIndex = parent->firstChildIndex; childIndex != InvalidNodeIndex;)
            {
                const NodeRecord* child = recordByIndex(childIndex);
                if (child == nullptr)
                {
                    return fail(Core::CoreErrorCode::Internal, "UI RadioButton group is invalid");
                }
                const u32 nextSiblingIndex = child->nextSiblingIndex;
                if (child->kind == BuiltinElementKind::RadioButton && childIndex < radioButtonStatesByNodeIndex.size() &&
                    radioButtonStatesByNodeIndex[childIndex].selected != (childIndex == activationTarget.index()))
                {
                    addRouteDirtyReservationCandidate(idForIndex(childIndex));
                }
                childIndex = nextSiblingIndex;
            }
        }
        addDropdownActivationDirtyReservationCandidates(activationTarget);
        if (Core::Status reservation = reserveRouteDirtyQueueSlots(); !reservation)
        {
            return Core::failure(reservation.error());
        }
        if (Core::Status dirty = preflightDefaultActionActivationDirty(activationTarget, pressedStateChanges); !dirty)
        {
            return Core::failure(dirty.error());
        }
        if (pressedStateChanges)
        {
            if (Core::Status dirty = markPaintDirty(activationTarget); !dirty)
            {
                return Core::failure(dirty.error());
            }
        }
        if (u8* toggleValue = behaviorStateStorage.tryToggleValue(activationTarget.index());
            toggleValue != nullptr)
        {
            if (Core::Status dirty = markPaintDirty(activationTarget); !dirty)
            {
                return Core::failure(dirty.error());
            }
            *toggleValue = *toggleValue == 0 ? 1 : 0;
        }
        if (record->kind == BuiltinElementKind::RadioButton)
        {
            if (Core::Status selected = applyRadioButtonSelection(activationTarget, true); !selected)
            {
                return Core::failure(selected.error());
            }
        }
        bool dropdownActivated = false;
        if (record->kind == BuiltinElementKind::Dropdown || record->kind == BuiltinElementKind::DropdownItem)
        {
            auto activated = activateDropdownControl(activationTarget);
            if (!activated)
            {
                return Core::failure(activated.error());
            }
            dropdownActivated = *activated;
        }
        if (control.has_value())
        {
            defaultActionPressState.setPressedTarget(*control, activationTarget);
        }
        const u64 actionRegistrationSerialBoundary = buttonActionRegistry.registrationSerial();
        const Detail::UIButtonActionInvocation actionCandidate =
            captureButtonAction(activationTarget, actionRegistrationSerialBoundary);
        if (!actionCandidate.hasValue())
        {
            // Focused control without a registered action still consumes Accept
            // so gameplay does not also fire. Selection controls already
            // changed state above; a bare Button without a callback did not.
            const bool activated =
                behaviorStateStorage.hasToggle(activationTarget.index()) ||
                record->kind == BuiltinElementKind::RadioButton || dropdownActivated;
            return UIDefaultActionResult{.consumed = true, .activated = activated};
        }
        const u64 currentButtonRouteSerial = ++buttonRouteSerial;
        ++routeDispatchDepth;
        auto dispatchCleanup = Core::makeScopeExit([this]() noexcept { finishRoutedPointerDispatch(); });
        invokeButtonAction(actionCandidate,
                           UIButtonActionEvent{
                               .buttonNode = activationTarget,
                               .source = source,
                               .platformFrame = platformFrame,
                               .sourceSequence = sourceSequence,
                           },
                           currentButtonRouteSerial);
        return UIDefaultActionResult{.consumed = true, .activated = true};
    }

    [[nodiscard]] Core::Status performAccessibilityAction(const UIAccessibilityAction& action)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI accessibility action cannot nest during routing");
        }
        drainDeferredRootDestroys();
        auto nodeResult = resolveNode(action.node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        NodeRecord* record = *nodeResult;
        if (!isNodeEnabled(action.node))
        {
            return fail(UIErrorCode::InvalidAccessibilityAction,
                        "UI accessibility action target is disabled");
        }

        UISemanticsAction requiredAction = UISemanticsAction::None;
        switch (action.kind)
        {
        case UIAccessibilityActionKind::Focus:
            requiredAction = UISemanticsAction::Focus;
            break;
        case UIAccessibilityActionKind::Invoke:
            requiredAction = UISemanticsAction::Activate;
            break;
        case UIAccessibilityActionKind::Toggle:
            requiredAction = UISemanticsAction::Toggle;
            break;
        case UIAccessibilityActionKind::SetRangeValue:
            requiredAction = UISemanticsAction::SetRangeValue;
            break;
        case UIAccessibilityActionKind::SetTextValue:
            requiredAction = UISemanticsAction::SetTextValue;
            break;
        default:
            return fail(UIErrorCode::InvalidAccessibilityAction,
                        "UI accessibility action kind is not recognized");
        }
        const auto& committedSemantics = committedSemanticsBuffers[publishedSemanticsBufferIndex];
        const auto semanticsEntry =
            std::find_if(committedSemantics.begin(), committedSemantics.end(), [&](const UISemanticsEntry& entry) {
                return entry.node == action.node;
            });
        if (semanticsEntry == committedSemantics.end() ||
            !hasSemanticsAction(semanticsEntry->actions, requiredAction))
        {
            return fail(UIErrorCode::InvalidAccessibilityAction,
                        "UI accessibility action is not published for the target semantics node");
        }

        switch (action.kind)
        {
        case UIAccessibilityActionKind::Focus:
            return requestFocus(action.node);
        case UIAccessibilityActionKind::Invoke:
            if (!behaviorStateStorage.hasActivate(action.node.index()))
            {
                return fail(UIErrorCode::InvalidAccessibilityAction,
                            "UI accessibility Invoke requires Activate behavior");
            }
            break;
        case UIAccessibilityActionKind::Toggle:
            if (!behaviorStateStorage.hasToggle(action.node.index()) &&
                !hasBehavior(record->behaviors, UIElementBehavior::ExclusiveChoice))
            {
                return fail(UIErrorCode::InvalidAccessibilityAction,
                            "UI accessibility Toggle requires toggle behavior");
            }
            break;
        case UIAccessibilityActionKind::SetRangeValue: {
            const Detail::UIRangeInputState* range = behaviorStateStorage.tryRangeInputState(action.node.index());
            if (!hasBehavior(record->behaviors, UIElementBehavior::RangeInput) ||
                range == nullptr ||
                action.node.index() >= semanticsStatesByNodeIndex.size() ||
                semanticsStatesByNodeIndex[action.node.index()].readOnly || !std::isfinite(action.rangeValue) ||
                action.rangeValue < static_cast<double>(range->minValue) ||
                action.rangeValue > static_cast<double>(range->maxValue) ||
                action.rangeValue < static_cast<double>((std::numeric_limits<float>::lowest)()) ||
                action.rangeValue > static_cast<double>((std::numeric_limits<float>::max)()))
            {
                return fail(UIErrorCode::InvalidAccessibilityAction,
                            "UI accessibility range value is invalid or read-only for the target RangeInput");
            }
            auto applied = applySliderValue(action.node, action.rangeValue, {}, 0, true);
            if (!applied)
            {
                return Core::failure(applied.error());
            }
            return Core::success();
        }
        case UIAccessibilityActionKind::SetTextValue: {
            if (!hasBehavior(record->behaviors, UIElementBehavior::TextInput) ||
                record->kind != BuiltinElementKind::TextEdit)
            {
                return fail(UIErrorCode::InvalidAccessibilityAction,
                            "UI accessibility text value requires a TextEdit");
            }
            const UINodeId root = idForIndex(record->rootIndex);
            return setTextFromUpdater(root, action.node, action.textValue);
        }
        }

        if (accessibilityActionSequence == (std::numeric_limits<u64>::max)())
        {
            return fail(UIErrorCode::CapacityExceeded, "UI accessibility action sequence is exhausted");
        }
        if (hasBehavior(record->behaviors, UIElementBehavior::Focusable))
        {
            if (Core::Status focus = requestFocus(action.node); !focus)
            {
                return focus;
            }
        }
        const u64 actionSequence = ++accessibilityActionSequence;
        if (action.kind == UIAccessibilityActionKind::Toggle &&
            behaviorStateStorage.hasToggle(action.node.index()) &&
            !behaviorStateStorage.hasActivate(action.node.index()))
        {
            u8* toggleValue = behaviorStateStorage.tryToggleValue(action.node.index());
            if (toggleValue == nullptr)
            {
                return fail(Core::CoreErrorCode::Internal,
                            "UI accessibility Toggle state is unavailable");
            }
            if (Core::Status dirty = markPaintDirty(action.node); !dirty)
            {
                return dirty;
            }
            *toggleValue = *toggleValue == 0 ? 1 : 0;
            return Core::success();
        }
        auto activated = routeDefaultActionActivate(
            Platform::PlatformFrameId{1}, actionSequence,
            UIButtonActivationSource::Accessibility, std::nullopt, action.node);
        if (!activated)
        {
            return Core::failure(activated.error());
        }
        if (!activated->consumed)
        {
            return fail(UIErrorCode::InvalidAccessibilityAction,
                        "UI accessibility action target could not be activated");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIDefaultActionResult>
    routeDefaultActionRelease(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                              UIButtonActivationSource source, const Platform::DigitalControlIdentity& control)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!platformFrame.hasValue() || sourceSequence == 0)
        {
            return fail(UIErrorCode::InvalidPointerInput,
                        "UI default-action release requires a platform frame and sequence");
        }
        if (Core::Status validControl = defaultActionPressState.validateControl(source, control);
            !validControl)
        {
            return Core::failure(validControl.error());
        }

        const UINodeId releasedTarget = defaultActionPressState.pressedTarget(control);
        if (!releasedTarget.hasValue())
        {
            return UIDefaultActionResult{};
        }

        defaultActionPressState.clearPressedTarget(control);
        if (contains(releasedTarget) && !isButtonPressed(releasedTarget))
        {
            // Physical Up cannot be replayed. Keep release as a successful
            // input barrier even when repaint capacity is temporarily full.
            static_cast<void>(markPaintDirty(releasedTarget));
        }
        return UIDefaultActionResult{
            .consumed = true,
            .activated = false,
        };
    }

    [[nodiscard]] Core::Status applyExplicitFocus(UINodeId nextFocus)
    {
        const UINodeId previousFocus = defaultActionFocusButton;
        const UINodeId previousTextFocus = textInputFocus;
        if (previousFocus == nextFocus &&
            ((nextFocus.hasValue() && isLiveTextEdit(nextFocus)) ? previousTextFocus == nextFocus
                                                                 : !previousTextFocus.hasValue()))
        {
            return Core::success();
        }

        if (Core::Status dirty = preflightPaintDirtyBatch({
                previousFocus,
                previousTextFocus,
                nextFocus,
            });
            !dirty)
        {
            return dirty;
        }
        if (Core::Status dirty = markPaintDirtyBatch({
                previousFocus,
                previousTextFocus,
                nextFocus,
            });
            !dirty)
        {
            return dirty;
        }

        defaultActionPressState.clearAll();
        if (previousTextFocus != nextFocus)
        {
            resetImeCompositionState();
        }
        defaultActionFocusButton = nextFocus;
        const NodeRecord* nextRecord =
            nextFocus.hasValue() && contains(nextFocus) ? nodes.tryGet(nextFocus.storageId()) : nullptr;
        textInputFocus = nextRecord != nullptr && nextRecord->kind == BuiltinElementKind::TextEdit ? nextFocus : UINodeId{};
        return Core::success();
    }

    [[nodiscard]] Core::Status requestFocus(UINodeId node)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI focus cannot be changed during pointer routing");
        }
        drainDeferredRootDestroys();
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isCommittedKeyboardFocusCandidate(node))
        {
            return fail(UIErrorCode::InvalidFocusTarget,
                        "UI focus target is not an enabled committed keyboard-focus candidate");
        }
        return applyExplicitFocus(node);
    }

    [[nodiscard]] Core::Status clearFocus()
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI focus cannot be cleared during pointer routing");
        }
        drainDeferredRootDestroys();
        return applyExplicitFocus({});
    }

    [[nodiscard]] Core::Status requestFocusFromUpdater(UINodeId updaterRoot, UINodeId node)
    {
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI focus target is not owned by the updater root");
        }
        return requestFocus(node);
    }

    [[nodiscard]] Core::Status clearFocusFromUpdater(UINodeId updaterRoot)
    {
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (defaultActionFocusButton.hasValue() && !isNodeWithinRoot(updaterRoot, defaultActionFocusButton))
        {
            return Core::success();
        }
        return clearFocus();
    }

    [[nodiscard]] UINodeId activeFocusScope() const noexcept
    {
        const auto& entries = committedHitBuffers[publishedHitBufferIndex];
        const u32 focusEntryIndex = findHitEntryIndex(defaultActionFocus(), entries);
        if (focusEntryIndex < entries.size())
        {
            const u32 scopeEntryIndex = entries[focusEntryIndex].focusScopeEntryIndex;
            if (scopeEntryIndex < entries.size())
            {
                return entries[scopeEntryIndex].node;
            }
        }
        return activeModalNode;
    }

    [[nodiscard]] UINodeId activeModal() const noexcept
    {
        return activeModalNode;
    }

    [[nodiscard]] UINodeId pointerCapture() const noexcept
    {
        const auto& entries = committedHitBuffers[publishedHitBufferIndex];
        return isPointerCaptureCandidate(capturedPointerNode, entries, committedActiveModalEntryIndex)
                   ? capturedPointerNode
                   : UINodeId{};
    }

    [[nodiscard]] UINodeId activePopup() const noexcept
    {
        if (!activePopupNode.hasValue() || !contains(activePopupNode) ||
            activePopupNode.index() >= popupStatesByNodeIndex.size() ||
            !popupStatesByNodeIndex[activePopupNode.index()].open)
        {
            return {};
        }
        const NodeRecord* record = nodes.tryGet(activePopupNode.storageId());
        return record != nullptr && record->kind == BuiltinElementKind::Popup ? activePopupNode : UINodeId{};
    }

    [[nodiscard]] Core::Result<UIDropdownCommandResult> routeDropdownCommand(UIDropdownCommand command,
                                                                             bool pressed)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI Dropdown command cannot run during pointer routing");
        }
        drainDeferredRootDestroys();

        if (!dropdownCommandPressLatch.accepts(command))
        {
            return fail(UIErrorCode::InvalidControlValue, "UI Dropdown command is not recognized");
        }

        if (!pressed)
        {
            return UIDropdownCommandResult{
                .consumed = dropdownCommandPressLatch.release(command),
                .changed = false,
                .focus = defaultActionFocus(),
            };
        }
        if (dropdownCommandPressLatch.isLatched(command))
        {
            return UIDropdownCommandResult{
                .consumed = true,
                .changed = false,
                .focus = defaultActionFocus(),
            };
        }

        const UINodeId popup = activePopup();
        const UINodeId dropdown = dropdownForPopup(popup);
        if (!popup.hasValue() || !dropdown.hasValue())
        {
            return UIDropdownCommandResult{};
        }

        if (command == UIDropdownCommand::Dismiss)
        {
            if (Core::Status closed = setPopupOpenState(popup, false); !closed)
            {
                return Core::failure(closed.error());
            }
            dropdownCommandPressLatch.latch(command);
            return UIDropdownCommandResult{
                .consumed = true,
                .changed = true,
                .focus = defaultActionFocus(),
            };
        }

        const auto& entries = committedHitBuffers[publishedHitBufferIndex];
        if (command == UIDropdownCommand::PreviousItem || command == UIDropdownCommand::NextItem)
        {
            const Detail::UISelectBehaviorState* select = behaviorStateStorage.trySelectState(dropdown.index());
            if (select == nullptr)
            {
                return fail(Core::CoreErrorCode::Internal, "UI Dropdown is missing Select behavior state");
            }
            const bool next = command == UIDropdownCommand::NextItem;
            u32 firstCandidate = InvalidUIHitEntryIndex;
            u32 lastCandidate = InvalidUIHitEntryIndex;
            u32 previousCandidate = InvalidUIHitEntryIndex;
            u32 nextCandidate = InvalidUIHitEntryIndex;
            u32 selectedCandidate = InvalidUIHitEntryIndex;
            bool currentFound = false;
            for (u32 entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
            {
                const UICommittedHitEntry& entry = entries[entryIndex];
                if (!contains(entry.node) || !hasBehavior(entry.behaviors, UIElementBehavior::SelectOption) ||
                    dropdownForItem(entry.node) != dropdown || !isNodeEnabled(entry.node) ||
                    entry.policy != UIPointerHitPolicy::Targetable ||
                    !hitEntryAllowedByModal(entry, committedActiveModalEntryIndex))
                {
                    continue;
                }
                if (firstCandidate == InvalidUIHitEntryIndex)
                {
                    firstCandidate = entryIndex;
                }
                lastCandidate = entryIndex;
                if (entry.node == select->selectedOption)
                {
                    selectedCandidate = entryIndex;
                }
                if (currentFound && nextCandidate == InvalidUIHitEntryIndex)
                {
                    nextCandidate = entryIndex;
                }
                if (entry.node == defaultActionFocusButton)
                {
                    currentFound = true;
                } else if (!currentFound)
                {
                    previousCandidate = entryIndex;
                }
            }

            u32 targetEntryIndex = InvalidUIHitEntryIndex;
            if (!currentFound)
            {
                targetEntryIndex = selectedCandidate != InvalidUIHitEntryIndex
                                       ? selectedCandidate
                                       : (next ? firstCandidate : lastCandidate);
            } else if (next)
            {
                targetEntryIndex = nextCandidate != InvalidUIHitEntryIndex
                                       ? nextCandidate
                                       : findHitEntryIndex(defaultActionFocusButton, entries);
            } else
            {
                targetEntryIndex = previousCandidate != InvalidUIHitEntryIndex
                                       ? previousCandidate
                                       : findHitEntryIndex(defaultActionFocusButton, entries);
            }

            bool changed = false;
            if (targetEntryIndex < entries.size())
            {
                const UINodeId nextFocus = entries[targetEntryIndex].node;
                changed = nextFocus != defaultActionFocusButton;
                if (changed)
                {
                    if (Core::Status focused = applyExplicitFocus(nextFocus); !focused)
                    {
                        return Core::failure(focused.error());
                    }
                }
            }
            dropdownCommandPressLatch.latch(command);
            return UIDropdownCommandResult{
                .consumed = true,
                .changed = changed,
                .focus = defaultActionFocus(),
            };
        }

        const u32 dropdownEntryIndex = findHitEntryIndex(dropdown, entries);
        u32 firstCandidate = InvalidUIHitEntryIndex;
        u32 lastCandidate = InvalidUIHitEntryIndex;
        u32 previousCandidate = InvalidUIHitEntryIndex;
        u32 nextCandidate = InvalidUIHitEntryIndex;
        if (dropdownEntryIndex < entries.size())
        {
            const u32 scopeEntryIndex = entries[dropdownEntryIndex].focusScopeEntryIndex;
            for (u32 entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
            {
                const UICommittedHitEntry& entry = entries[entryIndex];
                if (!contains(entry.node) || entry.node == dropdown || isNodeWithinSubtree(popup, entry.node) ||
                    !isNodeEnabled(entry.node) || entry.policy != UIPointerHitPolicy::Targetable ||
                    !hasBehavior(entry.behaviors, UIElementBehavior::Focusable) ||
                    !hitEntryAllowedByModal(entry, committedActiveModalEntryIndex) ||
                    !hitEntryIsWithinScope(entryIndex, scopeEntryIndex, entries))
                {
                    continue;
                }
                if (firstCandidate == InvalidUIHitEntryIndex)
                {
                    firstCandidate = entryIndex;
                }
                lastCandidate = entryIndex;
                if (entryIndex < dropdownEntryIndex)
                {
                    previousCandidate = entryIndex;
                } else if (entryIndex > dropdownEntryIndex && nextCandidate == InvalidUIHitEntryIndex)
                {
                    nextCandidate = entryIndex;
                }
            }
        }

        const bool exitNext = command == UIDropdownCommand::ExitNext;
        const u32 exitEntryIndex = exitNext
                                       ? (nextCandidate != InvalidUIHitEntryIndex ? nextCandidate : firstCandidate)
                                       : (previousCandidate != InvalidUIHitEntryIndex ? previousCandidate : lastCandidate);
        const UINodeId exitFocus = exitEntryIndex < entries.size() ? entries[exitEntryIndex].node : dropdown;
        const UINodeId previousFocus = defaultActionFocusButton;
        releaseRouteDirtyQueueReservations();
        auto reservationCleanup = Core::makeScopeExit([this]() noexcept { releaseRouteDirtyQueueReservations(); });
        addRouteLayoutDirtyReservationCandidates(popup);
        addRouteLayoutDirtyReservationCandidates(dropdown);
        addRouteDirtyReservationCandidate(previousFocus);
        addRouteDirtyReservationCandidate(textInputFocus);
        addRouteDirtyReservationCandidate(exitFocus);
        if (Core::Status reservation = reserveRouteDirtyQueueSlots(); !reservation)
        {
            return Core::failure(reservation.error());
        }
        if (Core::Status closed = setPopupOpenState(popup, false); !closed)
        {
            return Core::failure(closed.error());
        }
        if (defaultActionFocusButton != exitFocus)
        {
            if (Core::Status focused = applyExplicitFocus(exitFocus); !focused)
            {
                return Core::failure(focused.error());
            }
        }
        dropdownCommandPressLatch.latch(command);
        return UIDropdownCommandResult{
            .consumed = true,
            .changed = true,
            .focus = defaultActionFocus(),
        };
    }

    [[nodiscard]] Core::Result<UIListViewCommandResult> routeListViewCommand(UIListViewCommand command,
                                                                             bool pressed)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI ListView command cannot run during pointer routing");
        }
        drainDeferredRootDestroys();

        if (!listViewCommandPressLatch.accepts(command))
        {
            return fail(UIErrorCode::InvalidControlValue, "UI ListView command is not recognized");
        }
        if (!pressed)
        {
            return UIListViewCommandResult{
                .consumed = listViewCommandPressLatch.release(command)};
        }
        if (listViewCommandPressLatch.isLatched(command))
        {
            return UIListViewCommandResult{.consumed = true};
        }

        UINodeId listView = defaultActionFocusButton;
        const NodeRecord* focusRecord =
            contains(listView) ? nodes.tryGet(listView.storageId()) : nullptr;
        if (focusRecord != nullptr && focusRecord->kind == BuiltinElementKind::ListViewItem)
        {
            listView = listViewForItem(listView);
            focusRecord = contains(listView) ? nodes.tryGet(listView.storageId()) : nullptr;
        }
        if (focusRecord == nullptr || focusRecord->kind != BuiltinElementKind::ListView || !isNodeEnabled(listView))
        {
            return UIListViewCommandResult{};
        }

        ListViewState& state = listViewStatesByNodeIndex[listView.index()];
        const u64 itemCount = state.dataSource.hasValue() ? state.dataSource.itemCount(state.dataSource.state) : 0;
        const auto consumeWithoutChange = [&]() noexcept {
            listViewCommandPressLatch.latch(command);
            return UIListViewCommandResult{
                .consumed = true,
                .selection = state.selection,
            };
        };
        if (command == UIListViewCommand::Activate)
        {
            listViewCommandPressLatch.latch(command);
            return UIListViewCommandResult{
                .consumed = true,
                .changed = false,
                .activated = state.selection.hasValue(),
                .selection = state.selection,
            };
        }
        if (itemCount == 0)
        {
            return consumeWithoutChange();
        }

        const u64 pageItems = (std::max)(
            u64{1}, static_cast<u64>(std::floor(state.committedMetrics.viewportSize.height / state.style.rowHeight)));
        u64 candidate = 0;
        switch (command)
        {
        case UIListViewCommand::PreviousItem:
            candidate = state.selection.hasValue() && state.selection.logicalIndex != 0
                            ? state.selection.logicalIndex - 1
                            : 0;
            break;
        case UIListViewCommand::NextItem:
            candidate = state.selection.hasValue()
                            ? (std::min)(itemCount - 1, state.selection.logicalIndex + 1)
                            : 0;
            break;
        case UIListViewCommand::PreviousPage:
            candidate = state.selection.hasValue() && state.selection.logicalIndex > pageItems
                            ? state.selection.logicalIndex - pageItems
                            : 0;
            break;
        case UIListViewCommand::NextPage:
            candidate = state.selection.hasValue()
                            ? (std::min)(itemCount - 1, state.selection.logicalIndex + pageItems)
                            : 0;
            break;
        case UIListViewCommand::FirstItem:
            candidate = 0;
            break;
        case UIListViewCommand::LastItem:
            candidate = itemCount - 1;
            break;
        case UIListViewCommand::Activate:
            break;
        }

        const bool searchBackward = command == UIListViewCommand::PreviousItem ||
                                    command == UIListViewCommand::PreviousPage ||
                                    command == UIListViewCommand::LastItem;
        UIListViewItemDescriptor descriptor{};
        bool found = false;
        for (u64 visited = 0; visited < itemCount; ++visited)
        {
            auto resolved = resolveListViewLogicalItem(listView, candidate);
            if (!resolved)
            {
                return Core::failure(resolved.error());
            }
            if (resolved->enabled)
            {
                descriptor = *resolved;
                found = true;
                break;
            }
            if (searchBackward)
            {
                if (candidate == 0)
                {
                    break;
                }
                --candidate;
            } else
            {
                if (candidate + 1 >= itemCount)
                {
                    break;
                }
                ++candidate;
            }
        }
        if (!found)
        {
            return consumeWithoutChange();
        }

        const UIListViewSelection nextSelection{.key = descriptor.key, .logicalIndex = candidate};
        const bool changed = state.selection != nextSelection;
        if (changed)
        {
            if (Core::Status dirty = markPaintDirty(listView); !dirty)
            {
                return Core::failure(dirty.error());
            }
            state.selection = nextSelection;
        }
        const UINodeId updaterRoot = idForIndex(focusRecord->rootIndex);
        if (Core::Status revealed = scrollListViewToIndexFromUpdater(
                updaterRoot, listView, candidate, UIListViewScrollAlignment::Nearest);
            !revealed)
        {
            return Core::failure(revealed.error());
        }
        listViewCommandPressLatch.latch(command);
        return UIListViewCommandResult{
            .consumed = true,
            .changed = changed,
            .activated = false,
            .selection = state.selection,
        };
    }

    [[nodiscard]] Core::Result<UITreeViewCommandResult> routeTreeViewCommand(UITreeViewCommand command, bool pressed)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI TreeView command cannot run during pointer routing");
        }
        drainDeferredRootDestroys();

        if (!treeViewCommandPressLatch.accepts(command))
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TreeView command is not recognized");
        }
        if (!pressed)
        {
            return UITreeViewCommandResult{
                .consumed = treeViewCommandPressLatch.release(command)};
        }
        if (treeViewCommandPressLatch.isLatched(command))
        {
            return UITreeViewCommandResult{.consumed = true};
        }

        UINodeId treeView = defaultActionFocusButton;
        const NodeRecord* focusRecord = contains(treeView) ? nodes.tryGet(treeView.storageId()) : nullptr;
        if (focusRecord != nullptr && focusRecord->kind == BuiltinElementKind::TreeViewItem)
        {
            treeView = treeViewForItem(treeView);
            focusRecord = contains(treeView) ? nodes.tryGet(treeView.storageId()) : nullptr;
        }
        if (focusRecord == nullptr || focusRecord->kind != BuiltinElementKind::TreeView || !isNodeEnabled(treeView))
        {
            return UITreeViewCommandResult{};
        }

        TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
        const u64 itemCount = state.dataSource.hasValue() ? state.dataSource.itemCount(state.dataSource.state) : 0;
        const auto consumeWithoutChange = [&]() noexcept {
            treeViewCommandPressLatch.latch(command);
            return UITreeViewCommandResult{
                .consumed = true,
                .selection = state.selection,
            };
        };
        u64 selectedIndex = 0;
        UITreeViewItemDescriptor selectedDescriptor{};
        bool hasSelectedItem = false;
        if (state.selection.hasValue())
        {
            if (state.selection.logicalIndex < itemCount)
            {
                auto resolved = resolveTreeViewLogicalItem(treeView, state.selection.logicalIndex);
                if (!resolved)
                {
                    return Core::failure(resolved.error());
                }
                if (resolved->key == state.selection.key)
                {
                    selectedIndex = state.selection.logicalIndex;
                    selectedDescriptor = *resolved;
                    hasSelectedItem = true;
                }
            }
            if (!hasSelectedItem)
            {
                for (u64 logicalIndex = 0; logicalIndex < itemCount; ++logicalIndex)
                {
                    auto resolved = resolveTreeViewLogicalItem(treeView, logicalIndex);
                    if (!resolved)
                    {
                        return Core::failure(resolved.error());
                    }
                    if (resolved->key == state.selection.key)
                    {
                        selectedIndex = logicalIndex;
                        selectedDescriptor = *resolved;
                        hasSelectedItem = true;
                        break;
                    }
                }
            }
        }

        if (command == UITreeViewCommand::Activate)
        {
            if (!hasSelectedItem || !selectedDescriptor.enabled)
            {
                return consumeWithoutChange();
            }
            treeViewCommandPressLatch.latch(command);
            return UITreeViewCommandResult{
                .consumed = true,
                .activated = true,
                .selection = state.selection,
            };
        }
        if (itemCount == 0)
        {
            return consumeWithoutChange();
        }

        const UINodeId updaterRoot = idForIndex(focusRecord->rootIndex);
        auto applyExpansion = [&](bool expanded) -> Core::Result<UITreeViewCommandResult> {
            if (!hasSelectedItem || !selectedDescriptor.enabled || !selectedDescriptor.expandable)
            {
                return consumeWithoutChange();
            }
            if (Core::Status changed =
                    setTreeViewItemExpandedFromUpdater(updaterRoot, treeView, selectedIndex, expanded);
                !changed)
            {
                return Core::failure(changed.error());
            }
            treeViewCommandPressLatch.latch(command);
            return UITreeViewCommandResult{
                .consumed = true,
                .changed = true,
                .expansionChanged = true,
                .selection = state.selection,
            };
        };

        if (command == UITreeViewCommand::ToggleExpanded)
        {
            return applyExpansion(!selectedDescriptor.expanded);
        }
        if (command == UITreeViewCommand::CollapseOrParent && hasSelectedItem && selectedDescriptor.enabled &&
            selectedDescriptor.expandable && selectedDescriptor.expanded)
        {
            return applyExpansion(false);
        }
        if (command == UITreeViewCommand::ExpandOrFirstChild && hasSelectedItem && selectedDescriptor.enabled &&
            selectedDescriptor.expandable && !selectedDescriptor.expanded)
        {
            return applyExpansion(true);
        }

        u64 candidate = 0;
        UITreeViewItemDescriptor candidateDescriptor{};
        bool found = false;
        bool searchBackward = false;
        switch (command)
        {
        case UITreeViewCommand::PreviousItem:
        case UITreeViewCommand::PreviousPage:
        case UITreeViewCommand::NextItem:
        case UITreeViewCommand::NextPage:
        case UITreeViewCommand::FirstItem:
        case UITreeViewCommand::LastItem: {
            const u64 pageItems =
                (std::max)(u64{1}, static_cast<u64>(
                                       std::floor(state.committedMetrics.viewportSize.height / state.style.rowHeight)));
            switch (command)
            {
            case UITreeViewCommand::PreviousItem:
                candidate = hasSelectedItem && selectedIndex != 0 ? selectedIndex - 1 : 0;
                break;
            case UITreeViewCommand::NextItem:
                candidate = hasSelectedItem ? (std::min)(itemCount - 1, selectedIndex + 1) : 0;
                break;
            case UITreeViewCommand::PreviousPage:
                candidate = hasSelectedItem && selectedIndex > pageItems ? selectedIndex - pageItems : 0;
                break;
            case UITreeViewCommand::NextPage:
                candidate = hasSelectedItem ? (std::min)(itemCount - 1, selectedIndex + pageItems) : 0;
                break;
            case UITreeViewCommand::FirstItem:
                candidate = 0;
                break;
            case UITreeViewCommand::LastItem:
                candidate = itemCount - 1;
                break;
            default:
                break;
            }
            searchBackward = command == UITreeViewCommand::LastItem ||
                             (hasSelectedItem && (command == UITreeViewCommand::PreviousItem ||
                                                  command == UITreeViewCommand::PreviousPage));
            for (u64 visited = 0; visited < itemCount; ++visited)
            {
                auto resolved = resolveTreeViewLogicalItem(treeView, candidate);
                if (!resolved)
                {
                    return Core::failure(resolved.error());
                }
                if (resolved->enabled)
                {
                    candidateDescriptor = *resolved;
                    found = true;
                    break;
                }
                if (searchBackward)
                {
                    if (candidate == 0)
                    {
                        break;
                    }
                    --candidate;
                } else
                {
                    if (candidate + 1 >= itemCount)
                    {
                        break;
                    }
                    ++candidate;
                }
            }
            break;
        }
        case UITreeViewCommand::CollapseOrParent: {
            if (!hasSelectedItem || selectedDescriptor.level == 0)
            {
                return consumeWithoutChange();
            }
            u32 ancestorLevel = selectedDescriptor.level;
            for (u64 logicalIndex = selectedIndex; logicalIndex-- > 0;)
            {
                auto resolved = resolveTreeViewLogicalItem(treeView, logicalIndex);
                if (!resolved)
                {
                    return Core::failure(resolved.error());
                }
                if (resolved->level >= ancestorLevel)
                {
                    continue;
                }
                ancestorLevel = resolved->level;
                if (resolved->enabled)
                {
                    candidate = logicalIndex;
                    candidateDescriptor = *resolved;
                    found = true;
                    break;
                }
                if (ancestorLevel == 0)
                {
                    break;
                }
            }
            break;
        }
        case UITreeViewCommand::ExpandOrFirstChild: {
            if (!hasSelectedItem)
            {
                return consumeWithoutChange();
            }
            const u64 childLevel = static_cast<u64>(selectedDescriptor.level) + 1;
            for (u64 logicalIndex = selectedIndex + 1; logicalIndex < itemCount; ++logicalIndex)
            {
                auto resolved = resolveTreeViewLogicalItem(treeView, logicalIndex);
                if (!resolved)
                {
                    return Core::failure(resolved.error());
                }
                if (resolved->level <= selectedDescriptor.level)
                {
                    break;
                }
                if (static_cast<u64>(resolved->level) == childLevel && resolved->enabled)
                {
                    candidate = logicalIndex;
                    candidateDescriptor = *resolved;
                    found = true;
                    break;
                }
            }
            break;
        }
        case UITreeViewCommand::ToggleExpanded:
        case UITreeViewCommand::Activate:
            break;
        }

        if (!found)
        {
            return consumeWithoutChange();
        }
        const UITreeViewSelection nextSelection{
            .key = candidateDescriptor.key,
            .logicalIndex = candidate,
            .level = candidateDescriptor.level,
        };
        const bool changed = state.selection != nextSelection;
        if (changed)
        {
            if (Core::Status dirty = markPaintDirty(treeView); !dirty)
            {
                return Core::failure(dirty.error());
            }
            state.selection = nextSelection;
        }
        if (Core::Status revealed =
                scrollTreeViewToIndexFromUpdater(updaterRoot, treeView, candidate, UITreeViewScrollAlignment::Nearest);
            !revealed)
        {
            return Core::failure(revealed.error());
        }
        treeViewCommandPressLatch.latch(command);
        return UITreeViewCommandResult{
            .consumed = true,
            .changed = changed,
            .selection = state.selection,
        };
    }

    [[nodiscard]] Core::Status applyNavigationFocus(UINodeId nextFocus)
    {
        const UINodeId previousFocus = defaultActionFocusButton;
        const UINodeId dirtyPreviousFocus =
            previousFocus.hasValue() && previousFocus != nextFocus && contains(previousFocus) ? previousFocus
                                                                                              : UINodeId{};
        const UINodeId dirtyTextFocus =
            textInputFocus.hasValue() && textInputFocus != nextFocus && contains(textInputFocus) ? textInputFocus
                                                                                                 : UINodeId{};
        const UINodeId dirtyArmedSlider =
            armedSlider.hasValue() && armedSlider != nextFocus && contains(armedSlider) ? armedSlider : UINodeId{};
        if (Core::Status dirty = markPaintDirtyBatch({
                dirtyPreviousFocus,
                dirtyTextFocus,
                dirtyArmedSlider,
                nextFocus,
            });
            !dirty)
        {
            return dirty;
        }

        defaultActionPressState.clearAll();
        defaultActionFocusButton = nextFocus;
        clearArmedPrimaryButton();
        clearArmedSlider();
        clearArmedTextEdit();
        capturedPointerNode = {};
        const NodeRecord* nextRecord = nodes.tryGet(nextFocus.storageId());
        if (nextRecord != nullptr && nextRecord->kind == BuiltinElementKind::TextEdit)
        {
            if (textInputFocus != nextFocus)
            {
                clearImeFocus();
                textInputFocus = nextFocus;
            }
        } else
        {
            clearImeFocus();
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIDefaultFocusStepResult> routeDefaultActionFocusStep(bool reverse)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI focus traversal cannot run during pointer routing");
        }
        drainDeferredRootDestroys();

        const auto& entries = committedHitBuffers[publishedHitBufferIndex];
        u32 scopeEntryIndex = committedActiveModalEntryIndex;
        const u32 currentFocusEntryIndex = findHitEntryIndex(defaultActionFocusButton, entries);
        if (currentFocusEntryIndex < entries.size() &&
            entries[currentFocusEntryIndex].focusScopeEntryIndex < entries.size())
        {
            scopeEntryIndex = entries[currentFocusEntryIndex].focusScopeEntryIndex;
        }

        u32 firstCandidateEntryIndex = InvalidUIHitEntryIndex;
        u32 lastCandidateEntryIndex = InvalidUIHitEntryIndex;
        u32 previousCandidateEntryIndex = InvalidUIHitEntryIndex;
        u32 nextCandidateEntryIndex = InvalidUIHitEntryIndex;
        bool currentFocusFound = false;
        usize candidateCount = 0;
        for (u32 entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
        {
            const UICommittedHitEntry& entry = entries[entryIndex];
            if (!contains(entry.node) || !isNodeEnabled(entry.node) || entry.policy != UIPointerHitPolicy::Targetable ||
                !hasBehavior(entry.behaviors, UIElementBehavior::Focusable) ||
                !hitEntryAllowedByModal(entry, committedActiveModalEntryIndex) ||
                !hitEntryIsWithinScope(entryIndex, scopeEntryIndex, entries))
            {
                continue;
            }

            ++candidateCount;
            if (firstCandidateEntryIndex == InvalidUIHitEntryIndex)
            {
                firstCandidateEntryIndex = entryIndex;
            }
            lastCandidateEntryIndex = entryIndex;
            if (currentFocusFound && nextCandidateEntryIndex == InvalidUIHitEntryIndex)
            {
                nextCandidateEntryIndex = entryIndex;
            }
            if (entry.node == defaultActionFocusButton)
            {
                currentFocusFound = true;
            } else if (!currentFocusFound)
            {
                previousCandidateEntryIndex = entryIndex;
            }
        }
        if (candidateCount == 0)
        {
            if (Core::Status cleared = applyExplicitFocus({}); !cleared)
            {
                return Core::failure(cleared.error());
            }
            return UIDefaultFocusStepResult{};
        }

        const u32 nextEntryIndex =
            currentFocusFound
                ? (reverse ? (previousCandidateEntryIndex != InvalidUIHitEntryIndex ? previousCandidateEntryIndex
                                                                                    : lastCandidateEntryIndex)
                           : (nextCandidateEntryIndex != InvalidUIHitEntryIndex ? nextCandidateEntryIndex
                                                                                : firstCandidateEntryIndex))
                : (reverse ? lastCandidateEntryIndex : firstCandidateEntryIndex);
        const UINodeId nextFocus = entries[nextEntryIndex].node;
        const bool moved = !defaultActionFocusButton.hasValue() || defaultActionFocusButton != nextFocus;
        if (Core::Status focused = applyNavigationFocus(nextFocus); !focused)
        {
            return Core::failure(focused.error());
        }
        return UIDefaultFocusStepResult{
            .consumed = true,
            .moved = moved,
            .focus = nextFocus,
        };
    }

    [[nodiscard]] Core::Result<UIRangeInputCommandResult>
    routeRangeInputCommand(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                           UIRangeInputCommand command, bool pressed,
                           const Platform::DigitalControlIdentity& control)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI RangeInput command cannot run during pointer routing");
        }
        if (!platformFrame.hasValue() || sourceSequence == 0)
        {
            return fail(UIErrorCode::InvalidPointerInput,
                        "UI RangeInput command requires a platform frame and sequence");
        }
        if (command != UIRangeInputCommand::Decrease && command != UIRangeInputCommand::Increase)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI RangeInput command is not recognized");
        }
        if (Core::Status validControl = rangeInputPressLatch.validateControl(control); !validControl)
        {
            return Core::failure(validControl.error());
        }
        drainDeferredRootDestroys();

        if (!pressed)
        {
            return UIRangeInputCommandResult{
                .consumed = rangeInputPressLatch.release(control, command),
                .changed = false,
            };
        }
        if (rangeInputPressLatch.isLatched(control, command))
        {
            return UIRangeInputCommandResult{.consumed = true, .changed = false, .targeted = true};
        }

        const UINodeId target = defaultActionFocus();
        const NodeRecord* targetRecord = target.hasValue() ? nodes.tryGet(target.storageId()) : nullptr;
        const Detail::UIRangeInputState* state =
            target.hasValue() ? behaviorStateStorage.tryRangeInputState(target.index()) : nullptr;
        const bool targeted = targetRecord != nullptr && state != nullptr &&
                              hasBehavior(targetRecord->behaviors, UIElementBehavior::RangeInput);
        if (!targeted)
        {
            return UIRangeInputCommandResult{};
        }
        if (!isInteractiveRangeInput(target))
        {
            return UIRangeInputCommandResult{.targeted = true};
        }
        const double span = static_cast<double>(state->maxValue) - static_cast<double>(state->minValue);
        const double increment = state->step > 0.0F ? static_cast<double>(state->step) : span * 0.01;
        if (!(increment > 0.0) || !std::isfinite(increment))
        {
            return UIRangeInputCommandResult{.targeted = true};
        }
        const double requestedValue = static_cast<double>(state->value) +
                                      (command == UIRangeInputCommand::Increase ? increment : -increment);
        auto applied = applySliderValue(target, requestedValue, platformFrame, sourceSequence, true);
        if (!applied)
        {
            return Core::failure(applied.error());
        }
        if (!*applied)
        {
            return UIRangeInputCommandResult{.targeted = true};
        }
        rangeInputPressLatch.latch(control, command);
        return UIRangeInputCommandResult{.consumed = true, .changed = true, .targeted = true};
    }

    [[nodiscard]] Core::Result<UIDefaultFocusStepResult>
    routeFocusNavigation(UIFocusNavigationDirection direction, bool pressed)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!isValidFocusNavigationDirection(direction))
        {
            return fail(UIErrorCode::InvalidFocusTarget, "UI focus navigation direction is not recognized");
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI focus navigation cannot run during pointer routing");
        }
        drainDeferredRootDestroys();

        const UINodeId currentFocus = defaultActionFocus();
        if (!pressed)
        {
            return UIDefaultFocusStepResult{
                .consumed = focusNavigationPressLatch.release(direction),
                .moved = false,
                .focus = currentFocus,
            };
        }
        if (focusNavigationPressLatch.isLatched(direction))
        {
            return UIDefaultFocusStepResult{
                .consumed = true,
                .moved = false,
                .focus = currentFocus,
            };
        }
        if (!currentFocus.hasValue())
        {
            return UIDefaultFocusStepResult{};
        }
        const NodeRecord* currentRecord = nodes.tryGet(currentFocus.storageId());
        if (currentRecord == nullptr || ownsDirectionalNavigation(currentRecord->kind))
        {
            return UIDefaultFocusStepResult{
                .consumed = false,
                .moved = false,
                .focus = currentFocus,
            };
        }

        const auto& entries = committedHitBuffers[publishedHitBufferIndex];
        u32 scopeEntryIndex = committedActiveModalEntryIndex;
        const u32 currentFocusEntryIndex = findHitEntryIndex(currentFocus, entries);
        if (currentFocusEntryIndex < entries.size() &&
            entries[currentFocusEntryIndex].focusScopeEntryIndex < entries.size())
        {
            scopeEntryIndex = entries[currentFocusEntryIndex].focusScopeEntryIndex;
        }
        const auto isCandidate = [&](u32 entryIndex) noexcept {
            const UICommittedHitEntry& entry = entries[entryIndex];
            const NodeRecord* record = nodes.tryGet(entry.node.storageId());
            return contains(entry.node) && isNodeEnabled(entry.node) &&
                   record != nullptr && !isCompositeFocusItem(record->kind) &&
                   entry.policy == UIPointerHitPolicy::Targetable &&
                   hasBehavior(entry.behaviors, UIElementBehavior::Focusable) &&
                   hitEntryAllowedByModal(entry, committedActiveModalEntryIndex) &&
                   hitEntryIsWithinScope(entryIndex, scopeEntryIndex, entries);
        };
        const u32 nextEntryIndex =
            findFocusNavigationCandidate(entries, currentFocusEntryIndex, direction, isCandidate);
        if (nextEntryIndex == InvalidUIHitEntryIndex)
        {
            return UIDefaultFocusStepResult{
                .consumed = false,
                .moved = false,
                .focus = currentFocus,
            };
        }

        const UINodeId nextFocus = entries[nextEntryIndex].node;
        const bool moved = nextFocus != currentFocus;
        if (Core::Status focused = applyNavigationFocus(nextFocus); !focused)
        {
            return Core::failure(focused.error());
        }
        focusNavigationPressLatch.latch(direction);
        return UIDefaultFocusStepResult{
            .consumed = true,
            .moved = moved,
            .focus = nextFocus,
        };
    }

    [[nodiscard]] UINodeId defaultActionFocus() const noexcept
    {
        if (!isCommittedKeyboardFocusCandidate(defaultActionFocusButton))
        {
            return {};
        }
        const NodeRecord* record = nodes.tryGet(defaultActionFocusButton.storageId());
        if (record != nullptr && record->kind == BuiltinElementKind::TextEdit && textInputFocus != defaultActionFocusButton)
        {
            return {};
        }
        return defaultActionFocusButton;
    }

    [[nodiscard]] UINodeId imeFocus() const noexcept
    {
        if (!isCommittedTextEditFocusCandidate(textInputFocus) || defaultActionFocusButton != textInputFocus)
        {
            return {};
        }
        return textInputFocus;
    }

    [[nodiscard]] bool imeCompositionActive() const noexcept
    {
        return imeComposition.active() && isCommittedTextEditFocusCandidate(textInputFocus) &&
               defaultActionFocusButton == textInputFocus;
    }

    [[nodiscard]] std::string_view imePreeditUtf8() const noexcept
    {
        return imeComposition.preeditUtf8();
    }

    [[nodiscard]] u32 imePreeditCursorCodepoint() const noexcept
    {
        return imeComposition.cursorCodepoint();
    }

    [[nodiscard]] Core::Result<UITextInputRouteResult>
    routeTextComposition(Platform::WindowId window, Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                         std::string_view preeditUtf8, u32 cursorCodepoint, Platform::TextCompositionStage stage)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!window.hasValue() || window != ownerWindow)
        {
            return fail(UIErrorCode::WrongOwnerWindow, "UI text composition belongs to another owner window");
        }
        if (!platformFrame.hasValue() || sourceSequence == 0)
        {
            return fail(UIErrorCode::InvalidPointerInput, "UI text composition requires a platform frame and sequence");
        }

        using Stage = Platform::TextCompositionStage;
        const bool knownStage =
            stage == Stage::Started || stage == Stage::Updated || stage == Stage::Ended || stage == Stage::Cancelled;
        if (!knownStage)
        {
            return fail(UIErrorCode::InvalidText, "UI text composition stage is not recognized");
        }
        if (!isCommittedTextEditFocusCandidate(textInputFocus))
        {
            clearImeFocus();
            return UITextInputRouteResult{};
        }

        if (stage == Stage::Cancelled || stage == Stage::Ended)
        {
            // clearImeComposition marks paint dirty when preedit was active.
            if (Core::Status status = clearImeComposition(); !status)
            {
                return Core::failure(status.error());
            }
            return UITextInputRouteResult{.consumed = true, .applied = true};
        }
        if (!Core::isStrictUtf8WithoutNul(preeditUtf8))
        {
            return fail(UIErrorCode::InvalidText, "UI IME preedit must be strict UTF-8 without embedded NUL");
        }
        if (containsLineBreak(preeditUtf8))
        {
            return fail(UIErrorCode::InvalidText, "UI TextEdit preedit accepts one logical line without CR or LF");
        }
        if (Core::Status capacityStatus = Detail::UIImeCompositionState::validateCapacity(preeditUtf8);
            !capacityStatus)
        {
            return Core::failure(capacityStatus.error());
        }
        const auto codepoints = Core::countStrictUtf8CodepointsWithoutNul(preeditUtf8);
        if (!codepoints.has_value())
        {
            return fail(UIErrorCode::InvalidText, "UI IME preedit must be strict UTF-8 without embedded NUL");
        }
        if (Core::Status paintStatus = markPaintDirty(textInputFocus); !paintStatus)
        {
            return Core::failure(paintStatus.error());
        }
        imeComposition.assign(preeditUtf8, cursorCodepoint, *codepoints);
        return UITextInputRouteResult{.consumed = true, .applied = true};
    }

    [[nodiscard]] Core::Result<UITextInputRouteResult> routeTextInput(Platform::WindowId window,
                                                                      Platform::PlatformFrameId platformFrame,
                                                                      u64 sourceSequence,
                                                                      std::string_view committedUtf8)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!window.hasValue() || window != ownerWindow)
        {
            return fail(UIErrorCode::WrongOwnerWindow, "UI text input belongs to another owner window");
        }
        if (!platformFrame.hasValue() || sourceSequence == 0)
        {
            return fail(UIErrorCode::InvalidPointerInput, "UI text input requires a platform frame and sequence");
        }
        if (!isCommittedTextEditFocusCandidate(textInputFocus))
        {
            clearImeFocus();
            return UITextInputRouteResult{};
        }
        if (!Core::isStrictUtf8WithoutNul(committedUtf8))
        {
            return fail(UIErrorCode::InvalidText, "UI text input must be strict UTF-8 without embedded NUL");
        }
        if (containsLineBreak(committedUtf8))
        {
            return UITextInputRouteResult{.consumed = true, .applied = false};
        }
        if (committedUtf8.empty())
        {
            if (Core::Status status = clearImeComposition(); !status)
            {
                return Core::failure(status.error());
            }
            return UITextInputRouteResult{.consumed = true, .applied = true};
        }

        const UINodeId focusedTextEdit = textInputFocus;
        const NodeRecord* record = nodes.tryGet(focusedTextEdit.storageId());
        if (record == nullptr)
        {
            clearImeFocus();
            return UITextInputRouteResult{};
        }
        const UINodeId rootNode = idForIndex(record->rootIndex);
        if (!rootNode.hasValue())
        {
            clearImeFocus();
            return UITextInputRouteResult{};
        }

        const std::string_view current = textViewFor(focusedTextEdit.index());
        Detail::UITextInputState* textInputState =
            behaviorStateStorage.tryTextInputState(focusedTextEdit.index());
        if (textInputState == nullptr)
        {
            clearImeFocus();
            return fail(Core::CoreErrorCode::Internal, "UI TextEdit is missing TextInput behavior state");
        }
        const UITextSelection selection = textInputState->selection;
        const u32 selectionBegin = (std::min)(selection.anchorCodepoint, selection.caretCodepoint);
        const u32 selectionEnd = (std::max)(selection.anchorCodepoint, selection.caretCodepoint);
        const usize selectionBeginByte = utf8ByteOffsetForCodepoint(current, selectionBegin);
        const usize selectionEndByte = utf8ByteOffsetForCodepoint(current, selectionEnd);
        const usize retainedBytes = current.size() - (selectionEndByte - selectionBeginByte);
        if (retainedBytes > (std::numeric_limits<usize>::max)() - committedUtf8.size())
        {
            return fail(UIErrorCode::CapacityExceeded, "UI text input would overflow the text byte capacity");
        }
        std::string combined;
        try
        {
            // One-shot commit allocation; not a per-frame hot path.
            combined.reserve(retainedBytes + committedUtf8.size());
            combined.append(current.substr(0, selectionBeginByte));
            combined.append(committedUtf8);
            combined.append(current.substr(selectionEndByte));
        } catch (const std::bad_alloc&)
        {
            return fail(Core::CoreErrorCode::OutOfMemory, "UI text input scratch allocation failed");
        }
        if (Core::Status status = setTextFromUpdater(rootNode, focusedTextEdit, combined); !status)
        {
            return Core::failure(status.error());
        }
        if (Core::Status dirty = markPaintDirty(focusedTextEdit); !dirty)
        {
            return Core::failure(dirty.error());
        }
        const auto insertedCodepoints = Core::countStrictUtf8CodepointsWithoutNul(committedUtf8);
        const u32 nextCaret = selectionBegin + insertedCodepoints.value_or(0U);
        textInputState->selection = {
            .anchorCodepoint = nextCaret,
            .caretCodepoint = nextCaret,
        };
        if (Core::Status status = clearImeComposition(); !status)
        {
            return Core::failure(status.error());
        }
        return UITextInputRouteResult{.consumed = true, .applied = true};
    }

    [[nodiscard]] Core::Result<UITextInputRouteResult>
    routeTextEditCommand(Platform::WindowId window, Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                         UITextEditCommand command, bool extendSelection)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!window.hasValue() || window != ownerWindow)
        {
            return fail(UIErrorCode::WrongOwnerWindow, "UI TextEdit command belongs to another owner window");
        }
        if (!platformFrame.hasValue() || sourceSequence == 0)
        {
            return fail(UIErrorCode::InvalidPointerInput, "UI TextEdit command requires a platform frame and sequence");
        }
        if (!isCommittedTextEditFocusCandidate(textInputFocus))
        {
            clearImeFocus();
            return UITextInputRouteResult{};
        }

        const UINodeId focusedTextEdit = textInputFocus;
        const NodeRecord* record = nodes.tryGet(focusedTextEdit.storageId());
        if (record == nullptr)
        {
            clearImeFocus();
            return UITextInputRouteResult{};
        }
        const UINodeId rootNode = idForIndex(record->rootIndex);
        if (!rootNode.hasValue())
        {
            clearImeFocus();
            return UITextInputRouteResult{};
        }

        Detail::UITextInputState* editState =
            behaviorStateStorage.tryTextInputState(focusedTextEdit.index());
        if (editState == nullptr)
        {
            clearImeFocus();
            return fail(Core::CoreErrorCode::Internal, "UI TextEdit is missing TextInput behavior state");
        }
        const u32 codepointCount = textStatesByIndex[focusedTextEdit.index()].metrics.codepointCount;
        const UITextSelection currentSelection = editState->selection;
        const auto plan = planTextEditCommand(currentSelection, codepointCount,
                                              command, extendSelection);
        if (!plan.has_value())
        {
            return fail(UIErrorCode::InvalidText, "UI TextEdit command is not recognized");
        }

        if (!plan->deletesText)
        {
            if (plan->nextSelection == currentSelection)
            {
                if (Core::Status status = clearImeComposition(); !status)
                {
                    return Core::failure(status.error());
                }
                return UITextInputRouteResult{.consumed = true, .applied = false};
            }
            if (Core::Status paintStatus = markPaintDirty(focusedTextEdit); !paintStatus)
            {
                return Core::failure(paintStatus.error());
            }
            if (Core::Status status = clearImeComposition(); !status)
            {
                return Core::failure(status.error());
            }
            editState->selection = plan->nextSelection;
            return UITextInputRouteResult{.consumed = true, .applied = true};
        }

        if (plan->deleteBeginCodepoint == plan->deleteEndCodepoint)
        {
            if (Core::Status status = clearImeComposition(); !status)
            {
                return Core::failure(status.error());
            }
            return UITextInputRouteResult{.consumed = true, .applied = false};
        }
        const std::string_view current = textViewFor(focusedTextEdit.index());
        const usize deleteBeginByte =
            utf8ByteOffsetForCodepoint(current, plan->deleteBeginCodepoint);
        const usize deleteEndByte =
            utf8ByteOffsetForCodepoint(current, plan->deleteEndCodepoint);
        std::string combined;
        try
        {
            combined.reserve(current.size() - (deleteEndByte - deleteBeginByte));
            combined.append(current.substr(0, deleteBeginByte));
            combined.append(current.substr(deleteEndByte));
        } catch (const std::bad_alloc&)
        {
            return fail(Core::CoreErrorCode::OutOfMemory, "UI TextEdit command scratch allocation failed");
        }
        if (Core::Status status = setTextFromUpdater(rootNode, focusedTextEdit, combined); !status)
        {
            return Core::failure(status.error());
        }
        editState->selection = {
            .anchorCodepoint = plan->deleteBeginCodepoint,
            .caretCodepoint = plan->deleteBeginCodepoint,
        };
        if (Core::Status status = clearImeComposition(); !status)
        {
            return Core::failure(status.error());
        }
        return UITextInputRouteResult{.consumed = true, .applied = true};
    }

    [[nodiscard]] UIContextStatistics statistics() const noexcept
    {
        const Detail::UIBehaviorStateStorageCounters behaviorCounters =
            behaviorStateStorage.counters();
        const Detail::UIStyleSheetStorageStatistics styleStatistics =
            styleSheetStorage.statistics();
        return UIContextStatistics{
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
        };
    }
};

UIRoutedPointerListenerToken::UIRoutedPointerListenerToken(std::weak_ptr<Detail::UIContextLifetimeControl> lifetime,
                                                           u32 slot, u32 generation) noexcept
    : m_lifetime(std::move(lifetime)), m_slot(slot), m_generation(generation)
{
}

UIRoutedPointerListenerToken::~UIRoutedPointerListenerToken() noexcept
{
    reset();
}

UIRoutedPointerListenerToken::UIRoutedPointerListenerToken(UIRoutedPointerListenerToken&& other) noexcept
    : m_lifetime(std::move(other.m_lifetime)), m_slot(std::exchange(other.m_slot, 0)),
      m_generation(std::exchange(other.m_generation, 0))
{
}

UIRoutedPointerListenerToken& UIRoutedPointerListenerToken::operator=(UIRoutedPointerListenerToken&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    reset();
    m_lifetime = std::move(other.m_lifetime);
    m_slot = std::exchange(other.m_slot, 0);
    m_generation = std::exchange(other.m_generation, 0);
    return *this;
}

void UIRoutedPointerListenerToken::reset() noexcept
{
    const u32 generation = m_generation;
    if (generation == 0)
    {
        m_lifetime.reset();
        m_slot = 0;
        return;
    }

    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
    UIContext* immediateContext =
        lifetime ? lifetime->releaseRoutedPointerListener(m_slot, generation)
                 : nullptr;

    if (immediateContext != nullptr)
    {
        immediateContext->releaseRoutedPointerListenerFromToken(m_slot, generation);
    }
    m_lifetime.reset();
    m_slot = 0;
    m_generation = 0;
}

bool UIRoutedPointerListenerToken::isActive() const noexcept
{
    if (m_generation == 0)
    {
        return false;
    }
    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
    if (!lifetime)
    {
        return false;
    }
    return lifetime->isRoutedPointerListenerActive(m_slot, m_generation);
}

UIRoutedPointerListenerToken::operator bool() const noexcept
{
    return isActive();
}

UIRootOwner::UIRootOwner(std::weak_ptr<Detail::UIContextLifetimeControl> lifetime, UINodeId root) noexcept
    : m_lifetime(std::move(lifetime)), m_root(root)
{
}

UIRootOwner::~UIRootOwner() noexcept
{
    reset();
}

UIRootOwner::UIRootOwner(UIRootOwner&& other) noexcept : m_lifetime(std::move(other.m_lifetime)), m_root(other.m_root)
{
    other.m_root = {};
}

UIRootOwner& UIRootOwner::operator=(UIRootOwner&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    reset();
    m_lifetime = std::move(other.m_lifetime);
    m_root = other.m_root;
    other.m_root = {};
    return *this;
}

void UIRootOwner::reset() noexcept
{
    const UINodeId root = m_root;
    if (!root.hasValue())
    {
        m_lifetime.reset();
        return;
    }

    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
    if (!lifetime)
    {
        m_root = {};
        m_lifetime.reset();
        return;
    }

    UIContext* context = lifetime->releaseRoot(root);

    if (context != nullptr)
    {
        context->destroyRootFromOwner(root);
    }
    m_root = {};
    m_lifetime.reset();
}

UINodeId UIRootOwner::rootNodeId() const noexcept
{
    return m_root;
}

bool UIRootOwner::hasValue() const noexcept
{
    return m_root.hasValue();
}

UIRootOwner::operator bool() const noexcept
{
    return hasValue();
}

UIRootBuilder::UIRootBuilder(UIContext& context) noexcept : m_context(&context)
{
}

Core::Result<UIRootOwner> UIRootBuilder::createRoot()
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createRoot();
}

Core::Result<UINodeId> UIRootBuilder::createElement(UINodeId parent, const UIElementDescriptor& descriptor)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createElement(parent, descriptor);
}

UIElementBuildTransaction::UIElementBuildTransaction(UIContext& context, UINodeId updaterRoot,
                                                     UINodeId componentRoot,
                                                     UIComponentBuildBudget remainingBudget) noexcept
    : m_lifetime(context.m_impl->lifetime), m_updaterRoot(updaterRoot), m_componentRoot(componentRoot),
      m_remainingBudget(remainingBudget)
{
}

UIElementBuildTransaction::~UIElementBuildTransaction() noexcept
{
    reset();
}

UIElementBuildTransaction::UIElementBuildTransaction(UIElementBuildTransaction&& other) noexcept
    : m_lifetime(std::move(other.m_lifetime)), m_updaterRoot(std::exchange(other.m_updaterRoot, {})),
      m_componentRoot(std::exchange(other.m_componentRoot, {})),
      m_remainingBudget(std::exchange(other.m_remainingBudget, UIComponentBuildBudget{})),
      m_failure(std::move(other.m_failure))
{
    other.m_failure.reset();
}

UIElementBuildTransaction& UIElementBuildTransaction::operator=(UIElementBuildTransaction&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    reset();
    m_lifetime = std::move(other.m_lifetime);
    m_updaterRoot = std::exchange(other.m_updaterRoot, {});
    m_componentRoot = std::exchange(other.m_componentRoot, {});
    m_remainingBudget = std::exchange(other.m_remainingBudget, UIComponentBuildBudget{});
    m_failure = std::move(other.m_failure);
    other.m_failure.reset();
    return *this;
}

Core::Result<UINodeId> UIElementBuildTransaction::createElement(UINodeId parent,
                                                                const UIElementDescriptor& descriptor)
{
    if (m_failure.has_value())
    {
        return Core::failure(*m_failure);
    }
    if (!m_componentRoot.hasValue())
    {
        return fail(UIErrorCode::InvalidNode, "UI component build transaction is not active");
    }
    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
    UIContext* context = lifetime ? lifetime->attachedContext() : nullptr;
    if (context == nullptr)
    {
        m_componentRoot = {};
        return fail(UIErrorCode::WrongContext, "UI component build transaction context no longer exists");
    }
    auto created = context->createElementFromBuildTransaction(
        m_updaterRoot, m_componentRoot, parent, descriptor, m_remainingBudget);
    if (!created)
    {
        if (created.error().code == UIErrorCode::WrongOwnerThread)
        {
            return Core::failure(created.error());
        }
        m_failure = created.error();
        context->rollbackBuildTransaction(m_updaterRoot, m_componentRoot, m_remainingBudget);
        m_componentRoot = {};
        m_remainingBudget = {};
        return Core::failure(*m_failure);
    }
    return created;
}

Core::Result<UINodeId> UIElementBuildTransaction::commit()
{
    if (m_failure.has_value())
    {
        return Core::failure(*m_failure);
    }
    if (!m_componentRoot.hasValue())
    {
        return fail(UIErrorCode::InvalidNode, "UI component build transaction is not active");
    }
    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
    UIContext* context = lifetime ? lifetime->attachedContext() : nullptr;
    if (context == nullptr)
    {
        m_componentRoot = {};
        return fail(UIErrorCode::WrongContext, "UI component build transaction context no longer exists");
    }
    const UINodeId componentRoot = m_componentRoot;
    if (Core::Status status =
            context->commitBuildTransaction(m_updaterRoot, componentRoot, m_remainingBudget);
        !status)
    {
        if (status.error().code == UIErrorCode::WrongOwnerThread)
        {
            return Core::failure(status.error());
        }
        m_failure = status.error();
        m_componentRoot = {};
        m_remainingBudget = {};
        return Core::failure(*m_failure);
    }
    m_lifetime.reset();
    m_updaterRoot = {};
    m_componentRoot = {};
    m_remainingBudget = {};
    return componentRoot;
}

void UIElementBuildTransaction::reset() noexcept
{
    const UINodeId componentRoot = m_componentRoot;
    if (componentRoot.hasValue())
    {
        const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
        UIContext* context = lifetime ? lifetime->attachedContext() : nullptr;
        if (context != nullptr)
        {
            context->rollbackBuildTransaction(m_updaterRoot, componentRoot, m_remainingBudget);
        }
    }
    m_lifetime.reset();
    m_updaterRoot = {};
    m_componentRoot = {};
    m_remainingBudget = {};
    m_failure.reset();
}

UINodeId UIElementBuildTransaction::rootNodeId() const noexcept
{
    return m_componentRoot;
}

UIComponentBuildBudget UIElementBuildTransaction::remainingBudget() const noexcept
{
    return isActive() ? m_remainingBudget : UIComponentBuildBudget{};
}

bool UIElementBuildTransaction::isActive() const noexcept
{
    if (!m_componentRoot.hasValue())
    {
        return false;
    }
    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
    if (!lifetime)
    {
        return false;
    }
    UIContext* context = lifetime->attachedContext();
    return context != nullptr && context->isBuildTransactionActive(m_componentRoot);
}

UITreeUpdater::UITreeUpdater(UIContext& context, UINodeId root) noexcept : m_context(&context), m_root(root)
{
}

UITreeUpdater::UITreeUpdater(UITreeUpdater&& other) noexcept
    : m_context(std::exchange(other.m_context, nullptr)), m_root(std::exchange(other.m_root, {}))
{
}

UITreeUpdater& UITreeUpdater::operator=(UITreeUpdater&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    m_context = std::exchange(other.m_context, nullptr);
    m_root = std::exchange(other.m_root, {});
    return *this;
}

Core::Result<UINodeId> UITreeUpdater::createElement(UINodeId parent, const UIElementDescriptor& descriptor)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createElementFromUpdater(m_root, parent, descriptor);
}

Core::Result<UIElementBuildTransaction>
UITreeUpdater::beginBuildTransaction(UINodeId parent, const UIElementDescriptor& rootDescriptor,
                                     UIComponentBuildBudget budget)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->beginBuildTransactionFromUpdater(m_root, parent, rootDescriptor, budget);
}

bool UITreeUpdater::isAlive(UINodeId node) const noexcept
{
    return m_context != nullptr && m_context->isAliveInRoot(m_root, node);
}

Core::Status UITreeUpdater::setLayoutStyle(UINodeId node, const UILayoutStyle& style)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setLayoutStyleFromUpdater(m_root, node, style);
}

Core::Status UITreeUpdater::setPointerHitPolicy(UINodeId node, UIPointerHitPolicy policy)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setPointerHitPolicyFromUpdater(m_root, node, policy);
}

Core::Status UITreeUpdater::setEnabled(UINodeId node, bool enabled)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setEnabledFromUpdater(m_root, node, enabled);
}

Core::Result<bool> UITreeUpdater::isEnabled(UINodeId node) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isEnabledFromUpdater(m_root, node);
}

Core::Status UITreeUpdater::setFocusScopeMode(UINodeId node, UIFocusScopeMode mode)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setFocusScopeModeFromUpdater(m_root, node, mode);
}

Core::Result<UIFocusScopeMode> UITreeUpdater::focusScopeMode(UINodeId node) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->focusScopeModeFromUpdater(m_root, node);
}

Core::Status UITreeUpdater::requestFocus(UINodeId node)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->requestFocusFromUpdater(m_root, node);
}

Core::Status UITreeUpdater::clearFocus()
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->clearFocusFromUpdater(m_root);
}

Core::Status UITreeUpdater::setStyleRole(UINodeId node, UIStyleRoleId role)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setStyleRoleFromUpdater(m_root, node, role);
}

Core::Result<UIStyleRoleId> UITreeUpdater::styleRole(UINodeId node) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->styleRoleFromUpdater(m_root, node);
}

Core::Status UITreeUpdater::clearOverride(UINodeId node, UIStyleOverride properties)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->clearOverrideFromUpdater(m_root, node, properties);
}

Core::Status UITreeUpdater::setBoxPaint(UINodeId node, const UIBoxPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setBoxPaintFromUpdater(m_root, node, paint);
}

Core::Status UITreeUpdater::setButtonPaint(UINodeId button, const UIButtonPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setButtonPaintFromUpdater(m_root, button, paint);
}

Core::Result<UIButtonPaint> UITreeUpdater::buttonPaint(UINodeId button) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->buttonPaintFromUpdater(m_root, button);
}

Core::Status UITreeUpdater::setText(UINodeId node, std::string_view utf8)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setTextFromUpdater(m_root, node, utf8);
}

Core::Status UITreeUpdater::setTextStyle(UINodeId node, const UITextStyle& style)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setTextStyleFromUpdater(m_root, node, style);
}

Core::Status UITreeUpdater::setContentAlignment(UINodeId node, UIContentAlignment alignment)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setContentAlignmentFromUpdater(m_root, node, alignment);
}

Core::Result<std::string_view> UITreeUpdater::text(UINodeId node)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->textFromUpdater(m_root, node);
}

Core::Result<UITextStyle> UITreeUpdater::textStyle(UINodeId node)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->textStyleFromUpdater(m_root, node);
}

Core::Result<UIContentAlignment> UITreeUpdater::contentAlignment(UINodeId node) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->contentAlignmentFromUpdater(m_root, node);
}

Core::Status UITreeUpdater::setTextSelection(UINodeId textEdit, UITextSelection selection)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setTextSelectionFromUpdater(m_root, textEdit, selection);
}

Core::Result<UITextSelection> UITreeUpdater::textSelection(UINodeId textEdit) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->textSelectionFromUpdater(m_root, textEdit);
}

Core::Status UITreeUpdater::setTextEditPaint(UINodeId textEdit, const UITextEditPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setTextEditPaintFromUpdater(m_root, textEdit, paint);
}

Core::Result<UITextEditPaint> UITreeUpdater::textEditPaint(UINodeId textEdit) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->textEditPaintFromUpdater(m_root, textEdit);
}

Core::Status UITreeUpdater::setButtonAction(UINodeId button, UIButtonActionCallback callback)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setButtonActionFromUpdater(m_root, button, std::move(callback));
}

Core::Status UITreeUpdater::clearButtonAction(UINodeId button)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->clearButtonActionFromUpdater(m_root, button);
}

Core::Result<bool> UITreeUpdater::isButtonPressed(UINodeId button) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isButtonPressedFromUpdater(m_root, button);
}

Core::Status UITreeUpdater::setCheckboxAction(UINodeId checkbox, UIButtonActionCallback callback)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setCheckboxActionFromUpdater(m_root, checkbox, std::move(callback));
}

Core::Status UITreeUpdater::clearCheckboxAction(UINodeId checkbox)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->clearCheckboxActionFromUpdater(m_root, checkbox);
}

Core::Status UITreeUpdater::setCheckboxPaint(UINodeId checkbox, const UICheckboxPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setCheckboxPaintFromUpdater(m_root, checkbox, paint);
}

Core::Result<UICheckboxPaint> UITreeUpdater::checkboxPaint(UINodeId checkbox) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->checkboxPaintFromUpdater(m_root, checkbox);
}

Core::Status UITreeUpdater::setChecked(UINodeId checkbox, bool checked)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setCheckedFromUpdater(m_root, checkbox, checked);
}

Core::Result<bool> UITreeUpdater::isChecked(UINodeId checkbox) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isCheckedFromUpdater(m_root, checkbox);
}

Core::Result<bool> UITreeUpdater::isCheckboxPressed(UINodeId checkbox) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isCheckboxPressedFromUpdater(m_root, checkbox);
}

Core::Status UITreeUpdater::setSliderRange(UINodeId slider, float minValue, float maxValue, float step)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setSliderRangeFromUpdater(m_root, slider, minValue, maxValue, step);
}

Core::Status UITreeUpdater::setSliderValue(UINodeId slider, float value)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setSliderValueFromUpdater(m_root, slider, value);
}

Core::Result<float> UITreeUpdater::sliderValue(UINodeId slider) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->sliderValueFromUpdater(m_root, slider);
}

Core::Status UITreeUpdater::setSliderPaint(UINodeId slider, const UISliderPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setSliderPaintFromUpdater(m_root, slider, paint);
}

Core::Result<UISliderPaint> UITreeUpdater::sliderPaint(UINodeId slider) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->sliderPaintFromUpdater(m_root, slider);
}

Core::Status UITreeUpdater::setSliderChangeCallback(UINodeId slider, UISliderChangeCallback callback)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setSliderChangeCallbackFromUpdater(m_root, slider, std::move(callback));
}

Core::Status UITreeUpdater::clearSliderChangeCallback(UINodeId slider)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->clearSliderChangeCallbackFromUpdater(m_root, slider);
}

Core::Result<bool> UITreeUpdater::isSliderDragging(UINodeId slider) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isSliderDraggingFromUpdater(m_root, slider);
}

Core::Status UITreeUpdater::setScrollViewStyle(UINodeId scrollView, const UIScrollViewStyle& style)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setScrollViewStyleFromUpdater(m_root, scrollView, style);
}

Core::Result<UIScrollViewStyle> UITreeUpdater::scrollViewStyle(UINodeId scrollView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->scrollViewStyleFromUpdater(m_root, scrollView);
}

Core::Status UITreeUpdater::setScrollViewOffset(UINodeId scrollView, UIScrollOffset offset)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setScrollViewOffsetFromUpdater(m_root, scrollView, offset);
}

Core::Result<UIScrollOffset> UITreeUpdater::scrollViewOffset(UINodeId scrollView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->scrollViewOffsetFromUpdater(m_root, scrollView);
}

Core::Result<UIScrollViewMetrics> UITreeUpdater::scrollViewMetrics(UINodeId scrollView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->scrollViewMetricsFromUpdater(m_root, scrollView);
}

Core::Status UITreeUpdater::setScrollViewPaint(UINodeId scrollView, const UIScrollViewPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setScrollViewPaintFromUpdater(m_root, scrollView, paint);
}

Core::Result<UIScrollViewPaint> UITreeUpdater::scrollViewPaint(UINodeId scrollView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->scrollViewPaintFromUpdater(m_root, scrollView);
}

Core::Result<bool> UITreeUpdater::isScrollViewDragging(UINodeId scrollView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isScrollViewDraggingFromUpdater(m_root, scrollView);
}

Core::Status UITreeUpdater::setPopupStyle(UINodeId popup, const UIPopupStyle& style)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setPopupStyleFromUpdater(m_root, popup, style);
}

Core::Result<UIPopupStyle> UITreeUpdater::popupStyle(UINodeId popup) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->popupStyleFromUpdater(m_root, popup);
}

Core::Status UITreeUpdater::setPopupOpen(UINodeId popup, bool open)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setPopupOpenFromUpdater(m_root, popup, open);
}

Core::Result<bool> UITreeUpdater::isPopupOpen(UINodeId popup) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isPopupOpenFromUpdater(m_root, popup);
}

Core::Result<UIPopupMetrics> UITreeUpdater::popupMetrics(UINodeId popup) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->popupMetricsFromUpdater(m_root, popup);
}

Core::Status UITreeUpdater::setDropdownOpen(UINodeId dropdown, bool open)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setDropdownOpenFromUpdater(m_root, dropdown, open);
}

Core::Result<bool> UITreeUpdater::isDropdownOpen(UINodeId dropdown) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isDropdownOpenFromUpdater(m_root, dropdown);
}

Core::Status UITreeUpdater::setDropdownSelectedItem(UINodeId dropdown, UINodeId item)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setDropdownSelectedItemFromUpdater(m_root, dropdown, item);
}

Core::Result<UINodeId> UITreeUpdater::dropdownSelectedItem(UINodeId dropdown) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->dropdownSelectedItemFromUpdater(m_root, dropdown);
}

Core::Result<bool> UITreeUpdater::isDropdownItemSelected(UINodeId item) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isDropdownItemSelectedFromUpdater(m_root, item);
}

Core::Status UITreeUpdater::setDropdownPaint(UINodeId dropdown, const UIDropdownPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setDropdownPaintFromUpdater(m_root, dropdown, paint);
}

Core::Result<UIDropdownPaint> UITreeUpdater::dropdownPaint(UINodeId dropdown) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->dropdownPaintFromUpdater(m_root, dropdown);
}

Core::Status UITreeUpdater::setListViewDataSource(UINodeId listView, UIListViewDataSource source)
{
    return m_context != nullptr
               ? m_context->setListViewDataSourceFromUpdater(m_root, listView, source)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::clearListViewDataSource(UINodeId listView)
{
    return m_context != nullptr
               ? m_context->clearListViewDataSourceFromUpdater(m_root, listView)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::invalidateListViewItems(UINodeId listView)
{
    return m_context != nullptr
               ? m_context->invalidateListViewItemsFromUpdater(m_root, listView)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::setListViewStyle(UINodeId listView, const UIListViewStyle& style)
{
    return m_context != nullptr
               ? m_context->setListViewStyleFromUpdater(m_root, listView, style)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UIListViewStyle> UITreeUpdater::listViewStyle(UINodeId listView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->listViewStyleFromUpdater(m_root, listView);
}

Core::Status UITreeUpdater::setListViewPaint(UINodeId listView, const UIListViewPaint& paint)
{
    return m_context != nullptr
               ? m_context->setListViewPaintFromUpdater(m_root, listView, paint)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UIListViewPaint> UITreeUpdater::listViewPaint(UINodeId listView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->listViewPaintFromUpdater(m_root, listView);
}

Core::Result<UIListViewMetrics> UITreeUpdater::listViewMetrics(UINodeId listView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->listViewMetricsFromUpdater(m_root, listView);
}

Core::Status UITreeUpdater::setListViewSelectedIndex(UINodeId listView, u64 logicalIndex)
{
    return m_context != nullptr
               ? m_context->setListViewSelectedIndexFromUpdater(m_root, listView, logicalIndex)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::clearListViewSelection(UINodeId listView)
{
    return m_context != nullptr
               ? m_context->clearListViewSelectionFromUpdater(m_root, listView)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UIListViewSelection> UITreeUpdater::listViewSelection(UINodeId listView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->listViewSelectionFromUpdater(m_root, listView);
}

Core::Status UITreeUpdater::scrollListViewToIndex(UINodeId listView, u64 logicalIndex,
                                                 UIListViewScrollAlignment alignment)
{
    return m_context != nullptr ? m_context->scrollListViewToIndexFromUpdater(m_root, listView, logicalIndex, alignment)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::setTreeViewDataSource(UINodeId treeView, UITreeViewDataSource source)
{
    return m_context != nullptr ? m_context->setTreeViewDataSourceFromUpdater(m_root, treeView, source)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::clearTreeViewDataSource(UINodeId treeView)
{
    return m_context != nullptr ? m_context->clearTreeViewDataSourceFromUpdater(m_root, treeView)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::invalidateTreeViewItems(UINodeId treeView)
{
    return m_context != nullptr ? m_context->invalidateTreeViewItemsFromUpdater(m_root, treeView)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::setTreeViewStyle(UINodeId treeView, const UITreeViewStyle& style)
{
    return m_context != nullptr ? m_context->setTreeViewStyleFromUpdater(m_root, treeView, style)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UITreeViewStyle> UITreeUpdater::treeViewStyle(UINodeId treeView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->treeViewStyleFromUpdater(m_root, treeView);
}

Core::Status UITreeUpdater::setTreeViewPaint(UINodeId treeView, const UITreeViewPaint& paint)
{
    return m_context != nullptr ? m_context->setTreeViewPaintFromUpdater(m_root, treeView, paint)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UITreeViewPaint> UITreeUpdater::treeViewPaint(UINodeId treeView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->treeViewPaintFromUpdater(m_root, treeView);
}

Core::Result<UITreeViewMetrics> UITreeUpdater::treeViewMetrics(UINodeId treeView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->treeViewMetricsFromUpdater(m_root, treeView);
}

Core::Status UITreeUpdater::setTreeViewSelectedIndex(UINodeId treeView, u64 logicalIndex)
{
    return m_context != nullptr ? m_context->setTreeViewSelectedIndexFromUpdater(m_root, treeView, logicalIndex)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::clearTreeViewSelection(UINodeId treeView)
{
    return m_context != nullptr ? m_context->clearTreeViewSelectionFromUpdater(m_root, treeView)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UITreeViewSelection> UITreeUpdater::treeViewSelection(UINodeId treeView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->treeViewSelectionFromUpdater(m_root, treeView);
}

Core::Status UITreeUpdater::setTreeViewItemExpanded(UINodeId treeView, u64 logicalIndex, bool expanded)
{
    return m_context != nullptr
               ? m_context->setTreeViewItemExpandedFromUpdater(m_root, treeView, logicalIndex, expanded)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::scrollTreeViewToIndex(UINodeId treeView, u64 logicalIndex,
                                                  UITreeViewScrollAlignment alignment)
{
    return m_context != nullptr ? m_context->scrollTreeViewToIndexFromUpdater(m_root, treeView, logicalIndex, alignment)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::setProgressBarRange(UINodeId progressBar, float minValue, float maxValue)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setProgressBarRangeFromUpdater(m_root, progressBar, minValue, maxValue);
}

Core::Status UITreeUpdater::setProgressBarValue(UINodeId progressBar, float value)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setProgressBarValueFromUpdater(m_root, progressBar, value);
}

Core::Result<float> UITreeUpdater::progressBarValue(UINodeId progressBar) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->progressBarValueFromUpdater(m_root, progressBar);
}

Core::Status UITreeUpdater::setProgressBarPaint(UINodeId progressBar, const UIProgressBarPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setProgressBarPaintFromUpdater(m_root, progressBar, paint);
}

Core::Result<UIProgressBarPaint> UITreeUpdater::progressBarPaint(UINodeId progressBar) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->progressBarPaintFromUpdater(m_root, progressBar);
}

Core::Status UITreeUpdater::setRadioButtonPaint(UINodeId radioButton, const UIRadioButtonPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setRadioButtonPaintFromUpdater(m_root, radioButton, paint);
}

Core::Result<UIRadioButtonPaint> UITreeUpdater::radioButtonPaint(UINodeId radioButton) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->radioButtonPaintFromUpdater(m_root, radioButton);
}

Core::Status UITreeUpdater::setRadioButtonAction(UINodeId radioButton, UIButtonActionCallback callback)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setRadioButtonActionFromUpdater(m_root, radioButton, std::move(callback));
}

Core::Status UITreeUpdater::clearRadioButtonAction(UINodeId radioButton)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->clearRadioButtonActionFromUpdater(m_root, radioButton);
}

Core::Status UITreeUpdater::setRadioButtonSelected(UINodeId radioButton, bool selected)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setRadioButtonSelectedFromUpdater(m_root, radioButton, selected);
}

Core::Result<bool> UITreeUpdater::isRadioButtonSelected(UINodeId radioButton) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isRadioButtonSelectedFromUpdater(m_root, radioButton);
}

Core::Result<bool> UITreeUpdater::isRadioButtonPressed(UINodeId radioButton) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isRadioButtonPressedFromUpdater(m_root, radioButton);
}

Core::Result<UIRoutedPointerListenerToken>
UITreeUpdater::addRoutedPointerListener(UIRoutedPointerListenerDesc descriptor, UIRoutedPointerCallback callback)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->addRoutedPointerListenerFromUpdater(m_root, descriptor, std::move(callback));
}

Core::Status UITreeUpdater::destroy(UINodeId node)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->destroyNodeFromUpdater(m_root, node);
}

Core::Result<std::unique_ptr<UIContext>> UIContext::Create(Platform::WindowId ownerWindow,
                                                           UIContextCapacityConfig capacityConfig,
                                                           std::pmr::memory_resource& resource)
{
    // Validate before any allocation against the caller's resource so failed
    // Create remains allocation-free for invalid window/capacity probes.
    if (!ownerWindow.hasValue())
    {
        return fail(UIErrorCode::InvalidOwnerWindow, "UI context owner window id is empty");
    }
    if (Core::Status status = validateUIContextCapacityConfig(capacityConfig); !status)
    {
        return Core::failure(status.error());
    }

    // Placeholder rasterizer construction allocates against the caller's PMR and
    // must surface OOM as Result, not an uncaught bad_alloc (M10-A39 gate).
    try
    {
        auto rasterizer = createPlaceholderTextRasterizer({}, resource);
        if (!rasterizer)
        {
            return Core::failure(rasterizer.error());
        }
        return Create(ownerWindow, capacityConfig, std::move(*rasterizer), resource);
    } catch (const std::bad_alloc&)
    {
        return fail(Core::CoreErrorCode::OutOfMemory, "UI context allocation failed");
    } catch (const std::exception& exception)
    {
        return fail(Core::CoreErrorCode::Internal, std::string_view(exception.what()));
    } catch (...)
    {
        return fail(Core::CoreErrorCode::Internal, "UI context allocation failed");
    }
}

Core::Result<std::unique_ptr<UIContext>> UIContext::Create(Platform::WindowId ownerWindow,
                                                           UIContextCapacityConfig capacityConfig,
                                                           std::unique_ptr<IUITextRasterizer> textRasterizer,
                                                           std::pmr::memory_resource& resource)
{
    if (!ownerWindow.hasValue())
    {
        return fail(UIErrorCode::InvalidOwnerWindow, "UI context owner window id is empty");
    }
    if (!textRasterizer)
    {
        return fail(UIErrorCode::InvalidFont, "UI context text rasterizer is null");
    }

    auto normalizedResult = Detail::normalizeUIContextCapacityConfig(capacityConfig);
    if (!normalizedResult)
    {
        return Core::failure(normalizedResult.error());
    }

    try
    {
        const std::thread::id ownerThreadId = std::this_thread::get_id();
        auto lifetime = std::make_shared<Detail::UIContextLifetimeControl>(
            ownerThreadId, normalizedResult->rootCapacity, normalizedResult->routedPointerListenerCapacity);
        auto implResult = Impl::Create(ownerWindow, *normalizedResult, lifetime, resource);
        if (!implResult)
        {
            return Core::failure(implResult.error());
        }

        // Best-effort open of the built-in/empty face. Placeholder accepts {}.
        // FreeType adapters reject empty bytes; they remain without a face until
        // a later font-binding slice opens real bytes.
        auto faceResult = textRasterizer->openFace({});
        if (faceResult)
        {
            (*implResult)->textFace = *faceResult;
        }
        (*implResult)->textRasterizer = std::move(textRasterizer);

        auto atlasResult = UIGlyphAtlas::Create(
            UIGlyphAtlasCapacity{
                .width = 512,
                .height = 512,
                .maxGlyphs = 1024,
            },
            resource);
        if (atlasResult)
        {
            (*implResult)->glyphAtlas = std::move(*atlasResult);
        }

        auto context = std::unique_ptr<UIContext>(new UIContext(std::move(*implResult)));
        lifetime->attach(*context);
        return context;
    } catch (const std::bad_alloc&)
    {
        return fail(Core::CoreErrorCode::OutOfMemory, "UI context allocation failed");
    } catch (const std::exception& exception)
    {
        return fail(Core::CoreErrorCode::Internal, std::string_view(exception.what()));
    } catch (...)
    {
        return fail(Core::CoreErrorCode::Internal, "UI context allocation failed");
    }
}

UIContext::UIContext(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl))
{
}

UIContext::~UIContext() noexcept
{
    if (m_impl)
    {
        if (!m_impl->isOwnerThread() || m_impl->routeDispatchDepth != 0 ||
            m_impl->routedPointerListenerRegistry.operationInProgress() ||
            m_impl->buttonActionRegistry.operationInProgress() ||
            m_impl->sliderChangeCallbackRegistry.operationInProgress())
        {
            std::terminate();
        }
        m_impl->detachLifetime(this);
    }
}

Platform::WindowId UIContext::ownerWindow() const noexcept
{
    return m_impl->ownerWindow;
}

bool UIContext::contains(UINodeId node) const noexcept
{
    return m_impl->isOwnerThread() && m_impl->contains(node);
}

const UITheme& UIContext::productTheme() const noexcept
{
    return m_impl->productTheme;
}

Core::Status UIContext::setProductTheme(const UITheme& theme)
{
    return m_impl->setProductTheme(theme);
}

Core::Result<UIStyleClassId> UIContext::registerStyleClass()
{
    return m_impl->registerStyleClass();
}

Core::Result<UIStyleTokenId>
UIContext::registerStyleColorToken(UIStraightSrgba8Color value)
{
    return m_impl->registerStyleColorToken(value);
}

Core::Status UIContext::installStyleSheet(
    std::span<const UIStyleBoxFillRule> rules)
{
    return m_impl->installStyleSheet(rules);
}

Core::Status UIContext::openTextFont(std::span<const std::byte> fontBytes, i32 faceIndex)
{
    return m_impl->openTextFont(fontBytes, faceIndex);
}

UIRootBuilder UIContext::rootBuilder() noexcept
{
    return UIRootBuilder(*this);
}

Core::Result<UITreeUpdater> UIContext::treeUpdater(UIRootOwner& rootOwner)
{
    if (Core::Status ownerThread = m_impl->ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    m_impl->drainDeferredRootDestroys();
    if (!rootOwner.hasValue())
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a root owner");
    }
    if (rootOwner.rootNodeId().ownerWindow() != m_impl->ownerWindow)
    {
        return fail(UIErrorCode::WrongOwnerWindow, "UI root owner belongs to another owner window");
    }
    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = rootOwner.m_lifetime.lock();
    UIContext* attachedContext = lifetime ? lifetime->attachedContext() : nullptr;
    if (attachedContext == nullptr)
    {
        return fail(UIErrorCode::RootRequired, "UI root owner is detached");
    }
    if (attachedContext != this)
    {
        return fail(UIErrorCode::WrongContext, "UI root owner belongs to another context");
    }
    if (!m_impl->contains(rootOwner.rootNodeId()))
    {
        return fail(UIErrorCode::RootRequired, "UI root owner is no longer alive");
    }
    return UITreeUpdater(*this, rootOwner.rootNodeId());
}

Core::Status UIContext::commitStructure()
{
    return m_impl->commitStructure();
}

UICommittedStructureView UIContext::committedStructure() const noexcept
{
    return m_impl->committedStructure();
}

Core::Status UIContext::commitLayout(UILogicalSize viewportSize)
{
    return m_impl->commitLayout(viewportSize);
}

UICommittedLayoutView UIContext::committedLayout() const noexcept
{
    return m_impl->committedLayout();
}

UICommittedHitView UIContext::committedHit() const noexcept
{
    return m_impl->committedHit();
}

UICommittedPaintView UIContext::committedPaint() const noexcept
{
    return m_impl->committedPaint();
}

UICommittedSemanticsView UIContext::committedSemantics() const noexcept
{
    return m_impl->committedSemantics();
}

std::span<const u8> UIContext::glyphAtlasPixels() const noexcept
{
    return m_impl->glyphAtlasPixels();
}

u32 UIContext::glyphAtlasWidth() const noexcept
{
    return m_impl->glyphAtlasWidth();
}

u32 UIContext::glyphAtlasHeight() const noexcept
{
    return m_impl->glyphAtlasHeight();
}

UIPointerHitQueryResult UIContext::queryPointerHit(UILogicalPoint point) const noexcept
{
    return m_impl->queryPointerHit(point);
}

Core::Result<UIRoutedPointerListenerToken> UIContext::addRoutedPointerListener(UIRoutedPointerListenerDesc descriptor,
                                                                               UIRoutedPointerCallback callback)
{
    auto registration = m_impl->addRoutedPointerListener(descriptor, std::move(callback), {});
    if (!registration)
    {
        return Core::failure(registration.error());
    }
    return UIRoutedPointerListenerToken{
        m_impl->lifetime,
        registration->first,
        registration->second,
    };
}

Core::Result<UIPointerRouteResult> UIContext::routePointerInput(const UIPointerInputEvent& input)
{
    return m_impl->routePointerInput(input);
}

Core::Status UIContext::cancelPointerInteraction(Platform::WindowId routedWindow)
{
    return m_impl->cancelPointerInteraction(routedWindow);
}

Core::Status UIContext::cancelDefaultActionInteraction(Platform::WindowId routedWindow,
                                                       std::optional<Platform::GamepadId> gamepad)
{
    return m_impl->cancelDefaultActionInteraction(routedWindow, gamepad);
}

Core::Result<UIContext::UIDefaultActionResult>
UIContext::routeDefaultActionActivate(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                                      UIButtonActivationSource source,
                                      std::optional<Platform::DigitalControlIdentity> control)
{
    return m_impl->routeDefaultActionActivate(platformFrame, sourceSequence, source, std::move(control));
}

Core::Result<UIContext::UIDefaultActionResult>
UIContext::routeDefaultActionRelease(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                                     UIButtonActivationSource source, const Platform::DigitalControlIdentity& control)
{
    return m_impl->routeDefaultActionRelease(platformFrame, sourceSequence, source, control);
}

Core::Result<UIContext::UIDefaultFocusStepResult> UIContext::routeDefaultActionFocusStep(bool reverse)
{
    return m_impl->routeDefaultActionFocusStep(reverse);
}

Core::Result<UIContext::UIDefaultFocusStepResult>
UIContext::routeFocusNavigation(UIFocusNavigationDirection direction, bool pressed)
{
    return m_impl->routeFocusNavigation(direction, pressed);
}

Core::Result<UIRangeInputCommandResult>
UIContext::routeRangeInputCommand(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                                  UIRangeInputCommand command, bool pressed,
                                  const Platform::DigitalControlIdentity& control)
{
    return m_impl->routeRangeInputCommand(platformFrame, sourceSequence, command, pressed, control);
}

Core::Result<UIDropdownCommandResult> UIContext::routeDropdownCommand(UIDropdownCommand command, bool pressed)
{
    return m_impl->routeDropdownCommand(command, pressed);
}

Core::Result<UIListViewCommandResult> UIContext::routeListViewCommand(UIListViewCommand command, bool pressed)
{
    return m_impl->routeListViewCommand(command, pressed);
}

Core::Result<UITreeViewCommandResult> UIContext::routeTreeViewCommand(UITreeViewCommand command, bool pressed)
{
    return m_impl->routeTreeViewCommand(command, pressed);
}

UINodeId UIContext::defaultActionFocus() const noexcept
{
    if (!m_impl->isOwnerThread())
    {
        return {};
    }
    return m_impl->defaultActionFocus();
}

UINodeId UIContext::activeFocusScope() const noexcept
{
    if (!m_impl->isOwnerThread())
    {
        return {};
    }
    return m_impl->activeFocusScope();
}

UINodeId UIContext::activeModal() const noexcept
{
    if (!m_impl->isOwnerThread())
    {
        return {};
    }
    return m_impl->activeModal();
}

UINodeId UIContext::pointerCapture() const noexcept
{
    if (!m_impl->isOwnerThread())
    {
        return {};
    }
    return m_impl->pointerCapture();
}

UINodeId UIContext::activePopup() const noexcept
{
    if (!m_impl->isOwnerThread())
    {
        return {};
    }
    return m_impl->activePopup();
}

Core::Status UIContext::requestFocus(UINodeId node)
{
    return m_impl->requestFocus(node);
}

Core::Status UIContext::clearFocus()
{
    return m_impl->clearFocus();
}

Core::Status UIContext::performAccessibilityAction(const UIAccessibilityAction& action)
{
    return m_impl->performAccessibilityAction(action);
}

UINodeId UIContext::imeFocus() const noexcept
{
    if (!m_impl->isOwnerThread())
    {
        return {};
    }
    return m_impl->imeFocus();
}

bool UIContext::imeCompositionActive() const noexcept
{
    return m_impl->isOwnerThread() && m_impl->imeCompositionActive();
}

std::string_view UIContext::imePreeditUtf8() const noexcept
{
    if (!m_impl->isOwnerThread())
    {
        return {};
    }
    return m_impl->imePreeditUtf8();
}

u32 UIContext::imePreeditCursorCodepoint() const noexcept
{
    if (!m_impl->isOwnerThread())
    {
        return 0;
    }
    return m_impl->imePreeditCursorCodepoint();
}

Core::Result<UIContext::UITextInputRouteResult>
UIContext::routeTextComposition(Platform::WindowId window, Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                                std::string_view preeditUtf8, u32 cursorCodepoint, Platform::TextCompositionStage stage)
{
    return m_impl->routeTextComposition(window, platformFrame, sourceSequence, preeditUtf8, cursorCodepoint, stage);
}

Core::Result<UIContext::UITextInputRouteResult> UIContext::routeTextInput(Platform::WindowId window,
                                                                          Platform::PlatformFrameId platformFrame,
                                                                          u64 sourceSequence,
                                                                          std::string_view committedUtf8)
{
    return m_impl->routeTextInput(window, platformFrame, sourceSequence, committedUtf8);
}

Core::Result<UIContext::UITextInputRouteResult>
UIContext::routeTextEditCommand(Platform::WindowId window, Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                                UITextEditCommand command, bool extendSelection)
{
    return m_impl->routeTextEditCommand(window, platformFrame, sourceSequence, command, extendSelection);
}

UIContextStatistics UIContext::statistics() const noexcept
{
    return m_impl->statistics();
}

usize UIContext::liveNodeCount() const noexcept
{
    return m_impl->nodes.activeCount();
}

usize UIContext::liveRootCount() const noexcept
{
    return m_impl->liveRootCount;
}

Core::Result<UIRootOwner> UIContext::createRoot()
{
    return m_impl->createRoot(*this);
}

Core::Result<UINodeId> UIContext::createElement(UINodeId parent, const UIElementDescriptor& descriptor)
{
    return m_impl->createElement(parent, descriptor);
}

Core::Result<UINodeId> UIContext::createElementFromUpdater(UINodeId updaterRoot, UINodeId parent,
                                                           const UIElementDescriptor& descriptor)
{
    return m_impl->createElementFromUpdater(updaterRoot, parent, descriptor);
}

Core::Result<UIElementBuildTransaction>
UIContext::beginBuildTransactionFromUpdater(UINodeId updaterRoot, UINodeId parent,
                                            const UIElementDescriptor& rootDescriptor,
                                            UIComponentBuildBudget budget)
{
    return m_impl->beginBuildTransaction(*this, updaterRoot, parent, rootDescriptor, budget);
}

Core::Result<UINodeId>
UIContext::createElementFromBuildTransaction(UINodeId updaterRoot, UINodeId componentRoot, UINodeId parent,
                                             const UIElementDescriptor& descriptor,
                                             UIComponentBuildBudget& remainingBudget)
{
    return m_impl->createElementFromBuildTransaction(
        updaterRoot, componentRoot, parent, descriptor, remainingBudget);
}

Core::Status UIContext::commitBuildTransaction(UINodeId updaterRoot, UINodeId componentRoot,
                                               UIComponentBuildBudget& remainingBudget)
{
    return m_impl->commitBuildTransaction(updaterRoot, componentRoot, remainingBudget);
}

void UIContext::rollbackBuildTransaction(UINodeId updaterRoot, UINodeId componentRoot,
                                         UIComponentBuildBudget& remainingBudget) noexcept
{
    m_impl->rollbackBuildTransaction(updaterRoot, componentRoot, remainingBudget);
}

bool UIContext::isBuildTransactionActive(UINodeId componentRoot) const noexcept
{
    return m_impl->isBuildTransactionActive(componentRoot);
}

Core::Status UIContext::setLayoutStyleFromUpdater(UINodeId updaterRoot, UINodeId node, const UILayoutStyle& style)
{
    return m_impl->setLayoutStyleFromUpdater(updaterRoot, node, style);
}

Core::Status UIContext::setPointerHitPolicyFromUpdater(UINodeId updaterRoot, UINodeId node, UIPointerHitPolicy policy)
{
    return m_impl->setPointerHitPolicyFromUpdater(updaterRoot, node, policy);
}

Core::Status UIContext::setEnabledFromUpdater(UINodeId updaterRoot, UINodeId node, bool enabled)
{
    return m_impl->setEnabledFromUpdater(updaterRoot, node, enabled);
}

Core::Status UIContext::setFocusScopeModeFromUpdater(UINodeId updaterRoot, UINodeId node, UIFocusScopeMode mode)
{
    return m_impl->setFocusScopeModeFromUpdater(updaterRoot, node, mode);
}

Core::Result<UIFocusScopeMode> UIContext::focusScopeModeFromUpdater(UINodeId updaterRoot, UINodeId node) const
{
    return m_impl->focusScopeModeFromUpdater(updaterRoot, node);
}

Core::Status UIContext::requestFocusFromUpdater(UINodeId updaterRoot, UINodeId node)
{
    return m_impl->requestFocusFromUpdater(updaterRoot, node);
}

Core::Status UIContext::clearFocusFromUpdater(UINodeId updaterRoot)
{
    return m_impl->clearFocusFromUpdater(updaterRoot);
}

Core::Status UIContext::setStyleRoleFromUpdater(UINodeId updaterRoot, UINodeId node, UIStyleRoleId role)
{
    return m_impl->setStyleRoleFromUpdater(updaterRoot, node, role);
}

Core::Result<UIStyleRoleId> UIContext::styleRoleFromUpdater(UINodeId updaterRoot, UINodeId node) const
{
    return m_impl->styleRoleFromUpdater(updaterRoot, node);
}

Core::Status UIContext::clearOverrideFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                UIStyleOverride properties)
{
    return m_impl->clearOverrideFromUpdater(updaterRoot, node, properties);
}

Core::Result<bool> UIContext::isEnabledFromUpdater(UINodeId updaterRoot, UINodeId node) const
{
    return m_impl->isEnabledFromUpdater(updaterRoot, node);
}

Core::Status UIContext::setBoxPaintFromUpdater(UINodeId updaterRoot, UINodeId node, const UIBoxPaint& paint)
{
    return m_impl->setBoxPaintFromUpdater(updaterRoot, node, paint);
}

Core::Status UIContext::setButtonPaintFromUpdater(UINodeId updaterRoot, UINodeId button, const UIButtonPaint& paint)
{
    return m_impl->setButtonPaintFromUpdater(updaterRoot, button, paint);
}

Core::Result<UIButtonPaint> UIContext::buttonPaintFromUpdater(UINodeId updaterRoot, UINodeId button) const
{
    return m_impl->buttonPaintFromUpdater(updaterRoot, button);
}

Core::Status UIContext::setTextFromUpdater(UINodeId updaterRoot, UINodeId node, std::string_view utf8)
{
    return m_impl->setTextFromUpdater(updaterRoot, node, utf8);
}

Core::Status UIContext::setTextStyleFromUpdater(UINodeId updaterRoot, UINodeId node, const UITextStyle& style)
{
    return m_impl->setTextStyleFromUpdater(updaterRoot, node, style);
}

Core::Status UIContext::setContentAlignmentFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                       UIContentAlignment alignment)
{
    return m_impl->setContentAlignmentFromUpdater(updaterRoot, node, alignment);
}

Core::Result<std::string_view> UIContext::textFromUpdater(UINodeId updaterRoot, UINodeId node)
{
    return m_impl->textFromUpdater(updaterRoot, node);
}

Core::Result<UITextStyle> UIContext::textStyleFromUpdater(UINodeId updaterRoot, UINodeId node)
{
    return m_impl->textStyleFromUpdater(updaterRoot, node);
}

Core::Result<UIContentAlignment> UIContext::contentAlignmentFromUpdater(UINodeId updaterRoot,
                                                                        UINodeId node) const
{
    return m_impl->contentAlignmentFromUpdater(updaterRoot, node);
}

Core::Status UIContext::setTextSelectionFromUpdater(UINodeId updaterRoot, UINodeId textEdit, UITextSelection selection)
{
    return m_impl->setTextSelectionFromUpdater(updaterRoot, textEdit, selection);
}

Core::Result<UITextSelection> UIContext::textSelectionFromUpdater(UINodeId updaterRoot, UINodeId textEdit) const
{
    return m_impl->textSelectionFromUpdater(updaterRoot, textEdit);
}

Core::Status UIContext::setTextEditPaintFromUpdater(UINodeId updaterRoot, UINodeId textEdit,
                                                    const UITextEditPaint& paint)
{
    return m_impl->setTextEditPaintFromUpdater(updaterRoot, textEdit, paint);
}

Core::Result<UITextEditPaint> UIContext::textEditPaintFromUpdater(UINodeId updaterRoot, UINodeId textEdit) const
{
    return m_impl->textEditPaintFromUpdater(updaterRoot, textEdit);
}

Core::Status UIContext::setButtonActionFromUpdater(UINodeId updaterRoot, UINodeId button,
                                                   UIButtonActionCallback&& callback)
{
    return m_impl->setButtonActionFromUpdater(updaterRoot, button, std::move(callback));
}

Core::Status UIContext::clearButtonActionFromUpdater(UINodeId updaterRoot, UINodeId button)
{
    return m_impl->clearButtonActionFromUpdater(updaterRoot, button);
}

Core::Result<bool> UIContext::isButtonPressedFromUpdater(UINodeId updaterRoot, UINodeId button)
{
    return m_impl->isButtonPressedFromUpdater(updaterRoot, button);
}

Core::Status UIContext::setCheckboxActionFromUpdater(UINodeId updaterRoot, UINodeId checkbox,
                                                     UIButtonActionCallback&& callback)
{
    return m_impl->setCheckboxActionFromUpdater(updaterRoot, checkbox, std::move(callback));
}

Core::Status UIContext::clearCheckboxActionFromUpdater(UINodeId updaterRoot, UINodeId checkbox)
{
    return m_impl->clearCheckboxActionFromUpdater(updaterRoot, checkbox);
}

Core::Status UIContext::setCheckboxPaintFromUpdater(UINodeId updaterRoot, UINodeId checkbox,
                                                    const UICheckboxPaint& paint)
{
    return m_impl->setCheckboxPaintFromUpdater(updaterRoot, checkbox, paint);
}

Core::Result<UICheckboxPaint> UIContext::checkboxPaintFromUpdater(UINodeId updaterRoot, UINodeId checkbox) const
{
    return m_impl->checkboxPaintFromUpdater(updaterRoot, checkbox);
}

Core::Status UIContext::setCheckedFromUpdater(UINodeId updaterRoot, UINodeId checkbox, bool checked)
{
    return m_impl->setCheckedFromUpdater(updaterRoot, checkbox, checked);
}

Core::Result<bool> UIContext::isCheckedFromUpdater(UINodeId updaterRoot, UINodeId checkbox) const
{
    return m_impl->isCheckedFromUpdater(updaterRoot, checkbox);
}

Core::Result<bool> UIContext::isCheckboxPressedFromUpdater(UINodeId updaterRoot, UINodeId checkbox)
{
    return m_impl->isCheckboxPressedFromUpdater(updaterRoot, checkbox);
}

Core::Status UIContext::setSliderRangeFromUpdater(UINodeId updaterRoot, UINodeId slider, float minValue, float maxValue,
                                                  float step)
{
    return m_impl->setSliderRangeFromUpdater(updaterRoot, slider, minValue, maxValue, step);
}

Core::Status UIContext::setSliderValueFromUpdater(UINodeId updaterRoot, UINodeId slider, float value)
{
    return m_impl->setSliderValueFromUpdater(updaterRoot, slider, value);
}

Core::Result<float> UIContext::sliderValueFromUpdater(UINodeId updaterRoot, UINodeId slider) const
{
    return m_impl->sliderValueFromUpdater(updaterRoot, slider);
}

Core::Status UIContext::setSliderPaintFromUpdater(UINodeId updaterRoot, UINodeId slider, const UISliderPaint& paint)
{
    return m_impl->setSliderPaintFromUpdater(updaterRoot, slider, paint);
}

Core::Result<UISliderPaint> UIContext::sliderPaintFromUpdater(UINodeId updaterRoot, UINodeId slider) const
{
    return m_impl->sliderPaintFromUpdater(updaterRoot, slider);
}

Core::Status UIContext::setSliderChangeCallbackFromUpdater(UINodeId updaterRoot, UINodeId slider,
                                                           UISliderChangeCallback&& callback)
{
    return m_impl->setSliderChangeCallbackFromUpdater(updaterRoot, slider, std::move(callback));
}

Core::Status UIContext::clearSliderChangeCallbackFromUpdater(UINodeId updaterRoot, UINodeId slider)
{
    return m_impl->clearSliderChangeCallbackFromUpdater(updaterRoot, slider);
}

Core::Result<bool> UIContext::isSliderDraggingFromUpdater(UINodeId updaterRoot, UINodeId slider) const
{
    return m_impl->isSliderDraggingFromUpdater(updaterRoot, slider);
}

Core::Status UIContext::setScrollViewStyleFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                      const UIScrollViewStyle& style)
{
    return m_impl->setScrollViewStyleFromUpdater(updaterRoot, scrollView, style);
}

Core::Result<UIScrollViewStyle> UIContext::scrollViewStyleFromUpdater(UINodeId updaterRoot,
                                                                     UINodeId scrollView) const
{
    return m_impl->scrollViewStyleFromUpdater(updaterRoot, scrollView);
}

Core::Status UIContext::setScrollViewOffsetFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                       UIScrollOffset offset)
{
    return m_impl->setScrollViewOffsetFromUpdater(updaterRoot, scrollView, offset);
}

Core::Result<UIScrollOffset> UIContext::scrollViewOffsetFromUpdater(UINodeId updaterRoot,
                                                                   UINodeId scrollView) const
{
    return m_impl->scrollViewOffsetFromUpdater(updaterRoot, scrollView);
}

Core::Result<UIScrollViewMetrics> UIContext::scrollViewMetricsFromUpdater(UINodeId updaterRoot,
                                                                         UINodeId scrollView) const
{
    return m_impl->scrollViewMetricsFromUpdater(updaterRoot, scrollView);
}

Core::Status UIContext::setScrollViewPaintFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                      const UIScrollViewPaint& paint)
{
    return m_impl->setScrollViewPaintFromUpdater(updaterRoot, scrollView, paint);
}

Core::Result<UIScrollViewPaint> UIContext::scrollViewPaintFromUpdater(UINodeId updaterRoot,
                                                                     UINodeId scrollView) const
{
    return m_impl->scrollViewPaintFromUpdater(updaterRoot, scrollView);
}

Core::Result<bool> UIContext::isScrollViewDraggingFromUpdater(UINodeId updaterRoot,
                                                             UINodeId scrollView) const
{
    return m_impl->isScrollViewDraggingFromUpdater(updaterRoot, scrollView);
}

Core::Status UIContext::setPopupStyleFromUpdater(UINodeId updaterRoot, UINodeId popup,
                                                 const UIPopupStyle& style)
{
    return m_impl->setPopupStyleFromUpdater(updaterRoot, popup, style);
}

Core::Result<UIPopupStyle> UIContext::popupStyleFromUpdater(UINodeId updaterRoot, UINodeId popup) const
{
    return m_impl->popupStyleFromUpdater(updaterRoot, popup);
}

Core::Status UIContext::setPopupOpenFromUpdater(UINodeId updaterRoot, UINodeId popup, bool open)
{
    return m_impl->setPopupOpenFromUpdater(updaterRoot, popup, open);
}

Core::Result<bool> UIContext::isPopupOpenFromUpdater(UINodeId updaterRoot, UINodeId popup) const
{
    return m_impl->isPopupOpenFromUpdater(updaterRoot, popup);
}

Core::Result<UIPopupMetrics> UIContext::popupMetricsFromUpdater(UINodeId updaterRoot, UINodeId popup) const
{
    return m_impl->popupMetricsFromUpdater(updaterRoot, popup);
}

Core::Status UIContext::setDropdownOpenFromUpdater(UINodeId updaterRoot, UINodeId dropdown, bool open)
{
    return m_impl->setDropdownOpenFromUpdater(updaterRoot, dropdown, open);
}

Core::Result<bool> UIContext::isDropdownOpenFromUpdater(UINodeId updaterRoot, UINodeId dropdown) const
{
    return m_impl->isDropdownOpenFromUpdater(updaterRoot, dropdown);
}

Core::Status UIContext::setDropdownSelectedItemFromUpdater(UINodeId updaterRoot, UINodeId dropdown,
                                                           UINodeId item)
{
    return m_impl->setDropdownSelectedItemFromUpdater(updaterRoot, dropdown, item);
}

Core::Result<UINodeId> UIContext::dropdownSelectedItemFromUpdater(UINodeId updaterRoot,
                                                                  UINodeId dropdown) const
{
    return m_impl->dropdownSelectedItemFromUpdater(updaterRoot, dropdown);
}

Core::Result<bool> UIContext::isDropdownItemSelectedFromUpdater(UINodeId updaterRoot, UINodeId item) const
{
    return m_impl->isDropdownItemSelectedFromUpdater(updaterRoot, item);
}

Core::Status UIContext::setDropdownPaintFromUpdater(UINodeId updaterRoot, UINodeId dropdown,
                                                    const UIDropdownPaint& paint)
{
    return m_impl->setDropdownPaintFromUpdater(updaterRoot, dropdown, paint);
}

Core::Result<UIDropdownPaint> UIContext::dropdownPaintFromUpdater(UINodeId updaterRoot,
                                                                  UINodeId dropdown) const
{
    return m_impl->dropdownPaintFromUpdater(updaterRoot, dropdown);
}

Core::Status UIContext::setListViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                        UIListViewDataSource source)
{
    return m_impl->setListViewDataSourceFromUpdater(updaterRoot, listView, source);
}

Core::Status UIContext::clearListViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId listView)
{
    return m_impl->clearListViewDataSourceFromUpdater(updaterRoot, listView);
}

Core::Status UIContext::invalidateListViewItemsFromUpdater(UINodeId updaterRoot, UINodeId listView)
{
    return m_impl->invalidateListViewItemsFromUpdater(updaterRoot, listView);
}

Core::Status UIContext::setListViewStyleFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                   const UIListViewStyle& style)
{
    return m_impl->setListViewStyleFromUpdater(updaterRoot, listView, style);
}

Core::Result<UIListViewStyle> UIContext::listViewStyleFromUpdater(UINodeId updaterRoot,
                                                                 UINodeId listView) const
{
    return m_impl->listViewStyleFromUpdater(updaterRoot, listView);
}

Core::Status UIContext::setListViewPaintFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                   const UIListViewPaint& paint)
{
    return m_impl->setListViewPaintFromUpdater(updaterRoot, listView, paint);
}

Core::Result<UIListViewPaint> UIContext::listViewPaintFromUpdater(UINodeId updaterRoot,
                                                                 UINodeId listView) const
{
    return m_impl->listViewPaintFromUpdater(updaterRoot, listView);
}

Core::Result<UIListViewMetrics> UIContext::listViewMetricsFromUpdater(UINodeId updaterRoot,
                                                                     UINodeId listView) const
{
    return m_impl->listViewMetricsFromUpdater(updaterRoot, listView);
}

Core::Status UIContext::setListViewSelectedIndexFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                           u64 logicalIndex)
{
    return m_impl->setListViewSelectedIndexFromUpdater(updaterRoot, listView, logicalIndex);
}

Core::Status UIContext::clearListViewSelectionFromUpdater(UINodeId updaterRoot, UINodeId listView)
{
    return m_impl->clearListViewSelectionFromUpdater(updaterRoot, listView);
}

Core::Result<UIListViewSelection> UIContext::listViewSelectionFromUpdater(UINodeId updaterRoot,
                                                                         UINodeId listView) const
{
    return m_impl->listViewSelectionFromUpdater(updaterRoot, listView);
}

Core::Status UIContext::scrollListViewToIndexFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                        u64 logicalIndex, UIListViewScrollAlignment alignment)
{
    return m_impl->scrollListViewToIndexFromUpdater(updaterRoot, listView, logicalIndex, alignment);
}

Core::Status UIContext::setTreeViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                         UITreeViewDataSource source)
{
    return m_impl->setTreeViewDataSourceFromUpdater(updaterRoot, treeView, source);
}

Core::Status UIContext::clearTreeViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId treeView)
{
    return m_impl->clearTreeViewDataSourceFromUpdater(updaterRoot, treeView);
}

Core::Status UIContext::invalidateTreeViewItemsFromUpdater(UINodeId updaterRoot, UINodeId treeView)
{
    return m_impl->invalidateTreeViewItemsFromUpdater(updaterRoot, treeView);
}

Core::Status UIContext::setTreeViewStyleFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                    const UITreeViewStyle& style)
{
    return m_impl->setTreeViewStyleFromUpdater(updaterRoot, treeView, style);
}

Core::Result<UITreeViewStyle> UIContext::treeViewStyleFromUpdater(UINodeId updaterRoot, UINodeId treeView) const
{
    return m_impl->treeViewStyleFromUpdater(updaterRoot, treeView);
}

Core::Status UIContext::setTreeViewPaintFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                    const UITreeViewPaint& paint)
{
    return m_impl->setTreeViewPaintFromUpdater(updaterRoot, treeView, paint);
}

Core::Result<UITreeViewPaint> UIContext::treeViewPaintFromUpdater(UINodeId updaterRoot, UINodeId treeView) const
{
    return m_impl->treeViewPaintFromUpdater(updaterRoot, treeView);
}

Core::Result<UITreeViewMetrics> UIContext::treeViewMetricsFromUpdater(UINodeId updaterRoot, UINodeId treeView) const
{
    return m_impl->treeViewMetricsFromUpdater(updaterRoot, treeView);
}

Core::Status UIContext::setTreeViewSelectedIndexFromUpdater(UINodeId updaterRoot, UINodeId treeView, u64 logicalIndex)
{
    return m_impl->setTreeViewSelectedIndexFromUpdater(updaterRoot, treeView, logicalIndex);
}

Core::Status UIContext::clearTreeViewSelectionFromUpdater(UINodeId updaterRoot, UINodeId treeView)
{
    return m_impl->clearTreeViewSelectionFromUpdater(updaterRoot, treeView);
}

Core::Result<UITreeViewSelection> UIContext::treeViewSelectionFromUpdater(UINodeId updaterRoot, UINodeId treeView) const
{
    return m_impl->treeViewSelectionFromUpdater(updaterRoot, treeView);
}

Core::Status UIContext::setTreeViewItemExpandedFromUpdater(UINodeId updaterRoot, UINodeId treeView, u64 logicalIndex,
                                                           bool expanded)
{
    return m_impl->setTreeViewItemExpandedFromUpdater(updaterRoot, treeView, logicalIndex, expanded);
}

Core::Status UIContext::scrollTreeViewToIndexFromUpdater(UINodeId updaterRoot, UINodeId treeView, u64 logicalIndex,
                                                         UITreeViewScrollAlignment alignment)
{
    return m_impl->scrollTreeViewToIndexFromUpdater(updaterRoot, treeView, logicalIndex, alignment);
}

Core::Status UIContext::setProgressBarRangeFromUpdater(UINodeId updaterRoot, UINodeId progressBar, float minValue,
                                                       float maxValue)
{
    return m_impl->setProgressBarRangeFromUpdater(updaterRoot, progressBar, minValue, maxValue);
}

Core::Status UIContext::setProgressBarValueFromUpdater(UINodeId updaterRoot, UINodeId progressBar, float value)
{
    return m_impl->setProgressBarValueFromUpdater(updaterRoot, progressBar, value);
}

Core::Result<float> UIContext::progressBarValueFromUpdater(UINodeId updaterRoot, UINodeId progressBar) const
{
    return m_impl->progressBarValueFromUpdater(updaterRoot, progressBar);
}

Core::Status UIContext::setProgressBarPaintFromUpdater(UINodeId updaterRoot, UINodeId progressBar,
                                                       const UIProgressBarPaint& paint)
{
    return m_impl->setProgressBarPaintFromUpdater(updaterRoot, progressBar, paint);
}

Core::Result<UIProgressBarPaint> UIContext::progressBarPaintFromUpdater(UINodeId updaterRoot,
                                                                        UINodeId progressBar) const
{
    return m_impl->progressBarPaintFromUpdater(updaterRoot, progressBar);
}

Core::Status UIContext::setRadioButtonPaintFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                       const UIRadioButtonPaint& paint)
{
    return m_impl->setRadioButtonPaintFromUpdater(updaterRoot, radioButton, paint);
}

Core::Result<UIRadioButtonPaint> UIContext::radioButtonPaintFromUpdater(UINodeId updaterRoot,
                                                                        UINodeId radioButton) const
{
    return m_impl->radioButtonPaintFromUpdater(updaterRoot, radioButton);
}

Core::Status UIContext::setRadioButtonActionFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                        UIButtonActionCallback&& callback)
{
    return m_impl->setRadioButtonActionFromUpdater(updaterRoot, radioButton, std::move(callback));
}

Core::Status UIContext::clearRadioButtonActionFromUpdater(UINodeId updaterRoot, UINodeId radioButton)
{
    return m_impl->clearRadioButtonActionFromUpdater(updaterRoot, radioButton);
}

Core::Status UIContext::setRadioButtonSelectedFromUpdater(UINodeId updaterRoot, UINodeId radioButton, bool selected)
{
    return m_impl->setRadioButtonSelectedFromUpdater(updaterRoot, radioButton, selected);
}

Core::Result<bool> UIContext::isRadioButtonSelectedFromUpdater(UINodeId updaterRoot, UINodeId radioButton) const
{
    return m_impl->isRadioButtonSelectedFromUpdater(updaterRoot, radioButton);
}

Core::Result<bool> UIContext::isRadioButtonPressedFromUpdater(UINodeId updaterRoot, UINodeId radioButton) const
{
    return m_impl->isRadioButtonPressedFromUpdater(updaterRoot, radioButton);
}

Core::Result<UIRoutedPointerListenerToken>
UIContext::addRoutedPointerListenerFromUpdater(UINodeId updaterRoot, UIRoutedPointerListenerDesc descriptor,
                                               UIRoutedPointerCallback&& callback)
{
    auto registration = m_impl->addRoutedPointerListenerFromUpdater(updaterRoot, descriptor, std::move(callback));
    if (!registration)
    {
        return Core::failure(registration.error());
    }
    return UIRoutedPointerListenerToken{
        m_impl->lifetime,
        registration->first,
        registration->second,
    };
}

Core::Status UIContext::destroyNodeFromUpdater(UINodeId updaterRoot, UINodeId node)
{
    return m_impl->destroyFromUpdater(updaterRoot, node);
}

void UIContext::destroyRootFromOwner(UINodeId root) noexcept
{
    if (!m_impl->isOwnerThread())
    {
        return;
    }
    m_impl->drainDeferredRootDestroys();
    m_impl->destroyRootImmediately(root);
}

bool UIContext::isAliveInRoot(UINodeId updaterRoot, UINodeId node) const noexcept
{
    if (!m_impl->isOwnerThread() || !updaterRoot.hasValue())
    {
        return false;
    }
    return m_impl->isNodeWithinRoot(updaterRoot, node);
}

void UIContext::releaseRoutedPointerListenerFromToken(u32 slot, u32 generation) noexcept
{
    if (m_impl && m_impl->isOwnerThread())
    {
        m_impl->deactivateRoutedPointerListener(slot, generation, false);
    }
}

} // namespace Tina::UI
