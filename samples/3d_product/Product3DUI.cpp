#include "Product3DUI.hpp"

#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/ui/UITheme.hpp>
#include <tina/ui/UIElement.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace Tina::Sample3D {
namespace {

namespace UI = Tina::UI;

inline constexpr Core::u64 PanelCount = 7;
inline constexpr Core::u64 LabelCount = 13;
inline constexpr float ReferenceLogicalWidth = 1280.0F;
inline constexpr float ReferenceLogicalHeight = 720.0F;
inline constexpr UI::UIListViewItemKey AssetItemKeyBase = 2'000;
inline constexpr UI::UITreeViewItemKey SceneTreeItemKey = 1;
inline constexpr UI::UITreeViewItemKey CameraTreeItemKey = 2;
inline constexpr UI::UITreeViewItemKey ProductTreeItemKey = 3;
inline constexpr UI::UITreeViewItemKey ProductMeshTreeItemKey = 4;
inline constexpr UI::UITreeViewItemKey ProductMaterialTreeItemKey = 5;
inline constexpr UI::UITreeViewItemKey LightsTreeItemKey = 6;
inline constexpr UI::UITreeViewItemKey PostFxTreeItemKey = 7;

inline constexpr std::array<std::string_view, 7> AssetItemLabels{
    "Hero mesh", "MR material", "Base color", "Normal map", "Lighting rig", "Camera", "Environment",
};

struct Rect final {
    float left = 0.0F;
    float top = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

[[nodiscard]] UI::UILayoutStyle absoluteStyle(Rect rect, UI::UIEdgeSpacing padding = {}) noexcept
{
    UI::UILayoutStyle style{};
    style.placement = UI::UILayoutPlacement::Overlay;
    style.overlay.offset.x = UI::UILayoutLength::Px(rect.left);
    style.overlay.offset.y = UI::UILayoutLength::Px(rect.top);
    style.size.width = UI::UILayoutLength::Px(rect.width);
    style.size.height = UI::UILayoutLength::Px(rect.height);
    style.padding = padding;
    return style;
}

[[nodiscard]] UI::UILayoutStyle rootStyle() noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Percent(100.0F);
    style.size.height = UI::UILayoutLength::Percent(100.0F);
    return style;
}

[[nodiscard]] UI::UILayoutStyle rightAnchoredStyle(Rect rect) noexcept
{
    UI::UILayoutStyle style = absoluteStyle(rect);
    style.overlay.horizontal = UI::UIAxisAlignment::End;
    style.overlay.offset.x = UI::UILayoutLength::Px(0.0F);
    style.margin.right = ReferenceLogicalWidth - rect.left - rect.width;
    return style;
}

[[nodiscard]] UI::UILayoutStyle rightAnchoredStretchHeightStyle(Rect rect) noexcept
{
    UI::UILayoutStyle style = rightAnchoredStyle(rect);
    style.overlay.vertical = UI::UIAxisAlignment::Stretch;
    style.overlay.offset.y = UI::UILayoutLength::Px(0.0F);
    style.margin.top = rect.top;
    style.margin.bottom = ReferenceLogicalHeight - rect.top - rect.height;
    return style;
}

[[nodiscard]] UI::UILayoutStyle footerStyle(Rect rect) noexcept
{
    UI::UILayoutStyle style = absoluteStyle(rect);
    style.overlay.horizontal = UI::UIAxisAlignment::Stretch;
    style.overlay.vertical = UI::UIAxisAlignment::End;
    style.overlay.offset = {};
    style.margin = {
        .left = rect.left,
        .right = ReferenceLogicalWidth - rect.left - rect.width,
        .bottom = ReferenceLogicalHeight - rect.top - rect.height,
    };
    return style;
}

[[nodiscard]] Core::Status storeNode(Core::Result<UI::UINodeId>&& result, UI::UINodeId& destination)
{
    if (!result)
    {
        return Core::failure(std::move(result.error()));
    }
    destination = *result;
    return Core::success();
}

[[nodiscard]] Core::Result<UI::UINodeId> createPanel(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent, Rect rect)
{
    auto node = tree.createElement(parent, UI::makePanelElement());
    if (!node)
    {
        return Core::failure(std::move(node.error()));
    }
    if (Core::Status status = tree.setLayoutStyle(*node, absoluteStyle(rect)); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return *node;
}

[[nodiscard]] Core::Result<UI::UINodeId> createLabel(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent, Rect rect,
                                                     std::string_view text)
{
    auto node = tree.createElement(parent, UI::makeLabelElement());
    if (!node)
    {
        return Core::failure(std::move(node.error()));
    }
    if (Core::Status status = tree.setLayoutStyle(*node, absoluteStyle(rect)); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (Core::Status status = tree.setText(*node, text); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return *node;
}

[[nodiscard]] Core::Result<UI::UINodeId> createButton(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent, Rect rect,
                                                      std::string_view text)
{
    auto node = tree.createElement(parent, UI::makeButtonElement());
    if (!node)
    {
        return Core::failure(std::move(node.error()));
    }
    if (Core::Status status =
            tree.setLayoutStyle(*node, absoluteStyle(rect, UI::UIEdgeSpacing::HorizontalVertical(14.0F, 8.0F)));
        !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (Core::Status status = tree.setText(*node, text); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return *node;
}

[[nodiscard]] Product3DUITheme oppositeTheme(Product3DUITheme theme) noexcept
{
    return theme == Product3DUITheme::Dark ? Product3DUITheme::Light : Product3DUITheme::Dark;
}

[[nodiscard]] const UI::UITheme& themeFor(Product3DUITheme mode) noexcept
{
    static constexpr UI::UITheme Dark = UI::makeDefaultProductTheme();
    static constexpr UI::UITheme Light = UI::makeLightProductTheme();
    return mode == Product3DUITheme::Dark ? Dark : Light;
}

[[nodiscard]] std::string_view assetLabelForKey(UI::UIListViewItemKey key) noexcept
{
    if (key < AssetItemKeyBase || key >= AssetItemKeyBase + AssetItemLabels.size())
    {
        return "No asset";
    }
    return AssetItemLabels[static_cast<Core::usize>(key - AssetItemKeyBase)];
}

[[nodiscard]] std::string_view sceneLabelForKey(UI::UITreeViewItemKey key) noexcept
{
    switch (key)
    {
    case SceneTreeItemKey:
        return "Scene";
    case CameraTreeItemKey:
        return "Camera";
    case ProductTreeItemKey:
        return "Product";
    case ProductMeshTreeItemKey:
        return "Mesh";
    case ProductMaterialTreeItemKey:
        return "Material";
    case LightsTreeItemKey:
        return "Lights";
    case PostFxTreeItemKey:
        return "Post FX";
    default:
        return "No scene node";
    }
}

} // namespace

UI::UIListViewDataSource Product3DUI::assetListDataSource() const noexcept
{
    return UI::UIListViewDataSource{
        .state = this,
        .itemCount = &Product3DUI::assetListItemCount,
        .resolveItem = &Product3DUI::resolveAssetListItem,
    };
}

UI::UITreeViewDataSource Product3DUI::sceneTreeDataSource() noexcept
{
    return UI::UITreeViewDataSource{
        .state = this,
        .itemCount = &Product3DUI::sceneTreeItemCount,
        .resolveItem = &Product3DUI::resolveSceneTreeItem,
        .setItemExpanded = &Product3DUI::setSceneTreeItemExpanded,
    };
}

Core::u64 Product3DUI::assetListItemCount(const void* state) noexcept
{
    return state != nullptr ? AssetItemLabels.size() : 0;
}

bool Product3DUI::resolveAssetListItem(const void* state, Core::u64 logicalIndex,
                                       UI::UIListViewItemDescriptor& output) noexcept
{
    if (state == nullptr || logicalIndex >= AssetItemLabels.size())
    {
        return false;
    }
    output = UI::UIListViewItemDescriptor{
        .key = AssetItemKeyBase + logicalIndex,
        .label = AssetItemLabels[static_cast<Core::usize>(logicalIndex)],
        .enabled = true,
    };
    return true;
}

Core::u64 Product3DUI::sceneTreeItemCount(const void* state) noexcept
{
    if (state == nullptr)
    {
        return 0;
    }
    const auto& ui = *static_cast<const Product3DUI*>(state);
    Core::u64 count = 2;
    if (ui.sceneExpanded_)
    {
        count += 3;
        if (ui.productExpanded_)
        {
            count += 2;
        }
    }
    return count;
}

bool Product3DUI::resolveSceneTreeItem(const void* state, Core::u64 logicalIndex,
                                       UI::UITreeViewItemDescriptor& output) noexcept
{
    if (state == nullptr || logicalIndex >= sceneTreeItemCount(state))
    {
        return false;
    }
    const auto& ui = *static_cast<const Product3DUI*>(state);
    const auto emit = [&logicalIndex, &output](UI::UITreeViewItemKey key, std::string_view label, Core::u32 level,
                                               bool expandable = false, bool expanded = false) noexcept {
        if (logicalIndex == 0)
        {
            output = UI::UITreeViewItemDescriptor{
                .key = key,
                .label = label,
                .level = level,
                .enabled = true,
                .expandable = expandable,
                .expanded = expanded,
            };
            return true;
        }
        --logicalIndex;
        return false;
    };

    if (emit(SceneTreeItemKey, "Scene", 0, true, ui.sceneExpanded_))
    {
        return true;
    }
    if (ui.sceneExpanded_)
    {
        if (emit(CameraTreeItemKey, "Camera", 1))
        {
            return true;
        }
        if (emit(ProductTreeItemKey, "Product", 1, true, ui.productExpanded_))
        {
            return true;
        }
        if (ui.productExpanded_)
        {
            if (emit(ProductMeshTreeItemKey, "Mesh", 2))
            {
                return true;
            }
            if (emit(ProductMaterialTreeItemKey, "Material", 2))
            {
                return true;
            }
        }
        if (emit(LightsTreeItemKey, "Lights", 1))
        {
            return true;
        }
    }
    return emit(PostFxTreeItemKey, "Post FX", 0);
}

bool Product3DUI::setSceneTreeItemExpanded(void* state, UI::UITreeViewItemKey key, bool expanded) noexcept
{
    if (state == nullptr)
    {
        return false;
    }
    auto& ui = *static_cast<Product3DUI*>(state);
    bool* value = nullptr;
    switch (key)
    {
    case SceneTreeItemKey:
        value = &ui.sceneExpanded_;
        break;
    case ProductTreeItemKey:
        value = &ui.productExpanded_;
        break;
    default:
        return false;
    }
    if (*value != expanded)
    {
        *value = expanded;
        ++ui.evidence_->treeExpansionChanges;
        ui.statusDirty_ = true;
    }
    return true;
}

Product3DUI::Product3DUI(Product3DUIEvidence& evidence) noexcept : evidence_(&evidence)
{
}

Product3DUI::~Product3DUI()
{
    release();
}

Core::Status Product3DUI::build(GameStateEnterContext& context, Product3DUIConfig config)
{
    if (root_)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "3D product UI root is already built");
    }
    if (config.targetFrameCount == 0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "3D product UI requires a non-zero frame count");
    }

    config_ = config;
    currentTheme_ = config.initialTheme;
    evidence_->themeDemoRequested = config.automatedThemeDemo;
    evidence_->initialThemeLight = config.initialTheme == Product3DUITheme::Light;
    evidence_->finalThemeLight = evidence_->initialThemeLight;

    auto rootBuilder = context.primaryWindowUIRootBuilder();
    if (!rootBuilder)
    {
        return Core::failure(std::move(rootBuilder.error()));
    }
    auto root = rootBuilder->createRoot();
    if (!root)
    {
        return Core::failure(std::move(root.error()));
    }
    auto tree = rootBuilder->treeUpdater(*root);
    if (!tree)
    {
        return Core::failure(std::move(tree.error()));
    }

    if (Core::Status status = tree->setProductTheme(themeFor(config.initialTheme)); !status)
    {
        return status;
    }
    if (Core::Status status = tree->setLayoutStyle(root->rootNodeId(), rootStyle()); !status)
    {
        return status;
    }
    const UI::UINodeId rootNode = root->rootNodeId();

    const auto addPanel = [&](Rect rect, UI::UINodeId& destination) -> Core::Status {
        return storeNode(createPanel(*tree, rootNode, rect), destination);
    };
    if (Core::Status status = addPanel({24.0F, 20.0F, 560.0F, 78.0F}, nodes_.headerPanel); !status)
    {
        return status;
    }
    if (Core::Status status = addPanel({24.0F, 20.0F, 6.0F, 78.0F}, nodes_.headerAccent); !status)
    {
        return status;
    }
    if (Core::Status status = addPanel({932.0F, 20.0F, 324.0F, 376.0F}, nodes_.inspectorPanel); !status)
    {
        return status;
    }
    if (Core::Status status = addPanel({932.0F, 20.0F, 6.0F, 376.0F}, nodes_.inspectorAccent); !status)
    {
        return status;
    }
    if (Core::Status status = addPanel({932.0F, 412.0F, 324.0F, 284.0F}, nodes_.collectionPanel); !status)
    {
        return status;
    }
    if (Core::Status status = addPanel({932.0F, 412.0F, 6.0F, 284.0F}, nodes_.collectionAccent); !status)
    {
        return status;
    }
    if (Core::Status status = addPanel({24.0F, 646.0F, 884.0F, 50.0F}, nodes_.statusPanel); !status)
    {
        return status;
    }

    const auto addLabel = [&](Rect rect, std::string_view text, UI::UINodeId& destination) -> Core::Status {
        return storeNode(createLabel(*tree, rootNode, rect, text), destination);
    };
    if (Core::Status status = addLabel({48.0F, 32.0F, 510.0F, 30.0F}, "Tina 3D Studio", nodes_.title); !status)
    {
        return status;
    }
    if (Core::Status status = addLabel({48.0F, 65.0F, 510.0F, 22.0F}, "Cooked glTF / PBR Preview", nodes_.subtitle);
        !status)
    {
        return status;
    }
    if (Core::Status status = addLabel({956.0F, 38.0F, 276.0F, 28.0F}, "Scene Controls", nodes_.inspectorTitle);
        !status)
    {
        return status;
    }
    if (Core::Status status =
            addLabel({956.0F, 69.0F, 276.0F, 22.0F}, "PBR materials | 3 lights", nodes_.inspectorMeta);
        !status)
    {
        return status;
    }
    if (Core::Status status = addLabel({996.0F, 174.0F, 230.0F, 24.0F}, "Auto rotate", nodes_.autoRotateLabel); !status)
    {
        return status;
    }
    if (Core::Status status = addLabel({956.0F, 215.0F, 276.0F, 24.0F}, "Rotation speed", nodes_.rotationSpeedLabel);
        !status)
    {
        return status;
    }
    if (Core::Status status = addLabel({956.0F, 293.0F, 190.0F, 24.0F}, "Frame progress", nodes_.progressCaption);
        !status)
    {
        return status;
    }
    if (Core::Status status = addLabel({1168.0F, 293.0F, 64.0F, 24.0F}, "0%", nodes_.progressValue); !status)
    {
        return status;
    }
    if (Core::Status status = addLabel({956.0F, 428.0F, 276.0F, 28.0F}, "Scene & Assets", nodes_.collectionTitle);
        !status)
    {
        return status;
    }
    if (Core::Status status =
            addLabel({956.0F, 459.0F, 276.0F, 22.0F}, "Virtualized product data", nodes_.collectionMeta);
        !status)
    {
        return status;
    }
    if (Core::Status status = addLabel({956.0F, 489.0F, 128.0F, 22.0F}, "Assets", nodes_.assetListLabel); !status)
    {
        return status;
    }
    if (Core::Status status = addLabel({1096.0F, 489.0F, 136.0F, 22.0F}, "Scene graph", nodes_.sceneTreeLabel);
        !status)
    {
        return status;
    }
    if (Core::Status status =
            addLabel({44.0F, 659.0F, 844.0F, 26.0F}, "Dark theme | Auto rotate | 1.00x", nodes_.status);
        !status)
    {
        return status;
    }

    if (Core::Status status = storeNode(
            createButton(*tree, rootNode, {956.0F, 105.0F, 276.0F, 42.0F},
                         config.initialTheme == Product3DUITheme::Dark ? "Switch to light" : "Switch to dark"),
            nodes_.themeButton);
        !status)
    {
        return status;
    }
    if (Core::Status status = tree->setButtonAction(
            nodes_.themeButton, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                ++evidence_->themeButtonActivations;
                requestedTheme_ = oppositeTheme(requestedTheme_.value_or(currentTheme_));
            }});
        !status)
    {
        return status;
    }

    auto checkbox = tree->createElement(rootNode, UI::makeCheckboxElement());
    if (!checkbox)
    {
        return Core::failure(std::move(checkbox.error()));
    }
    nodes_.autoRotateCheckbox = *checkbox;
    if (Core::Status status = tree->setLayoutStyle(*checkbox, absoluteStyle({956.0F, 170.0F, 28.0F, 28.0F})); !status)
    {
        return status;
    }
    if (Core::Status status = tree->setChecked(*checkbox, true); !status)
    {
        return status;
    }
    if (Core::Status status = tree->setCheckboxAction(
            *checkbox, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                ++evidence_->checkboxActivations;
                autoRotateDirty_ = true;
            }});
        !status)
    {
        return status;
    }

    auto slider = tree->createElement(rootNode, UI::makeSliderElement());
    if (!slider)
    {
        return Core::failure(std::move(slider.error()));
    }
    nodes_.rotationSpeedSlider = *slider;
    if (Core::Status status = tree->setLayoutStyle(*slider, absoluteStyle({956.0F, 245.0F, 276.0F, 32.0F})); !status)
    {
        return status;
    }
    if (Core::Status status = tree->setSliderRange(*slider, 0.25F, 2.0F, 0.25F); !status)
    {
        return status;
    }
    if (Core::Status status = tree->setSliderValue(*slider, 1.0F); !status)
    {
        return status;
    }
    if (Core::Status status = tree->setSliderChangeCallback(
            *slider, UI::UISliderChangeCallback{[this](const UI::UISliderChangeEvent& event) noexcept {
                requestedRotationSpeed_ = event.value;
                ++evidence_->sliderChanges;
            }});
        !status)
    {
        return status;
    }

    auto progress = tree->createElement(rootNode, UI::makeProgressBarElement());
    if (!progress)
    {
        return Core::failure(std::move(progress.error()));
    }
    nodes_.frameProgress = *progress;
    if (Core::Status status = tree->setLayoutStyle(*progress, absoluteStyle({956.0F, 327.0F, 276.0F, 18.0F})); !status)
    {
        return status;
    }
    if (Core::Status status = tree->setProgressBarRange(*progress, 0.0F, 100.0F); !status)
    {
        return status;
    }
    if (Core::Status status = tree->setProgressBarValue(*progress, 0.0F); !status)
    {
        return status;
    }

    auto assetList = tree->createElement(rootNode, UI::makeListViewElement({.materializedItemCapacity = 7}));
    if (!assetList)
    {
        return Core::failure(std::move(assetList.error()));
    }
    nodes_.assetList = *assetList;
    if (Core::Status status = tree->setLayoutStyle(*assetList, absoluteStyle({956.0F, 516.0F, 128.0F, 156.0F}));
        !status)
    {
        return status;
    }
    if (Core::Status status = tree->setListViewStyle(
            *assetList,
            UI::UIListViewStyle{
                .rowHeight = 27.0F,
                .overscanRows = 1,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = 27.0F,
            });
        !status)
    {
        return status;
    }
    if (Core::Status status = tree->setListViewDataSource(*assetList, assetListDataSource()); !status)
    {
        return status;
    }
    if (Core::Status status = tree->setListViewSelectedIndex(*assetList, 0); !status)
    {
        return status;
    }

    auto sceneTree = tree->createElement(rootNode, UI::makeTreeViewElement({.materializedItemCapacity = 7}));
    if (!sceneTree)
    {
        return Core::failure(std::move(sceneTree.error()));
    }
    nodes_.sceneTree = *sceneTree;
    if (Core::Status status = tree->setLayoutStyle(*sceneTree, absoluteStyle({1096.0F, 516.0F, 136.0F, 156.0F}));
        !status)
    {
        return status;
    }
    if (Core::Status status = tree->setTreeViewStyle(
            *sceneTree,
            UI::UITreeViewStyle{
                .rowHeight = 27.0F,
                .overscanRows = 1,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = 27.0F,
                .indentation = 12.0F,
                .disclosureExtent = 9.0F,
                .disclosureGap = 4.0F,
            });
        !status)
    {
        return status;
    }
    if (Core::Status status = tree->setTreeViewDataSource(*sceneTree, sceneTreeDataSource()); !status)
    {
        return status;
    }
    if (Core::Status status = tree->setTreeViewSelectedIndex(*sceneTree, 0); !status)
    {
        return status;
    }
    evidence_->listSelectionKey = AssetItemKeyBase;
    evidence_->treeSelectionKey = SceneTreeItemKey;

    if (Core::Status status = applyResponsiveLayout(*tree); !status)
    {
        return status;
    }

    if (Core::Status status = applyTheme(*tree, config.initialTheme, false); !status)
    {
        return status;
    }
    if (Core::Status status = applyProgress(*tree, 0); !status)
    {
        return status;
    }
    if (Core::Status status = publishStatus(*tree); !status)
    {
        return status;
    }

    auto checked = tree->isChecked(nodes_.autoRotateCheckbox);
    auto speed = tree->sliderValue(nodes_.rotationSpeedSlider);
    auto progressValue = tree->progressBarValue(nodes_.frameProgress);
    auto assetSelection = tree->listViewSelection(nodes_.assetList);
    auto sceneSelection = tree->treeViewSelection(nodes_.sceneTree);
    if (!checked)
    {
        return Core::failure(std::move(checked.error()));
    }
    if (!speed)
    {
        return Core::failure(std::move(speed.error()));
    }
    if (!progressValue)
    {
        return Core::failure(std::move(progressValue.error()));
    }
    if (!assetSelection)
    {
        return Core::failure(std::move(assetSelection.error()));
    }
    if (!sceneSelection)
    {
        return Core::failure(std::move(sceneSelection.error()));
    }
    evidence_->listSelectionKey = assetSelection->key;
    evidence_->treeSelectionKey = sceneSelection->key;
    evidence_->controlsInitialStateVerified =
        *checked && std::abs(*speed - 1.0F) <= 0.0001F && std::abs(*progressValue) <= 0.0001F &&
        assetSelection->key == AssetItemKeyBase && sceneSelection->key == SceneTreeItemKey;
    if (!evidence_->controlsInitialStateVerified)
    {
        return Core::failure(Core::CoreErrorCode::Internal, "3D product UI initial control state verification failed");
    }

    evidence_->rootsCreated = 1;
    evidence_->panelsCreated = PanelCount;
    evidence_->labelsCreated = LabelCount;
    evidence_->buttonsCreated = 1;
    evidence_->checkboxesCreated = 1;
    evidence_->slidersCreated = 1;
    evidence_->progressBarsCreated = 1;
    evidence_->listViewsCreated = 1;
    evidence_->treeViewsCreated = 1;
    evidence_->rootAlive = true;
    root_ = std::move(*root);
    return Core::success();
}

Core::Status Product3DUI::applyResponsiveLayout(PrimaryWindowUITreeUpdater& tree)
{
    struct NodeLayout final {
        UI::UINodeId node{};
        Rect reference{};
    };

    const std::array rightAnchored{
        NodeLayout{nodes_.inspectorPanel, {932.0F, 20.0F, 324.0F, 376.0F}},
        NodeLayout{nodes_.inspectorAccent, {932.0F, 20.0F, 6.0F, 376.0F}},
        NodeLayout{nodes_.inspectorTitle, {956.0F, 38.0F, 276.0F, 28.0F}},
        NodeLayout{nodes_.inspectorMeta, {956.0F, 69.0F, 276.0F, 22.0F}},
        NodeLayout{nodes_.autoRotateLabel, {996.0F, 174.0F, 230.0F, 24.0F}},
        NodeLayout{nodes_.rotationSpeedLabel, {956.0F, 215.0F, 276.0F, 24.0F}},
        NodeLayout{nodes_.progressCaption, {956.0F, 293.0F, 190.0F, 24.0F}},
        NodeLayout{nodes_.progressValue, {1168.0F, 293.0F, 64.0F, 24.0F}},
        NodeLayout{nodes_.collectionTitle, {956.0F, 428.0F, 276.0F, 28.0F}},
        NodeLayout{nodes_.collectionMeta, {956.0F, 459.0F, 276.0F, 22.0F}},
        NodeLayout{nodes_.assetListLabel, {956.0F, 489.0F, 128.0F, 22.0F}},
        NodeLayout{nodes_.sceneTreeLabel, {1096.0F, 489.0F, 136.0F, 22.0F}},
        NodeLayout{nodes_.themeButton, {956.0F, 105.0F, 276.0F, 42.0F}},
        NodeLayout{nodes_.autoRotateCheckbox, {956.0F, 170.0F, 28.0F, 28.0F}},
        NodeLayout{nodes_.rotationSpeedSlider, {956.0F, 245.0F, 276.0F, 32.0F}},
        NodeLayout{nodes_.frameProgress, {956.0F, 327.0F, 276.0F, 18.0F}},
    };
    for (const NodeLayout& layout : rightAnchored)
    {
        if (Core::Status status = tree.setLayoutStyle(layout.node, rightAnchoredStyle(layout.reference)); !status)
        {
            return status;
        }
    }

    const std::array rightAnchoredStretchHeight{
        NodeLayout{nodes_.collectionPanel, {932.0F, 412.0F, 324.0F, 284.0F}},
        NodeLayout{nodes_.collectionAccent, {932.0F, 412.0F, 6.0F, 284.0F}},
        NodeLayout{nodes_.assetList, {956.0F, 516.0F, 128.0F, 156.0F}},
        NodeLayout{nodes_.sceneTree, {1096.0F, 516.0F, 136.0F, 156.0F}},
    };
    for (const NodeLayout& layout : rightAnchoredStretchHeight)
    {
        if (Core::Status status = tree.setLayoutStyle(
                layout.node, rightAnchoredStretchHeightStyle(layout.reference));
            !status)
        {
            return status;
        }
    }

    if (Core::Status status = tree.setLayoutStyle(
            nodes_.statusPanel, footerStyle({24.0F, 646.0F, 884.0F, 50.0F}));
        !status)
    {
        return status;
    }
    if (Core::Status status = tree.setLayoutStyle(
            nodes_.status, footerStyle({44.0F, 659.0F, 844.0F, 26.0F}));
        !status)
    {
        return status;
    }

    evidence_->responsiveLayoutVerified = true;
    return Core::success();
}

Core::Status Product3DUI::applyTheme(PrimaryWindowUITreeUpdater& tree, Product3DUITheme mode, bool countSwitch)
{
    const UI::UITheme& theme = themeFor(mode);
    if (Core::Status status = tree.setProductTheme(theme); !status)
    {
        return status;
    }

    if (Core::Status status =
            tree.setBoxPaint(nodes_.headerPanel, UI::makePanelBoxPaint(theme, UI::scaleColorAlpha(theme.surface1, 224),
                                                                       UI::UIElevation::Low));
        !status)
    {
        return status;
    }
    if (Core::Status status = tree.setBoxPaint(nodes_.headerAccent, UI::makeSolidBox(theme.accent)); !status)
    {
        return status;
    }
    if (Core::Status status = tree.setBoxPaint(
            nodes_.inspectorPanel,
            UI::makePanelBoxPaint(theme, UI::scaleColorAlpha(theme.surface0, 236), UI::UIElevation::Low));
        !status)
    {
        return status;
    }
    if (Core::Status status = tree.setBoxPaint(nodes_.inspectorAccent, UI::makeSolidBox(theme.textAccent)); !status)
    {
        return status;
    }
    if (Core::Status status = tree.setBoxPaint(
            nodes_.collectionPanel,
            UI::makePanelBoxPaint(theme, UI::scaleColorAlpha(theme.surface0, 236), UI::UIElevation::Low));
        !status)
    {
        return status;
    }
    if (Core::Status status = tree.setBoxPaint(nodes_.collectionAccent, UI::makeSolidBox(theme.textAccent)); !status)
    {
        return status;
    }
    if (Core::Status status =
            tree.setBoxPaint(nodes_.statusPanel, UI::makePanelBoxPaint(theme, UI::scaleColorAlpha(theme.surface2, 222),
                                                                       UI::UIElevation::None));
        !status)
    {
        return status;
    }

    if (Core::Status status = tree.setTextStyle(nodes_.title, UI::makeTitleTextStyle(theme, 27.0F)); !status)
    {
        return status;
    }
    if (Core::Status status = tree.setTextStyle(nodes_.subtitle, UI::makeSecondaryTextStyle(theme, 16.0F)); !status)
    {
        return status;
    }
    if (Core::Status status = tree.setTextStyle(nodes_.inspectorTitle, UI::makeTitleTextStyle(theme, 22.0F)); !status)
    {
        return status;
    }
    if (Core::Status status = tree.setTextStyle(nodes_.inspectorMeta, UI::makeSecondaryTextStyle(theme, 15.0F));
        !status)
    {
        return status;
    }
    if (Core::Status status = tree.setTextStyle(nodes_.collectionTitle, UI::makeTitleTextStyle(theme, 22.0F)); !status)
    {
        return status;
    }
    if (Core::Status status = tree.setTextStyle(nodes_.collectionMeta, UI::makeSecondaryTextStyle(theme, 15.0F));
        !status)
    {
        return status;
    }
    const std::array bodyLabels{nodes_.autoRotateLabel, nodes_.rotationSpeedLabel, nodes_.progressCaption,
                                nodes_.assetListLabel, nodes_.sceneTreeLabel};
    for (UI::UINodeId label : bodyLabels)
    {
        if (Core::Status status = tree.setTextStyle(label, UI::makeBodyTextStyle(theme, 17.0F)); !status)
        {
            return status;
        }
    }
    if (Core::Status status = tree.setTextStyle(nodes_.progressValue, UI::makeAccentTextStyle(theme, 17.0F)); !status)
    {
        return status;
    }
    if (Core::Status status = tree.setTextStyle(nodes_.status, UI::makeSecondaryTextStyle(theme, 16.0F)); !status)
    {
        return status;
    }
    const UI::UIBoxPaint collectionBox = UI::makePanelBoxPaint(theme, theme.surface0, UI::UIElevation::None);
    if (Core::Status status = tree.setBoxPaint(nodes_.assetList, collectionBox); !status)
    {
        return status;
    }
    if (Core::Status status = tree.setListViewPaint(nodes_.assetList, UI::makeListViewPaint(theme)); !status)
    {
        return status;
    }
    if (Core::Status status = tree.setBoxPaint(nodes_.sceneTree, collectionBox); !status)
    {
        return status;
    }
    if (Core::Status status = tree.setTreeViewPaint(nodes_.sceneTree, UI::makeTreeViewPaint(theme)); !status)
    {
        return status;
    }
    if (Core::Status status =
            tree.setText(nodes_.themeButton, mode == Product3DUITheme::Dark ? "Switch to light" : "Switch to dark");
        !status)
    {
        return status;
    }

    auto activeTheme = tree.productTheme();
    auto buttonPaint = tree.buttonPaint(nodes_.themeButton);
    auto checkboxPaint = tree.checkboxPaint(nodes_.autoRotateCheckbox);
    auto sliderPaint = tree.sliderPaint(nodes_.rotationSpeedSlider);
    auto progressPaint = tree.progressBarPaint(nodes_.frameProgress);
    auto listPaint = tree.listViewPaint(nodes_.assetList);
    auto treePaint = tree.treeViewPaint(nodes_.sceneTree);
    if (!activeTheme)
    {
        return Core::failure(std::move(activeTheme.error()));
    }
    if (!buttonPaint)
    {
        return Core::failure(std::move(buttonPaint.error()));
    }
    if (!checkboxPaint)
    {
        return Core::failure(std::move(checkboxPaint.error()));
    }
    if (!sliderPaint)
    {
        return Core::failure(std::move(sliderPaint.error()));
    }
    if (!progressPaint)
    {
        return Core::failure(std::move(progressPaint.error()));
    }
    if (!listPaint)
    {
        return Core::failure(std::move(listPaint.error()));
    }
    if (!treePaint)
    {
        return Core::failure(std::move(treePaint.error()));
    }
    if (*activeTheme != theme || *buttonPaint != UI::makeButtonChrome(theme).states ||
        *checkboxPaint != UI::makeCheckboxChrome(theme).indicator ||
        *sliderPaint != UI::makeSliderChrome(theme).slider || *progressPaint != UI::makeProgressBarChrome(theme).bar ||
        *listPaint != UI::makeListViewPaint(theme) || *treePaint != UI::makeTreeViewPaint(theme))
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "3D product controls did not inherit the requested UI Theme");
    }

    evidence_->inheritedChromeVerified = true;
    if (countSwitch && mode != currentTheme_)
    {
        ++evidence_->themeSwitches;
    }
    currentTheme_ = mode;
    evidence_->finalThemeLight = mode == Product3DUITheme::Light;
    statusDirty_ = true;
    return Core::success();
}

Core::Status Product3DUI::applyProgress(PrimaryWindowUITreeUpdater& tree, Core::u64 completedFrames)
{
    const double ratio = static_cast<double>((std::min)(completedFrames, config_.targetFrameCount)) /
                         static_cast<double>(config_.targetFrameCount);
    const float progress = static_cast<float>(ratio * 100.0);
    if (Core::Status status = tree.setProgressBarValue(nodes_.frameProgress, progress); !status)
    {
        return status;
    }

    const int percent = static_cast<int>(std::lround(progress));
    if (percent != lastProgressPercent_)
    {
        std::array<char, 16> text{};
        auto [end, error] = std::to_chars(text.data(), text.data() + text.size() - 1, percent);
        if (error != std::errc{})
        {
            return Core::failure(Core::CoreErrorCode::Internal, "3D product progress formatting failed");
        }
        *end++ = '%';
        if (Core::Status status = tree.setText(
                nodes_.progressValue, std::string_view{text.data(), static_cast<Core::usize>(end - text.data())});
            !status)
        {
            return status;
        }
        lastProgressPercent_ = percent;
    }

    ++evidence_->progressUpdates;
    evidence_->finalProgress = progress;
    return Core::success();
}

Core::Status Product3DUI::publishStatus(PrimaryWindowUITreeUpdater& tree)
{
    static constexpr std::array<std::string_view, 8> SpeedLabels{
        "0.25x", "0.50x", "0.75x", "1.00x", "1.25x", "1.50x", "1.75x", "2.00x",
    };
    const int speedIndex = std::clamp(static_cast<int>(std::lround((rotationSpeed_ - 0.25F) / 0.25F)), 0, 7);

    std::string text = currentTheme_ == Product3DUITheme::Dark ? "Dark theme | " : "Light theme | ";
    text += autoRotate_ ? "Auto rotate | " : "Rotation paused | ";
    text += SpeedLabels[static_cast<Core::usize>(speedIndex)];
    text += " | ";
    text += assetLabelForKey(evidence_->listSelectionKey);
    text += " / ";
    text += sceneLabelForKey(evidence_->treeSelectionKey);
    if (Core::Status status = tree.setText(nodes_.status, text); !status)
    {
        return status;
    }
    statusDirty_ = false;
    return Core::success();
}

Core::Status Product3DUI::update(UIUpdateContext& context, Core::u64 completedFrames)
{
    if (!root_ || !context.hasPrimaryWindowUI())
    {
        return Core::success();
    }

    const Core::u64 firstThemeFrame = (std::max)(Core::u64{1}, config_.targetFrameCount / Core::u64{3});
    const Core::u64 secondThemeFrame =
        (std::max)(firstThemeFrame + Core::u64{1}, config_.targetFrameCount - config_.targetFrameCount / Core::u64{3});
    if (config_.automatedThemeDemo && !firstAutomatedThemeStepQueued_ && completedFrames >= firstThemeFrame)
    {
        requestedTheme_ = oppositeTheme(config_.initialTheme);
        requestedAssetSelection_ = 3;
        requestedSceneExpansion_ = false;
        firstAutomatedThemeStepQueued_ = true;
        ++evidence_->automatedThemeSteps;
        ++evidence_->automatedCollectionSteps;
    } else if (config_.automatedThemeDemo && firstAutomatedThemeStepQueued_ && !secondAutomatedThemeStepQueued_ &&
               completedFrames >= secondThemeFrame)
    {
        requestedTheme_ = config_.initialTheme;
        requestedSceneExpansion_ = true;
        requestedSceneSelection_ = 3;
        secondAutomatedThemeStepQueued_ = true;
        ++evidence_->automatedThemeSteps;
        ++evidence_->automatedCollectionSteps;
    }

    auto tree = context.primaryWindowUITreeUpdater(root_);
    if (!tree)
    {
        return Core::failure(std::move(tree.error()));
    }

    if (requestedTheme_.has_value())
    {
        const Product3DUITheme requested = *requestedTheme_;
        requestedTheme_.reset();
        if (Core::Status status = applyTheme(*tree, requested, true); !status)
        {
            return status;
        }
    }
    if (requestedRotationSpeed_.has_value())
    {
        rotationSpeed_ = std::clamp(*requestedRotationSpeed_, 0.25F, 2.0F);
        requestedRotationSpeed_.reset();
        evidence_->rotationSpeed = rotationSpeed_;
        statusDirty_ = true;
    }
    if (autoRotateDirty_)
    {
        autoRotateDirty_ = false;
        auto checked = tree->isChecked(nodes_.autoRotateCheckbox);
        if (!checked)
        {
            return Core::failure(std::move(checked.error()));
        }
        autoRotate_ = *checked;
        evidence_->autoRotate = autoRotate_;
        statusDirty_ = true;
    }
    if (requestedAssetSelection_.has_value())
    {
        const Core::u64 index = *requestedAssetSelection_;
        requestedAssetSelection_.reset();
        if (Core::Status status = tree->setListViewSelectedIndex(nodes_.assetList, index); !status)
        {
            return status;
        }
    }
    if (requestedSceneExpansion_.has_value())
    {
        const bool expanded = *requestedSceneExpansion_;
        requestedSceneExpansion_.reset();
        if (Core::Status status = tree->setTreeViewItemExpanded(nodes_.sceneTree, 0, expanded); !status)
        {
            return status;
        }
    }
    if (requestedSceneSelection_.has_value())
    {
        const Core::u64 index = *requestedSceneSelection_;
        requestedSceneSelection_.reset();
        if (Core::Status status = tree->setTreeViewSelectedIndex(nodes_.sceneTree, index); !status)
        {
            return status;
        }
    }
    auto assetSelection = tree->listViewSelection(nodes_.assetList);
    auto sceneSelection = tree->treeViewSelection(nodes_.sceneTree);
    if (!assetSelection)
    {
        return Core::failure(std::move(assetSelection.error()));
    }
    if (!sceneSelection)
    {
        return Core::failure(std::move(sceneSelection.error()));
    }
    if (assetSelection->key != evidence_->listSelectionKey)
    {
        evidence_->listSelectionKey = assetSelection->key;
        statusDirty_ = true;
    }
    if (sceneSelection->key != evidence_->treeSelectionKey)
    {
        evidence_->treeSelectionKey = sceneSelection->key;
        statusDirty_ = true;
    }
    if (Core::Status status = applyProgress(*tree, completedFrames); !status)
    {
        return status;
    }
    if (statusDirty_)
    {
        if (Core::Status status = publishStatus(*tree); !status)
        {
            return status;
        }
    }
    return Core::success();
}

void Product3DUI::release() noexcept
{
    if (!root_)
    {
        return;
    }
    root_.reset();
    evidence_->rootAlive = false;
    ++evidence_->rootsReleased;
}

} // namespace Tina::Sample3D
