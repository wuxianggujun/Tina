#pragma once

#include <tina/ui/UIAuthoring.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UIInputRouter.hpp>
#include <tina/ui/UIMotionController.hpp>
#include <tina/ui/UIPublicationPipeline.hpp>
#include <tina/ui/UIStyleController.hpp>
#include <tina/ui/UITextSystem.hpp>

#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/id/GenerationPool.hpp>
#include <tina/core/memory/CountingMemoryResource.hpp>
#include <tina/core/memory/MemoryTracker.hpp>
#include <tina/core/text/Utf8.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/ui/UIDirty.hpp>
#include <tina/ui/UIMotion.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/UITheme.hpp>
#include <tina/ui/text/UIGlyphAtlas.hpp>
#include <tina/ui/text/UITextRasterizer.hpp>

#include "UIButtonActionRegistry.hpp"
#include "UIBehaviorStateStorage.hpp"
#include "UICanvasCommandStorage.hpp"
#include "UICommandPressLatch.hpp"
#include "UIControlGeometry.hpp"
#include "UIControlPaintEmitter.hpp"
#include "UIControlValuePrimitives.hpp"
#include "UIContextLifetimeControl.hpp"
#include "UIDataGridLayout.hpp"
#include "UIDataGridStateStorage.hpp"
#include "UIDialogStateStorage.hpp"
#include "UIElementContractResolver.hpp"
#include "UIFlexLayout.hpp"
#include "UIGridLayout.hpp"
#include "UIFocusNavigation.hpp"
#include "UIGridCommandNavigation.hpp"
#include "UIGridScrollBarGeometry.hpp"
#include "UIGraphemeBreak.hpp"
#include "UIDefaultActionPressState.hpp"
#include "UIDirtyQueueStorage.hpp"
#include "UIImeCompositionState.hpp"
#include "UIImageContentStorage.hpp"
#include "UIMotionTrackStorage.hpp"
#include "UIKeyframeTimelineStorage.hpp"
#include "UIMenuInput.hpp"
#include "UIMenuLayout.hpp"
#include "UIMenuStateStorage.hpp"
#include "UINineSlicePaintEmitter.hpp"
#include "UIInputPrimitives.hpp"
#include "UILayoutMeasurement.hpp"
#include "UILayoutPrimitives.hpp"
#include "UIPaintPrimitives.hpp"
#include "UIPaintSnapshotBuilder.hpp"
#include "UIPointerRouteInspection.hpp"
#include "UIPointerRoutePath.hpp"
#include "UIPropertyNormalization.hpp"
#include "UIRangeInputPressLatch.hpp"
#include "UIRoutedPointerListenerRegistry.hpp"
#include "UIScrollViewLayout.hpp"
#include "UISemanticsSnapshotBuilder.hpp"
#include "UISliderChangeCallbackRegistry.hpp"
#include "UISplitViewInput.hpp"
#include "UISplitViewLayout.hpp"
#include "UISplitViewStateStorage.hpp"
#include "UITabViewInput.hpp"
#include "UITabViewLayout.hpp"
#include "UITabViewStateStorage.hpp"
#include "UIStyleRoleResolver.hpp"
#include "UIStyleSheetStorage.hpp"
#include "UIThemeTransitionResolver.hpp"
#include "UISwitchGeometry.hpp"
#include "UITooltipLayout.hpp"
#include "UITooltipStateStorage.hpp"
#include "UIVirtualCollectionLayout.hpp"
#include "UIVirtualGridLayout.hpp"
#include "UIVirtualGridViewStateStorage.hpp"
#include "UITextEditModel.hpp"
#include "UITextEditPaintEmitter.hpp"
#include "UITextStorage.hpp"
#include "UIWidgetStateModels.hpp"
#include "UIWidgetTraits.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <exception>
#include <expected>
#include <initializer_list>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Tina::UI::Detail::UIContextPrivate {

using NodeStorageId = Core::GenerationId<::Tina::UI::Detail::UINodeRegistryTag>;

// UIContext uses fixed PMR storage, but the upstream resource is normally the
// process default heap. Keep the existing Core memory ledger beside the Context
// so a profile can distinguish retained UI bytes from process-level memory.
class UIAllocationLedger final {
  public:
    explicit UIAllocationLedger(std::pmr::memory_resource& upstream) noexcept
        : resource_(tracker_, Core::MemoryTag::UI, upstream)
    {
    }

    [[nodiscard]] std::pmr::memory_resource& resource() noexcept
    {
        return resource_;
    }

    [[nodiscard]] Core::MemoryStatistics statistics() const noexcept
    {
        return tracker_.snapshot(Core::MemoryTag::UI);
    }

  private:
    Core::MemoryTracker tracker_{};
    Core::CountingMemoryResource resource_;
};

[[nodiscard]] inline usize allocationIncrease(usize before, usize after) noexcept
{
    return after >= before ? after - before : 0U;
}

inline constexpr u32 InvalidNodeIndex = NodeStorageId::InvalidIndex;

using TextByteAllocation = ::Tina::UI::Detail::UITextStorage::Allocation;
using ::Tina::UI::Detail::appendBoxChromePaints;
using ::Tina::UI::Detail::appendFlexLineItem;
using ::Tina::UI::Detail::appendFlexMeasuredItem;
using ::Tina::UI::Detail::appendFlowMeasuredChild;
using ::Tina::UI::Detail::appendGridMeasuredChild;
using ::Tina::UI::Detail::beginGridMeasurement;
using ::Tina::UI::Detail::applyOpacity;
using ::Tina::UI::Detail::buildPointerRoutePath;
using ::Tina::UI::Detail::BuiltinElementKind;
using ::Tina::UI::Detail::clampHeight;
using ::Tina::UI::Detail::clampWidth;
using ::Tina::UI::Detail::combineVisibility;
using ::Tina::UI::Detail::containsLineBreak;
using ::Tina::UI::Detail::containsPointHalfOpen;
using ::Tina::UI::Detail::countBoxChromePaintEntries;
using ::Tina::UI::Detail::defaultBehaviorsForKind;
using ::Tina::UI::Detail::defaultContentAlignment;
using ::Tina::UI::Detail::defaultSemanticsForKind;
using ::Tina::UI::Detail::defaultStyleRoleForKind;
using ::Tina::UI::Detail::DropdownState;
using ::Tina::UI::Detail::DataGridCellState;
using ::Tina::UI::Detail::DataGridColumnState;
using ::Tina::UI::Detail::DataGridLayoutScratch;
using ::Tina::UI::Detail::DataGridRowState;
using ::Tina::UI::Detail::DataGridState;
using ::Tina::UI::Detail::FlexLineSummary;
using ::Tina::UI::Detail::FlexLinePlan;
using ::Tina::UI::Detail::FlexWrapMeasurement;
using ::Tina::UI::Detail::finishFlexMeasurement;
using ::Tina::UI::Detail::GridMeasurement;
using ::Tina::UI::Detail::gridAreaRect;
using ::Tina::UI::Detail::hasLayoutWork;
using ::Tina::UI::Detail::gridMeasuredContentSize;
using ::Tina::UI::Detail::horizontalMargin;
using ::Tina::UI::Detail::intersectRects;
using ::Tina::UI::Detail::inspectPointerRouteTargets;
using ::Tina::UI::Detail::isButtonChromeKind;
using ::Tina::UI::Detail::isFiniteLayoutRect;
using ::Tina::UI::Detail::isFiniteNonNegative;
using ::Tina::UI::Detail::isValidFlexLineSummary;
using ::Tina::UI::Detail::isValidFlexWrapMeasurement;
using ::Tina::UI::Detail::isValidGridMeasurement;
using ::Tina::UI::Detail::isValidContentAlignment;
using ::Tina::UI::Detail::isValidEventPhaseMask;
using ::Tina::UI::Detail::isValidFocusScopeMode;
using ::Tina::UI::Detail::isValidPointerHitPolicy;
using ::Tina::UI::Detail::isValidRoutedPointerEventKind;
using ::Tina::UI::Detail::layoutSubtreeCompletionMask;
using ::Tina::UI::Detail::LayoutFlowMeasurement;
using ::Tina::UI::Detail::LayoutNodeMeasureContent;
using ::Tina::UI::Detail::LayoutPassStatistics;
using ::Tina::UI::Detail::LayoutPreparedInputs;
using ::Tina::UI::Detail::LayoutScratchState;
using ::Tina::UI::Detail::LayoutWorkArrange;
using ::Tina::UI::Detail::LayoutWorkArrangeComplete;
using ::Tina::UI::Detail::LayoutWorkMeasure;
using ::Tina::UI::Detail::LayoutWorkMeasureComplete;
using ::Tina::UI::Detail::ListViewItemState;
using ::Tina::UI::Detail::ListViewLayoutScratch;
using ::Tina::UI::Detail::ListViewState;
using ::Tina::UI::Detail::MenuItemState;
using ::Tina::UI::Detail::MenuState;
using ::Tina::UI::Detail::makeListViewScrollBarGeometry;
using ::Tina::UI::Detail::makeDataGridScrollBarGeometry;
using ::Tina::UI::Detail::makeDataGridScrollMetrics;
using ::Tina::UI::Detail::makeScrollBarGeometry;
using ::Tina::UI::Detail::makeTreeViewDisclosureRect;
using ::Tina::UI::Detail::makeTreeViewScrollBarGeometry;
using ::Tina::UI::Detail::makeVirtualGridViewScrollBarGeometry;
using ::Tina::UI::Detail::normalizedRangeFraction;
using ::Tina::UI::Detail::normalizeBoxPaint;
using ::Tina::UI::Detail::normalizeDropdownPaint;
using ::Tina::UI::Detail::normalizeFloat;
using ::Tina::UI::Detail::normalizeLayoutStyle;
using ::Tina::UI::Detail::normalizeImageContent;
using ::Tina::UI::Detail::normalizeListViewCreateConfig;
using ::Tina::UI::Detail::normalizeListViewPaint;
using ::Tina::UI::Detail::normalizeListViewStyle;
using ::Tina::UI::Detail::normalizePopupStyle;
using ::Tina::UI::Detail::normalizeScrollOffset;
using ::Tina::UI::Detail::normalizeScrollViewPaint;
using ::Tina::UI::Detail::normalizeScrollViewStyle;
using ::Tina::UI::Detail::normalizeTreeViewCreateConfig;
using ::Tina::UI::Detail::normalizeTreeViewPaint;
using ::Tina::UI::Detail::normalizeTreeViewStyle;
using ::Tina::UI::Detail::normalizeVirtualGridViewCreateConfig;
using ::Tina::UI::Detail::normalizeVirtualGridViewPaint;
using ::Tina::UI::Detail::normalizeVirtualGridViewStyle;
using ::Tina::UI::Detail::normalizeDataGridCreateConfig;
using ::Tina::UI::Detail::normalizeDataGridPaint;
using ::Tina::UI::Detail::normalizeDataGridStyle;
using ::Tina::UI::Detail::resolveDataGridCommandNavigation;
using ::Tina::UI::Detail::resolveVirtualGridViewCommandNavigation;
using ::Tina::UI::Detail::NormalizedUIContextCapacityConfig;
using ::Tina::UI::Detail::makeNineSlicePatches;
using ::Tina::UI::Detail::planTextEditCommand;
using ::Tina::UI::Detail::quantizeSliderValue;
using ::Tina::UI::Detail::ResolvedLength;
using ::Tina::UI::Detail::resolveInset;
using ::Tina::UI::Detail::resolveContentPlacement;
using ::Tina::UI::Detail::resolveElementBuiltinKind;
using ::Tina::UI::Detail::resolveFlexItemRect;
using ::Tina::UI::Detail::resolveResponsiveLayoutStyle;
using ::Tina::UI::Detail::resolveFlexLinePlan;
using ::Tina::UI::Detail::resolveGridItemRectInArea;
using ::Tina::UI::Detail::resolveGridLayout;
using ::Tina::UI::Detail::resolveGridArea;
using ::Tina::UI::Detail::resolveLength;
using ::Tina::UI::Detail::resolveLengthNoFallbackCount;
using ::Tina::UI::Detail::resolveMeasuredLayoutSize;
using ::Tina::UI::Detail::resolveOverlayRect;
using ::Tina::UI::Detail::resolvePopupPlacement;
using ::Tina::UI::Detail::resolveMenuPlacement;
using ::Tina::UI::Detail::resolveTooltipPlacement;
using ::Tina::UI::Detail::resolveCommittedLineGeometry;
using ::Tina::UI::Detail::resolveScrollViewLayout;
using ::Tina::UI::Detail::resolveScrollThumbOffset;
using ::Tina::UI::Detail::resolveScrollTrackPageOffset;
using ::Tina::UI::Detail::resolveScrollWheelOffset;
using ::Tina::UI::Detail::resolveSliderValueFromPointer;
using ::Tina::UI::Detail::resolveVirtualCollectionLayout;
using ::Tina::UI::Detail::resolveVirtualGridItemRect;
using ::Tina::UI::Detail::resolveVirtualGridLayout;
using ::Tina::UI::Detail::resolveVirtualRowScrollOffset;
using ::Tina::UI::Detail::resolveVirtualScrollWheelOffset;
using ::Tina::UI::Detail::resolvedHeight;
using ::Tina::UI::Detail::resolvedWidth;
using ::Tina::UI::Detail::scrollAxisMaxOffset;
using ::Tina::UI::Detail::scrollAxisOffset;
using ::Tina::UI::Detail::ScrollBarGeometry;
using ::Tina::UI::Detail::ScrollBarPointerHit;
using ::Tina::UI::Detail::ScrollViewLayoutScratch;
using ::Tina::UI::Detail::ScrollViewLayoutInput;
using ::Tina::UI::Detail::resolveSplitViewFractionFromPointer;
using ::Tina::UI::Detail::resolveSplitViewLayout;
using ::Tina::UI::Detail::splitterGrabOffset;
using ::Tina::UI::Detail::SplitterState;
using ::Tina::UI::Detail::SplitViewState;
using ::Tina::UI::Detail::UIScrollBehaviorState;
using ::Tina::UI::Detail::SliderPaintGeometry;
using ::Tina::UI::Detail::sliderPaintGeometry;
using ::Tina::UI::Detail::setScrollAxisOffset;
using ::Tina::UI::Detail::textEditCodepointFromHorizontalPosition;
using ::Tina::UI::Detail::TreeViewItemState;
using ::Tina::UI::Detail::TreeViewLayoutScratch;
using ::Tina::UI::Detail::TreeViewState;
using ::Tina::UI::Detail::VirtualGridLayoutError;
using ::Tina::UI::Detail::VirtualGridViewItemState;
using ::Tina::UI::Detail::VirtualGridViewLayoutScratch;
using ::Tina::UI::Detail::VirtualGridViewState;
using ::Tina::UI::Detail::utf8ByteOffsetForCodepoint;
using ::Tina::UI::Detail::validateSemanticsContract;
using ::Tina::UI::Detail::verticalMargin;
using ::Tina::UI::Detail::VirtualCollectionLayoutError;
using ::Tina::UI::Detail::UITextEditCommandPlan;
using ::Tina::UI::Detail::WidgetTextState;
using ::Tina::UI::Detail::findHitEntryIndex;
using ::Tina::UI::Detail::findFocusNavigationCandidate;
using ::Tina::UI::Detail::hitEntryAllowedByModal;
using ::Tina::UI::Detail::hitEntryIsWithinScope;
using ::Tina::UI::Detail::isValidFocusNavigationDirection;
using ::Tina::UI::Detail::phaseMaskFor;
using ::Tina::UI::Detail::pointerHitTargetForEntry;
using ::Tina::UI::Detail::PopupLayoutScratch;
using ::Tina::UI::Detail::PopupState;
using ::Tina::UI::Detail::TooltipLayoutScratch;
using ::Tina::UI::Detail::TooltipState;
using ::Tina::UI::Detail::UISplitViewStateStorage;
using ::Tina::UI::Detail::UITabViewStateStorage;
using ::Tina::UI::Detail::UIMenuStateStorage;
using ::Tina::UI::Detail::UITabViewRegions;
using ::Tina::UI::Detail::TabViewState;
using ::Tina::UI::Detail::TabState;
using ::Tina::UI::Detail::isValidTabViewCommand;
using ::Tina::UI::Detail::isValidMenuCommand;
using ::Tina::UI::Detail::isValidMenuInvocationCommand;
using ::Tina::UI::Detail::resolveTabViewRegions;
using ::Tina::UI::Detail::ProgressBarState;
using ::Tina::UI::Detail::RadioButtonState;
using ::Tina::UI::Detail::supportsWidgetText;
using ::Tina::UI::Detail::defaultThemeBindingsFor;
using ::Tina::UI::Detail::isValidStyleRole;
using ::Tina::UI::Detail::ProductChromeStorage;
using ::Tina::UI::Detail::ThemeBindingBoxPaint;
using ::Tina::UI::Detail::ThemeBindingButtonPaint;
using ::Tina::UI::Detail::ThemeBindingCheckboxPaint;
using ::Tina::UI::Detail::ThemeBindingDropdownPaint;
using ::Tina::UI::Detail::ThemeBindingGridPaint;
using ::Tina::UI::Detail::ThemeBindingImageTint;
using ::Tina::UI::Detail::ThemeBindingListViewPaint;
using ::Tina::UI::Detail::ThemeBindingProgressBarPaint;
using ::Tina::UI::Detail::ThemeBindingRadioButtonPaint;
using ::Tina::UI::Detail::ThemeBindingScrollViewPaint;
using ::Tina::UI::Detail::ThemeBindingSliderPaint;
using ::Tina::UI::Detail::ThemeBindingSplitterPaint;
using ::Tina::UI::Detail::ThemeBindingTextEditPaint;
using ::Tina::UI::Detail::ThemeBindingTabPaint;
using ::Tina::UI::Detail::ThemeBindingTextStyle;
using ::Tina::UI::Detail::ThemeBindingTreeViewPaint;
using ::Tina::UI::Detail::UIPointerRouteInspectionError;
using ::Tina::UI::Detail::UIPointerRoutePathError;
using ::Tina::UI::Detail::UINineSlicePatch;
using ::Tina::UI::Detail::UINineSlicePatchBatch;

inline constexpr u8 ThemeDirtyPaint = 1U << 0U;
inline constexpr u8 ThemeDirtyLayoutSelf = 1U << 1U;
inline constexpr u8 ThemeDirtyLayoutAncestor = 1U << 2U;
inline constexpr float CollectionRowHorizontalPadding = 8.0F;
inline constexpr float CollectionRowTextFitTolerance = 0.001F;
inline constexpr float VirtualGridPreviewMaxExtent = 48.0F;
inline constexpr float VirtualGridListPreviewExtent = 40.0F;
inline constexpr float VirtualGridListItemHeightThreshold = 72.0F;
inline constexpr float VirtualGridPreviewGap = 4.0F;

inline void configureCollectionRowLayout(UILayoutStyle& layout, float rowHeight,
                                  float leftPadding = CollectionRowHorizontalPadding) noexcept
{
    layout.size.height = UILayoutLength::Px(rowHeight);
    layout.padding = UIEdgeSpacing{
        .left = leftPadding,
        .top = 0.0F,
        .right = CollectionRowHorizontalPadding,
        .bottom = 0.0F,
    };
}

[[nodiscard]] constexpr bool ownsDirectionalNavigation(BuiltinElementKind kind) noexcept
{
    return kind == BuiltinElementKind::TextEdit || kind == BuiltinElementKind::Dropdown ||
           kind == BuiltinElementKind::ListView || kind == BuiltinElementKind::TreeView ||
           kind == BuiltinElementKind::VirtualGridView ||
           kind == BuiltinElementKind::DataGrid ||
           kind == BuiltinElementKind::Splitter;
}

[[nodiscard]] constexpr bool isCompositeFocusItem(BuiltinElementKind kind) noexcept
{
    return kind == BuiltinElementKind::DropdownItem || kind == BuiltinElementKind::ListViewItem ||
           kind == BuiltinElementKind::TreeViewItem ||
           kind == BuiltinElementKind::VirtualGridViewItem ||
           kind == BuiltinElementKind::DataGridCell;
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
    UISemanticsLiveSetting liveSetting = UISemanticsLiveSetting::Off;
    bool hasExplicitName = false;
    bool hasExplicitDescription = false;
    bool useContentAsName = false;
    bool readOnly = false;
};

static_assert(sizeof(NodeRecord) <= 48);
static_assert(std::is_nothrow_destructible_v<NodeRecord>);

using NodePool = Core::GenerationPool<NodeRecord, ::Tina::UI::Detail::UINodeRegistryTag>;

struct CommittedHitBuildResult final {
    usize targetCount = 0;
    u32 activeModalEntryIndex = InvalidUIHitEntryIndex;
};

[[nodiscard]] inline Core::Error makeError(Core::ErrorCode code, std::string_view message,
                                    Core::SourceLocation location = Core::SourceLocation::current())
{
    return Core::Error{code, message, location};
}

[[nodiscard]] inline std::unexpected<Core::Error> fail(Core::ErrorCode code, std::string_view message,
                                                Core::SourceLocation location = Core::SourceLocation::current())
{
    return Core::failure(makeError(code, message, location));
}

[[nodiscard]] constexpr ::Tina::UI::Detail::UIBehaviorStateSlotCounts
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
toBehaviorSlotBudget(::Tina::UI::Detail::UIBehaviorStateSlotCounts counts) noexcept
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

struct UIComponentBuildReservation final {
    UINodeId componentRoot{};
    UIComponentBuildBudget remaining{};
    ::Tina::UI::Detail::UITextStorage::Reservation text{};
    ::Tina::UI::Detail::UICanvasCommandStorage::Reservation canvas{};
    ::Tina::UI::Detail::UIBehaviorStateSlotCounts behaviors{};
    bool active = false;
};

enum class UIFlowNodeKind : u8 {
    None = 0,
    Layer,
    Screen,
};

inline constexpr usize FlowActionSlotCount = 3;

[[nodiscard]] constexpr usize flowActionSlotIndex(UIFlowAction action) noexcept
{
    switch (action)
    {
    case UIFlowAction::Back:
        return 0;
    case UIFlowAction::Confirm:
        return 1;
    case UIFlowAction::Menu:
        return 2;
    }
    return FlowActionSlotCount;
}

class UIFlowActionPressState final {
  public:
    explicit UIFlowActionPressState(Platform::WindowId ownerWindow) noexcept
        : ownerWindow_(ownerWindow)
    {
    }

    [[nodiscard]] Core::Status validate(UIFlowAction action, UIFlowActionSource source,
                                        const Platform::DigitalControlIdentity& control) const
    {
        const usize actionIndex = flowActionSlotIndex(action);
        if (actionIndex >= FlowActionSlotCount)
        {
            return Core::failure(UIErrorCode::InvalidFlowAction,
                                 "UI Flow action is not supported by this router");
        }
        if (source == UIFlowActionSource::Keyboard)
        {
            const auto* key = std::get_if<Platform::KeyControlIdentity>(&control);
            if (key != nullptr && key->window == ownerWindow_ &&
                keyboardControlIndex(action, key->key).has_value())
            {
                return Core::success();
            }
        }
        else if (source == UIFlowActionSource::Gamepad)
        {
            const auto* button =
                std::get_if<Platform::GamepadButtonControlIdentity>(&control);
            if (button != nullptr && button->routedWindow == ownerWindow_ &&
                button->gamepad.hasValue() &&
                button->gamepad.index() < gamepadPresses_[actionIndex].size() &&
                matchesGamepadControl(action, button->button))
            {
                return Core::success();
            }
        }
        return Core::failure(UIErrorCode::InvalidFlowAction,
                             "UI Flow action control does not match its source");
    }

    [[nodiscard]] bool isPressed(UIFlowAction action,
                                 const Platform::DigitalControlIdentity& control) const noexcept
    {
        if (const auto* key = std::get_if<Platform::KeyControlIdentity>(&control);
            key != nullptr)
        {
            const auto controlIndex = keyboardControlIndex(action, key->key);
            return key->window == ownerWindow_ && controlIndex.has_value() &&
                   keyboardPresses_[*controlIndex];
        }
        const auto* button =
            std::get_if<Platform::GamepadButtonControlIdentity>(&control);
        const usize actionIndex = flowActionSlotIndex(action);
        return button != nullptr && button->routedWindow == ownerWindow_ &&
               actionIndex < FlowActionSlotCount &&
               matchesGamepadControl(action, button->button) &&
               button->gamepad.hasValue() &&
               button->gamepad.index() < gamepadPresses_[actionIndex].size() &&
               gamepadPresses_[actionIndex][button->gamepad.index()] == button->gamepad;
    }

    void setPressed(UIFlowAction action,
                    const Platform::DigitalControlIdentity& control) noexcept
    {
        if (const auto* key = std::get_if<Platform::KeyControlIdentity>(&control);
            key != nullptr)
        {
            const auto controlIndex = keyboardControlIndex(action, key->key);
            if (key->window == ownerWindow_ && controlIndex.has_value())
            {
                keyboardPresses_[*controlIndex] = true;
            }
            return;
        }
        const auto* button =
            std::get_if<Platform::GamepadButtonControlIdentity>(&control);
        const usize actionIndex = flowActionSlotIndex(action);
        if (button != nullptr && button->routedWindow == ownerWindow_ &&
            actionIndex < FlowActionSlotCount &&
            matchesGamepadControl(action, button->button) &&
            button->gamepad.hasValue() &&
            button->gamepad.index() < gamepadPresses_[actionIndex].size())
        {
            gamepadPresses_[actionIndex][button->gamepad.index()] = button->gamepad;
        }
    }

    void clearPressed(UIFlowAction action,
                      const Platform::DigitalControlIdentity& control) noexcept
    {
        if (const auto* key = std::get_if<Platform::KeyControlIdentity>(&control);
            key != nullptr)
        {
            const auto controlIndex = keyboardControlIndex(action, key->key);
            if (key->window == ownerWindow_ && controlIndex.has_value())
            {
                keyboardPresses_[*controlIndex] = false;
            }
            return;
        }
        const auto* button =
            std::get_if<Platform::GamepadButtonControlIdentity>(&control);
        const usize actionIndex = flowActionSlotIndex(action);
        if (button != nullptr && actionIndex < FlowActionSlotCount &&
            button->gamepad.hasValue() &&
            button->gamepad.index() < gamepadPresses_[actionIndex].size() &&
            gamepadPresses_[actionIndex][button->gamepad.index()] == button->gamepad)
        {
            gamepadPresses_[actionIndex][button->gamepad.index()] = {};
        }
    }

    void clearAll() noexcept
    {
        keyboardPresses_.fill(false);
        for (auto& actionPresses : gamepadPresses_)
        {
            actionPresses.fill({});
        }
    }

    void clearGamepad(Platform::GamepadId gamepad) noexcept
    {
        if (!gamepad.hasValue() || gamepad.index() >= gamepadPresses_[0].size())
        {
            return;
        }
        for (auto& actionPresses : gamepadPresses_)
        {
            if (actionPresses[gamepad.index()] == gamepad)
            {
                actionPresses[gamepad.index()] = {};
            }
        }
    }

  private:
    [[nodiscard]] static std::optional<usize>
    keyboardControlIndex(UIFlowAction action, Platform::Key key) noexcept
    {
        if (action == UIFlowAction::Back && key == Platform::Key::Escape)
        {
            return 0;
        }
        if (action == UIFlowAction::Confirm && key == Platform::Key::Enter)
        {
            return 1;
        }
        if (action == UIFlowAction::Confirm && key == Platform::Key::KeypadEnter)
        {
            return 2;
        }
        if (action == UIFlowAction::Menu && key == Platform::Key::P)
        {
            return 3;
        }
        return std::nullopt;
    }

    [[nodiscard]] static bool matchesGamepadControl(
        UIFlowAction action, Platform::GamepadButton button) noexcept
    {
        return (action == UIFlowAction::Back && button == Platform::GamepadButton::East) ||
               (action == UIFlowAction::Confirm && button == Platform::GamepadButton::South) ||
               (action == UIFlowAction::Menu && button == Platform::GamepadButton::Start);
    }

    Platform::WindowId ownerWindow_{};
    std::array<bool, 4> keyboardPresses_{};
    std::array<
        std::array<Platform::GamepadId, Platform::PlatformFrameBuilder::MaximumGamepadSlots>,
        FlowActionSlotCount>
        gamepadPresses_{};
};

struct UIFlowActionSlot final {
    UIFlowActionCallback callback{};
    bool registered = false;
};

struct UIFlowGamepadAssignment final {
    Platform::GamepadId gamepad{};
    UIFlowLocalUserId localUser{};
};

struct UIFlowNodeState final {
    UIFlowNodeKind kind = UIFlowNodeKind::None;
    UINodeId layer{};
    UINodeId previous{};
    UINodeId next{};
    UINodeId bottom{};
    UINodeId top{};
    std::array<UIFlowActionSlot, FlowActionSlotCount> actions{};
    bool stacked = false;
};

} // namespace Tina::UI::Detail::UIContextPrivate
