#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/runtime/PhaseContexts.hpp>
#include <tina/ui/UIContext.hpp>

#include <array>
#include <optional>

namespace Tina::SampleUI {

enum class ShowcaseTheme : Core::u8 {
    Dark,
    Light,
};

enum class ShowcaseQuality : Core::u8 {
    Performance,
    Balanced,
    Quality,
};

struct ShowcaseUISnapshot final {
    ShowcaseTheme theme = ShowcaseTheme::Dark;
    float progressValue = 0.0F;
    Core::u64 themeSwitches = 0;
    Core::u64 buttonActivations = 0;
    Core::u64 sliderChanges = 0;
    Core::usize controlCount = 0;
    ShowcaseQuality quality = ShowcaseQuality::Balanced;
    bool notificationsEnabled = false;
    bool rootAlive = false;
};

class ShowcaseUI final {
  public:
    ShowcaseUI() = default;
    ~ShowcaseUI() = default;

    ShowcaseUI(const ShowcaseUI&) = delete;
    ShowcaseUI& operator=(const ShowcaseUI&) = delete;
    ShowcaseUI(ShowcaseUI&&) = delete;
    ShowcaseUI& operator=(ShowcaseUI&&) = delete;

    [[nodiscard]] Core::Status build(GameStateEnterContext& context, ShowcaseTheme initialTheme);
    [[nodiscard]] Core::Status update(UIUpdateContext& context);

    void requestAutomatedStep(Core::u64 frameIndex) noexcept;
    void release() noexcept;

    [[nodiscard]] ShowcaseUISnapshot snapshot() const noexcept;

  private:
    enum class StatusMessage : Core::u8 {
        Ready,
        PrimaryAction,
        DestructiveAction,
        Reset,
        NotificationsEnabled,
        NotificationsDisabled,
        ThemeDark,
        ThemeLight,
        QualityPerformance,
        QualityBalanced,
        QualityQuality,
        ProgressChanged,
    };

    struct Nodes final {
        UI::UINodeId background{};
        UI::UINodeId header{};
        UI::UINodeId headerAccent{};
        UI::UINodeId navigation{};
        std::array<UI::UINodeId, 4> cards{};
        std::array<UI::UINodeId, 4> navigationAccents{};
        std::array<UI::UINodeId, 4> paletteSwatches{};
        UI::UINodeId statusPanel{};
        UI::UINodeId qualityGroup{};
        UI::UINodeId themeGroup{};

        UI::UINodeId title{};
        UI::UINodeId subtitle{};
        UI::UINodeId liveBadge{};
        UI::UINodeId navigationTitle{};
        std::array<UI::UINodeId, 4> navigationLabels{};
        UI::UINodeId navigationHelp{};
        std::array<UI::UINodeId, 4> cardTitles{};
        std::array<UI::UINodeId, 4> cardSubtitles{};
        UI::UINodeId notificationsLabel{};
        UI::UINodeId progressLabel{};
        UI::UINodeId profileLabel{};
        UI::UINodeId qualityLabel{};
        UI::UINodeId appearanceLabel{};
        UI::UINodeId statusLabel{};

        UI::UINodeId primaryButton{};
        UI::UINodeId destructiveButton{};
        UI::UINodeId disabledButton{};
        UI::UINodeId resetButton{};
        UI::UINodeId notificationsCheckbox{};
        UI::UINodeId progressSlider{};
        UI::UINodeId progressBar{};
        UI::UINodeId profileTextEdit{};
        std::array<UI::UINodeId, 3> qualityRadios{};
        std::array<UI::UINodeId, 2> themeRadios{};
    };

    [[nodiscard]] Core::Status applyTheme(PrimaryWindowUITreeUpdater& tree, ShowcaseTheme theme, bool countSwitch);
    [[nodiscard]] Core::Status applyProgress(PrimaryWindowUITreeUpdater& tree, float value);
    [[nodiscard]] Core::Status applyReset(PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Core::Status publishStatus(PrimaryWindowUITreeUpdater& tree, StatusMessage message);

    UI::UIRootOwner root_{};
    Nodes nodes_{};
    ShowcaseTheme initialTheme_ = ShowcaseTheme::Dark;
    ShowcaseTheme currentTheme_ = ShowcaseTheme::Dark;
    ShowcaseQuality quality_ = ShowcaseQuality::Balanced;
    StatusMessage pendingStatus_ = StatusMessage::Ready;
    std::optional<ShowcaseTheme> requestedTheme_{};
    std::optional<float> requestedSliderValue_{};
    float requestedProgressValue_ = 72.0F;
    float progressValue_ = 72.0F;
    Core::u64 themeSwitches_ = 0;
    Core::u64 buttonActivations_ = 0;
    Core::u64 sliderChanges_ = 0;
    Core::usize controlCount_ = 0;
    bool progressDirty_ = false;
    bool notificationsDirty_ = false;
    bool notificationsEnabled_ = false;
    bool resetRequested_ = false;
    bool statusDirty_ = true;
};

} // namespace Tina::SampleUI
