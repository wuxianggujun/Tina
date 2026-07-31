#pragma once

#include <tina/runtime/PrimaryWindowUI.hpp>

#include <optional>
#include <string_view>
#include <thread>

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
    PrimaryWindowUICapabilityState() noexcept;

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
    [[nodiscard]] Core::Result<PrimaryWindowUIRootBuilder> rootBuilder(u64 epoch);
    [[nodiscard]] Core::Result<PrimaryWindowUITreeUpdater> treeUpdater(u64 epoch, PrimaryWindowUIPhase phase,
                                                                       UI::UIRootOwner& rootOwner);

    [[nodiscard]] Core::Result<UI::UIRootOwner> createRoot(u64 epoch);
    [[nodiscard]] Core::Result<bool> isAlive(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
                                             UI::UINodeId node);
    [[nodiscard]] Core::Result<UI::UINodeId>
    createElement(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater, UI::UINodeId parent,
                  const UI::UIElementDescriptor& descriptor);
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
    [[nodiscard]] Core::Status clearOverride(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                             UI::UINodeId node, UI::UIStyleOverride properties);
    [[nodiscard]] Core::Result<UI::UITheme> productTheme(u64 epoch, PrimaryWindowUIPhase phase);
    [[nodiscard]] Core::Status setProductTheme(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITheme& theme);
    [[nodiscard]] Core::Status setBoxPaint(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                           UI::UINodeId node, const UI::UIBoxPaint& paint);
    [[nodiscard]] Core::Status setButtonPaint(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                              UI::UINodeId button, const UI::UIButtonPaint& paint);
    [[nodiscard]] Core::Result<UI::UIButtonPaint> buttonPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                              const UI::UITreeUpdater& updater, UI::UINodeId button);
    [[nodiscard]] Core::Status setText(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                       UI::UINodeId node, std::string_view utf8);
    [[nodiscard]] Core::Status setTextStyle(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                            UI::UINodeId node, const UI::UITextStyle& style);
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
    [[nodiscard]] Core::Result<u64> beginPhase(PrimaryWindowUIPhase phase, UI::UIContext* context);
    [[nodiscard]] Core::Status validate(u64 epoch, PrimaryWindowUIPhase phase, bool requireContext,
                                        std::string_view operation);
    [[nodiscard]] Core::Error rememberFirstError(Core::Error error, std::string_view operation);

    std::thread::id ownerThreadId_{};
    UI::UIContext* context_ = nullptr;
    u64 epoch_ = 0;
    PrimaryWindowUIPhase phase_ = PrimaryWindowUIPhase::None;
    std::optional<Core::Error> firstError_;
};

} // namespace Tina::Runtime::Detail
