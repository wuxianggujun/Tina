#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/platform/Window.hpp>
#include <tina/ui/UIButton.hpp>
#include <tina/ui/UICheckbox.hpp>
#include <tina/ui/UICollapsibleSection.hpp>
#include <tina/ui/UIColorField.hpp>
#include <tina/ui/UIColorPicker.hpp>
#include <tina/ui/UIComponentBuild.hpp>
#include <tina/ui/UIContent.hpp>
#include <tina/ui/UIDataGrid.hpp>
#include <tina/ui/UIDialog.hpp>
#include <tina/ui/UIDropdown.hpp>
#include <tina/ui/UIElement.hpp>
#include <tina/ui/UIEventRouting.hpp>
#include <tina/ui/UIFlow.hpp>
#include <tina/ui/UIFocus.hpp>
#include <tina/ui/UIFormField.hpp>
#include <tina/ui/UIIconButton.hpp>
#include <tina/ui/UIListView.hpp>
#include <tina/ui/UIMenu.hpp>
#include <tina/ui/UINumberField.hpp>
#include <tina/ui/UIPopup.hpp>
#include <tina/ui/UIProgressBar.hpp>
#include <tina/ui/UIRadioButton.hpp>
#include <tina/ui/UIRangeInput.hpp>
#include <tina/ui/UIRoutedPointerListenerToken.hpp>
#include <tina/ui/UIScrollView.hpp>
#include <tina/ui/UISlider.hpp>
#include <tina/ui/UISnackbar.hpp>
#include <tina/ui/UISplitView.hpp>
#include <tina/ui/UITabView.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/UITextEdit.hpp>
#include <tina/ui/UITooltip.hpp>
#include <tina/ui/UITreeView.hpp>
#include <tina/ui/UIVirtualGridView.hpp>

#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace Tina::UI::Detail {

class UIContextLifetimeControl;

} // namespace Tina::UI::Detail

namespace Tina::UI {

class UIContext;
class UIAuthoring;

// Move-only ownership of one retained root. Owner-thread destruction reclaims
// immediately; destruction on another thread enters a bounded preallocated
// release queue drained at the next owner-thread UI mutation/commit.
class UIRootOwner final {
  public:
    UIRootOwner() noexcept = default;
    ~UIRootOwner() noexcept;

    UIRootOwner(const UIRootOwner&) = delete;
    UIRootOwner& operator=(const UIRootOwner&) = delete;

    UIRootOwner(UIRootOwner&& other) noexcept;
    UIRootOwner& operator=(UIRootOwner&& other) noexcept;

    void reset() noexcept;

    [[nodiscard]] UINodeId rootNodeId() const noexcept;
    [[nodiscard]] bool hasValue() const noexcept;
    explicit operator bool() const noexcept;

  private:
    friend class UIContext;
    friend class UIAuthoring;
    friend class UIRootBuilder;
    friend class UITreeUpdater;

    UIRootOwner(std::weak_ptr<Detail::UIContextLifetimeControl> lifetime, UINodeId root) noexcept;

    std::weak_ptr<Detail::UIContextLifetimeControl> m_lifetime{};
    UINodeId m_root{};
};

// Non-owning owner-thread view. It must not outlive the UIContext that created it.
class UIRootBuilder final {
  public:
    UIRootBuilder() noexcept = default;

    [[nodiscard]] Core::Result<UIRootOwner> createRoot();
    [[nodiscard]] Core::Result<UINodeId> createElement(UINodeId parent, const UIElementDescriptor& descriptor);

  private:
    friend class UIContext;
    friend class UIAuthoring;

    explicit UIRootBuilder(UIContext& context) noexcept;

    UIContext* m_context = nullptr;
};

// Move-only bounded construction of one component subtree. The root and every
// descendant remain live retained nodes, but no committed snapshot can observe
// them until the caller commits layout. Any create failure poisons the build;
// destruction or reset then reclaims the entire component subtree and its
// retained text/canvas storage.
class UIElementBuildTransaction final {
  public:
    UIElementBuildTransaction() noexcept = default;
    ~UIElementBuildTransaction() noexcept;

    UIElementBuildTransaction(const UIElementBuildTransaction&) = delete;
    UIElementBuildTransaction& operator=(const UIElementBuildTransaction&) = delete;

    UIElementBuildTransaction(UIElementBuildTransaction&& other) noexcept;
    UIElementBuildTransaction& operator=(UIElementBuildTransaction&& other) noexcept;

    [[nodiscard]] Core::Result<UINodeId> createElement(UINodeId parent,
                                                       const UIElementDescriptor& descriptor);
    [[nodiscard]] Core::Result<UINodeId> commit();
    void reset() noexcept;

    [[nodiscard]] UINodeId rootNodeId() const noexcept;
    [[nodiscard]] UIComponentBuildBudget remainingBudget() const noexcept;
    [[nodiscard]] bool isActive() const noexcept;

  private:
    friend class UIContext;
    friend class UITreeUpdater;

    UIElementBuildTransaction(UIContext& context, UINodeId updaterRoot, UINodeId componentRoot,
                              UIComponentBuildBudget remainingBudget) noexcept;

    std::weak_ptr<Detail::UIContextLifetimeControl> m_lifetime{};
    UINodeId m_updaterRoot{};
    UINodeId m_componentRoot{};
    UIComponentBuildBudget m_remainingBudget{};
    std::optional<Core::Error> m_failure{};
};

// Non-owning owner-thread view scoped to one live root. It must not outlive its
// UIContext; generation checks reject a root or child destroyed while it exists.
class UITreeUpdater final {
  public:
    UITreeUpdater() noexcept = default;

    UITreeUpdater(const UITreeUpdater&) = delete;
    UITreeUpdater& operator=(const UITreeUpdater&) = delete;

    UITreeUpdater(UITreeUpdater&& other) noexcept;
    UITreeUpdater& operator=(UITreeUpdater&& other) noexcept;

    [[nodiscard]] Core::Result<UINodeId> createElement(UINodeId parent, const UIElementDescriptor& descriptor);
    [[nodiscard]] Core::Result<UIElementBuildTransaction>
    beginBuildTransaction(UINodeId parent, const UIElementDescriptor& rootDescriptor,
                          UIComponentBuildBudget budget);
    [[nodiscard]] Core::Result<UIIconButtonParts>
    buildIconButton(UINodeId parent, const UIIconButtonConfig& config);
    [[nodiscard]] Core::Result<UIFormFieldParts>
    buildFormField(UINodeId parent, const UIFormFieldConfig& config);
    [[nodiscard]] Core::Result<UIDialogParts>
    buildDialog(UINodeId parent, const UIDialogConfig& config);
    // Dialog presentation is retained separately from authored visibility.
    // buildDialog starts closed; open/dismiss publishes through the next
    // successful commitLayout(). One registered Dialog may be open per Window.
    [[nodiscard]] Core::Status openDialog(UINodeId dialog);
    [[nodiscard]] Core::Status dismissDialog(UINodeId dialog);
    [[nodiscard]] Core::Result<bool> isDialogOpen(UINodeId dialog) const;
    [[nodiscard]] Core::Result<UISnackbarHostParts>
    buildSnackbarHost(UINodeId parent, const UISnackbarHostConfig& config);
    [[nodiscard]] Core::Result<UINumberFieldParts>
    buildNumberField(UINodeId parent, const UINumberFieldConfig& config);
    [[nodiscard]] Core::Result<UICollapsibleSectionParts>
    buildCollapsibleSection(UINodeId parent, const UICollapsibleSectionConfig& config);
    [[nodiscard]] Core::Result<UIColorFieldParts>
    buildColorField(UINodeId parent, const UIColorFieldConfig& config);
    [[nodiscard]] Core::Result<UIColorPickerParts>
    buildColorPicker(UINodeId parent, const UIColorPickerConfig& config);
    // A Layer is a direct child of this updater's root. A Screen is a direct
    // child of its Layer and starts inactive. Only the stack top is published;
    // inactive Screens behave as Collapsed without rewriting authored style.
    [[nodiscard]] Core::Result<UIFlowLayerId> registerFlowLayer(UINodeId layer);
    [[nodiscard]] Core::Result<UIFlowScreenId> registerFlowScreen(UIFlowLayerId layer, UINodeId screen);
    [[nodiscard]] Core::Status pushFlowScreen(UIFlowScreenId screen);
    [[nodiscard]] Core::Result<UIFlowScreenId> popFlowScreen(UIFlowLayerId layer);
    [[nodiscard]] Core::Result<UIFlowScreenId> replaceFlowScreen(UIFlowScreenId screen);
    [[nodiscard]] Core::Result<UIFlowScreenId> activeFlowScreen(UIFlowLayerId layer) const;
    [[nodiscard]] Core::Result<bool> isFlowScreenActive(UIFlowScreenId screen) const;
    [[nodiscard]] Core::Status assignFlowGamepad(Platform::GamepadId gamepad,
                                                 UIFlowLocalUserId localUser);
    [[nodiscard]] Core::Status clearFlowGamepadAssignment(Platform::GamepadId gamepad);
    [[nodiscard]] Core::Result<UIFlowLocalUserId>
    flowLocalUserForGamepad(Platform::GamepadId gamepad) const;
    [[nodiscard]] Core::Result<UIFlowInputDeviceState>
    flowInputDeviceState(UIFlowLocalUserId localUser) const;
    [[nodiscard]] Core::Status setFlowScreenAction(UIFlowScreenId screen, UIFlowAction action,
                                                   UIFlowActionCallback callback);
    [[nodiscard]] Core::Status clearFlowScreenAction(UIFlowScreenId screen, UIFlowAction action);
    [[nodiscard]] bool isAlive(UINodeId node) const noexcept;
    // Copies the node's world rect from the last successful layout commit.
    // Returns InvalidNode when the node is outside this updater root or absent
    // from the committed snapshot. The returned value owns no snapshot borrow.
    [[nodiscard]] Core::Result<UILogicalRect> committedLayoutRect(UINodeId node) const;
    [[nodiscard]] Core::Status setLayoutStyle(UINodeId node, const UILayoutStyle& style);
    [[nodiscard]] Core::Status setPointerHitPolicy(UINodeId node, UIPointerHitPolicy policy);
    // Enables or disables a published widget. Disabled widgets remain in the
    // semantics snapshot, but cannot receive default focus or widget actions.
    // The state change is owner-thread only and is committed atomically with
    // its paint/semantics invalidation.
    [[nodiscard]] Core::Status setEnabled(UINodeId node, bool enabled);
    [[nodiscard]] Core::Result<bool> isEnabled(UINodeId node) const;
    [[nodiscard]] Core::Status setFocusScopeMode(UINodeId node, UIFocusScopeMode mode);
    [[nodiscard]] Core::Result<UIFocusScopeMode> focusScopeMode(UINodeId node) const;
    [[nodiscard]] Core::Status requestFocus(UINodeId node);
    [[nodiscard]] Core::Status clearFocus();
    // Committed keyboard focus owned by this updater root, or an unset node when
    // focus is elsewhere or absent. Read-only: it dirties nothing and reports the
    // same target that Tab, requestFocus() and pointer activation converge on.
    [[nodiscard]] Core::Result<UINodeId> focusedNode() const;
    [[nodiscard]] Core::Status setStyleRole(UINodeId node, UIStyleRoleId role);
    [[nodiscard]] Core::Result<UIStyleRoleId> styleRole(UINodeId node) const;
    [[nodiscard]] Core::Status clearOverride(UINodeId node, UIStyleOverride properties = UIStyleOverride::All);
    [[nodiscard]] Core::Status setBoxPaint(UINodeId node, const UIBoxPaint& paint);
    // Paint-only image tint/opacity. Does not dirty Measure/Arrange/Hit.
    [[nodiscard]] Core::Status setImageTint(UINodeId node, UIStraightSrgba8Color tint);
    [[nodiscard]] Core::Result<UIStraightSrgba8Color> imageTint(UINodeId node) const;
    [[nodiscard]] Core::Status setButtonPaint(UINodeId button, const UIButtonPaint& paint);
    [[nodiscard]] Core::Result<UIButtonPaint> buttonPaint(UINodeId button) const;
    // Intrinsic-text elements only. Stores strict UTF-8 without NUL into the
    // fixed text byte budget and dirties Measure for Auto-sized content.
    // Glyph raster and FreeType remain out of this API.
    [[nodiscard]] Core::Status setText(UINodeId node, std::string_view utf8);
    [[nodiscard]] Core::Status setTextStyle(UINodeId node, const UITextStyle& style);
    // Paint-only single-line overflow policy. Ellipsis drops trailing grapheme
    // clusters that do not fit the committed content box and appends
    // UITextEllipsisUtf8. Intrinsic measure keeps reporting the untruncated
    // size and the accessibility name keeps the full text.
    [[nodiscard]] Core::Status setTextOverflow(UINodeId node, UITextOverflow overflow);
    [[nodiscard]] Core::Result<UITextOverflow> textOverflow(UINodeId node);
    [[nodiscard]] Core::Status setTextWrapMode(UINodeId node, UITextWrapMode wrapMode);
    [[nodiscard]] Core::Result<UITextWrapMode> textWrapMode(UINodeId node);
    // Zero restores unlimited wrapped lines. A positive limit requires Words
    // wrapping and clamps the last visible line with an ellipsis.
    [[nodiscard]] Core::Status setTextLineClamp(UINodeId node, UITextLineClamp lineClamp);
    [[nodiscard]] Core::Result<UITextLineClamp> textLineClamp(UINodeId node);
    [[nodiscard]] Core::Status setContentAlignment(UINodeId node, UIContentAlignment alignment);
    [[nodiscard]] Core::Result<std::string_view> text(UINodeId node);
    [[nodiscard]] Core::Result<UITextStyle> textStyle(UINodeId node);
    [[nodiscard]] Core::Result<UIContentAlignment> contentAlignment(UINodeId node) const;
    [[nodiscard]] Core::Status setTextSelection(UINodeId textEdit, UITextSelection selection);
    [[nodiscard]] Core::Result<UITextSelection> textSelection(UINodeId textEdit) const;
    [[nodiscard]] Core::Status setTextEditPaint(UINodeId textEdit, const UITextEditPaint& paint);
    [[nodiscard]] Core::Result<UITextEditPaint> textEditPaint(UINodeId textEdit) const;
    [[nodiscard]] Core::Status setTextChangedCallback(
        UINodeId textEdit, UITextChangedCallback callback);
    [[nodiscard]] Core::Status clearTextChangedCallback(UINodeId textEdit);
    [[nodiscard]] Core::Status setTextSubmitCallback(
        UINodeId textEdit, UITextSubmitCallback callback);
    [[nodiscard]] Core::Status clearTextSubmitCallback(UINodeId textEdit);
    // Activate-capable Elements own action/pressed state. Virtual collection
    // items keep their dedicated datasource action path.
    [[nodiscard]] Core::Status setButtonAction(UINodeId button, UIButtonActionCallback callback);
    [[nodiscard]] Core::Status clearButtonAction(UINodeId button);
    [[nodiscard]] Core::Result<bool> isButtonPressed(UINodeId button) const;
    // Checkbox and Switch reuse Button action slots/arm path; callback
    // fires after the shared Toggle value changes.
    [[nodiscard]] Core::Status setCheckboxAction(UINodeId checkbox, UIButtonActionCallback callback);
    [[nodiscard]] Core::Status clearCheckboxAction(UINodeId checkbox);
    [[nodiscard]] Core::Status setCheckboxPaint(UINodeId checkbox, const UICheckboxPaint& paint);
    [[nodiscard]] Core::Result<UICheckboxPaint> checkboxPaint(UINodeId checkbox) const;
    // Toggle-capable Elements own checked state. Checkbox paint APIs remain
    // restricted to the resolved Checkbox built-in, including the official
    // Switch authoring profile.
    [[nodiscard]] Core::Status setChecked(UINodeId checkbox, bool checked);
    [[nodiscard]] Core::Result<bool> isChecked(UINodeId checkbox) const;
    [[nodiscard]] Core::Result<bool> isCheckboxPressed(UINodeId checkbox) const;
    // RangeInput-capable Elements own finite min/max/value/step state. Slider
    // paint, callback, and drag geometry remain restricted to private
    // Slider-resolved nodes.
    [[nodiscard]] Core::Status setSliderRange(UINodeId slider, float minValue, float maxValue, float step = 0.0F);
    [[nodiscard]] Core::Status setSliderValue(UINodeId slider, float value);
    [[nodiscard]] Core::Result<float> sliderValue(UINodeId slider) const;
    [[nodiscard]] Core::Status setSliderPaint(UINodeId slider, const UISliderPaint& paint);
    [[nodiscard]] Core::Result<UISliderPaint> sliderPaint(UINodeId slider) const;
    [[nodiscard]] Core::Status setSliderChangeCallback(UINodeId slider, UISliderChangeCallback callback);
    [[nodiscard]] Core::Status clearSliderChangeCallback(UINodeId slider);
    [[nodiscard]] Core::Result<bool> isSliderDragging(UINodeId slider) const;
    [[nodiscard]] Core::Status setSplitViewParts(UINodeId splitView, UINodeId primaryPane,
                                                 UINodeId splitter, UINodeId secondaryPane);
    [[nodiscard]] Core::Status clearSplitViewParts(UINodeId splitView);
    [[nodiscard]] Core::Result<UISplitViewParts> splitViewParts(UINodeId splitView) const;
    [[nodiscard]] Core::Status setSplitViewFraction(UINodeId splitView, float fraction);
    [[nodiscard]] Core::Result<float> splitViewFraction(UINodeId splitView) const;
    [[nodiscard]] Core::Result<UISplitViewMetrics> splitViewMetrics(UINodeId splitView) const;
    [[nodiscard]] Core::Result<bool> isSplitterDragging(UINodeId splitter) const;
    [[nodiscard]] Core::Status setSplitterPaint(UINodeId splitter,
                                                 const UISplitterPaint& paint);
    [[nodiscard]] Core::Result<UISplitterPaint> splitterPaint(UINodeId splitter) const;
    [[nodiscard]] Core::Status setTabViewItems(UINodeId tabView,
                                               std::span<const UITabViewItem> items,
                                               u32 activeIndex = 0);
    [[nodiscard]] Core::Status clearTabViewItems(UINodeId tabView);
    [[nodiscard]] Core::Result<u32> tabViewItemCount(UINodeId tabView) const;
    [[nodiscard]] Core::Result<UITabViewItem> tabViewItemAt(UINodeId tabView, u32 index) const;
    [[nodiscard]] Core::Status setTabViewActiveTab(UINodeId tabView, UINodeId tab);
    [[nodiscard]] Core::Result<UINodeId> tabViewActiveTab(UINodeId tabView) const;
    [[nodiscard]] Core::Result<UINodeId> tabViewActivePanel(UINodeId tabView) const;
    [[nodiscard]] Core::Result<UITabViewMetrics> tabViewMetrics(UINodeId tabView) const;
    [[nodiscard]] Core::Result<UITabViewCommandResult>
    routeTabViewCommand(UINodeId tabView, UITabViewCommand command);
    [[nodiscard]] Core::Status setTabPaint(UINodeId tab, const UITabPaint& paint);
    [[nodiscard]] Core::Result<UITabPaint> tabPaint(UINodeId tab) const;
    [[nodiscard]] Core::Status setScrollViewStyle(UINodeId scrollView, const UIScrollViewStyle& style);
    [[nodiscard]] Core::Result<UIScrollViewStyle> scrollViewStyle(UINodeId scrollView) const;
    [[nodiscard]] Core::Status setScrollViewOffset(UINodeId scrollView, UIScrollOffset offset);
    [[nodiscard]] Core::Result<UIScrollOffset> scrollViewOffset(UINodeId scrollView) const;
    [[nodiscard]] Core::Result<UIScrollViewMetrics> scrollViewMetrics(UINodeId scrollView) const;
    [[nodiscard]] Core::Status setScrollViewPaint(UINodeId scrollView, const UIScrollViewPaint& paint);
    [[nodiscard]] Core::Result<UIScrollViewPaint> scrollViewPaint(UINodeId scrollView) const;
    [[nodiscard]] Core::Result<bool> isScrollViewDragging(UINodeId scrollView) const;
    [[nodiscard]] Core::Status setPopupStyle(UINodeId popup, const UIPopupStyle& style);
    [[nodiscard]] Core::Result<UIPopupStyle> popupStyle(UINodeId popup) const;
    [[nodiscard]] Core::Status setPopupOpen(UINodeId popup, bool open);
    [[nodiscard]] Core::Result<bool> isPopupOpen(UINodeId popup) const;
    [[nodiscard]] Core::Result<UIPopupMetrics> popupMetrics(UINodeId popup) const;
    [[nodiscard]] Core::Status setTooltipAnchor(UINodeId tooltip, UINodeId anchor);
    [[nodiscard]] Core::Status clearTooltipAnchor(UINodeId tooltip);
    [[nodiscard]] Core::Result<UINodeId> tooltipAnchor(UINodeId tooltip) const;
    [[nodiscard]] Core::Status showTooltip(UINodeId tooltip);
    [[nodiscard]] Core::Status dismissTooltip(UINodeId tooltip);
    [[nodiscard]] Core::Result<bool> isTooltipOpen(UINodeId tooltip) const;
    [[nodiscard]] Core::Result<UITooltipMetrics> tooltipMetrics(UINodeId tooltip) const;
    [[nodiscard]] Core::Status setMenuAnchor(UINodeId menu, UINodeId anchor);
    [[nodiscard]] Core::Status clearMenuAnchor(UINodeId menu);
    [[nodiscard]] Core::Result<UINodeId> menuAnchor(UINodeId menu) const;
    [[nodiscard]] Core::Status setMenuOpen(UINodeId menu, bool open);
    [[nodiscard]] Core::Result<bool> isMenuOpen(UINodeId menu) const;
    [[nodiscard]] Core::Result<UIMenuMetrics> menuMetrics(UINodeId menu) const;
    [[nodiscard]] Core::Status setMenuItemSubmenu(UINodeId item, UINodeId submenu);
    [[nodiscard]] Core::Status clearMenuItemSubmenu(UINodeId item);
    [[nodiscard]] Core::Result<UINodeId> menuItemSubmenu(UINodeId item) const;
    [[nodiscard]] Core::Result<UINodeId> menuParentItem(UINodeId menu) const;
    [[nodiscard]] Core::Status setMenuItemChecked(UINodeId item, bool checked);
    [[nodiscard]] Core::Result<bool> isMenuItemChecked(UINodeId item) const;
    [[nodiscard]] Core::Result<UIMenuCommandResult>
    routeMenuCommand(UINodeId menu, UIMenuCommand command);
    [[nodiscard]] Core::Status setDropdownOpen(UINodeId dropdown, bool open);
    [[nodiscard]] Core::Result<bool> isDropdownOpen(UINodeId dropdown) const;
    [[nodiscard]] Core::Status setDropdownSelectedItem(UINodeId dropdown, UINodeId item);
    [[nodiscard]] Core::Result<UINodeId> dropdownSelectedItem(UINodeId dropdown) const;
    [[nodiscard]] Core::Result<bool> isDropdownItemSelected(UINodeId item) const;
    [[nodiscard]] Core::Status setDropdownPaint(UINodeId dropdown, const UIDropdownPaint& paint);
    [[nodiscard]] Core::Result<UIDropdownPaint> dropdownPaint(UINodeId dropdown) const;
    [[nodiscard]] Core::Status setListViewDataSource(UINodeId listView, UIListViewDataSource source);
    [[nodiscard]] Core::Status clearListViewDataSource(UINodeId listView);
    [[nodiscard]] Core::Status invalidateListViewItems(UINodeId listView);
    [[nodiscard]] Core::Status setListViewStyle(UINodeId listView, const UIListViewStyle& style);
    [[nodiscard]] Core::Result<UIListViewStyle> listViewStyle(UINodeId listView) const;
    [[nodiscard]] Core::Status setListViewPaint(UINodeId listView, const UIListViewPaint& paint);
    [[nodiscard]] Core::Result<UIListViewPaint> listViewPaint(UINodeId listView) const;
    [[nodiscard]] Core::Result<UIListViewMetrics> listViewMetrics(UINodeId listView) const;
    [[nodiscard]] Core::Status setListViewSelectedIndex(UINodeId listView, u64 logicalIndex);
    [[nodiscard]] Core::Status clearListViewSelection(UINodeId listView);
    [[nodiscard]] Core::Result<UIListViewSelection> listViewSelection(UINodeId listView) const;
    [[nodiscard]] Core::Status
    scrollListViewToIndex(UINodeId listView, u64 logicalIndex,
                          UIListViewScrollAlignment alignment = UIListViewScrollAlignment::Nearest);
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
    [[nodiscard]] Core::Result<UINodeId>
    virtualGridViewMaterializedItemNode(UINodeId virtualGridView,
                                        u64 logicalIndex) const;
    [[nodiscard]] Core::Status setVirtualGridViewSelectedIndex(
        UINodeId virtualGridView, u64 logicalIndex);
    [[nodiscard]] Core::Status clearVirtualGridViewSelection(UINodeId virtualGridView);
    [[nodiscard]] Core::Result<UIVirtualGridViewSelection>
    virtualGridViewSelection(UINodeId virtualGridView) const;
    [[nodiscard]] Core::Status scrollVirtualGridViewToIndex(
        UINodeId virtualGridView, u64 logicalIndex,
        UIVirtualGridViewScrollAlignment alignment =
            UIVirtualGridViewScrollAlignment::Nearest);
    [[nodiscard]] Core::Status setDataGridDataSource(
        UINodeId dataGrid, UIDataGridDataSource source);
    [[nodiscard]] Core::Status clearDataGridDataSource(UINodeId dataGrid);
    [[nodiscard]] Core::Status invalidateDataGridItems(UINodeId dataGrid);
    [[nodiscard]] Core::Status setDataGridStyle(
        UINodeId dataGrid, const UIDataGridStyle& style);
    [[nodiscard]] Core::Result<UIDataGridStyle> dataGridStyle(UINodeId dataGrid) const;
    [[nodiscard]] Core::Status setDataGridPaint(
        UINodeId dataGrid, const UIDataGridPaint& paint);
    [[nodiscard]] Core::Result<UIDataGridPaint> dataGridPaint(UINodeId dataGrid) const;
    [[nodiscard]] Core::Result<UIDataGridMetrics> dataGridMetrics(UINodeId dataGrid) const;
    [[nodiscard]] Core::Status setDataGridSelectedCell(
        UINodeId dataGrid, u64 logicalRow, u32 logicalColumn);
    [[nodiscard]] Core::Status clearDataGridSelection(UINodeId dataGrid);
    [[nodiscard]] Core::Result<UIDataGridSelection> dataGridSelection(UINodeId dataGrid) const;
    [[nodiscard]] Core::Status scrollDataGridToCell(
        UINodeId dataGrid, u64 logicalRow, u32 logicalColumn,
        UIDataGridScrollAlignment alignment = UIDataGridScrollAlignment::Nearest);
    [[nodiscard]] Core::Status setTreeViewDataSource(UINodeId treeView, UITreeViewDataSource source);
    [[nodiscard]] Core::Status clearTreeViewDataSource(UINodeId treeView);
    [[nodiscard]] Core::Status invalidateTreeViewItems(UINodeId treeView);
    [[nodiscard]] Core::Status setTreeViewStyle(UINodeId treeView, const UITreeViewStyle& style);
    [[nodiscard]] Core::Result<UITreeViewStyle> treeViewStyle(UINodeId treeView) const;
    [[nodiscard]] Core::Status setTreeViewPaint(UINodeId treeView, const UITreeViewPaint& paint);
    [[nodiscard]] Core::Result<UITreeViewPaint> treeViewPaint(UINodeId treeView) const;
    [[nodiscard]] Core::Result<UITreeViewMetrics> treeViewMetrics(UINodeId treeView) const;
    // Returns the committed materialized row node for a logical item. The
    // result is empty when that logical row is outside the current materialized
    // window; callers can use treeViewMetrics() to decide whether to scroll it
    // into view first.
    [[nodiscard]] Core::Result<UINodeId>
    treeViewMaterializedItemNode(UINodeId treeView, u64 logicalIndex) const;
    [[nodiscard]] Core::Status setTreeViewSelectedIndex(UINodeId treeView, u64 logicalIndex);
    [[nodiscard]] Core::Status clearTreeViewSelection(UINodeId treeView);
    [[nodiscard]] Core::Result<UITreeViewSelection> treeViewSelection(UINodeId treeView) const;
    [[nodiscard]] Core::Status setTreeViewItemExpanded(UINodeId treeView, u64 logicalIndex, bool expanded);
    [[nodiscard]] Core::Status
    scrollTreeViewToIndex(UINodeId treeView, u64 logicalIndex,
                          UITreeViewScrollAlignment alignment = UITreeViewScrollAlignment::Nearest);
    [[nodiscard]] Core::Status setProgressBarRange(UINodeId progressBar, float minValue, float maxValue);
    [[nodiscard]] Core::Status setProgressBarValue(UINodeId progressBar, float value);
    [[nodiscard]] Core::Result<float> progressBarValue(UINodeId progressBar) const;
    [[nodiscard]] Core::Status setProgressBarPaint(UINodeId progressBar, const UIProgressBarPaint& paint);
    [[nodiscard]] Core::Result<UIProgressBarPaint> progressBarPaint(UINodeId progressBar) const;
    [[nodiscard]] Core::Status setRadioButtonPaint(UINodeId radioButton, const UIRadioButtonPaint& paint);
    [[nodiscard]] Core::Result<UIRadioButtonPaint> radioButtonPaint(UINodeId radioButton) const;
    [[nodiscard]] Core::Status setRadioButtonAction(UINodeId radioButton, UIButtonActionCallback callback);
    [[nodiscard]] Core::Status clearRadioButtonAction(UINodeId radioButton);
    [[nodiscard]] Core::Status setRadioButtonSelected(UINodeId radioButton, bool selected);
    [[nodiscard]] Core::Result<bool> isRadioButtonSelected(UINodeId radioButton) const;
    [[nodiscard]] Core::Result<bool> isRadioButtonPressed(UINodeId radioButton) const;
    [[nodiscard]] Core::Result<UIRoutedPointerListenerToken>
    addRoutedPointerListener(UIRoutedPointerListenerDesc descriptor, UIRoutedPointerCallback callback);
    [[nodiscard]] Core::Status destroy(UINodeId node);

  private:
    friend class UIContext;
    friend class UIAuthoring;

    UITreeUpdater(UIContext& context, UINodeId root) noexcept;

    UIContext* m_context = nullptr;
    UINodeId m_root{};
};


// Owner-thread authoring capability. Root-scoped mutation remains on
// UITreeUpdater so generation and single-root validation cannot be bypassed.
class UIAuthoring final {
  public:
    [[nodiscard]] UIRootBuilder rootBuilder() noexcept;
    [[nodiscard]] Core::Result<UITreeUpdater> treeUpdater(UIRootOwner& rootOwner);

  private:
    friend class UIContext;

    explicit UIAuthoring(UIContext& context) noexcept : m_context(&context) {}

    UIContext* m_context = nullptr;
};

} // namespace Tina::UI
