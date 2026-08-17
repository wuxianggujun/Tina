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
    Core::u64 treeExpansionChanges = 0;
    Core::u64 styleTokenUpdates = 0;
    Core::u64 motionBegins = 0;
    UI::UIListViewItemKey listSelectionKey = UI::InvalidUIListViewItemKey;
    UI::UITreeViewItemKey treeSelectionKey = UI::InvalidUITreeViewItemKey;
    Core::usize dropdownSelection = 0;
    float scrollOffset = 0.0F;
    Core::usize controlCount = 0;
    Core::usize imageProductCount = 0;
    Core::usize asymmetricCornerProductCount = 0;
    ShowcaseQuality quality = ShowcaseQuality::Balanced;
    bool notificationsEnabled = false;
    bool stylesheetInstalled = false;
    bool rootAlive = false;
    bool multilineNotesScrolled = false;
};

class ShowcaseUI final {
  public:
    ShowcaseUI() = default;
    ~ShowcaseUI() = default;

    ShowcaseUI(const ShowcaseUI&) = delete;
    ShowcaseUI& operator=(const ShowcaseUI&) = delete;
    ShowcaseUI(ShowcaseUI&&) = delete;
    ShowcaseUI& operator=(ShowcaseUI&&) = delete;

    [[nodiscard]] Core::Status build(GameStateEnterContext& context, ShowcaseTheme initialTheme,
                                     Render::Texture2DFrameResourceResolver imageResolver);
    [[nodiscard]] Core::Status update(UIUpdateContext& context);

    void requestAutomatedStep(Core::u64 frameIndex) noexcept;
    [[nodiscard]] bool unbindImageResolver() noexcept;
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
        DropdownChanged,
        ListSelectionChanged,
        TreeSelectionChanged,
        TreeExpansionChanged,
        ScrollChanged,
    };

    struct Nodes final {
        UI::UINodeId background{};
        UI::UINodeId header{};
        UI::UINodeId headerAccent{};
        UI::UINodeId navigation{};
        std::array<UI::UINodeId, 6> cards{};
        std::array<UI::UINodeId, 5> navigationAccents{};
        std::array<UI::UINodeId, 3> paletteSwatches{};
        UI::UINodeId statusPanel{};
        UI::UINodeId qualityGroup{};
        UI::UINodeId themeGroup{};

        UI::UINodeId title{};
        UI::UINodeId subtitle{};
        UI::UINodeId liveBadge{};
        UI::UINodeId navigationTitle{};
        std::array<UI::UINodeId, 5> navigationLabels{};
        UI::UINodeId navigationHelp{};
        std::array<UI::UINodeId, 6> cardTitles{};
        std::array<UI::UINodeId, 6> cardSubtitles{};
        UI::UINodeId notificationsLabel{};
        UI::UINodeId progressLabel{};
        UI::UINodeId profileLabel{};
        UI::UINodeId qualityLabel{};
        UI::UINodeId appearanceLabel{};
        UI::UINodeId statusLabel{};
        UI::UINodeId dropdownLabel{};
        UI::UINodeId listLabel{};
        UI::UINodeId treeLabel{};
        UI::UINodeId scrollLabel{};
        std::array<UI::UINodeId, 6> scrollContentLabels{};

        UI::UINodeId primaryButton{};
        UI::UINodeId primaryButtonIcon{};
        UI::UINodeId primaryButtonLabel{};
        UI::UINodeId destructiveButton{};
        UI::UINodeId destructiveButtonIcon{};
        UI::UINodeId disabledButton{};
        UI::UINodeId resetButton{};
        UI::UINodeId notificationsCheckbox{};
        UI::UINodeId progressSlider{};
        UI::UINodeId progressBar{};
        UI::UINodeId profileTextEdit{};
        std::array<UI::UINodeId, 3> qualityRadios{};
        std::array<UI::UINodeId, 2> themeRadios{};
        UI::UINodeId dropdown{};
        UI::UINodeId dropdownPopup{};
        std::array<UI::UINodeId, 3> dropdownItems{};
        UI::UINodeId listView{};
        UI::UINodeId treeView{};
        UI::UINodeId scrollView{};
        UI::UINodeId scrollContent{};
        UI::UINodeId inventoryThumbnail{};
        UI::UINodeId multilineNotes{};
    };

    [[nodiscard]] Core::Status applyTheme(PrimaryWindowUITreeUpdater& tree, ShowcaseTheme theme, bool countSwitch);
    [[nodiscard]] Core::Status applyProgress(PrimaryWindowUITreeUpdater& tree, float value);
    [[nodiscard]] Core::Status applyReset(PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Core::Status publishStatus(PrimaryWindowUITreeUpdater& tree, StatusMessage message);
    [[nodiscard]] UI::UIListViewDataSource listDataSource() const noexcept;
    [[nodiscard]] UI::UITreeViewDataSource treeDataSource() noexcept;

    static Core::u64 listItemCount(const void* state) noexcept;
    static bool resolveListItem(const void* state, Core::u64 logicalIndex,
                                UI::UIListViewItemDescriptor& output) noexcept;
    static Core::u64 treeItemCount(const void* state) noexcept;
    static bool resolveTreeItem(const void* state, Core::u64 logicalIndex,
                                UI::UITreeViewItemDescriptor& output) noexcept;
    static bool setTreeItemExpanded(void* state, UI::UITreeViewItemKey key, bool expanded) noexcept;

    UI::UIRootOwner root_{};
    PrimaryWindowUIImageResolverRegistration imageResolver_{};
    Nodes nodes_{};
    ShowcaseTheme initialTheme_ = ShowcaseTheme::Dark;
    ShowcaseTheme currentTheme_ = ShowcaseTheme::Dark;
    ShowcaseQuality quality_ = ShowcaseQuality::Balanced;
    StatusMessage pendingStatus_ = StatusMessage::Ready;
    std::optional<ShowcaseTheme> requestedTheme_{};
    std::optional<float> requestedSliderValue_{};
    std::optional<Core::u64> requestedListSelection_{};
    std::optional<Core::u64> requestedTreeSelection_{};
    std::optional<bool> requestedWorldExpansion_{};
    std::optional<Core::usize> requestedDropdownSelection_{};
    std::optional<bool> requestedDropdownOpen_{};
    std::optional<float> requestedScrollOffset_{};
    float requestedProgressValue_ = 72.0F;
    float progressValue_ = 72.0F;
    float scrollOffset_ = 0.0F;
    Core::u64 themeSwitches_ = 0;
    Core::u64 buttonActivations_ = 0;
    Core::u64 sliderChanges_ = 0;
    Core::u64 treeExpansionChanges_ = 0;
    UI::UIListViewItemKey listSelectionKey_ = UI::InvalidUIListViewItemKey;
    UI::UITreeViewItemKey treeSelectionKey_ = UI::InvalidUITreeViewItemKey;
    UI::UINodeId dropdownSelection_{};
    Core::usize dropdownSelectionIndex_ = 0;
    Core::usize controlCount_ = 0;
    Core::usize imageProductCount_ = 0;
    Core::usize asymmetricCornerProductCount_ = 0;
    bool multilineNotesScrolled_ = false;
    Core::u64 styleTokenUpdates_ = 0;
    Core::u64 motionBegins_ = 0;
    UI::UIStyleClassId showcaseChromeClass_{};
    UI::UIStyleTokenId headerAccentToken_{};
    bool stylesheetInstalled_ = false;
    bool progressDirty_ = false;
    bool notificationsDirty_ = false;
    bool notificationsEnabled_ = false;
    bool resetRequested_ = false;
    bool statusDirty_ = true;
    bool treeExpansionDirty_ = false;
    bool worldExpanded_ = true;
    bool playerExpanded_ = true;
    bool uiExpanded_ = false;
};

} // namespace Tina::SampleUI
