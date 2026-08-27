#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/Texture2DFrameResourceResolver.hpp>
#include <tina/ui/UIAuthoring.hpp>
#include <tina/ui/UIButton.hpp>
#include <tina/ui/UICheckbox.hpp>
#include <tina/ui/UICollapsibleSection.hpp>
#include <tina/ui/UIColorField.hpp>
#include <tina/ui/UIColorPicker.hpp>
#include <tina/ui/UIContent.hpp>
#include <tina/ui/UIDataGrid.hpp>
#include <tina/ui/UIDialog.hpp>
#include <tina/ui/UIDropdown.hpp>
#include <tina/ui/UIElement.hpp>
#include <tina/ui/UIListView.hpp>
#include <tina/ui/UIFormField.hpp>
#include <tina/ui/UIIconButton.hpp>
#include <tina/ui/UIMenu.hpp>
#include <tina/ui/UIMotion.hpp>
#include <tina/ui/UINumberField.hpp>
#include <tina/ui/UIProgressBar.hpp>
#include <tina/ui/UIPopup.hpp>
#include <tina/ui/UIRadioButton.hpp>
#include <tina/ui/UIScrollView.hpp>
#include <tina/ui/UISemantics.hpp>
#include <tina/ui/UISlider.hpp>
#include <tina/ui/UISnackbar.hpp>
#include <tina/ui/UISplitView.hpp>
#include <tina/ui/UIStyle.hpp>
#include <tina/ui/UITheme.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/UITextEdit.hpp>
#include <tina/ui/UITooltip.hpp>
#include <tina/ui/UITreeView.hpp>
#include <tina/ui/UIVirtualGridView.hpp>

#include <span>
#include <string_view>

namespace Tina::Runtime::Detail {

enum class PrimaryWindowUIPhase : u8;
class PrimaryWindowUICapabilityState;

} // namespace Tina::Runtime::Detail

namespace Tina {

// Move-only, phase-scoped bounded construction of one retained component
// subtree. The transaction must be committed or destroyed before its Runtime
// callback returns. An escaped active transaction is rolled back when the
// phase finishes, and later operations fail the epoch check.
class PrimaryWindowUIBuildTransaction final {
  public:
    ~PrimaryWindowUIBuildTransaction() noexcept;

    PrimaryWindowUIBuildTransaction(const PrimaryWindowUIBuildTransaction&) = delete;
    PrimaryWindowUIBuildTransaction& operator=(const PrimaryWindowUIBuildTransaction&) = delete;

    PrimaryWindowUIBuildTransaction(PrimaryWindowUIBuildTransaction&& other) noexcept;
    PrimaryWindowUIBuildTransaction& operator=(PrimaryWindowUIBuildTransaction&& other) noexcept;

    [[nodiscard]] Core::Result<UI::UINodeId> createElement(
        UI::UINodeId parent, const UI::UIElementDescriptor& descriptor);
    [[nodiscard]] Core::Result<UI::UINodeId> commit();
    void reset() noexcept;

    [[nodiscard]] UI::UINodeId rootNodeId() const noexcept;
    [[nodiscard]] UI::UIComponentBuildBudget remainingBudget() const noexcept;
    [[nodiscard]] bool isActive() const noexcept;
    explicit operator bool() const noexcept;

  private:
    PrimaryWindowUIBuildTransaction(Runtime::Detail::PrimaryWindowUICapabilityState& state,
                                    u64 epoch,
                                    Runtime::Detail::PrimaryWindowUIPhase phase) noexcept;

    Runtime::Detail::PrimaryWindowUICapabilityState* m_state = nullptr;
    u64 m_epoch = 0;
    Runtime::Detail::PrimaryWindowUIPhase m_phase{};

    friend class Runtime::Detail::PrimaryWindowUICapabilityState;
};

// Move-only ownership of one root-scoped image resolver registration. The
// registration and its resolver userData must outlive every frame that can
// paint the root, and must be reset on the Runtime owner thread before the
// corresponding UIRootOwner or EngineHost is destroyed.
class PrimaryWindowUIImageResolverRegistration final {
  public:
    PrimaryWindowUIImageResolverRegistration() noexcept = default;
    ~PrimaryWindowUIImageResolverRegistration() noexcept;

    PrimaryWindowUIImageResolverRegistration(const PrimaryWindowUIImageResolverRegistration&) = delete;
    PrimaryWindowUIImageResolverRegistration& operator=(const PrimaryWindowUIImageResolverRegistration&) = delete;

    PrimaryWindowUIImageResolverRegistration(PrimaryWindowUIImageResolverRegistration&& other) noexcept;
    PrimaryWindowUIImageResolverRegistration&
    operator=(PrimaryWindowUIImageResolverRegistration&& other) noexcept;

    void reset() noexcept;
    [[nodiscard]] bool isActive() const noexcept;
    explicit operator bool() const noexcept;

  private:
    PrimaryWindowUIImageResolverRegistration(Runtime::Detail::PrimaryWindowUICapabilityState& state,
                                              u32 slot, u32 generation) noexcept;

    Runtime::Detail::PrimaryWindowUICapabilityState* m_state = nullptr;
    u32 m_slot = 0;
    u32 m_generation = 0;

    friend class Runtime::Detail::PrimaryWindowUICapabilityState;
};

// Move-only, callback-scoped access to the retained tree owned by one primary-
// window UI root. Every operation validates the Runtime phase epoch before it
// reaches UIContext; store UIRootOwner/UINodeId, not this facade.
class PrimaryWindowUITreeUpdater final {
  public:
    PrimaryWindowUITreeUpdater(const PrimaryWindowUITreeUpdater&) = delete;
    PrimaryWindowUITreeUpdater& operator=(const PrimaryWindowUITreeUpdater&) = delete;

    PrimaryWindowUITreeUpdater(PrimaryWindowUITreeUpdater&& other) noexcept;
    PrimaryWindowUITreeUpdater& operator=(PrimaryWindowUITreeUpdater&& other) noexcept;

    [[nodiscard]] Core::Result<bool> isAlive(UI::UINodeId node) const;
    // Copies the node's world rect from the previous successful UI layout
    // publication. The query is root-scoped and expires with this phase facade.
    [[nodiscard]] Core::Result<UI::UILogicalRect> committedLayoutRect(UI::UINodeId node) const;
    [[nodiscard]] Core::Result<UI::UINodeId> createElement(UI::UINodeId parent,
                                                          const UI::UIElementDescriptor& descriptor);
    [[nodiscard]] Core::Result<UI::UIFlowLayerId> registerFlowLayer(UI::UINodeId layer);
    [[nodiscard]] Core::Result<UI::UIFlowScreenId>
    registerFlowScreen(UI::UIFlowLayerId layer, UI::UINodeId screen);
    [[nodiscard]] Core::Status pushFlowScreen(UI::UIFlowScreenId screen);
    [[nodiscard]] Core::Result<UI::UIFlowScreenId> popFlowScreen(UI::UIFlowLayerId layer);
    [[nodiscard]] Core::Result<UI::UIFlowScreenId> replaceFlowScreen(UI::UIFlowScreenId screen);
    [[nodiscard]] Core::Result<UI::UIFlowScreenId> activeFlowScreen(UI::UIFlowLayerId layer) const;
    [[nodiscard]] Core::Result<bool> isFlowScreenActive(UI::UIFlowScreenId screen) const;
    [[nodiscard]] Core::Status assignFlowGamepad(Platform::GamepadId gamepad,
                                                 UI::UIFlowLocalUserId localUser);
    [[nodiscard]] Core::Status clearFlowGamepadAssignment(Platform::GamepadId gamepad);
    [[nodiscard]] Core::Result<UI::UIFlowLocalUserId>
    flowLocalUserForGamepad(Platform::GamepadId gamepad) const;
    [[nodiscard]] Core::Result<UI::UIFlowInputDeviceState>
    flowInputDeviceState(UI::UIFlowLocalUserId localUser) const;
    [[nodiscard]] Core::Status setFlowScreenAction(UI::UIFlowScreenId screen,
                                                   UI::UIFlowAction action,
                                                   UI::UIFlowActionCallback callback);
    [[nodiscard]] Core::Status clearFlowScreenAction(UI::UIFlowScreenId screen,
                                                     UI::UIFlowAction action);
    [[nodiscard]] Core::Result<PrimaryWindowUIBuildTransaction>
    beginBuildTransaction(UI::UINodeId parent,
                          const UI::UIElementDescriptor& rootDescriptor,
                          UI::UIComponentBuildBudget budget);
    [[nodiscard]] Core::Result<UI::UIIconButtonParts>
    buildIconButton(UI::UINodeId parent, const UI::UIIconButtonConfig& config);
    [[nodiscard]] Core::Result<UI::UIFormFieldParts>
    buildFormField(UI::UINodeId parent, const UI::UIFormFieldConfig& config);
    [[nodiscard]] Core::Result<UI::UIDialogParts>
    buildDialog(UI::UINodeId parent, const UI::UIDialogConfig& config);
    [[nodiscard]] Core::Result<UI::UISnackbarHostParts>
    buildSnackbarHost(UI::UINodeId parent, const UI::UISnackbarHostConfig& config);
    [[nodiscard]] Core::Result<UI::UINumberFieldParts>
    buildNumberField(UI::UINodeId parent, const UI::UINumberFieldConfig& config);
    [[nodiscard]] Core::Result<UI::UICollapsibleSectionParts>
    buildCollapsibleSection(UI::UINodeId parent,
                            const UI::UICollapsibleSectionConfig& config);
    [[nodiscard]] Core::Result<UI::UIColorFieldParts>
    buildColorField(UI::UINodeId parent, const UI::UIColorFieldConfig& config);
    [[nodiscard]] Core::Result<UI::UIColorPickerParts>
    buildColorPicker(UI::UINodeId parent, const UI::UIColorPickerConfig& config);
    [[nodiscard]] Core::Status setLayoutStyle(UI::UINodeId node, const UI::UILayoutStyle& style);
    [[nodiscard]] Core::Status setPointerHitPolicy(UI::UINodeId node, UI::UIPointerHitPolicy policy);
    [[nodiscard]] Core::Status setEnabled(UI::UINodeId node, bool enabled);
    [[nodiscard]] Core::Result<bool> isEnabled(UI::UINodeId node) const;
    [[nodiscard]] Core::Status setFocusScopeMode(UI::UINodeId node, UI::UIFocusScopeMode mode);
    [[nodiscard]] Core::Result<UI::UIFocusScopeMode> focusScopeMode(UI::UINodeId node) const;
    [[nodiscard]] Core::Status requestFocus(UI::UINodeId node);
    [[nodiscard]] Core::Status clearFocus();
    // Committed keyboard focus owned by this root, or an unset node when focus is
    // elsewhere. Read-only, so products can commit an edit when focus leaves a
    // field without the UI layer growing a per-widget change callback.
    [[nodiscard]] Core::Result<UI::UINodeId> focusedNode() const;
    [[nodiscard]] Core::Status setStyleRole(UI::UINodeId node, UI::UIStyleRoleId role);
    [[nodiscard]] Core::Result<UI::UIStyleRoleId> styleRole(UI::UINodeId node) const;
    [[nodiscard]] Core::Result<UI::UIStraightSrgba8Color>
    styleColorToken(UI::UIStyleTokenId token) const;
    [[nodiscard]] Core::Status setStyleColorToken(
        UI::UIStyleTokenId token, UI::UIStraightSrgba8Color value);
    [[nodiscard]] Core::Status clearOverride(
        UI::UINodeId node,
        UI::UIStyleOverride properties = UI::UIStyleOverride::All);
    // Context-wide theme mutation remains phase-scoped even though this facade
    // is rooted. Existing local paint/text overrides are preserved.
    [[nodiscard]] Core::Result<UI::UITheme> productTheme() const;
    [[nodiscard]] Core::Status setProductTheme(const UI::UITheme& theme);
    [[nodiscard]] Core::Status setBoxPaint(UI::UINodeId node, const UI::UIBoxPaint& paint);
    // Paint-only: image tint/opacity does not dirty layout or hit.
    [[nodiscard]] Core::Status setImageTint(UI::UINodeId node, UI::UIStraightSrgba8Color tint);
    [[nodiscard]] Core::Result<UI::UIStraightSrgba8Color> imageTint(UI::UINodeId node) const;
    // Paint-only Motion (UI-MOTION-001). Host samples on commitLayout; M==0 is free.
    [[nodiscard]] Core::Status setReducedMotion(bool enabled);
    [[nodiscard]] Core::Result<bool> reducedMotion() const;
    [[nodiscard]] Core::Status setStyleBackgroundColorTransition(const UI::UITransitionSpec& spec);
    [[nodiscard]] Core::Result<UI::UITransitionSpec> styleBackgroundColorTransition() const;
    [[nodiscard]] Core::Status beginBackgroundColorTransition(
        UI::UINodeId node, UI::UIStraightSrgba8Color target, const UI::UITransitionSpec& spec);
    [[nodiscard]] Core::Status beginBorderColorTransition(
        UI::UINodeId node, UI::UIStraightSrgba8Color target, const UI::UITransitionSpec& spec);
    [[nodiscard]] Core::Status beginTextColorTransition(
        UI::UINodeId node, UI::UIStraightSrgba8Color target, const UI::UITransitionSpec& spec);
    [[nodiscard]] Core::Status beginOpacityTransition(
        UI::UINodeId node, float targetOpacity, const UI::UITransitionSpec& spec);
    [[nodiscard]] Core::Status beginCornerRadiusTransition(
        UI::UINodeId node, float targetRadius, const UI::UITransitionSpec& spec);
    [[nodiscard]] Core::Status beginVisualOffsetTransition(
        UI::UINodeId node, float targetOffsetX, float targetOffsetY, const UI::UITransitionSpec& spec);
    // UI-MOTION-002 typed paint/bounded-layout timeline facade. Definitions
    // are copied by UIContext and remain bounded by its fixed capacities.
    [[nodiscard]] Core::Result<UI::UITimelineId> createTimeline(const UI::UITimelineDesc& desc);
    [[nodiscard]] Core::Status replaceTimeline(UI::UITimelineId timeline,
                                               const UI::UITimelineDesc& desc);
    [[nodiscard]] Core::Status playTimeline(UI::UITimelineId timeline);
    [[nodiscard]] Core::Status cancelTimeline(UI::UITimelineId timeline);
    [[nodiscard]] Core::Status destroyTimeline(UI::UITimelineId timeline);
    [[nodiscard]] Core::Result<bool> isTimelineActive(UI::UITimelineId timeline) const;
    [[nodiscard]] Core::Status setButtonPaint(UI::UINodeId button, const UI::UIButtonPaint& paint);
    [[nodiscard]] Core::Result<UI::UIButtonPaint> buttonPaint(UI::UINodeId button) const;
    [[nodiscard]] Core::Status setText(UI::UINodeId node, std::string_view utf8);
    [[nodiscard]] Core::Status setTextStyle(UI::UINodeId node, const UI::UITextStyle& style);
    [[nodiscard]] Core::Status setTextWrapMode(UI::UINodeId node, UI::UITextWrapMode wrapMode);
    [[nodiscard]] Core::Result<UI::UITextWrapMode> textWrapMode(UI::UINodeId node);
    [[nodiscard]] Core::Status setTextLineClamp(
        UI::UINodeId node, UI::UITextLineClamp lineClamp);
    [[nodiscard]] Core::Result<UI::UITextLineClamp> textLineClamp(UI::UINodeId node);
    [[nodiscard]] Core::Status setTextOverflow(UI::UINodeId node, UI::UITextOverflow overflow);
    [[nodiscard]] Core::Result<UI::UITextOverflow> textOverflow(UI::UINodeId node);
    [[nodiscard]] Core::Status setContentAlignment(UI::UINodeId node, UI::UIContentAlignment alignment);
    [[nodiscard]] Core::Result<std::string_view> text(UI::UINodeId node);
    [[nodiscard]] Core::Result<UI::UITextStyle> textStyle(UI::UINodeId node);
    [[nodiscard]] Core::Result<UI::UIContentAlignment> contentAlignment(UI::UINodeId node) const;
    [[nodiscard]] Core::Status setTextSelection(UI::UINodeId textEdit, UI::UITextSelection selection);
    [[nodiscard]] Core::Result<UI::UITextSelection> textSelection(UI::UINodeId textEdit) const;
    [[nodiscard]] Core::Status setTextEditPaint(UI::UINodeId textEdit, const UI::UITextEditPaint& paint);
    [[nodiscard]] Core::Result<UI::UITextEditPaint> textEditPaint(UI::UINodeId textEdit) const;
    [[nodiscard]] Core::Status setButtonAction(UI::UINodeId button, UI::UIButtonActionCallback callback);
    [[nodiscard]] Core::Status clearButtonAction(UI::UINodeId button);
    [[nodiscard]] Core::Result<bool> isButtonPressed(UI::UINodeId button) const;
    [[nodiscard]] Core::Status setCheckboxAction(UI::UINodeId checkbox, UI::UIButtonActionCallback callback);
    [[nodiscard]] Core::Status clearCheckboxAction(UI::UINodeId checkbox);
    [[nodiscard]] Core::Status setCheckboxPaint(UI::UINodeId checkbox, const UI::UICheckboxPaint& paint);
    [[nodiscard]] Core::Result<UI::UICheckboxPaint> checkboxPaint(UI::UINodeId checkbox) const;
    [[nodiscard]] Core::Status setChecked(UI::UINodeId checkbox, bool checked);
    [[nodiscard]] Core::Result<bool> isChecked(UI::UINodeId checkbox) const;
    [[nodiscard]] Core::Result<bool> isCheckboxPressed(UI::UINodeId checkbox) const;
    [[nodiscard]] Core::Status setSliderRange(UI::UINodeId slider, float minValue, float maxValue, float step = 0.0F);
    [[nodiscard]] Core::Status setSliderValue(UI::UINodeId slider, float value);
    [[nodiscard]] Core::Result<float> sliderValue(UI::UINodeId slider) const;
    [[nodiscard]] Core::Status setSliderPaint(UI::UINodeId slider, const UI::UISliderPaint& paint);
    [[nodiscard]] Core::Result<UI::UISliderPaint> sliderPaint(UI::UINodeId slider) const;
    [[nodiscard]] Core::Status setSliderChangeCallback(UI::UINodeId slider, UI::UISliderChangeCallback callback);
    [[nodiscard]] Core::Status clearSliderChangeCallback(UI::UINodeId slider);
    [[nodiscard]] Core::Result<bool> isSliderDragging(UI::UINodeId slider) const;
    [[nodiscard]] Core::Status setSplitViewParts(UI::UINodeId splitView,
                                                 UI::UINodeId primaryPane,
                                                 UI::UINodeId splitter,
                                                 UI::UINodeId secondaryPane);
    [[nodiscard]] Core::Status clearSplitViewParts(UI::UINodeId splitView);
    [[nodiscard]] Core::Result<UI::UISplitViewParts>
    splitViewParts(UI::UINodeId splitView) const;
    [[nodiscard]] Core::Status setSplitViewFraction(UI::UINodeId splitView, float fraction);
    [[nodiscard]] Core::Result<float> splitViewFraction(UI::UINodeId splitView) const;
    [[nodiscard]] Core::Result<UI::UISplitViewMetrics>
    splitViewMetrics(UI::UINodeId splitView) const;
    [[nodiscard]] Core::Result<bool> isSplitterDragging(UI::UINodeId splitter) const;
    [[nodiscard]] Core::Status setSplitterPaint(
        UI::UINodeId splitter, const UI::UISplitterPaint& paint);
    [[nodiscard]] Core::Result<UI::UISplitterPaint>
    splitterPaint(UI::UINodeId splitter) const;
    [[nodiscard]] Core::Status setTabViewItems(
        UI::UINodeId tabView, std::span<const UI::UITabViewItem> items,
        u32 activeIndex = 0);
    [[nodiscard]] Core::Status clearTabViewItems(UI::UINodeId tabView);
    [[nodiscard]] Core::Result<u32> tabViewItemCount(UI::UINodeId tabView) const;
    [[nodiscard]] Core::Result<UI::UITabViewItem>
    tabViewItemAt(UI::UINodeId tabView, u32 index) const;
    [[nodiscard]] Core::Status setTabViewActiveTab(UI::UINodeId tabView, UI::UINodeId tab);
    [[nodiscard]] Core::Result<UI::UINodeId> tabViewActiveTab(UI::UINodeId tabView) const;
    [[nodiscard]] Core::Result<UI::UINodeId> tabViewActivePanel(UI::UINodeId tabView) const;
    [[nodiscard]] Core::Result<UI::UITabViewMetrics>
    tabViewMetrics(UI::UINodeId tabView) const;
    [[nodiscard]] Core::Result<UI::UITabViewCommandResult>
    routeTabViewCommand(UI::UINodeId tabView, UI::UITabViewCommand command);
    [[nodiscard]] Core::Status setTabPaint(UI::UINodeId tab, const UI::UITabPaint& paint);
    [[nodiscard]] Core::Result<UI::UITabPaint> tabPaint(UI::UINodeId tab) const;
    [[nodiscard]] Core::Status setScrollViewStyle(UI::UINodeId scrollView, const UI::UIScrollViewStyle& style);
    [[nodiscard]] Core::Result<UI::UIScrollViewStyle> scrollViewStyle(UI::UINodeId scrollView) const;
    [[nodiscard]] Core::Status setScrollViewOffset(UI::UINodeId scrollView, UI::UIScrollOffset offset);
    [[nodiscard]] Core::Result<UI::UIScrollOffset> scrollViewOffset(UI::UINodeId scrollView) const;
    [[nodiscard]] Core::Result<UI::UIScrollViewMetrics> scrollViewMetrics(UI::UINodeId scrollView) const;
    [[nodiscard]] Core::Status setScrollViewPaint(UI::UINodeId scrollView, const UI::UIScrollViewPaint& paint);
    [[nodiscard]] Core::Result<UI::UIScrollViewPaint> scrollViewPaint(UI::UINodeId scrollView) const;
    [[nodiscard]] Core::Result<bool> isScrollViewDragging(UI::UINodeId scrollView) const;
    [[nodiscard]] Core::Status setListViewDataSource(UI::UINodeId listView, UI::UIListViewDataSource source);
    [[nodiscard]] Core::Status clearListViewDataSource(UI::UINodeId listView);
    [[nodiscard]] Core::Status invalidateListViewItems(UI::UINodeId listView);
    [[nodiscard]] Core::Status setListViewStyle(UI::UINodeId listView, const UI::UIListViewStyle& style);
    [[nodiscard]] Core::Result<UI::UIListViewStyle> listViewStyle(UI::UINodeId listView) const;
    [[nodiscard]] Core::Status setListViewPaint(UI::UINodeId listView, const UI::UIListViewPaint& paint);
    [[nodiscard]] Core::Result<UI::UIListViewPaint> listViewPaint(UI::UINodeId listView) const;
    [[nodiscard]] Core::Result<UI::UIListViewMetrics> listViewMetrics(UI::UINodeId listView) const;
    [[nodiscard]] Core::Status setListViewSelectedIndex(UI::UINodeId listView, u64 logicalIndex);
    [[nodiscard]] Core::Status clearListViewSelection(UI::UINodeId listView);
    [[nodiscard]] Core::Result<UI::UIListViewSelection> listViewSelection(UI::UINodeId listView) const;
    [[nodiscard]] Core::Status
    scrollListViewToIndex(UI::UINodeId listView, u64 logicalIndex,
                          UI::UIListViewScrollAlignment alignment = UI::UIListViewScrollAlignment::Nearest);
    [[nodiscard]] Core::Status setVirtualGridViewDataSource(
        UI::UINodeId virtualGridView, UI::UIVirtualGridViewDataSource source);
    [[nodiscard]] Core::Status clearVirtualGridViewDataSource(UI::UINodeId virtualGridView);
    [[nodiscard]] Core::Status invalidateVirtualGridViewItems(UI::UINodeId virtualGridView);
    [[nodiscard]] Core::Status setVirtualGridViewStyle(
        UI::UINodeId virtualGridView, const UI::UIVirtualGridViewStyle& style);
    [[nodiscard]] Core::Result<UI::UIVirtualGridViewStyle>
    virtualGridViewStyle(UI::UINodeId virtualGridView) const;
    [[nodiscard]] Core::Status setVirtualGridViewPaint(
        UI::UINodeId virtualGridView, const UI::UIVirtualGridViewPaint& paint);
    [[nodiscard]] Core::Result<UI::UIVirtualGridViewPaint>
    virtualGridViewPaint(UI::UINodeId virtualGridView) const;
    [[nodiscard]] Core::Result<UI::UIVirtualGridViewMetrics>
    virtualGridViewMetrics(UI::UINodeId virtualGridView) const;
    [[nodiscard]] Core::Result<UI::UINodeId>
    virtualGridViewMaterializedItemNode(UI::UINodeId virtualGridView,
                                        u64 logicalIndex) const;
    [[nodiscard]] Core::Status setVirtualGridViewSelectedIndex(
        UI::UINodeId virtualGridView, u64 logicalIndex);
    [[nodiscard]] Core::Status clearVirtualGridViewSelection(UI::UINodeId virtualGridView);
    [[nodiscard]] Core::Result<UI::UIVirtualGridViewSelection>
    virtualGridViewSelection(UI::UINodeId virtualGridView) const;
    [[nodiscard]] Core::Status scrollVirtualGridViewToIndex(
        UI::UINodeId virtualGridView, u64 logicalIndex,
        UI::UIVirtualGridViewScrollAlignment alignment =
            UI::UIVirtualGridViewScrollAlignment::Nearest);
    [[nodiscard]] Core::Status setDataGridDataSource(
        UI::UINodeId dataGrid, UI::UIDataGridDataSource source);
    [[nodiscard]] Core::Status clearDataGridDataSource(UI::UINodeId dataGrid);
    [[nodiscard]] Core::Status invalidateDataGridItems(UI::UINodeId dataGrid);
    [[nodiscard]] Core::Status setDataGridStyle(
        UI::UINodeId dataGrid, const UI::UIDataGridStyle& style);
    [[nodiscard]] Core::Result<UI::UIDataGridStyle>
    dataGridStyle(UI::UINodeId dataGrid) const;
    [[nodiscard]] Core::Status setDataGridPaint(
        UI::UINodeId dataGrid, const UI::UIDataGridPaint& paint);
    [[nodiscard]] Core::Result<UI::UIDataGridPaint>
    dataGridPaint(UI::UINodeId dataGrid) const;
    [[nodiscard]] Core::Result<UI::UIDataGridMetrics>
    dataGridMetrics(UI::UINodeId dataGrid) const;
    [[nodiscard]] Core::Status setDataGridSelectedCell(
        UI::UINodeId dataGrid, u64 logicalRow, u32 logicalColumn);
    [[nodiscard]] Core::Status clearDataGridSelection(UI::UINodeId dataGrid);
    [[nodiscard]] Core::Result<UI::UIDataGridSelection>
    dataGridSelection(UI::UINodeId dataGrid) const;
    [[nodiscard]] Core::Status scrollDataGridToCell(
        UI::UINodeId dataGrid, u64 logicalRow, u32 logicalColumn,
        UI::UIDataGridScrollAlignment alignment =
            UI::UIDataGridScrollAlignment::Nearest);
    [[nodiscard]] Core::Status setTreeViewDataSource(UI::UINodeId treeView, UI::UITreeViewDataSource source);
    [[nodiscard]] Core::Status clearTreeViewDataSource(UI::UINodeId treeView);
    [[nodiscard]] Core::Status invalidateTreeViewItems(UI::UINodeId treeView);
    [[nodiscard]] Core::Status setTreeViewStyle(UI::UINodeId treeView, const UI::UITreeViewStyle& style);
    [[nodiscard]] Core::Result<UI::UITreeViewStyle> treeViewStyle(UI::UINodeId treeView) const;
    [[nodiscard]] Core::Status setTreeViewPaint(UI::UINodeId treeView, const UI::UITreeViewPaint& paint);
    [[nodiscard]] Core::Result<UI::UITreeViewPaint> treeViewPaint(UI::UINodeId treeView) const;
    [[nodiscard]] Core::Result<UI::UITreeViewMetrics> treeViewMetrics(UI::UINodeId treeView) const;
    [[nodiscard]] Core::Result<UI::UINodeId>
    treeViewMaterializedItemNode(UI::UINodeId treeView, u64 logicalIndex) const;
    [[nodiscard]] Core::Status setTreeViewSelectedIndex(UI::UINodeId treeView, u64 logicalIndex);
    [[nodiscard]] Core::Status clearTreeViewSelection(UI::UINodeId treeView);
    [[nodiscard]] Core::Result<UI::UITreeViewSelection> treeViewSelection(UI::UINodeId treeView) const;
    [[nodiscard]] Core::Status setTreeViewItemExpanded(UI::UINodeId treeView, u64 logicalIndex, bool expanded);
    [[nodiscard]] Core::Status
    scrollTreeViewToIndex(UI::UINodeId treeView, u64 logicalIndex,
                          UI::UITreeViewScrollAlignment alignment = UI::UITreeViewScrollAlignment::Nearest);
    [[nodiscard]] Core::Status setPopupStyle(UI::UINodeId popup, const UI::UIPopupStyle& style);
    [[nodiscard]] Core::Result<UI::UIPopupStyle> popupStyle(UI::UINodeId popup) const;
    [[nodiscard]] Core::Status setPopupOpen(UI::UINodeId popup, bool open);
    [[nodiscard]] Core::Result<bool> isPopupOpen(UI::UINodeId popup) const;
    [[nodiscard]] Core::Result<UI::UIPopupMetrics> popupMetrics(UI::UINodeId popup) const;
    [[nodiscard]] Core::Status openDialog(UI::UINodeId dialog);
    [[nodiscard]] Core::Status dismissDialog(UI::UINodeId dialog);
    [[nodiscard]] Core::Result<bool> isDialogOpen(UI::UINodeId dialog) const;
    [[nodiscard]] Core::Status setTooltipAnchor(UI::UINodeId tooltip, UI::UINodeId anchor);
    [[nodiscard]] Core::Status clearTooltipAnchor(UI::UINodeId tooltip);
    [[nodiscard]] Core::Result<UI::UINodeId> tooltipAnchor(UI::UINodeId tooltip) const;
    [[nodiscard]] Core::Status showTooltip(UI::UINodeId tooltip);
    [[nodiscard]] Core::Status dismissTooltip(UI::UINodeId tooltip);
    [[nodiscard]] Core::Result<bool> isTooltipOpen(UI::UINodeId tooltip) const;
    [[nodiscard]] Core::Result<UI::UITooltipMetrics> tooltipMetrics(UI::UINodeId tooltip) const;
    [[nodiscard]] Core::Status setMenuAnchor(UI::UINodeId menu, UI::UINodeId anchor);
    [[nodiscard]] Core::Status clearMenuAnchor(UI::UINodeId menu);
    [[nodiscard]] Core::Result<UI::UINodeId> menuAnchor(UI::UINodeId menu) const;
    [[nodiscard]] Core::Status setMenuOpen(UI::UINodeId menu, bool open);
    [[nodiscard]] Core::Result<bool> isMenuOpen(UI::UINodeId menu) const;
    [[nodiscard]] Core::Result<UI::UIMenuMetrics> menuMetrics(UI::UINodeId menu) const;
    [[nodiscard]] Core::Status setMenuItemSubmenu(UI::UINodeId item, UI::UINodeId submenu);
    [[nodiscard]] Core::Status clearMenuItemSubmenu(UI::UINodeId item);
    [[nodiscard]] Core::Result<UI::UINodeId> menuItemSubmenu(UI::UINodeId item) const;
    [[nodiscard]] Core::Result<UI::UINodeId> menuParentItem(UI::UINodeId menu) const;
    [[nodiscard]] Core::Status setMenuItemChecked(UI::UINodeId item, bool checked);
    [[nodiscard]] Core::Result<bool> isMenuItemChecked(UI::UINodeId item) const;
    [[nodiscard]] Core::Result<UI::UIMenuCommandResult>
    routeMenuCommand(UI::UINodeId menu, UI::UIMenuCommand command);
    [[nodiscard]] Core::Status setDropdownOpen(UI::UINodeId dropdown, bool open);
    [[nodiscard]] Core::Result<bool> isDropdownOpen(UI::UINodeId dropdown) const;
    [[nodiscard]] Core::Status setDropdownSelectedItem(UI::UINodeId dropdown, UI::UINodeId item);
    [[nodiscard]] Core::Result<UI::UINodeId> dropdownSelectedItem(UI::UINodeId dropdown) const;
    [[nodiscard]] Core::Result<bool> isDropdownItemSelected(UI::UINodeId item) const;
    [[nodiscard]] Core::Status setDropdownPaint(UI::UINodeId dropdown, const UI::UIDropdownPaint& paint);
    [[nodiscard]] Core::Result<UI::UIDropdownPaint> dropdownPaint(UI::UINodeId dropdown) const;
    [[nodiscard]] Core::Status setProgressBarRange(UI::UINodeId progressBar, float minValue, float maxValue);
    [[nodiscard]] Core::Status setProgressBarValue(UI::UINodeId progressBar, float value);
    [[nodiscard]] Core::Result<float> progressBarValue(UI::UINodeId progressBar) const;
    [[nodiscard]] Core::Status setProgressBarPaint(UI::UINodeId progressBar, const UI::UIProgressBarPaint& paint);
    [[nodiscard]] Core::Result<UI::UIProgressBarPaint> progressBarPaint(UI::UINodeId progressBar) const;
    [[nodiscard]] Core::Status setRadioButtonPaint(UI::UINodeId radioButton, const UI::UIRadioButtonPaint& paint);
    [[nodiscard]] Core::Result<UI::UIRadioButtonPaint> radioButtonPaint(UI::UINodeId radioButton) const;
    [[nodiscard]] Core::Status setRadioButtonAction(UI::UINodeId radioButton, UI::UIButtonActionCallback callback);
    [[nodiscard]] Core::Status clearRadioButtonAction(UI::UINodeId radioButton);
    [[nodiscard]] Core::Status setRadioButtonSelected(UI::UINodeId radioButton, bool selected);
    [[nodiscard]] Core::Result<bool> isRadioButtonSelected(UI::UINodeId radioButton) const;
    [[nodiscard]] Core::Result<bool> isRadioButtonPressed(UI::UINodeId radioButton) const;
    [[nodiscard]] Core::Result<UI::UIRoutedPointerListenerToken>
    addRoutedPointerListener(UI::UIRoutedPointerListenerDesc descriptor, UI::UIRoutedPointerCallback callback);
    [[nodiscard]] Core::Status destroy(UI::UINodeId node);

  private:
    PrimaryWindowUITreeUpdater(Runtime::Detail::PrimaryWindowUICapabilityState& state, u64 epoch,
                               Runtime::Detail::PrimaryWindowUIPhase phase, UI::UITreeUpdater updater) noexcept;

    Runtime::Detail::PrimaryWindowUICapabilityState* m_state = nullptr;
    u64 m_epoch = 0;
    Runtime::Detail::PrimaryWindowUIPhase m_phase{};
    UI::UITreeUpdater m_updater{};

    friend class Runtime::Detail::PrimaryWindowUICapabilityState;
};

// Move-only, GameStateEnter-only capability for creating retained roots. The
// returned UIRootOwner is the persistent ownership token; this builder expires
// unconditionally when the onEnter callback returns.
class PrimaryWindowUIRootBuilder final {
  public:
    PrimaryWindowUIRootBuilder(const PrimaryWindowUIRootBuilder&) = delete;
    PrimaryWindowUIRootBuilder& operator=(const PrimaryWindowUIRootBuilder&) = delete;

    PrimaryWindowUIRootBuilder(PrimaryWindowUIRootBuilder&& other) noexcept;
    PrimaryWindowUIRootBuilder& operator=(PrimaryWindowUIRootBuilder&& other) noexcept;

    // Startup stylesheet authoring is available only during GameStateEnter and
    // before createRoot() publishes the first retained node. The Context owns
    // returned class/token ids and copied token values, validates rule references,
    // and atomically preserves the previous sheet when compilation fails.
    [[nodiscard]] Core::Result<UI::UIStyleClassId> registerStyleClass();
    [[nodiscard]] Core::Result<UI::UIStyleTokenId>
    registerStyleColorToken(UI::UIStraightSrgba8Color value);
    [[nodiscard]] Core::Status installStyleSheet(
        std::span<const UI::UIStyleBoxFillRule> rules);
    // Density is selected before the first retained root is created. A density
    // change remains rejected once any root is live; live-root color-scheme
    // changes continue through PrimaryWindowUITreeUpdater.
    [[nodiscard]] Core::Result<UI::UITheme> productTheme() const;
    [[nodiscard]] Core::Status setProductTheme(const UI::UITheme& theme);
    [[nodiscard]] Core::Result<UI::UIRootOwner> createRoot();
    [[nodiscard]] Core::Result<PrimaryWindowUITreeUpdater> treeUpdater(UI::UIRootOwner& rootOwner);
    [[nodiscard]] Core::Result<PrimaryWindowUIImageResolverRegistration>
    bindImageResolver(UI::UIRootOwner& rootOwner,
                      Render::Texture2DFrameResourceResolver resolver);

  private:
    PrimaryWindowUIRootBuilder(Runtime::Detail::PrimaryWindowUICapabilityState& state, u64 epoch) noexcept;

    Runtime::Detail::PrimaryWindowUICapabilityState* m_state = nullptr;
    u64 m_epoch = 0;

    friend class Runtime::Detail::PrimaryWindowUICapabilityState;
};

} // namespace Tina
