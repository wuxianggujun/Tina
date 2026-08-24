#pragma once

#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/ui/UIContextConfig.hpp>
#include <tina/ui/UIContextStatistics.hpp>

#include <optional>
#include <string_view>
#include <thread>
#include <vector>

namespace Tina::Runtime::Detail {

enum class PrimaryWindowUIPhase : u8 {
    None,
    GameStateEnter,
    UIUpdate,
};

// Runtime-private epoch owner. One instance serializes all Game SDK access to
// the primary-window UIContext and retains the first facade error until the
// enclosing callback finishes.
class PrimaryWindowUICapabilityState final {
  public:
    explicit PrimaryWindowUICapabilityState(
        usize imageResolverCapacity = UI::UIContextCapacityConfig::DefaultRootCapacity);

    PrimaryWindowUICapabilityState(const PrimaryWindowUICapabilityState&) = delete;
    PrimaryWindowUICapabilityState& operator=(const PrimaryWindowUICapabilityState&) = delete;
    PrimaryWindowUICapabilityState(PrimaryWindowUICapabilityState&&) = delete;
    PrimaryWindowUICapabilityState& operator=(PrimaryWindowUICapabilityState&&) = delete;

    [[nodiscard]] Core::Result<u64> beginGameStateEnterPhase(UI::UIContext* context);
    [[nodiscard]] Core::Result<u64> beginUIUpdatePhase(UI::UIContext* context);
    [[nodiscard]] Core::Status finishPhase(u64 epoch, PrimaryWindowUIPhase phase);
    void abortPhase(u64 epoch, PrimaryWindowUIPhase phase) noexcept;

    [[nodiscard]] bool hasPrimaryWindowUI(u64 epoch, PrimaryWindowUIPhase phase) const noexcept;
    // Last committed semantics (after startup/frame commitLayout). Owner-thread, phase-scoped.
    // For accessibility adapters / product evidence; not a platform UIA bridge.
    [[nodiscard]] Core::Result<UI::UICommittedSemanticsView> committedSemantics(u64 epoch, PrimaryWindowUIPhase phase);
    [[nodiscard]] Core::Result<UI::UIContextStatistics> statistics(u64 epoch, PrimaryWindowUIPhase phase);
    [[nodiscard]] Core::Result<PrimaryWindowUIRootBuilder> rootBuilder(u64 epoch);
    [[nodiscard]] Core::Result<PrimaryWindowUITreeUpdater> treeUpdater(u64 epoch, PrimaryWindowUIPhase phase,
                                                                       UI::UIRootOwner& rootOwner);
    [[nodiscard]] Core::Result<PrimaryWindowUIImageResolverRegistration>
    bindImageResolver(u64 epoch, UI::UIRootOwner& rootOwner,
                      Render::Texture2DFrameResourceResolver resolver);
    [[nodiscard]] const Render::Texture2DFrameResourceResolver*
    findImageResolver(UI::UINodeId root) const noexcept;

    [[nodiscard]] Core::Result<UI::UIStyleClassId> registerStyleClass(u64 epoch);
    [[nodiscard]] Core::Result<UI::UIStyleTokenId>
    registerStyleColorToken(u64 epoch, UI::UIStraightSrgba8Color value);
    [[nodiscard]] Core::Status installStyleSheet(
        u64 epoch, std::span<const UI::UIStyleBoxFillRule> rules);
    [[nodiscard]] Core::Result<UI::UITheme> rootBuilderProductTheme(u64 epoch);
    [[nodiscard]] Core::Status setRootBuilderProductTheme(u64 epoch, const UI::UITheme& theme);
    [[nodiscard]] Core::Result<UI::UIRootOwner> createRoot(u64 epoch);
    [[nodiscard]] Core::Result<bool> isAlive(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
                                             UI::UINodeId node);
    [[nodiscard]] Core::Result<UI::UILogicalRect>
    committedLayoutRect(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
                        UI::UINodeId node);
    [[nodiscard]] Core::Result<UI::UINodeId>
    createElement(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater, UI::UINodeId parent,
                  const UI::UIElementDescriptor& descriptor);
    [[nodiscard]] Core::Result<UI::UIFlowLayerId>
    registerFlowLayer(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater, UI::UINodeId layer);
    [[nodiscard]] Core::Result<UI::UIFlowScreenId>
    registerFlowScreen(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                       UI::UIFlowLayerId layer, UI::UINodeId screen);
    [[nodiscard]] Core::Status
    pushFlowScreen(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                   UI::UIFlowScreenId screen);
    [[nodiscard]] Core::Result<UI::UIFlowScreenId>
    popFlowScreen(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                  UI::UIFlowLayerId layer);
    [[nodiscard]] Core::Result<UI::UIFlowScreenId>
    replaceFlowScreen(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                      UI::UIFlowScreenId screen);
    [[nodiscard]] Core::Result<UI::UIFlowScreenId>
    activeFlowScreen(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
                     UI::UIFlowLayerId layer);
    [[nodiscard]] Core::Result<bool>
    isFlowScreenActive(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
                       UI::UIFlowScreenId screen);
    [[nodiscard]] Core::Status
    assignFlowGamepad(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                      Platform::GamepadId gamepad, UI::UIFlowLocalUserId localUser);
    [[nodiscard]] Core::Status
    clearFlowGamepadAssignment(u64 epoch, PrimaryWindowUIPhase phase,
                               UI::UITreeUpdater& updater, Platform::GamepadId gamepad);
    [[nodiscard]] Core::Result<UI::UIFlowLocalUserId>
    flowLocalUserForGamepad(u64 epoch, PrimaryWindowUIPhase phase,
                            const UI::UITreeUpdater& updater, Platform::GamepadId gamepad);
    [[nodiscard]] Core::Result<UI::UIFlowInputDeviceState>
    flowInputDeviceState(u64 epoch, PrimaryWindowUIPhase phase,
                         const UI::UITreeUpdater& updater, UI::UIFlowLocalUserId localUser);
    [[nodiscard]] Core::Status
    setFlowScreenAction(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                        UI::UIFlowScreenId screen, UI::UIFlowAction action,
                        UI::UIFlowActionCallback callback);
    [[nodiscard]] Core::Status
    clearFlowScreenAction(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                          UI::UIFlowScreenId screen, UI::UIFlowAction action);
    [[nodiscard]] Core::Result<PrimaryWindowUIBuildTransaction>
    beginBuildTransaction(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                          UI::UINodeId parent, const UI::UIElementDescriptor& rootDescriptor,
                          UI::UIComponentBuildBudget budget);
    [[nodiscard]] Core::Result<UI::UIIconButtonParts>
    buildIconButton(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                    UI::UINodeId parent, const UI::UIIconButtonConfig& config);
    [[nodiscard]] Core::Result<UI::UIFormFieldParts>
    buildFormField(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                   UI::UINodeId parent, const UI::UIFormFieldConfig& config);
    [[nodiscard]] Core::Result<UI::UIDialogParts>
    buildDialog(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                UI::UINodeId parent, const UI::UIDialogConfig& config);
    [[nodiscard]] Core::Result<UI::UISnackbarHostParts>
    buildSnackbarHost(u64 epoch, PrimaryWindowUIPhase phase,
                      UI::UITreeUpdater& updater, UI::UINodeId parent,
                      const UI::UISnackbarHostConfig& config);
    [[nodiscard]] Core::Result<UI::UINumberFieldParts>
    buildNumberField(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                     UI::UINodeId parent, const UI::UINumberFieldConfig& config);
    [[nodiscard]] Core::Result<UI::UICollapsibleSectionParts>
    buildCollapsibleSection(u64 epoch, PrimaryWindowUIPhase phase,
                            UI::UITreeUpdater& updater, UI::UINodeId parent,
                            const UI::UICollapsibleSectionConfig& config);
    [[nodiscard]] Core::Result<UI::UIColorFieldParts>
    buildColorField(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                    UI::UINodeId parent, const UI::UIColorFieldConfig& config);
    [[nodiscard]] Core::Result<UI::UIColorPickerParts>
    buildColorPicker(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                     UI::UINodeId parent, const UI::UIColorPickerConfig& config);
    [[nodiscard]] Core::Result<UI::UINodeId>
    createElementFromBuildTransaction(u64 epoch, PrimaryWindowUIPhase phase, UI::UINodeId parent,
                                      const UI::UIElementDescriptor& descriptor);
    [[nodiscard]] Core::Result<UI::UINodeId>
    commitBuildTransaction(u64 epoch, PrimaryWindowUIPhase phase);
    void resetBuildTransaction(u64 epoch, PrimaryWindowUIPhase phase) noexcept;
    [[nodiscard]] UI::UINodeId buildTransactionRootNodeId(u64 epoch,
                                                          PrimaryWindowUIPhase phase) const noexcept;
    [[nodiscard]] UI::UIComponentBuildBudget buildTransactionRemainingBudget(
        u64 epoch, PrimaryWindowUIPhase phase) const noexcept;
    [[nodiscard]] bool isBuildTransactionActive(u64 epoch,
                                                PrimaryWindowUIPhase phase) const noexcept;
    [[nodiscard]] Core::Status setLayoutStyle(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                              UI::UINodeId node, const UI::UILayoutStyle& style);
    [[nodiscard]] Core::Status setPointerHitPolicy(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                   UI::UINodeId node, UI::UIPointerHitPolicy policy);
    [[nodiscard]] Core::Status setEnabled(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                          UI::UINodeId node, bool enabled);
    [[nodiscard]] Core::Result<bool> isEnabled(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
                                               UI::UINodeId node);
    [[nodiscard]] Core::Status setFocusScopeMode(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                 UI::UINodeId node, UI::UIFocusScopeMode mode);
    [[nodiscard]] Core::Result<UI::UIFocusScopeMode>
    focusScopeMode(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater, UI::UINodeId node);
    [[nodiscard]] Core::Status requestFocus(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                            UI::UINodeId node);
    [[nodiscard]] Core::Status clearFocus(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater);
    [[nodiscard]] Core::Status setStyleRole(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                            UI::UINodeId node, UI::UIStyleRoleId role);
    [[nodiscard]] Core::Result<UI::UIStyleRoleId>
    styleRole(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater, UI::UINodeId node);
    [[nodiscard]] Core::Result<UI::UIStraightSrgba8Color>
    styleColorToken(u64 epoch, PrimaryWindowUIPhase phase, UI::UIStyleTokenId token);
    [[nodiscard]] Core::Status setStyleColorToken(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UIStyleTokenId token,
        UI::UIStraightSrgba8Color value);
    [[nodiscard]] Core::Status clearOverride(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                             UI::UINodeId node, UI::UIStyleOverride properties);
    [[nodiscard]] Core::Result<UI::UITheme> productTheme(u64 epoch, PrimaryWindowUIPhase phase);
    [[nodiscard]] Core::Status setProductTheme(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITheme& theme);
    [[nodiscard]] Core::Status setBoxPaint(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                           UI::UINodeId node, const UI::UIBoxPaint& paint);
    [[nodiscard]] Core::Status setImageTint(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                            UI::UINodeId node, UI::UIStraightSrgba8Color tint);
    [[nodiscard]] Core::Result<UI::UIStraightSrgba8Color>
    imageTint(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater, UI::UINodeId node);
    [[nodiscard]] Core::Status setReducedMotion(u64 epoch, PrimaryWindowUIPhase phase, bool enabled);
    [[nodiscard]] Core::Result<bool> reducedMotion(u64 epoch, PrimaryWindowUIPhase phase);
    [[nodiscard]] Core::Status setStyleBackgroundColorTransition(u64 epoch, PrimaryWindowUIPhase phase,
                                                                 const UI::UITransitionSpec& spec);
    [[nodiscard]] Core::Result<UI::UITransitionSpec> styleBackgroundColorTransition(u64 epoch,
                                                                                    PrimaryWindowUIPhase phase);
    [[nodiscard]] Core::Status beginBackgroundColorTransition(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UINodeId node, UI::UIStraightSrgba8Color target,
        const UI::UITransitionSpec& spec);
    [[nodiscard]] Core::Status beginBorderColorTransition(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UINodeId node, UI::UIStraightSrgba8Color target,
        const UI::UITransitionSpec& spec);
    [[nodiscard]] Core::Status beginTextColorTransition(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UINodeId node, UI::UIStraightSrgba8Color target,
        const UI::UITransitionSpec& spec);
    [[nodiscard]] Core::Status beginOpacityTransition(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UINodeId node, float targetOpacity,
        const UI::UITransitionSpec& spec);
    [[nodiscard]] Core::Status beginCornerRadiusTransition(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UINodeId node, float targetRadius,
        const UI::UITransitionSpec& spec);
    [[nodiscard]] Core::Status beginVisualOffsetTransition(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UINodeId node, float targetOffsetX, float targetOffsetY,
        const UI::UITransitionSpec& spec);
    [[nodiscard]] Core::Result<UI::UITimelineId> createTimeline(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITimelineDesc& desc);
    [[nodiscard]] Core::Status replaceTimeline(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITimelineId timeline,
        const UI::UITimelineDesc& desc);
    [[nodiscard]] Core::Status playTimeline(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITimelineId timeline);
    [[nodiscard]] Core::Status cancelTimeline(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITimelineId timeline);
    [[nodiscard]] Core::Status destroyTimeline(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITimelineId timeline);
    [[nodiscard]] Core::Result<bool> isTimelineActive(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITimelineId timeline);
    [[nodiscard]] Core::Status setButtonPaint(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                              UI::UINodeId button, const UI::UIButtonPaint& paint);
    [[nodiscard]] Core::Result<UI::UIButtonPaint> buttonPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                              const UI::UITreeUpdater& updater, UI::UINodeId button);
    [[nodiscard]] Core::Status setText(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                       UI::UINodeId node, std::string_view utf8);
    [[nodiscard]] Core::Status setTextStyle(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                            UI::UINodeId node, const UI::UITextStyle& style);
    [[nodiscard]] Core::Status setTextOverflow(u64 epoch, PrimaryWindowUIPhase phase,
                                               UI::UITreeUpdater& updater, UI::UINodeId node,
                                               UI::UITextOverflow overflow);
    [[nodiscard]] Core::Result<UI::UITextOverflow>
    textOverflow(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                 UI::UINodeId node);
    [[nodiscard]] Core::Status setContentAlignment(u64 epoch, PrimaryWindowUIPhase phase,
                                                   UI::UITreeUpdater& updater, UI::UINodeId node,
                                                   UI::UIContentAlignment alignment);
    [[nodiscard]] Core::Result<std::string_view> text(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                      UI::UINodeId node);
    [[nodiscard]] Core::Result<UI::UITextStyle> textStyle(u64 epoch, PrimaryWindowUIPhase phase,
                                                          UI::UITreeUpdater& updater, UI::UINodeId node);
    [[nodiscard]] Core::Result<UI::UIContentAlignment>
    contentAlignment(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
                     UI::UINodeId node);
    [[nodiscard]] Core::Status setTextSelection(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                UI::UINodeId textEdit, UI::UITextSelection selection);
    [[nodiscard]] Core::Result<UI::UITextSelection>
    textSelection(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater, UI::UINodeId textEdit);
    [[nodiscard]] Core::Status setTextEditPaint(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                UI::UINodeId textEdit, const UI::UITextEditPaint& paint);
    [[nodiscard]] Core::Result<UI::UITextEditPaint>
    textEditPaint(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater, UI::UINodeId textEdit);
    [[nodiscard]] Core::Status setButtonAction(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                               UI::UINodeId button, UI::UIButtonActionCallback callback);
    [[nodiscard]] Core::Status clearButtonAction(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                 UI::UINodeId button);
    [[nodiscard]] Core::Result<bool> isButtonPressed(u64 epoch, PrimaryWindowUIPhase phase,
                                                     const UI::UITreeUpdater& updater, UI::UINodeId button);
    [[nodiscard]] Core::Status setCheckboxAction(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                 UI::UINodeId checkbox, UI::UIButtonActionCallback callback);
    [[nodiscard]] Core::Status clearCheckboxAction(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                   UI::UINodeId checkbox);
    [[nodiscard]] Core::Status setCheckboxPaint(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                UI::UINodeId checkbox, const UI::UICheckboxPaint& paint);
    [[nodiscard]] Core::Result<UI::UICheckboxPaint>
    checkboxPaint(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater, UI::UINodeId checkbox);
    [[nodiscard]] Core::Status setChecked(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                          UI::UINodeId checkbox, bool checked);
    [[nodiscard]] Core::Result<bool> isChecked(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
                                               UI::UINodeId checkbox);
    [[nodiscard]] Core::Result<bool> isCheckboxPressed(u64 epoch, PrimaryWindowUIPhase phase,
                                                       const UI::UITreeUpdater& updater, UI::UINodeId checkbox);
    [[nodiscard]] Core::Status setSliderRange(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                              UI::UINodeId slider, float minValue, float maxValue, float step);
    [[nodiscard]] Core::Status setSliderValue(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                              UI::UINodeId slider, float value);
    [[nodiscard]] Core::Result<float> sliderValue(u64 epoch, PrimaryWindowUIPhase phase,
                                                  const UI::UITreeUpdater& updater, UI::UINodeId slider);
    [[nodiscard]] Core::Status setSliderPaint(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                              UI::UINodeId slider, const UI::UISliderPaint& paint);
    [[nodiscard]] Core::Result<UI::UISliderPaint> sliderPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                              const UI::UITreeUpdater& updater, UI::UINodeId slider);
    [[nodiscard]] Core::Status setSliderChangeCallback(u64 epoch, PrimaryWindowUIPhase phase,
                                                       UI::UITreeUpdater& updater, UI::UINodeId slider,
                                                       UI::UISliderChangeCallback callback);
    [[nodiscard]] Core::Status clearSliderChangeCallback(u64 epoch, PrimaryWindowUIPhase phase,
                                                         UI::UITreeUpdater& updater, UI::UINodeId slider);
    [[nodiscard]] Core::Result<bool> isSliderDragging(u64 epoch, PrimaryWindowUIPhase phase,
                                                      const UI::UITreeUpdater& updater, UI::UINodeId slider);
    [[nodiscard]] Core::Status setSplitViewParts(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId splitView, UI::UINodeId primaryPane, UI::UINodeId splitter,
        UI::UINodeId secondaryPane);
    [[nodiscard]] Core::Status clearSplitViewParts(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId splitView);
    [[nodiscard]] Core::Result<UI::UISplitViewParts> splitViewParts(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId splitView);
    [[nodiscard]] Core::Status setSplitViewFraction(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId splitView, float fraction);
    [[nodiscard]] Core::Result<float> splitViewFraction(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId splitView);
    [[nodiscard]] Core::Result<UI::UISplitViewMetrics> splitViewMetrics(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId splitView);
    [[nodiscard]] Core::Result<bool> isSplitterDragging(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId splitter);
    [[nodiscard]] Core::Status setSplitterPaint(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId splitter, const UI::UISplitterPaint& paint);
    [[nodiscard]] Core::Result<UI::UISplitterPaint> splitterPaint(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId splitter);
    [[nodiscard]] Core::Status setTabViewItems(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId tabView, std::span<const UI::UITabViewItem> items, u32 activeIndex);
    [[nodiscard]] Core::Status clearTabViewItems(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId tabView);
    [[nodiscard]] Core::Result<u32> tabViewItemCount(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId tabView);
    [[nodiscard]] Core::Result<UI::UITabViewItem> tabViewItemAt(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId tabView, u32 index);
    [[nodiscard]] Core::Status setTabViewActiveTab(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId tabView, UI::UINodeId tab);
    [[nodiscard]] Core::Result<UI::UINodeId> tabViewActiveTab(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId tabView);
    [[nodiscard]] Core::Result<UI::UINodeId> tabViewActivePanel(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId tabView);
    [[nodiscard]] Core::Result<UI::UITabViewMetrics> tabViewMetrics(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId tabView);
    [[nodiscard]] Core::Result<UI::UITabViewCommandResult> routeTabViewCommand(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId tabView, UI::UITabViewCommand command);
    [[nodiscard]] Core::Status setTabPaint(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId tab, const UI::UITabPaint& paint);
    [[nodiscard]] Core::Result<UI::UITabPaint> tabPaint(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId tab);
    [[nodiscard]] Core::Status setScrollViewStyle(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                  UI::UINodeId scrollView, const UI::UIScrollViewStyle& style);
    [[nodiscard]] Core::Result<UI::UIScrollViewStyle>
    scrollViewStyle(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
                    UI::UINodeId scrollView);
    [[nodiscard]] Core::Status setScrollViewOffset(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                   UI::UINodeId scrollView, UI::UIScrollOffset offset);
    [[nodiscard]] Core::Result<UI::UIScrollOffset>
    scrollViewOffset(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
                     UI::UINodeId scrollView);
    [[nodiscard]] Core::Result<UI::UIScrollViewMetrics>
    scrollViewMetrics(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
                      UI::UINodeId scrollView);
    [[nodiscard]] Core::Status setScrollViewPaint(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                  UI::UINodeId scrollView, const UI::UIScrollViewPaint& paint);
    [[nodiscard]] Core::Result<UI::UIScrollViewPaint>
    scrollViewPaint(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
                    UI::UINodeId scrollView);
    [[nodiscard]] Core::Result<bool> isScrollViewDragging(u64 epoch, PrimaryWindowUIPhase phase,
                                                          const UI::UITreeUpdater& updater,
                                                          UI::UINodeId scrollView);
    [[nodiscard]] Core::Status setListViewDataSource(u64 epoch, PrimaryWindowUIPhase phase,
                                                     UI::UITreeUpdater& updater, UI::UINodeId listView,
                                                     UI::UIListViewDataSource source);
    [[nodiscard]] Core::Status clearListViewDataSource(u64 epoch, PrimaryWindowUIPhase phase,
                                                       UI::UITreeUpdater& updater, UI::UINodeId listView);
    [[nodiscard]] Core::Status invalidateListViewItems(u64 epoch, PrimaryWindowUIPhase phase,
                                                       UI::UITreeUpdater& updater, UI::UINodeId listView);
    [[nodiscard]] Core::Status setListViewStyle(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                UI::UINodeId listView, const UI::UIListViewStyle& style);
    [[nodiscard]] Core::Result<UI::UIListViewStyle>
    listViewStyle(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater, UI::UINodeId listView);
    [[nodiscard]] Core::Status setListViewPaint(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                UI::UINodeId listView, const UI::UIListViewPaint& paint);
    [[nodiscard]] Core::Result<UI::UIListViewPaint>
    listViewPaint(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater, UI::UINodeId listView);
    [[nodiscard]] Core::Result<UI::UIListViewMetrics>
    listViewMetrics(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater, UI::UINodeId listView);
    [[nodiscard]] Core::Status setListViewSelectedIndex(u64 epoch, PrimaryWindowUIPhase phase,
                                                        UI::UITreeUpdater& updater, UI::UINodeId listView,
                                                        u64 logicalIndex);
    [[nodiscard]] Core::Status clearListViewSelection(u64 epoch, PrimaryWindowUIPhase phase,
                                                      UI::UITreeUpdater& updater, UI::UINodeId listView);
    [[nodiscard]] Core::Result<UI::UIListViewSelection>
    listViewSelection(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
                      UI::UINodeId listView);
    [[nodiscard]] Core::Status scrollListViewToIndex(u64 epoch, PrimaryWindowUIPhase phase,
                                                     UI::UITreeUpdater& updater, UI::UINodeId listView,
                                                     u64 logicalIndex, UI::UIListViewScrollAlignment alignment);
    [[nodiscard]] Core::Status setVirtualGridViewDataSource(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId virtualGridView, UI::UIVirtualGridViewDataSource source);
    [[nodiscard]] Core::Status clearVirtualGridViewDataSource(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId virtualGridView);
    [[nodiscard]] Core::Status invalidateVirtualGridViewItems(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId virtualGridView);
    [[nodiscard]] Core::Status setVirtualGridViewStyle(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId virtualGridView, const UI::UIVirtualGridViewStyle& style);
    [[nodiscard]] Core::Result<UI::UIVirtualGridViewStyle>
    virtualGridViewStyle(u64 epoch, PrimaryWindowUIPhase phase,
                         const UI::UITreeUpdater& updater,
                         UI::UINodeId virtualGridView);
    [[nodiscard]] Core::Status setVirtualGridViewPaint(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId virtualGridView, const UI::UIVirtualGridViewPaint& paint);
    [[nodiscard]] Core::Result<UI::UIVirtualGridViewPaint>
    virtualGridViewPaint(u64 epoch, PrimaryWindowUIPhase phase,
                         const UI::UITreeUpdater& updater,
                         UI::UINodeId virtualGridView);
    [[nodiscard]] Core::Result<UI::UIVirtualGridViewMetrics>
    virtualGridViewMetrics(u64 epoch, PrimaryWindowUIPhase phase,
                           const UI::UITreeUpdater& updater,
                           UI::UINodeId virtualGridView);
    [[nodiscard]] Core::Result<UI::UINodeId>
    virtualGridViewMaterializedItemNode(
        u64 epoch, PrimaryWindowUIPhase phase,
        const UI::UITreeUpdater& updater, UI::UINodeId virtualGridView,
        u64 logicalIndex);
    [[nodiscard]] Core::Status setVirtualGridViewSelectedIndex(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId virtualGridView, u64 logicalIndex);
    [[nodiscard]] Core::Status clearVirtualGridViewSelection(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId virtualGridView);
    [[nodiscard]] Core::Result<UI::UIVirtualGridViewSelection>
    virtualGridViewSelection(u64 epoch, PrimaryWindowUIPhase phase,
                             const UI::UITreeUpdater& updater,
                             UI::UINodeId virtualGridView);
    [[nodiscard]] Core::Status scrollVirtualGridViewToIndex(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId virtualGridView, u64 logicalIndex,
        UI::UIVirtualGridViewScrollAlignment alignment);
    [[nodiscard]] Core::Status setDataGridDataSource(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId dataGrid, UI::UIDataGridDataSource source);
    [[nodiscard]] Core::Status clearDataGridDataSource(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId dataGrid);
    [[nodiscard]] Core::Status invalidateDataGridItems(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId dataGrid);
    [[nodiscard]] Core::Status setDataGridStyle(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId dataGrid, const UI::UIDataGridStyle& style);
    [[nodiscard]] Core::Result<UI::UIDataGridStyle>
    dataGridStyle(u64 epoch, PrimaryWindowUIPhase phase,
                  const UI::UITreeUpdater& updater, UI::UINodeId dataGrid);
    [[nodiscard]] Core::Status setDataGridPaint(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId dataGrid, const UI::UIDataGridPaint& paint);
    [[nodiscard]] Core::Result<UI::UIDataGridPaint>
    dataGridPaint(u64 epoch, PrimaryWindowUIPhase phase,
                  const UI::UITreeUpdater& updater, UI::UINodeId dataGrid);
    [[nodiscard]] Core::Result<UI::UIDataGridMetrics>
    dataGridMetrics(u64 epoch, PrimaryWindowUIPhase phase,
                    const UI::UITreeUpdater& updater, UI::UINodeId dataGrid);
    [[nodiscard]] Core::Status setDataGridSelectedCell(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId dataGrid, u64 logicalRow, u32 logicalColumn);
    [[nodiscard]] Core::Status clearDataGridSelection(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId dataGrid);
    [[nodiscard]] Core::Result<UI::UIDataGridSelection>
    dataGridSelection(u64 epoch, PrimaryWindowUIPhase phase,
                      const UI::UITreeUpdater& updater, UI::UINodeId dataGrid);
    [[nodiscard]] Core::Status scrollDataGridToCell(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId dataGrid, u64 logicalRow, u32 logicalColumn,
        UI::UIDataGridScrollAlignment alignment);
    [[nodiscard]] Core::Status setTreeViewDataSource(u64 epoch, PrimaryWindowUIPhase phase,
                                                     UI::UITreeUpdater& updater, UI::UINodeId treeView,
                                                     UI::UITreeViewDataSource source);
    [[nodiscard]] Core::Status clearTreeViewDataSource(u64 epoch, PrimaryWindowUIPhase phase,
                                                       UI::UITreeUpdater& updater, UI::UINodeId treeView);
    [[nodiscard]] Core::Status invalidateTreeViewItems(u64 epoch, PrimaryWindowUIPhase phase,
                                                       UI::UITreeUpdater& updater, UI::UINodeId treeView);
    [[nodiscard]] Core::Status setTreeViewStyle(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                UI::UINodeId treeView, const UI::UITreeViewStyle& style);
    [[nodiscard]] Core::Result<UI::UITreeViewStyle>
    treeViewStyle(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater, UI::UINodeId treeView);
    [[nodiscard]] Core::Status setTreeViewPaint(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                UI::UINodeId treeView, const UI::UITreeViewPaint& paint);
    [[nodiscard]] Core::Result<UI::UITreeViewPaint>
    treeViewPaint(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater, UI::UINodeId treeView);
    [[nodiscard]] Core::Result<UI::UITreeViewMetrics>
    treeViewMetrics(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater, UI::UINodeId treeView);
    [[nodiscard]] Core::Result<UI::UINodeId> treeViewMaterializedItemNode(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId treeView, u64 logicalIndex);
    [[nodiscard]] Core::Status setTreeViewSelectedIndex(u64 epoch, PrimaryWindowUIPhase phase,
                                                        UI::UITreeUpdater& updater, UI::UINodeId treeView,
                                                        u64 logicalIndex);
    [[nodiscard]] Core::Status clearTreeViewSelection(u64 epoch, PrimaryWindowUIPhase phase,
                                                      UI::UITreeUpdater& updater, UI::UINodeId treeView);
    [[nodiscard]] Core::Result<UI::UITreeViewSelection>
    treeViewSelection(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
                      UI::UINodeId treeView);
    [[nodiscard]] Core::Status setTreeViewItemExpanded(u64 epoch, PrimaryWindowUIPhase phase,
                                                       UI::UITreeUpdater& updater, UI::UINodeId treeView,
                                                       u64 logicalIndex, bool expanded);
    [[nodiscard]] Core::Status scrollTreeViewToIndex(u64 epoch, PrimaryWindowUIPhase phase,
                                                     UI::UITreeUpdater& updater, UI::UINodeId treeView,
                                                     u64 logicalIndex, UI::UITreeViewScrollAlignment alignment);
    [[nodiscard]] Core::Status setPopupStyle(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                             UI::UINodeId popup, const UI::UIPopupStyle& style);
    [[nodiscard]] Core::Result<UI::UIPopupStyle> popupStyle(u64 epoch, PrimaryWindowUIPhase phase,
                                                            const UI::UITreeUpdater& updater, UI::UINodeId popup);
    [[nodiscard]] Core::Status setPopupOpen(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                           UI::UINodeId popup, bool open);
    [[nodiscard]] Core::Result<bool> isPopupOpen(u64 epoch, PrimaryWindowUIPhase phase,
                                                const UI::UITreeUpdater& updater, UI::UINodeId popup);
    [[nodiscard]] Core::Result<UI::UIPopupMetrics> popupMetrics(u64 epoch, PrimaryWindowUIPhase phase,
                                                                const UI::UITreeUpdater& updater, UI::UINodeId popup);
    [[nodiscard]] Core::Status openDialog(u64 epoch, PrimaryWindowUIPhase phase,
                                          UI::UITreeUpdater& updater, UI::UINodeId dialog);
    [[nodiscard]] Core::Status dismissDialog(u64 epoch, PrimaryWindowUIPhase phase,
                                             UI::UITreeUpdater& updater, UI::UINodeId dialog);
    [[nodiscard]] Core::Result<bool> isDialogOpen(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId dialog);
    [[nodiscard]] Core::Status setTooltipAnchor(u64 epoch, PrimaryWindowUIPhase phase,
                                                UI::UITreeUpdater& updater, UI::UINodeId tooltip,
                                                UI::UINodeId anchor);
    [[nodiscard]] Core::Status clearTooltipAnchor(u64 epoch, PrimaryWindowUIPhase phase,
                                                  UI::UITreeUpdater& updater, UI::UINodeId tooltip);
    [[nodiscard]] Core::Result<UI::UINodeId> tooltipAnchor(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId tooltip);
    [[nodiscard]] Core::Status showTooltip(u64 epoch, PrimaryWindowUIPhase phase,
                                           UI::UITreeUpdater& updater, UI::UINodeId tooltip);
    [[nodiscard]] Core::Status dismissTooltip(u64 epoch, PrimaryWindowUIPhase phase,
                                              UI::UITreeUpdater& updater, UI::UINodeId tooltip);
    [[nodiscard]] Core::Result<bool> isTooltipOpen(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId tooltip);
    [[nodiscard]] Core::Result<UI::UITooltipMetrics> tooltipMetrics(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId tooltip);
    [[nodiscard]] Core::Status setMenuAnchor(u64 epoch, PrimaryWindowUIPhase phase,
                                             UI::UITreeUpdater& updater, UI::UINodeId menu,
                                             UI::UINodeId anchor);
    [[nodiscard]] Core::Status clearMenuAnchor(u64 epoch, PrimaryWindowUIPhase phase,
                                               UI::UITreeUpdater& updater, UI::UINodeId menu);
    [[nodiscard]] Core::Result<UI::UINodeId> menuAnchor(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId menu);
    [[nodiscard]] Core::Status setMenuOpen(u64 epoch, PrimaryWindowUIPhase phase,
                                           UI::UITreeUpdater& updater, UI::UINodeId menu,
                                           bool open);
    [[nodiscard]] Core::Result<bool> isMenuOpen(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId menu);
    [[nodiscard]] Core::Result<UI::UIMenuMetrics> menuMetrics(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId menu);
    [[nodiscard]] Core::Status setMenuItemSubmenu(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId item, UI::UINodeId submenu);
    [[nodiscard]] Core::Status clearMenuItemSubmenu(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId item);
    [[nodiscard]] Core::Result<UI::UINodeId> menuItemSubmenu(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId item);
    [[nodiscard]] Core::Result<UI::UINodeId> menuParentItem(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId menu);
    [[nodiscard]] Core::Status setMenuItemChecked(u64 epoch, PrimaryWindowUIPhase phase,
                                                  UI::UITreeUpdater& updater, UI::UINodeId item,
                                                  bool checked);
    [[nodiscard]] Core::Result<bool> isMenuItemChecked(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId item);
    [[nodiscard]] Core::Result<UI::UIMenuCommandResult> routeMenuCommand(
        u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
        UI::UINodeId menu, UI::UIMenuCommand command);
    [[nodiscard]] Core::Status setDropdownOpen(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                              UI::UINodeId dropdown, bool open);
    [[nodiscard]] Core::Result<bool> isDropdownOpen(u64 epoch, PrimaryWindowUIPhase phase,
                                                   const UI::UITreeUpdater& updater, UI::UINodeId dropdown);
    [[nodiscard]] Core::Status setDropdownSelectedItem(u64 epoch, PrimaryWindowUIPhase phase,
                                                      UI::UITreeUpdater& updater, UI::UINodeId dropdown,
                                                      UI::UINodeId item);
    [[nodiscard]] Core::Result<UI::UINodeId> dropdownSelectedItem(u64 epoch, PrimaryWindowUIPhase phase,
                                                                  const UI::UITreeUpdater& updater,
                                                                  UI::UINodeId dropdown);
    [[nodiscard]] Core::Result<bool> isDropdownItemSelected(u64 epoch, PrimaryWindowUIPhase phase,
                                                           const UI::UITreeUpdater& updater, UI::UINodeId item);
    [[nodiscard]] Core::Status setDropdownPaint(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                               UI::UINodeId dropdown, const UI::UIDropdownPaint& paint);
    [[nodiscard]] Core::Result<UI::UIDropdownPaint> dropdownPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                                  const UI::UITreeUpdater& updater,
                                                                  UI::UINodeId dropdown);
    [[nodiscard]] Core::Status setProgressBarRange(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                   UI::UINodeId progressBar, float minValue, float maxValue);
    [[nodiscard]] Core::Status setProgressBarValue(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                   UI::UINodeId progressBar, float value);
    [[nodiscard]] Core::Result<float> progressBarValue(u64 epoch, PrimaryWindowUIPhase phase,
                                                       const UI::UITreeUpdater& updater, UI::UINodeId progressBar);
    [[nodiscard]] Core::Status setProgressBarPaint(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                   UI::UINodeId progressBar, const UI::UIProgressBarPaint& paint);
    [[nodiscard]] Core::Result<UI::UIProgressBarPaint>
    progressBarPaint(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater, UI::UINodeId progressBar);
    [[nodiscard]] Core::Status setRadioButtonPaint(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                   UI::UINodeId radioButton, const UI::UIRadioButtonPaint& paint);
    [[nodiscard]] Core::Result<UI::UIRadioButtonPaint>
    radioButtonPaint(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater, UI::UINodeId radioButton);
    [[nodiscard]] Core::Status setRadioButtonAction(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                    UI::UINodeId radioButton, UI::UIButtonActionCallback callback);
    [[nodiscard]] Core::Status clearRadioButtonAction(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                      UI::UINodeId radioButton);
    [[nodiscard]] Core::Status setRadioButtonSelected(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                      UI::UINodeId radioButton, bool selected);
    [[nodiscard]] Core::Result<bool> isRadioButtonSelected(u64 epoch, PrimaryWindowUIPhase phase,
                                                           const UI::UITreeUpdater& updater, UI::UINodeId radioButton);
    [[nodiscard]] Core::Result<bool> isRadioButtonPressed(u64 epoch, PrimaryWindowUIPhase phase,
                                                          const UI::UITreeUpdater& updater, UI::UINodeId radioButton);
    [[nodiscard]] Core::Result<UI::UIRoutedPointerListenerToken>
    addRoutedPointerListener(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                             UI::UIRoutedPointerListenerDesc descriptor, UI::UIRoutedPointerCallback callback);
    [[nodiscard]] Core::Status destroy(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                       UI::UINodeId node);

  private:
    struct ImageResolverSlot final {
        UI::UINodeId root{};
        Render::Texture2DFrameResourceResolver resolver{};
        u32 generation = 1;
        bool active = false;
        bool retired = false;
    };

    [[nodiscard]] Core::Result<u64> beginPhase(PrimaryWindowUIPhase phase, UI::UIContext* context);
    [[nodiscard]] Core::Status validate(u64 epoch, PrimaryWindowUIPhase phase, bool requireContext,
                                        std::string_view operation);
    [[nodiscard]] Core::Error rememberFirstError(Core::Error error, std::string_view operation);
    void unbindImageResolver(u32 slot, u32 generation) noexcept;
    [[nodiscard]] bool isImageResolverActive(u32 slot, u32 generation) const noexcept;

    std::thread::id ownerThreadId_{};
    UI::UIContext* context_ = nullptr;
    u64 epoch_ = 0;
    PrimaryWindowUIPhase phase_ = PrimaryWindowUIPhase::None;
    std::optional<Core::Error> firstError_;
    std::optional<UI::UIElementBuildTransaction> buildTransaction_;
    u64 buildTransactionEpoch_ = 0;
    PrimaryWindowUIPhase buildTransactionPhase_ = PrimaryWindowUIPhase::None;
    std::vector<ImageResolverSlot> imageResolverSlots_{};

    friend class ::Tina::PrimaryWindowUIImageResolverRegistration;
    friend class ::Tina::PrimaryWindowUIBuildTransaction;
};

} // namespace Tina::Runtime::Detail
