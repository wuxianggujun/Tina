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
    [[nodiscard]] Core::Result<UI::UICommittedSemanticsView> committedSemantics(u64 epoch,
                                                                                PrimaryWindowUIPhase phase);
    [[nodiscard]] Core::Result<PrimaryWindowUIRootBuilder> rootBuilder(u64 epoch);
    [[nodiscard]] Core::Result<PrimaryWindowUITreeUpdater> treeUpdater(u64 epoch, PrimaryWindowUIPhase phase,
                                                                       UI::UIRootOwner& rootOwner);

    [[nodiscard]] Core::Result<UI::UIRootOwner> createRoot(u64 epoch);
    [[nodiscard]] Core::Result<bool> isAlive(u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
                                             UI::UINodeId node);
    [[nodiscard]] Core::Result<UI::UINodeId> createPanel(u64 epoch, PrimaryWindowUIPhase phase,
                                                         UI::UITreeUpdater& updater, UI::UINodeId parent);
    [[nodiscard]] Core::Result<UI::UINodeId> createLabel(u64 epoch, PrimaryWindowUIPhase phase,
                                                         UI::UITreeUpdater& updater, UI::UINodeId parent);
    [[nodiscard]] Core::Result<UI::UINodeId> createTextEdit(u64 epoch, PrimaryWindowUIPhase phase,
                                                            UI::UITreeUpdater& updater, UI::UINodeId parent);
    [[nodiscard]] Core::Result<UI::UINodeId> createButton(u64 epoch, PrimaryWindowUIPhase phase,
                                                          UI::UITreeUpdater& updater, UI::UINodeId parent);
    [[nodiscard]] Core::Result<UI::UINodeId> createCheckbox(u64 epoch, PrimaryWindowUIPhase phase,
                                                            UI::UITreeUpdater& updater, UI::UINodeId parent);
    [[nodiscard]] Core::Result<UI::UINodeId> createSlider(u64 epoch, PrimaryWindowUIPhase phase,
                                                          UI::UITreeUpdater& updater, UI::UINodeId parent);
    [[nodiscard]] Core::Result<UI::UINodeId> createProgressBar(u64 epoch, PrimaryWindowUIPhase phase,
                                                               UI::UITreeUpdater& updater, UI::UINodeId parent);
    [[nodiscard]] Core::Result<UI::UINodeId> createRadioButton(u64 epoch, PrimaryWindowUIPhase phase,
                                                               UI::UITreeUpdater& updater, UI::UINodeId parent);
    [[nodiscard]] Core::Status setLayoutStyle(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                              UI::UINodeId node, const UI::UILayoutStyle& style);
    [[nodiscard]] Core::Status setPointerHitPolicy(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                   UI::UINodeId node, UI::UIPointerHitPolicy policy);
    [[nodiscard]] Core::Status setEnabled(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                          UI::UINodeId node, bool enabled);
    [[nodiscard]] Core::Result<bool> isEnabled(u64 epoch, PrimaryWindowUIPhase phase,
                                               const UI::UITreeUpdater& updater, UI::UINodeId node);
    [[nodiscard]] Core::Result<UI::UITheme> productTheme(
        u64 epoch,
        PrimaryWindowUIPhase phase);
    [[nodiscard]] Core::Status setProductTheme(
        u64 epoch,
        PrimaryWindowUIPhase phase,
        const UI::UITheme& theme);
    [[nodiscard]] Core::Status setBoxPaint(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                           UI::UINodeId node, const UI::UIBoxPaint& paint);
    [[nodiscard]] Core::Status setButtonPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                               UI::UITreeUpdater& updater, UI::UINodeId button,
                                               const UI::UIButtonPaint& paint);
    [[nodiscard]] Core::Result<UI::UIButtonPaint> buttonPaint(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId button);
    [[nodiscard]] Core::Status setText(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                       UI::UINodeId node, std::string_view utf8);
    [[nodiscard]] Core::Status setTextStyle(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                            UI::UINodeId node, const UI::UITextStyle& style);
    [[nodiscard]] Core::Result<std::string_view> text(u64 epoch, PrimaryWindowUIPhase phase,
                                                      UI::UITreeUpdater& updater, UI::UINodeId node);
    [[nodiscard]] Core::Result<UI::UITextStyle> textStyle(u64 epoch, PrimaryWindowUIPhase phase,
                                                          UI::UITreeUpdater& updater, UI::UINodeId node);
    [[nodiscard]] Core::Status setTextSelection(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                                UI::UINodeId textEdit, UI::UITextSelection selection);
    [[nodiscard]] Core::Result<UI::UITextSelection> textSelection(u64 epoch, PrimaryWindowUIPhase phase,
                                                                  const UI::UITreeUpdater& updater,
                                                                  UI::UINodeId textEdit);
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
    [[nodiscard]] Core::Result<UI::UICheckboxPaint> checkboxPaint(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater, UI::UINodeId checkbox);
    [[nodiscard]] Core::Status setChecked(u64 epoch, PrimaryWindowUIPhase phase, UI::UITreeUpdater& updater,
                                          UI::UINodeId checkbox, bool checked);
    [[nodiscard]] Core::Result<bool> isChecked(u64 epoch, PrimaryWindowUIPhase phase,
                                               const UI::UITreeUpdater& updater, UI::UINodeId checkbox);
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
    [[nodiscard]] Core::Result<UI::UISliderPaint> sliderPaint(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater, UI::UINodeId slider);
    [[nodiscard]] Core::Status setSliderChangeCallback(u64 epoch, PrimaryWindowUIPhase phase,
                                                       UI::UITreeUpdater& updater, UI::UINodeId slider,
                                                       UI::UISliderChangeCallback callback);
    [[nodiscard]] Core::Status clearSliderChangeCallback(u64 epoch, PrimaryWindowUIPhase phase,
                                                         UI::UITreeUpdater& updater, UI::UINodeId slider);
    [[nodiscard]] Core::Result<bool> isSliderDragging(u64 epoch, PrimaryWindowUIPhase phase,
                                                      const UI::UITreeUpdater& updater, UI::UINodeId slider);
    [[nodiscard]] Core::Status setProgressBarRange(u64 epoch, PrimaryWindowUIPhase phase,
                                                   UI::UITreeUpdater& updater, UI::UINodeId progressBar,
                                                   float minValue, float maxValue);
    [[nodiscard]] Core::Status setProgressBarValue(u64 epoch, PrimaryWindowUIPhase phase,
                                                   UI::UITreeUpdater& updater, UI::UINodeId progressBar,
                                                   float value);
    [[nodiscard]] Core::Result<float> progressBarValue(u64 epoch, PrimaryWindowUIPhase phase,
                                                       const UI::UITreeUpdater& updater,
                                                       UI::UINodeId progressBar);
    [[nodiscard]] Core::Status setProgressBarPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                   UI::UITreeUpdater& updater, UI::UINodeId progressBar,
                                                   const UI::UIProgressBarPaint& paint);
    [[nodiscard]] Core::Result<UI::UIProgressBarPaint> progressBarPaint(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId progressBar);
    [[nodiscard]] Core::Status setRadioButtonPaint(u64 epoch, PrimaryWindowUIPhase phase,
                                                   UI::UITreeUpdater& updater, UI::UINodeId radioButton,
                                                   const UI::UIRadioButtonPaint& paint);
    [[nodiscard]] Core::Result<UI::UIRadioButtonPaint> radioButtonPaint(
        u64 epoch, PrimaryWindowUIPhase phase, const UI::UITreeUpdater& updater,
        UI::UINodeId radioButton);
    [[nodiscard]] Core::Status setRadioButtonAction(u64 epoch, PrimaryWindowUIPhase phase,
                                                    UI::UITreeUpdater& updater, UI::UINodeId radioButton,
                                                    UI::UIButtonActionCallback callback);
    [[nodiscard]] Core::Status clearRadioButtonAction(u64 epoch, PrimaryWindowUIPhase phase,
                                                      UI::UITreeUpdater& updater, UI::UINodeId radioButton);
    [[nodiscard]] Core::Status setRadioButtonSelected(u64 epoch, PrimaryWindowUIPhase phase,
                                                      UI::UITreeUpdater& updater, UI::UINodeId radioButton,
                                                      bool selected);
    [[nodiscard]] Core::Result<bool> isRadioButtonSelected(u64 epoch, PrimaryWindowUIPhase phase,
                                                           const UI::UITreeUpdater& updater,
                                                           UI::UINodeId radioButton);
    [[nodiscard]] Core::Result<bool> isRadioButtonPressed(u64 epoch, PrimaryWindowUIPhase phase,
                                                          const UI::UITreeUpdater& updater,
                                                          UI::UINodeId radioButton);
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
