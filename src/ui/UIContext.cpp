#include <tina/ui/UIContext.hpp>

#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/id/GenerationPool.hpp>
#include <tina/core/text/Utf8.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/ui/UIDirty.hpp>
#include <tina/ui/UIMotion.hpp>
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
#include "detail/UIGraphemeBreak.hpp"
#include "detail/UIDefaultActionPressState.hpp"
#include "detail/UIDirtyQueueStorage.hpp"
#include "detail/UIImeCompositionState.hpp"
#include "detail/UIImageContentStorage.hpp"
#include "detail/UIMotionTrackStorage.hpp"
#include "detail/UIKeyframeTimelineStorage.hpp"
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
#include "detail/UISplitViewInput.hpp"
#include "detail/UISplitViewLayout.hpp"
#include "detail/UISplitViewStateStorage.hpp"
#include "detail/UIStyleRoleResolver.hpp"
#include "detail/UIStyleSheetStorage.hpp"
#include "detail/UIThemeTransitionResolver.hpp"
#include "detail/UITooltipLayout.hpp"
#include "detail/UITooltipStateStorage.hpp"
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
#include <optional>
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
using Detail::resolveTooltipPlacement;
using Detail::resolveCommittedLineGeometry;
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
using Detail::resolveSplitViewFractionFromPointer;
using Detail::resolveSplitViewLayout;
using Detail::splitterGrabOffset;
using Detail::SplitterState;
using Detail::SplitViewState;
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
using Detail::UITextEditCommandPlan;
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
using Detail::TooltipLayoutScratch;
using Detail::TooltipState;
using Detail::UISplitViewStateStorage;
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
inline constexpr float CollectionRowHorizontalPadding = 8.0F;
inline constexpr float CollectionRowTextFitTolerance = 0.001F;

void configureCollectionRowLayout(UILayoutStyle& layout, float rowHeight,
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
           kind == BuiltinElementKind::Splitter;
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
    bool hasExplicitDescription = false;
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
    // Default duration 0 = instant stylesheet BoxFill resolve (historical path).
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
    UISplitViewStateStorage splitViewStorage;
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
          focusRestoreByNodeIndex(&resource), flowStatesByNodeIndex(&resource), styleRolesByNodeIndex(&resource),
          styleSheetStorage({
                                .classCapacity = capacities.styleClassCapacity,
                                .tokenCapacity = capacities.styleTokenCapacity,
                                .ruleCapacity = capacities.styleRuleCapacity,
                                .bucketCapacity = capacities.styleBucketCapacity,
                                .maxRulesPerBucket = capacities.styleRulesPerBucketCapacity,
                            },
                            resource),
          motionTrackStorage(capacities.motionTrackCapacity, resource),
          timelineStorage(capacities.nodeCapacity, capacities.timelineCapacity,
                          capacities.timelineTrackCapacity, capacities.timelineKeyframeCapacity,
                          capacities.activeTimelineCapacity, resource),
          timelineLayoutNodeScratch(&resource),
          timelinePaintNodeScratch(&resource),
          presentationOpacityByNodeIndex(&resource),
          presentationOpacityValidByNodeIndex(&resource),
          presentationOffsetXByNodeIndex(&resource),
          presentationOffsetYByNodeIndex(&resource),
          presentationOffsetValidByNodeIndex(&resource),
          styleClassesByNodeIndex(&resource), styleClassCountsByNodeIndex(&resource),
          styleStatesByNodeIndex(&resource),
          resolvedStyleInitializedByNodeIndex(&resource), resolvedBoxFillCacheByNodeIndex(&resource),
          resolvedStyleColorTokenByNodeIndex(&resource),
          styleTokenDependencyNextByNodeIndex(&resource),
          styleTokenDependencyPrevByNodeIndex(&resource),
          styleTokenDependencyHeadByTokenIndex(&resource),
          resolvedImageTintTokenByNodeIndex(&resource),
          imageTintTokenDependencyNextByNodeIndex(&resource),
          imageTintTokenDependencyPrevByNodeIndex(&resource),
          imageTintTokenDependencyHeadByTokenIndex(&resource),
          resolvedImageTintCacheByNodeIndex(&resource),
          resolvedImageTintValidByNodeIndex(&resource),
          boxPaintsByIndex(&resource),
          buttonPaintsByNodeIndex(&resource),
          themeBindingsByNodeIndex(&resource), styleOverridesByNodeIndex(&resource), themeDirtyScratchByNodeIndex(&resource),
          themeTextMetricsScratchByNodeIndex(&resource), localSolidFillCacheByIndex(&resource),
          localTextColorCacheByIndex(&resource), textStatesByIndex(&resource), semanticsStatesByNodeIndex(&resource),
          paintSnapshotBuilder(capacities.paintSnapshotCapacity),
          semanticsSnapshotBuilder(capacities.nodeCapacity, capacities.nodeCapacity, resource),
          canvasCommandStorage(capacities.nodeCapacity, capacities.canvasCommandCapacity, resource),
          imageContentStorage(capacities.nodeCapacity, capacities.imageContentCapacity, resource),
           textEditPaintsByNodeIndex(&resource), textEditMultilineByNodeIndex(&resource),
           textEditVisualLinesByNodeIndex(&resource), textEditVisualLayoutsByNodeIndex(&resource),
           textEditScrollYByNodeIndex(&resource), candidateTextEditVisualLinesByNodeIndex(&resource),
           candidateTextEditVisualLayoutsByNodeIndex(&resource), candidateTextEditScrollYByNodeIndex(&resource),
           textEditPreferredXByNodeIndex(&resource), textEditCaretAffinityByNodeIndex(&resource),
          progressBarStatesByNodeIndex(&resource), radioButtonStatesByNodeIndex(&resource),
          scrollViewPaintsByNodeIndex(&resource), scrollViewLayoutScratchByNodeIndex(&resource),
          dropdownStatesByNodeIndex(&resource), popupStatesByNodeIndex(&resource),
          popupLayoutScratchByNodeIndex(&resource),
          tooltipStorage(capacities.nodeCapacity, resource),
          splitViewStorage(capacities.nodeCapacity, resource),
          listViewStatesByNodeIndex(&resource),
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
          defaultActionPressState(owner), flowActionPressState(owner), rangeInputPressLatch(owner),
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
        for (usize index = 0; index < observedFlowInputDevices.size(); ++index)
        {
            observedFlowInputDevices[index].localUser =
                UIFlowLocalUserId{static_cast<u32>(index + 1U)};
        }
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
            .applyDefaultProductChrome = normalized.applyDefaultProductChrome,
        };

        auto impl = std::unique_ptr<Impl>(new Impl(ownerWindow, capacities, std::this_thread::get_id(),
                                                   std::move(lifetimeControl), std::move(*poolResult), resource));
        impl->motionClock = &impl->motionDefaultClock;
        impl->timelineLayoutNodeScratch.reserve(normalized.timelineTrackCapacity);
        impl->timelinePaintNodeScratch.reserve(normalized.timelineTrackCapacity);
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

    [[nodiscard]] bool isActiveFlowScreenIndex(u32 index) const noexcept
    {
        if (index >= flowStatesByNodeIndex.size())
        {
            return false;
        }
        const UIFlowNodeState& screenState = flowStatesByNodeIndex[index];
        if (screenState.kind != UIFlowNodeKind::Screen || !screenState.stacked ||
            !contains(screenState.layer))
        {
            return false;
        }
        const u32 layerIndex = screenState.layer.index();
        return layerIndex < flowStatesByNodeIndex.size() &&
               flowStatesByNodeIndex[layerIndex].kind == UIFlowNodeKind::Layer &&
               flowStatesByNodeIndex[layerIndex].top == idForIndex(index);
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
            const UILayoutStyle style = presentationLayoutStyle(index);
            UIVisibility ownVisibility = style.visibility;
            if (record->kind == BuiltinElementKind::Popup && index < popupStatesByNodeIndex.size() &&
                !popupStatesByNodeIndex[index].open)
            {
                ownVisibility = UIVisibility::Collapsed;
            }
            if (record->kind == BuiltinElementKind::Tooltip && index < tooltipStorage.capacity() &&
                !tooltipStorage.stateByIndex(index).open)
            {
                ownVisibility = UIVisibility::Collapsed;
            }
            if (index < flowStatesByNodeIndex.size() &&
                flowStatesByNodeIndex[index].kind == UIFlowNodeKind::Screen &&
                !isActiveFlowScreenIndex(index))
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
                scratch.inTooltipSubtree = record->kind == BuiltinElementKind::Tooltip;
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
                scratch.inTooltipSubtree =
                    record->kind == BuiltinElementKind::Tooltip || parentScratch.inTooltipSubtree;
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
            const UILayoutStyle style = presentationLayoutStyle(index);
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
                const UILayoutStyle childStyle = presentationLayoutStyle(childIndex);
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
                    if (record->kind == BuiltinElementKind::RadioButton &&
                        index < radioButtonStatesByNodeIndex.size() &&
                        radioButtonStatesByNodeIndex[index].paint.indicatorVisible)
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
                index < radioButtonStatesByNodeIndex.size() &&
                radioButtonStatesByNodeIndex[index].paint.indicatorVisible)
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
        const UILayoutStyle style = presentationLayoutStyle(index);
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
        const UILayoutStyle childStyle = presentationLayoutStyle(childIndex);
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
                         resolveOverlayRect(presentationLayoutStyle(childIndex),
                                            layoutScratchByIndex[childIndex],
                                            parentContentRect, statistics),
                         parentWorldRect, descendantClip);
    }

    void arrangePopupChild(u32 popupIndex, UILogicalRect anchorRect, UILogicalRect viewportRect,
                           LayoutPassStatistics& statistics) noexcept
    {
        const UILayoutStyle popupLayoutStyle = presentationLayoutStyle(popupIndex);
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

    [[nodiscard]] const UICommittedLayoutEntry*
    committedLayoutEntryFor(UINodeId node) const noexcept
    {
        if (!node.hasValue())
        {
            return nullptr;
        }
        const auto& entries = committedLayoutBuffers[publishedLayoutBufferIndex];
        const auto found = std::find_if(
            entries.begin(), entries.end(),
            [node](const UICommittedLayoutEntry& entry) noexcept {
                return entry.node == node;
            });
        return found != entries.end() ? &*found : nullptr;
    }

    void arrangeTooltipChild(u32 tooltipIndex, UILogicalRect parentWorldRect,
                             UILogicalRect viewportRect,
                             LayoutPassStatistics& statistics) noexcept
    {
        TooltipState& tooltip = tooltipStorage.stateByIndex(tooltipIndex);
        LayoutScratchState& tooltipScratch = layoutScratchByIndex[tooltipIndex];
        const UINodeId tooltipNode = idForIndex(tooltipIndex);
        const UICommittedLayoutEntry* anchorEntry = committedLayoutEntryFor(tooltip.anchor);
        if (!tooltip.open || tooltipStorage.activeTooltip() != tooltipNode ||
            !hasValidTooltipRelationship(tooltipNode, tooltip.anchor) ||
            anchorEntry == nullptr ||
            anchorEntry->effectiveVisibility != UIVisibility::Visible)
        {
            tooltipScratch.effectiveVisibility = UIVisibility::Collapsed;
            assignLayoutRect(tooltipIndex, {}, parentWorldRect, viewportRect);
            tooltipStorage.layoutScratchByIndex(tooltipIndex) = {};
            return;
        }

        refreshMeasuredSizeForParentContent(tooltipIndex, viewportRect, statistics);
        const auto resolved = resolveTooltipPlacement(
            presentationLayoutStyle(tooltipIndex), tooltipScratch, tooltip.config,
            anchorEntry->worldRect, viewportRect, statistics);
        assignLayoutRect(tooltipIndex, resolved.rect, parentWorldRect, viewportRect);
        tooltipStorage.layoutScratchByIndex(tooltipIndex).metrics = UITooltipMetrics{
            .anchorRect = anchorEntry->worldRect,
            .tooltipRect = resolved.rect,
            .resolvedPlacement = resolved.placement,
            .open = tooltipScratch.effectiveVisibility == UIVisibility::Visible,
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
            if (textStatesByIndex[currentChild].metrics.measuredSize.height >
                state.style.rowHeight + CollectionRowTextFitTolerance)
            {
                return fail(UIErrorCode::InvalidControlValue,
                            "UI ListView row height is smaller than its text line box");
            }
            configureCollectionRowLayout(layoutStylesByIndex[currentChild], state.style.rowHeight);
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
            if (textStatesByIndex[currentChild].metrics.measuredSize.height >
                state.style.rowHeight + CollectionRowTextFitTolerance)
            {
                return fail(UIErrorCode::InvalidControlValue,
                            "UI TreeView row height is smaller than its text line box");
            }
            configureCollectionRowLayout(rowStyle, state.style.rowHeight,
                                         normalizeFloat(static_cast<float>(leftPadding)));
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
        const UILayoutStyle parentStyle = presentationLayoutStyle(parentIndex);
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
        if (parentRecord->kind == BuiltinElementKind::Popup ||
            parentRecord->kind == BuiltinElementKind::Tooltip ||
            parentStyle.clipDescendants)
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
            if (parentRecord->kind == BuiltinElementKind::Tooltip &&
                parentIndex < tooltipStorage.capacity())
            {
                tooltipStorage.layoutScratchByIndex(parentIndex) = {};
            }
            if (parentRecord->kind == BuiltinElementKind::SplitView &&
                parentIndex < splitViewStorage.capacity())
            {
                splitViewStorage.layoutScratchByIndex(parentIndex) = {};
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
        if (parentRecord->kind == BuiltinElementKind::SplitView)
        {
            const UINodeId splitView = idForIndex(parentIndex);
            const SplitViewState* state = splitViewStorage.trySplitView(splitView);
            if (state == nullptr)
            {
                return fail(Core::CoreErrorCode::Internal,
                            "UI SplitView is missing retained state");
            }
            const UISplitViewParts parts = splitViewStorage.parts(splitView);
            if (parts.hasValue())
            {
                const auto plan = resolveSplitViewLayout(
                    unscrolledContentRect, state->config, state->requestedFraction);
                splitViewStorage.layoutScratchByIndex(parentIndex).metrics = plan.metrics;
                assignLayoutRect(parts.primaryPane.index(), plan.metrics.primaryRect,
                                 parentWorldRect, descendantClip);
                assignLayoutRect(parts.splitter.index(), plan.metrics.splitterRect,
                                 parentWorldRect, descendantClip);
                assignLayoutRect(parts.secondaryPane.index(), plan.metrics.secondaryRect,
                                 parentWorldRect, descendantClip);
                return Core::success();
            }
            splitViewStorage.layoutScratchByIndex(parentIndex).metrics = UISplitViewMetrics{
                .fraction = state->requestedFraction,
                .orientation = state->config.orientation,
            };
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
            const UILayoutStyle childStyle = presentationLayoutStyle(childIndex);
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
            descendantClip = intersectRects(descendantClip, plan.viewportRect);
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
            const UILayoutStyle childStyle = presentationLayoutStyle(currentChild);
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
                } else if (childRecord->kind == BuiltinElementKind::Tooltip &&
                           currentChild < tooltipStorage.capacity())
                {
                    arrangeTooltipChild(currentChild, parentWorldRect, viewportRect, statistics);
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
        const UILayoutStyle layout = presentationLayoutStyle(index);
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
            index < radioButtonStatesByNodeIndex.size() &&
            radioButtonStatesByNodeIndex[index].paint.indicatorVisible)
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
        const auto appendPass = [&](bool popupPass, bool tooltipPass) noexcept {
            for (const u32 index : order)
            {
                if (layoutScratchByIndex[index].inPopupSubtree != popupPass ||
                    layoutScratchByIndex[index].inTooltipSubtree != tooltipPass)
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
        appendPass(false, false);
        appendPass(true, false);
        appendPass(false, true);
        appendPass(true, true);
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
        if (record != nullptr && record->kind == BuiltinElementKind::RadioButton &&
            nodeIndex < radioButtonStatesByNodeIndex.size())
        {
            const RadioButtonState& radio = radioButtonStatesByNodeIndex[nodeIndex];
            const UIRadioButtonPaint& paint = radio.paint;
            const auto applyOverride = [&color](UIStraightSrgba8Color overrideColor) noexcept {
                if (overrideColor.alpha != 0)
                {
                    color = premultiply(overrideColor);
                }
            };
            if (radio.selected)
            {
                applyOverride(paint.selectedBackgroundColor);
            }
            if (!radio.selected && defaultActionFocusButton == node)
            {
                applyOverride(paint.focusedBackgroundColor);
            }
            if (!radio.selected && hoveredPrimaryControl == node)
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
        else if (record.kind == BuiltinElementKind::RadioButton)
        {
            relevantOverrides |= static_cast<u16>(UIStyleOverride::RadioButtonPaint);
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

    [[nodiscard]] Detail::UIStyleBoxFillResolution resolveStyleBoxFill(
        u32 nodeIndex, UIStyleState states) const noexcept
    {
        const usize classCount = styleClassCountsByNodeIndex[nodeIndex];
        const auto classes = std::span<const UIStyleClassId>(
            styleClassesByNodeIndex[nodeIndex].data(), classCount);
        return styleSheetStorage.resolveValidated(
            styleRolesByNodeIndex[nodeIndex], classes, states);
    }

    [[nodiscard]] std::span<const UIStyleClassId> styleClassesFor(u32 nodeIndex) const noexcept
    {
        return std::span<const UIStyleClassId>(styleClassesByNodeIndex[nodeIndex].data(),
                                               styleClassCountsByNodeIndex[nodeIndex]);
    }

    [[nodiscard]] bool styleBackgroundTransitionEnabled() const noexcept
    {
        return styleBackgroundColorTransitionSpec.duration.count() > 0.0;
    }

    [[nodiscard]] bool needsStyleBackgroundMotionReservation(UIStyleRoleId role,
                                                             std::span<const UIStyleClassId> classes) const noexcept
    {
        return styleBackgroundTransitionEnabled() &&
               styleSheetStorage.hasStatefulBoxFillCandidateValidated(role, classes);
    }

    void unlinkTokenDependencyList(u32 nodeIndex, std::pmr::vector<UIStyleTokenId>& tokenByNode,
                                   std::pmr::vector<u32>& nextByNode, std::pmr::vector<u32>& prevByNode,
                                   std::pmr::vector<u32>& headByToken) noexcept
    {
        if (nodeIndex >= tokenByNode.size())
        {
            return;
        }
        const UIStyleTokenId token = tokenByNode[nodeIndex];
        if (!token.hasValue())
        {
            return;
        }
        const usize tokenSlot = token.value - 1U;
        if (tokenSlot >= headByToken.size())
        {
            tokenByNode[nodeIndex] = {};
            nextByNode[nodeIndex] = 0;
            prevByNode[nodeIndex] = 0;
            return;
        }
        const u32 next = nextByNode[nodeIndex];
        const u32 prev = prevByNode[nodeIndex];
        if (prev == 0)
        {
            headByToken[tokenSlot] = next;
        }
        else
        {
            nextByNode[prev - 1U] = next;
        }
        if (next != 0)
        {
            prevByNode[next - 1U] = prev;
        }
        nextByNode[nodeIndex] = 0;
        prevByNode[nodeIndex] = 0;
        tokenByNode[nodeIndex] = {};
    }

    void linkTokenDependencyList(u32 nodeIndex, UIStyleTokenId token,
                                 std::pmr::vector<UIStyleTokenId>& tokenByNode,
                                 std::pmr::vector<u32>& nextByNode, std::pmr::vector<u32>& prevByNode,
                                 std::pmr::vector<u32>& headByToken) noexcept
    {
        if (!token.hasValue() || nodeIndex >= tokenByNode.size())
        {
            return;
        }
        const usize tokenSlot = token.value - 1U;
        if (tokenSlot >= headByToken.size())
        {
            return;
        }
        const u32 nodeLink = nodeIndex + 1U;
        const u32 head = headByToken[tokenSlot];
        nextByNode[nodeIndex] = head;
        prevByNode[nodeIndex] = 0;
        if (head != 0)
        {
            prevByNode[head - 1U] = nodeLink;
        }
        headByToken[tokenSlot] = nodeLink;
        tokenByNode[nodeIndex] = token;
    }

    void unlinkStyleTokenDependency(u32 nodeIndex) noexcept
    {
        unlinkTokenDependencyList(nodeIndex, resolvedStyleColorTokenByNodeIndex,
                                  styleTokenDependencyNextByNodeIndex,
                                  styleTokenDependencyPrevByNodeIndex,
                                  styleTokenDependencyHeadByTokenIndex);
    }

    void linkStyleTokenDependency(u32 nodeIndex, UIStyleTokenId token) noexcept
    {
        linkTokenDependencyList(nodeIndex, token, resolvedStyleColorTokenByNodeIndex,
                                styleTokenDependencyNextByNodeIndex,
                                styleTokenDependencyPrevByNodeIndex,
                                styleTokenDependencyHeadByTokenIndex);
    }

    void setResolvedStyleColorTokenDependency(u32 nodeIndex, UIStyleTokenId token) noexcept
    {
        if (nodeIndex >= resolvedStyleColorTokenByNodeIndex.size())
        {
            return;
        }
        if (resolvedStyleColorTokenByNodeIndex[nodeIndex] == token)
        {
            return;
        }
        unlinkStyleTokenDependency(nodeIndex);
        if (token.hasValue())
        {
            linkStyleTokenDependency(nodeIndex, token);
        }
    }

    void unlinkImageTintTokenDependency(u32 nodeIndex) noexcept
    {
        unlinkTokenDependencyList(nodeIndex, resolvedImageTintTokenByNodeIndex,
                                  imageTintTokenDependencyNextByNodeIndex,
                                  imageTintTokenDependencyPrevByNodeIndex,
                                  imageTintTokenDependencyHeadByTokenIndex);
    }

    void linkImageTintTokenDependency(u32 nodeIndex, UIStyleTokenId token) noexcept
    {
        linkTokenDependencyList(nodeIndex, token, resolvedImageTintTokenByNodeIndex,
                                imageTintTokenDependencyNextByNodeIndex,
                                imageTintTokenDependencyPrevByNodeIndex,
                                imageTintTokenDependencyHeadByTokenIndex);
    }

    void setResolvedImageTintTokenDependency(u32 nodeIndex, UIStyleTokenId token) noexcept
    {
        if (nodeIndex >= resolvedImageTintTokenByNodeIndex.size())
        {
            return;
        }
        if (resolvedImageTintTokenByNodeIndex[nodeIndex] == token)
        {
            return;
        }
        unlinkImageTintTokenDependency(nodeIndex);
        if (token.hasValue())
        {
            linkImageTintTokenDependency(nodeIndex, token);
        }
    }

    [[nodiscard]] bool hasLocalImageTintOverride(u32 nodeIndex) const noexcept
    {
        return nodeIndex < styleOverridesByNodeIndex.size() &&
               (styleOverridesByNodeIndex[nodeIndex] &
                static_cast<u16>(UIStyleOverride::ImageTint)) != 0;
    }

    [[nodiscard]] UIStraightSrgba8Color resolvedImageTintColor(u32 nodeIndex,
                                                               const UIImageContent& image) const noexcept
    {
        if (hasLocalImageTintOverride(nodeIndex))
        {
            return image.tint;
        }
        if (nodeIndex < resolvedImageTintValidByNodeIndex.size() &&
            resolvedImageTintValidByNodeIndex[nodeIndex] != 0)
        {
            return resolvedImageTintCacheByNodeIndex[nodeIndex];
        }
        return image.tint;
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
        const bool hadResolvedStyle = resolvedStyleInitializedByNodeIndex[nodeIndex] != 0;
        const UIStyleState previousStates = styleStatesByNodeIndex[nodeIndex];
        const UIPremultipliedRgba8Color previousFill = resolvedBoxFillCacheByNodeIndex[nodeIndex];
        styleStatesByNodeIndex[nodeIndex] = states;
        UIPremultipliedRgba8Color resolvedFill = resolveBuiltinBoxFillColor(
            node, nodeIndex, localSolidFillCacheByIndex[nodeIndex]);

        const Detail::UIStyleBoxFillResolution resolution =
            resolveStyleBoxFill(nodeIndex, states);

        if (hasLocalBoxFillOverride(nodeIndex, *record))
        {
            resolvedBoxFillCacheByNodeIndex[nodeIndex] = resolvedFill;
            setResolvedStyleColorTokenDependency(nodeIndex, {});
        }
        else
        {
            if (resolution.color.has_value())
            {
                resolvedFill = premultiply(*resolution.color);
            }
            // Optional paint-only motion when interaction state changes the
            // stylesheet BoxFill color. Style binding has already reserved the
            // track, so activation failure is an internal invariant violation.
            const bool stateChanged = previousStates != states;
            const bool colorChanged = previousFill != resolvedFill;
            const bool canAnimate =
                hadResolvedStyle && stateChanged && colorChanged && !reducedMotionEnabled &&
                styleBackgroundTransitionEnabled() && motionTrackStorage.hasPersistentReservation(
                                                          node, UIAnimatableProperty::BackgroundColor);
            if (canAnimate)
            {
                const auto presentation = motionPresentationFor(node);
                const UIStraightSrgba8Color startColor = presentation.hasBackgroundColor
                                                             ? presentation.backgroundColor
                                                             : unpremultiplyColor(previousFill);
                const UIStraightSrgba8Color targetColor =
                    resolution.color.has_value() ? *resolution.color : unpremultiplyColor(resolvedFill);
                if (Core::Status motionStatus = motionTrackStorage.activateReservedStyleColor(
                        node, startColor, targetColor, styleBackgroundColorTransitionSpec, motionNow());
                    !motionStatus)
                {
                    std::terminate();
                }
            }
            resolvedBoxFillCacheByNodeIndex[nodeIndex] = resolvedFill;
            setResolvedStyleColorTokenDependency(nodeIndex, resolution.colorToken);
        }

        if (hasLocalImageTintOverride(nodeIndex) || !resolution.imageTint.has_value())
        {
            if (nodeIndex < resolvedImageTintValidByNodeIndex.size())
            {
                resolvedImageTintValidByNodeIndex[nodeIndex] = 0;
                resolvedImageTintCacheByNodeIndex[nodeIndex] = {};
            }
            setResolvedImageTintTokenDependency(nodeIndex, {});
        }
        else
        {
            resolvedImageTintCacheByNodeIndex[nodeIndex] = *resolution.imageTint;
            resolvedImageTintValidByNodeIndex[nodeIndex] = 1;
            setResolvedImageTintTokenDependency(nodeIndex, resolution.imageTintToken);
        }
        resolvedStyleInitializedByNodeIndex[nodeIndex] = 1;
        return resolution.candidateRuleCount;
    }

    [[nodiscard]] usize refreshResolvedStyleCache(u32 nodeIndex) noexcept
    {
        return refreshResolvedStyleCache(nodeIndex,
                                         deriveStyleState(idForIndex(nodeIndex), nodeIndex));
    }

    [[nodiscard]] UIBoxPaint resolvedBoxChrome(UINodeId node, u32 nodeIndex) const noexcept
    {
        // Motion presentation (border/radius/visual offset) is applied first;
        // button pressed/focus chrome may still override for interaction feedback.
        UIBoxPaint chrome = presentationBoxPaint(node, nodeIndex);
        const NodeRecord* record = recordByIndex(nodeIndex);
        if (record == nullptr || !isCandidateNodeEnabled(node))
        {
            return chrome;
        }

        UIStraightSrgba8Color focusedBorderColor{};
        if (isButtonChromeKind(record->kind) && nodeIndex < buttonPaintsByNodeIndex.size())
        {
            focusedBorderColor = buttonPaintsByNodeIndex[nodeIndex].focusedBorderColor;
        }
        else if (record->kind == BuiltinElementKind::RadioButton &&
                 nodeIndex < radioButtonStatesByNodeIndex.size())
        {
            focusedBorderColor = radioButtonStatesByNodeIndex[nodeIndex].paint.focusedBorderColor;
        }
        else
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

        if (defaultActionFocusButton == node && focusedBorderColor.alpha != 0)
        {
            chrome.borderLight = focusedBorderColor;
            chrome.borderDark = focusedBorderColor;
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
        const auto drawable = [&layoutEntry](const UICanvasCommand& command) noexcept {
            if (command.color.alpha == 0)
            {
                return false;
            }
            if (command.kind == UICanvasCommandKind::SolidLine)
            {
                return resolveCommittedLineGeometry(
                           UILineGeometry{
                               .start = command.lineStart,
                               .end = command.lineEnd,
                               .thickness = command.lineThickness,
                           },
                           {.x = layoutEntry.worldRect.x, .y = layoutEntry.worldRect.y})
                    .has_value();
            }
            return command.bounds.width > 0.0F && command.bounds.height > 0.0F;
        };
        canvasCommandStorage.forEach(layoutEntry.node.index(), [&](const UICanvasCommand& command) noexcept {
            if (!drawable(command))
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
        const auto localRect = [&layoutEntry](const UICanvasCommand& command) noexcept {
            return UILogicalRect{
                .x = normalizeFloat(layoutEntry.worldRect.x + command.bounds.x),
                .y = normalizeFloat(layoutEntry.worldRect.y + command.bounds.y),
                .width = command.bounds.width,
                .height = command.bounds.height,
            };
        };
        const auto drawable = [&layoutEntry](const UICanvasCommand& command) noexcept {
            if (command.color.alpha == 0)
            {
                return false;
            }
            if (command.kind == UICanvasCommandKind::SolidLine)
            {
                return resolveCommittedLineGeometry(
                           UILineGeometry{
                               .start = command.lineStart,
                               .end = command.lineEnd,
                               .thickness = command.lineThickness,
                           },
                           {.x = layoutEntry.worldRect.x, .y = layoutEntry.worldRect.y})
                    .has_value();
            }
            return command.bounds.width > 0.0F && command.bounds.height > 0.0F;
        };
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
            if (!drawable(command))
            {
                return;
            }
            if (command.kind == UICanvasCommandKind::SolidRect)
            {
                output.push_back(UICommittedPaintEntry{
                    .node = layoutEntry.node,
                    .root = root,
                    .worldRect = localRect(command),
                    .effectiveClip = canvasClip,
                    .paintOrdinal = nextPaintOrdinal,
                    .solidFill = premultiply(command.color),
                    .cornerRadii = command.cornerRadii,
                });
                ++nextPaintOrdinal;
            } else if (command.kind == UICanvasCommandKind::SolidEllipse)
            {
                output.push_back(UICommittedPaintEntry{
                    .node = layoutEntry.node,
                    .root = root,
                    .worldRect = localRect(command),
                    .effectiveClip = canvasClip,
                    .paintOrdinal = nextPaintOrdinal,
                    .solidFill = premultiply(command.color),
                    .kind = UICommittedPaintKind::SolidEllipse,
                    .ellipseStrokeWidth = command.ellipseStrokeWidth,
                });
                ++nextPaintOrdinal;
            } else if (command.kind == UICanvasCommandKind::SolidLine)
            {
                const auto geometry = resolveCommittedLineGeometry(
                    UILineGeometry{
                        .start = command.lineStart,
                        .end = command.lineEnd,
                        .thickness = command.lineThickness,
                    },
                    {.x = layoutEntry.worldRect.x, .y = layoutEntry.worldRect.y});
                if (!geometry)
                {
                    return;
                }
                output.push_back(UICommittedPaintEntry{
                    .node = layoutEntry.node,
                    .root = root,
                    .worldRect = geometry->worldEnvelope,
                    .effectiveClip = canvasClip,
                    .paintOrdinal = nextPaintOrdinal,
                    .solidFill = premultiply(command.color),
                    .kind = UICommittedPaintKind::SolidLine,
                    .lineStart = geometry->worldStart,
                    .lineEnd = geometry->worldEnd,
                    .lineThickness = command.lineThickness,
                });
                ++nextPaintOrdinal;
            } else if (command.kind == UICanvasCommandKind::Image)
            {
                appendImage(
                    command,
                    localRect(command),
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
                   nodeIndex < radioButtonStatesByNodeIndex.size() &&
                   radioButtonStatesByNodeIndex[nodeIndex].paint.indicatorVisible)
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

    [[nodiscard]] Detail::UITextEditPaintState resolveTextEditPaintState(
        UINodeId node, bool applyDisabledOpacity, bool useCandidateVisualState) const noexcept
    {
        const u32 nodeIndex = node.index();
        const NodeRecord* record = recordByIndex(nodeIndex);
        const bool focused = record != nullptr && record->kind == BuiltinElementKind::TextEdit &&
                             isNodeEnabled(node) && textInputFocus == node && isLiveTextEdit(textInputFocus);
        const WidgetTextState* textState =
            nodeIndex < textStatesByIndex.size() ? &textStatesByIndex[nodeIndex] : nullptr;
        const UITextStyle style = textState != nullptr ? textState->style : UITextStyle{};
        UIPremultipliedRgba8Color textColor{};
        if (textState != nullptr)
        {
            const auto motionPresentation = motionPresentationFor(node);
            if (motionPresentation.hasTextColor)
            {
                textColor = premultiply(motionPresentation.textColor);
            }
            else if (applyDisabledOpacity && nodeIndex < localTextColorCacheByIndex.size())
            {
                textColor = widgetPaintColor(node, localTextColorCacheByIndex[nodeIndex]);
            }
            else
            {
                textColor = premultiply(style.color);
            }
            if (motionPresentation.hasOpacity ||
                (nodeIndex < presentationOpacityValidByNodeIndex.size() &&
                 presentationOpacityValidByNodeIndex[nodeIndex] != 0))
            {
                const float opacity = currentOpacity(node, nodeIndex);
                if (opacity < 1.0F)
                {
                    textColor = applyOpacity(
                        textColor,
                        static_cast<u8>(std::clamp(opacity, 0.0F, 1.0F) * 255.0F + 0.5F));
                }
            }
        }
        const bool preeditActive = focused && imeComposition.active();
        const bool multilineEnabled = nodeIndex < textEditMultilineByNodeIndex.size() &&
                                      textEditMultilineByNodeIndex[nodeIndex].enabled;
        const UITextEditPaint paint =
            nodeIndex < textEditPaintsByNodeIndex.size() ? textEditPaintsByNodeIndex[nodeIndex]
                                                         : UITextEditPaint{};
        const Detail::UITextInputState* textInputState = behaviorStateStorage.tryTextInputState(nodeIndex);
        const auto& visualLinesByNodeIndex = useCandidateVisualState
                                                 ? candidateTextEditVisualLinesByNodeIndex
                                                 : textEditVisualLinesByNodeIndex;
        const auto& visualLayoutsByNodeIndex = useCandidateVisualState
                                                   ? candidateTextEditVisualLayoutsByNodeIndex
                                                   : textEditVisualLayoutsByNodeIndex;
        const auto& scrollYByNodeIndex = useCandidateVisualState
                                             ? candidateTextEditScrollYByNodeIndex
                                             : textEditScrollYByNodeIndex;
        return Detail::UITextEditPaintState{
            .focused = focused,
            .preeditActive = preeditActive,
            .committedText = presentationTextViewFor(nodeIndex),
            .selection = focused && textInputState != nullptr ? textInputState->selection : UITextSelection{},
            .caretAffinity = nodeIndex < textEditCaretAffinityByNodeIndex.size()
                                 ? textEditCaretAffinityByNodeIndex[nodeIndex]
                                 : Detail::UITextEditCaretAffinity::Downstream,
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
            .overflow = textState != nullptr ? textState->overflow : UITextOverflow::Clip,
            .multilineEnabled = multilineEnabled,
            .wrapMode = nodeIndex < textEditMultilineByNodeIndex.size()
                            ? textEditMultilineByNodeIndex[nodeIndex].wrapMode
                            : UITextEditWrapMode::NoWrap,
            .visualLines = multilineEnabled && nodeIndex < visualLinesByNodeIndex.size()
                               ? std::span<const Detail::UITextEditVisualLine>(visualLinesByNodeIndex[nodeIndex])
                               : std::span<const Detail::UITextEditVisualLine>{},
            .visualLayout = multilineEnabled && nodeIndex < visualLayoutsByNodeIndex.size()
                                ? visualLayoutsByNodeIndex[nodeIndex]
                                : Detail::UITextEditVisualLayout{},
            .scrollY = nodeIndex < scrollYByNodeIndex.size()
                           ? scrollYByNodeIndex[nodeIndex] : 0.0F,
        };
    }

    // Both the counting and the appending paint pass must observe the same
    // available width, so the content box is injected here instead of at each
    // call site. ADR 0022: truncation reads the one committed content
    // placement and never re-derives geometry from worldRect + padding.
    [[nodiscard]] Detail::UITextEditPaintState resolveTextEditPaintStateFor(
        const UICommittedLayoutEntry& layoutEntry, bool applyDisabledOpacity,
        bool useCandidateVisualState) const noexcept
    {
        Detail::UITextEditPaintState state = resolveTextEditPaintState(
            layoutEntry.node, applyDisabledOpacity, useCandidateVisualState);
        state.availableWidth = layoutEntry.contentPlacement.contentBox.width;
        state.intrinsicWidth = layoutEntry.contentPlacement.hasIntrinsicContent
                                   ? layoutEntry.contentPlacement.intrinsicSize.width
                                   : 0.0F;
        return state;
    }

    [[nodiscard]] Core::Status buildTextEditVisualState(
        std::span<const UICommittedLayoutEntry> layoutEntries) noexcept
    {
        for (const UICommittedLayoutEntry& entry : layoutEntries)
        {
            const u32 index = entry.node.index();
            if (index >= textEditMultilineByNodeIndex.size() ||
                !textEditMultilineByNodeIndex[index].enabled ||
                index >= textStatesByIndex.size())
            {
                continue;
            }
            const UITextEditMultilineConfig config = textEditMultilineByNodeIndex[index];
            auto& lines = candidateTextEditVisualLinesByNodeIndex[index];
            lines.clear();
            const WidgetTextState& text = textStatesByIndex[index];
            const float lineHeight = text.style.logicalSize * text.style.lineHeightScale;
            const float fallback = text.style.logicalSize * text.style.advanceScale;
            if (!std::isfinite(lineHeight) || lineHeight <= 0.0F ||
                !std::isfinite(fallback) || fallback <= 0.0F)
            {
                return fail(UIErrorCode::InvalidText, "UI multiline TextEdit metrics are invalid");
            }
            const usize capacity = config.maximumVisualLines != 0
                                       ? config.maximumVisualLines
                                       : capacityConfig.textEditVisualLineCapacity;
            lines.resize(capacity);
            std::span<const UITextGlyphRaster> glyphs{};
            if (textRasterizer != nullptr && textFace.hasValue())
            {
                auto raster = textRasterizer->raster(textFace, textViewFor(index), text.style);
                if (raster)
                {
                    glyphs = raster->glyphs;
                }
            }
            Detail::UITextEditVisualLayout result{};
            if (!Detail::buildTextEditVisualLayout(
                    textViewFor(index), entry.contentPlacement.contentBox.width,
                    entry.contentPlacement.contentBox.height, lineHeight, fallback,
                    config.wrapMode, glyphs, lines, result))
            {
                return fail(UIErrorCode::CapacityExceeded,
                            "UI multiline TextEdit visual-line capacity has been exhausted");
            }
            lines.resize(result.lineCount);
            candidateTextEditVisualLayoutsByNodeIndex[index] = result;
            float scroll = textEditScrollYByNodeIndex[index];
            if (!std::isfinite(scroll) || scroll < 0.0F)
            {
                scroll = 0.0F;
            }
            candidateTextEditScrollYByNodeIndex[index] =
                (std::min)(scroll, result.maximumScrollY);
        }
        return Core::success();
    }
    [[nodiscard]] Core::Result<usize> countPaintEntries(
        const UICommittedLayoutEntry& layoutEntry, bool useCandidateTextEditVisualState) const
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
            presentationBoxFill(layoutEntry.node, nodeIndex);
        const UILogicalRect paintWorld =
            presentationPaintWorldRect(layoutEntry.node, nodeIndex, layoutEntry.worldRect);
        paintEntryCount += countBoxChromePaintEntries(boxPaint, paintWorld, !resolvedFill.isTransparent());
        paintEntryCount += countCanvasPaintEntries(layoutEntry);
        if (const UIImageContent* image = imageContentStorage.get(nodeIndex); image != nullptr)
        {
            const UIStraightSrgba8Color tint = resolvedImageTintColor(nodeIndex, *image);
            if (tint.alpha != 0 && layoutEntry.contentPlacement.contentBox.width > 0.0F &&
                layoutEntry.contentPlacement.contentBox.height > 0.0F)
            {
                ++paintEntryCount;
            }
        }
        auto controlPaintBatch = resolveControlPaintBatch(layoutEntry, false);
        if (!controlPaintBatch)
        {
            return Core::failure(controlPaintBatch.error());
        }
        paintEntryCount += controlPaintBatch->size();
        paintEntryCount += Detail::UITextEditPaintEmitter::countEntries(
            resolveTextEditPaintStateFor(
                layoutEntry, false, useCandidateTextEditVisualState));
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
                               const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal,
                               bool useCandidateTextEditVisualState) noexcept
    {
        const auto caretGeometry = Detail::UITextEditPaintEmitter::append(
            output, layoutEntry, nextPaintOrdinal,
            resolveTextEditPaintStateFor(
                layoutEntry, true, useCandidateTextEditVisualState));
        if (caretGeometry.has_value() && caretGeometry->effectiveClip.width > 0.0F &&
            caretGeometry->effectiveClip.height > 0.0F &&
            caretGeometry->worldRect.x < caretGeometry->effectiveClip.right() &&
            caretGeometry->worldRect.right() > caretGeometry->effectiveClip.x &&
            caretGeometry->worldRect.y < caretGeometry->effectiveClip.bottom() &&
            caretGeometry->worldRect.bottom() > caretGeometry->effectiveClip.y)
        {
            candidateTextInputCaretRect = caretGeometry->worldRect;
        }
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
        if (image == nullptr || record == nullptr)
        {
            return;
        }
        const UIStraightSrgba8Color tint = resolvedImageTintColor(nodeIndex, *image);
        if (tint.alpha == 0)
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
            .solidFill = premultiply(tint),
            .kind = UICommittedPaintKind::Image,
            .imageSource = image->source,
            .imageSampling = image->sampling,
        });
        ++nextPaintOrdinal;
    }

    [[nodiscard]] Core::Status appendPaintEntries(std::pmr::vector<UICommittedPaintEntry>& output,
                                                  const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal,
                                                  bool useCandidateTextEditVisualState)
    {
        const u32 nodeIndex = layoutEntry.node.index();
        const UIBoxPaint boxPaint = resolvedBoxChrome(layoutEntry.node, nodeIndex);
        const UIPremultipliedRgba8Color fill =
            presentationBoxFill(layoutEntry.node, nodeIndex);
        const UILogicalRect paintWorld =
            presentationPaintWorldRect(layoutEntry.node, nodeIndex, layoutEntry.worldRect);
        appendBoxChromePaints(output, layoutEntry.node, paintWorld, layoutEntry.effectiveClip,
                              nextPaintOrdinal, boxPaint, fill);
        appendCanvasPaints(output, layoutEntry, nextPaintOrdinal);
        appendImagePaint(output, layoutEntry, nextPaintOrdinal);
        auto controlPaintBatch = resolveControlPaintBatch(layoutEntry, true);
        if (!controlPaintBatch)
        {
            return Core::failure(controlPaintBatch.error());
        }
        controlPaintBatch->appendTo(output, layoutEntry.node, layoutEntry.effectiveClip, nextPaintOrdinal);
        appendTextGlyphPaints(
            output, layoutEntry, nextPaintOrdinal, useCandidateTextEditVisualState);
        return Core::success();
    }

    struct PaintSnapshotSourceContext final {
        Impl* impl = nullptr;
        bool useCandidateTextEditVisualState = false;
    };

    [[nodiscard]] static constexpr Detail::UIPaintSnapshotSourceAdapter paintSnapshotSourceAdapter() noexcept
    {
        return Detail::UIPaintSnapshotSourceAdapter{
            .countEntries = [](const void* context, const UICommittedLayoutEntry& layoutEntry) -> Core::Result<usize> {
                const auto& source = *static_cast<const PaintSnapshotSourceContext*>(context);
                return source.impl->countPaintEntries(
                    layoutEntry, source.useCandidateTextEditVisualState);
            },
            .appendEntries = [](void* context, std::pmr::vector<UICommittedPaintEntry>& output,
                                const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal) -> Core::Status {
                auto& source = *static_cast<PaintSnapshotSourceContext*>(context);
                return source.impl->appendPaintEntries(
                    output, layoutEntry, nextPaintOrdinal,
                    source.useCandidateTextEditVisualState);
            },
        };
    }

    [[nodiscard]] Core::Result<usize>
    validatePaintCandidateCapacity(std::span<const UICommittedLayoutEntry> layoutEntries,
                                   bool useCandidateTextEditVisualState)
    {
        PaintSnapshotSourceContext source{
            .impl = this,
            .useCandidateTextEditVisualState = useCandidateTextEditVisualState,
        };
        return paintSnapshotBuilder.validateCapacity(
            layoutEntries, &source, paintSnapshotSourceAdapter());
    }

    [[nodiscard]] Core::Status buildCommittedPaint(std::pmr::vector<UICommittedPaintEntry>& output,
                                                   std::span<const UICommittedLayoutEntry> layoutEntries,
                                                   bool useCandidateTextEditVisualState)
    {
        candidateTextInputCaretRect.reset();
        PaintSnapshotSourceContext source{
            .impl = this,
            .useCandidateTextEditVisualState = useCandidateTextEditVisualState,
        };
        return paintSnapshotBuilder.build(
            output, layoutEntries, &source, paintSnapshotSourceAdapter());
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
        if (!state.hasExplicitDescription)
        {
            const UINodeId tooltip = tooltipStorage.tooltipForAnchor(node);
            if (hasValidTooltipRelationship(tooltip, node))
            {
                source.description = textViewFor(tooltip.index());
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
                 record->kind == BuiltinElementKind::RadioButton || record->kind == BuiltinElementKind::TextEdit ||
                 record->kind == BuiltinElementKind::Splitter))
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
        splitterDragGrabOffset = 0.0F;
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

    void resetTextEditPreferredX(UINodeId node) noexcept
    {
        if (node.hasValue() && node.index() < textEditPreferredXByNodeIndex.size())
        {
            textEditPreferredXByNodeIndex[node.index()].reset();
        }
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

    void releaseFlowNode(u32 index) noexcept
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
        splitViewStorage.resetNode(index);
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

    [[nodiscard]] Core::Status markLayoutAndPaintDirtyBatch(
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

    [[nodiscard]] Core::Status markLayoutDirtyBatch(std::span<const UINodeId> requestedNodes)
    {
        return markLayoutAndPaintDirtyBatch(requestedNodes, {});
    }

    [[nodiscard]] Core::Status markLayoutDirtyBatch(
        std::initializer_list<UINodeId> requestedNodes)
    {
        return markLayoutDirtyBatch(
            std::span<const UINodeId>(requestedNodes.begin(), requestedNodes.size()));
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

    // Dispatches through UIStylePropertyKind static dirty metadata (UI-STYLE-001).
    // Keeps capacity/atomic dirty-queue helpers as the only mutation path.
    [[nodiscard]] Core::Status markStylePropertyDirty(UINodeId node, UIStylePropertyKind kind)
    {
        static_assert(!stylePropertyDirtiesLayout(UIStylePropertyKind::ColorOrOpacity));
        static_assert(stylePropertyDirtiesPaint(UIStylePropertyKind::ColorOrOpacity));
        static_assert(!stylePropertyDirtiesLayout(UIStylePropertyKind::ColorToken));
        static_assert(stylePropertyDirtiesPaint(UIStylePropertyKind::ColorToken));
        static_assert(stylePropertyDirtiesLayout(UIStylePropertyKind::TextStyle));
        static_assert(stylePropertyDirtiesPaint(UIStylePropertyKind::TextStyle));
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

    [[nodiscard]] Core::Result<UIStraightSrgba8Color>
    styleColorToken(UIStyleTokenId token) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        return styleSheetStorage.colorToken(token);
    }

    struct StyleTokenUpdateStatistics final {
        usize inspectedNodeCount = 0;
        usize resolvedNodeCount = 0;
        usize affectedNodeCount = 0;
        usize candidateRuleCount = 0;
    };

    [[nodiscard]] u32 tokenDependencyHead(UIStyleTokenId token,
                                          const std::pmr::vector<u32>& headByToken) const noexcept
    {
        if (!token.hasValue())
        {
            return 0;
        }
        const usize tokenSlot = token.value - 1U;
        if (tokenSlot >= headByToken.size())
        {
            return 0;
        }
        return headByToken[tokenSlot];
    }

    [[nodiscard]] Core::Status preflightStyleColorTokenDirtyQueue(
        UIStyleTokenId token, StyleTokenUpdateStatistics& statistics)
    {
        compactDirtyQueue();
        usize requiredQueueEntries = 0;
        const auto visitList = [&](u32 head, const std::pmr::vector<u32>& nextByNode,
                                   auto isSuppressed) {
            for (u32 link = head; link != 0;)
            {
                const u32 index = link - 1U;
                if (index >= nextByNode.size())
                {
                    break;
                }
                link = nextByNode[index];
                ++statistics.inspectedNodeCount;
                const NodeRecord* record = recordByIndex(index);
                if (record == nullptr || isSuppressed(index, *record))
                {
                    continue;
                }
                // Reverse index already stores the winning token; no full-tree
                // resolve or candidate-rule scan is required on the update path.
                ++statistics.resolvedNodeCount;
                ++statistics.affectedNodeCount;
                if (!dirtyQueueStorage.isQueued(index) &&
                    !dirtyQueueStorage.isReserved(index))
                {
                    ++requiredQueueEntries;
                }
            }
        };
        visitList(tokenDependencyHead(token, styleTokenDependencyHeadByTokenIndex),
                  styleTokenDependencyNextByNodeIndex,
                  [this](u32 index, const NodeRecord& record) {
                      return hasLocalBoxFillOverride(index, record);
                  });
        visitList(tokenDependencyHead(token, imageTintTokenDependencyHeadByTokenIndex),
                  imageTintTokenDependencyNextByNodeIndex,
                  [this](u32 index, const NodeRecord&) {
                      return hasLocalImageTintOverride(index);
                  });

        const usize occupiedQueueEntries = occupiedDirtyQueueSlotCount();
        if (occupiedQueueEntries > dirtyQueueStorage.queueCapacity() ||
            requiredQueueEntries >
                dirtyQueueStorage.queueCapacity() - occupiedQueueEntries)
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI style token update exceeds dirty queue capacity");
        }
        return Core::success();
    }

    void publishStyleColorTokenDirtyState(UIStyleTokenId token) noexcept
    {
        bool changed = false;
        const auto publishList = [&](u32 head, const std::pmr::vector<u32>& nextByNode,
                                     auto isSuppressed) {
            for (u32 link = head; link != 0;)
            {
                const u32 index = link - 1U;
                if (index >= nextByNode.size())
                {
                    break;
                }
                link = nextByNode[index];
                const NodeRecord* record = recordByIndex(index);
                if (record == nullptr || isSuppressed(index, *record))
                {
                    continue;
                }
                if (!dirtyQueueStorage.isQueued(index))
                {
                    dirtyQueueStorage.enqueue(idForIndex(index));
                }
                dirtyQueueStorage.flags(index) |= UIDirty::Paint;
                changed = true;
            }
        };
        publishList(tokenDependencyHead(token, styleTokenDependencyHeadByTokenIndex),
                    styleTokenDependencyNextByNodeIndex,
                    [this](u32 index, const NodeRecord& record) {
                        return hasLocalBoxFillOverride(index, record);
                    });
        publishList(tokenDependencyHead(token, imageTintTokenDependencyHeadByTokenIndex),
                    imageTintTokenDependencyNextByNodeIndex,
                    [this](u32 index, const NodeRecord&) {
                        return hasLocalImageTintOverride(index);
                    });
        if (changed)
        {
            phaseDirty |= PhasePaint;
        }
    }

    [[nodiscard]] Core::Status setStyleColorToken(
        UIStyleTokenId token, UIStraightSrgba8Color value)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();

        auto current = styleSheetStorage.colorToken(token);
        if (!current)
        {
            return Core::failure(current.error());
        }
        lastStyleTokenUpdateInspectedNodeCount = 0;
        lastStyleTokenUpdateResolvedNodeCount = 0;
        lastStyleTokenUpdateAffectedNodeCount = 0;
        lastStyleTokenUpdateCandidateRuleCount = 0;
        if (*current == value)
        {
            return Core::success();
        }

        StyleTokenUpdateStatistics statistics{};
        const Core::Status capacity =
            preflightStyleColorTokenDirtyQueue(token, statistics);
        lastStyleTokenUpdateInspectedNodeCount = statistics.inspectedNodeCount;
        lastStyleTokenUpdateResolvedNodeCount = statistics.resolvedNodeCount;
        lastStyleTokenUpdateAffectedNodeCount = statistics.affectedNodeCount;
        lastStyleTokenUpdateCandidateRuleCount = statistics.candidateRuleCount;
        if (!capacity)
        {
            return capacity;
        }
        if (Core::Status update = styleSheetStorage.setColorToken(token, value);
            !update)
        {
            return update;
        }
        publishStyleColorTokenDirtyState(token);
        return Core::success();
    }

    [[nodiscard]] Core::MonotonicTimePoint motionNow() const noexcept
    {
        const Core::IMonotonicClock* clock =
            motionClock != nullptr ? motionClock : &motionDefaultClock;
        return clock->now();
    }

    [[nodiscard]] Core::Status setMotionClock(const Core::IMonotonicClock* clock)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        motionClock = clock != nullptr ? clock : &motionDefaultClock;
        return Core::success();
    }

    [[nodiscard]] Core::Status setReducedMotion(bool enabled)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        if (reducedMotionEnabled == enabled)
        {
            return Core::success();
        }
        if (!enabled)
        {
            reducedMotionEnabled = false;
            return Core::success();
        }
        if (Core::Status collected = collectActiveTimelineDirtyNodes(); !collected)
        {
            return collected;
        }
        if (Core::Status dirty = markCollectedTimelineDirtyNodes(); !dirty)
        {
            return dirty;
        }
        reducedMotionEnabled = true;
        // Snap every active track to target and free slots (ADR: reduced-motion
        // does not keep tracks on the active list after the call).
        motionTrackStorage.snapAllActive();
        for (const Detail::UIMotionTrackStorage::Completed& completed :
             motionTrackStorage.lastCompleted())
        {
            if (!contains(completed.node))
            {
                continue;
            }
            applyCompletedMotion(completed);
        }
        timelineStorage.snapAllActive();
        const auto timelineTargets = timelineStorage.lastTargets();
        for (const Detail::UIKeyframeTimelineStorage::Target& target : timelineTargets)
        {
            applyTimelineTarget(target);
        }
        if (!motionTrackStorage.lastCompleted().empty() || !timelineTargets.empty())
        {
            phaseDirty |= PhasePaint;
        }
        return Core::success();
    }

    [[nodiscard]] bool reducedMotion() const noexcept
    {
        return reducedMotionEnabled;
    }

    [[nodiscard]] Core::Status setStyleBackgroundColorTransition(const UITransitionSpec& spec)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (spec.property != UIAnimatableProperty::BackgroundColor)
        {
            return fail(UIErrorCode::InvalidStyle, "Style background transition only supports BackgroundColor");
        }
        if (!isValidUIEasing(spec.easing))
        {
            return fail(UIErrorCode::InvalidStyle, "Style background transition easing is invalid");
        }
        if (spec.duration.count() < 0.0 || spec.delay.count() < 0.0 || !std::isfinite(spec.duration.count()) ||
            !std::isfinite(spec.delay.count()))
        {
            return fail(UIErrorCode::InvalidStyle,
                        "Style background transition duration/delay must be finite and non-negative");
        }
        const bool wasEnabled = styleBackgroundTransitionEnabled();
        const bool willEnable = spec.duration.count() > 0.0;
        if (!wasEnabled && willEnable)
        {
            usize requiredSlots = 0;
            for (u32 index = 0; index < idsByIndex.size(); ++index)
            {
                const UINodeId node = idForIndex(index);
                if (!contains(node) ||
                    !styleSheetStorage.hasStatefulBoxFillCandidateValidated(styleRolesByNodeIndex[index],
                                                                            styleClassesFor(index)) ||
                    motionTrackStorage.hasTrack(node, UIAnimatableProperty::BackgroundColor))
                {
                    continue;
                }
                ++requiredSlots;
            }
            if (requiredSlots > motionTrackStorage.availableCount())
            {
                return fail(UIErrorCode::CapacityExceeded,
                            "UI style background transitions exceed the reserved motion track capacity");
            }
            for (u32 index = 0; index < idsByIndex.size(); ++index)
            {
                const UINodeId node = idForIndex(index);
                if (!contains(node) || !styleSheetStorage.hasStatefulBoxFillCandidateValidated(
                                           styleRolesByNodeIndex[index], styleClassesFor(index)))
                {
                    continue;
                }
                if (timelineStorage.hasPresentationOwner(node, UIAnimatableProperty::BackgroundColor))
                {
                    return fail(UIErrorCode::InvalidStyle,
                                "Style motion reservation conflicts with an active keyframe timeline");
                }
                if (Core::Status reserved =
                        motionTrackStorage.reservePersistent(node, UIAnimatableProperty::BackgroundColor);
                    !reserved)
                {
                    std::terminate();
                }
            }
        } else if (wasEnabled && !willEnable)
        {
            motionTrackStorage.releaseAllPersistentReservations(UIAnimatableProperty::BackgroundColor);
        }

        styleBackgroundColorTransitionSpec = spec;
        return Core::success();
    }

    [[nodiscard]] UITransitionSpec styleBackgroundColorTransition() const noexcept
    {
        return styleBackgroundColorTransitionSpec;
    }

    void commitMotionProperty(const Detail::UIMotionTrackStorage::Completed& completed) noexcept
    {
        const u32 index = completed.node.index();
        switch (completed.property)
        {
        case UIAnimatableProperty::BackgroundColor:
            boxPaintsByIndex[index].solidFill = UISolidFill{.color = completed.color};
            detachThemeBinding(index, ThemeBindingBoxPaint);
            resolvedBoxFillCacheByNodeIndex[index] = premultiply(completed.color);
            setResolvedStyleColorTokenDependency(index, {});
            break;
        case UIAnimatableProperty::BorderColor:
            boxPaintsByIndex[index].borderLight = completed.color;
            boxPaintsByIndex[index].borderDark = completed.color;
            detachThemeBinding(index, ThemeBindingBoxPaint);
            break;
        case UIAnimatableProperty::TextColor:
            if (index < textStatesByIndex.size() && textStatesByIndex[index].hasContent)
            {
                textStatesByIndex[index].style.color = completed.color;
                detachThemeBinding(index, ThemeBindingTextStyle);
                localTextColorCacheByIndex[index] = premultiply(completed.color);
            }
            break;
        case UIAnimatableProperty::Opacity:
            if (index < presentationOpacityValidByNodeIndex.size())
            {
                presentationOpacityByNodeIndex[index] =
                    std::clamp(completed.scalar, 0.0F, 1.0F);
                presentationOpacityValidByNodeIndex[index] = 1;
            }
            break;
        case UIAnimatableProperty::CornerRadius:
            boxPaintsByIndex[index].cornerRadii = UILogicalCornerRadii::uniform(
                (std::max)(0.0F, completed.scalar));
            detachThemeBinding(index, ThemeBindingBoxPaint);
            break;
        case UIAnimatableProperty::VisualOffset:
            if (index < presentationOffsetValidByNodeIndex.size())
            {
                presentationOffsetXByNodeIndex[index] = completed.offset.x;
                presentationOffsetYByNodeIndex[index] = completed.offset.y;
                presentationOffsetValidByNodeIndex[index] = 1;
            }
            break;
        case UIAnimatableProperty::LayoutWidth:
        case UIAnimatableProperty::LayoutHeight:
        case UIAnimatableProperty::LayoutOffset:
            // Direct transition storage never creates layout tracks.
            break;
        }
    }

    void applyCompletedMotion(const Detail::UIMotionTrackStorage::Completed& completed) noexcept
    {
        if (!contains(completed.node))
        {
            return;
        }
        if (completed.completionMode == Detail::UIMotionTrackStorage::CompletionMode::StylePresentationOnly)
        {
            return;
        }

        commitMotionProperty(completed);
    }

    void applyTimelineTarget(const Detail::UIKeyframeTimelineStorage::Target& target) noexcept
    {
        if (!contains(target.node))
        {
            return;
        }
        UILayoutStyle& layout = layoutStylesByIndex[target.node.index()];
        switch (target.property)
        {
        case UIAnimatableProperty::LayoutWidth:
            layout.size.width = UILayoutLength::Px((std::max)(0.0F, target.value.scalar));
            return;
        case UIAnimatableProperty::LayoutHeight:
            layout.size.height = UILayoutLength::Px((std::max)(0.0F, target.value.scalar));
            return;
        case UIAnimatableProperty::LayoutOffset:
            layout.overlay.offset.x = UILayoutLength::Px(target.value.offset.x);
            layout.overlay.offset.y = UILayoutLength::Px(target.value.offset.y);
            return;
        case UIAnimatableProperty::BackgroundColor:
        case UIAnimatableProperty::BorderColor:
        case UIAnimatableProperty::TextColor:
        case UIAnimatableProperty::Opacity:
        case UIAnimatableProperty::CornerRadius:
        case UIAnimatableProperty::VisualOffset:
            break;
        }
        Detail::UIMotionTrackStorage::Completed completed{
            .node = target.node,
            .property = target.property,
            .completionMode = Detail::UIMotionTrackStorage::CompletionMode::CommitProperty,
        };
        switch (target.value.kind)
        {
        case UIKeyframeValueKind::Color:
            completed.valueKind = Detail::UIMotionTrackStorage::ValueKind::Color;
            completed.color = target.value.color;
            break;
        case UIKeyframeValueKind::Scalar:
            completed.valueKind = Detail::UIMotionTrackStorage::ValueKind::Scalar;
            completed.scalar = target.value.scalar;
            break;
        case UIKeyframeValueKind::Offset:
            completed.valueKind = Detail::UIMotionTrackStorage::ValueKind::Offset;
            completed.offset = {
                .x = target.value.offset.x,
                .y = target.value.offset.y,
            };
            break;
        }
        // Timeline control operations preflight and mark the complete target
        // set before mutating playback. Candidate completion already built the
        // matching paint snapshot, so target application itself stays infallible.
        commitMotionProperty(completed);
    }

    [[nodiscard]] UIStraightSrgba8Color unpremultiplyColor(UIPremultipliedRgba8Color premul) const noexcept
    {
        if (premul.alpha == 0)
        {
            return {};
        }
        const float inv = 255.0F / static_cast<float>(premul.alpha);
        return UIStraightSrgba8Color{
            .red = static_cast<u8>(std::min(255.0F, static_cast<float>(premul.red) * inv + 0.5F)),
            .green = static_cast<u8>(std::min(255.0F, static_cast<float>(premul.green) * inv + 0.5F)),
            .blue = static_cast<u8>(std::min(255.0F, static_cast<float>(premul.blue) * inv + 0.5F)),
            .alpha = premul.alpha,
        };
    }

    [[nodiscard]] Detail::UIMotionTrackStorage::NodePresentation
    motionPresentationFor(UINodeId node) const noexcept
    {
        auto presentation = motionTrackStorage.presentationFor(node);
        const auto timeline = timelineStorage.presentationFor(node);
        if (timeline.hasBackgroundColor)
        {
            presentation.hasBackgroundColor = true;
            presentation.backgroundColor = timeline.backgroundColor;
        }
        if (timeline.hasBorderColor)
        {
            presentation.hasBorderColor = true;
            presentation.borderColor = timeline.borderColor;
        }
        if (timeline.hasTextColor)
        {
            presentation.hasTextColor = true;
            presentation.textColor = timeline.textColor;
        }
        if (timeline.hasOpacity)
        {
            presentation.hasOpacity = true;
            presentation.opacity = timeline.opacity;
        }
        if (timeline.hasCornerRadius)
        {
            presentation.hasCornerRadius = true;
            presentation.cornerRadius = timeline.cornerRadius;
        }
        if (timeline.hasVisualOffset)
        {
            presentation.hasVisualOffset = true;
            presentation.visualOffset = {
                .x = timeline.visualOffset.x,
                .y = timeline.visualOffset.y,
            };
        }
        return presentation;
    }

    [[nodiscard]] UILayoutStyle presentationLayoutStyle(u32 nodeIndex) const noexcept
    {
        UILayoutStyle result = layoutStylesByIndex[nodeIndex];
        const auto presentation = timelineStorage.presentationFor(idForIndex(nodeIndex));
        if (presentation.hasLayoutWidth)
        {
            result.size.width = UILayoutLength::Px(presentation.layoutWidth);
        }
        if (presentation.hasLayoutHeight)
        {
            result.size.height = UILayoutLength::Px(presentation.layoutHeight);
        }
        if (presentation.hasLayoutOffset)
        {
            result.overlay.offset.x = UILayoutLength::Px(presentation.layoutOffset.x);
            result.overlay.offset.y = UILayoutLength::Px(presentation.layoutOffset.y);
        }
        return result;
    }

    [[nodiscard]] UIStraightSrgba8Color currentBackgroundColor(UINodeId node, u32 nodeIndex) const noexcept
    {
        const auto presentation = motionPresentationFor(node);
        if (presentation.hasBackgroundColor)
        {
            return presentation.backgroundColor;
        }
        if (nodeIndex < resolvedBoxFillCacheByNodeIndex.size())
        {
            return unpremultiplyColor(resolvedBoxFillCacheByNodeIndex[nodeIndex]);
        }
        if (boxPaintsByIndex[nodeIndex].solidFill.has_value())
        {
            return boxPaintsByIndex[nodeIndex].solidFill->color;
        }
        return {};
    }

    [[nodiscard]] UIStraightSrgba8Color currentBorderColor(UINodeId node, u32 nodeIndex) const noexcept
    {
        const auto presentation = motionPresentationFor(node);
        if (presentation.hasBorderColor)
        {
            return presentation.borderColor;
        }
        const UIBoxPaint& paint = boxPaintsByIndex[nodeIndex];
        return paint.borderLight.alpha != 0 ? paint.borderLight : paint.borderDark;
    }

    [[nodiscard]] UIStraightSrgba8Color currentTextColor(UINodeId node, u32 nodeIndex) const noexcept
    {
        const auto presentation = motionPresentationFor(node);
        if (presentation.hasTextColor)
        {
            return presentation.textColor;
        }
        if (nodeIndex < textStatesByIndex.size() && textStatesByIndex[nodeIndex].hasContent)
        {
            return textStatesByIndex[nodeIndex].style.color;
        }
        return {};
    }

    [[nodiscard]] float currentOpacity(UINodeId node, u32 nodeIndex) const noexcept
    {
        const auto presentation = motionPresentationFor(node);
        if (presentation.hasOpacity)
        {
            return presentation.opacity;
        }
        if (nodeIndex < presentationOpacityValidByNodeIndex.size() &&
            presentationOpacityValidByNodeIndex[nodeIndex] != 0)
        {
            return presentationOpacityByNodeIndex[nodeIndex];
        }
        return 1.0F;
    }

    [[nodiscard]] float currentCornerRadius(UINodeId node, u32 nodeIndex) const noexcept
    {
        const auto presentation = motionPresentationFor(node);
        if (presentation.hasCornerRadius)
        {
            return presentation.cornerRadius;
        }
        return boxPaintsByIndex[nodeIndex].cornerRadii.topLeft;
    }

    [[nodiscard]] Detail::UIMotionTrackStorage::Scalar2 currentVisualOffset(UINodeId node,
                                                                            u32 nodeIndex) const noexcept
    {
        const auto presentation = motionPresentationFor(node);
        if (presentation.hasVisualOffset)
        {
            return presentation.visualOffset;
        }
        if (nodeIndex < presentationOffsetValidByNodeIndex.size() &&
            presentationOffsetValidByNodeIndex[nodeIndex] != 0)
        {
            return {.x = presentationOffsetXByNodeIndex[nodeIndex],
                    .y = presentationOffsetYByNodeIndex[nodeIndex]};
        }
        // Default start for a new VisualOffset track is identity (no paint shift).
        // Authored drop-shadow offsets remain on UIBoxPaint.shadowOffset*.
        static_cast<void>(node);
        static_cast<void>(nodeIndex);
        return {};
    }

    [[nodiscard]] Core::Status beginColorPropertyTransition(
        UINodeId node, UIAnimatableProperty property, UIStraightSrgba8Color target,
        const UITransitionSpec& spec)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (Core::Status validation = Detail::validateTransitionSpec(spec, property);
            !validation)
        {
            return validation;
        }
        if (timelineStorage.hasPresentationOwner(node, property))
        {
            return fail(UIErrorCode::InvalidStyle,
                        "Direct UI motion conflicts with an active keyframe timeline");
        }
        const u32 index = node.index();
        UIStraightSrgba8Color start{};
        switch (property)
        {
        case UIAnimatableProperty::BackgroundColor:
            start = currentBackgroundColor(node, index);
            break;
        case UIAnimatableProperty::BorderColor:
            start = currentBorderColor(node, index);
            break;
        case UIAnimatableProperty::TextColor:
            if (index >= textStatesByIndex.size() || !textStatesByIndex[index].hasContent)
            {
                return fail(UIErrorCode::InvalidText,
                            "UI text color transition requires intrinsic text content");
            }
            start = currentTextColor(node, index);
            break;
        default:
            return fail(UIErrorCode::InvalidStyle, "UI color transition property is unsupported");
        }
        const Core::MonotonicTimePoint now = motionNow();
        if (reducedMotionEnabled || spec.duration.count() <= 0.0)
        {
            if (Core::Status dirty =
                    markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
                !dirty)
            {
                return dirty;
            }
            applyCompletedMotion(Detail::UIMotionTrackStorage::Completed{
                .node = node,
                .property = property,
                .valueKind = Detail::UIMotionTrackStorage::ValueKind::Color,
                .color = target,
            });
            motionTrackStorage.cancelActiveProperty(node, property);
            return Core::success();
        }
        if (Core::Status status = motionTrackStorage.beginOrRetargetColor(
                node, property, start, target, spec, now);
            !status)
        {
            return status;
        }
        return markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
    }

    [[nodiscard]] Core::Status beginBackgroundColorTransition(
        UINodeId node, UIStraightSrgba8Color target, const UITransitionSpec& spec)
    {
        return beginColorPropertyTransition(node, UIAnimatableProperty::BackgroundColor, target, spec);
    }

    [[nodiscard]] Core::Status beginBorderColorTransition(
        UINodeId node, UIStraightSrgba8Color target, const UITransitionSpec& spec)
    {
        return beginColorPropertyTransition(node, UIAnimatableProperty::BorderColor, target, spec);
    }

    [[nodiscard]] Core::Status beginTextColorTransition(
        UINodeId node, UIStraightSrgba8Color target, const UITransitionSpec& spec)
    {
        return beginColorPropertyTransition(node, UIAnimatableProperty::TextColor, target, spec);
    }

    [[nodiscard]] Core::Status beginOpacityTransition(
        UINodeId node, float targetOpacity, const UITransitionSpec& spec)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (Core::Status validation = Detail::validateTransitionSpec(
                spec, UIAnimatableProperty::Opacity);
            !validation)
        {
            return validation;
        }
        if (timelineStorage.hasPresentationOwner(node, UIAnimatableProperty::Opacity))
        {
            return fail(UIErrorCode::InvalidStyle,
                        "Direct UI motion conflicts with an active keyframe timeline");
        }
        if (!std::isfinite(targetOpacity))
        {
            return fail(UIErrorCode::InvalidStyle, "UI opacity target must be finite");
        }
        targetOpacity = std::clamp(targetOpacity, 0.0F, 1.0F);
        const u32 index = node.index();
        const float start = currentOpacity(node, index);
        const Core::MonotonicTimePoint now = motionNow();
        if (reducedMotionEnabled || spec.duration.count() <= 0.0)
        {
            if (Core::Status dirty =
                    markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
                !dirty)
            {
                return dirty;
            }
            applyCompletedMotion(Detail::UIMotionTrackStorage::Completed{
                .node = node,
                .property = UIAnimatableProperty::Opacity,
                .valueKind = Detail::UIMotionTrackStorage::ValueKind::Scalar,
                .scalar = targetOpacity,
            });
            motionTrackStorage.cancelActiveProperty(node, UIAnimatableProperty::Opacity);
            return Core::success();
        }
        if (Core::Status status = motionTrackStorage.beginOrRetargetScalar(
                node, UIAnimatableProperty::Opacity, start, targetOpacity, spec, now);
            !status)
        {
            return status;
        }
        return markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
    }

    [[nodiscard]] Core::Status beginCornerRadiusTransition(
        UINodeId node, float targetRadius, const UITransitionSpec& spec)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (Core::Status validation = Detail::validateTransitionSpec(
                spec, UIAnimatableProperty::CornerRadius);
            !validation)
        {
            return validation;
        }
        const u32 index = node.index();
        if (boxPaintsByIndex[index].primitive != UIBoxPrimitiveKind::Rectangle)
        {
            return fail(UIErrorCode::InvalidStyle,
                        "UI corner-radius motion requires Rectangle box paint");
        }
        if (timelineStorage.hasPresentationOwner(node, UIAnimatableProperty::CornerRadius))
        {
            return fail(UIErrorCode::InvalidStyle,
                        "Direct UI motion conflicts with an active keyframe timeline");
        }
        if (!std::isfinite(targetRadius) || targetRadius < 0.0F)
        {
            return fail(UIErrorCode::InvalidStyle, "UI corner radius target must be finite and non-negative");
        }
        const auto presentation = motionPresentationFor(node);
        if (!presentation.hasCornerRadius &&
            !boxPaintsByIndex[index].cornerRadii.isUniform())
        {
            return fail(UIErrorCode::InvalidStyle,
                        "Uniform corner-radius motion cannot start from per-corner authored radii");
        }
        const float start = currentCornerRadius(node, index);
        const Core::MonotonicTimePoint now = motionNow();
        if (reducedMotionEnabled || spec.duration.count() <= 0.0)
        {
            if (Core::Status dirty =
                    markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
                !dirty)
            {
                return dirty;
            }
            applyCompletedMotion(Detail::UIMotionTrackStorage::Completed{
                .node = node,
                .property = UIAnimatableProperty::CornerRadius,
                .valueKind = Detail::UIMotionTrackStorage::ValueKind::Scalar,
                .scalar = targetRadius,
            });
            motionTrackStorage.cancelActiveProperty(node, UIAnimatableProperty::CornerRadius);
            return Core::success();
        }
        if (Core::Status status = motionTrackStorage.beginOrRetargetScalar(
                node, UIAnimatableProperty::CornerRadius, start, targetRadius, spec, now);
            !status)
        {
            return status;
        }
        return markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
    }

    [[nodiscard]] Core::Status beginVisualOffsetTransition(
        UINodeId node, float targetOffsetX, float targetOffsetY, const UITransitionSpec& spec)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (Core::Status validation = Detail::validateTransitionSpec(
                spec, UIAnimatableProperty::VisualOffset);
            !validation)
        {
            return validation;
        }
        if (timelineStorage.hasPresentationOwner(node, UIAnimatableProperty::VisualOffset))
        {
            return fail(UIErrorCode::InvalidStyle,
                        "Direct UI motion conflicts with an active keyframe timeline");
        }
        if (!std::isfinite(targetOffsetX) || !std::isfinite(targetOffsetY))
        {
            return fail(UIErrorCode::InvalidStyle, "UI visual offset target must be finite");
        }
        const u32 index = node.index();
        const auto start = currentVisualOffset(node, index);
        const Core::MonotonicTimePoint now = motionNow();
        const Detail::UIMotionTrackStorage::Scalar2 target{.x = targetOffsetX, .y = targetOffsetY};
        if (reducedMotionEnabled || spec.duration.count() <= 0.0)
        {
            if (Core::Status dirty =
                    markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
                !dirty)
            {
                return dirty;
            }
            applyCompletedMotion(Detail::UIMotionTrackStorage::Completed{
                .node = node,
                .property = UIAnimatableProperty::VisualOffset,
                .valueKind = Detail::UIMotionTrackStorage::ValueKind::Offset,
                .offset = target,
            });
            motionTrackStorage.cancelActiveProperty(node, UIAnimatableProperty::VisualOffset);
            return Core::success();
        }
        if (Core::Status status = motionTrackStorage.beginOrRetargetOffset(
                node, UIAnimatableProperty::VisualOffset, start, target, spec, now);
            !status)
        {
            return status;
        }
        return markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
    }

    struct TimelineValidationContext final {
        Impl* impl = nullptr;
        UITimelineId timeline{};
    };

    struct TimelineDirtyNodeCollectionContext final {
        std::pmr::vector<UINodeId>* layoutNodes = nullptr;
        std::pmr::vector<UINodeId>* paintNodes = nullptr;
    };

    [[nodiscard]] static Core::Status collectTimelineDirtyNodeVisitor(
        void* rawContext, const Detail::UIKeyframeTimelineStorage::TrackView& track)
    {
        auto& context = *static_cast<TimelineDirtyNodeCollectionContext*>(rawContext);
        std::pmr::vector<UINodeId>* nodes = isLayoutAnimatableProperty(track.property)
                                                    ? context.layoutNodes
                                                    : context.paintNodes;
        if (nodes == nullptr || nodes->size() >= nodes->capacity())
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI timeline dirty-node scratch capacity has been exhausted");
        }
        nodes->push_back(track.node);
        return Core::success();
    }

    [[nodiscard]] static Core::Status validateTimelineTrackVisitor(
        void* rawContext, const Detail::UIKeyframeTimelineStorage::TrackView& track)
    {
        auto& context = *static_cast<TimelineValidationContext*>(rawContext);
        Impl& impl = *context.impl;
        auto nodeResult = impl.resolveNode(track.node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (Core::Status capability =
                impl.validateTimelinePlaybackPropertyCapability(
                    track.node, track.property);
            !capability)
        {
            return capability;
        }
        if (impl.motionTrackStorage.hasTrack(track.node, track.property))
        {
            return fail(UIErrorCode::InvalidStyle,
                        "UI timeline property conflicts with a direct or persistent motion owner");
        }
        if (impl.timelineStorage.hasPresentationOwner(track.node, track.property, context.timeline))
        {
            return fail(UIErrorCode::InvalidStyle,
                        "UI timeline property is already owned by another active timeline");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status validateTimelineNodes(UITimelineId timeline)
    {
        TimelineValidationContext context{.impl = this, .timeline = timeline};
        return timelineStorage.visitTracks(timeline, &context, &validateTimelineTrackVisitor);
    }

    [[nodiscard]] Core::Status collectTimelineDirtyNodes(UITimelineId timeline)
    {
        timelineLayoutNodeScratch.clear();
        timelinePaintNodeScratch.clear();
        TimelineDirtyNodeCollectionContext context{
            .layoutNodes = &timelineLayoutNodeScratch,
            .paintNodes = &timelinePaintNodeScratch,
        };
        return timelineStorage.visitTracks(
            timeline, &context, &collectTimelineDirtyNodeVisitor);
    }

    [[nodiscard]] Core::Status collectActiveTimelineDirtyNodes()
    {
        timelineLayoutNodeScratch.clear();
        timelinePaintNodeScratch.clear();
        TimelineDirtyNodeCollectionContext context{
            .layoutNodes = &timelineLayoutNodeScratch,
            .paintNodes = &timelinePaintNodeScratch,
        };
        return timelineStorage.visitActiveTracks(
            &context, &collectTimelineDirtyNodeVisitor);
    }

    [[nodiscard]] Core::Status markCollectedTimelineDirtyNodes()
    {
        return markLayoutAndPaintDirtyBatch(
            std::span<const UINodeId>(timelineLayoutNodeScratch.data(),
                                      timelineLayoutNodeScratch.size()),
            std::span<const UINodeId>(timelinePaintNodeScratch.data(),
                                      timelinePaintNodeScratch.size()));
    }

    [[nodiscard]] Core::Status validateTimelineDefinitionPropertyCapability(
        UINodeId node, UIAnimatableProperty property) const
    {
        const u32 index = node.index();
        if (property == UIAnimatableProperty::TextColor &&
            (index >= textStatesByIndex.size() || !textStatesByIndex[index].hasContent))
        {
            return fail(UIErrorCode::InvalidText,
                        "UI timeline text color track requires intrinsic text content");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status validateTimelinePlaybackPropertyCapability(
        UINodeId node, UIAnimatableProperty property) const
    {
        if (Core::Status definitionCapability =
                validateTimelineDefinitionPropertyCapability(node, property);
            !definitionCapability)
        {
            return definitionCapability;
        }
        if (property == UIAnimatableProperty::CornerRadius &&
            boxPaintsByIndex[node.index()].primitive != UIBoxPrimitiveKind::Rectangle)
        {
            return fail(UIErrorCode::InvalidStyle,
                        "UI corner-radius timeline requires Rectangle box paint");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<UITimelineId> createTimeline(const UITimelineDesc& desc)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        // Validate the borrowed descriptor before copying it. The storage
        // repeats structural/keyframe validation and performs the capacity
        // preflight before publishing any slot.
        for (const UITimelineTrackDesc& track : desc.tracks)
        {
            auto nodeResult = resolveNode(track.node);
            if (!nodeResult)
            {
                return Core::failure(nodeResult.error());
            }
            if (Core::Status capability =
                    validateTimelineDefinitionPropertyCapability(
                        track.node, track.property);
                !capability)
            {
                return Core::failure(capability.error());
            }
        }
        return timelineStorage.create(desc);
    }

    [[nodiscard]] Core::Status replaceTimeline(
        UITimelineId timeline, const UITimelineDesc& desc)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!timelineStorage.contains(timeline))
        {
            return fail(UIErrorCode::InvalidStyle,
                        "UI timeline ID is invalid, stale, or belongs to another context");
        }
        for (const UITimelineTrackDesc& track : desc.tracks)
        {
            auto nodeResult = resolveNode(track.node);
            if (!nodeResult)
            {
                return Core::failure(nodeResult.error());
            }
            if (Core::Status capability =
                    validateTimelineDefinitionPropertyCapability(
                        track.node, track.property);
                !capability)
            {
                return capability;
            }
        }
        if (Core::Status preflight = timelineStorage.preflightReplace(timeline, desc); !preflight)
        {
            return preflight;
        }
        auto wasActive = timelineStorage.isActive(timeline);
        if (!wasActive)
        {
            return Core::failure(wasActive.error());
        }
        if (*wasActive)
        {
            if (Core::Status collected = collectTimelineDirtyNodes(timeline); !collected)
            {
                return collected;
            }
            if (Core::Status dirty = markCollectedTimelineDirtyNodes(); !dirty)
            {
                return dirty;
            }
        }
        if (Core::Status status = timelineStorage.replace(timeline, desc); !status)
        {
            return status;
        }
        const auto targets = timelineStorage.lastTargets();
        for (const Detail::UIKeyframeTimelineStorage::Target& target : targets)
        {
            applyTimelineTarget(target);
        }
        if (!targets.empty())
        {
            phaseDirty |= PhasePaint;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status playTimeline(UITimelineId timeline)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!timelineStorage.contains(timeline))
        {
            return fail(UIErrorCode::InvalidStyle,
                        "UI timeline ID is invalid, stale, or belongs to another context");
        }
        if (Core::Status validation = validateTimelineNodes(timeline); !validation)
        {
            return validation;
        }
        if (!reducedMotionEnabled)
        {
            if (Core::Status preflight = timelineStorage.preflightPlay(timeline); !preflight)
            {
                return preflight;
            }
        }
        if (Core::Status collected = collectTimelineDirtyNodes(timeline); !collected)
        {
            return collected;
        }
        if (Core::Status dirty = markCollectedTimelineDirtyNodes(); !dirty)
        {
            return dirty;
        }
        if (reducedMotionEnabled)
        {
            if (Core::Status status = timelineStorage.snapToFinal(timeline); !status)
            {
                return status;
            }
        }
        else if (Core::Status status = timelineStorage.play(timeline, motionNow()); !status)
        {
            return status;
        }
        const auto targets = timelineStorage.lastTargets();
        for (const Detail::UIKeyframeTimelineStorage::Target& target : targets)
        {
            applyTimelineTarget(target);
        }
        phaseDirty |= PhasePaint;
        return Core::success();
    }

    [[nodiscard]] Core::Status cancelTimeline(UITimelineId timeline)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        auto wasActive = timelineStorage.isActive(timeline);
        if (!wasActive)
        {
            return Core::failure(wasActive.error());
        }
        if (*wasActive)
        {
            if (Core::Status collected = collectTimelineDirtyNodes(timeline); !collected)
            {
                return collected;
            }
            if (Core::Status dirty = markCollectedTimelineDirtyNodes(); !dirty)
            {
                return dirty;
            }
        }
        if (Core::Status status = timelineStorage.cancel(timeline); !status)
        {
            return status;
        }
        const auto targets = timelineStorage.lastTargets();
        for (const Detail::UIKeyframeTimelineStorage::Target& target : targets)
        {
            applyTimelineTarget(target);
        }
        if (!targets.empty())
        {
            phaseDirty |= PhasePaint;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status destroyTimeline(UITimelineId timeline)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        auto wasActive = timelineStorage.isActive(timeline);
        if (!wasActive)
        {
            return Core::failure(wasActive.error());
        }
        if (*wasActive)
        {
            if (Core::Status collected = collectTimelineDirtyNodes(timeline); !collected)
            {
                return collected;
            }
            if (Core::Status dirty = markCollectedTimelineDirtyNodes(); !dirty)
            {
                return dirty;
            }
        }
        if (Core::Status status = timelineStorage.destroy(timeline); !status)
        {
            return status;
        }
        const auto targets = timelineStorage.lastTargets();
        for (const Detail::UIKeyframeTimelineStorage::Target& target : targets)
        {
            applyTimelineTarget(target);
        }
        if (!targets.empty())
        {
            phaseDirty |= PhasePaint;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<bool> isTimelineActive(UITimelineId timeline) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        return timelineStorage.isActive(timeline);
    }

    [[nodiscard]] Core::Status sampleMotion(Core::MonotonicTimePoint now)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        const usize before = motionTrackStorage.activeCount();
        motionTrackStorage.beginCandidateSample(now);
        const usize timelineBefore = timelineStorage.activeCount();
        timelineStorage.beginCandidateSample(now);
        const auto layoutNodes = timelineStorage.candidateLayoutNodes();
        if (!layoutNodes.empty())
        {
            motionTrackStorage.ensureCandidateTransaction();
            if (Core::Status dirty = markLayoutDirtyBatch(layoutNodes); !dirty)
            {
                motionTrackStorage.rollbackCandidateSample();
                timelineStorage.discardCandidateSample();
                ++layoutTimelineCommitFailureCount;
                return dirty;
            }
            // Candidate presentation stays staged until commitLayout publishes
            // Layout, Hit, and Paint from this exact sample together.
            phaseDirty |= PhasePaint;
        }
        else
        {
            motionTrackStorage.commitCandidateSample();
            for (const Detail::UIMotionTrackStorage::Completed& completed :
                 motionTrackStorage.lastCompleted())
            {
                applyCompletedMotion(completed);
            }
            timelineStorage.commitCandidateSample();
            const auto timelineTargets = timelineStorage.lastTargets();
            for (const Detail::UIKeyframeTimelineStorage::Target& target : timelineTargets)
            {
                applyTimelineTarget(target);
            }
        }
        if (motionTrackStorage.lastSampledCount() == 0 && before == 0 &&
            timelineStorage.lastSampledTimelineCount() == 0 && timelineBefore == 0)
        {
            return Core::success();
        }
        phaseDirty |= PhasePaint;
        return Core::success();
    }

    [[nodiscard]] UIBoxPaint presentationBoxPaint(UINodeId node, u32 nodeIndex) const noexcept
    {
        UIBoxPaint paint = boxPaintsByIndex[nodeIndex];
        const auto presentation = motionPresentationFor(node);
        if (presentation.hasBorderColor)
        {
            paint.borderLight = presentation.borderColor;
            paint.borderDark = presentation.borderColor;
        }
        if (presentation.hasCornerRadius)
        {
            paint.cornerRadii = UILogicalCornerRadii::uniform(
                presentation.cornerRadius);
        }
        // VisualOffset is applied to paint worldRect (not hit/layout) by callers.
        return paint;
    }

    [[nodiscard]] UILogicalRect presentationPaintWorldRect(UINodeId node, u32 nodeIndex,
                                                           UILogicalRect worldRect) const noexcept
    {
        // Only residual/active motion offsets shift chrome; default currentVisualOffset
        // falls back to box shadow offsets for transition *starts*, but paint shift
        // must not double-count authored drop-shadow offsets.
        const auto presentation = motionPresentationFor(node);
        float dx = 0.0F;
        float dy = 0.0F;
        if (presentation.hasVisualOffset)
        {
            dx = presentation.visualOffset.x;
            dy = presentation.visualOffset.y;
        }
        else if (nodeIndex < presentationOffsetValidByNodeIndex.size() &&
                 presentationOffsetValidByNodeIndex[nodeIndex] != 0)
        {
            dx = presentationOffsetXByNodeIndex[nodeIndex];
            dy = presentationOffsetYByNodeIndex[nodeIndex];
        }
        if (dx == 0.0F && dy == 0.0F)
        {
            return worldRect;
        }
        worldRect.x = normalizeFloat(worldRect.x + dx);
        worldRect.y = normalizeFloat(worldRect.y + dy);
        return worldRect;
    }

    [[nodiscard]] UIPremultipliedRgba8Color presentationBoxFill(UINodeId node,
                                                                u32 nodeIndex) const noexcept
    {
        UIPremultipliedRgba8Color fill = resolvedBoxFillCacheByNodeIndex[nodeIndex];
        const auto presentation = motionPresentationFor(node);
        if (presentation.hasBackgroundColor)
        {
            fill = premultiply(presentation.backgroundColor);
        }
        float opacity = 1.0F;
        if (presentation.hasOpacity)
        {
            opacity = presentation.opacity;
        }
        else if (nodeIndex < presentationOpacityValidByNodeIndex.size() &&
                 presentationOpacityValidByNodeIndex[nodeIndex] != 0)
        {
            opacity = presentationOpacityByNodeIndex[nodeIndex];
        }
        if (opacity < 1.0F)
        {
            const u8 opacityByte = static_cast<u8>(std::clamp(opacity, 0.0F, 1.0F) * 255.0F + 0.5F);
            fill = applyOpacity(fill, opacityByte);
        }
        return fill;
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

    [[nodiscard]] Core::Result<UINodeId> createNode(
        BuiltinElementKind kind, UIElementBehavior behaviors,
        std::optional<UIStyleRoleId> authoredStyleRole = std::nullopt,
        std::span<const UIStyleClassId> authoredStyleClasses = {})
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

        const UIStyleRoleId styleRole = authoredStyleRole.value_or(defaultStyleRoleForKind(kind));
        const bool reserveStyleMotion =
            needsStyleBackgroundMotionReservation(styleRole, authoredStyleClasses);
        if (reserveStyleMotion && motionTrackStorage.availableCount() == 0)
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI style background transition reservation capacity has been exhausted");
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
        styleRolesByNodeIndex[node.index()] = styleRole;
        if (reserveStyleMotion)
        {
            if (Core::Status reserved =
                    motionTrackStorage.reservePersistent(node, UIAnimatableProperty::BackgroundColor);
                !reserved)
            {
                std::terminate();
            }
        }
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
        if (kind == BuiltinElementKind::Popup || kind == BuiltinElementKind::Tooltip)
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
            .hasExplicitDescription = descriptor.description.has_value(),
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
        const bool needsStyleMotion =
            needsStyleBackgroundMotionReservation(descriptor.visual.styleRole, descriptor.visual.styleClasses);
        const bool hasStyleMotion =
            motionTrackStorage.hasPersistentReservation(node, UIAnimatableProperty::BackgroundColor);
        if (needsStyleMotion && !hasStyleMotion)
        {
            if (motionTrackStorage.availableCount() == 0)
            {
                return fail(UIErrorCode::CapacityExceeded,
                            "UI style background transition reservation capacity has been exhausted");
            }
            if (Core::Status reserved =
                    motionTrackStorage.reservePersistent(node, UIAnimatableProperty::BackgroundColor);
                !reserved)
            {
                std::terminate();
            }
        } else if (!needsStyleMotion && hasStyleMotion)
        {
            motionTrackStorage.releasePersistentReservation(node, UIAnimatableProperty::BackgroundColor);
        }
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
        if (kind == BuiltinElementKind::Tooltip)
        {
            if (!descriptor.tooltip.has_value())
            {
                return fail(Core::CoreErrorCode::Internal,
                            "UI Tooltip is missing its retained configuration");
            }
            tooltipStorage.initializeTooltip(node, *descriptor.tooltip);
        }
        if (kind == BuiltinElementKind::SplitView)
        {
            if (!descriptor.splitView.has_value())
            {
                return fail(Core::CoreErrorCode::Internal,
                            "UI SplitView is missing its retained configuration");
            }
            splitViewStorage.initializeSplitView(node, *descriptor.splitView);
        }
        if (kind == BuiltinElementKind::Splitter)
        {
            if (!descriptor.splitter.has_value())
            {
                return fail(Core::CoreErrorCode::Internal,
                            "UI Splitter is missing its retained configuration");
            }
            splitViewStorage.initializeSplitter(node, *descriptor.splitter);
            Detail::UIRangeInputState* range = behaviorStateStorage.tryRangeInputState(node.index());
            if (range == nullptr)
            {
                return fail(Core::CoreErrorCode::Internal,
                            "UI Splitter is missing RangeInput behavior state");
            }
            range->minValue = 0.0F;
            range->maxValue = 1.0F;
            range->step = descriptor.splitter->keyboardStep;
            range->value = 0.5F;
        }

        if (kind == BuiltinElementKind::TextEdit)
        {
            textEditMultilineByNodeIndex[node.index()] = descriptor.textEditMultiline;
            if (descriptor.textEditMultiline.enabled)
            {
                // Reserve the authored row budget while the element is being
                // created.  The commit path is noexcept and must only resize
                // this already-reserved vector; a late PMR allocation would
                // otherwise turn a bounded capacity failure into termination.
                try
                {
                    const usize visualLineCapacity =
                        descriptor.textEditMultiline.maximumVisualLines != 0
                            ? descriptor.textEditMultiline.maximumVisualLines
                            : capacityConfig.textEditVisualLineCapacity;
                    auto& visualLines = textEditVisualLinesByNodeIndex[node.index()];
                    visualLines.reserve(visualLineCapacity);
                    auto& candidateVisualLines =
                        candidateTextEditVisualLinesByNodeIndex[node.index()];
                    candidateVisualLines.reserve(visualLineCapacity);
                }
                catch (const std::bad_alloc&)
                {
                    return fail(UIErrorCode::CapacityExceeded,
                                "UI multiline TextEdit visual-line storage could not be reserved");
                }
            }
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
        if (descriptor.visual.boxPaint.has_value() &&
            !Detail::isValidLogicalCornerRadii(
                descriptor.visual.boxPaint->cornerRadii))
        {
            return fail(UIErrorCode::InvalidElementDescriptor,
                        "UI box corner radii must be finite and non-negative");
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
        if (kind != BuiltinElementKind::Tooltip && descriptor.tooltip.has_value())
        {
            return fail(UIErrorCode::InvalidElementDescriptor,
                        "UI Tooltip configuration requires the Tooltip contract");
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
        if (kind == BuiltinElementKind::TextEdit)
        {
            const UITextEditMultilineConfig& multiline = descriptor.textEditMultiline;
            if (!std::isfinite(multiline.wheelStep) || multiline.wheelStep < 0.0F ||
                (multiline.enabled && multiline.maximumVisualLines == 0))
            {
                return fail(UIErrorCode::InvalidElementDescriptor,
                            "UI multiline TextEdit requires finite wheel step and visual-line capacity");
            }
            if (multiline.maximumBytes > (std::numeric_limits<u32>::max)())
            {
                return fail(UIErrorCode::CapacityExceeded, "UI multiline TextEdit byte limit is too large");
            }
            if (multiline.enabled &&
                static_cast<usize>(multiline.maximumVisualLines) > capacityConfig.textEditVisualLineCapacity)
            {
                return fail(UIErrorCode::CapacityExceeded,
                            "UI multiline TextEdit visual-line capacity exceeds the context budget");
            }
        }
        else if (descriptor.textEditMultiline != UITextEditMultilineConfig{})
        {
            return fail(UIErrorCode::InvalidElementDescriptor,
                        "UI multiline TextEdit configuration requires a TextEdit element");
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
            if ((kind == BuiltinElementKind::RadioButton && containsLineBreak(*descriptor.text)) ||
                (kind == BuiltinElementKind::TextEdit &&
                 (descriptor.text->find('\r') != std::string_view::npos ||
                  (!descriptor.textEditMultiline.enabled && descriptor.text->find('\n') != std::string_view::npos))))
            {
                return fail(UIErrorCode::InvalidText,
                            "UI RadioButton and single-line TextEdit accept one logical line without CR or LF");
            }
            if (kind == BuiltinElementKind::TextEdit && descriptor.textEditMultiline.enabled &&
                descriptor.textEditMultiline.maximumVisualLines != 0)
            {
                const usize hardLineCount = 1U + static_cast<usize>(std::count(
                    descriptor.text->begin(), descriptor.text->end(), '\n'));
                if (hardLineCount > descriptor.textEditMultiline.maximumVisualLines)
                {
                    return fail(UIErrorCode::CapacityExceeded,
                                "UI multiline TextEdit visual-line capacity has been exceeded");
                }
            }
        } else if (!descriptor.image.has_value() && descriptor.contentAlignment != UIContentAlignment{})
        {
            return fail(UIErrorCode::InvalidElementDescriptor,
                        "UI element content alignment requires intrinsic text content");
        }

        Core::Result<UINodeId> nodeResult =
            kind == BuiltinElementKind::ListView
                ? createListViewComposite(parent, descriptor.listView, descriptor.visual.styleRole,
                                          descriptor.visual.styleClasses)
                : kind == BuiltinElementKind::TreeView
                      ? createTreeViewComposite(parent, descriptor.treeView, descriptor.visual.styleRole,
                                                descriptor.visual.styleClasses)
                      : createChild(parent, kind, descriptor.behaviors, descriptor.visual.styleRole,
                                    descriptor.visual.styleClasses);
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
        if (kind == BuiltinElementKind::Modal)
        {
            // A newly authored Modal changes the Window interaction scope. Keep
            // Tooltip presentation out of the same pending publication rather
            // than waiting for the new Modal to become the committed barrier.
            hardDismissAllTooltipsNoFail(true);
        }
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
                std::optional<UIElementBehavior> authoredBehaviors = std::nullopt,
                std::optional<UIStyleRoleId> authoredStyleRole = std::nullopt,
                std::span<const UIStyleClassId> authoredStyleClasses = {})
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
        if (parentRecord.kind == BuiltinElementKind::Tooltip ||
            parentRecord.kind == BuiltinElementKind::Splitter)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI Tooltip and Splitter elements cannot own child elements");
        }
        if (kind == BuiltinElementKind::Splitter &&
            parentRecord.kind != BuiltinElementKind::SplitView)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI Splitter requires a SplitView parent");
        }
        if (parentRecord.kind == BuiltinElementKind::SplitView)
        {
            usize directChildCount = 0;
            for (u32 childIndex = parentRecord.firstChildIndex;
                 childIndex != InvalidNodeIndex;)
            {
                const NodeRecord* child = recordByIndex(childIndex);
                if (child == nullptr)
                {
                    break;
                }
                ++directChildCount;
                childIndex = child->nextSiblingIndex;
            }
            if (directChildCount >= 3)
            {
                return fail(UIErrorCode::InvalidParent,
                            "UI SplitView accepts exactly three direct parts");
            }
        }
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
        } else if (kind == BuiltinElementKind::Tooltip)
        {
            if (parentRecord.kind == BuiltinElementKind::Dropdown ||
                parentRecord.kind == BuiltinElementKind::Popup ||
                parentRecord.kind == BuiltinElementKind::ListView ||
                parentRecord.kind == BuiltinElementKind::TreeView)
            {
                return fail(UIErrorCode::InvalidParent,
                            "UI Tooltip requires an ordinary retained parent");
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

        auto nodeResult = createNode(kind, authoredBehaviors.value_or(defaultBehaviorsForKind(kind)),
                                     authoredStyleRole, authoredStyleClasses);
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

    [[nodiscard]] Core::Result<UINodeId>
    createListViewComposite(UINodeId parent, UIListViewCreateConfig config,
                            UIStyleRoleId authoredStyleRole,
                            std::span<const UIStyleClassId> authoredStyleClasses)
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

        auto listResult = createChild(parent, BuiltinElementKind::ListView, std::nullopt,
                                      authoredStyleRole, authoredStyleClasses);
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
            configureCollectionRowLayout(itemLayout, state.style.rowHeight);
            textStatesByIndex[item.index()].overflow = state.style.rowTextOverflow;
        }
        rollback.release();
        return listView;
    }

    [[nodiscard]] Core::Result<UINodeId>
    createTreeViewComposite(UINodeId parent, UITreeViewCreateConfig config,
                            UIStyleRoleId authoredStyleRole,
                            std::span<const UIStyleClassId> authoredStyleClasses)
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

        auto treeResult = createChild(parent, BuiltinElementKind::TreeView, std::nullopt,
                                      authoredStyleRole, authoredStyleClasses);
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
            configureCollectionRowLayout(itemLayout, state.style.rowHeight);
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

    [[nodiscard]] Core::Status validateFlowUpdaterRoot(UINodeId updaterRoot) const
    {
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI Flow requires a live updater root");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status markFlowVisibilityDirty(std::initializer_list<UINodeId> screens)
    {
        // A pending structure publication already forces a complete layout
        // rebuild, so startup registration/push needs no dirty-queue slots.
        if (isPhaseDirty(PhaseStructure))
        {
            return Core::success();
        }
        Core::Status status = markLayoutDirtyBatch(screens);
        if (!status && status.error().code == UIErrorCode::CapacityExceeded)
        {
            ++flowCapacityFailureCount;
        }
        return status;
    }

    [[nodiscard]] Core::Result<UIFlowLayerId> registerFlowLayerFromUpdater(UINodeId updaterRoot,
                                                                           UINodeId layer)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (Core::Status root = validateFlowUpdaterRoot(updaterRoot); !root)
        {
            return Core::failure(root.error());
        }
        if (!contains(layer) || !isNodeWithinRoot(updaterRoot, layer) || layer == updaterRoot ||
            layer.index() >= flowStatesByNodeIndex.size())
        {
            return fail(UIErrorCode::InvalidFlowLayer,
                        "UI Flow Layer must be a live node owned by the updater root");
        }
        const NodeRecord* layerRecord = nodes.tryGet(layer.storageId());
        if (layerRecord == nullptr || layerRecord->parentIndex != updaterRoot.index())
        {
            return fail(UIErrorCode::InvalidFlowLayer,
                        "UI Flow Layer must be a direct child of the updater root");
        }
        UIFlowNodeState& state = flowStatesByNodeIndex[layer.index()];
        if (state.kind != UIFlowNodeKind::None)
        {
            return fail(UIErrorCode::InvalidFlowLayer,
                        "UI Flow Layer node is already registered as a Layer or Screen");
        }
        if (registeredFlowLayerCount >= capacityConfig.flowLayerCapacity)
        {
            ++flowCapacityFailureCount;
            return fail(UIErrorCode::CapacityExceeded, "UI Flow Layer capacity has been exhausted");
        }

        state.kind = UIFlowNodeKind::Layer;
        ++registeredFlowLayerCount;
        flowLayerHighWater = (std::max)(flowLayerHighWater, registeredFlowLayerCount);
        return UIFlowLayerId{layer};
    }

    [[nodiscard]] Core::Result<UIFlowScreenId>
    registerFlowScreenFromUpdater(UINodeId updaterRoot, UIFlowLayerId layerId, UINodeId screen)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (Core::Status root = validateFlowUpdaterRoot(updaterRoot); !root)
        {
            return Core::failure(root.error());
        }
        const UINodeId layer = layerId.nodeId();
        if (!contains(layer) || !isNodeWithinRoot(updaterRoot, layer) ||
            layer.index() >= flowStatesByNodeIndex.size() ||
            flowStatesByNodeIndex[layer.index()].kind != UIFlowNodeKind::Layer)
        {
            return fail(UIErrorCode::InvalidFlowLayer, "UI Flow Layer identity is stale or unregistered");
        }
        if (!contains(screen) || !isNodeWithinRoot(updaterRoot, screen) || screen == updaterRoot ||
            screen.index() >= flowStatesByNodeIndex.size())
        {
            return fail(UIErrorCode::InvalidFlowScreen,
                        "UI Flow Screen must be a live node owned by the updater root");
        }
        const NodeRecord* screenRecord = nodes.tryGet(screen.storageId());
        if (screenRecord == nullptr || screenRecord->parentIndex != layer.index())
        {
            return fail(UIErrorCode::InvalidFlowScreen,
                        "UI Flow Screen must be a direct child of its Layer");
        }
        UIFlowNodeState& state = flowStatesByNodeIndex[screen.index()];
        if (state.kind != UIFlowNodeKind::None)
        {
            return fail(UIErrorCode::InvalidFlowScreen,
                        "UI Flow Screen node is already registered as a Layer or Screen");
        }
        if (registeredFlowScreenCount >= capacityConfig.flowScreenCapacity)
        {
            ++flowCapacityFailureCount;
            return fail(UIErrorCode::CapacityExceeded, "UI Flow Screen capacity has been exhausted");
        }
        if (Core::Status dirty = markFlowVisibilityDirty({screen}); !dirty)
        {
            return Core::failure(dirty.error());
        }

        state.kind = UIFlowNodeKind::Screen;
        state.layer = layer;
        ++registeredFlowScreenCount;
        flowScreenHighWater = (std::max)(flowScreenHighWater, registeredFlowScreenCount);
        return UIFlowScreenId{screen};
    }

    [[nodiscard]] Core::Status pushFlowScreenFromUpdater(UINodeId updaterRoot, UIFlowScreenId screenId)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status root = validateFlowUpdaterRoot(updaterRoot); !root)
        {
            return root;
        }
        const UINodeId screen = screenId.nodeId();
        if (!contains(screen) || !isNodeWithinRoot(updaterRoot, screen) ||
            screen.index() >= flowStatesByNodeIndex.size() ||
            flowStatesByNodeIndex[screen.index()].kind != UIFlowNodeKind::Screen)
        {
            return fail(UIErrorCode::InvalidFlowScreen, "UI Flow Screen identity is stale or unregistered");
        }
        UIFlowNodeState& screenState = flowStatesByNodeIndex[screen.index()];
        if (screenState.stacked)
        {
            return fail(UIErrorCode::InvalidFlowOperation,
                        "UI Flow Screen cannot be pushed more than once in its Layer stack");
        }
        if (!contains(screenState.layer) ||
            flowStatesByNodeIndex[screenState.layer.index()].kind != UIFlowNodeKind::Layer)
        {
            return fail(UIErrorCode::InvalidFlowLayer, "UI Flow Screen Layer identity is stale");
        }
        UIFlowNodeState& layerState = flowStatesByNodeIndex[screenState.layer.index()];
        const UINodeId previousTop = layerState.top;
        if (Core::Status dirty = markFlowVisibilityDirty({previousTop, screen}); !dirty)
        {
            return dirty;
        }

        screenState.previous = previousTop;
        screenState.next = {};
        screenState.stacked = true;
        if (contains(previousTop))
        {
            flowStatesByNodeIndex[previousTop.index()].next = screen;
        }
        else
        {
            layerState.bottom = screen;
        }
        layerState.top = screen;
        ++stackedFlowScreenCount;
        flowStackHighWater = (std::max)(flowStackHighWater, stackedFlowScreenCount);
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIFlowScreenId> popFlowScreenFromUpdater(UINodeId updaterRoot,
                                                                        UIFlowLayerId layerId)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (Core::Status root = validateFlowUpdaterRoot(updaterRoot); !root)
        {
            return Core::failure(root.error());
        }
        const UINodeId layer = layerId.nodeId();
        if (!contains(layer) || !isNodeWithinRoot(updaterRoot, layer) ||
            layer.index() >= flowStatesByNodeIndex.size() ||
            flowStatesByNodeIndex[layer.index()].kind != UIFlowNodeKind::Layer)
        {
            return fail(UIErrorCode::InvalidFlowLayer, "UI Flow Layer identity is stale or unregistered");
        }
        UIFlowNodeState& layerState = flowStatesByNodeIndex[layer.index()];
        const UINodeId popped = layerState.top;
        if (!contains(popped))
        {
            return fail(UIErrorCode::InvalidFlowOperation, "UI Flow Layer stack is empty");
        }
        UIFlowNodeState& poppedState = flowStatesByNodeIndex[popped.index()];
        const UINodeId nextTop = poppedState.previous;
        if (Core::Status dirty = markFlowVisibilityDirty({popped, nextTop}); !dirty)
        {
            return Core::failure(dirty.error());
        }

        if (contains(nextTop))
        {
            flowStatesByNodeIndex[nextTop.index()].next = {};
        }
        else
        {
            layerState.bottom = {};
        }
        layerState.top = nextTop;
        poppedState.previous = {};
        poppedState.next = {};
        poppedState.stacked = false;
        if (stackedFlowScreenCount == 0)
        {
            std::terminate();
        }
        --stackedFlowScreenCount;
        return UIFlowScreenId{popped};
    }

    [[nodiscard]] Core::Result<UIFlowScreenId> replaceFlowScreenFromUpdater(UINodeId updaterRoot,
                                                                            UIFlowScreenId replacementId)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (Core::Status root = validateFlowUpdaterRoot(updaterRoot); !root)
        {
            return Core::failure(root.error());
        }
        const UINodeId replacement = replacementId.nodeId();
        if (!contains(replacement) || !isNodeWithinRoot(updaterRoot, replacement) ||
            replacement.index() >= flowStatesByNodeIndex.size() ||
            flowStatesByNodeIndex[replacement.index()].kind != UIFlowNodeKind::Screen)
        {
            return fail(UIErrorCode::InvalidFlowScreen, "UI Flow Screen identity is stale or unregistered");
        }
        UIFlowNodeState& replacementState = flowStatesByNodeIndex[replacement.index()];
        if (replacementState.stacked)
        {
            return fail(UIErrorCode::InvalidFlowOperation,
                        "UI Flow replacement Screen is already present in its Layer stack");
        }
        if (!contains(replacementState.layer) ||
            flowStatesByNodeIndex[replacementState.layer.index()].kind != UIFlowNodeKind::Layer)
        {
            return fail(UIErrorCode::InvalidFlowLayer, "UI Flow replacement Layer identity is stale");
        }
        UIFlowNodeState& layerState = flowStatesByNodeIndex[replacementState.layer.index()];
        const UINodeId replaced = layerState.top;
        if (!contains(replaced))
        {
            return fail(UIErrorCode::InvalidFlowOperation,
                        "UI Flow replace requires a non-empty Layer stack");
        }
        UIFlowNodeState& replacedState = flowStatesByNodeIndex[replaced.index()];
        const UINodeId previous = replacedState.previous;
        if (Core::Status dirty = markFlowVisibilityDirty({replaced, replacement}); !dirty)
        {
            return Core::failure(dirty.error());
        }

        replacementState.previous = previous;
        replacementState.next = {};
        replacementState.stacked = true;
        if (contains(previous))
        {
            flowStatesByNodeIndex[previous.index()].next = replacement;
        }
        else
        {
            layerState.bottom = replacement;
        }
        layerState.top = replacement;
        replacedState.previous = {};
        replacedState.next = {};
        replacedState.stacked = false;
        return UIFlowScreenId{replaced};
    }

    [[nodiscard]] Core::Result<UIFlowScreenId> activeFlowScreenFromUpdater(UINodeId updaterRoot,
                                                                           UIFlowLayerId layerId) const
    {
        if (!isOwnerThread())
        {
            return fail(UIErrorCode::WrongOwnerThread, "UI Flow queries require the UI owner thread");
        }
        if (Core::Status root = validateFlowUpdaterRoot(updaterRoot); !root)
        {
            return Core::failure(root.error());
        }
        const UINodeId layer = layerId.nodeId();
        if (!contains(layer) || !isNodeWithinRoot(updaterRoot, layer) ||
            layer.index() >= flowStatesByNodeIndex.size() ||
            flowStatesByNodeIndex[layer.index()].kind != UIFlowNodeKind::Layer)
        {
            return fail(UIErrorCode::InvalidFlowLayer, "UI Flow Layer identity is stale or unregistered");
        }
        return UIFlowScreenId{flowStatesByNodeIndex[layer.index()].top};
    }

    [[nodiscard]] Core::Result<bool> isFlowScreenActiveFromUpdater(UINodeId updaterRoot,
                                                                   UIFlowScreenId screenId) const
    {
        if (!isOwnerThread())
        {
            return fail(UIErrorCode::WrongOwnerThread, "UI Flow queries require the UI owner thread");
        }
        if (Core::Status root = validateFlowUpdaterRoot(updaterRoot); !root)
        {
            return Core::failure(root.error());
        }
        const UINodeId screen = screenId.nodeId();
        if (!contains(screen) || !isNodeWithinRoot(updaterRoot, screen) ||
            screen.index() >= flowStatesByNodeIndex.size() ||
            flowStatesByNodeIndex[screen.index()].kind != UIFlowNodeKind::Screen)
        {
            return fail(UIErrorCode::InvalidFlowScreen, "UI Flow Screen identity is stale or unregistered");
        }
        return isActiveFlowScreenIndex(screen.index());
    }

    [[nodiscard]] Core::Status
    setFlowScreenActionFromUpdater(UINodeId updaterRoot, UIFlowScreenId screenId,
                                   UIFlowAction action, UIFlowActionCallback&& callback)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        if (routeDispatchDepth != 0 || flowActionCallbackOperationDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI Flow action registration cannot change during input routing");
        }
        drainDeferredRootDestroys();
        if (Core::Status root = validateFlowUpdaterRoot(updaterRoot); !root)
        {
            return root;
        }
        const usize actionIndex = flowActionSlotIndex(action);
        if (actionIndex >= FlowActionSlotCount || !callback.hasValue())
        {
            return fail(UIErrorCode::InvalidFlowAction,
                        "UI Flow Screen action requires a supported action and non-empty callback");
        }
        const UINodeId screen = screenId.nodeId();
        if (!contains(screen) || !isNodeWithinRoot(updaterRoot, screen) ||
            screen.index() >= flowStatesByNodeIndex.size() ||
            flowStatesByNodeIndex[screen.index()].kind != UIFlowNodeKind::Screen)
        {
            return fail(UIErrorCode::InvalidFlowScreen,
                        "UI Flow Screen identity is stale or unregistered");
        }

        UIFlowNodeState& state = flowStatesByNodeIndex[screen.index()];
        UIFlowActionSlot& actionSlot = state.actions[actionIndex];
        const bool replacing = actionSlot.registered;
        if (!replacing && registeredFlowActionCount >= capacityConfig.flowScreenCapacity)
        {
            ++flowCapacityFailureCount;
            return fail(UIErrorCode::CapacityExceeded,
                        "UI Flow Screen action capacity has been exhausted");
        }
        ++flowActionCallbackOperationDepth;
        auto callbackOperation = Core::makeScopeExit([this]() noexcept {
            --flowActionCallbackOperationDepth;
        });
        actionSlot.callback = std::move(callback);
        actionSlot.registered = true;
        if (!replacing)
        {
            ++registeredFlowActionCount;
            flowActionHighWater = (std::max)(flowActionHighWater, registeredFlowActionCount);
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status
    clearFlowScreenActionFromUpdater(UINodeId updaterRoot, UIFlowScreenId screenId,
                                     UIFlowAction action)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        if (routeDispatchDepth != 0 || flowActionCallbackOperationDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI Flow action registration cannot change during input routing");
        }
        drainDeferredRootDestroys();
        if (Core::Status root = validateFlowUpdaterRoot(updaterRoot); !root)
        {
            return root;
        }
        const usize actionIndex = flowActionSlotIndex(action);
        if (actionIndex >= FlowActionSlotCount)
        {
            return fail(UIErrorCode::InvalidFlowAction,
                        "UI Flow Screen action is not supported by this router");
        }
        const UINodeId screen = screenId.nodeId();
        if (!contains(screen) || !isNodeWithinRoot(updaterRoot, screen) ||
            screen.index() >= flowStatesByNodeIndex.size() ||
            flowStatesByNodeIndex[screen.index()].kind != UIFlowNodeKind::Screen)
        {
            return fail(UIErrorCode::InvalidFlowScreen,
                        "UI Flow Screen identity is stale or unregistered");
        }

        UIFlowNodeState& state = flowStatesByNodeIndex[screen.index()];
        UIFlowActionSlot& actionSlot = state.actions[actionIndex];
        if (actionSlot.registered)
        {
            if (registeredFlowActionCount == 0)
            {
                std::terminate();
            }
            actionSlot.registered = false;
            --registeredFlowActionCount;
            ++flowActionCallbackOperationDepth;
            auto callbackOperation = Core::makeScopeExit([this]() noexcept {
                --flowActionCallbackOperationDepth;
            });
            actionSlot.callback.reset();
        }
        return Core::success();
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
            if (tooltipStorage.releaseNode(node, motionNow()))
            {
                markTooltipPresentationDirty();
            }
            static_cast<void>(splitViewStorage.releaseNode(node));
            if (record->kind == BuiltinElementKind::Modal)
            {
                hardDismissAllTooltipsNoFail(true);
            }
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
            timelineStorage.releaseNode(node);
            const auto timelineTargets = timelineStorage.lastTargets();
            for (const Detail::UIKeyframeTimelineStorage::Target& target : timelineTargets)
            {
                applyTimelineTarget(target);
            }
            motionTrackStorage.releaseNode(node);
            releaseFlowNode(currentIndex);
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

        const NodeRecord* nodeRecord = *nodeResult;
        if (nodeRecord->kind == BuiltinElementKind::Tooltip &&
            normalizedStyle->placement != UILayoutPlacement::Overlay)
        {
            return fail(UIErrorCode::InvalidLayout,
                        "UI Tooltip nodes always require Overlay placement");
        }

        UILayoutStyle& currentStyle = layoutStylesByIndex[node.index()];
        if (currentStyle == *normalizedStyle)
        {
            return Core::success();
        }

        if (Core::Status dirtyStatus =
                markStylePropertyDirty(node, UIStylePropertyKind::LayoutStyle);
            !dirtyStatus)
        {
            return dirtyStatus;
        }
        currentStyle = *normalizedStyle;
        if (nodeRecord->kind == BuiltinElementKind::Modal ||
            normalizedStyle->visibility != UIVisibility::Visible)
        {
            // Modal scope mutations and a Hidden/Collapsed endpoint or ancestor
            // are hard dismissal barriers. The next successful commit publishes
            // the visibility/layout/hit/paint/semantics transition atomically.
            hardDismissAllTooltipsNoFail(true);
        }
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
        if ((*nodeResult)->kind == BuiltinElementKind::Tooltip &&
            policy != UIPointerHitPolicy::Ignore)
        {
            return fail(UIErrorCode::InvalidPointerPolicy,
                        "UI Tooltip nodes always ignore Pointer hit testing");
        }

        UIPointerHitPolicy& currentPolicy = pointerHitPoliciesByIndex[node.index()];
        if (currentPolicy == policy)
        {
            return Core::success();
        }
        const bool clearHover = hoveredPrimaryControl == node && policy != UIPointerHitPolicy::Targetable;
        if (clearHover)
        {
            // Hover chrome is paint-only; hit policy itself is HitTest metadata.
            if (Core::Status dirtyStatus =
                    markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
                !dirtyStatus)
            {
                return dirtyStatus;
            }
        }
        if (Core::Status dirtyStatus =
                markStylePropertyDirty(node, UIStylePropertyKind::PointerHitPolicy);
            !dirtyStatus)
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
                resetTextEditPreferredX(node);
                resetImeCompositionState();
            }
            if (tooltipForAnchor(node).hasValue())
            {
                hardDismissAllTooltipsNoFail(true);
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
        if ((*nodeResult)->kind == BuiltinElementKind::Tooltip &&
            mode != UIFocusScopeMode::None)
        {
            return fail(UIErrorCode::InvalidFocusScope,
                        "UI Tooltip nodes never establish a focus scope");
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
        const bool needsStyleMotion = needsStyleBackgroundMotionReservation(role, styleClassesFor(index));
        const bool hasStyleMotion =
            motionTrackStorage.hasPersistentReservation(node, UIAnimatableProperty::BackgroundColor);
        if (needsStyleMotion && !hasStyleMotion &&
            !motionTrackStorage.hasTrack(node, UIAnimatableProperty::BackgroundColor) &&
            motionTrackStorage.availableCount() == 0)
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI style role requires an unavailable background transition reservation");
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

        motionTrackStorage.cancelActiveProperty(node, UIAnimatableProperty::BackgroundColor);
        if (needsStyleMotion && !hasStyleMotion)
        {
            if (Core::Status reserved =
                    motionTrackStorage.reservePersistent(node, UIAnimatableProperty::BackgroundColor);
                !reserved)
            {
                std::terminate();
            }
        } else if (!needsStyleMotion && hasStyleMotion)
        {
            motionTrackStorage.releasePersistentReservation(node, UIAnimatableProperty::BackgroundColor);
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
        if ((overridesToClear & boxFillOverrideMask(**nodeResult)) != 0 ||
            (overridesToClear & static_cast<u16>(UIStyleOverride::ImageTint)) != 0)
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
        if ((overridesToClear & static_cast<u16>(UIStyleOverride::ImageTint)) != 0)
        {
            static_cast<void>(refreshResolvedStyleCache(index));
        }
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
        if (!Detail::isValidLogicalCornerRadii(paint.cornerRadii))
        {
            return fail(UIErrorCode::InvalidStyle,
                        "UI box corner radii must be finite and non-negative");
        }
        if (timelineStorage.hasPresentationOwner(
                node, UIAnimatableProperty::BackgroundColor) ||
            timelineStorage.hasPresentationOwner(node, UIAnimatableProperty::BorderColor) ||
            timelineStorage.hasPresentationOwner(node, UIAnimatableProperty::CornerRadius))
        {
            return fail(UIErrorCode::InvalidStyle,
                        "UI box paint setter conflicts with an active keyframe timeline");
        }

        const UIBoxPaint normalizedPaint = Detail::normalizeBoxPaint(paint);
        UIBoxPaint& currentPaint = boxPaintsByIndex[node.index()];
        const bool hasLocalOverride =
            (styleOverridesByNodeIndex[node.index()] &
             static_cast<u16>(UIStyleOverride::BoxPaint)) != 0;
        const bool hasActiveBackgroundMotion =
            motionTrackStorage.findActive(node, UIAnimatableProperty::BackgroundColor) != nullptr;
        const bool hasActiveBorderMotion =
            motionTrackStorage.findActive(node, UIAnimatableProperty::BorderColor) != nullptr;
        const bool hasActiveCornerRadiusMotion =
            motionTrackStorage.findActive(node, UIAnimatableProperty::CornerRadius) != nullptr;
        const bool hasActiveBoxPaintMotion =
            hasActiveBackgroundMotion || hasActiveBorderMotion || hasActiveCornerRadiusMotion;
        if (currentPaint == normalizedPaint && hasLocalOverride && !hasActiveBoxPaintMotion)
        {
            return Core::success();
        }
        if (Core::Status dirtyStatus =
                markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
            !dirtyStatus)
        {
            return dirtyStatus;
        }
        motionTrackStorage.cancelActiveProperty(node, UIAnimatableProperty::BackgroundColor);
        motionTrackStorage.cancelActiveProperty(node, UIAnimatableProperty::BorderColor);
        motionTrackStorage.cancelActiveProperty(node, UIAnimatableProperty::CornerRadius);
        if (currentPaint != normalizedPaint)
        {
            currentPaint = normalizedPaint;
        }
        detachThemeBinding(node.index(), ThemeBindingBoxPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Status setImageTintFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                       UIStraightSrgba8Color tint)
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
        const UIImageContent* current = imageContentStorage.get(node.index());
        if (current == nullptr)
        {
            return fail(UIErrorCode::InvalidElementDescriptor,
                        "UI image tint requires retained image content");
        }
        const u32 index = node.index();
        const UIStraightSrgba8Color previousResolved = resolvedImageTintColor(index, *current);
        const bool alreadyLocal = hasLocalImageTintOverride(index);
        // Effective color unchanged: still detach sheet if needed, but no paint dirty.
        if (previousResolved == tint)
        {
            if (!alreadyLocal)
            {
                styleOverridesByNodeIndex[index] |= static_cast<u16>(UIStyleOverride::ImageTint);
                setResolvedImageTintTokenDependency(index, {});
                if (index < resolvedImageTintValidByNodeIndex.size())
                {
                    resolvedImageTintValidByNodeIndex[index] = 0;
                    resolvedImageTintCacheByNodeIndex[index] = {};
                }
                if (current->tint != tint)
                {
                    return imageContentStorage.setTint(index, tint);
                }
            }
            return Core::success();
        }
        // Dirty metadata: ImageTint → ColorOrOpacity (Paint only, no Measure).
        // Local setter detaches stylesheet image tint (override wins).
        if (Core::Status dirtyStatus =
                markStylePropertyDirty(node, UIStylePropertyKind::ColorOrOpacity);
            !dirtyStatus)
        {
            return dirtyStatus;
        }
        if (Core::Status setTint = imageContentStorage.setTint(index, tint); !setTint)
        {
            return setTint;
        }
        styleOverridesByNodeIndex[index] |= static_cast<u16>(UIStyleOverride::ImageTint);
        setResolvedImageTintTokenDependency(index, {});
        if (index < resolvedImageTintValidByNodeIndex.size())
        {
            resolvedImageTintValidByNodeIndex[index] = 0;
            resolvedImageTintCacheByNodeIndex[index] = {};
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIStraightSrgba8Color> imageTintFromUpdater(UINodeId updaterRoot,
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
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }
        const UIImageContent* current = imageContentStorage.get(node.index());
        if (current == nullptr)
        {
            return fail(UIErrorCode::InvalidElementDescriptor,
                        "UI image tint requires retained image content");
        }
        return resolvedImageTintColor(node.index(), *current);
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
        const UITextEditMultilineConfig multiline =
            node.index() < textEditMultilineByNodeIndex.size()
                ? textEditMultilineByNodeIndex[node.index()] : UITextEditMultilineConfig{};
        if ((record->kind == BuiltinElementKind::RadioButton && containsLineBreak(utf8)) ||
            (utf8.find('\r') != std::string_view::npos) ||
            (record->kind == BuiltinElementKind::TextEdit && !multiline.enabled &&
             utf8.find('\n') != std::string_view::npos))
        {
            return fail(UIErrorCode::InvalidText,
                        "UI RadioButton and single-line TextEdit accept one logical line without CR or LF");
        }
        if (record->kind == BuiltinElementKind::TextEdit && multiline.maximumBytes != 0 &&
            utf8.size() > multiline.maximumBytes)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI multiline TextEdit byte capacity has been exceeded");
        }
        if (record->kind == BuiltinElementKind::TextEdit && multiline.enabled &&
            multiline.maximumVisualLines != 0)
        {
            const usize hardLineCount = 1U + static_cast<usize>(std::count(utf8.begin(), utf8.end(), '\n'));
            if (hardLineCount > multiline.maximumVisualLines)
            {
                return fail(UIErrorCode::CapacityExceeded,
                            "UI multiline TextEdit visual-line capacity has been exceeded");
            }
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
            if (record->kind == BuiltinElementKind::TextEdit &&
                node.index() < textEditPreferredXByNodeIndex.size())
            {
                textEditPreferredXByNodeIndex[node.index()].reset();
            }
            if (record->kind == BuiltinElementKind::TextEdit &&
                node.index() < textEditCaretAffinityByNodeIndex.size())
            {
                textEditCaretAffinityByNodeIndex[node.index()] =
                    Detail::UITextEditCaretAffinity::Downstream;
            }
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
        if (record->kind == BuiltinElementKind::TextEdit &&
            node.index() < textEditPreferredXByNodeIndex.size())
        {
            textEditPreferredXByNodeIndex[node.index()].reset();
        }
        if (record->kind == BuiltinElementKind::TextEdit &&
            node.index() < textEditCaretAffinityByNodeIndex.size())
        {
            textEditCaretAffinityByNodeIndex[node.index()] =
                Detail::UITextEditCaretAffinity::Downstream;
        }
        if (Detail::UITextInputState* textInputState =
                behaviorStateStorage.tryTextInputState(node.index());
            textInputState != nullptr)
        {
            UITextSelection& selection = textInputState->selection;
            selection.anchorCodepoint = Detail::nearestGraphemeBoundary(
                utf8, (std::min)(selection.anchorCodepoint, metrics->codepointCount));
            selection.caretCodepoint = Detail::nearestGraphemeBoundary(
                utf8, (std::min)(selection.caretCodepoint, metrics->codepointCount));
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

        if (Core::Status dirtyStatus =
                markStylePropertyDirty(node, UIStylePropertyKind::TextStyle);
            !dirtyStatus)
        {
            return dirtyStatus;
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
        if (Core::Status dirtyStatus =
                markStylePropertyDirty(node, UIStylePropertyKind::ContentAlignment);
            !dirtyStatus)
        {
            return dirtyStatus;
        }
        state.alignment = alignment;
        return Core::success();
    }

    [[nodiscard]] Core::Status setTextOverflowFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                          UITextOverflow overflow)
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
            return fail(UIErrorCode::InvalidText, "UI text overflow requires an element with intrinsic text");
        }
        if (overflow != UITextOverflow::Clip && overflow != UITextOverflow::Ellipsis)
        {
            return fail(UIErrorCode::InvalidText, "UI text overflow must be Clip or Ellipsis");
        }

        WidgetTextState& state = textStatesByIndex[node.index()];
        if (state.overflow == overflow)
        {
            return Core::success();
        }
        // Paint only: intrinsic metrics keep describing the untruncated text, so
        // neither layout nor the accessibility name changes.
        if (Core::Status dirtyStatus =
                markStylePropertyDirty(node, UIStylePropertyKind::TextOverflow);
            !dirtyStatus)
        {
            return dirtyStatus;
        }
        state.overflow = overflow;
        return Core::success();
    }

    [[nodiscard]] Core::Result<UITextOverflow> textOverflowFromUpdater(
        UINodeId updaterRoot, UINodeId node)
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
            return fail(UIErrorCode::InvalidText, "UI text overflow requires an element with intrinsic text");
        }
        return textStatesByIndex[node.index()].overflow;
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
        const std::string_view text = textViewFor(textEdit.index());
        if (!Detail::isGraphemeBoundary(text, selection.anchorCodepoint) ||
            !Detail::isGraphemeBoundary(text, selection.caretCodepoint))
        {
            return fail(UIErrorCode::InvalidText,
                        "UI TextEdit selection must align to grapheme boundaries");
        }
        const bool affinityChanged =
            textEdit.index() < textEditCaretAffinityByNodeIndex.size() &&
            textEditCaretAffinityByNodeIndex[textEdit.index()] !=
                Detail::UITextEditCaretAffinity::Downstream;
        if (state->selection == selection && !affinityChanged)
        {
            if (textEdit.index() < textEditPreferredXByNodeIndex.size())
            {
                textEditPreferredXByNodeIndex[textEdit.index()].reset();
            }
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
        if (textEdit.index() < textEditPreferredXByNodeIndex.size())
        {
            textEditPreferredXByNodeIndex[textEdit.index()].reset();
        }
        if (textEdit.index() < textEditCaretAffinityByNodeIndex.size())
        {
            textEditCaretAffinityByNodeIndex[textEdit.index()] =
                Detail::UITextEditCaretAffinity::Downstream;
        }
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

    [[nodiscard]] bool isPointerAdjustableRangeInput(UINodeId node) const noexcept
    {
        if (!isInteractiveRangeInput(node))
        {
            return false;
        }
        const NodeRecord* record = nodes.tryGet(node.storageId());
        return record != nullptr &&
               (record->kind == BuiltinElementKind::Slider ||
                (record->kind == BuiltinElementKind::Splitter &&
                 splitViewStorage.splitViewForSplitter(node).hasValue()));
    }

    [[nodiscard]] Core::Result<bool> applySliderValue(UINodeId slider, double requestedValue,
                                                       Platform::PlatformFrameId platformFrame,
                                                       u64 sourceSequence, bool requireInteractive,
                                                       bool quantizeToStep = true)
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
        const float next = quantizeToStep
                               ? quantizeSliderValue(requestedValue, state->minValue,
                                                     state->maxValue, state->step)
                               : static_cast<float>(std::clamp(
                                     requestedValue, static_cast<double>(state->minValue),
                                     static_cast<double>(state->maxValue)));
        if (next == state->value)
        {
            return false;
        }
        const bool splitter = record->kind == BuiltinElementKind::Splitter;
        const UINodeId splitView = splitter ? splitViewStorage.splitViewForSplitter(slider)
                                            : UINodeId{};
        if (splitter && !splitView.hasValue())
        {
            return false;
        }
        Core::Status dirty = splitter ? markLayoutStyleDirty(splitView)
                                      : markPaintDirty(slider);
        if (!dirty)
        {
            return Core::failure(dirty.error());
        }
        state->value = next;
        if (splitter)
        {
            splitViewStorage.setRequestedFraction(splitView, next);
        }
        else
        {
            invokeSliderChangeCallback(captureSliderChangeCallback(slider), UISliderChangeEvent{
                                                                             .sliderNode = slider,
                                                                             .value = next,
                                                                             .platformFrame = platformFrame,
                                                                             .sourceSequence = sourceSequence,
                                                                         });
        }
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

    [[nodiscard]] Core::Result<bool> applySplitterValueFromPointer(
        UINodeId splitter, UILogicalPoint position,
        Platform::PlatformFrameId platformFrame, u64 sourceSequence)
    {
        const UINodeId splitView = splitViewStorage.splitViewForSplitter(splitter);
        const SplitViewState* state = splitViewStorage.trySplitView(splitView);
        const Detail::UIRangeInputState* range =
            behaviorStateStorage.tryRangeInputState(splitter.index());
        if (state == nullptr || range == nullptr || !isInteractiveRangeInput(splitter))
        {
            return false;
        }
        const UISplitViewMetrics metrics = splitViewStorage.committedMetrics(splitView);
        if (metrics.splitterRect.width <= 0.0F || metrics.splitterRect.height <= 0.0F)
        {
            return false;
        }
        UILogicalRect contentRect{};
        if (metrics.orientation == UISplitViewOrientation::Horizontal)
        {
            contentRect = {
                metrics.primaryRect.x,
                metrics.primaryRect.y,
                metrics.primaryRect.width + metrics.splitterRect.width +
                    metrics.secondaryRect.width,
                metrics.primaryRect.height,
            };
        }
        else
        {
            contentRect = {
                metrics.primaryRect.x,
                metrics.primaryRect.y,
                metrics.primaryRect.width,
                metrics.primaryRect.height + metrics.splitterRect.height +
                    metrics.secondaryRect.height,
            };
        }
        const auto fraction = resolveSplitViewFractionFromPointer(
            contentRect, state->config, position, splitterDragGrabOffset);
        if (!fraction.has_value())
        {
            return false;
        }
        return applySliderValue(splitter, *fraction, platformFrame, sourceSequence, true,
                                false);
    }

    [[nodiscard]] Core::Result<bool> applyRangeInputValueFromPointer(
        UINodeId node, UILogicalPoint position,
        Platform::PlatformFrameId platformFrame, u64 sourceSequence)
    {
        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr)
        {
            return false;
        }
        if (record->kind == BuiltinElementKind::Splitter)
        {
            return applySplitterValueFromPointer(node, position, platformFrame, sourceSequence);
        }
        return applySliderValueFromPointer(node, position, platformFrame, sourceSequence);
    }

    [[nodiscard]] Detail::UITextEditVisualHit textEditHitFromPointer(
        UINodeId textEdit, UILogicalPoint position) const noexcept
    {
        if (!isLiveTextEdit(textEdit))
        {
            return {};
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
        if (textEdit.index() < textEditMultilineByNodeIndex.size() &&
            textEditMultilineByNodeIndex[textEdit.index()].enabled &&
            textEdit.index() < textEditVisualLayoutsByNodeIndex.size() &&
            textEditVisualLayoutsByNodeIndex[textEdit.index()].lineCount != 0)
        {
            const float relativeX = position.x - placement.origin.x;
            const float relativeY = position.y - placement.origin.y;
            const WidgetTextState& textState = textStatesByIndex[textEdit.index()];
            const float fallbackAdvance = textState.style.logicalSize * textState.style.advanceScale;
            std::span<const UITextGlyphRaster> glyphs{};
            if (textRasterizer && textFace.hasValue())
            {
                auto raster = textRasterizer->raster(textFace, textViewFor(textEdit.index()), textState.style);
                if (raster) { glyphs = raster->glyphs; }
            }
            return Detail::textEditHitFromVisualPosition(
                textViewFor(textEdit.index()), relativeX, relativeY,
                textEditScrollYByNodeIndex[textEdit.index()],
                textEditVisualLayoutsByNodeIndex[textEdit.index()],
                textEditVisualLinesByNodeIndex[textEdit.index()], fallbackAdvance, glyphs);
        }
        const WidgetTextState& textState = textStatesByIndex[textEdit.index()];
        const u32 codepointCount = textState.metrics.codepointCount;
        if (!foundPlacement || codepointCount == 0)
        {
            return {};
        }
        const float relativeX = position.x - placement.origin.x;
        const float fallbackAdvance =
            textState.style.logicalSize * textState.style.advanceScale;

        if (textRasterizer && textFace.hasValue())
        {
            auto raster = textRasterizer->raster(textFace, textViewFor(textEdit.index()), textState.style);
            if (raster)
            {
                return {
                    .codepoint = textEditCodepointFromHorizontalPosition(
                        textViewFor(textEdit.index()), relativeX, fallbackAdvance,
                        raster->glyphs),
                };
            }
        }
        return {
            .codepoint = textEditCodepointFromHorizontalPosition(
                textViewFor(textEdit.index()), relativeX, fallbackAdvance),
        };
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
        const Detail::UITextEditVisualHit hit =
            textEditHitFromPointer(textEdit, position);
        next.caretCodepoint = hit.codepoint;
        if (!extendSelection)
        {
            next.anchorCodepoint = next.caretCodepoint;
        }
        const bool affinityChanged =
            textEdit.index() < textEditCaretAffinityByNodeIndex.size() &&
            textEditCaretAffinityByNodeIndex[textEdit.index()] != hit.affinity;
        if (next == state->selection && !affinityChanged)
        {
            if (textEdit.index() < textEditPreferredXByNodeIndex.size())
            {
                textEditPreferredXByNodeIndex[textEdit.index()].reset();
            }
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
        // Pointer caret placement resets preferred-X.
        if (textEdit.index() < textEditPreferredXByNodeIndex.size())
        {
            textEditPreferredXByNodeIndex[textEdit.index()].reset();
        }
        if (textEdit.index() < textEditCaretAffinityByNodeIndex.size())
        {
            textEditCaretAffinityByNodeIndex[textEdit.index()] = hit.affinity;
        }
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
        const NodeRecord* rangeRecord = nodes.tryGet(slider.storageId());
        if (rangeRecord != nullptr && rangeRecord->kind == BuiltinElementKind::Splitter &&
            (minValue != 0.0F || maxValue != 1.0F))
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI Splitter RangeInput is fixed to zero through one");
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

    [[nodiscard]] Core::Result<NodeRecord*> resolveSplitView(UINodeId splitView)
    {
        auto nodeResult = resolveNode(splitView);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != BuiltinElementKind::SplitView ||
            !splitViewStorage.containsSplitView(splitView))
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI SplitView API requires a SplitView node");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveSplitter(UINodeId splitter)
    {
        auto nodeResult = resolveNode(splitter);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != BuiltinElementKind::Splitter ||
            !splitViewStorage.containsSplitter(splitter))
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI Splitter API requires a Splitter node");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Status validateSplitViewUpdaterRoot(
        UINodeId updaterRoot, UINodeId splitView) const
    {
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired,
                        "UI tree updater requires a live root owner");
        }
        auto splitViewResult = const_cast<Impl*>(this)->resolveSplitView(splitView);
        if (!splitViewResult)
        {
            return Core::failure(splitViewResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, splitView))
        {
            return fail(UIErrorCode::InvalidNode,
                        "UI SplitView is not owned by the updater root");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status setSplitViewPartsFromUpdater(
        UINodeId updaterRoot, UINodeId splitView, UINodeId primaryPane,
        UINodeId splitter, UINodeId secondaryPane)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateSplitViewUpdaterRoot(updaterRoot, splitView); !valid)
        {
            return valid;
        }
        if (!primaryPane.hasValue() || !splitter.hasValue() || !secondaryPane.hasValue() ||
            primaryPane == splitter || primaryPane == secondaryPane || splitter == secondaryPane ||
            splitView == primaryPane || splitView == splitter || splitView == secondaryPane)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI SplitView parts must be four distinct live nodes");
        }
        auto primaryResult = resolveNode(primaryPane);
        auto splitterResult = resolveSplitter(splitter);
        auto secondaryResult = resolveNode(secondaryPane);
        if (!primaryResult)
        {
            return Core::failure(primaryResult.error());
        }
        if (!splitterResult)
        {
            return Core::failure(splitterResult.error());
        }
        if (!secondaryResult)
        {
            return Core::failure(secondaryResult.error());
        }
        const NodeRecord* splitViewRecord = nodes.tryGet(splitView.storageId());
        const NodeRecord* primaryRecord = *primaryResult;
        const NodeRecord* splitterRecord = *splitterResult;
        const NodeRecord* secondaryRecord = *secondaryResult;
        if (splitViewRecord == nullptr || primaryRecord->rootIndex != splitViewRecord->rootIndex ||
            splitterRecord->rootIndex != splitViewRecord->rootIndex ||
            secondaryRecord->rootIndex != splitViewRecord->rootIndex)
        {
            return fail(UIErrorCode::WrongContext,
                        "UI SplitView and all parts must belong to the same root");
        }
        if (primaryRecord->parentIndex != splitView.index() ||
            splitterRecord->parentIndex != splitView.index() ||
            secondaryRecord->parentIndex != splitView.index())
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI SplitView parts must be direct children of the SplitView");
        }
        if (primaryRecord->kind == BuiltinElementKind::Splitter ||
            secondaryRecord->kind == BuiltinElementKind::Splitter ||
            layoutStylesByIndex[primaryPane.index()].placement != UILayoutPlacement::Flow ||
            layoutStylesByIndex[splitter.index()].placement != UILayoutPlacement::Flow ||
            layoutStylesByIndex[secondaryPane.index()].placement != UILayoutPlacement::Flow)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI SplitView panes require ordinary Flow children and one Splitter");
        }
        usize directChildCount = 0;
        for (u32 childIndex = splitViewRecord->firstChildIndex;
             childIndex != InvalidNodeIndex;)
        {
            const NodeRecord* child = recordByIndex(childIndex);
            if (child == nullptr)
            {
                break;
            }
            ++directChildCount;
            childIndex = child->nextSiblingIndex;
        }
        if (directChildCount != 3)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI SplitView requires exactly three direct children");
        }
        for (const UINodeId part : {primaryPane, splitter, secondaryPane})
        {
            const UINodeId existingOwner = splitViewStorage.splitViewForPart(part);
            if (existingOwner.hasValue() && existingOwner != splitView)
            {
                return fail(UIErrorCode::InvalidParent,
                            "UI SplitView part already belongs to another SplitView");
            }
        }

        const UISplitViewParts nextParts{primaryPane, splitter, secondaryPane};
        if (splitViewStorage.parts(splitView) == nextParts)
        {
            return Core::success();
        }
        if (Core::Status dirty = markLayoutStyleDirty(splitView); !dirty)
        {
            return dirty;
        }
        splitViewStorage.linkValidated(splitView, nextParts);
        if (Detail::UIRangeInputState* range =
                behaviorStateStorage.tryRangeInputState(splitter.index());
            range != nullptr)
        {
            const SplitterState* splitterState = splitViewStorage.trySplitter(splitter);
            range->minValue = 0.0F;
            range->maxValue = 1.0F;
            range->step = splitterState != nullptr ? splitterState->config.keyboardStep : 0.02F;
            range->value = splitViewStorage.requestedFraction(splitView);
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status clearSplitViewPartsFromUpdater(
        UINodeId updaterRoot, UINodeId splitView)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateSplitViewUpdaterRoot(updaterRoot, splitView); !valid)
        {
            return valid;
        }
        const UISplitViewParts previous = splitViewStorage.parts(splitView);
        if (!previous.hasValue())
        {
            return Core::success();
        }
        if (Core::Status dirty = markLayoutStyleDirty(splitView); !dirty)
        {
            return dirty;
        }
        if (armedSlider == previous.splitter)
        {
            clearArmedSlider();
            if (capturedPointerNode == previous.splitter)
            {
                capturedPointerNode = {};
            }
        }
        static_cast<void>(splitViewStorage.unlinkSplitView(splitView));
        return Core::success();
    }

    [[nodiscard]] Core::Result<UISplitViewParts> splitViewPartsFromUpdater(
        UINodeId updaterRoot, UINodeId splitView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (Core::Status valid = validateSplitViewUpdaterRoot(updaterRoot, splitView); !valid)
        {
            return Core::failure(valid.error());
        }
        return splitViewStorage.parts(splitView);
    }

    [[nodiscard]] Core::Status setSplitViewFractionFromUpdater(
        UINodeId updaterRoot, UINodeId splitView, float fraction)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateSplitViewUpdaterRoot(updaterRoot, splitView); !valid)
        {
            return valid;
        }
        if (!std::isfinite(fraction) || fraction < 0.0F || fraction > 1.0F)
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI SplitView fraction must be finite and within zero to one");
        }
        if (splitViewStorage.requestedFraction(splitView) == fraction)
        {
            return Core::success();
        }
        if (Core::Status dirty = markLayoutStyleDirty(splitView); !dirty)
        {
            return dirty;
        }
        splitViewStorage.setRequestedFraction(splitView, fraction);
        const UISplitViewParts parts = splitViewStorage.parts(splitView);
        if (parts.hasValue())
        {
            if (Detail::UIRangeInputState* range =
                    behaviorStateStorage.tryRangeInputState(parts.splitter.index());
                range != nullptr)
            {
                range->value = fraction;
            }
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<float> splitViewFractionFromUpdater(
        UINodeId updaterRoot, UINodeId splitView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (Core::Status valid = validateSplitViewUpdaterRoot(updaterRoot, splitView); !valid)
        {
            return Core::failure(valid.error());
        }
        return splitViewStorage.requestedFraction(splitView);
    }

    [[nodiscard]] Core::Result<UISplitViewMetrics> splitViewMetricsFromUpdater(
        UINodeId updaterRoot, UINodeId splitView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (Core::Status valid = validateSplitViewUpdaterRoot(updaterRoot, splitView); !valid)
        {
            return Core::failure(valid.error());
        }
        return splitViewStorage.committedMetrics(splitView);
    }

    [[nodiscard]] Core::Result<bool> isSplitterDraggingFromUpdater(
        UINodeId updaterRoot, UINodeId splitter) const
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
        auto splitterResult = const_cast<Impl*>(this)->resolveSplitter(splitter);
        if (!splitterResult)
        {
            return Core::failure(splitterResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, splitter))
        {
            return fail(UIErrorCode::InvalidNode,
                        "UI Splitter is not owned by the updater root");
        }
        return armedSlider == splitter;
    }

    [[nodiscard]] UINodeId rootForSplitView(UINodeId splitView) const noexcept
    {
        const NodeRecord* record = nodes.tryGet(splitView.storageId());
        return record != nullptr ? idForIndex(record->rootIndex) : UINodeId{};
    }

    [[nodiscard]] Core::Status setSplitViewParts(
        UINodeId splitView, UINodeId primaryPane, UINodeId splitter,
        UINodeId secondaryPane)
    {
        return setSplitViewPartsFromUpdater(rootForSplitView(splitView), splitView,
                                            primaryPane, splitter, secondaryPane);
    }

    [[nodiscard]] Core::Status clearSplitViewParts(UINodeId splitView)
    {
        return clearSplitViewPartsFromUpdater(rootForSplitView(splitView), splitView);
    }

    [[nodiscard]] Core::Result<UISplitViewParts> splitViewParts(UINodeId splitView) const
    {
        return splitViewPartsFromUpdater(rootForSplitView(splitView), splitView);
    }

    [[nodiscard]] Core::Status setSplitViewFraction(UINodeId splitView, float fraction)
    {
        return setSplitViewFractionFromUpdater(rootForSplitView(splitView), splitView, fraction);
    }

    [[nodiscard]] Core::Result<float> splitViewFraction(UINodeId splitView) const
    {
        return splitViewFractionFromUpdater(rootForSplitView(splitView), splitView);
    }

    [[nodiscard]] Core::Result<UISplitViewMetrics> splitViewMetrics(UINodeId splitView) const
    {
        return splitViewMetricsFromUpdater(rootForSplitView(splitView), splitView);
    }

    [[nodiscard]] Core::Result<bool> isSplitterDragging(UINodeId splitter) const
    {
        const NodeRecord* record = nodes.tryGet(splitter.storageId());
        const UINodeId root = record != nullptr ? idForIndex(record->rootIndex) : UINodeId{};
        return isSplitterDraggingFromUpdater(root, splitter);
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

    [[nodiscard]] bool isLiveMultilineTextEdit(UINodeId node) const noexcept
    {
        if (!node.hasValue() || !contains(node) || !isLiveTextEdit(node) ||
            node.index() >= textEditMultilineByNodeIndex.size())
        {
            return false;
        }
        const UITextEditMultilineConfig& config = textEditMultilineByNodeIndex[node.index()];
        return config.enabled && config.verticalScrollEnabled &&
               node.index() < textEditVisualLayoutsByNodeIndex.size() &&
               textEditVisualLayoutsByNodeIndex[node.index()].maximumScrollY > 0.0F;
    }

    [[nodiscard]] bool textEditWheelWouldChange(UINodeId textEdit, UILogicalPoint delta) const noexcept
    {
        if (!isLiveMultilineTextEdit(textEdit) || !isNodeEnabled(textEdit))
        {
            return false;
        }
        const u32 idx = textEdit.index();
        const float wheelStep = textEditMultilineByNodeIndex[idx].wheelStep;
        const float current = textEditScrollYByNodeIndex[idx];
        const float maxScroll = textEditVisualLayoutsByNodeIndex[idx].maximumScrollY;
        const float step = std::isfinite(wheelStep) && wheelStep > 0.0F ? wheelStep : 24.0F;
        const float next = (std::clamp)(current - delta.y * step, 0.0F, maxScroll);
        return next != current;
    }

    [[nodiscard]] Core::Result<bool> applyTextEditScrollWheel(UINodeId textEdit, UILogicalPoint delta)
    {
        if (!isLiveMultilineTextEdit(textEdit) || !isNodeEnabled(textEdit))
        {
            return false;
        }
        const u32 idx = textEdit.index();
        const float wheelStep = textEditMultilineByNodeIndex[idx].wheelStep;
        const float current = textEditScrollYByNodeIndex[idx];
        const float maxScroll = textEditVisualLayoutsByNodeIndex[idx].maximumScrollY;
        const float step = std::isfinite(wheelStep) && wheelStep > 0.0F ? wheelStep : 24.0F;
        const float next = (std::clamp)(current - delta.y * step, 0.0F, maxScroll);
        if (next == current)
        {
            return false;
        }
        if (Core::Status dirty = markPaintDirty(textEdit); !dirty)
        {
            return Core::failure(dirty.error());
        }
        textEditScrollYByNodeIndex[idx] = next;
        return true;
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

    [[nodiscard]] Core::Result<NodeRecord*> resolveTooltip(UINodeId tooltip)
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

    [[nodiscard]] bool hasValidTooltipRelationship(UINodeId tooltip,
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

    [[nodiscard]] UINodeId tooltipForAnchor(UINodeId anchor) const noexcept
    {
        if (!contains(anchor))
        {
            return {};
        }
        const UINodeId tooltip = tooltipStorage.tooltipForAnchor(anchor);
        return hasValidTooltipRelationship(tooltip, anchor) ? tooltip : UINodeId{};
    }

    [[nodiscard]] bool isAuthoredTooltipNodeVisible(UINodeId node) const noexcept
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

    [[nodiscard]] bool isTooltipAnchorEligible(UINodeId tooltip) const noexcept
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

    void markTooltipPresentationDirty() noexcept
    {
        // A Window has at most one active Tooltip, so a bounded full snapshot
        // rebuild avoids a second dirty ancestry path inside the frame coordinator.
        phaseDirty |= PhaseLayout | PhaseHit | PhasePaint | PhaseSemantics;
        layoutReuseCacheValid = false;
    }

    [[nodiscard]] Detail::UITooltipAdvanceCandidate
    tooltipAdvanceCandidate(UINodeId tooltip) const noexcept
    {
        return {
            .tooltip = tooltip,
            .eligible = isTooltipAnchorEligible(tooltip),
        };
    }

    void advanceTooltips(Core::MonotonicTimePoint now) noexcept
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

    void hardDismissAllTooltipsNoFail(bool suppress) noexcept
    {
        if (tooltipStorage.hardDismiss(defaultActionFocusButton, suppress))
        {
            markTooltipPresentationDirty();
        }
    }

    void dismissTooltipNoFail(UINodeId tooltip, bool suppress) noexcept
    {
        if (tooltipStorage.dismiss(tooltip, suppress, motionNow()))
        {
            markTooltipPresentationDirty();
        }
    }


    void reconcileTooltipAfterPublication(
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

    [[nodiscard]] UINodeId tooltipAnchorFromCommittedHit(
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

    [[nodiscard]] Core::Status setTooltipAnchorRelation(UINodeId tooltip,
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

    [[nodiscard]] Core::Status clearTooltipAnchorRelation(UINodeId tooltip)
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

    [[nodiscard]] Core::Status setTooltipAnchor(UINodeId tooltip,
                                                UINodeId anchor)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        return setTooltipAnchorRelation(tooltip, anchor);
    }

    [[nodiscard]] Core::Status clearTooltipAnchor(UINodeId tooltip)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        return clearTooltipAnchorRelation(tooltip);
    }

    [[nodiscard]] Core::Result<UINodeId>
    tooltipAnchor(UINodeId tooltip) const
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

    [[nodiscard]] Core::Status showTooltip(UINodeId tooltip)
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

    [[nodiscard]] Core::Status dismissTooltip(UINodeId tooltip)
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

    [[nodiscard]] Core::Result<bool> isTooltipOpen(UINodeId tooltip) const
    {
        auto metrics = tooltipMetrics(tooltip);
        if (!metrics)
        {
            return Core::failure(metrics.error());
        }
        return metrics->open;
    }

    [[nodiscard]] Core::Result<UITooltipMetrics>
    tooltipMetrics(UINodeId tooltip) const
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

    [[nodiscard]] Core::Status validateTooltipUpdaterRoot(
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

    [[nodiscard]] Core::Status setTooltipAnchorFromUpdater(
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

    [[nodiscard]] Core::Status clearTooltipAnchorFromUpdater(
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

    [[nodiscard]] Core::Result<UINodeId> tooltipAnchorFromUpdater(
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

    [[nodiscard]] Core::Status showTooltipFromUpdater(
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

    [[nodiscard]] Core::Status dismissTooltipFromUpdater(
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

    [[nodiscard]] Core::Result<bool> isTooltipOpenFromUpdater(
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

    [[nodiscard]] Core::Result<UITooltipMetrics> tooltipMetricsFromUpdater(
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
            configureCollectionRowLayout(layoutStylesByIndex[child], state.style.rowHeight);
            textStatesByIndex[child].overflow = state.style.rowTextOverflow;
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
            configureCollectionRowLayout(layoutStylesByIndex[child], state.style.rowHeight,
                                         layoutStylesByIndex[child].padding.left);
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
        resetTextEditPreferredX(textInputFocus);
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
        const bool layoutChanged = state.paint.labelGap != paint.labelGap ||
                                   state.paint.indicatorVisible != paint.indicatorVisible;
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
            if (record == nullptr || record->kind != BuiltinElementKind::Tooltip ||
                index >= tooltipStorage.capacity())
            {
                continue;
            }
            tooltipStorage.publishMetrics(index);
        }
        for (const u32 index : order)
        {
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr || record->kind != BuiltinElementKind::SplitView ||
                index >= splitViewStorage.capacity())
            {
                continue;
            }
            splitViewStorage.publishMetrics(index);
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
                                                              candidateLayoutEntries);
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

    [[nodiscard]] std::optional<UILogicalRect> committedTextInputCaretRectValue() const noexcept
    {
        return committedTextInputCaretRect;
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
            } else if (nearestSliderRecord != nullptr &&
                       isPointerAdjustableRangeInput(nearestSlider))
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
                                       isPointerAdjustableRangeInput(nearestSlider);
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
        if (input.kind == UIRoutedPointerEventKind::Move)
        {
            // Hover intent always follows the physical committed hit rather
            // than Pointer Capture routing. Tooltip itself is Ignore, so it can
            // never become this target or prevent click-through.
            tooltipStorage.setHoveredAnchor(
                tooltipAnchorFromCommittedHit(result.pointQuery.target, entries));
        } else if (input.kind == UIRoutedPointerEventKind::ButtonDown ||
                   input.kind == UIRoutedPointerEventKind::Wheel)
        {
            hardDismissAllTooltipsNoFail(true);
        }
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
        tooltipStorage.setHoveredAnchor({});
        hardDismissAllTooltipsNoFail(true);
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

    [[nodiscard]] static bool isValidFlowLocalUser(UIFlowLocalUserId localUser) noexcept
    {
        return localUser.hasValue() && localUser.value() <= UIFlowLocalUserCapacity;
    }

    [[nodiscard]] static usize flowLocalUserIndex(UIFlowLocalUserId localUser) noexcept
    {
        return static_cast<usize>(localUser.value() - 1U);
    }

    [[nodiscard]] Core::Status validateFlowLocalUser(UIFlowLocalUserId localUser) const
    {
        if (!isValidFlowLocalUser(localUser))
        {
            return fail(UIErrorCode::InvalidFlowLocalUser,
                        "UI Flow local user is outside the fixed local-user capacity");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status validateFlowGamepad(Platform::GamepadId gamepad) const
    {
        if (!gamepad.hasValue() || gamepad.index() >= flowGamepadAssignments.size())
        {
            return fail(UIErrorCode::InvalidFlowOperation,
                        "UI Flow Gamepad identity is outside the Platform slot capacity");
        }
        return Core::success();
    }

    [[nodiscard]] UIFlowLocalUserId
    flowLocalUserForGamepadUnchecked(Platform::GamepadId gamepad) const noexcept
    {
        const UIFlowGamepadAssignment& assignment =
            flowGamepadAssignments[gamepad.index()];
        return assignment.gamepad == gamepad ? assignment.localUser
                                              : UIFlowPrimaryLocalUser;
    }

    [[nodiscard]] Core::Status fallbackFlowInputDevicesForGamepads(
        Platform::GamepadId first,
        std::optional<Platform::GamepadId> second = std::nullopt)
    {
        const auto matches = [first, second](const UIFlowInputDeviceState& state) noexcept {
            return state.device == UIFlowInputDevice::Gamepad &&
                   state.gamepad.has_value() &&
                   (*state.gamepad == first ||
                    (second.has_value() && *state.gamepad == *second));
        };
        for (const UIFlowInputDeviceState& state : observedFlowInputDevices)
        {
            if (matches(state) && state.revision == (std::numeric_limits<u64>::max)())
            {
                return fail(UIErrorCode::CapacityExceeded,
                            "UI Flow input-device revision is exhausted");
            }
        }
        for (UIFlowInputDeviceState& state : observedFlowInputDevices)
        {
            if (!matches(state))
            {
                continue;
            }
            state.device = UIFlowInputDevice::KeyboardMouse;
            state.gamepad.reset();
            ++state.revision;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status fallbackAllFlowInputDevices()
    {
        for (const UIFlowInputDeviceState& state : observedFlowInputDevices)
        {
            if (state.device == UIFlowInputDevice::Gamepad &&
                state.revision == (std::numeric_limits<u64>::max)())
            {
                return fail(UIErrorCode::CapacityExceeded,
                            "UI Flow input-device revision is exhausted");
            }
        }
        for (UIFlowInputDeviceState& state : observedFlowInputDevices)
        {
            if (state.device != UIFlowInputDevice::Gamepad)
            {
                continue;
            }
            state.device = UIFlowInputDevice::KeyboardMouse;
            state.gamepad.reset();
            ++state.revision;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status assignFlowGamepad(
        Platform::GamepadId gamepad, UIFlowLocalUserId localUser)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        if (Core::Status validUser = validateFlowLocalUser(localUser); !validUser)
        {
            return validUser;
        }
        if (Core::Status validGamepad = validateFlowGamepad(gamepad); !validGamepad)
        {
            return validGamepad;
        }

        UIFlowGamepadAssignment& assignment = flowGamepadAssignments[gamepad.index()];
        if (assignment.gamepad == gamepad && assignment.localUser == localUser)
        {
            return Core::success();
        }
        const std::optional<Platform::GamepadId> replacedGamepad =
            assignment.gamepad.hasValue()
                ? std::optional<Platform::GamepadId>{assignment.gamepad}
                : std::nullopt;
        if (Core::Status fallback =
                fallbackFlowInputDevicesForGamepads(gamepad, replacedGamepad);
            !fallback)
        {
            return fallback;
        }
        assignment = UIFlowGamepadAssignment{
            .gamepad = gamepad,
            .localUser = localUser,
        };
        return Core::success();
    }

    [[nodiscard]] Core::Status
    clearFlowGamepadAssignment(Platform::GamepadId gamepad)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        if (Core::Status validGamepad = validateFlowGamepad(gamepad); !validGamepad)
        {
            return validGamepad;
        }

        UIFlowGamepadAssignment& assignment = flowGamepadAssignments[gamepad.index()];
        if (assignment.gamepad != gamepad)
        {
            return Core::success();
        }
        if (Core::Status fallback = fallbackFlowInputDevicesForGamepads(gamepad);
            !fallback)
        {
            return fallback;
        }
        assignment = {};
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIFlowLocalUserId>
    flowLocalUserForGamepad(Platform::GamepadId gamepad) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (Core::Status validGamepad = validateFlowGamepad(gamepad); !validGamepad)
        {
            return Core::failure(validGamepad.error());
        }
        return flowLocalUserForGamepadUnchecked(gamepad);
    }

    [[nodiscard]] Core::Result<UIFlowInputDeviceState>
    flowInputDeviceState(UIFlowLocalUserId localUser) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (Core::Status validUser = validateFlowLocalUser(localUser); !validUser)
        {
            return Core::failure(validUser.error());
        }
        return observedFlowInputDevices[flowLocalUserIndex(localUser)];
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

        if (gamepad.has_value())
        {
            if (Core::Status validGamepad = validateFlowGamepad(*gamepad);
                !validGamepad)
            {
                return validGamepad;
            }
            if (Core::Status fallback = fallbackFlowInputDevicesForGamepads(*gamepad);
                !fallback)
            {
                return fallback;
            }
            UIFlowGamepadAssignment& assignment =
                flowGamepadAssignments[gamepad->index()];
            if (assignment.gamepad == *gamepad)
            {
                assignment = {};
            }
        }
        else
        {
            if (Core::Status fallback = fallbackAllFlowInputDevices(); !fallback)
            {
                return fallback;
            }
            for (UIFlowGamepadAssignment& assignment : flowGamepadAssignments)
            {
                assignment = {};
            }
        }

        dropdownCommandPressLatch.clear();
        listViewCommandPressLatch.clear();
        treeViewCommandPressLatch.clear();
        focusNavigationPressLatch.clear();

        if (gamepad.has_value())
        {
            rangeInputPressLatch.clearGamepad(*gamepad);
            flowActionPressState.clearGamepad(*gamepad);
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
        flowActionPressState.clearAll();
        defaultActionPressState.clearAll();
        if (cancelledTarget.hasValue() && contains(cancelledTarget))
        {
            static_cast<void>(markPaintDirty(cancelledTarget));
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status observeFlowInputDevice(
        Platform::PlatformFrameId platformFrame, u64 sourceSequence,
        UIFlowLocalUserId localUser, UIFlowInputDevice device,
        std::optional<Platform::GamepadId> gamepad)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        if (Core::Status validUser = validateFlowLocalUser(localUser); !validUser)
        {
            return validUser;
        }
        UIFlowInputDeviceState& observedFlowInputDevice =
            observedFlowInputDevices[flowLocalUserIndex(localUser)];
        if (!platformFrame.hasValue() || sourceSequence == 0 ||
            sourceSequence <= observedFlowInputDevice.sourceSequence)
        {
            return fail(UIErrorCode::InvalidFlowOperation,
                        "UI Flow input-device observation must be strictly ordered");
        }
        if ((device == UIFlowInputDevice::KeyboardMouse && gamepad.has_value()) ||
            (device == UIFlowInputDevice::Gamepad &&
             (!gamepad.has_value() || !gamepad->hasValue())))
        {
            return fail(UIErrorCode::InvalidFlowOperation,
                        "UI Flow input-device observation has an invalid Gamepad identity");
        }
        if (device == UIFlowInputDevice::Gamepad)
        {
            if (Core::Status validGamepad = validateFlowGamepad(*gamepad);
                !validGamepad)
            {
                return validGamepad;
            }
            if (flowLocalUserForGamepadUnchecked(*gamepad) != localUser)
            {
                return fail(UIErrorCode::InvalidFlowLocalUser,
                            "UI Flow Gamepad observation does not match its local-user assignment");
            }
        }

        const bool changed = device != observedFlowInputDevice.device ||
                             gamepad != observedFlowInputDevice.gamepad;
        if (changed && observedFlowInputDevice.revision ==
                           (std::numeric_limits<u64>::max)())
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI Flow input-device revision is exhausted");
        }

        observedFlowInputDevice.device = device;
        observedFlowInputDevice.gamepad = gamepad;
        observedFlowInputDevice.platformFrame = platformFrame;
        observedFlowInputDevice.sourceSequence = sourceSequence;
        if (changed)
        {
            ++observedFlowInputDevice.revision;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIFlowActionRouteResult>
    routeFlowAction(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                    UIFlowLocalUserId localUser, UIFlowAction action,
                    UIFlowActionSource source, bool pressed,
                    const Platform::DigitalControlIdentity& control)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI Flow action cannot nest during input routing");
        }
        drainDeferredRootDestroys();
        if (!platformFrame.hasValue() || sourceSequence == 0)
        {
            return fail(UIErrorCode::InvalidFlowAction,
                        "UI Flow action requires a platform frame and source sequence");
        }
        if (Core::Status validUser = validateFlowLocalUser(localUser); !validUser)
        {
            return Core::failure(validUser.error());
        }
        if (Core::Status valid = flowActionPressState.validate(action, source, control);
            !valid)
        {
            return Core::failure(valid.error());
        }
        if (source == UIFlowActionSource::Keyboard &&
            localUser != UIFlowPrimaryLocalUser)
        {
            return fail(UIErrorCode::InvalidFlowLocalUser,
                        "UI Flow keyboard actions belong to the primary local user");
        }
        if (source == UIFlowActionSource::Gamepad)
        {
            const auto* button =
                std::get_if<Platform::GamepadButtonControlIdentity>(&control);
            if (button == nullptr ||
                flowLocalUserForGamepadUnchecked(button->gamepad) != localUser)
            {
                return fail(UIErrorCode::InvalidFlowLocalUser,
                            "UI Flow Gamepad action does not match its local-user assignment");
            }
        }

        const bool alreadyPressed = flowActionPressState.isPressed(action, control);
        if (!pressed)
        {
            if (alreadyPressed)
            {
                flowActionPressState.clearPressed(action, control);
            }
            return UIFlowActionRouteResult{.consumed = alreadyPressed};
        }
        if (alreadyPressed)
        {
            return UIFlowActionRouteResult{.consumed = true};
        }

        UINodeId targetScreen{};
        const auto& committedLayout = committedLayoutBuffers[publishedLayoutBufferIndex];
        for (auto entry = committedLayout.rbegin(); entry != committedLayout.rend(); ++entry)
        {
            const UINodeId candidate = entry->node;
            if (entry->effectiveVisibility != UIVisibility::Visible || !contains(candidate) ||
                candidate.index() >= flowStatesByNodeIndex.size())
            {
                continue;
            }
            const UIFlowNodeState& state = flowStatesByNodeIndex[candidate.index()];
            if (state.kind == UIFlowNodeKind::Screen && isActiveFlowScreenIndex(candidate.index()))
            {
                targetScreen = candidate;
                break;
            }
        }
        if (!targetScreen.hasValue())
        {
            return UIFlowActionRouteResult{};
        }

        UIFlowNodeState& targetState = flowStatesByNodeIndex[targetScreen.index()];
        const usize actionIndex = flowActionSlotIndex(action);
        if (actionIndex >= FlowActionSlotCount)
        {
            return fail(UIErrorCode::InvalidFlowAction,
                        "UI Flow action is not supported by this router");
        }
        UIFlowActionSlot& actionSlot = targetState.actions[actionIndex];
        if (!actionSlot.registered || !actionSlot.callback.hasValue())
        {
            return UIFlowActionRouteResult{.screen = UIFlowScreenId{targetScreen}};
        }

        flowActionPressState.setPressed(action, control);
        ++flowActionCallbackOperationDepth;
        UIFlowActionCallback callback = std::move(actionSlot.callback);
        --flowActionCallbackOperationDepth;
        ++flowActionInvocationCount;
        ++routeDispatchDepth;
        auto dispatchCleanup = Core::makeScopeExit([this]() noexcept {
            finishRoutedPointerDispatch();
        });
        callback(UIFlowActionEvent{
            .screen = UIFlowScreenId{targetScreen},
            .localUser = localUser,
            .action = action,
            .source = source,
            .platformFrame = platformFrame,
            .sourceSequence = sourceSequence,
        });
        if (contains(targetScreen) && targetScreen.index() < flowStatesByNodeIndex.size())
        {
            UIFlowNodeState& liveState = flowStatesByNodeIndex[targetScreen.index()];
            UIFlowActionSlot& liveActionSlot = liveState.actions[actionIndex];
            if (liveState.kind == UIFlowNodeKind::Screen && liveActionSlot.registered &&
                !liveActionSlot.callback.hasValue())
            {
                ++flowActionCallbackOperationDepth;
                liveActionSlot.callback = std::move(callback);
                --flowActionCallbackOperationDepth;
            }
        }
        return UIFlowActionRouteResult{
            .consumed = true,
            .invoked = true,
            .screen = UIFlowScreenId{targetScreen},
        };
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
        if (previousTextFocus != textInputFocus)
        {
            resetTextEditPreferredX(previousTextFocus);
            resetTextEditPreferredX(textInputFocus);
        }
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
                resetTextEditPreferredX(nextFocus);
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
            hardDismissAllTooltipsNoFail(true);
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
            hardDismissAllTooltipsNoFail(true);
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
        hardDismissAllTooltipsNoFail(true);
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
        const bool validCommittedUtf8 = Core::isStrictUtf8WithoutNul(committedUtf8);
        if (!isCommittedTextEditFocusCandidate(textInputFocus))
        {
            if (validCommittedUtf8)
            {
                hardDismissAllTooltipsNoFail(true);
            }
            clearImeFocus();
            return UITextInputRouteResult{};
        }
        if (!validCommittedUtf8)
        {
            return fail(UIErrorCode::InvalidText, "UI text input must be strict UTF-8 without embedded NUL");
        }
        const UITextEditMultilineConfig multiline =
            textInputFocus.index() < textEditMultilineByNodeIndex.size()
                ? textEditMultilineByNodeIndex[textInputFocus.index()] : UITextEditMultilineConfig{};
        if (committedUtf8.find('\r') != std::string_view::npos ||
            (!multiline.enabled && committedUtf8.find('\n') != std::string_view::npos))
        {
            hardDismissAllTooltipsNoFail(true);
            return UITextInputRouteResult{.consumed = true, .applied = false};
        }
        if (committedUtf8.empty())
        {
            if (Core::Status status = clearImeComposition(); !status)
            {
                return Core::failure(status.error());
            }
            hardDismissAllTooltipsNoFail(true);
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
        const u32 insertedEnd = selectionBegin + insertedCodepoints.value_or(0U);
        const u32 nextCaret =
            Detail::graphemeBoundaryAtOrAfter(combined, insertedEnd);
        textInputState->selection = {
            .anchorCodepoint = nextCaret,
            .caretCodepoint = nextCaret,
        };
        // Reset preferred-X after text insertion.
        if (multiline.enabled && focusedTextEdit.index() < textEditPreferredXByNodeIndex.size())
        {
            textEditPreferredXByNodeIndex[focusedTextEdit.index()].reset();
        }
        if (focusedTextEdit.index() < textEditCaretAffinityByNodeIndex.size())
        {
            textEditCaretAffinityByNodeIndex[focusedTextEdit.index()] =
                Detail::UITextEditCaretAffinity::Downstream;
        }
        if (Core::Status status = clearImeComposition(); !status)
        {
            return Core::failure(status.error());
        }
        hardDismissAllTooltipsNoFail(true);
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
        const std::string_view current =
            textViewFor(focusedTextEdit.index());
        const UITextSelection currentSelection = editState->selection;
        const u32 idx = focusedTextEdit.index();
        const auto multilineConfig = idx < textEditMultilineByNodeIndex.size()
                                         ? textEditMultilineByNodeIndex[idx]
                                         : UITextEditMultilineConfig{};
        const bool isVerticalCommand =
            command == UITextEditCommand::MoveUp || command == UITextEditCommand::MoveDown;
        const Detail::UITextEditCaretAffinity currentCaretAffinity =
            idx < textEditCaretAffinityByNodeIndex.size()
                ? textEditCaretAffinityByNodeIndex[idx]
                : Detail::UITextEditCaretAffinity::Downstream;
        std::optional<UITextEditCommandPlan> plan;
        if (multilineConfig.enabled && idx < textEditVisualLayoutsByNodeIndex.size() &&
            textEditVisualLayoutsByNodeIndex[idx].lineCount != 0 &&
            (command == UITextEditCommand::MoveLeft || command == UITextEditCommand::MoveRight ||
             command == UITextEditCommand::MoveUp || command == UITextEditCommand::MoveDown ||
             command == UITextEditCommand::MoveHome || command == UITextEditCommand::MoveEnd))
        {
            const float fallback = textStatesByIndex[idx].style.logicalSize *
                                   textStatesByIndex[idx].style.advanceScale;
            const std::optional<float> preferredX =
                isVerticalCommand && idx < textEditPreferredXByNodeIndex.size()
                    ? textEditPreferredXByNodeIndex[idx]
                    : std::nullopt;
            std::span<const UITextGlyphRaster> glyphs{};
            if (isVerticalCommand && textRasterizer != nullptr && textFace.hasValue())
            {
                auto raster = textRasterizer->raster(
                    textFace, current, textStatesByIndex[idx].style);
                if (raster)
                {
                    glyphs = raster->glyphs;
                }
            }
            plan = Detail::planTextEditVisualCommand(
                current, editState->selection, command, extendSelection,
                textEditVisualLinesByNodeIndex[idx], currentCaretAffinity,
                preferredX, fallback, glyphs);
        }
        else
        {
            plan = planTextEditCommand(
                current, currentSelection, command, extendSelection);
        }
        if (!plan.has_value())
        {
            return fail(UIErrorCode::InvalidText, "UI TextEdit command is not recognized");
        }
        const std::optional<float> nextPreferredX =
            isVerticalCommand ? plan->updatedPreferredX : std::nullopt;
        const bool caretAffinityChanged =
            plan->nextCaretAffinity != currentCaretAffinity;
        const auto commitNavigationState = [&]() noexcept {
            if (idx < textEditPreferredXByNodeIndex.size())
            {
                textEditPreferredXByNodeIndex[idx] = nextPreferredX;
            }
            if (idx < textEditCaretAffinityByNodeIndex.size())
            {
                textEditCaretAffinityByNodeIndex[idx] = plan->nextCaretAffinity;
            }
        };

        if (!plan->deletesText)
        {
            if (plan->nextSelection == currentSelection && !caretAffinityChanged)
            {
                if (Core::Status status = clearImeComposition(); !status)
                {
                    return Core::failure(status.error());
                }
                commitNavigationState();
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
            commitNavigationState();
            return UITextInputRouteResult{.consumed = true, .applied = true};
        }

        if (plan->deleteBeginCodepoint == plan->deleteEndCodepoint)
        {
            if (Core::Status status = clearImeComposition(); !status)
            {
                return Core::failure(status.error());
            }
            commitNavigationState();
            return UITextInputRouteResult{.consumed = true, .applied = false};
        }
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
        const u32 nextCaret = Detail::graphemeBoundaryAtOrAfter(
            combined, plan->deleteBeginCodepoint);
        editState->selection = {
            .anchorCodepoint = nextCaret,
            .caretCodepoint = nextCaret,
        };
        // Reset preferred-X after text deletion.
        if (multilineConfig.enabled && focusedTextEdit.index() < textEditPreferredXByNodeIndex.size())
        {
            textEditPreferredXByNodeIndex[focusedTextEdit.index()].reset();
        }
        if (Core::Status status = clearImeComposition(); !status)
        {
            return Core::failure(status.error());
        }
        commitNavigationState();
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

Core::Result<UIFlowLayerId> UITreeUpdater::registerFlowLayer(UINodeId layer)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->registerFlowLayerFromUpdater(m_root, layer);
}

Core::Result<UIFlowScreenId> UITreeUpdater::registerFlowScreen(UIFlowLayerId layer, UINodeId screen)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->registerFlowScreenFromUpdater(m_root, layer, screen);
}

Core::Status UITreeUpdater::pushFlowScreen(UIFlowScreenId screen)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->pushFlowScreenFromUpdater(m_root, screen);
}

Core::Result<UIFlowScreenId> UITreeUpdater::popFlowScreen(UIFlowLayerId layer)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->popFlowScreenFromUpdater(m_root, layer);
}

Core::Result<UIFlowScreenId> UITreeUpdater::replaceFlowScreen(UIFlowScreenId screen)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->replaceFlowScreenFromUpdater(m_root, screen);
}

Core::Result<UIFlowScreenId> UITreeUpdater::activeFlowScreen(UIFlowLayerId layer) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->activeFlowScreenFromUpdater(m_root, layer);
}

Core::Result<bool> UITreeUpdater::isFlowScreenActive(UIFlowScreenId screen) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isFlowScreenActiveFromUpdater(m_root, screen);
}

Core::Status UITreeUpdater::assignFlowGamepad(Platform::GamepadId gamepad,
                                              UIFlowLocalUserId localUser)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    if (!m_context->isAliveInRoot(m_root, m_root))
    {
        return fail(UIErrorCode::InvalidNode, "UI tree updater root is stale");
    }
    return m_context->assignFlowGamepad(gamepad, localUser);
}

Core::Status UITreeUpdater::clearFlowGamepadAssignment(Platform::GamepadId gamepad)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    if (!m_context->isAliveInRoot(m_root, m_root))
    {
        return fail(UIErrorCode::InvalidNode, "UI tree updater root is stale");
    }
    return m_context->clearFlowGamepadAssignment(gamepad);
}

Core::Result<UIFlowLocalUserId>
UITreeUpdater::flowLocalUserForGamepad(Platform::GamepadId gamepad) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    if (!m_context->isAliveInRoot(m_root, m_root))
    {
        return fail(UIErrorCode::InvalidNode, "UI tree updater root is stale");
    }
    return m_context->flowLocalUserForGamepad(gamepad);
}

Core::Result<UIFlowInputDeviceState>
UITreeUpdater::flowInputDeviceState(UIFlowLocalUserId localUser) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    if (!m_context->isAliveInRoot(m_root, m_root))
    {
        return fail(UIErrorCode::InvalidNode, "UI tree updater root is stale");
    }
    return m_context->flowInputDeviceState(localUser);
}

Core::Status UITreeUpdater::setFlowScreenAction(UIFlowScreenId screen, UIFlowAction action,
                                                UIFlowActionCallback callback)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setFlowScreenActionFromUpdater(m_root, screen, action,
                                                      std::move(callback));
}

Core::Status UITreeUpdater::clearFlowScreenAction(UIFlowScreenId screen, UIFlowAction action)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->clearFlowScreenActionFromUpdater(m_root, screen, action);
}

bool UITreeUpdater::isAlive(UINodeId node) const noexcept
{
    return m_context != nullptr && m_context->isAliveInRoot(m_root, node);
}

Core::Result<UILogicalRect> UITreeUpdater::committedLayoutRect(UINodeId node) const
{
    if (m_context == nullptr)
    {
        return Core::failure(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    if (!m_context->isAliveInRoot(m_root, node))
    {
        return Core::failure(UIErrorCode::InvalidNode,
                             "UI committed layout query requires a live node in the updater root");
    }
    for (const UICommittedLayoutEntry& entry : m_context->committedLayout().entries())
    {
        if (entry.node == node)
        {
            return entry.worldRect;
        }
    }
    return Core::failure(UIErrorCode::InvalidNode,
                         "UI node is absent from the committed layout snapshot");
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

Core::Status UITreeUpdater::setImageTint(UINodeId node, UIStraightSrgba8Color tint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setImageTintFromUpdater(m_root, node, tint);
}

Core::Result<UIStraightSrgba8Color> UITreeUpdater::imageTint(UINodeId node) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->imageTintFromUpdater(m_root, node);
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

Core::Status UITreeUpdater::setTextOverflow(UINodeId node, UITextOverflow overflow)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setTextOverflowFromUpdater(m_root, node, overflow);
}

Core::Result<UITextOverflow> UITreeUpdater::textOverflow(UINodeId node)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->textOverflowFromUpdater(m_root, node);
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

Core::Status UITreeUpdater::setSplitViewParts(
    UINodeId splitView, UINodeId primaryPane, UINodeId splitter,
    UINodeId secondaryPane)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setSplitViewPartsFromUpdater(
        m_root, splitView, primaryPane, splitter, secondaryPane);
}

Core::Status UITreeUpdater::clearSplitViewParts(UINodeId splitView)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->clearSplitViewPartsFromUpdater(m_root, splitView);
}

Core::Result<UISplitViewParts> UITreeUpdater::splitViewParts(UINodeId splitView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->splitViewPartsFromUpdater(m_root, splitView);
}

Core::Status UITreeUpdater::setSplitViewFraction(UINodeId splitView, float fraction)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setSplitViewFractionFromUpdater(m_root, splitView, fraction);
}

Core::Result<float> UITreeUpdater::splitViewFraction(UINodeId splitView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->splitViewFractionFromUpdater(m_root, splitView);
}

Core::Result<UISplitViewMetrics> UITreeUpdater::splitViewMetrics(UINodeId splitView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->splitViewMetricsFromUpdater(m_root, splitView);
}

Core::Result<bool> UITreeUpdater::isSplitterDragging(UINodeId splitter) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isSplitterDraggingFromUpdater(m_root, splitter);
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

Core::Status UITreeUpdater::setTooltipAnchor(UINodeId tooltip, UINodeId anchor)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setTooltipAnchorFromUpdater(m_root, tooltip, anchor);
}

Core::Status UITreeUpdater::clearTooltipAnchor(UINodeId tooltip)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->clearTooltipAnchorFromUpdater(m_root, tooltip);
}

Core::Result<UINodeId> UITreeUpdater::tooltipAnchor(UINodeId tooltip) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->tooltipAnchorFromUpdater(m_root, tooltip);
}

Core::Status UITreeUpdater::showTooltip(UINodeId tooltip)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->showTooltipFromUpdater(m_root, tooltip);
}

Core::Status UITreeUpdater::dismissTooltip(UINodeId tooltip)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->dismissTooltipFromUpdater(m_root, tooltip);
}

Core::Result<bool> UITreeUpdater::isTooltipOpen(UINodeId tooltip) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isTooltipOpenFromUpdater(m_root, tooltip);
}

Core::Result<UITooltipMetrics> UITreeUpdater::tooltipMetrics(UINodeId tooltip) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->tooltipMetricsFromUpdater(m_root, tooltip);
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

Core::Result<UIStraightSrgba8Color>
UIContext::styleColorToken(UIStyleTokenId token) const
{
    return m_impl->styleColorToken(token);
}

Core::Status UIContext::setStyleColorToken(
    UIStyleTokenId token, UIStraightSrgba8Color value)
{
    return m_impl->setStyleColorToken(token, value);
}

Core::Status UIContext::setMotionClock(const Core::IMonotonicClock* clock)
{
    return m_impl->setMotionClock(clock);
}

Core::Status UIContext::setReducedMotion(bool enabled)
{
    return m_impl->setReducedMotion(enabled);
}

bool UIContext::reducedMotion() const noexcept
{
    return m_impl->reducedMotion();
}

Core::Status UIContext::setStyleBackgroundColorTransition(const UITransitionSpec& spec)
{
    return m_impl->setStyleBackgroundColorTransition(spec);
}

UITransitionSpec UIContext::styleBackgroundColorTransition() const noexcept
{
    return m_impl->styleBackgroundColorTransition();
}

Core::Status UIContext::beginBackgroundColorTransition(
    UINodeId node, UIStraightSrgba8Color target, const UITransitionSpec& spec)
{
    return m_impl->beginBackgroundColorTransition(node, target, spec);
}

Core::Status UIContext::beginBorderColorTransition(
    UINodeId node, UIStraightSrgba8Color target, const UITransitionSpec& spec)
{
    return m_impl->beginBorderColorTransition(node, target, spec);
}

Core::Status UIContext::beginTextColorTransition(
    UINodeId node, UIStraightSrgba8Color target, const UITransitionSpec& spec)
{
    return m_impl->beginTextColorTransition(node, target, spec);
}

Core::Status UIContext::beginOpacityTransition(
    UINodeId node, float targetOpacity, const UITransitionSpec& spec)
{
    return m_impl->beginOpacityTransition(node, targetOpacity, spec);
}

Core::Status UIContext::beginCornerRadiusTransition(
    UINodeId node, float targetRadius, const UITransitionSpec& spec)
{
    return m_impl->beginCornerRadiusTransition(node, targetRadius, spec);
}

Core::Status UIContext::beginVisualOffsetTransition(
    UINodeId node, float targetOffsetX, float targetOffsetY, const UITransitionSpec& spec)
{
    return m_impl->beginVisualOffsetTransition(node, targetOffsetX, targetOffsetY, spec);
}

Core::Result<UITimelineId> UIContext::createTimeline(const UITimelineDesc& desc)
{
    return m_impl->createTimeline(desc);
}

Core::Status UIContext::replaceTimeline(UITimelineId timeline, const UITimelineDesc& desc)
{
    return m_impl->replaceTimeline(timeline, desc);
}

Core::Status UIContext::playTimeline(UITimelineId timeline)
{
    return m_impl->playTimeline(timeline);
}

Core::Status UIContext::cancelTimeline(UITimelineId timeline)
{
    return m_impl->cancelTimeline(timeline);
}

Core::Status UIContext::destroyTimeline(UITimelineId timeline)
{
    return m_impl->destroyTimeline(timeline);
}

Core::Result<bool> UIContext::isTimelineActive(UITimelineId timeline) const
{
    return m_impl->isTimelineActive(timeline);
}

Core::Status UIContext::sampleMotion(Core::MonotonicTimePoint now)
{
    return m_impl->sampleMotion(now);
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

std::optional<UILogicalRect> UIContext::committedTextInputCaretRect() const noexcept
{
    return m_impl->committedTextInputCaretRectValue();
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

Core::Result<UIFlowActionRouteResult>
UIContext::routeFlowAction(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                           UIFlowLocalUserId localUser, UIFlowAction action,
                           UIFlowActionSource source, bool pressed,
                           const Platform::DigitalControlIdentity& control)
{
    return m_impl->routeFlowAction(platformFrame, sourceSequence, localUser, action,
                                   source, pressed, control);
}

Core::Status UIContext::observeFlowInputDevice(
    Platform::PlatformFrameId platformFrame, u64 sourceSequence,
    UIFlowLocalUserId localUser, UIFlowInputDevice device,
    std::optional<Platform::GamepadId> gamepad)
{
    return m_impl->observeFlowInputDevice(platformFrame, sourceSequence, localUser,
                                          device, gamepad);
}

Core::Status UIContext::assignFlowGamepad(Platform::GamepadId gamepad,
                                          UIFlowLocalUserId localUser)
{
    return m_impl->assignFlowGamepad(gamepad, localUser);
}

Core::Status UIContext::clearFlowGamepadAssignment(Platform::GamepadId gamepad)
{
    return m_impl->clearFlowGamepadAssignment(gamepad);
}

Core::Result<UIFlowLocalUserId>
UIContext::flowLocalUserForGamepad(Platform::GamepadId gamepad) const
{
    return m_impl->flowLocalUserForGamepad(gamepad);
}

Core::Result<UIFlowInputDeviceState>
UIContext::flowInputDeviceState(UIFlowLocalUserId localUser) const
{
    return m_impl->flowInputDeviceState(localUser);
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

Core::Status UIContext::setSplitViewParts(
    UINodeId splitView, UINodeId primaryPane, UINodeId splitter,
    UINodeId secondaryPane)
{
    return m_impl->setSplitViewParts(splitView, primaryPane, splitter, secondaryPane);
}

Core::Status UIContext::clearSplitViewParts(UINodeId splitView)
{
    return m_impl->clearSplitViewParts(splitView);
}

Core::Result<UISplitViewParts> UIContext::splitViewParts(UINodeId splitView) const
{
    return m_impl->splitViewParts(splitView);
}

Core::Status UIContext::setSplitViewFraction(UINodeId splitView, float fraction)
{
    return m_impl->setSplitViewFraction(splitView, fraction);
}

Core::Result<float> UIContext::splitViewFraction(UINodeId splitView) const
{
    return m_impl->splitViewFraction(splitView);
}

Core::Result<UISplitViewMetrics> UIContext::splitViewMetrics(UINodeId splitView) const
{
    return m_impl->splitViewMetrics(splitView);
}

Core::Result<bool> UIContext::isSplitterDragging(UINodeId splitter) const
{
    return m_impl->isSplitterDragging(splitter);
}

Core::Status UIContext::setTooltipAnchor(UINodeId tooltip, UINodeId anchor)
{
    return m_impl->setTooltipAnchor(tooltip, anchor);
}

Core::Status UIContext::clearTooltipAnchor(UINodeId tooltip)
{
    return m_impl->clearTooltipAnchor(tooltip);
}

Core::Result<UINodeId> UIContext::tooltipAnchor(UINodeId tooltip) const
{
    return m_impl->tooltipAnchor(tooltip);
}

Core::Status UIContext::showTooltip(UINodeId tooltip)
{
    return m_impl->showTooltip(tooltip);
}

Core::Status UIContext::dismissTooltip(UINodeId tooltip)
{
    return m_impl->dismissTooltip(tooltip);
}

Core::Result<bool> UIContext::isTooltipOpen(UINodeId tooltip) const
{
    return m_impl->isTooltipOpen(tooltip);
}

Core::Result<UITooltipMetrics> UIContext::tooltipMetrics(UINodeId tooltip) const
{
    return m_impl->tooltipMetrics(tooltip);
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

Core::Result<UIFlowLayerId> UIContext::registerFlowLayerFromUpdater(UINodeId updaterRoot, UINodeId layer)
{
    return m_impl->registerFlowLayerFromUpdater(updaterRoot, layer);
}

Core::Result<UIFlowScreenId> UIContext::registerFlowScreenFromUpdater(UINodeId updaterRoot,
                                                                      UIFlowLayerId layer, UINodeId screen)
{
    return m_impl->registerFlowScreenFromUpdater(updaterRoot, layer, screen);
}

Core::Status UIContext::pushFlowScreenFromUpdater(UINodeId updaterRoot, UIFlowScreenId screen)
{
    return m_impl->pushFlowScreenFromUpdater(updaterRoot, screen);
}

Core::Result<UIFlowScreenId> UIContext::popFlowScreenFromUpdater(UINodeId updaterRoot, UIFlowLayerId layer)
{
    return m_impl->popFlowScreenFromUpdater(updaterRoot, layer);
}

Core::Result<UIFlowScreenId> UIContext::replaceFlowScreenFromUpdater(UINodeId updaterRoot,
                                                                     UIFlowScreenId screen)
{
    return m_impl->replaceFlowScreenFromUpdater(updaterRoot, screen);
}

Core::Result<UIFlowScreenId> UIContext::activeFlowScreenFromUpdater(UINodeId updaterRoot,
                                                                    UIFlowLayerId layer) const
{
    return m_impl->activeFlowScreenFromUpdater(updaterRoot, layer);
}

Core::Result<bool> UIContext::isFlowScreenActiveFromUpdater(UINodeId updaterRoot,
                                                            UIFlowScreenId screen) const
{
    return m_impl->isFlowScreenActiveFromUpdater(updaterRoot, screen);
}

Core::Status UIContext::setFlowScreenActionFromUpdater(UINodeId updaterRoot,
                                                       UIFlowScreenId screen,
                                                       UIFlowAction action,
                                                       UIFlowActionCallback&& callback)
{
    return m_impl->setFlowScreenActionFromUpdater(updaterRoot, screen, action,
                                                  std::move(callback));
}

Core::Status UIContext::clearFlowScreenActionFromUpdater(UINodeId updaterRoot,
                                                         UIFlowScreenId screen,
                                                         UIFlowAction action)
{
    return m_impl->clearFlowScreenActionFromUpdater(updaterRoot, screen, action);
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

Core::Status UIContext::setImageTintFromUpdater(UINodeId updaterRoot, UINodeId node, UIStraightSrgba8Color tint)
{
    return m_impl->setImageTintFromUpdater(updaterRoot, node, tint);
}

Core::Result<UIStraightSrgba8Color> UIContext::imageTintFromUpdater(UINodeId updaterRoot, UINodeId node) const
{
    return m_impl->imageTintFromUpdater(updaterRoot, node);
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

Core::Status UIContext::setTextOverflowFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                   UITextOverflow overflow)
{
    return m_impl->setTextOverflowFromUpdater(updaterRoot, node, overflow);
}

Core::Result<UITextOverflow> UIContext::textOverflowFromUpdater(UINodeId updaterRoot,
                                                                UINodeId node)
{
    return m_impl->textOverflowFromUpdater(updaterRoot, node);
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

Core::Status UIContext::setSplitViewPartsFromUpdater(
    UINodeId updaterRoot, UINodeId splitView, UINodeId primaryPane,
    UINodeId splitter, UINodeId secondaryPane)
{
    return m_impl->setSplitViewPartsFromUpdater(
        updaterRoot, splitView, primaryPane, splitter, secondaryPane);
}

Core::Status UIContext::clearSplitViewPartsFromUpdater(
    UINodeId updaterRoot, UINodeId splitView)
{
    return m_impl->clearSplitViewPartsFromUpdater(updaterRoot, splitView);
}

Core::Result<UISplitViewParts> UIContext::splitViewPartsFromUpdater(
    UINodeId updaterRoot, UINodeId splitView) const
{
    return m_impl->splitViewPartsFromUpdater(updaterRoot, splitView);
}

Core::Status UIContext::setSplitViewFractionFromUpdater(
    UINodeId updaterRoot, UINodeId splitView, float fraction)
{
    return m_impl->setSplitViewFractionFromUpdater(updaterRoot, splitView, fraction);
}

Core::Result<float> UIContext::splitViewFractionFromUpdater(
    UINodeId updaterRoot, UINodeId splitView) const
{
    return m_impl->splitViewFractionFromUpdater(updaterRoot, splitView);
}

Core::Result<UISplitViewMetrics> UIContext::splitViewMetricsFromUpdater(
    UINodeId updaterRoot, UINodeId splitView) const
{
    return m_impl->splitViewMetricsFromUpdater(updaterRoot, splitView);
}

Core::Result<bool> UIContext::isSplitterDraggingFromUpdater(
    UINodeId updaterRoot, UINodeId splitter) const
{
    return m_impl->isSplitterDraggingFromUpdater(updaterRoot, splitter);
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

Core::Status UIContext::setTooltipAnchorFromUpdater(UINodeId updaterRoot,
                                                    UINodeId tooltip,
                                                    UINodeId anchor)
{
    return m_impl->setTooltipAnchorFromUpdater(updaterRoot, tooltip, anchor);
}

Core::Status UIContext::clearTooltipAnchorFromUpdater(UINodeId updaterRoot,
                                                      UINodeId tooltip)
{
    return m_impl->clearTooltipAnchorFromUpdater(updaterRoot, tooltip);
}

Core::Result<UINodeId> UIContext::tooltipAnchorFromUpdater(
    UINodeId updaterRoot, UINodeId tooltip) const
{
    return m_impl->tooltipAnchorFromUpdater(updaterRoot, tooltip);
}

Core::Status UIContext::showTooltipFromUpdater(UINodeId updaterRoot,
                                               UINodeId tooltip)
{
    return m_impl->showTooltipFromUpdater(updaterRoot, tooltip);
}

Core::Status UIContext::dismissTooltipFromUpdater(UINodeId updaterRoot,
                                                  UINodeId tooltip)
{
    return m_impl->dismissTooltipFromUpdater(updaterRoot, tooltip);
}

Core::Result<bool> UIContext::isTooltipOpenFromUpdater(
    UINodeId updaterRoot, UINodeId tooltip) const
{
    return m_impl->isTooltipOpenFromUpdater(updaterRoot, tooltip);
}

Core::Result<UITooltipMetrics> UIContext::tooltipMetricsFromUpdater(
    UINodeId updaterRoot, UINodeId tooltip) const
{
    return m_impl->tooltipMetricsFromUpdater(updaterRoot, tooltip);
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
