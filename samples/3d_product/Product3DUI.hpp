#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/runtime/PhaseContexts.hpp>
#include <tina/ui/UIContext.hpp>

#include <optional>

namespace Tina::Sample3D {

enum class Product3DUITheme : Core::u8 {
    Dark,
    Light,
};

struct Product3DUIConfig final {
    Product3DUITheme initialTheme = Product3DUITheme::Dark;
    Core::u64 targetFrameCount = 300;
    bool automatedThemeDemo = false;
};

struct Product3DUIEvidence final {
    Core::u64 rootsCreated = 0;
    Core::u64 rootsReleased = 0;
    Core::u64 panelsCreated = 0;
    Core::u64 labelsCreated = 0;
    Core::u64 buttonsCreated = 0;
    Core::u64 checkboxesCreated = 0;
    Core::u64 slidersCreated = 0;
    Core::u64 progressBarsCreated = 0;
    Core::u64 listViewsCreated = 0;
    Core::u64 treeViewsCreated = 0;
    Core::u64 themeSwitches = 0;
    Core::u64 themeButtonActivations = 0;
    Core::u64 checkboxActivations = 0;
    Core::u64 sliderChanges = 0;
    Core::u64 progressUpdates = 0;
    Core::u64 automatedThemeSteps = 0;
    Core::u64 automatedCollectionSteps = 0;
    Core::u64 treeExpansionChanges = 0;
    UI::UIListViewItemKey listSelectionKey = UI::InvalidUIListViewItemKey;
    UI::UITreeViewItemKey treeSelectionKey = UI::InvalidUITreeViewItemKey;
    float rotationSpeed = 1.0F;
    float finalProgress = 0.0F;
    bool themeDemoRequested = false;
    bool initialThemeLight = false;
    bool finalThemeLight = false;
    bool autoRotate = true;
    bool inheritedChromeVerified = false;
    bool controlsInitialStateVerified = false;
    bool rootAlive = false;
};

class Product3DUI final {
  public:
    explicit Product3DUI(Product3DUIEvidence& evidence) noexcept;
    ~Product3DUI();

    Product3DUI(const Product3DUI&) = delete;
    Product3DUI& operator=(const Product3DUI&) = delete;
    Product3DUI(Product3DUI&&) = delete;
    Product3DUI& operator=(Product3DUI&&) = delete;

    [[nodiscard]] Core::Status build(GameStateEnterContext& context, Product3DUIConfig config);
    [[nodiscard]] Core::Status update(UIUpdateContext& context, Core::u64 completedFrames);
    void release() noexcept;

    [[nodiscard]] bool autoRotate() const noexcept
    {
        return autoRotate_;
    }
    [[nodiscard]] float rotationSpeed() const noexcept
    {
        return rotationSpeed_;
    }

  private:
    struct Nodes final {
        UI::UINodeId headerPanel{};
        UI::UINodeId headerAccent{};
        UI::UINodeId inspectorPanel{};
        UI::UINodeId inspectorAccent{};
        UI::UINodeId collectionPanel{};
        UI::UINodeId collectionAccent{};
        UI::UINodeId statusPanel{};

        UI::UINodeId title{};
        UI::UINodeId subtitle{};
        UI::UINodeId inspectorTitle{};
        UI::UINodeId inspectorMeta{};
        UI::UINodeId autoRotateLabel{};
        UI::UINodeId rotationSpeedLabel{};
        UI::UINodeId progressCaption{};
        UI::UINodeId progressValue{};
        UI::UINodeId collectionTitle{};
        UI::UINodeId collectionMeta{};
        UI::UINodeId assetListLabel{};
        UI::UINodeId sceneTreeLabel{};
        UI::UINodeId status{};

        UI::UINodeId themeButton{};
        UI::UINodeId autoRotateCheckbox{};
        UI::UINodeId rotationSpeedSlider{};
        UI::UINodeId frameProgress{};
        UI::UINodeId assetList{};
        UI::UINodeId sceneTree{};
    };

    [[nodiscard]] Core::Status applyTheme(PrimaryWindowUITreeUpdater& tree, Product3DUITheme theme, bool countSwitch);
    [[nodiscard]] Core::Status applyProgress(PrimaryWindowUITreeUpdater& tree, Core::u64 completedFrames);
    [[nodiscard]] Core::Status publishStatus(PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] UI::UIListViewDataSource assetListDataSource() const noexcept;
    [[nodiscard]] UI::UITreeViewDataSource sceneTreeDataSource() noexcept;

    static Core::u64 assetListItemCount(const void* state) noexcept;
    static bool resolveAssetListItem(const void* state, Core::u64 logicalIndex,
                                     UI::UIListViewItemDescriptor& output) noexcept;
    static Core::u64 sceneTreeItemCount(const void* state) noexcept;
    static bool resolveSceneTreeItem(const void* state, Core::u64 logicalIndex,
                                     UI::UITreeViewItemDescriptor& output) noexcept;
    static bool setSceneTreeItemExpanded(void* state, UI::UITreeViewItemKey key, bool expanded) noexcept;

    Product3DUIEvidence* evidence_ = nullptr;
    Product3DUIConfig config_{};
    UI::UIRootOwner root_{};
    Nodes nodes_{};
    Product3DUITheme currentTheme_ = Product3DUITheme::Dark;
    std::optional<Product3DUITheme> requestedTheme_{};
    std::optional<float> requestedRotationSpeed_{};
    std::optional<Core::u64> requestedAssetSelection_{};
    std::optional<Core::u64> requestedSceneSelection_{};
    std::optional<bool> requestedSceneExpansion_{};
    float rotationSpeed_ = 1.0F;
    int lastProgressPercent_ = -1;
    bool autoRotate_ = true;
    bool autoRotateDirty_ = false;
    bool sceneExpanded_ = true;
    bool productExpanded_ = true;
    bool statusDirty_ = true;
    bool firstAutomatedThemeStepQueued_ = false;
    bool secondAutomatedThemeStepQueued_ = false;
};

} // namespace Tina::Sample3D
