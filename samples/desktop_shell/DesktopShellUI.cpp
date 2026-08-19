#include "DesktopShellUI.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <utility>

namespace Tina::SampleUI {
namespace {

// Frozen shell defaults from docs/ui-modern-desktop.md "默认尺寸".
inline constexpr float LeftDockDefaultWidth = 272.0F;
inline constexpr float LeftDockMinWidth = 200.0F;
inline constexpr float InspectorDefaultWidth = 320.0F;
inline constexpr float InspectorMinWidth = 240.0F;
inline constexpr float TimelineDefaultHeight = 220.0F;
inline constexpr float TimelineMinHeight = 160.0F;
inline constexpr float ViewportMinWidth = 480.0F;
inline constexpr float ViewportMinHeight = 320.0F;

// Responsive breakpoints. Below these the shell collapses panes instead of
// shrinking type or letting panes overlap.
inline constexpr float FullTierMinWidth = 1280.0F;
inline constexpr float CompressedTierMinWidth = 960.0F;

[[nodiscard]] UI::UILayoutStyle fillStyle() noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Percent(100.0F);
    style.size.height = UI::UILayoutLength::Percent(100.0F);
    return style;
}

[[nodiscard]] UI::UILayoutStyle fillWidthStyle(float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Percent(100.0F);
    style.size.height = UI::UILayoutLength::Px(height);
    style.flexItem.shrink = 0.0F;
    return style;
}

// A pane that consumes the remaining main-axis space.
[[nodiscard]] UI::UILayoutStyle growingStyle() noexcept
{
    UI::UILayoutStyle style{};
    style.flexItem.grow = 1.0F;
    style.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    style.size.width = UI::UILayoutLength::Percent(100.0F);
    style.size.height = UI::UILayoutLength::Percent(100.0F);
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

// Surface profile selects product chrome without adding behaviour or state.
// Plain = transparent structural container, Filled = band/dock surface,
// Elevated = raised surface.
[[nodiscard]] Core::Result<UI::UINodeId> createSurface(PrimaryWindowUITreeUpdater& tree,
                                                       UI::UINodeId parent,
                                                       const UI::UILayoutStyle& layout,
                                                       UI::UISurfaceVariant variant)
{
    return tree.createElement(parent, UI::makeSurfaceElement({.variant = variant}, layout));
}

[[nodiscard]] Core::Result<UI::UINodeId> createDivider(PrimaryWindowUITreeUpdater& tree,
                                                       UI::UINodeId parent,
                                                       const UI::UILayoutStyle& layout)
{
    return tree.createElement(parent, UI::makeDividerElement({}, layout));
}

[[nodiscard]] UI::UIEdgeSpacing horizontalPadding(float logicalPixels) noexcept
{
    return UI::UIEdgeSpacing::HorizontalVertical(logicalPixels, 0.0F);
}

[[nodiscard]] Core::Result<UI::UINodeId> createLabel(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
                                                     const UI::UILayoutStyle& layout, std::string_view text)
{
    return tree.createElement(parent, UI::makeLabelElement(text, layout));
}

[[nodiscard]] Core::Result<UI::UINodeId> createButton(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
                                                      const UI::UILayoutStyle& layout, std::string_view text,
                                                      UI::UIStyleRoleId role = UI::UIStyleRoleId::ButtonTonal)
{
    UI::UIElementDescriptor descriptor = UI::makeButtonElement(text, layout);
    descriptor.visual.styleRole = role;
    return tree.createElement(parent, descriptor);
}

[[nodiscard]] UI::UITheme themeFor(ShellTheme theme, UI::UIDensity density) noexcept
{
    return theme == ShellTheme::Dark
               ? UI::makeModernDesktopTheme(UI::UIColorScheme::Dark, density)
               : UI::makeModernDesktopTheme(UI::UIColorScheme::Light, density);
}

[[nodiscard]] ShellResponsiveTier tierForWidth(float logicalWidth) noexcept
{
    if (logicalWidth >= FullTierMinWidth) {
        return ShellResponsiveTier::Full;
    }
    if (logicalWidth >= CompressedTierMinWidth) {
        return ShellResponsiveTier::Compressed;
    }
    return ShellResponsiveTier::Minimal;
}

// Converts an absolute pane size into the SplitView primary fraction. The
// SplitView still clamps against its own minimums; this only expresses intent.
[[nodiscard]] float fractionForPrimary(float primarySize, float totalSize) noexcept
{
    if (!std::isfinite(primarySize) || !std::isfinite(totalSize) || totalSize <= 0.0F) {
        return 0.5F;
    }
    return std::clamp(primarySize / totalSize, 0.05F, 0.95F);
}

[[nodiscard]] float fractionForSecondary(float secondarySize, float totalSize) noexcept
{
    if (!std::isfinite(secondarySize) || !std::isfinite(totalSize) || totalSize <= 0.0F) {
        return 0.5F;
    }
    return std::clamp(1.0F - (secondarySize / totalSize), 0.05F, 0.95F);
}

struct ShellCommand final {
    std::string_view label;
    UI::UIStyleRoleId role;
};

// Hierarchy rows. Two collapsible groups keep the projection small enough to
// reason about while still exercising expand/collapse and indentation.
struct HierarchyRow final {
    std::string_view label;
    u32 level;
    bool expandable;
};

inline constexpr std::array<HierarchyRow, 3> HierarchyRoots{{
    {"World", 0, true},
    {"Lighting", 0, true},
    {"PostProcess", 0, false},
}};

inline constexpr std::array<std::string_view, 3> WorldChildren{{
    "Terrain",
    "Player",
    "Props",
}};

inline constexpr std::array<std::string_view, 2> LightingChildren{{
    "Sun",
    "SkyProbe",
}};

inline constexpr std::array<std::string_view, 5> AssetRows{{
    "materials/",
    "meshes/",
    "scripts/",
    "textures/",
    "audio/",
}};

inline constexpr UI::UITreeViewItemKey HierarchyKeyBase = 100;
inline constexpr UI::UIListViewItemKey AssetKeyBase = 200;

// One Primary per cluster; destructive commands use Danger. Icon-only tools are
// out of this slice because they require Tooltip anchors per icon.
inline constexpr std::array<ShellCommand, 5> PrimaryCommands{{
    {"Play", UI::UIStyleRoleId::ButtonPrimary},
    {"Save", UI::UIStyleRoleId::ButtonOutlined},
    {"Undo", UI::UIStyleRoleId::ButtonText},
    {"Redo", UI::UIStyleRoleId::ButtonText},
    {"Delete", UI::UIStyleRoleId::ButtonDanger},
}};

inline constexpr std::array<std::string_view, 3> DocumentLabels{{
    "Scene.tscn",
    "Standard.mat",
    "Player.lua",
}};

inline constexpr std::array<std::string_view, 4> InspectorRows{{
    "Transform",
    "Renderer",
    "Collider",
    "Script",
}};

} // namespace

Core::Status DesktopShellUI::build(GameStateEnterContext& context, DesktopShellState& state)
{
    if (root_) {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Desktop shell root is already built");
    }

    auto rootBuilder = context.primaryWindowUIRootBuilder();
    if (!rootBuilder) {
        return Core::failure(std::move(rootBuilder.error()));
    }

    state_ = &state;
    currentTheme_ = state.theme;
    density_ = state.density;
    initialDensity_ = state.density;

    const UI::UITheme theme = themeFor(state.theme, state.density);
    // Density binds at root construction, so the complete theme is staged
    // before createRoot() publishes the first retained node.
    if (Core::Status status = rootBuilder->setProductTheme(theme); !status) {
        return status;
    }

    if (!state.styleRegistered) {
        auto accentClass = rootBuilder->registerStyleClass();
        if (!accentClass) {
            return Core::failure(std::move(accentClass.error()));
        }
        auto accentToken = rootBuilder->registerStyleColorToken(theme.colors.primary);
        if (!accentToken) {
            return Core::failure(std::move(accentToken.error()));
        }
        const std::array sheetRules{
            UI::UIStyleBoxFillRule{
                .role = UI::UIStyleRoleId::PanelSurface,
                .styleClass = *accentClass,
                .colorToken = *accentToken,
            },
        };
        if (Core::Status status = rootBuilder->installStyleSheet(std::span(sheetRules)); !status) {
            return status;
        }
        state.accentClass = *accentClass;
        state.accentToken = *accentToken;
        state.styleRegistered = true;
    }

    auto root = rootBuilder->createRoot();
    if (!root) {
        return Core::failure(std::move(root.error()));
    }
    auto tree = rootBuilder->treeUpdater(*root);
    if (!tree) {
        return Core::failure(std::move(tree.error()));
    }

    if (Core::Status status = tree->setLayoutStyle(root->rootNodeId(), fillStyle()); !status) {
        return status;
    }

    // Root background band stack: Command Bar, Tabs, Context Toolbar,
    // Workspace, Status Bar. Overlays anchor into this same root.
    UI::UILayoutStyle backgroundLayout = fillStyle();
    backgroundLayout.flexContainer.direction = UI::UIFlexDirection::Column;
    backgroundLayout.flexContainer.gap = UI::UILayoutGap::All(0.0F);
    if (Core::Status status =
            storeNode(createSurface(*tree, root->rootNodeId(), backgroundLayout,
                                    UI::UISurfaceVariant::Plain),
                      nodes_.background);
        !status) {
        return status;
    }

    if (Core::Status status = buildCommandBar(*tree, theme); !status) {
        return status;
    }
    if (Core::Status status = buildDocumentTabs(*tree, theme); !status) {
        return status;
    }
    if (Core::Status status = buildContextToolbar(*tree, theme); !status) {
        return status;
    }
    if (Core::Status status = buildWorkspace(*tree, theme); !status) {
        return status;
    }
    if (Core::Status status = buildStatusBar(*tree, theme); !status) {
        return status;
    }

    root_.emplace(std::move(*root));
    layoutDirty_ = true;
    statusDirty_ = true;
    return Core::success();
}

UI::UIListViewDataSource DesktopShellUI::listDataSource() const noexcept
{
    return UI::UIListViewDataSource{
        .state = this,
        .itemCount = &DesktopShellUI::assetItemCount,
        .resolveItem = &DesktopShellUI::resolveAssetItem,
    };
}

UI::UITreeViewDataSource DesktopShellUI::treeDataSource() noexcept
{
    return UI::UITreeViewDataSource{
        .state = this,
        .itemCount = &DesktopShellUI::hierarchyItemCount,
        .resolveItem = &DesktopShellUI::resolveHierarchyItem,
        .setItemExpanded = &DesktopShellUI::setHierarchyItemExpanded,
    };
}

u64 DesktopShellUI::assetItemCount(const void* state) noexcept
{
    return state != nullptr ? AssetRows.size() : 0;
}

bool DesktopShellUI::resolveAssetItem(const void* state, u64 logicalIndex,
                                      UI::UIListViewItemDescriptor& output) noexcept
{
    if (state == nullptr || logicalIndex >= AssetRows.size()) {
        return false;
    }
    output = UI::UIListViewItemDescriptor{
        .key = AssetKeyBase + logicalIndex,
        .label = AssetRows[static_cast<Core::usize>(logicalIndex)],
        .enabled = true,
    };
    return true;
}

// Visible depth-first projection: World(+3) then Lighting(+2) then PostProcess.
u64 DesktopShellUI::hierarchyItemCount(const void* state) noexcept
{
    if (state == nullptr) {
        return 0;
    }
    const auto& shell = *static_cast<const DesktopShellUI*>(state);
    u64 count = HierarchyRoots.size();
    if (shell.state_ == nullptr) {
        return count;
    }
    if (shell.state_->worldExpanded) {
        count += WorldChildren.size();
    }
    if (shell.state_->lightingExpanded) {
        count += LightingChildren.size();
    }
    return count;
}

bool DesktopShellUI::resolveHierarchyItem(const void* state, u64 logicalIndex,
                                          UI::UITreeViewItemDescriptor& output) noexcept
{
    if (state == nullptr) {
        return false;
    }
    const auto& shell = *static_cast<const DesktopShellUI*>(state);
    if (shell.state_ == nullptr || logicalIndex >= hierarchyItemCount(state)) {
        return false;
    }
    const bool worldExpanded = shell.state_->worldExpanded;
    const bool lightingExpanded = shell.state_->lightingExpanded;

    u64 cursor = 0;
    // World root.
    if (logicalIndex == cursor) {
        output = UI::UITreeViewItemDescriptor{
            .key = HierarchyKeyBase,
            .label = HierarchyRoots[0].label,
            .level = 0,
            .enabled = true,
            .expandable = true,
            .expanded = worldExpanded,
        };
        return true;
    }
    ++cursor;
    if (worldExpanded) {
        if (logicalIndex < cursor + WorldChildren.size()) {
            const auto child = static_cast<Core::usize>(logicalIndex - cursor);
            output = UI::UITreeViewItemDescriptor{
                .key = HierarchyKeyBase + 1 + logicalIndex - cursor,
                .label = WorldChildren[child],
                .level = 1,
                .enabled = true,
                .expandable = false,
                .expanded = false,
            };
            return true;
        }
        cursor += WorldChildren.size();
    }

    // Lighting root.
    if (logicalIndex == cursor) {
        output = UI::UITreeViewItemDescriptor{
            .key = HierarchyKeyBase + 10,
            .label = HierarchyRoots[1].label,
            .level = 0,
            .enabled = true,
            .expandable = true,
            .expanded = lightingExpanded,
        };
        return true;
    }
    ++cursor;
    if (lightingExpanded) {
        if (logicalIndex < cursor + LightingChildren.size()) {
            const auto child = static_cast<Core::usize>(logicalIndex - cursor);
            output = UI::UITreeViewItemDescriptor{
                .key = HierarchyKeyBase + 11 + logicalIndex - cursor,
                .label = LightingChildren[child],
                .level = 1,
                .enabled = true,
                .expandable = false,
                .expanded = false,
            };
            return true;
        }
        cursor += LightingChildren.size();
    }

    // PostProcess root.
    if (logicalIndex == cursor) {
        output = UI::UITreeViewItemDescriptor{
            .key = HierarchyKeyBase + 20,
            .label = HierarchyRoots[2].label,
            .level = 0,
            .enabled = true,
            .expandable = false,
            .expanded = false,
        };
        return true;
    }
    return false;
}

bool DesktopShellUI::setHierarchyItemExpanded(void* state, u64 logicalIndex, bool expanded) noexcept
{
    if (state == nullptr) {
        return false;
    }
    auto& shell = *static_cast<DesktopShellUI*>(state);
    if (shell.state_ == nullptr) {
        return false;
    }
    // Only the two group roots are expandable, and their indices depend on the
    // current projection. Reject anything else so the projection stays stable.
    if (logicalIndex == 0) {
        shell.state_->worldExpanded = expanded;
        return true;
    }
    const u64 lightingIndex =
        1 + (shell.state_->worldExpanded ? WorldChildren.size() : 0);
    if (logicalIndex == lightingIndex) {
        shell.state_->lightingExpanded = expanded;
        return true;
    }
    return false;
}

Core::Status DesktopShellUI::buildCommandBar(PrimaryWindowUITreeUpdater& tree, const UI::UITheme& theme)
{
    UI::UILayoutStyle barLayout = fillWidthStyle(theme.controls.commandBarHeight);
    barLayout.padding =
        UI::UIEdgeSpacing::HorizontalVertical(theme.spacing.space5, theme.spacing.space2);
    barLayout.flexContainer.direction = UI::UIFlexDirection::Row;
    barLayout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    barLayout.flexContainer.gap.column = theme.spacing.space4;
    if (Core::Status status =
            storeNode(createSurface(tree, nodes_.background, barLayout, UI::UISurfaceVariant::Filled),
                      nodes_.commandBar);
        !status) {
        return status;
    }
    ++bandCount_;

    // Stylesheet-driven accent rule: colour comes from the registered token, so
    // a theme switch updates it through setStyleColorToken, not setBoxPaint.
    UI::UILayoutStyle accentLayout{};
    accentLayout.size.width = UI::UILayoutLength::Px(3.0F);
    accentLayout.size.height = UI::UILayoutLength::Percent(60.0F);
    accentLayout.flexItem.shrink = 0.0F;
    UI::UIElementDescriptor accentDesc = UI::makePanelElement(accentLayout);
    accentDesc.visual.styleRole = UI::UIStyleRoleId::PanelSurface;
    accentDesc.visual.styleClasses = std::span(&state_->accentClass, 1);
    if (Core::Status status = storeNode(tree.createElement(nodes_.commandBar, accentDesc),
                                        nodes_.accentRule);
        !status) {
        return status;
    }

    UI::UILayoutStyle identityLayout{};
    identityLayout.flexItem.shrink = 0.0F;
    if (Core::Status status =
            storeNode(createLabel(tree, nodes_.commandBar, identityLayout, "Tina Desktop Shell"),
                      nodes_.brandLabel);
        !status) {
        return status;
    }

    // Command cluster. A Divider separates it from the identity group rather
    // than wrapping each command in its own card.
    UI::UILayoutStyle dividerLayout{};
    dividerLayout.size.width = UI::UILayoutLength::Px(1.0F);
    dividerLayout.size.height = UI::UILayoutLength::Percent(55.0F);
    dividerLayout.flexItem.shrink = 0.0F;
    dividerLayout.margin = horizontalPadding(theme.spacing.space2);
    UI::UINodeId divider{};
    if (Core::Status status = storeNode(
            createDivider(tree, nodes_.commandBar, dividerLayout), divider);
        !status) {
        return status;
    }

    for (const ShellCommand& command : PrimaryCommands) {
        UI::UILayoutStyle buttonLayout{};
        buttonLayout.size.height = UI::UILayoutLength::Px(theme.controls.buttonHeight);
        buttonLayout.flexItem.shrink = 0.0F;
        buttonLayout.padding = horizontalPadding(theme.spacing.space4);
        UI::UINodeId button{};
        if (Core::Status status = storeNode(
                createButton(tree, nodes_.commandBar, buttonLayout, command.label, command.role), button);
            !status) {
            return status;
        }
        ++commandCount_;
    }

    // Spacer pushes the window/application cluster to the trailing edge.
    UI::UILayoutStyle spacerLayout{};
    spacerLayout.flexItem.grow = 1.0F;
    spacerLayout.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    UI::UINodeId spacer{};
    if (Core::Status status =
            storeNode(createSurface(tree, nodes_.commandBar, spacerLayout, UI::UISurfaceVariant::Plain),
                      spacer);
        !status) {
        return status;
    }

    UI::UILayoutStyle menuLayout{};
    menuLayout.size.height = UI::UILayoutLength::Px(theme.controls.buttonHeight);
    menuLayout.flexItem.shrink = 0.0F;
    menuLayout.padding = horizontalPadding(theme.spacing.space4);
    if (Core::Status status = storeNode(
            createButton(tree, nodes_.commandBar, menuLayout, "View", UI::UIStyleRoleId::ButtonText),
            nodes_.menuAnchor);
        !status) {
        return status;
    }
    ++commandCount_;
    return Core::success();
}

Core::Status DesktopShellUI::buildDocumentTabs(PrimaryWindowUITreeUpdater& tree, const UI::UITheme& theme)
{
    UI::UILayoutStyle stripLayout = fillWidthStyle(theme.controls.tabHeight);
    stripLayout.padding = horizontalPadding(theme.spacing.space3);
    stripLayout.flexContainer.direction = UI::UIFlexDirection::Row;
    stripLayout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    stripLayout.flexContainer.gap.column = theme.spacing.space1;
    if (Core::Status status = storeNode(
            createSurface(tree, nodes_.background, stripLayout, UI::UISurfaceVariant::Filled),
            nodes_.documentTabs);
        !status) {
        return status;
    }
    ++bandCount_;

    const auto activeIndex = static_cast<usize>(state_->activeDocument);
    for (usize index = 0; index < DocumentLabels.size(); ++index) {
        UI::UILayoutStyle tabLayout{};
        tabLayout.size.height = UI::UILayoutLength::Px(theme.controls.tabHeight);
        tabLayout.flexItem.shrink = 0.0F;
        tabLayout.padding = horizontalPadding(theme.spacing.space4);
        // Selected tab uses a RadioButton role so the shell reuses the existing
        // mutually-exclusive selection behaviour instead of a parallel state.
        UI::UIElementDescriptor tabDesc =
            UI::makeRadioButtonElement(DocumentLabels[index], tabLayout);
        tabDesc.semantics.name = DocumentLabels[index];
        auto tab = tree.createElement(nodes_.documentTabs, tabDesc);
        if (!tab) {
            return Core::failure(std::move(tab.error()));
        }
        if (Core::Status status =
                tree.setRadioButtonSelected(*tab, index == activeIndex);
            !status) {
            return status;
        }
        nodes_.documentTabButtons[index] = *tab;
    }
    return Core::success();
}

Core::Status DesktopShellUI::buildContextToolbar(PrimaryWindowUITreeUpdater& tree, const UI::UITheme& theme)
{
    UI::UILayoutStyle toolbarLayout = fillWidthStyle(theme.controls.contextToolbarHeight);
    toolbarLayout.padding =
        UI::UIEdgeSpacing::HorizontalVertical(theme.spacing.space5, theme.spacing.space1);
    toolbarLayout.flexContainer.direction = UI::UIFlexDirection::Row;
    toolbarLayout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    toolbarLayout.flexContainer.gap.column = theme.spacing.space5;
    if (Core::Status status = storeNode(
            createSurface(tree, nodes_.background, toolbarLayout, UI::UISurfaceVariant::Filled),
            nodes_.contextToolbar);
        !status) {
        return status;
    }
    ++bandCount_;

    // Zoom/grid/camera information lives here, never over the scene.
    UI::UILayoutStyle captionLayout{};
    captionLayout.flexItem.shrink = 0.0F;
    UI::UINodeId zoomLabel{};
    if (Core::Status status =
            storeNode(createLabel(tree, nodes_.contextToolbar, captionLayout, "Zoom 100%"), zoomLabel);
        !status) {
        return status;
    }
    UI::UINodeId gridLabel{};
    if (Core::Status status =
            storeNode(createLabel(tree, nodes_.contextToolbar, captionLayout, "Grid 1.0 m"), gridLabel);
        !status) {
        return status;
    }
    if (Core::Status status = storeNode(
            createLabel(tree, nodes_.contextToolbar, captionLayout, "Camera Perspective"),
            nodes_.cameraLabel);
        !status) {
        return status;
    }
    return Core::success();
}

// Workspace = SplitView A (left dock | main), where main is SplitView B
// (center | inspector) and center is SplitView C (viewport | timeline).
// Three nested SplitViews express the whole dock layout; no Dock runtime.
Core::Status DesktopShellUI::buildWorkspace(PrimaryWindowUITreeUpdater& tree, const UI::UITheme& theme)
{
    UI::UILayoutStyle workspaceLayout = growingStyle();
    if (Core::Status status = storeNode(
            createSurface(tree, nodes_.background, workspaceLayout, UI::UISurfaceVariant::Plain),
            nodes_.workspace);
        !status) {
        return status;
    }
    ++bandCount_;

    const float splitterHit = theme.controls.splitterHitExtent;

    // SplitView A: left dock is primary, main area is secondary.
    const UI::UISplitViewConfig configA{
        .orientation = UI::UISplitViewOrientation::Horizontal,
        .initialFraction = 0.22F,
        .minPrimarySize = LeftDockMinWidth,
        .minSecondarySize = ViewportMinWidth + InspectorMinWidth,
        .splitterExtent = splitterHit,
    };
    if (Core::Status status = storeNode(
            tree.createElement(nodes_.workspace, UI::makeSplitViewElement(configA, fillStyle())),
            nodes_.splitA);
        !status) {
        return status;
    }
    ++splitViewCount_;

    // Left Dock is a full-height surface band, not a floating card.
    UI::UILayoutStyle dockLayout = fillStyle();
    dockLayout.flexContainer.direction = UI::UIFlexDirection::Column;
    dockLayout.padding = UI::UIEdgeSpacing::All(theme.spacing.space4);
    dockLayout.flexContainer.gap.row = theme.spacing.space4;
    if (Core::Status status = storeNode(
            createSurface(tree, nodes_.splitA, dockLayout, UI::UISurfaceVariant::Filled),
            nodes_.leftDock);
        !status) {
        return status;
    }
    if (Core::Status status = storeNode(
            tree.createElement(nodes_.splitA, UI::makeSplitterElement({})), nodes_.splitterA);
        !status) {
        return status;
    }
    UI::UILayoutStyle mainLayout = fillStyle();
    if (Core::Status status = storeNode(
            createSurface(tree, nodes_.splitA, mainLayout, UI::UISurfaceVariant::Plain), nodes_.main);
        !status) {
        return status;
    }
    if (Core::Status status =
            tree.setSplitViewParts(nodes_.splitA, nodes_.leftDock, nodes_.splitterA, nodes_.main);
        !status) {
        return status;
    }

    // SplitView B: center is primary, Inspector is secondary.
    const UI::UISplitViewConfig configB{
        .orientation = UI::UISplitViewOrientation::Horizontal,
        .initialFraction = 0.74F,
        .minPrimarySize = ViewportMinWidth,
        .minSecondarySize = InspectorMinWidth,
        .splitterExtent = splitterHit,
    };
    if (Core::Status status = storeNode(
            tree.createElement(nodes_.main, UI::makeSplitViewElement(configB, fillStyle())),
            nodes_.splitB);
        !status) {
        return status;
    }
    ++splitViewCount_;

    UI::UILayoutStyle centerLayout = fillStyle();
    if (Core::Status status = storeNode(
            createSurface(tree, nodes_.splitB, centerLayout, UI::UISurfaceVariant::Plain),
            nodes_.center);
        !status) {
        return status;
    }
    if (Core::Status status = storeNode(
            tree.createElement(nodes_.splitB, UI::makeSplitterElement({})), nodes_.splitterB);
        !status) {
        return status;
    }
    UI::UILayoutStyle inspectorLayout = fillStyle();
    inspectorLayout.flexContainer.direction = UI::UIFlexDirection::Column;
    inspectorLayout.padding = UI::UIEdgeSpacing::All(theme.spacing.space4);
    inspectorLayout.flexContainer.gap.row = theme.spacing.space3;
    if (Core::Status status = storeNode(
            createSurface(tree, nodes_.splitB, inspectorLayout, UI::UISurfaceVariant::Filled),
            nodes_.inspector);
        !status) {
        return status;
    }
    if (Core::Status status =
            tree.setSplitViewParts(nodes_.splitB, nodes_.center, nodes_.splitterB, nodes_.inspector);
        !status) {
        return status;
    }

    // SplitView C: viewport is primary, timeline is secondary.
    const UI::UISplitViewConfig configC{
        .orientation = UI::UISplitViewOrientation::Vertical,
        .initialFraction = 0.68F,
        .minPrimarySize = ViewportMinHeight,
        .minSecondarySize = TimelineMinHeight,
        .splitterExtent = splitterHit,
    };
    if (Core::Status status = storeNode(
            tree.createElement(nodes_.center, UI::makeSplitViewElement(configC, fillStyle())),
            nodes_.splitC);
        !status) {
        return status;
    }
    ++splitViewCount_;

    // Viewport uses the Sunken elevation and is never wrapped in a card.
    UI::UILayoutStyle viewportLayout = fillStyle();
    viewportLayout.flexContainer.direction = UI::UIFlexDirection::Column;
    viewportLayout.flexContainer.justifyContent = UI::UIJustifyContent::Center;
    viewportLayout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    if (Core::Status status = storeNode(
            createSurface(tree, nodes_.splitC, viewportLayout, UI::UISurfaceVariant::Plain),
            nodes_.viewport);
        !status) {
        return status;
    }
    if (Core::Status status = storeNode(
            tree.createElement(nodes_.splitC, UI::makeSplitterElement({})), nodes_.splitterC);
        !status) {
        return status;
    }
    UI::UILayoutStyle timelineLayout = fillStyle();
    timelineLayout.flexContainer.direction = UI::UIFlexDirection::Column;
    timelineLayout.padding = UI::UIEdgeSpacing::All(theme.spacing.space4);
    timelineLayout.flexContainer.gap.row = theme.spacing.space2;
    if (Core::Status status = storeNode(
            createSurface(tree, nodes_.splitC, timelineLayout, UI::UISurfaceVariant::Filled),
            nodes_.timeline);
        !status) {
        return status;
    }
    if (Core::Status status =
            tree.setSplitViewParts(nodes_.splitC, nodes_.viewport, nodes_.splitterC, nodes_.timeline);
        !status) {
        return status;
    }

    if (Core::Status status = buildLeftDockContent(tree, theme); !status) {
        return status;
    }
    if (Core::Status status = buildViewportContent(tree, theme); !status) {
        return status;
    }
    if (Core::Status status = buildInspectorContent(tree, theme); !status) {
        return status;
    }
    return buildTimelineContent(tree, theme);
}

Core::Status DesktopShellUI::buildLeftDockContent(PrimaryWindowUITreeUpdater& tree,
                                                  const UI::UITheme& theme)
{
    // Section header uses Section typography; no disclosure Button because these
    // sections are not collapsible in this reference.
    UI::UILayoutStyle headerLayout{};
    headerLayout.size.width = UI::UILayoutLength::Percent(100.0F);
    headerLayout.flexItem.shrink = 0.0F;
    UI::UINodeId hierarchyHeader{};
    if (Core::Status status = storeNode(
            createLabel(tree, nodes_.leftDock, headerLayout, "Hierarchy"), hierarchyHeader);
        !status) {
        return status;
    }

    UI::UILayoutStyle treeLayout = growingStyle();
    auto hierarchy = tree.createElement(
        nodes_.leftDock, UI::makeTreeViewElement({.materializedItemCapacity = 12}, treeLayout));
    if (!hierarchy) {
        return Core::failure(std::move(hierarchy.error()));
    }
    nodes_.hierarchyTree = *hierarchy;
    if (Core::Status status = tree.setTreeViewStyle(
            nodes_.hierarchyTree,
            UI::UITreeViewStyle{
                .rowHeight = theme.controls.treeRowHeight,
                .overscanRows = 1,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = theme.controls.treeRowHeight,
                // Row indentation uses a spacing token, not a magic value.
                .indentation = theme.spacing.space5,
            });
        !status) {
        return status;
    }
    if (Core::Status status = tree.setTreeViewDataSource(nodes_.hierarchyTree, treeDataSource());
        !status) {
        return status;
    }
    if (Core::Status status = tree.setTreeViewSelectedIndex(nodes_.hierarchyTree, 0); !status) {
        return status;
    }
    state_->hierarchySelection = HierarchyKeyBase;

    UI::UINodeId divider{};
    UI::UILayoutStyle dividerLayout{};
    dividerLayout.size.width = UI::UILayoutLength::Percent(100.0F);
    dividerLayout.size.height = UI::UILayoutLength::Px(1.0F);
    dividerLayout.flexItem.shrink = 0.0F;
    if (Core::Status status = storeNode(
            createDivider(tree, nodes_.leftDock, dividerLayout), divider);
        !status) {
        return status;
    }

    UI::UINodeId assetsHeader{};
    if (Core::Status status = storeNode(
            createLabel(tree, nodes_.leftDock, headerLayout, "Project Assets"), assetsHeader);
        !status) {
        return status;
    }

    UI::UILayoutStyle listLayout = growingStyle();
    auto assets = tree.createElement(
        nodes_.leftDock, UI::makeListViewElement({.materializedItemCapacity = 8}, listLayout));
    if (!assets) {
        return Core::failure(std::move(assets.error()));
    }
    nodes_.assetList = *assets;
    if (Core::Status status = tree.setListViewStyle(
            nodes_.assetList,
            UI::UIListViewStyle{
                .rowHeight = theme.controls.listRowHeight,
                .overscanRows = 1,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = theme.controls.listRowHeight,
            });
        !status) {
        return status;
    }
    if (Core::Status status = tree.setListViewDataSource(nodes_.assetList, listDataSource());
        !status) {
        return status;
    }
    if (Core::Status status = tree.setListViewSelectedIndex(nodes_.assetList, 0); !status) {
        return status;
    }
    state_->assetSelection = AssetKeyBase;
    return Core::success();
}

Core::Status DesktopShellUI::buildViewportContent(PrimaryWindowUITreeUpdater& tree,
                                                  const UI::UITheme& theme)
{
    // Placeholder for the active document surface. Real products put their
    // render target here; overlays would be Ignore-hit children.
    UI::UILayoutStyle hintLayout{};
    hintLayout.flexItem.shrink = 0.0F;
    if (Core::Status status = storeNode(
            createLabel(tree, nodes_.viewport, hintLayout, "Viewport — active document"),
            nodes_.viewportHint);
        !status) {
        return status;
    }
    UI::UILayoutStyle captionLayout{};
    captionLayout.flexItem.shrink = 0.0F;
    captionLayout.margin.top = theme.spacing.space2;
    if (Core::Status status = storeNode(
            createLabel(tree, nodes_.viewport, captionLayout, "Sunken surface, unobstructed"),
            nodes_.viewportSurface);
        !status) {
        return status;
    }
    return Core::success();
}

Core::Status DesktopShellUI::buildInspectorContent(PrimaryWindowUITreeUpdater& tree,
                                                   const UI::UITheme& theme)
{
    UI::UILayoutStyle titleLayout{};
    titleLayout.size.width = UI::UILayoutLength::Percent(100.0F);
    titleLayout.flexItem.shrink = 0.0F;
    if (Core::Status status = storeNode(
            createLabel(tree, nodes_.inspector, titleLayout, "Inspector"), nodes_.inspectorTitle);
        !status) {
        return status;
    }

    // Property rows are a two-column label/control pair, not one card per row.
    for (const std::string_view row : InspectorRows) {
        UI::UILayoutStyle rowLayout{};
        rowLayout.size.width = UI::UILayoutLength::Percent(100.0F);
        rowLayout.size.height = UI::UILayoutLength::Px(theme.controls.textEditHeight);
        rowLayout.flexItem.shrink = 0.0F;
        rowLayout.flexContainer.direction = UI::UIFlexDirection::Row;
        rowLayout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        rowLayout.flexContainer.gap.column = theme.spacing.space4;
        UI::UINodeId rowNode{};
        if (Core::Status status = storeNode(
                createSurface(tree, nodes_.inspector, rowLayout, UI::UISurfaceVariant::Plain), rowNode);
            !status) {
            return status;
        }

        UI::UILayoutStyle labelLayout{};
        labelLayout.size.width = UI::UILayoutLength::Px(96.0F);
        labelLayout.flexItem.shrink = 0.0F;
        UI::UINodeId label{};
        if (Core::Status status = storeNode(createLabel(tree, rowNode, labelLayout, row), label);
            !status) {
            return status;
        }

        // Control stretches to fill the remaining row width.
        UI::UILayoutStyle controlLayout{};
        controlLayout.flexItem.grow = 1.0F;
        controlLayout.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        controlLayout.size.height = UI::UILayoutLength::Px(theme.controls.textEditHeight);
        auto control = tree.createElement(rowNode, UI::makeTextEditElement({}, controlLayout));
        if (!control) {
            return Core::failure(std::move(control.error()));
        }
        if (Core::Status status = tree.setText(*control, "0.000"); !status) {
            return status;
        }
        ++inspectorRowCount_;
    }
    return Core::success();
}

Core::Status DesktopShellUI::buildTimelineContent(PrimaryWindowUITreeUpdater& tree,
                                                  const UI::UITheme& theme)
{
    UI::UILayoutStyle labelLayout{};
    labelLayout.size.width = UI::UILayoutLength::Percent(100.0F);
    labelLayout.flexItem.shrink = 0.0F;
    if (Core::Status status = storeNode(
            createLabel(tree, nodes_.timeline, labelLayout, "Timeline / Output"), nodes_.timelineLabel);
        !status) {
        return status;
    }

    UI::UILayoutStyle trackLayout = growingStyle();
    trackLayout.margin.top = theme.spacing.space2;
    UI::UINodeId track{};
    if (Core::Status status = storeNode(
            createSurface(tree, nodes_.timeline, trackLayout, UI::UISurfaceVariant::Plain), track);
        !status) {
        return status;
    }
    return Core::success();
}

Core::Status DesktopShellUI::buildStatusBar(PrimaryWindowUITreeUpdater& tree, const UI::UITheme& theme)
{
    UI::UILayoutStyle statusLayout = fillWidthStyle(theme.controls.statusBarHeight);
    statusLayout.padding = horizontalPadding(theme.spacing.space5);
    statusLayout.flexContainer.direction = UI::UIFlexDirection::Row;
    statusLayout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    statusLayout.flexContainer.gap.column = theme.spacing.space5;
    if (Core::Status status = storeNode(
            createSurface(tree, nodes_.background, statusLayout, UI::UISurfaceVariant::Filled),
            nodes_.statusBar);
        !status) {
        return status;
    }
    ++bandCount_;

    UI::UILayoutStyle labelLayout{};
    labelLayout.flexItem.shrink = 0.0F;
    if (Core::Status status =
            storeNode(createLabel(tree, nodes_.statusBar, labelLayout, "Ready"), nodes_.statusLabel);
        !status) {
        return status;
    }
    return Core::success();
}

// Reads the single committed geometry the UI published. The shell never derives
// pane rects itself; it only mirrors what SplitView resolved.
Core::Status DesktopShellUI::refreshCommittedGeometry(PrimaryWindowUITreeUpdater& tree)
{
    auto metricsA = tree.splitViewMetrics(nodes_.splitA);
    if (!metricsA) {
        return Core::failure(std::move(metricsA.error()));
    }
    auto metricsB = tree.splitViewMetrics(nodes_.splitB);
    if (!metricsB) {
        return Core::failure(std::move(metricsB.error()));
    }
    auto metricsC = tree.splitViewMetrics(nodes_.splitC);
    if (!metricsC) {
        return Core::failure(std::move(metricsC.error()));
    }

    leftDockWidth_ = metricsA->primaryRect.width;
    inspectorWidth_ = metricsB->secondaryRect.width;
    viewportWidth_ = metricsC->primaryRect.width;
    viewportHeight_ = metricsC->primaryRect.height;
    timelineHeight_ = metricsC->secondaryRect.height;

    auto statusRect = tree.committedLayoutRect(nodes_.statusBar);
    if (!statusRect) {
        return Core::failure(std::move(statusRect.error()));
    }
    statusBarHeight_ = statusRect->height;

    // The viewport must keep its minimum area and must not be overlapped by the
    // inspector: SplitView panes are disjoint, so comparing edges is enough.
    const float viewportRight = metricsC->primaryRect.x + viewportWidth_;
    const float inspectorLeft = metricsB->secondaryRect.x;
    viewportUnobstructed_ = viewportWidth_ > 0.0F && viewportHeight_ > 0.0F &&
                            viewportRight <= inspectorLeft + 1.0F;
    return Core::success();
}

// Responsive rules: collapse whole panes, never scale type. Timeline collapses
// first, then the inspector becomes command-driven below 960.
Core::Status DesktopShellUI::applyResponsiveTier(PrimaryWindowUITreeUpdater& tree)
{
    const ShellResponsiveTier tier = tierForWidth(logicalWidth_);
    tier_ = tier;

    const bool timelineCollapsed = tier != ShellResponsiveTier::Full;
    const bool inspectorVisible = tier != ShellResponsiveTier::Minimal;
    state_->timelineCollapsed = timelineCollapsed;
    state_->inspectorVisible = inspectorVisible;

    // Timeline: collapse by giving the viewport the whole vertical extent.
    auto metricsC = tree.splitViewMetrics(nodes_.splitC);
    if (!metricsC) {
        return Core::failure(std::move(metricsC.error()));
    }
    const float verticalTotal =
        metricsC->primaryRect.height + metricsC->splitterRect.height +
        metricsC->secondaryRect.height;
    const float timelineFraction =
        timelineCollapsed ? 1.0F : fractionForSecondary(TimelineDefaultHeight, verticalTotal);
    if (Core::Status status = tree.setSplitViewFraction(nodes_.splitC, timelineFraction); !status) {
        return status;
    }
    if (Core::Status status = tree.setLayoutStyle(
            nodes_.splitterC,
            timelineCollapsed ? UI::UILayoutStyle{.visibility = UI::UIVisibility::Collapsed}
                              : UI::UILayoutStyle{});
        !status) {
        return status;
    }

    auto metricsB = tree.splitViewMetrics(nodes_.splitB);
    if (!metricsB) {
        return Core::failure(std::move(metricsB.error()));
    }
    const float horizontalTotal =
        metricsB->primaryRect.width + metricsB->splitterRect.width +
        metricsB->secondaryRect.width;
    const float inspectorFraction =
        inspectorVisible ? fractionForSecondary(InspectorDefaultWidth, horizontalTotal) : 1.0F;
    if (Core::Status status = tree.setSplitViewFraction(nodes_.splitB, inspectorFraction); !status) {
        return status;
    }

    auto metricsA = tree.splitViewMetrics(nodes_.splitA);
    if (!metricsA) {
        return Core::failure(std::move(metricsA.error()));
    }
    const float dockTotal = metricsA->primaryRect.width +
                            metricsA->splitterRect.width +
                            metricsA->secondaryRect.width;
    const float dockTarget =
        tier == ShellResponsiveTier::Full ? LeftDockDefaultWidth : LeftDockMinWidth;
    if (Core::Status status =
            tree.setSplitViewFraction(nodes_.splitA, fractionForPrimary(dockTarget, dockTotal));
        !status) {
        return status;
    }
    return Core::success();
}

Core::Status DesktopShellUI::update(UIUpdateContext& context)
{
    if (!root_ || !context.hasPrimaryWindowUI()) {
        return Core::success();
    }
    auto tree = context.primaryWindowUITreeUpdater(*root_);
    if (!tree) {
        return Core::failure(std::move(tree.error()));
    }

    if (layoutDirty_) {
        if (Core::Status status = applyResponsiveTier(*tree); !status) {
            return status;
        }
        layoutDirty_ = false;
        statusDirty_ = true;
    }

    if (Core::Status status = refreshCommittedGeometry(*tree); !status) {
        return status;
    }

    if (statusDirty_) {
        const std::string_view message =
            tier_ == ShellResponsiveTier::Full      ? "Ready — full workspace"
            : tier_ == ShellResponsiveTier::Compressed ? "Ready — timeline collapsed"
                                                       : "Ready — inspector on demand";
        if (Core::Status status = tree->setText(nodes_.statusLabel, message); !status) {
            return status;
        }
        statusDirty_ = false;
    }
    return Core::success();
}

void DesktopShellUI::setLogicalWidth(float logicalWidth) noexcept
{
    if (!std::isfinite(logicalWidth) || logicalWidth <= 0.0F) {
        return;
    }
    if (logicalWidth == logicalWidth_) {
        return;
    }
    logicalWidth_ = logicalWidth;
    layoutDirty_ = true;
}

void DesktopShellUI::release() noexcept
{
    root_.reset();
}

DesktopShellSnapshot DesktopShellUI::snapshot() const noexcept
{
    return DesktopShellSnapshot{
        .theme = currentTheme_,
        .density = density_,
        .tier = tier_,
        .activeDocument = state_ == nullptr ? ShellDocument::Scene : state_->activeDocument,
        .splitViewCount = splitViewCount_,
        .commandCount = commandCount_,
        .bandCount = bandCount_,
        .inspectorRowCount = inspectorRowCount_,
        .leftDockWidth = leftDockWidth_,
        .viewportWidth = viewportWidth_,
        .viewportHeight = viewportHeight_,
        .inspectorWidth = inspectorWidth_,
        .timelineHeight = timelineHeight_,
        .statusBarHeight = statusBarHeight_,
        .themeSwitches = themeSwitches_,
        .densitySwitchRequests = densitySwitchRequests_,
        .documentSwitches = documentSwitches_,
        .splitterDrags = splitterDrags_,
        .commandActivations = commandActivations_,
        .viewportUnobstructed = viewportUnobstructed_,
        .timelineCollapsed = state_ != nullptr && state_->timelineCollapsed,
        .inspectorVisible = state_ == nullptr || state_->inspectorVisible,
        .dialogOpen = state_ != nullptr && state_->dialogOpen,
        .menuOpen = state_ != nullptr && state_->menuOpen,
        .rootAlive = root_.has_value(),
    };
}

} // namespace Tina::SampleUI
