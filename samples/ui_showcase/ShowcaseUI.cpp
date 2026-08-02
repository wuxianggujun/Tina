#include "ShowcaseUI.hpp"
#include "ShowcaseImageFixture.hpp"

#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/ui/UITheme.hpp>
#include <tina/ui/UIElement.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

namespace Tina::SampleUI {
namespace {

namespace UI = Tina::UI;

constexpr UI::UIListViewItemKey ListItemKeyBase = 1'000;
constexpr UI::UITreeViewItemKey WorldTreeItemKey = 1;
constexpr UI::UITreeViewItemKey CameraTreeItemKey = 2;
constexpr UI::UITreeViewItemKey PlayerTreeItemKey = 3;
constexpr UI::UITreeViewItemKey PlayerSpriteTreeItemKey = 4;
constexpr UI::UITreeViewItemKey PlayerPhysicsTreeItemKey = 5;
constexpr UI::UITreeViewItemKey LightingTreeItemKey = 6;
constexpr UI::UITreeViewItemKey UITreeItemKey = 7;
constexpr UI::UITreeViewItemKey HUDTreeItemKey = 8;
constexpr UI::UITreeViewItemKey SettingsTreeItemKey = 9;
constexpr UI::UITreeViewItemKey AudioTreeItemKey = 10;

constexpr std::array<std::string_view, 12> ListItemLabels{
    "Dashboard",      "Scene viewport", "Asset browser", "Entity inspector",
    "Animation graph", "Audio mixer",    "Input mapping", "Physics layers",
    "Render passes",   "Build profiles",  "Diagnostics",   "Package manager",
};

constexpr std::array<std::string_view, 3> DropdownItemLabels{
    "All assets",
    "Ready to ship",
    "Needs review",
};

constexpr std::array<std::string_view, 6> ScrollContentLabels{
    "Scrollable settings", "Bloom intensity", "Shadow quality",
    "Texture streaming",   "Frame pacing",    "Accessibility scale",
};

[[nodiscard]] UI::UILayoutStyle rootStyle() noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Percent(100.0F);
    style.size.height = UI::UILayoutLength::Percent(100.0F);
    return style;
}

[[nodiscard]] UI::UILayoutStyle sizedStyle(float width, float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] UI::UILayoutStyle fillStyle() noexcept
{
    return rootStyle();
}

[[nodiscard]] UI::UILayoutStyle fillWidthStyle(float height) noexcept
{
    UI::UILayoutStyle style = sizedStyle(0.0F, height);
    style.size.width = UI::UILayoutLength::Percent(100.0F);
    return style;
}

[[nodiscard]] UI::UILayoutStyle growingStyle() noexcept
{
    UI::UILayoutStyle style{};
    style.flexItem.grow = 1.0F;
    style.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    return style;
}

[[nodiscard]] UI::UILayoutStyle overlayStyle(float width, float height) noexcept
{
    UI::UILayoutStyle style = sizedStyle(width, height);
    style.placement = UI::UILayoutPlacement::Overlay;
    return style;
}

[[nodiscard]] Core::Status storeNode(Core::Result<UI::UINodeId>&& result, UI::UINodeId& destination)
{
    if (!result) {
        return Core::failure(std::move(result.error()));
    }
    destination = *result;
    return Core::success();
}

[[nodiscard]] Core::Result<UI::UINodeId> createPanel(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
                                                     const UI::UILayoutStyle& layout)
{
    return tree.createElement(parent, UI::makePanelElement(layout));
}

[[nodiscard]] Core::Result<UI::UINodeId> createLabel(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
                                                     const UI::UILayoutStyle& layout, std::string_view text)
{
    return tree.createElement(parent, UI::makeLabelElement(text, layout));
}

[[nodiscard]] Core::Result<UI::UINodeId> createButton(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
                                                      const UI::UILayoutStyle& layout, std::string_view text)
{
    return tree.createElement(parent, UI::makeButtonElement(text, layout));
}

[[nodiscard]] Core::Result<UI::UINodeId> createRadio(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
                                                     const UI::UILayoutStyle& layout, std::string_view text)
{
    return tree.createElement(parent, UI::makeRadioButtonElement(text, layout));
}

[[nodiscard]] Core::Status setTextStyle(PrimaryWindowUITreeUpdater& tree, UI::UINodeId node,
                                        const UI::UITextStyle& style)
{
    if (!node.hasValue()) {
        return Core::success();
    }
    return tree.setTextStyle(node, style);
}

[[nodiscard]] Core::Status setTextStyles(PrimaryWindowUITreeUpdater& tree, std::span<const UI::UINodeId> nodes,
                                         const UI::UITextStyle& style)
{
    for (UI::UINodeId node : nodes) {
        if (Core::Status status = setTextStyle(tree, node, style); !status) {
            return status;
        }
    }
    return Core::success();
}

[[nodiscard]] const UI::UITheme& themeFor(ShowcaseTheme mode, const UI::UITheme& dark,
                                          const UI::UITheme& light) noexcept
{
    return mode == ShowcaseTheme::Dark ? dark : light;
}

} // namespace

UI::UIListViewDataSource ShowcaseUI::listDataSource() const noexcept
{
    return UI::UIListViewDataSource{
        .state = this,
        .itemCount = &ShowcaseUI::listItemCount,
        .resolveItem = &ShowcaseUI::resolveListItem,
    };
}

UI::UITreeViewDataSource ShowcaseUI::treeDataSource() noexcept
{
    return UI::UITreeViewDataSource{
        .state = this,
        .itemCount = &ShowcaseUI::treeItemCount,
        .resolveItem = &ShowcaseUI::resolveTreeItem,
        .setItemExpanded = &ShowcaseUI::setTreeItemExpanded,
    };
}

Core::u64 ShowcaseUI::listItemCount(const void* state) noexcept
{
    return state != nullptr ? ListItemLabels.size() : 0;
}

bool ShowcaseUI::resolveListItem(const void* state, Core::u64 logicalIndex,
                                 UI::UIListViewItemDescriptor& output) noexcept
{
    if (state == nullptr || logicalIndex >= ListItemLabels.size()) {
        return false;
    }
    output = UI::UIListViewItemDescriptor{
        .key = ListItemKeyBase + logicalIndex,
        .label = ListItemLabels[static_cast<Core::usize>(logicalIndex)],
        .enabled = true,
    };
    return true;
}

Core::u64 ShowcaseUI::treeItemCount(const void* state) noexcept
{
    if (state == nullptr) {
        return 0;
    }
    const auto& showcase = *static_cast<const ShowcaseUI*>(state);
    Core::u64 count = 3;
    if (showcase.worldExpanded_) {
        count += 3;
        if (showcase.playerExpanded_) {
            count += 2;
        }
    }
    if (showcase.uiExpanded_) {
        count += 2;
    }
    return count;
}

bool ShowcaseUI::resolveTreeItem(const void* state, Core::u64 logicalIndex,
                                 UI::UITreeViewItemDescriptor& output) noexcept
{
    if (state == nullptr || logicalIndex >= treeItemCount(state)) {
        return false;
    }
    const auto& showcase = *static_cast<const ShowcaseUI*>(state);
    const auto emit = [&logicalIndex, &output](UI::UITreeViewItemKey key, std::string_view label, Core::u32 level,
                                               bool expandable = false, bool expanded = false) noexcept {
        if (logicalIndex == 0) {
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

    if (emit(WorldTreeItemKey, "World", 0, true, showcase.worldExpanded_)) {
        return true;
    }
    if (showcase.worldExpanded_) {
        if (emit(CameraTreeItemKey, "Camera", 1)) {
            return true;
        }
        if (emit(PlayerTreeItemKey, "Player", 1, true, showcase.playerExpanded_)) {
            return true;
        }
        if (showcase.playerExpanded_) {
            if (emit(PlayerSpriteTreeItemKey, "Sprite", 2)) {
                return true;
            }
            if (emit(PlayerPhysicsTreeItemKey, "PhysicsBody", 2)) {
                return true;
            }
        }
        if (emit(LightingTreeItemKey, "Lighting", 1)) {
            return true;
        }
    }
    if (emit(UITreeItemKey, "UI", 0, true, showcase.uiExpanded_)) {
        return true;
    }
    if (showcase.uiExpanded_) {
        if (emit(HUDTreeItemKey, "HUD", 1)) {
            return true;
        }
        if (emit(SettingsTreeItemKey, "Settings", 1)) {
            return true;
        }
    }
    return emit(AudioTreeItemKey, "Audio", 0);
}

bool ShowcaseUI::setTreeItemExpanded(void* state, UI::UITreeViewItemKey key, bool expanded) noexcept
{
    if (state == nullptr) {
        return false;
    }
    auto& showcase = *static_cast<ShowcaseUI*>(state);
    bool* expansion = nullptr;
    switch (key) {
    case WorldTreeItemKey:
        expansion = &showcase.worldExpanded_;
        break;
    case PlayerTreeItemKey:
        expansion = &showcase.playerExpanded_;
        break;
    case UITreeItemKey:
        expansion = &showcase.uiExpanded_;
        break;
    default:
        return false;
    }
    if (*expansion != expanded) {
        *expansion = expanded;
        ++showcase.treeExpansionChanges_;
        showcase.treeExpansionDirty_ = true;
    }
    return true;
}

Core::Status ShowcaseUI::build(GameStateEnterContext& context, ShowcaseTheme initialTheme,
                               Render::Texture2DFrameResourceResolver imageResolver)
{
    if (root_) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "UI showcase root is already built");
    }

    auto rootBuilder = context.primaryWindowUIRootBuilder();
    if (!rootBuilder) {
        return Core::failure(std::move(rootBuilder.error()));
    }

    // Startup-only StyleClass/ColorToken/sheet: product Integration evidence for
    // UI-STYLE-001. Header accent fill is stylesheet-driven via token updates on
    // Dark/Light switch (not setBoxPaint).
    initialTheme_ = initialTheme;
    currentTheme_ = initialTheme;
    const UI::UITheme productTheme =
        initialTheme == ShowcaseTheme::Dark ? UI::makeDefaultProductTheme() : UI::makeLightProductTheme();
    auto chromeClass = rootBuilder->registerStyleClass();
    if (!chromeClass) {
        return Core::failure(std::move(chromeClass.error()));
    }
    auto accentToken = rootBuilder->registerStyleColorToken(productTheme.accent);
    if (!accentToken) {
        return Core::failure(std::move(accentToken.error()));
    }
    showcaseChromeClass_ = *chromeClass;
    headerAccentToken_ = *accentToken;
    const std::array sheetRules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .styleClass = showcaseChromeClass_,
            .colorToken = headerAccentToken_,
        },
    };
    if (Core::Status status = rootBuilder->installStyleSheet(std::span(sheetRules)); !status) {
        return status;
    }
    stylesheetInstalled_ = true;

    auto root = rootBuilder->createRoot();
    if (!root) {
        return Core::failure(std::move(root.error()));
    }
    auto imageResolverRegistration = rootBuilder->bindImageResolver(*root, imageResolver);
    if (!imageResolverRegistration) {
        return Core::failure(std::move(imageResolverRegistration.error()));
    }
    auto tree = rootBuilder->treeUpdater(*root);
    if (!tree) {
        return Core::failure(std::move(tree.error()));
    }

    if (Core::Status status = tree->setProductTheme(productTheme); !status) {
        return status;
    }
    if (Core::Status status = tree->setLayoutStyle(root->rootNodeId(), rootStyle()); !status) {
        return status;
    }

    const UI::UINodeId rootNode = root->rootNodeId();
    UI::UILayoutStyle backgroundLayout = fillStyle();
    backgroundLayout.padding = {
        .left = 24.0F,
        .top = 20.0F,
        .right = 24.0F,
        .bottom = 24.0F,
    };
    backgroundLayout.flexContainer.gap = UI::UILayoutGap::All(16.0F);
    if (Core::Status status = storeNode(createPanel(*tree, rootNode, backgroundLayout), nodes_.background);
        !status) {
        return status;
    }

    UI::UILayoutStyle headerLayout = fillWidthStyle(72.0F);
    headerLayout.padding.right = 34.0F;
    headerLayout.flexContainer.direction = UI::UIFlexDirection::Row;
    headerLayout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    headerLayout.flexContainer.gap.column = 16.0F;
    if (Core::Status status = storeNode(createPanel(*tree, nodes_.background, headerLayout), nodes_.header);
        !status) {
        return status;
    }

    UI::UILayoutStyle headerAccentLayout = sizedStyle(8.0F, 0.0F);
    headerAccentLayout.size.height = UI::UILayoutLength::Percent(100.0F);
    headerAccentLayout.flexItem.shrink = 0.0F;
    UI::UIElementDescriptor headerAccentDesc = UI::makePanelElement(headerAccentLayout);
    headerAccentDesc.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
    headerAccentDesc.visual.styleClasses = std::span(&showcaseChromeClass_, 1);
    if (Core::Status status =
            storeNode(tree->createElement(nodes_.header, headerAccentDesc), nodes_.headerAccent);
        !status) {
        return status;
    }

    UI::UINodeId headerCopy{};
    UI::UILayoutStyle headerCopyLayout = growingStyle();
    headerCopyLayout.size.height = UI::UILayoutLength::Px(52.0F);
    headerCopyLayout.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    if (Core::Status status = storeNode(createPanel(*tree, nodes_.header, headerCopyLayout), headerCopy);
        !status) {
        return status;
    }

    UI::UINodeId main{};
    UI::UILayoutStyle mainLayout = growingStyle();
    mainLayout.size.width = UI::UILayoutLength::Percent(100.0F);
    mainLayout.flexContainer.direction = UI::UIFlexDirection::Row;
    mainLayout.flexContainer.gap.column = 20.0F;
    if (Core::Status status = storeNode(createPanel(*tree, nodes_.background, mainLayout), main); !status) {
        return status;
    }

    UI::UILayoutStyle navigationLayout = sizedStyle(220.0F, 0.0F);
    navigationLayout.size.height = UI::UILayoutLength::Percent(100.0F);
    navigationLayout.flexItem.shrink = 0.0F;
    navigationLayout.padding = {
        .left = 24.0F,
        .top = 24.0F,
        .right = 24.0F,
        .bottom = 26.0F,
    };
    if (Core::Status status = storeNode(createPanel(*tree, main, navigationLayout), nodes_.navigation);
        !status) {
        return status;
    }

    UI::UINodeId cardsArea{};
    UI::UILayoutStyle cardsAreaLayout = growingStyle();
    cardsAreaLayout.size.height = UI::UILayoutLength::Percent(100.0F);
    cardsAreaLayout.flexContainer.gap.row = 16.0F;
    if (Core::Status status = storeNode(createPanel(*tree, main, cardsAreaLayout), cardsArea); !status) {
        return status;
    }

    constexpr std::array<float, 3> CardRowHeights{270.0F, 302.0F, 244.0F};
    std::array<UI::UINodeId, 3> cardRows{};
    for (Core::usize rowIndex = 0; rowIndex < cardRows.size(); ++rowIndex) {
        UI::UILayoutStyle rowLayout = fillWidthStyle(CardRowHeights[rowIndex]);
        rowLayout.flexContainer.direction = UI::UIFlexDirection::Row;
        rowLayout.flexContainer.gap.column = 16.0F;
        if (Core::Status status = storeNode(createPanel(*tree, cardsArea, rowLayout), cardRows[rowIndex]);
            !status) {
            return status;
        }

        UI::UILayoutStyle firstCardLayout = sizedStyle(476.0F, 0.0F);
        firstCardLayout.size.height = UI::UILayoutLength::Percent(100.0F);
        firstCardLayout.flexItem.shrink = 0.0F;
        firstCardLayout.padding = {
            .left = 24.0F,
            .top = 22.0F,
            .right = 24.0F,
            .bottom = 24.0F,
        };
        if (Core::Status status =
                storeNode(createPanel(*tree, cardRows[rowIndex], firstCardLayout), nodes_.cards[rowIndex * 2U]);
            !status) {
            return status;
        }

        UI::UILayoutStyle secondCardLayout = growingStyle();
        secondCardLayout.size.height = UI::UILayoutLength::Percent(100.0F);
        secondCardLayout.padding = firstCardLayout.padding;
        if (Core::Status status = storeNode(createPanel(*tree, cardRows[rowIndex], secondCardLayout),
                                            nodes_.cards[rowIndex * 2U + 1U]);
            !status) {
            return status;
        }
    }

    if (Core::Status status = storeNode(
            createLabel(*tree, headerCopy, fillWidthStyle(30.0F), "Tina UI Control Studio"), nodes_.title);
        !status) {
        return status;
    }
    if (Core::Status status = storeNode(createLabel(*tree, headerCopy, fillWidthStyle(22.0F),
                                                    "Retained controls · 即时换肤 · Keyboard / Gamepad"),
                                        nodes_.subtitle);
        !status) {
        return status;
    }
    if (Core::Status status = storeNode(
            createLabel(*tree, nodes_.header, sizedStyle(172.0F, 24.0F), "LIVE THEME"), nodes_.liveBadge);
        !status) {
        return status;
    }
    if (Core::Status status = storeNode(
            createLabel(*tree, nodes_.navigation, fillWidthStyle(22.0F), "控件索引"), nodes_.navigationTitle);
        !status) {
        return status;
    }

    UI::UINodeId navigationBody{};
    UI::UILayoutStyle navigationBodyLayout = growingStyle();
    navigationBodyLayout.size.width = UI::UILayoutLength::Percent(100.0F);
    navigationBodyLayout.margin.top = 30.0F;
    navigationBodyLayout.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
    if (Core::Status status =
            storeNode(createPanel(*tree, nodes_.navigation, navigationBodyLayout), navigationBody);
        !status) {
        return status;
    }

    UI::UINodeId navigationItems{};
    UI::UILayoutStyle navigationItemsLayout = fillWidthStyle(246.0F);
    navigationItemsLayout.flexContainer.gap.row = 24.0F;
    if (Core::Status status =
            storeNode(createPanel(*tree, navigationBody, navigationItemsLayout), navigationItems);
        !status) {
        return status;
    }

    constexpr std::array<std::string_view, 5> NavigationTexts{
        "Buttons & states",
        "Value controls",
        "Form controls",
        "Theme palette",
        "Data & navigation",
    };
    for (Core::usize index = 0; index < NavigationTexts.size(); ++index) {
        UI::UINodeId navigationRow{};
        UI::UILayoutStyle rowLayout = fillWidthStyle(30.0F);
        rowLayout.flexContainer.direction = UI::UIFlexDirection::Row;
        rowLayout.flexContainer.gap.column = 12.0F;
        if (Core::Status status = storeNode(createPanel(*tree, navigationItems, rowLayout), navigationRow);
            !status) {
            return status;
        }
        if (Core::Status status = storeNode(
                createPanel(*tree, navigationRow, sizedStyle(4.0F, 30.0F)), nodes_.navigationAccents[index]);
            !status) {
            return status;
        }

        UI::UILayoutStyle labelLayout = growingStyle();
        labelLayout.size.height = UI::UILayoutLength::Px(24.0F);
        labelLayout.margin.top = 4.0F;
        if (Core::Status status = storeNode(
                createLabel(*tree, navigationRow, labelLayout, NavigationTexts[index]),
                nodes_.navigationLabels[index]);
            !status) {
            return status;
        }
    }
    if (Core::Status status = storeNode(
            createLabel(*tree, navigationBody, sizedStyle(170.0F, 38.0F),
                        "20 controls online\nTheme synchronized"),
            nodes_.navigationHelp);
        !status) {
        return status;
    }

    constexpr std::array<std::string_view, 6> CardTitles{
        "Buttons & states",
        "Value controls",
        "Form controls",
        "Theme & palette",
        "Selection & popup",
        "Hierarchy & scrolling",
    };
    constexpr std::array<std::string_view, 6> CardSubtitles{
        "Hover, press, focus and disabled chrome",
        "Slider drives a determinate ProgressBar",
        "UTF-8 TextEdit and exclusive Radio groups",
        "Switch the whole retained tree at runtime",
        "Dropdown plus a virtualized ListView",
        "TreeView projection and clipped ScrollView",
    };
    for (Core::usize index = 0; index < CardTitles.size(); ++index) {
        if (Core::Status status = storeNode(
                createLabel(*tree, nodes_.cards[index], fillWidthStyle(26.0F), CardTitles[index]),
                nodes_.cardTitles[index]);
            !status) {
            return status;
        }
        UI::UILayoutStyle subtitleLayout = fillWidthStyle(22.0F);
        subtitleLayout.margin.top = 2.0F;
        if (Core::Status status =
                storeNode(createLabel(*tree, nodes_.cards[index], subtitleLayout, CardSubtitles[index]),
                          nodes_.cardSubtitles[index]);
            !status) {
            return status;
        }
    }

    std::array<UI::UINodeId, 3> buttonRows{};
    for (Core::usize rowIndex = 0; rowIndex < buttonRows.size(); ++rowIndex) {
        UI::UILayoutStyle rowLayout = fillWidthStyle(rowIndex < 2U ? 44.0F : 28.0F);
        rowLayout.margin.top = rowIndex == 0U ? 18.0F : 20.0F;
        rowLayout.flexContainer.direction = UI::UIFlexDirection::Row;
        rowLayout.flexContainer.gap.column = rowIndex < 2U ? 24.0F : 12.0F;
        if (Core::Status status =
                storeNode(createPanel(*tree, nodes_.cards[0], rowLayout), buttonRows[rowIndex]);
            !status) {
            return status;
        }
    }

    UI::UILayoutStyle buttonLayout = sizedStyle(198.0F, 44.0F);
    buttonLayout.padding = UI::UIEdgeSpacing::HorizontalVertical(14.0F, 8.0F);
    buttonLayout.flexContainer.direction = UI::UIFlexDirection::Row;
    buttonLayout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    buttonLayout.flexContainer.justifyContent = UI::UIJustifyContent::Center;
    buttonLayout.flexContainer.gap.column = 8.0F;
    UI::UIElementDescriptor primaryButton = UI::makeButtonElement({}, buttonLayout);
    primaryButton.semantics.name = "Primary action";
    primaryButton.semantics.useContentAsName = false;
    if (Core::Status status =
            storeNode(tree->createElement(buttonRows[0], primaryButton), nodes_.primaryButton);
        !status) {
        return status;
    }
    const UI::UIImageContent primaryIcon{
        .source = showcaseAtlasSource({.x = 0, .y = 0, .width = 16, .height = 16},
                                      {.width = 16.0F, .height = 16.0F}),
        .fit = UI::UIImageFit::Contain,
        .tint = UI::rgba8(255, 255, 255),
        .sampling = UI::UIImageSampling::Nearest,
    };
    if (Core::Status status = storeNode(
            tree->createElement(nodes_.primaryButton,
                                UI::makeIconElement(primaryIcon, sizedStyle(20.0F, 20.0F))),
            nodes_.primaryButtonIcon);
        !status) {
        return status;
    }
    UI::UIElementDescriptor primaryLabel =
        UI::makeLabelElement("Primary action", sizedStyle(124.0F, 24.0F));
    primaryLabel.semantics.mode = UI::UISemanticsMode::Exclude;
    primaryLabel.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    if (Core::Status status = storeNode(
            tree->createElement(nodes_.primaryButton, primaryLabel), nodes_.primaryButtonLabel);
        !status) {
        return status;
    }

    UI::UILayoutStyle destructiveLayout = sizedStyle(44.0F, 44.0F);
    destructiveLayout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    destructiveLayout.flexContainer.justifyContent = UI::UIJustifyContent::Center;
    UI::UIElementDescriptor destructiveButton = UI::makeButtonElement({}, destructiveLayout);
    destructiveButton.semantics.name = "Destructive action";
    destructiveButton.semantics.useContentAsName = false;
    if (Core::Status status = storeNode(
            tree->createElement(buttonRows[0], destructiveButton), nodes_.destructiveButton);
        !status) {
        return status;
    }
    const UI::UIImageContent destructiveIcon{
        .source = showcaseAtlasSource({.x = 16, .y = 0, .width = 16, .height = 16},
                                      {.width = 16.0F, .height = 16.0F}),
        .fit = UI::UIImageFit::Contain,
        .tint = UI::rgba8(255, 255, 255),
        .sampling = UI::UIImageSampling::Nearest,
    };
    if (Core::Status status = storeNode(
            tree->createElement(nodes_.destructiveButton,
                                UI::makeIconElement(destructiveIcon, sizedStyle(20.0F, 20.0F))),
            nodes_.destructiveButtonIcon);
        !status) {
        return status;
    }

    UI::UIElementDescriptor disabledButton = UI::makeButtonElement("Disabled", buttonLayout);
    disabledButton.enabled = false;
    if (Core::Status status =
            storeNode(tree->createElement(buttonRows[1], disabledButton), nodes_.disabledButton);
        !status) {
        return status;
    }
    if (Core::Status status =
            storeNode(createButton(*tree, buttonRows[1], buttonLayout, "Reset state"), nodes_.resetButton);
        !status) {
        return status;
    }

    auto checkbox = tree->createElement(buttonRows[2], UI::makeCheckboxElement(sizedStyle(28.0F, 28.0F)));
    if (!checkbox) {
        return Core::failure(std::move(checkbox.error()));
    }
    nodes_.notificationsCheckbox = *checkbox;
    UI::UILayoutStyle notificationLabelLayout = growingStyle();
    notificationLabelLayout.size.height = UI::UILayoutLength::Px(25.0F);
    notificationLabelLayout.margin.top = 3.0F;
    if (Core::Status status = storeNode(
            createLabel(*tree, buttonRows[2], notificationLabelLayout, "Enable notifications"),
            nodes_.notificationsLabel);
        !status) {
        return status;
    }
    if (Core::Status status = tree->setChecked(nodes_.notificationsCheckbox, true); !status) {
        return status;
    }
    notificationsEnabled_ = true;

    UI::UILayoutStyle sliderLayout = fillWidthStyle(30.0F);
    sliderLayout.margin.top = 22.0F;
    auto slider = tree->createElement(nodes_.cards[1], UI::makeSliderElement(sliderLayout));
    if (!slider) {
        return Core::failure(std::move(slider.error()));
    }
    nodes_.progressSlider = *slider;
    if (Core::Status status = tree->setSliderRange(nodes_.progressSlider, 0.0F, 100.0F, 1.0F); !status) {
        return status;
    }
    if (Core::Status status = tree->setSliderValue(nodes_.progressSlider, progressValue_); !status) {
        return status;
    }

    UI::UILayoutStyle progressLayout = fillWidthStyle(22.0F);
    progressLayout.margin.top = 20.0F;
    auto progress = tree->createElement(nodes_.cards[1], UI::makeProgressBarElement(progressLayout));
    if (!progress) {
        return Core::failure(std::move(progress.error()));
    }
    nodes_.progressBar = *progress;
    if (Core::Status status = tree->setProgressBarRange(nodes_.progressBar, 0.0F, 100.0F); !status) {
        return status;
    }
    if (Core::Status status = tree->setProgressBarValue(nodes_.progressBar, progressValue_); !status) {
        return status;
    }

    UI::UILayoutStyle progressLabelLayout = sizedStyle(200.0F, 28.0F);
    progressLabelLayout.margin.top = 18.0F;
    if (Core::Status status = storeNode(
            createLabel(*tree, nodes_.cards[1], progressLabelLayout, "72%"), nodes_.progressLabel);
        !status) {
        return status;
    }

    UI::UILayoutStyle profileLabelLayout = sizedStyle(180.0F, 22.0F);
    profileLabelLayout.margin.top = 12.0F;
    if (Core::Status status = storeNode(
            createLabel(*tree, nodes_.cards[2], profileLabelLayout, "Profile name"), nodes_.profileLabel);
        !status) {
        return status;
    }

    UI::UILayoutStyle textEditLayout = fillWidthStyle(46.0F);
    textEditLayout.margin.top = 6.0F;
    textEditLayout.padding = UI::UIEdgeSpacing::HorizontalVertical(12.0F, 6.0F);
    auto textEdit = tree->createElement(nodes_.cards[2], UI::makeTextEditElement("Tina Player", textEditLayout));
    if (!textEdit) {
        return Core::failure(std::move(textEdit.error()));
    }
    nodes_.profileTextEdit = *textEdit;
    if (Core::Status status = tree->setTextSelection(nodes_.profileTextEdit,
                                                     UI::UITextSelection{.anchorCodepoint = 11, .caretCodepoint = 11});
        !status) {
        return status;
    }

    UI::UILayoutStyle qualityLabelLayout = sizedStyle(180.0F, 22.0F);
    qualityLabelLayout.margin.top = 20.0F;
    if (Core::Status status = storeNode(
            createLabel(*tree, nodes_.cards[2], qualityLabelLayout, "Render profile"), nodes_.qualityLabel);
        !status) {
        return status;
    }

    UI::UILayoutStyle qualityGroupLayout = sizedStyle(420.0F, 56.0F);
    qualityGroupLayout.margin.top = 4.0F;
    qualityGroupLayout.padding.top = 4.0F;
    qualityGroupLayout.padding.bottom = 12.0F;
    qualityGroupLayout.flexContainer.direction = UI::UIFlexDirection::Row;
    qualityGroupLayout.flexContainer.gap.column = 6.0F;
    if (Core::Status status = storeNode(
            createPanel(*tree, nodes_.cards[2], qualityGroupLayout), nodes_.qualityGroup);
        !status) {
        return status;
    }

    constexpr std::array<std::string_view, 3> QualityLabels{
        "Performance",
        "Balanced",
        "Quality",
    };
    constexpr std::array<float, 3> QualityWidths{164.0F, 132.0F, 112.0F};
    for (Core::usize index = 0; index < QualityLabels.size(); ++index) {
        if (Core::Status status =
                storeNode(createRadio(*tree, nodes_.qualityGroup, sizedStyle(QualityWidths[index], 40.0F),
                                      QualityLabels[index]),
                          nodes_.qualityRadios[index]);
            !status) {
            return status;
        }
    }
    if (Core::Status status = tree->setRadioButtonSelected(nodes_.qualityRadios[1], true); !status) {
        return status;
    }

    if (Core::Status status = storeNode(
            createLabel(*tree, nodes_.cards[3], fillWidthStyle(20.0F), "Appearance"),
            nodes_.appearanceLabel);
        !status) {
        return status;
    }

    UI::UILayoutStyle themeGroupLayout = sizedStyle(286.0F, 40.0F);
    themeGroupLayout.flexContainer.direction = UI::UIFlexDirection::Row;
    themeGroupLayout.flexContainer.gap.column = 22.0F;
    if (Core::Status status =
            storeNode(createPanel(*tree, nodes_.cards[3], themeGroupLayout), nodes_.themeGroup);
        !status) {
        return status;
    }

    constexpr std::array<std::string_view, 2> ThemeLabels{"Dark", "Light"};
    for (Core::usize index = 0; index < ThemeLabels.size(); ++index) {
        if (Core::Status status =
                storeNode(createRadio(*tree, nodes_.themeGroup, sizedStyle(132.0F, 40.0F), ThemeLabels[index]),
                          nodes_.themeRadios[index]);
            !status) {
            return status;
        }
    }
    if (Core::Status status =
            tree->setRadioButtonSelected(nodes_.themeRadios[initialTheme == ShowcaseTheme::Dark ? 0U : 1U], true);
        !status) {
        return status;
    }

    UI::UINodeId paletteRow{};
    UI::UILayoutStyle paletteRowLayout = fillWidthStyle(44.0F);
    paletteRowLayout.margin.top = 32.0F;
    paletteRowLayout.flexContainer.direction = UI::UIFlexDirection::Row;
    paletteRowLayout.flexContainer.gap.column = 20.0F;
    if (Core::Status status =
            storeNode(createPanel(*tree, nodes_.cards[3], paletteRowLayout), paletteRow);
        !status) {
        return status;
    }
    const UI::UIImageContent inventoryThumbnail{
        .source = showcaseAtlasSource({.x = 32, .y = 0, .width = 32, .height = 32},
                                      {.width = 32.0F, .height = 32.0F}),
        .fit = UI::UIImageFit::Cover,
        .tint = UI::rgba8(255, 255, 255),
        .sampling = UI::UIImageSampling::Linear,
    };
    if (Core::Status status = storeNode(
            tree->createElement(paletteRow,
                                UI::makeImageElement(inventoryThumbnail, "Inventory potion thumbnail",
                                                     sizedStyle(44.0F, 44.0F))),
            nodes_.inventoryThumbnail);
        !status) {
        return status;
    }
    for (UI::UINodeId& swatch : nodes_.paletteSwatches) {
        if (Core::Status status = storeNode(createPanel(*tree, paletteRow, sizedStyle(92.0F, 44.0F)), swatch);
            !status) {
            return status;
        }
    }

    UI::UILayoutStyle statusPanelLayout = sizedStyle(430.0F, 48.0F);
    statusPanelLayout.margin.top = 22.0F;
    statusPanelLayout.padding.left = 16.0F;
    statusPanelLayout.padding.right = 16.0F;
    statusPanelLayout.flexContainer.direction = UI::UIFlexDirection::Row;
    statusPanelLayout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    const std::array statusPanelCanvas{
        UI::UICanvasCommand{
            .kind = UI::UICanvasCommandKind::NineSlice,
            .bounds = {.x = 0.0F, .y = 0.0F, .width = 430.0F, .height = 48.0F},
            .color = UI::rgba8(255, 255, 255),
            .imageSource = showcaseAtlasSource({.x = 0, .y = 32, .width = 32, .height = 32},
                                               {.width = 32.0F, .height = 32.0F}),
            .imageSourceInsets = {.left = 8, .top = 8, .right = 8, .bottom = 8},
            .imageDestinationInsets = {.left = 8.0F, .top = 8.0F, .right = 8.0F, .bottom = 8.0F},
            .imageSampling = UI::UIImageSampling::Linear,
        },
    };
    UI::UIElementDescriptor statusPanel = UI::makePanelElement(statusPanelLayout);
    statusPanel.visual.canvas = statusPanelCanvas;
    if (Core::Status status =
            storeNode(tree->createElement(nodes_.cards[3], statusPanel), nodes_.statusPanel);
        !status) {
        return status;
    }
    UI::UILayoutStyle statusLabelLayout = growingStyle();
    statusLabelLayout.size.height = UI::UILayoutLength::Px(24.0F);
    if (Core::Status status = storeNode(
            createLabel(*tree, nodes_.statusPanel, statusLabelLayout, "Ready"), nodes_.statusLabel);
        !status) {
        return status;
    }

    UI::UINodeId selectionLabels{};
    UI::UILayoutStyle selectionLabelsLayout = fillWidthStyle(20.0F);
    selectionLabelsLayout.margin.top = 8.0F;
    selectionLabelsLayout.flexContainer.direction = UI::UIFlexDirection::Row;
    selectionLabelsLayout.flexContainer.gap.column = 18.0F;
    if (Core::Status status =
            storeNode(createPanel(*tree, nodes_.cards[4], selectionLabelsLayout), selectionLabels);
        !status) {
        return status;
    }
    if (Core::Status status = storeNode(
            createLabel(*tree, selectionLabels, sizedStyle(190.0F, 20.0F), "Asset filter"),
            nodes_.dropdownLabel);
        !status) {
        return status;
    }
    if (Core::Status status = storeNode(
            createLabel(*tree, selectionLabels, sizedStyle(220.0F, 20.0F), "Virtualized workspace"),
            nodes_.listLabel);
        !status) {
        return status;
    }

    UI::UINodeId selectionControls{};
    UI::UILayoutStyle selectionControlsLayout = fillWidthStyle(114.0F);
    selectionControlsLayout.margin.top = 6.0F;
    selectionControlsLayout.flexContainer.direction = UI::UIFlexDirection::Row;
    selectionControlsLayout.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    selectionControlsLayout.flexContainer.gap.column = 18.0F;
    if (Core::Status status =
            storeNode(createPanel(*tree, nodes_.cards[4], selectionControlsLayout), selectionControls);
        !status) {
        return status;
    }

    UI::UILayoutStyle dropdownLayout = sizedStyle(190.0F, 40.0F);
    dropdownLayout.padding = UI::UIEdgeSpacing::HorizontalVertical(12.0F, 6.0F);
    auto dropdown = tree->createElement(
        selectionControls, UI::makeDropdownElement(DropdownItemLabels[0], dropdownLayout));
    if (!dropdown) {
        return Core::failure(std::move(dropdown.error()));
    }
    nodes_.dropdown = *dropdown;
    auto popup = tree->createElement(nodes_.dropdown, UI::makePopupElement(overlayStyle(190.0F, 108.0F)));
    if (!popup) {
        return Core::failure(std::move(popup.error()));
    }
    nodes_.dropdownPopup = *popup;
    if (Core::Status status = tree->setPopupStyle(nodes_.dropdownPopup,
                                                  UI::UIPopupStyle{
                                                      .placement = UI::UIPopupPlacement::Below,
                                                      .anchorGap = 4.0F,
                                                      .matchAnchorWidth = true,
                                                  });
        !status) {
        return status;
    }
    for (Core::usize index = 0; index < nodes_.dropdownItems.size(); ++index) {
        auto item = tree->createElement(
            nodes_.dropdownPopup,
            UI::makeDropdownItemElement(DropdownItemLabels[index], sizedStyle(190.0F, 36.0F)));
        if (!item) {
            return Core::failure(std::move(item.error()));
        }
        nodes_.dropdownItems[index] = *item;
    }
    if (Core::Status status = tree->setDropdownSelectedItem(nodes_.dropdown, nodes_.dropdownItems[0]); !status) {
        return status;
    }
    if (Core::Status status = tree->setDropdownOpen(nodes_.dropdown, false); !status) {
        return status;
    }
    dropdownSelection_ = nodes_.dropdownItems[0];
    dropdownSelectionIndex_ = 0;

    auto listView = tree->createElement(
        selectionControls,
        UI::makeListViewElement({.materializedItemCapacity = 8}, sizedStyle(220.0F, 114.0F)));
    if (!listView) {
        return Core::failure(std::move(listView.error()));
    }
    nodes_.listView = *listView;
    if (Core::Status status = tree->setListViewStyle(nodes_.listView,
                                                     UI::UIListViewStyle{
                                                         .rowHeight = 26.0F,
                                                         .overscanRows = 1,
                                                         .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                                                         .wheelStep = 26.0F,
                                                     });
        !status) {
        return status;
    }
    if (Core::Status status = tree->setListViewDataSource(nodes_.listView, listDataSource()); !status) {
        return status;
    }
    if (Core::Status status = tree->setListViewSelectedIndex(nodes_.listView, 2); !status) {
        return status;
    }
    listSelectionKey_ = ListItemKeyBase + 2;

    UI::UINodeId hierarchyLabels{};
    UI::UILayoutStyle hierarchyLabelsLayout = fillWidthStyle(20.0F);
    hierarchyLabelsLayout.margin.top = 8.0F;
    hierarchyLabelsLayout.flexContainer.direction = UI::UIFlexDirection::Row;
    hierarchyLabelsLayout.flexContainer.gap.column = 16.0F;
    if (Core::Status status =
            storeNode(createPanel(*tree, nodes_.cards[5], hierarchyLabelsLayout), hierarchyLabels);
        !status) {
        return status;
    }
    if (Core::Status status = storeNode(
            createLabel(*tree, hierarchyLabels, sizedStyle(250.0F, 20.0F), "Scene hierarchy"),
            nodes_.treeLabel);
        !status) {
        return status;
    }
    if (Core::Status status = storeNode(
            createLabel(*tree, hierarchyLabels, sizedStyle(186.0F, 20.0F), "Settings feed"),
            nodes_.scrollLabel);
        !status) {
        return status;
    }

    UI::UINodeId hierarchyControls{};
    UI::UILayoutStyle hierarchyControlsLayout = fillWidthStyle(114.0F);
    hierarchyControlsLayout.margin.top = 6.0F;
    hierarchyControlsLayout.flexContainer.direction = UI::UIFlexDirection::Row;
    hierarchyControlsLayout.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    hierarchyControlsLayout.flexContainer.gap.column = 16.0F;
    if (Core::Status status =
            storeNode(createPanel(*tree, nodes_.cards[5], hierarchyControlsLayout), hierarchyControls);
        !status) {
        return status;
    }

    auto treeView = tree->createElement(
        hierarchyControls,
        UI::makeTreeViewElement({.materializedItemCapacity = 9}, sizedStyle(250.0F, 114.0F)));
    if (!treeView) {
        return Core::failure(std::move(treeView.error()));
    }
    nodes_.treeView = *treeView;
    if (Core::Status status = tree->setTreeViewStyle(nodes_.treeView,
                                                     UI::UITreeViewStyle{
                                                         .rowHeight = 26.0F,
                                                         .overscanRows = 1,
                                                         .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                                                         .wheelStep = 26.0F,
                                                         .indentation = 15.0F,
                                                         .disclosureExtent = 10.0F,
                                                         .disclosureGap = 5.0F,
                                                     });
        !status) {
        return status;
    }
    if (Core::Status status = tree->setTreeViewDataSource(nodes_.treeView, treeDataSource()); !status) {
        return status;
    }
    if (Core::Status status = tree->setTreeViewSelectedIndex(nodes_.treeView, 0); !status) {
        return status;
    }
    treeSelectionKey_ = WorldTreeItemKey;

    auto scrollView = tree->createElement(
        hierarchyControls, UI::makeScrollViewElement(sizedStyle(186.0F, 114.0F)));
    if (!scrollView) {
        return Core::failure(std::move(scrollView.error()));
    }
    nodes_.scrollView = *scrollView;
    if (Core::Status status = tree->setScrollViewStyle(nodes_.scrollView,
                                                       UI::UIScrollViewStyle{
                                                           .axes = UI::UIScrollAxes::Vertical,
                                                           .scrollBarVisibility = UI::UIScrollBarVisibility::Always,
                                                           .wheelStep = 30.0F,
                                                       });
        !status) {
        return status;
    }
    UI::UILayoutStyle scrollContentLayout = sizedStyle(170.0F, 248.0F);
    scrollContentLayout.padding = {
        .left = 8.0F,
        .top = 10.0F,
        .right = 8.0F,
        .bottom = 24.0F,
    };
    scrollContentLayout.flexContainer.gap.row = 14.0F;
    auto scrollContent = tree->createElement(
        nodes_.scrollView, UI::makePanelElement(scrollContentLayout));
    if (!scrollContent) {
        return Core::failure(std::move(scrollContent.error()));
    }
    nodes_.scrollContent = *scrollContent;
    for (Core::usize index = 0; index < ScrollContentLabels.size(); ++index) {
        if (Core::Status status = storeNode(
                createLabel(*tree, nodes_.scrollContent, sizedStyle(146.0F, 24.0F), ScrollContentLabels[index]),
                nodes_.scrollContentLabels[index]);
            !status) {
            return status;
        }
    }

    if (Core::Status status = tree->setButtonAction(
            nodes_.primaryButton, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                ++buttonActivations_;
                pendingStatus_ = StatusMessage::PrimaryAction;
                statusDirty_ = true;
            }});
        !status) {
        return status;
    }
    if (Core::Status status = tree->setButtonAction(
            nodes_.destructiveButton, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                ++buttonActivations_;
                pendingStatus_ = StatusMessage::DestructiveAction;
                statusDirty_ = true;
            }});
        !status) {
        return status;
    }
    if (Core::Status status = tree->setButtonAction(
            nodes_.resetButton, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                ++buttonActivations_;
                resetRequested_ = true;
            }});
        !status) {
        return status;
    }
    if (Core::Status status = tree->setCheckboxAction(
            nodes_.notificationsCheckbox, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                notificationsDirty_ = true;
            }});
        !status) {
        return status;
    }
    if (Core::Status status = tree->setSliderChangeCallback(
            nodes_.progressSlider, UI::UISliderChangeCallback{[this](const UI::UISliderChangeEvent& event) noexcept {
                requestedProgressValue_ = event.value;
                progressDirty_ = true;
                ++sliderChanges_;
            }});
        !status) {
        return status;
    }

    constexpr std::array<ShowcaseQuality, 3> QualityModes{
        ShowcaseQuality::Performance,
        ShowcaseQuality::Balanced,
        ShowcaseQuality::Quality,
    };
    constexpr std::array<StatusMessage, 3> QualityMessages{
        StatusMessage::QualityPerformance,
        StatusMessage::QualityBalanced,
        StatusMessage::QualityQuality,
    };
    for (Core::usize index = 0; index < nodes_.qualityRadios.size(); ++index) {
        if (Core::Status status = tree->setRadioButtonAction(
                nodes_.qualityRadios[index],
                UI::UIButtonActionCallback{[this, mode = QualityModes[index],
                                            message = QualityMessages[index]](const UI::UIButtonActionEvent&) noexcept {
                    quality_ = mode;
                    pendingStatus_ = message;
                    statusDirty_ = true;
                }});
            !status) {
            return status;
        }
    }
    if (Core::Status status = tree->setRadioButtonAction(
            nodes_.themeRadios[0], UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                requestedTheme_ = ShowcaseTheme::Dark;
            }});
        !status) {
        return status;
    }
    if (Core::Status status = tree->setRadioButtonAction(
            nodes_.themeRadios[1], UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                requestedTheme_ = ShowcaseTheme::Light;
            }});
        !status) {
        return status;
    }

    if (Core::Status status = applyTheme(*tree, initialTheme, false); !status) {
        return status;
    }
    if (Core::Status status = applyProgress(*tree, progressValue_); !status) {
        return status;
    }
    if (Core::Status status = publishStatus(*tree, StatusMessage::Ready); !status) {
        return status;
    }

    controlCount_ = 20;
    imageProductCount_ = 4;
    imageResolver_ = std::move(*imageResolverRegistration);
    root_ = std::move(*root);
    return Core::success();
}

Core::Status ShowcaseUI::applyTheme(PrimaryWindowUITreeUpdater& tree, ShowcaseTheme mode, bool countSwitch)
{
    constexpr UI::UITheme Dark = UI::makeDefaultProductTheme();
    constexpr UI::UITheme Light = UI::makeLightProductTheme();
    const UI::UITheme& theme = themeFor(mode, Dark, Light);

    if (Core::Status status = tree.setProductTheme(theme); !status) {
        return status;
    }
    if (Core::Status status = tree.setBoxPaint(nodes_.background, UI::makeSolidBox(theme.surface0)); !status) {
        return status;
    }
    if (Core::Status status =
            tree.setBoxPaint(nodes_.header, UI::makePanelBoxPaint(theme, theme.surface1, UI::UIElevation::Low));
        !status) {
        return status;
    }
    // Header accent uses stylesheet ColorToken (product Integration path).
    if (Core::Status status = tree.setStyleColorToken(headerAccentToken_, theme.accent); !status) {
        return status;
    }
    ++styleTokenUpdates_;
    if (Core::Status status =
            tree.setBoxPaint(nodes_.navigation, UI::makePanelBoxPaint(theme, theme.surface1, UI::UIElevation::Low));
        !status) {
        return status;
    }
    for (UI::UINodeId card : nodes_.cards) {
        if (Core::Status status =
                tree.setBoxPaint(card, UI::makePanelBoxPaint(theme, theme.surface1, UI::UIElevation::Low));
            !status) {
            return status;
        }
    }

    const std::array accentColors{
        theme.accent,
        theme.textAccent,
        theme.focusRing,
        theme.danger,
        theme.scrollBarThumbActive,
    };
    for (Core::usize index = 0; index < nodes_.navigationAccents.size(); ++index) {
        if (Core::Status status =
                tree.setBoxPaint(nodes_.navigationAccents[index], UI::makeSolidBox(accentColors[index]));
            !status) {
            return status;
        }
    }
    const std::array swatchColors{
        theme.surface0,
        theme.surface2,
        theme.accent,
    };
    for (Core::usize index = 0; index < nodes_.paletteSwatches.size(); ++index) {
        if (Core::Status status =
                tree.setBoxPaint(nodes_.paletteSwatches[index],
                                 UI::makePanelBoxPaint(theme, swatchColors[index], UI::UIElevation::None));
            !status) {
            return status;
        }
    }
    if (Core::Status status =
            tree.setBoxPaint(nodes_.statusPanel, UI::makePanelBoxPaint(theme, theme.surface2, UI::UIElevation::None));
        !status) {
        return status;
    }
    const UI::UIBoxPaint collectionBox = UI::makePanelBoxPaint(theme, theme.surface0, UI::UIElevation::None);
    if (Core::Status status = tree.setBoxPaint(nodes_.listView, collectionBox); !status) {
        return status;
    }
    if (Core::Status status = tree.setListViewPaint(nodes_.listView, UI::makeListViewPaint(theme)); !status) {
        return status;
    }
    if (Core::Status status = tree.setBoxPaint(nodes_.treeView, collectionBox); !status) {
        return status;
    }
    if (Core::Status status = tree.setTreeViewPaint(nodes_.treeView, UI::makeTreeViewPaint(theme)); !status) {
        return status;
    }
    if (Core::Status status = tree.setBoxPaint(nodes_.scrollView, collectionBox); !status) {
        return status;
    }
    if (Core::Status status = tree.setBoxPaint(nodes_.scrollContent, UI::makeSolidBox(theme.surface0)); !status) {
        return status;
    }
    if (Core::Status status = tree.setScrollViewPaint(nodes_.scrollView, UI::makeScrollViewPaint(theme)); !status) {
        return status;
    }

    const UI::UIDropdownChrome dropdown = UI::makeDropdownChrome(theme);
    if (Core::Status status = tree.setBoxPaint(nodes_.dropdown, dropdown.box); !status) {
        return status;
    }
    if (Core::Status status = tree.setButtonPaint(nodes_.dropdown, dropdown.states); !status) {
        return status;
    }
    if (Core::Status status = tree.setDropdownPaint(nodes_.dropdown, dropdown.dropdown); !status) {
        return status;
    }
    if (Core::Status status = tree.setTextStyle(nodes_.dropdown, dropdown.label); !status) {
        return status;
    }
    if (Core::Status status = tree.setBoxPaint(nodes_.dropdownPopup, UI::makePopupBoxPaint(theme)); !status) {
        return status;
    }
    const UI::UIButtonChrome dropdownItem = UI::makeDropdownItemChrome(theme);
    for (UI::UINodeId item : nodes_.dropdownItems) {
        if (Core::Status status = tree.setBoxPaint(item, dropdownItem.box); !status) {
            return status;
        }
        if (Core::Status status = tree.setButtonPaint(item, dropdownItem.states); !status) {
            return status;
        }
        if (Core::Status status = tree.setTextStyle(item, dropdownItem.label); !status) {
            return status;
        }
    }

    if (Core::Status status = setTextStyle(tree, nodes_.title, UI::makeTitleTextStyle(theme, 28.0F)); !status) {
        return status;
    }
    if (Core::Status status = setTextStyle(tree, nodes_.subtitle, UI::makeSecondaryTextStyle(theme, 16.0F)); !status) {
        return status;
    }
    if (Core::Status status = setTextStyle(tree, nodes_.liveBadge, UI::makeAccentTextStyle(theme, 16.0F)); !status) {
        return status;
    }
    if (Core::Status status = setTextStyle(tree, nodes_.navigationTitle, UI::makeSecondaryTextStyle(theme, 15.0F));
        !status) {
        return status;
    }
    if (Core::Status status = setTextStyles(tree, nodes_.navigationLabels, UI::makeBodyTextStyle(theme, 17.0F));
        !status) {
        return status;
    }
    if (Core::Status status = setTextStyle(tree, nodes_.navigationHelp, UI::makeSecondaryTextStyle(theme, 14.0F));
        !status) {
        return status;
    }
    if (Core::Status status = setTextStyles(tree, nodes_.cardTitles, UI::makeTitleTextStyle(theme, 20.0F)); !status) {
        return status;
    }
    if (Core::Status status = setTextStyles(tree, nodes_.cardSubtitles, UI::makeSecondaryTextStyle(theme, 15.0F));
        !status) {
        return status;
    }
    const std::array bodyLabels{
        nodes_.notificationsLabel,
    };
    if (Core::Status status = setTextStyles(tree, bodyLabels, UI::makeBodyTextStyle(theme, 17.0F)); !status) {
        return status;
    }
    const UI::UIButtonChrome primary = UI::makeButtonChrome(theme);
    if (Core::Status status = setTextStyle(tree, nodes_.primaryButtonLabel, primary.label); !status) {
        return status;
    }
    const std::array secondaryLabels{
        nodes_.profileLabel,
        nodes_.qualityLabel,
        nodes_.appearanceLabel,
        nodes_.statusLabel,
        nodes_.dropdownLabel,
        nodes_.listLabel,
        nodes_.treeLabel,
        nodes_.scrollLabel,
    };
    if (Core::Status status = setTextStyles(tree, secondaryLabels, UI::makeSecondaryTextStyle(theme, 16.0F)); !status) {
        return status;
    }
    if (Core::Status status = setTextStyle(tree, nodes_.progressLabel, UI::makeAccentTextStyle(theme, 20.0F));
        !status) {
        return status;
    }
    if (Core::Status status =
            setTextStyles(tree, nodes_.scrollContentLabels, UI::makeBodyTextStyle(theme, 14.0F));
        !status) {
        return status;
    }

    const UI::UIButtonChrome destructive = UI::makeButtonChrome(theme, UI::scaleColorAlpha(theme.danger, 230));
    if (Core::Status status = tree.setBoxPaint(nodes_.destructiveButton, destructive.box); !status) {
        return status;
    }
    if (Core::Status status = tree.setButtonPaint(nodes_.destructiveButton, destructive.states); !status) {
        return status;
    }
    if (Core::Status status = tree.setTextStyle(nodes_.destructiveButton, destructive.label); !status) {
        return status;
    }

    if (Core::Status status =
            tree.setRadioButtonSelected(nodes_.themeRadios[mode == ShowcaseTheme::Dark ? 0U : 1U], true);
        !status) {
        return status;
    }

    if (countSwitch && mode != currentTheme_) {
        ++themeSwitches_;
    }
    currentTheme_ = mode;
    pendingStatus_ = mode == ShowcaseTheme::Dark ? StatusMessage::ThemeDark : StatusMessage::ThemeLight;
    statusDirty_ = true;
    return Core::success();
}

Core::Status ShowcaseUI::applyProgress(PrimaryWindowUITreeUpdater& tree, float value)
{
    const float clamped = std::clamp(value, 0.0F, 100.0F);
    if (Core::Status status = tree.setProgressBarValue(nodes_.progressBar, clamped); !status) {
        return status;
    }

    std::array<char, 16> text{};
    const int percent = static_cast<int>(std::lround(clamped));
    auto [end, error] = std::to_chars(text.data(), text.data() + text.size() - 1, percent);
    if (error != std::errc{}) {
        return Core::failure(Core::CoreErrorCode::Internal, "UI showcase progress label formatting failed");
    }
    *end = '%';
    ++end;
    if (Core::Status status = tree.setText(nodes_.progressLabel,
                                           std::string_view{text.data(), static_cast<Core::usize>(end - text.data())});
        !status) {
        return status;
    }

    progressValue_ = clamped;
    progressDirty_ = false;
    pendingStatus_ = StatusMessage::ProgressChanged;
    statusDirty_ = true;
    return Core::success();
}

Core::Status ShowcaseUI::applyReset(PrimaryWindowUITreeUpdater& tree)
{
    if (Core::Status status = tree.setChecked(nodes_.notificationsCheckbox, true); !status) {
        return status;
    }
    notificationsEnabled_ = true;
    notificationsDirty_ = false;

    if (Core::Status status = tree.setSliderValue(nodes_.progressSlider, 72.0F); !status) {
        return status;
    }
    if (Core::Status status = applyProgress(tree, 72.0F); !status) {
        return status;
    }
    if (Core::Status status = tree.setText(nodes_.profileTextEdit, "Tina Player"); !status) {
        return status;
    }
    if (Core::Status status = tree.setTextSelection(nodes_.profileTextEdit,
                                                    UI::UITextSelection{.anchorCodepoint = 11, .caretCodepoint = 11});
        !status) {
        return status;
    }
    if (Core::Status status = tree.setRadioButtonSelected(nodes_.qualityRadios[1], true); !status) {
        return status;
    }
    quality_ = ShowcaseQuality::Balanced;

    if (Core::Status status = tree.setDropdownSelectedItem(nodes_.dropdown, nodes_.dropdownItems[0]); !status) {
        return status;
    }
    if (Core::Status status = tree.setDropdownOpen(nodes_.dropdown, false); !status) {
        return status;
    }
    if (Core::Status status = tree.setText(nodes_.dropdown, DropdownItemLabels[0]); !status) {
        return status;
    }
    dropdownSelection_ = nodes_.dropdownItems[0];
    dropdownSelectionIndex_ = 0;

    if (Core::Status status = tree.setListViewSelectedIndex(nodes_.listView, 2); !status) {
        return status;
    }
    if (Core::Status status = tree.scrollListViewToIndex(nodes_.listView, 0, UI::UIListViewScrollAlignment::Start);
        !status) {
        return status;
    }
    listSelectionKey_ = ListItemKeyBase + 2;

    if (Core::Status status = tree.setTreeViewItemExpanded(nodes_.treeView, 0, true); !status) {
        return status;
    }
    if (Core::Status status = tree.setTreeViewItemExpanded(nodes_.treeView, 2, true); !status) {
        return status;
    }
    if (Core::Status status = tree.setTreeViewItemExpanded(nodes_.treeView, 6, false); !status) {
        return status;
    }
    if (Core::Status status = tree.setTreeViewSelectedIndex(nodes_.treeView, 0); !status) {
        return status;
    }
    if (Core::Status status = tree.scrollTreeViewToIndex(nodes_.treeView, 0, UI::UITreeViewScrollAlignment::Start);
        !status) {
        return status;
    }
    treeSelectionKey_ = WorldTreeItemKey;

    if (Core::Status status = tree.setScrollViewOffset(nodes_.scrollView, {}); !status) {
        return status;
    }
    scrollOffset_ = 0.0F;
    pendingStatus_ = StatusMessage::Reset;
    statusDirty_ = true;
    return Core::success();
}

Core::Status ShowcaseUI::publishStatus(PrimaryWindowUITreeUpdater& tree, StatusMessage message)
{
    std::string_view text = "Ready · all controls are live";
    switch (message) {
    case StatusMessage::Ready:
        break;
    case StatusMessage::PrimaryAction:
        text = "Primary action committed";
        break;
    case StatusMessage::DestructiveAction:
        text = "Destructive action previewed";
        break;
    case StatusMessage::Reset:
        text = "Control state reset";
        break;
    case StatusMessage::NotificationsEnabled:
        text = "Notifications enabled";
        break;
    case StatusMessage::NotificationsDisabled:
        text = "Notifications disabled";
        break;
    case StatusMessage::ThemeDark:
        text = "Dark theme applied to the retained tree";
        break;
    case StatusMessage::ThemeLight:
        text = "Light theme applied to the retained tree";
        break;
    case StatusMessage::QualityPerformance:
        text = "Render profile: Performance";
        break;
    case StatusMessage::QualityBalanced:
        text = "Render profile: Balanced";
        break;
    case StatusMessage::QualityQuality:
        text = "Render profile: Quality";
        break;
    case StatusMessage::ProgressChanged:
        text = "Progress synchronized from Slider";
        break;
    case StatusMessage::DropdownChanged:
        text = "Dropdown selection updated";
        break;
    case StatusMessage::ListSelectionChanged:
        text = "ListView selection updated";
        break;
    case StatusMessage::TreeSelectionChanged:
        text = "TreeView selection updated";
        break;
    case StatusMessage::TreeExpansionChanged:
        text = "TreeView projection expanded or collapsed";
        break;
    case StatusMessage::ScrollChanged:
        text = "ScrollView offset updated";
        break;
    }
    if (Core::Status status = tree.setText(nodes_.statusLabel, text); !status) {
        return status;
    }
    pendingStatus_ = message;
    statusDirty_ = false;
    return Core::success();
}

Core::Status ShowcaseUI::update(UIUpdateContext& context)
{
    if (!root_ || !context.hasPrimaryWindowUI()) {
        return Core::success();
    }
    auto tree = context.primaryWindowUITreeUpdater(root_);
    if (!tree) {
        return Core::failure(std::move(tree.error()));
    }

    if (resetRequested_) {
        resetRequested_ = false;
        if (Core::Status status = applyReset(*tree); !status) {
            return status;
        }
    }
    if (requestedSliderValue_.has_value()) {
        const float value = *requestedSliderValue_;
        requestedSliderValue_.reset();
        if (Core::Status status = tree->setSliderValue(nodes_.progressSlider, value); !status) {
            return status;
        }
    }
    if (requestedListSelection_.has_value()) {
        const Core::u64 logicalIndex = *requestedListSelection_;
        requestedListSelection_.reset();
        if (Core::Status status = tree->setListViewSelectedIndex(nodes_.listView, logicalIndex); !status) {
            return status;
        }
        listSelectionKey_ = ListItemKeyBase + logicalIndex;
        pendingStatus_ = StatusMessage::ListSelectionChanged;
        statusDirty_ = true;
    }
    if (requestedWorldExpansion_.has_value()) {
        const bool expanded = *requestedWorldExpansion_;
        requestedWorldExpansion_.reset();
        if (Core::Status status = tree->setTreeViewItemExpanded(nodes_.treeView, 0, expanded); !status) {
            return status;
        }
    }
    if (requestedTreeSelection_.has_value()) {
        const Core::u64 logicalIndex = *requestedTreeSelection_;
        requestedTreeSelection_.reset();
        if (Core::Status status = tree->setTreeViewSelectedIndex(nodes_.treeView, logicalIndex); !status) {
            return status;
        }
        auto selection = tree->treeViewSelection(nodes_.treeView);
        if (!selection) {
            return Core::failure(std::move(selection.error()));
        }
        treeSelectionKey_ = selection->key;
        pendingStatus_ = StatusMessage::TreeSelectionChanged;
        statusDirty_ = true;
    }
    if (requestedDropdownSelection_.has_value()) {
        const Core::usize index = *requestedDropdownSelection_;
        requestedDropdownSelection_.reset();
        if (index >= nodes_.dropdownItems.size()) {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "UI showcase requested an invalid Dropdown item");
        }
        if (Core::Status status = tree->setDropdownSelectedItem(nodes_.dropdown, nodes_.dropdownItems[index]);
            !status) {
            return status;
        }
        if (Core::Status status = tree->setText(nodes_.dropdown, DropdownItemLabels[index]); !status) {
            return status;
        }
        dropdownSelection_ = nodes_.dropdownItems[index];
        dropdownSelectionIndex_ = index;
        pendingStatus_ = StatusMessage::DropdownChanged;
        statusDirty_ = true;
    }
    if (requestedDropdownOpen_.has_value()) {
        const bool open = *requestedDropdownOpen_;
        requestedDropdownOpen_.reset();
        if (Core::Status status = tree->setDropdownOpen(nodes_.dropdown, open); !status) {
            return status;
        }
    }
    if (requestedScrollOffset_.has_value()) {
        const float offset = *requestedScrollOffset_;
        requestedScrollOffset_.reset();
        if (Core::Status status = tree->setScrollViewOffset(nodes_.scrollView, {.x = 0.0F, .y = offset}); !status) {
            return status;
        }
        scrollOffset_ = offset;
        pendingStatus_ = StatusMessage::ScrollChanged;
        statusDirty_ = true;
    }
    if (requestedTheme_.has_value()) {
        const ShowcaseTheme theme = *requestedTheme_;
        requestedTheme_.reset();
        if (Core::Status status = applyTheme(*tree, theme, true); !status) {
            return status;
        }
    }
    if (progressDirty_) {
        if (Core::Status status = applyProgress(*tree, requestedProgressValue_); !status) {
            return status;
        }
    }
    if (notificationsDirty_) {
        notificationsDirty_ = false;
        auto checked = tree->isChecked(nodes_.notificationsCheckbox);
        if (!checked) {
            return Core::failure(std::move(checked.error()));
        }
        notificationsEnabled_ = *checked;
        pendingStatus_ =
            notificationsEnabled_ ? StatusMessage::NotificationsEnabled : StatusMessage::NotificationsDisabled;
        statusDirty_ = true;
    }
    auto listSelection = tree->listViewSelection(nodes_.listView);
    if (!listSelection) {
        return Core::failure(std::move(listSelection.error()));
    }
    if (listSelection->key != listSelectionKey_) {
        listSelectionKey_ = listSelection->key;
        pendingStatus_ = StatusMessage::ListSelectionChanged;
        statusDirty_ = true;
    }
    auto treeSelection = tree->treeViewSelection(nodes_.treeView);
    if (!treeSelection) {
        return Core::failure(std::move(treeSelection.error()));
    }
    if (treeSelection->key != treeSelectionKey_) {
        treeSelectionKey_ = treeSelection->key;
        pendingStatus_ = StatusMessage::TreeSelectionChanged;
        statusDirty_ = true;
    }
    auto dropdownSelection = tree->dropdownSelectedItem(nodes_.dropdown);
    if (!dropdownSelection) {
        return Core::failure(std::move(dropdownSelection.error()));
    }
    if (*dropdownSelection != dropdownSelection_) {
        for (Core::usize index = 0; index < nodes_.dropdownItems.size(); ++index) {
            if (nodes_.dropdownItems[index] == *dropdownSelection) {
                if (Core::Status status = tree->setText(nodes_.dropdown, DropdownItemLabels[index]); !status) {
                    return status;
                }
                dropdownSelectionIndex_ = index;
                break;
            }
        }
        dropdownSelection_ = *dropdownSelection;
        pendingStatus_ = StatusMessage::DropdownChanged;
        statusDirty_ = true;
    }
    auto scrollOffset = tree->scrollViewOffset(nodes_.scrollView);
    if (!scrollOffset) {
        return Core::failure(std::move(scrollOffset.error()));
    }
    if (scrollOffset->y != scrollOffset_) {
        scrollOffset_ = scrollOffset->y;
        pendingStatus_ = StatusMessage::ScrollChanged;
        statusDirty_ = true;
    }
    if (treeExpansionDirty_) {
        treeExpansionDirty_ = false;
        pendingStatus_ = StatusMessage::TreeExpansionChanged;
        statusDirty_ = true;
    }
    if (statusDirty_) {
        if (Core::Status status = publishStatus(*tree, pendingStatus_); !status) {
            return status;
        }
    }
    return Core::success();
}

void ShowcaseUI::requestAutomatedStep(Core::u64 frameIndex) noexcept
{
    if (frameIndex == 15) {
        requestedListSelection_ = 7;
    } else if (frameIndex == 20) {
        requestedTreeSelection_ = 3;
    } else if (frameIndex == 25) {
        requestedDropdownOpen_ = true;
    } else if (frameIndex == 30) {
        requestedTheme_ = initialTheme_ == ShowcaseTheme::Dark ? ShowcaseTheme::Light : ShowcaseTheme::Dark;
    } else if (frameIndex == 40) {
        requestedDropdownSelection_ = 1;
    } else if (frameIndex == 45) {
        requestedDropdownOpen_ = false;
    } else if (frameIndex == 50) {
        requestedWorldExpansion_ = false;
    } else if (frameIndex == 60) {
        requestedSliderValue_ = 84.0F;
    } else if (frameIndex == 70) {
        requestedWorldExpansion_ = true;
    } else if (frameIndex == 75) {
        requestedScrollOffset_ = 80.0F;
    } else if (frameIndex == 80) {
        requestedTreeSelection_ = 3;
    } else if (frameIndex == 90) {
        requestedTheme_ = initialTheme_;
    }
}

bool ShowcaseUI::unbindImageResolver() noexcept
{
    if (!imageResolver_) {
        return false;
    }
    imageResolver_.reset();
    return true;
}

void ShowcaseUI::release() noexcept
{
    static_cast<void>(unbindImageResolver());
    root_.reset();
}

ShowcaseUISnapshot ShowcaseUI::snapshot() const noexcept
{
    return ShowcaseUISnapshot{
        .theme = currentTheme_,
        .progressValue = progressValue_,
        .themeSwitches = themeSwitches_,
        .buttonActivations = buttonActivations_,
        .sliderChanges = sliderChanges_,
        .treeExpansionChanges = treeExpansionChanges_,
        .styleTokenUpdates = styleTokenUpdates_,
        .listSelectionKey = listSelectionKey_,
        .treeSelectionKey = treeSelectionKey_,
        .dropdownSelection = dropdownSelectionIndex_,
        .scrollOffset = scrollOffset_,
        .controlCount = controlCount_,
        .imageProductCount = imageProductCount_,
        .quality = quality_,
        .notificationsEnabled = notificationsEnabled_,
        .stylesheetInstalled = stylesheetInstalled_,
        .rootAlive = static_cast<bool>(root_),
    };
}

} // namespace Tina::SampleUI
