#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/Texture2DFrameResourceResolver.hpp>
#include <tina/ui/UIButton.hpp>
#include <tina/ui/UICheckbox.hpp>
#include <tina/ui/UIContent.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UIDropdown.hpp>
#include <tina/ui/UIElement.hpp>
#include <tina/ui/UIListView.hpp>
#include <tina/ui/UIMotion.hpp>
#include <tina/ui/UIProgressBar.hpp>
#include <tina/ui/UIPopup.hpp>
#include <tina/ui/UIRadioButton.hpp>
#include <tina/ui/UIScrollView.hpp>
#include <tina/ui/UISemantics.hpp>
#include <tina/ui/UISlider.hpp>
#include <tina/ui/UIStyle.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/UITextEdit.hpp>
#include <tina/ui/UITreeView.hpp>

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
    [[nodiscard]] Core::Result<UI::UINodeId> createElement(UI::UINodeId parent,
                                                          const UI::UIElementDescriptor& descriptor);
    [[nodiscard]] Core::Result<PrimaryWindowUIBuildTransaction>
    beginBuildTransaction(UI::UINodeId parent,
                          const UI::UIElementDescriptor& rootDescriptor,
                          UI::UIComponentBuildBudget budget);
    [[nodiscard]] Core::Status setLayoutStyle(UI::UINodeId node, const UI::UILayoutStyle& style);
    [[nodiscard]] Core::Status setPointerHitPolicy(UI::UINodeId node, UI::UIPointerHitPolicy policy);
    [[nodiscard]] Core::Status setEnabled(UI::UINodeId node, bool enabled);
    [[nodiscard]] Core::Result<bool> isEnabled(UI::UINodeId node) const;
    [[nodiscard]] Core::Status setFocusScopeMode(UI::UINodeId node, UI::UIFocusScopeMode mode);
    [[nodiscard]] Core::Result<UI::UIFocusScopeMode> focusScopeMode(UI::UINodeId node) const;
    [[nodiscard]] Core::Status requestFocus(UI::UINodeId node);
    [[nodiscard]] Core::Status clearFocus();
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
    [[nodiscard]] Core::Status setButtonPaint(UI::UINodeId button, const UI::UIButtonPaint& paint);
    [[nodiscard]] Core::Result<UI::UIButtonPaint> buttonPaint(UI::UINodeId button) const;
    [[nodiscard]] Core::Status setText(UI::UINodeId node, std::string_view utf8);
    [[nodiscard]] Core::Status setTextStyle(UI::UINodeId node, const UI::UITextStyle& style);
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
    [[nodiscard]] Core::Status setTreeViewDataSource(UI::UINodeId treeView, UI::UITreeViewDataSource source);
    [[nodiscard]] Core::Status clearTreeViewDataSource(UI::UINodeId treeView);
    [[nodiscard]] Core::Status invalidateTreeViewItems(UI::UINodeId treeView);
    [[nodiscard]] Core::Status setTreeViewStyle(UI::UINodeId treeView, const UI::UITreeViewStyle& style);
    [[nodiscard]] Core::Result<UI::UITreeViewStyle> treeViewStyle(UI::UINodeId treeView) const;
    [[nodiscard]] Core::Status setTreeViewPaint(UI::UINodeId treeView, const UI::UITreeViewPaint& paint);
    [[nodiscard]] Core::Result<UI::UITreeViewPaint> treeViewPaint(UI::UINodeId treeView) const;
    [[nodiscard]] Core::Result<UI::UITreeViewMetrics> treeViewMetrics(UI::UINodeId treeView) const;
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
