#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

inline constexpr float LeftDockInitialFraction = 0.22F;
inline constexpr float MainCenterInitialFraction = 0.68F;
inline constexpr float ViewportInitialFraction = 0.68F;
inline constexpr float LeftDockMinWidth = 200.0F;
inline constexpr float ViewportMinWidth = 480.0F;
inline constexpr float InspectorMinWidth = 280.0F;
inline constexpr float ViewportMinHeight = 320.0F;
inline constexpr float TimelineMinHeight = 160.0F;
inline constexpr float AnimationModeButtonWidth = 120.0F;
inline constexpr float AnimationFrameSlotWidth = 44.0F;
inline constexpr u64 SnackbarUndoActionToken = 1U;

[[nodiscard]] constexpr UI::UITheme makeEditorProductTheme() noexcept
{
    UI::UITheme theme = UI::makeModernDesktopTheme(
        UI::UIColorScheme::Dark, UI::UIDensity::Compact);

    // Neutral graphite surfaces keep dense authoring chrome quiet; teal is
    // reserved for selection/focus and coral for destructive actions.
    theme.colors.background = UI::rgb(0x0D1012);
    theme.colors.surface = UI::rgb(0x15191C);
    theme.colors.surfaceContainerLow = UI::rgb(0x1C2125);
    theme.colors.surfaceContainer = UI::rgb(0x252C31);
    theme.colors.surfaceContainerHigh = UI::rgb(0x30383E);
    theme.colors.onSurface = UI::rgb(0xF0F4F5);
    theme.colors.onSurfaceVariant = UI::rgb(0xA7B2B8);
    theme.colors.outline = UI::rgb(0x59666C);
    theme.colors.outlineVariant = UI::rgb(0x30383D);
    theme.colors.primary = UI::rgb(0x64D8B4);
    theme.colors.onPrimary = UI::rgb(0x06241B);
    theme.colors.primaryContainer = UI::rgb(0x173D34);
    theme.colors.onPrimaryContainer = UI::rgb(0xC6F7E8);
    theme.colors.error = UI::rgb(0xFF9B91);
    theme.colors.onError = UI::rgb(0x3B0707);
    theme.colors.errorContainer = UI::rgb(0x5A2423);
    theme.colors.onErrorContainer = UI::rgb(0xFFDAD5);
    theme.colors.success = UI::rgb(0x72D39D);
    theme.colors.warning = UI::rgb(0xF1C75B);
    theme.colors.focusRing = UI::rgb(0x8BE8CC);
    theme.colors.scrim = UI::rgb(0x000000, 122);
    theme.colors.shadow = UI::rgb(0x000000, 112);

    theme.states.hoveredAlpha = 24;
    theme.states.focusVisibleAlpha = 34;
    theme.states.pressedAlpha = 42;
    theme.typography.title = 18.0F;
    theme.typography.section = 15.0F;
    theme.typography.body = 14.0F;
    theme.typography.control = 13.0F;
    theme.controls.dialogMinWidth = 560.0F;
    theme.controls.panelCornerRadius = 6.0F;
    theme.controls.controlCornerRadius = 4.0F;
    theme.controls.panelShadowOffsetX = 0.0F;
    theme.controls.panelShadowOffsetY = 2.0F;
    theme.elevations.raisedOffsetY = 1.0F;
    theme.elevations.floatingOffsetY = 3.0F;
    theme.elevations.modalOffsetY = 5.0F;
    return theme;
}

[[nodiscard]] UI::UILayoutStyle sceneDeleteDialogSurfaceLayout(
    const UI::UITheme& theme) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(theme.controls.dialogMinWidth);
    style.minMax.maxWidth = UI::UILayoutLength::Percent(100.0F);
    style.padding = UI::UIEdgeSpacing::All(theme.spacing.space8);
    style.flexContainer.gap = UI::UILayoutGap::All(theme.spacing.space6);
    return style;
}

template <typename NodeResult>
[[nodiscard]] Tina::Core::Status storeNode(NodeResult&& result, UI::UINodeId& output)
{
    if (!result) {
        return Tina::Core::failure(std::move(result.error()));
    }
    output = *result;
    return Tina::Core::success();
}

[[nodiscard]] Tina::Core::Status appendVerticalDivider(
    Tina::PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
    float height)
{
    UI::UILayoutStyle layout{};
    layout.size.height = UI::UILayoutLength::Px(height);
    layout.flexItem.shrink = 0.0F;
    auto divider = tree.createElement(
        parent,
        UI::makeDividerElement({
            .orientation = UI::UIDividerOrientation::Vertical,
            .tone = UI::UIDividerTone::Subtle,
        }, layout));
    if (!divider) {
        return Tina::Core::failure(std::move(divider.error()));
    }
    return Tina::Core::success();
}

} // namespace

auto EditorWorkspaceState::UiBuildContext::createSurface(
    UI::UINodeId parent, UI::UILayoutStyle layout,
    UI::UISurfaceVariant variant) -> Tina::Core::Result<UI::UINodeId>
{
    return tree.createElement(
        parent, UI::makeSurfaceElement({.variant = variant}, layout));
}

auto EditorWorkspaceState::UiBuildContext::createPanel(
    UI::UINodeId parent, UI::UILayoutStyle layout)
    -> Tina::Core::Result<UI::UINodeId>
{
    return tree.createElement(parent, UI::makePanelElement(layout));
}

auto EditorWorkspaceState::UiBuildContext::createLabel(
    UI::UINodeId parent, std::string_view text, UI::UILayoutStyle layout,
    const UI::UITextStyle& style) -> Tina::Core::Result<UI::UINodeId>
{
    UI::UIElementDescriptor descriptor = UI::makeLabelElement(text, layout);
    descriptor.textStyle = style;
    return tree.createElement(parent, descriptor);
}

auto EditorWorkspaceState::UiBuildContext::createBadge(
    UI::UINodeId parent, std::string_view text, UI::UILayoutStyle layout,
    UI::UIBadgeTone tone) -> Tina::Core::Result<UI::UINodeId>
{
    return tree.createElement(
        parent, UI::makeBadgeElement(text, {.tone = tone}, layout));
}

auto EditorWorkspaceState::UiBuildContext::createButton(
    UI::UINodeId parent, std::string_view text, UI::UILayoutStyle layout,
    bool enabled, UI::UIStyleRoleId role) -> Tina::Core::Result<UI::UINodeId>
{
    UI::UIElementDescriptor descriptor = UI::makeButtonElement(text, layout);
    descriptor.visual.styleRole = role;
    descriptor.enabled = enabled;
    return tree.createElement(parent, descriptor);
}

auto EditorWorkspaceState::UiBuildContext::createSegmentedButton(
    UI::UINodeId parent, std::string_view text, UI::UILayoutStyle layout,
    bool enabled) -> Tina::Core::Result<UI::UINodeId>
{
    UI::UIElementDescriptor descriptor = UI::makeRadioButtonElement(text, layout);
    descriptor.contentAlignment.horizontal = UI::UIAxisAlignment::Center;
    descriptor.visual.styleRole = UI::UIStyleRoleId::SegmentedButton;
    descriptor.enabled = enabled;
    return tree.createElement(parent, descriptor);
}

auto EditorWorkspaceState::UiBuildContext::createIconButton(
    UI::UINodeId parent, EditorIcon icon, std::string_view accessibleName,
    UI::UILayoutStyle layout, bool enabled, UI::UIButtonVariant variant)
    -> Tina::Core::Result<UI::UINodeId>
{
    auto parts = EditorIconButton::Build(
        tree, parent, productTheme, icon, accessibleName, layout, enabled,
        variant);
    if (!parts) {
        return Tina::Core::failure(std::move(parts.error()));
    }
    return parts->button;
}

auto EditorWorkspaceState::UiBuildContext::createIconToggleButton(
    UI::UINodeId parent, EditorIcon icon, std::string_view accessibleName,
    UI::UILayoutStyle layout, bool enabled)
    -> Tina::Core::Result<UI::UINodeId>
{
    auto parts = EditorIconToggleButton::Build(
        tree, parent, productTheme, icon, accessibleName, layout, enabled);
    if (!parts) {
        return Tina::Core::failure(std::move(parts.error()));
    }
    return parts->button;
}

auto EditorWorkspaceState::UiBuildContext::createTextEdit(
    UI::UINodeId parent, std::string_view text, UI::UILayoutStyle layout,
    bool enabled) -> Tina::Core::Result<UI::UINodeId>
{
    UI::UIElementDescriptor descriptor = UI::makeTextEditElement(text, layout);
    descriptor.textStyle = compactText;
    descriptor.enabled = enabled;
    return tree.createElement(parent, descriptor);
}

auto EditorWorkspaceState::buildCommandBarUi(UiBuildContext& ui, UI::UINodeId parent)
    -> Tina::Core::Status
{
    UI::UINodeId toolbar{};
    UI::UILayoutStyle toolbarStyle = fillWidth(ui.productTheme.controls.commandBarHeight);
    toolbarStyle.flexItem.shrink = 0.0F;
    toolbarStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    toolbarStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    toolbarStyle.flexContainer.gap.column = ui.productTheme.spacing.space4;
    toolbarStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(
        ui.productTheme.spacing.space5, ui.productTheme.spacing.space1);
    if (auto status = storeNode(ui.createSurface(parent, toolbarStyle, UI::UISurfaceVariant::Filled),
                                toolbar);
        !status) {
        return status;
    }

    UI::UINodeId toolbarBrand{};
    if (auto status = storeNode(ui.createLabel(toolbar, "Tina Editor", fixedSize(132.0F, 26.0F), ui.titleText),
                                toolbarBrand);
        !status) {
        return status;
    }
    UI::UINodeId toolbarSpacer{};
    UI::UILayoutStyle toolbarSpacerStyle{};
    toolbarSpacerStyle.flexItem.grow = 1.0F;
    toolbarSpacerStyle.flexItem.shrink = 1.0F;
    toolbarSpacerStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    if (auto status = storeNode(
            ui.createPanel(toolbar, toolbarSpacerStyle), toolbarSpacer);
        !status) {
        return status;
    }
    const WorkspaceSessionState& initialSession = activeWorkspaceSession();
    UI::UINodeId playGroup{};
    if (auto status = storeNode(EditorToolbarGroup::Build(
                                    ui.tree, toolbar, ui.productTheme),
                                playGroup);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    playGroup, EditorIcon::Play, "Play",
                                    {}, true, UI::UIButtonVariant::Primary),
                                playButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    playGroup, EditorIcon::Pause, "Pause", {}, false),
                                pauseButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    playGroup, EditorIcon::Step, "Step", {}, false),
                                stepButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    playGroup, EditorIcon::Stop, "Stop", {}, false),
                                stopButton_);
        !status) {
        return status;
    }
    if (auto status = appendVerticalDivider(
            ui.tree, toolbar, ui.productTheme.controls.buttonHeight);
        !status) {
        return status;
    }
    UI::UINodeId historyGroup{};
    if (auto status = storeNode(EditorToolbarGroup::Build(
                                    ui.tree, toolbar, ui.productTheme),
                                historyGroup);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    historyGroup, EditorIcon::Undo, "Undo"),
                                undoButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    historyGroup, EditorIcon::Redo, "Redo"),
                                redoButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    historyGroup, EditorIcon::Save, "Save", {},
                                    initialSession.hasDocumentPath(),
                                    UI::UIButtonVariant::Outlined),
                                saveButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    historyGroup, EditorIcon::SaveAs, "Save As", {}, true,
                                    UI::UIButtonVariant::Outlined),
                                saveAsButton_);
        !status) {
        return status;
    }

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildDocumentTabsUi(UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    UI::UINodeId documentTabsBar{};
    UI::UILayoutStyle documentTabsBarStyle = fillWidth(ui.productTheme.controls.tabHeight);
    documentTabsBarStyle.flexItem.shrink = 0.0F;
    documentTabsBarStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    documentTabsBarStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    documentTabsBarStyle.flexContainer.gap.column = ui.productTheme.spacing.space1;
    documentTabsBarStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(
        ui.productTheme.spacing.space3, ui.productTheme.spacing.space0);
    if (auto status = storeNode(ui.createSurface(parent, documentTabsBarStyle,
                                              UI::UISurfaceVariant::Filled),
                                documentTabsBar);
        !status) {
        return status;
    }
    for (u32 index = 0; index < DocumentTabSlots; ++index) {
        const auto* tab = documentTabs_.tab(index);
        if (auto status = storeNode(ui.createSegmentedButton(documentTabsBar,
                                                          tab != nullptr ? tab->title : std::string_view{},
                                                          editorDocumentTabLayout(
                                                              ui.productTheme,
                                                              tab != nullptr
                                                                  ? UI::UIVisibility::Visible
                                                                  : UI::UIVisibility::Collapsed),
                                                          tab != nullptr),
                                    documentTabButtons_[index]);
            !status) {
            return status;
        }
        if (auto status = ui.tree.setTextOverflow(documentTabButtons_[index],
                                                  UI::UITextOverflow::Ellipsis);
            !status) {
            return status;
        }
    }
    if (auto status = storeNode(ui.createIconButton(
                                    documentTabsBar, EditorIcon::Close,
                                    "Close document", {}, false),
                                closeDocumentButton_);
        !status) {
        return status;
    }

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildWorkspaceUi(UiBuildContext& ui, UI::UINodeId parent)
    -> Tina::Core::Status
{
    const float splitterExtent = ui.productTheme.controls.splitterHitExtent;
    const UI::UILayoutStyle workspaceStyle = growingRegion();

    UI::UINodeId splitA{};
    const UI::UISplitViewConfig splitAConfig{
        .orientation = UI::UISplitViewOrientation::Horizontal,
        .initialFraction = LeftDockInitialFraction,
        .minPrimarySize = LeftDockMinWidth,
        .minSecondarySize = ViewportMinWidth + InspectorMinWidth,
        .splitterExtent = splitterExtent,
    };
    if (auto status = storeNode(
            ui.tree.createElement(parent, UI::makeSplitViewElement(splitAConfig, workspaceStyle)),
            splitA);
        !status) {
        return status;
    }

    UI::UINodeId leftDock{};
    if (auto status = buildLeftDockUi(ui, splitA, leftDock); !status) {
        return status;
    }
    UI::UINodeId splitterA{};
    if (auto status = storeNode(
            ui.tree.createElement(splitA, UI::makeSplitterElement({})), splitterA);
        !status) {
        return status;
    }
    UI::UINodeId main{};
    if (auto status = storeNode(
            ui.createPanel(splitA, growingRegion()), main);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setSplitViewParts(splitA, leftDock, splitterA, main); !status) {
        return status;
    }

    UI::UINodeId splitB{};
    const UI::UISplitViewConfig splitBConfig{
        .orientation = UI::UISplitViewOrientation::Horizontal,
        .initialFraction = MainCenterInitialFraction,
        .minPrimarySize = ViewportMinWidth,
        .minSecondarySize = InspectorMinWidth,
        .splitterExtent = splitterExtent,
    };
    if (auto status = storeNode(
            ui.tree.createElement(main, UI::makeSplitViewElement(splitBConfig, growingRegion())),
            splitB);
        !status) {
        return status;
    }

    UI::UINodeId center{};
    if (auto status = storeNode(
            ui.createPanel(splitB, growingRegion()), center);
        !status) {
        return status;
    }
    UI::UINodeId splitterB{};
    if (auto status = storeNode(
            ui.tree.createElement(splitB, UI::makeSplitterElement({})), splitterB);
        !status) {
        return status;
    }
    UI::UINodeId inspector{};
    if (auto status = buildInspectorUi(ui, splitB, inspector); !status) {
        return status;
    }
    if (auto status = ui.tree.setSplitViewParts(splitB, center, splitterB, inspector); !status) {
        return status;
    }

    UI::UINodeId splitC{};
    const UI::UISplitViewConfig splitCConfig{
        .orientation = UI::UISplitViewOrientation::Vertical,
        .initialFraction = ViewportInitialFraction,
        .minPrimarySize = ViewportMinHeight,
        .minSecondarySize = TimelineMinHeight,
        .splitterExtent = splitterExtent,
    };
    if (auto status = storeNode(
            ui.tree.createElement(center, UI::makeSplitViewElement(splitCConfig, growingRegion())),
            splitC);
        !status) {
        return status;
    }

    UI::UINodeId viewport{};
    if (auto status = buildViewportUi(ui, splitC, viewport); !status) {
        return status;
    }
    UI::UINodeId splitterC{};
    if (auto status = storeNode(
            ui.tree.createElement(splitC, UI::makeSplitterElement({})), splitterC);
        !status) {
        return status;
    }
    UI::UINodeId timeline{};
    if (auto status = buildTimelineUi(ui, splitC, timeline); !status) {
        return status;
    }
    return ui.tree.setSplitViewParts(splitC, viewport, splitterC, timeline);
}

auto EditorWorkspaceState::buildLeftDockUi(
    UiBuildContext& ui, UI::UINodeId parent, UI::UINodeId& left) -> Tina::Core::Status
{
    UI::UILayoutStyle leftStyle = growingRegion();
    leftStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    leftStyle.flexContainer.gap.row = ui.productTheme.spacing.space3;
    leftStyle.padding = UI::UIEdgeSpacing::All(ui.productTheme.spacing.space4);
    if (auto status = storeNode(
            ui.createSurface(parent, leftStyle, UI::UISurfaceVariant::Filled), left);
        !status) {
        return status;
    }

    UI::UILayoutStyle hierarchyHeaderStyle = fillWidth(ui.productTheme.controls.buttonHeight);
    auto hierarchyHeader = EditorPanelHeader::Build(
        ui.tree, left, ui.productTheme, "Hierarchy", ui.sectionText,
        hierarchyHeaderStyle);
    if (!hierarchyHeader) {
        return Tina::Core::failure(std::move(hierarchyHeader.error()));
    }
    std::string hierarchyCountText =
        std::to_string(hierarchyRows_.empty() ? 0U : hierarchyRows_.size() - 1U);
    hierarchyCountText += " nodes";
    if (auto status = storeNode(ui.createBadge(
                                    hierarchyHeader->actions, hierarchyCountText,
                                    fixedSize(72.0F, 20.0F)),
                                hierarchyCount_);
        !status) {
        return status;
    }

    auto hierarchySearch = EditorSearchField::Build(
        ui.tree, left, ui.productTheme, {}, "Search hierarchy",
        fillWidth(ui.productTheme.controls.textEditHeight), true);
    if (!hierarchySearch) {
        return Tina::Core::failure(std::move(hierarchySearch.error()));
    }
    hierarchySearchInput_ = hierarchySearch->textEdit;
    UI::UINodeId hierarchyActions{};
    UI::UILayoutStyle hierarchyActionsStyle = fillWidth(ui.productTheme.controls.buttonHeight);
    hierarchyActionsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    hierarchyActionsStyle.flexContainer.gap.column = ui.productTheme.spacing.space3;
    if (auto status = storeNode(ui.createPanel(left, hierarchyActionsStyle),
                                hierarchyActions);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    hierarchyActions, EditorIcon::Add,
                                    "Add entity", {}, true,
                                    UI::UIButtonVariant::Primary),
                                addEntityButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    hierarchyActions, EditorIcon::Duplicate,
                                    "Duplicate entity", {}, false),
                                duplicateEntityButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    hierarchyActions, EditorIcon::Delete,
                                    "Delete entity", {}, false,
                                    UI::UIButtonVariant::Danger),
                                deleteEntityButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    hierarchyActions, EditorIcon::Focus,
                                    "Focus entity", {}, false),
                                focusEntityButton_);
        !status) {
        return status;
    }

    UI::UILayoutStyle hierarchyStyle = growingRegion();
    hierarchyStyle.minMax.minHeight = UI::UILayoutLength::Px(128.0F);
    auto hierarchy = ui.tree.createElement(
        left, UI::makeTreeViewElement({.materializedItemCapacity = HierarchyMaterializedCapacity},
                                      hierarchyStyle));
    if (!hierarchy) {
        return Tina::Core::failure(std::move(hierarchy.error()));
    }
    hierarchyTree_ = *hierarchy;
    if (auto status = ui.tree.setTreeViewStyle(
            hierarchyTree_,
            UI::UITreeViewStyle{
                .rowHeight = ui.productTheme.controls.treeRowHeight,
                .overscanRows = 1,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = ui.productTheme.controls.treeRowHeight,
                .indentation = ui.productTheme.spacing.space6,
                .disclosureExtent = 10.0F,
                .disclosureGap = ui.productTheme.spacing.space2,
            });
        !status) {
        return status;
    }
    if (auto status = ui.tree.setTreeViewPaint(hierarchyTree_, UI::makeTreeViewPaint(ui.productTheme)); !status) {
        return status;
    }
    if (auto status = ui.tree.setTreeViewDataSource(hierarchyTree_, hierarchyDataSource()); !status) {
        return status;
    }
    if (auto status = ui.tree.setTreeViewSelectedIndex(hierarchyTree_, 0); !status) {
        return status;
    }

    UI::UILayoutStyle projectHeaderStyle = fillWidth(ui.productTheme.controls.buttonHeight);
    auto projectHeader = EditorPanelHeader::Build(
        ui.tree, left, ui.productTheme, "Project Assets", ui.sectionText,
        projectHeaderStyle);
    if (!projectHeader) {
        return Tina::Core::failure(std::move(projectHeader.error()));
    }
    std::string initialProjectCount = std::to_string(projectAssets_.visibleItemCount());
    initialProjectCount += " / ";
    initialProjectCount += std::to_string(projectAssets_.itemCount());
    if (auto status = storeNode(ui.createBadge(
                                    projectHeader->actions, initialProjectCount,
                                    fixedSize(58.0F, 20.0F)),
                                projectAssetCount_);
        !status) {
        return status;
    }

    UI::UINodeId projectFilters{};
    UI::UILayoutStyle projectFiltersStyle = fillWidth(ui.productTheme.controls.buttonHeight);
    projectFiltersStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    projectFiltersStyle.flexContainer.gap.column = ui.productTheme.spacing.space2;
    if (auto status = storeNode(ui.createPanel(left, projectFiltersStyle),
                                projectFilters);
        !status) {
        return status;
    }
    const std::array<std::string_view, 4> projectFilterLabels{"All", "2D", "3D", "Media"};
    for (u32 index = 0; index < projectFilterLabels.size(); ++index) {
        UI::UILayoutStyle filterStyle = fixedSize(
            0.0F, ui.productTheme.controls.buttonHeight);
        filterStyle.size.width = UI::UILayoutLength::Auto();
        filterStyle.flexItem.grow = 1.0F;
        filterStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        if (auto status = storeNode(ui.createSegmentedButton(projectFilters, projectFilterLabels[index],
                                                          filterStyle),
                                    projectFilterButtons_[index]);
            !status) {
            return status;
        }
    }

    UI::UILayoutStyle projectListStyle = growingRegion();
    projectListStyle.minMax.minHeight = UI::UILayoutLength::Px(96.0F);
    auto projectList = ui.tree.createElement(
        left, UI::makeVirtualGridViewElement(
                  {.materializedItemCapacity = AssetBrowserMaterializedCapacity},
                  projectListStyle));
    if (!projectList) {
        return Tina::Core::failure(std::move(projectList.error()));
    }
    projectAssetList_ = *projectList;
    if (auto status = ui.tree.setVirtualGridViewStyle(
            projectAssetList_,
            UI::UIVirtualGridViewStyle{
                .minimumItemWidth = ProjectAssetMinimumItemWidth,
                .itemHeight = ui.productTheme.controls.listRowHeight,
                .overscanRows = 1,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = ui.productTheme.controls.listRowHeight,
                .itemTextOverflow = UI::UITextOverflow::Ellipsis,
            });
        !status) {
        return status;
    }
    if (auto status = ui.tree.setVirtualGridViewPaint(projectAssetList_,
                                             UI::makeVirtualGridViewPaint(ui.productTheme));
        !status) {
        return status;
    }
    if (auto status = ui.tree.setVirtualGridViewDataSource(projectAssetList_,
                                                  projectAssetDataSource());
        !status) {
        return status;
    }
    observedProjectAssetSelectionIndex_.reset();
    projectAssetSelectionSyncPending_ =
        projectAssets_.visibleItemCount() != 0U;

    UI::UILayoutStyle projectAssetSummaryStyle = fillWidth(22.0F);
    projectAssetSummaryStyle.flexItem.shrink = 0.0F;
    if (auto status = storeNode(
            ui.createLabel(left, "No assets match this filter",
                           projectAssetSummaryStyle, ui.secondaryText),
            projectAssetSummary_);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setTextOverflow(
            projectAssetSummary_, UI::UITextOverflow::Ellipsis);
        !status) {
        return status;
    }

    sourceImportSectionLayout_ = fillWidth(
        ui.productTheme.controls.buttonHeight + ui.productTheme.spacing.space2 + 80.0F);
    sourceImportSectionLayout_.flexItem.shrink = 0.0F;
    sourceImportSectionLayout_.flexContainer.direction = UI::UIFlexDirection::Column;
    sourceImportSectionLayout_.flexContainer.gap.row = ui.productTheme.spacing.space2;
    sourceImportSectionLayout_.visibility = sourceImportUnits_.empty()
                                                ? UI::UIVisibility::Collapsed
                                                : UI::UIVisibility::Visible;
    if (auto status = storeNode(
            ui.createPanel(left, sourceImportSectionLayout_), sourceImportSection_);
        !status) {
        return status;
    }

    UI::UILayoutStyle sourceImportHeaderStyle = fillWidth(ui.productTheme.controls.buttonHeight);
    auto sourceImportHeader = EditorPanelHeader::Build(
        ui.tree, sourceImportSection_, ui.productTheme, "Source Imports", ui.sectionText,
        sourceImportHeaderStyle);
    if (!sourceImportHeader) {
        return Tina::Core::failure(std::move(sourceImportHeader.error()));
    }
    if (auto status = storeNode(ui.createBadge(
                                    sourceImportHeader->actions,
                                    std::to_string(sourceImportUnits_.size()),
                                    fixedSize(36.0F, 20.0F)),
                                sourceImportCount_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    sourceImportHeader->actions, EditorIcon::Delete,
                                    "Remove source import", {},
                                    activeProjectWorkspace_.has_value() &&
                                        !sourceImportUnits_.empty()),
                                removeSourceImportButton_);
        !status) {
        return status;
    }

    UI::UILayoutStyle sourceImportListStyle = fillWidth(80.0F);
    sourceImportListStyle.minMax.minHeight = UI::UILayoutLength::Px(64.0F);
    auto sourceImportList = ui.tree.createElement(
        sourceImportSection_, UI::makeListViewElement(
                  {.materializedItemCapacity = SourceImportMaterializedCapacity},
                  sourceImportListStyle));
    if (!sourceImportList) {
        return Tina::Core::failure(std::move(sourceImportList.error()));
    }
    sourceImportList_ = *sourceImportList;
    if (auto status = ui.tree.setListViewStyle(
            sourceImportList_,
            UI::UIListViewStyle{
                .rowHeight = ui.productTheme.controls.listRowHeight,
                .overscanRows = 1,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = ui.productTheme.controls.listRowHeight,
            });
        !status) {
        return status;
    }
    if (auto status = ui.tree.setListViewPaint(
            sourceImportList_, UI::makeListViewPaint(ui.productTheme));
        !status) {
        return status;
    }
    if (auto status = ui.tree.setListViewDataSource(
            sourceImportList_, sourceImportDataSource());
        !status) {
        return status;
    }
    if (!sourceImportUnits_.empty()) {
        if (auto status = ui.tree.setListViewSelectedIndex(sourceImportList_, 0U);
            !status) {
            return status;
        }
        observedSourceImportSelectionIndex_ = 0U;
    }

    UI::UINodeId projectActions{};
    UI::UILayoutStyle projectActionsStyle = fillWidth(
        ui.productTheme.controls.buttonHeight);
    projectActionsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    projectActionsStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    projectActionsStyle.flexContainer.gap.column = ui.productTheme.spacing.space3;
    if (auto status = storeNode(ui.createPanel(left, projectActionsStyle),
                                projectActions);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    projectActions, EditorIcon::Add,
                                    "New project"),
                                createProjectButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    projectActions, EditorIcon::Open,
                                    "Open project"),
                                openProjectButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    projectActions, EditorIcon::Import,
                                    "Import source", {},
                                    activeProjectWorkspace_.has_value()),
                                importSourceButton_);
        !status) {
        return status;
    }

    if (auto status = appendVerticalDivider(
            ui.tree, projectActions, ui.productTheme.controls.iconButtonExtent);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    projectActions, EditorIcon::MoveRight,
                                    "Open asset", {},
                                             projectAssets_.visibleItemCount() != 0U),
                                openProjectAssetButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    projectActions, EditorIcon::Refresh,
                                    "Refresh catalog", {},
                                    assetResources_.projectCatalogConfigured),
                                refreshProjectCatalogButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(projectActions, "Catalog",
                                            fixedSize(52.0F, 20.0F), ui.accentText),
                                projectAssetSource_);
        !status) {
        return status;
    }

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildViewportUi(
    UiBuildContext& ui, UI::UINodeId parent, UI::UINodeId& center) -> Tina::Core::Status
{
    UI::UILayoutStyle centerStyle = growingRegion();
    centerStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    centerStyle.flexContainer.gap.row = ui.productTheme.spacing.space3;
    centerStyle.padding = UI::UIEdgeSpacing::All(ui.productTheme.spacing.space4);
    if (auto status = storeNode(ui.createPanel(parent, centerStyle), center);
        !status) {
        return status;
    }

    UI::UINodeId viewportHeader{};
    UI::UILayoutStyle viewportHeaderStyle = fillWidth(ui.productTheme.controls.buttonHeight);
    viewportHeaderStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    viewportHeaderStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    viewportHeaderStyle.flexContainer.gap.column = ui.productTheme.spacing.space4;
    if (auto status = storeNode(ui.createPanel(center, viewportHeaderStyle),
                                viewportHeader);
        !status) {
        return status;
    }
    UI::UILayoutStyle breadcrumbStyle = fixedSize(0.0F, 24.0F);
    breadcrumbStyle.size.width = UI::UILayoutLength::Auto();
    breadcrumbStyle.flexItem.grow = 1.0F;
    breadcrumbStyle.flexItem.shrink = 1.0F;
    breadcrumbStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    if (auto status = storeNode(ui.createLabel(viewportHeader,
                                            workspaceMode_ == WorkspaceMode::World2D
                                                ? "Scene / World2D"
                                                : "Scene / World3D",
                                            breadcrumbStyle, ui.secondaryText),
                                breadcrumb_);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setTextOverflow(breadcrumb_, UI::UITextOverflow::Ellipsis);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(viewportHeader,
                         workspaceMode_ == WorkspaceMode::World2D
                             ? "Orthographic"
                             : "View: Custom",
                         fixedSize(136.0F, ui.productTheme.controls.buttonHeight),
                         workspaceMode_ == WorkspaceMode::World3D),
            viewportMode_);
        !status) {
        return status;
    }

    UI::UINodeId viewportTools{};
    UI::UILayoutStyle viewportToolsStyle = fillWidth(
        ui.productTheme.controls.contextToolbarHeight);
    viewportToolsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    viewportToolsStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    viewportToolsStyle.flexContainer.gap.column = ui.productTheme.spacing.space3;
    if (auto status = storeNode(ui.createSurface(center, viewportToolsStyle, UI::UISurfaceVariant::Filled),
                                viewportTools);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconToggleButton(
                                    viewportTools, EditorIcon::Select, "Select tool"),
                                selectToolButtons_[0]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconToggleButton(
                                    viewportTools, EditorIcon::Move, "Move tool"),
                                translateToolButtons_[0]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconToggleButton(
                                    viewportTools, EditorIcon::Rotate, "Rotate tool"),
                                rotateToolButtons_[0]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconToggleButton(
                                    viewportTools, EditorIcon::Scale, "Scale tool"),
                                scaleToolButtons_[0]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconToggleButton(
                                    viewportTools, EditorIcon::Paint,
                                    "Paint tiles", {}, false),
                                tilePaintToolButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconToggleButton(
                                    viewportTools, EditorIcon::Erase,
                                    "Erase tiles", {}, false),
                                tileEraseToolButton_);
        !status) {
        return status;
    }
    if (auto status = appendVerticalDivider(
            ui.tree, viewportTools, ui.productTheme.controls.iconButtonExtent);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconToggleButton(
                                    viewportTools, EditorIcon::World,
                                    "World or local transform orientation"),
                                orientationButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconToggleButton(
                                    viewportTools, EditorIcon::Snap,
                                    "Transform snapping"),
                                snapButton_);
        !status) {
        return status;
    }
    if (auto status = appendVerticalDivider(
            ui.tree, viewportTools, ui.productTheme.controls.iconButtonExtent);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(
                                    viewportTools, "Replace",
                                    fixedSize(58.0F, ui.productTheme.controls.buttonHeight)),
                                marqueeModeButtons_[0]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(
                                    viewportTools, "Add",
                                    fixedSize(40.0F, ui.productTheme.controls.buttonHeight)),
                                marqueeModeButtons_[1]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(
                                    viewportTools, "Toggle",
                                    fixedSize(52.0F, ui.productTheme.controls.buttonHeight)),
                                marqueeModeButtons_[2]);
        !status) {
        return status;
    }
    if (auto status = appendVerticalDivider(
            ui.tree, viewportTools, ui.productTheme.controls.iconButtonExtent);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    viewportTools, EditorIcon::FrameAll,
                                    "Frame all"),
                                frameAllButton_);
        !status) {
        return status;
    }
    UI::UINodeId viewportCanvas{};
    UI::UILayoutStyle viewportCanvasStyle = growingRegion();
    viewportCanvasStyle.minMax.minHeight = UI::UILayoutLength::Px(220.0F);
    viewportCanvasStyle.padding = UI::UIEdgeSpacing::All(ui.productTheme.spacing.space2);
    viewportCanvasStyle.flexContainer.gap.row = ui.productTheme.spacing.space2;
    if (auto status = storeNode(ui.createPanel(center, viewportCanvasStyle),
                                viewportCanvas);
        !status) {
        return status;
    }
    UI::UINodeId viewportCanvasTop{};
    UI::UILayoutStyle viewportCanvasTopStyle = fillWidth(22.0F);
    viewportCanvasTopStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    viewportCanvasTopStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
    if (auto status = storeNode(ui.createPanel(viewportCanvas, viewportCanvasTopStyle),
                                viewportCanvasTop);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(viewportCanvasTop,
                                            workspaceMode_ == WorkspaceMode::World2D
                                                ? "Tile Grid 1 m"
                                                : "Grid 1 m",
                                            fixedSize(160.0F, 20.0F), ui.secondaryText),
                                gridStatus_);
        !status) {
        return status;
    }
    const std::string_view initialAssetStatus =
        counters_.catalogUnresolvedReferences != 0
            ? "Catalog refs unresolved"
            : (assetResources_.projectCatalogConfigured ? "Project Catalog ready"
                                                        : "Built-in Catalog ready");
    if (auto status = storeNode(ui.createLabel(viewportCanvasTop, initialAssetStatus,
                                            fixedSize(240.0F, 20.0F), ui.accentText),
                                previewAssetStatus_);
        !status) {
        return status;
    }

    UI::UINodeId viewportSceneArea{};
    UI::UILayoutStyle viewportSceneAreaStyle = growingRegion();
    viewportSceneAreaStyle.flexContainer.justifyContent = UI::UIJustifyContent::Start;
    viewportSceneAreaStyle.flexContainer.alignItems = UI::UIAxisAlignment::Stretch;
    if (auto status = storeNode(ui.createPanel(viewportCanvas, viewportSceneAreaStyle),
                                viewportSceneArea);
        !status) {
        return status;
    }
    UI::UINodeId previewFrame{};
    UI::UILayoutStyle previewFrameStyle = growingRegion();
    previewFrameStyle.minMax.minHeight = UI::UILayoutLength::Px(180.0F);
    previewFrameStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    previewFrameStyle.flexContainer.justifyContent = UI::UIJustifyContent::Start;
    previewFrameStyle.flexContainer.alignItems = UI::UIAxisAlignment::Stretch;
    if (auto status = storeNode(ui.createPanel(viewportSceneArea, previewFrameStyle),
                                previewFrame);
        !status) {
        return status;
    }
    UI::UILayoutStyle previewWorldLayerStyle = growingRegion();
    previewWorldLayerStyle.minMax.minHeight = UI::UILayoutLength::Px(180.0F);
    previewWorldLayerStyle.placement = UI::UILayoutPlacement::Flow;
    previewWorldLayerStyle.clipDescendants = true;
    if (auto status = storeNode(ui.createPanel(previewFrame, previewWorldLayerStyle),
                                viewportPreviewLayer_);
        !status) {
        return status;
    }
    for (UI::UINodeId& gridNode : viewportGridNodes_) {
        UI::UILayoutStyle gridLineStyle = fixedSize(1.0F, 1.0F);
        gridLineStyle.placement = UI::UILayoutPlacement::Overlay;
        gridLineStyle.visibility = UI::UIVisibility::Collapsed;
        UI::UIElementDescriptor gridLine = UI::makePanelElement(gridLineStyle);
        gridLine.visual.boxPaint = UI::makeSolidBox(UI::rgb(0x7A8797, 64));
        gridLine.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
        gridLine.semantics.mode = UI::UISemanticsMode::Exclude;
        if (auto status = storeNode(
                ui.tree.createElement(viewportPreviewLayer_, gridLine), gridNode);
            !status) {
            return status;
        }
    }
    for (UI::UINodeId& gizmoNode : viewportGizmoVisualNodes_) {
        UI::UILayoutStyle gizmoStyle = fixedSize(4.0F, 4.0F);
        gizmoStyle.placement = UI::UILayoutPlacement::Overlay;
        gizmoStyle.visibility = UI::UIVisibility::Collapsed;
        UI::UIElementDescriptor gizmoPoint = UI::makePanelElement(gizmoStyle);
        gizmoPoint.visual.boxPaint = UI::makeSolidBox(UI::rgb(0xE16060, 230));
        gizmoPoint.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
        gizmoPoint.semantics.mode = UI::UISemanticsMode::Exclude;
        if (auto status = storeNode(
                ui.tree.createElement(viewportPreviewLayer_, gizmoPoint), gizmoNode);
            !status) {
            return status;
        }
    }
    UI::UILayoutStyle marqueeStyle = fixedSize(1.0F, 1.0F);
    marqueeStyle.placement = UI::UILayoutPlacement::Overlay;
    marqueeStyle.visibility = UI::UIVisibility::Collapsed;
    UI::UIElementDescriptor marquee = UI::makePanelElement(marqueeStyle);
    marquee.visual.boxPaint = UI::makeSolidBox(UI::rgb(0x4C9AFF, 52));
    marquee.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    marquee.semantics.mode = UI::UISemanticsMode::Exclude;
    if (auto status = storeNode(
            ui.tree.createElement(viewportPreviewLayer_, marquee), viewportMarqueeNode_);
        !status) {
        return status;
    }

    UI::UINodeId viewportCanvasBottom{};
    UI::UILayoutStyle viewportCanvasBottomStyle = fillWidth(22.0F);
    viewportCanvasBottomStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    viewportCanvasBottomStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
    if (auto status = storeNode(ui.createPanel(viewportCanvas, viewportCanvasBottomStyle),
                                viewportCanvasBottom);
        !status) {
        return status;
    }
    UI::UINodeId viewportOrigin{};
    if (auto status = storeNode(ui.createLabel(viewportCanvasBottom, "Origin 0, 0", fixedSize(76.0F, 20.0F),
                                            ui.secondaryText),
                                viewportOrigin);
        !status) {
        return status;
    }
    UI::UINodeId viewportExtent{};
    if (auto status = storeNode(ui.createLabel(viewportCanvasBottom, "1280 x 800 logical",
                                            fixedSize(120.0F, 20.0F), ui.secondaryText),
                                viewportExtent);
        !status) {
        return status;
    }

    UI::UINodeId viewportFooter{};
    UI::UILayoutStyle viewportFooterStyle = fillWidth(
        ui.productTheme.controls.contextToolbarHeight);
    viewportFooterStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    viewportFooterStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
    viewportFooterStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    viewportFooterStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(
        ui.productTheme.spacing.space4, ui.productTheme.spacing.space1);
    if (auto status = storeNode(ui.createSurface(center, viewportFooterStyle, UI::UISurfaceVariant::Filled),
                                viewportFooter);
        !status) {
        return status;
    }
    UI::UILayoutStyle cameraStatusStyle = fixedSize(0.0F, 22.0F);
    cameraStatusStyle.size.width = UI::UILayoutLength::Auto();
    cameraStatusStyle.flexItem.grow = 1.0F;
    cameraStatusStyle.flexItem.shrink = 1.0F;
    cameraStatusStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    if (auto status = storeNode(ui.createLabel(viewportFooter,
                                            workspaceMode_ == WorkspaceMode::World2D
                                                ? "Camera2D"
                                                : "Camera3D",
                                            cameraStatusStyle, ui.bodyText),
                                cameraStatus_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(viewportFooter, "Select | Local | Snap",
                                            fixedSize(160.0F, 22.0F), ui.secondaryText),
                                viewportToolStatus_);
        !status) {
        return status;
    }

    UI::UINodeId zoomControls{};
    UI::UILayoutStyle zoomControlsStyle = fixedSize(
        208.0F, ui.productTheme.controls.sliderHeight);
    zoomControlsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    zoomControlsStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    zoomControlsStyle.flexContainer.gap.column = ui.productTheme.spacing.space2;
    if (auto status = storeNode(ui.createPanel(viewportFooter, zoomControlsStyle),
                                zoomControls);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    zoomControls, EditorIcon::ZoomOut,
                                    "Zoom out"),
                                zoomOutButton_);
        !status) {
        return status;
    }
    UI::UIElementDescriptor zoomSliderDesc = UI::makeSliderElement(
        fixedSize(86.0F, ui.productTheme.controls.sliderHeight));
    if (auto status = storeNode(ui.tree.createElement(zoomControls, zoomSliderDesc), zoomSlider_);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setSliderRange(zoomSlider_, 25.0F, 400.0F, 25.0F); !status) {
        return status;
    }
    if (auto status = ui.tree.setSliderValue(zoomSlider_, viewportZoomPercent_); !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(zoomControls, "100%", fixedSize(46.0F, 22.0F),
                                            ui.accentText),
                                zoomValue_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    zoomControls, EditorIcon::ZoomIn,
                                    "Zoom in"),
                                zoomInButton_);
        !status) {
        return status;
    }

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildInspectorUi(
    UiBuildContext& ui, UI::UINodeId parent, UI::UINodeId& right) -> Tina::Core::Status
{
    UI::UILayoutStyle rightStyle = growingRegion();
    rightStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    rightStyle.flexContainer.gap.row = ui.productTheme.spacing.space3;
    rightStyle.padding = UI::UIEdgeSpacing::All(ui.productTheme.spacing.space4);
    if (auto status = storeNode(ui.createSurface(parent, rightStyle, UI::UISurfaceVariant::Filled),
                                right);
        !status) {
        return status;
    }

    UI::UILayoutStyle inspectorHeaderStyle = fillWidth(ui.productTheme.controls.buttonHeight);
    auto inspectorHeader = EditorPanelHeader::Build(
        ui.tree, right, ui.productTheme, "Inspector", ui.sectionText,
        inspectorHeaderStyle);
    if (!inspectorHeader) {
        return Tina::Core::failure(std::move(inspectorHeader.error()));
    }
    if (auto status = storeNode(ui.createBadge(
                                    inspectorHeader->actions, "Selection",
                                    fixedSize(88.0F, 20.0F),
                                    UI::UIBadgeTone::Accent),
                                inspectorMode_);
        !status) {
        return status;
    }

    UI::UILayoutStyle inspectorScrollStyle = growingRegion();
    inspectorScrollStyle.minMax.minHeight = UI::UILayoutLength::Px(180.0F);
    if (auto status = storeNode(
            ui.tree.createElement(
                right, UI::makeScrollViewElement(inspectorScrollStyle)),
            inspectorScroll_);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setScrollViewStyle(
            inspectorScroll_,
            UI::UIScrollViewStyle{
                .axes = UI::UIScrollAxes::Vertical,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = ui.productTheme.controls.listRowHeight,
            });
        !status) {
        return status;
    }
    counters_.inspectorScrollConfigured = true;

    UI::UINodeId inspectorContent{};
    UI::UILayoutStyle inspectorContentStyle = fillWidth(1980.0F);
    inspectorContentStyle.padding = UI::UIEdgeSpacing::All(ui.productTheme.spacing.space4);
    inspectorContentStyle.flexContainer.gap.row = ui.productTheme.spacing.space3;
    if (auto status = storeNode(ui.createSurface(inspectorScroll_, inspectorContentStyle,
                                              UI::UISurfaceVariant::Filled),
                                inspectorContent);
        !status) {
        return status;
    }

    const auto makeInspectorSectionHeaderLayout = [&]() noexcept {
        UI::UILayoutStyle layout = fillWidth(
            ui.productTheme.typography.section +
            ui.productTheme.spacing.space2);
        layout.flexItem.shrink = 0.0F;
        layout.flexContainer.direction = UI::UIFlexDirection::Row;
        layout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        layout.flexContainer.gap =
            UI::UILayoutGap::All(ui.productTheme.spacing.space3);
        return layout;
    };
    const auto appendInspectorSectionHeader =
        [&](std::string_view title, UI::UINodeId* retainedRoot,
            UI::UILayoutStyle* retainedLayout) -> Tina::Core::Status {
        UI::UILayoutStyle layout = makeInspectorSectionHeaderLayout();
        auto header = EditorSectionHeader::Build(
            ui.tree, inspectorContent, ui.productTheme, title, ui.sectionText,
            layout);
        if (!header) {
            return Tina::Core::failure(std::move(header.error()));
        }
        if (retainedRoot != nullptr) {
            *retainedRoot = header->root;
        }
        if (retainedLayout != nullptr) {
            *retainedLayout = layout;
        }
        return Tina::Core::success();
    };

    if (auto status = appendInspectorSectionHeader(
            "Identity", nullptr, nullptr);
        !status) {
        return status;
    }
    auto nameRow = EditorPropertyRow::Build(
        ui.tree, inspectorContent, ui.productTheme, "Name", ui.secondaryText,
        fillWidth(24.0F));
    if (!nameRow) {
        return Tina::Core::failure(std::move(nameRow.error()));
    }
    if (auto status = storeNode(ui.createLabel(nameRow->value, {}, fillWidth(22.0F), ui.bodyText),
                                inspectorName_);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setTextOverflow(
            inspectorName_, UI::UITextOverflow::Ellipsis);
        !status) {
        return status;
    }
    auto kindRow = EditorPropertyRow::Build(
        ui.tree, inspectorContent, ui.productTheme, "Kind", ui.secondaryText,
        fillWidth(24.0F));
    if (!kindRow) {
        return Tina::Core::failure(std::move(kindRow.error()));
    }
    if (auto status = storeNode(ui.createLabel(kindRow->value, {}, fillWidth(22.0F), ui.secondaryText),
                                inspectorKind_);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setTextOverflow(
            inspectorKind_, UI::UITextOverflow::Ellipsis);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(inspectorContent, {}, fillWidth(24.0F), ui.compactText),
                                inspectorNote_);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setTextOverflow(
            inspectorNote_, UI::UITextOverflow::Ellipsis);
        !status) {
        return status;
    }
    UI::UILayoutStyle assetRowStyle = fillWidth(42.0F);
    assetRowStyle.visibility = assetInspectorActive_
                                   ? UI::UIVisibility::Visible
                                   : UI::UIVisibility::Collapsed;
    auto assetRow = EditorPropertyRow::Build(
        ui.tree, inspectorContent, ui.productTheme, "Asset", ui.secondaryText,
        assetRowStyle);
    if (!assetRow) {
        return Tina::Core::failure(std::move(assetRow.error()));
    }
    inspectorAssetRow_ = assetRow->root;
    if (auto status = storeNode(ui.createLabel(assetRow->value, {}, fillWidth(42.0F), ui.secondaryText),
                                inspectorAssetPath_);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setTextOverflow(
            inspectorAssetPath_, UI::UITextOverflow::Ellipsis);
        !status) {
        return status;
    }
    UI::UILayoutStyle dependencySummaryStyle = fillWidth(22.0F);
    dependencySummaryStyle.flexItem.shrink = 0.0F;
    dependencySummaryStyle.visibility = assetRowStyle.visibility;
    if (auto status = storeNode(ui.createLabel(inspectorContent, {}, dependencySummaryStyle, ui.sectionText),
                                inspectorDependencySummary_);
        !status) {
        return status;
    }
    UI::UILayoutStyle dependencyListStyle = fillWidth(156.0F);
    dependencyListStyle.flexItem.shrink = 0.0F;
    dependencyListStyle.visibility = assetRowStyle.visibility;
    auto inspectorDependencies = ui.tree.createElement(
        inspectorContent,
        UI::makeListViewElement(
            {.materializedItemCapacity = 6}, dependencyListStyle));
    if (!inspectorDependencies) {
        return Tina::Core::failure(std::move(inspectorDependencies.error()));
    }
    inspectorDependencyList_ = *inspectorDependencies;
    if (auto status = ui.tree.setListViewStyle(
            inspectorDependencyList_,
            UI::UIListViewStyle{
                .rowHeight = ui.productTheme.controls.listRowHeight,
                .overscanRows = 1,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = ui.productTheme.controls.listRowHeight,
            });
        !status) {
        return status;
    }
    if (auto status = ui.tree.setListViewPaint(
            inspectorDependencyList_, UI::makeListViewPaint(ui.productTheme));
        !status) {
        return status;
    }
    if (auto status = ui.tree.setListViewDataSource(
            inspectorDependencyList_, inspectorDependencyDataSource());
        !status) {
        return status;
    }

    if (auto status = appendInspectorSectionHeader(
            "Transform", &inspectorTransformHeader_,
            &inspectorTransformHeaderLayout_);
        !status) {
        return status;
    }
    const auto createTransformField =
        [&](InspectorTransformField field, std::string_view caption, float value,
            bool enabled, UI::UINodeId& valueNode) -> Tina::Core::Status {
        UI::UILayoutStyle fieldLayout{};
        fieldLayout.size.width = UI::UILayoutLength::Percent(100.0F);
        fieldLayout.size.height = UI::UILayoutLength::Px(
            ui.productTheme.controls.textEditHeight);
        fieldLayout.flexItem.shrink = 0.0F;
        fieldLayout.flexContainer.direction = UI::UIFlexDirection::Row;
        fieldLayout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        fieldLayout.flexContainer.gap =
            UI::UILayoutGap::All(ui.productTheme.spacing.space2);
        UI::UILayoutStyle labelLayout{};
        labelLayout.size.width = UI::UILayoutLength::Px(92.0F);
        labelLayout.flexItem.shrink = 0.0F;
        UI::UILayoutStyle valueLayout{};
        valueLayout.size.width = UI::UILayoutLength::Auto();
        valueLayout.size.height = UI::UILayoutLength::Px(
            ui.productTheme.controls.textEditHeight);
        valueLayout.flexItem.grow = 1.0F;
        valueLayout.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        auto parts = ui.tree.buildNumberField(
            inspectorContent,
            UI::UINumberFieldConfig{
                .label = caption,
                .value = value,
                .valueSpec = inspectorTransformNumberSpec(field),
                .decrementAccessibleName = "Decrease transform value",
                .incrementAccessibleName = "Increase transform value",
                .labelPlacement = UI::UINumberFieldLabelPlacement::Leading,
                .layout = fieldLayout,
                .labelLayout = labelLayout,
                .textEditLayout = valueLayout,
                .enabled = enabled,
            });
        if (!parts) {
            return Tina::Core::failure(std::move(parts.error()));
        }
        valueNode = parts->textEdit;
        auto& controls = inspectorTransformNumberFields_[
            inspectorTransformFieldIndex(field)];
        controls.root = parts->root;
        controls.layout = fieldLayout;
        controls.decrementButton = parts->decrementButton;
        controls.incrementButton = parts->incrementButton;
        return Tina::Core::success();
    };
    if (auto status = createTransformField(
            InspectorTransformField::PositionX, "Position X", 0.0F, true,
            inspectorPositionX_);
        !status) {
        return status;
    }
    if (auto status = createTransformField(
            InspectorTransformField::PositionY, "Position Y", 0.0F, true,
            inspectorPositionY_);
        !status) {
        return status;
    }
    if (auto status = createTransformField(
            InspectorTransformField::PositionZ, "Position Z", 0.0F, false,
            inspectorPositionZ_);
        !status) {
        return status;
    }
    if (auto status = createTransformField(
            InspectorTransformField::RotationX, "Rotation X", 0.0F, false,
            inspectorRotationX_);
        !status) {
        return status;
    }
    if (auto status = createTransformField(
            InspectorTransformField::RotationY, "Rotation Y", 0.0F, false,
            inspectorRotationY_);
        !status) {
        return status;
    }
    if (auto status = createTransformField(
            InspectorTransformField::RotationZ, "Rotation Z", 0.0F, true,
            inspectorRotationZ_);
        !status) {
        return status;
    }
    if (auto status = createTransformField(
            InspectorTransformField::ScaleX, "Scale X", 1.0F, true,
            inspectorScaleX_);
        !status) {
        return status;
    }
    if (auto status = createTransformField(
            InspectorTransformField::ScaleY, "Scale Y", 1.0F, true,
            inspectorScaleY_);
        !status) {
        return status;
    }
    if (auto status = createTransformField(
            InspectorTransformField::ScaleZ, "Scale Z", 1.0F, false,
            inspectorScaleZ_);
        !status) {
        return status;
    }
    applyTransformButtonLayout_ = fillWidth(
        ui.productTheme.controls.buttonHeight);
    applyTransformButtonLayout_.flexContainer.justifyContent =
        UI::UIJustifyContent::Center;
    applyTransformButtonLayout_.flexContainer.alignItems =
        UI::UIAxisAlignment::Center;
    auto applyTransformButton = EditorIconButton::Build(
        ui.tree, inspectorContent, ui.productTheme, EditorIcon::Apply,
        "Apply transform", applyTransformButtonLayout_);
    if (!applyTransformButton) {
        return Tina::Core::failure(std::move(applyTransformButton.error()));
    }
    applyTransformButtonRoot_ = applyTransformButton->root;
    applyTransformButton_ = applyTransformButton->button;

    if (auto status = appendInspectorSectionHeader(
            "Components", &inspectorComponentsHeader_,
            &inspectorComponentsHeaderLayout_);
        !status) {
        return status;
    }
    const auto createComponentSection =
        [&](ComponentSectionUi& section, std::string_view name,
            std::span<const std::string_view> captions, bool withAssign,
            std::string_view applyCaption,
            bool withPointLightColor = false) -> Tina::Core::Status {
        section.rootLayout.size.width = UI::UILayoutLength::Percent(100.0F);
        section.rootLayout.flexItem.shrink = 0.0F;
        section.rootLayout.flexContainer.direction =
            UI::UIFlexDirection::Column;
        section.rootLayout.flexContainer.gap =
            UI::UILayoutGap::All(ui.productTheme.spacing.space2);
        section.indicatorLayout.size.width =
            UI::UILayoutLength::Px(ui.productTheme.controls.iconExtent);
        section.indicatorLayout.size.height =
            UI::UILayoutLength::Px(ui.productTheme.controls.iconExtent);
        section.indicatorLayout.flexItem.alignSelf = UI::UIAlignSelf::Center;
        section.contentLayout.size.width = UI::UILayoutLength::Percent(100.0F);
        section.contentLayout.flexContainer.direction = UI::UIFlexDirection::Column;
        section.contentLayout.flexContainer.gap.row = ui.productTheme.spacing.space3;
        auto collapsible = ui.tree.buildCollapsibleSection(
            inspectorContent,
            UI::UICollapsibleSectionConfig{
                .title = name,
                .collapsedIndicator = editorIconContent(EditorIcon::ChevronRight),
                .expandedIndicator = editorIconContent(EditorIcon::ChevronDown),
                .layout = section.rootLayout,
                .indicatorLayout = section.indicatorLayout,
                .contentLayout = section.contentLayout,
                .expanded = section.expanded,
            });
        if (!collapsible) {
            return Tina::Core::failure(std::move(collapsible.error()));
        }
        section.collapsible = *collapsible;

        UI::UINodeId headerRow{};
        UI::UILayoutStyle headerRowStyle = fillWidth(ui.productTheme.controls.buttonHeight);
        headerRowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        headerRowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        headerRowStyle.flexContainer.gap.column = ui.productTheme.spacing.space4;
        if (auto status = storeNode(ui.createPanel(section.collapsible.content, headerRowStyle),
                                    headerRow);
            !status) {
            return status;
        }
        UI::UIElementDescriptor switchDesc = UI::makeSwitchElement({
            .accessibleName = name,
            .size = UI::UISwitchSize::Compact,
        });
        switchDesc.enabled = false;
        if (auto status = storeNode(ui.tree.createElement(headerRow, switchDesc),
                                    section.activeSwitch);
            !status) {
            return status;
        }
        if (auto status = storeNode(ui.createIconButton(
                                        headerRow, EditorIcon::Add,
                                        "Add component", {}, false),
                                    section.addButton);
            !status) {
            return status;
        }
        if (auto status = storeNode(ui.createIconButton(
                                        headerRow, EditorIcon::Delete,
                                        "Remove component", {}, false,
                                        UI::UIButtonVariant::Danger),
                                    section.removeButton);
            !status) {
            return status;
        }
        if (withPointLightColor) {
            UI::UILayoutStyle colorFieldLayout{};
            colorFieldLayout.size.width = UI::UILayoutLength::Percent(100.0F);
            colorFieldLayout.flexItem.shrink = 0.0F;
            auto colorField = ui.tree.buildColorField(
                section.collapsible.content,
                UI::UIColorFieldConfig{
                    .label = "Color",
                    .value = pointLightColorValue_,
                    .swatchAccessibleName = "Choose PointLight2D color",
                    .layout = colorFieldLayout,
                    .enabled = false,
                });
            if (!colorField) {
                return Tina::Core::failure(std::move(colorField.error()));
            }
            pointLightColorField_ = *colorField;

            pointLightColorPickerLayout_.size.width =
                UI::UILayoutLength::Percent(100.0F);
            pointLightColorPickerLayout_.flexItem.shrink = 0.0F;
            pointLightColorPickerLayout_.visibility = UI::UIVisibility::Collapsed;
            auto colorPicker = ui.tree.buildColorPicker(
                section.collapsible.content,
                UI::UIColorPickerConfig{
                    .value = pointLightColorValue_,
                    .layout = pointLightColorPickerLayout_,
                    .includeAlpha = false,
                    .enabled = false,
                });
            if (!colorPicker) {
                return Tina::Core::failure(std::move(colorPicker.error()));
            }
            pointLightColorPicker_ = *colorPicker;
        }
        section.fieldCount = captions.size();
        for (Tina::Core::usize fieldIndex = 0; fieldIndex < captions.size(); ++fieldIndex) {
            UI::UILayoutStyle rowStyle = fillWidth(ui.productTheme.controls.textEditHeight);
            auto row = EditorPropertyRow::Build(
                ui.tree, section.collapsible.content, ui.productTheme,
                captions[fieldIndex], ui.secondaryText, rowStyle);
            if (!row) {
                return Tina::Core::failure(std::move(row.error()));
            }
            UI::UILayoutStyle valueStyle = fixedSize(
                0.0F, ui.productTheme.controls.textEditHeight);
            valueStyle.size.width = UI::UILayoutLength::Auto();
            valueStyle.flexItem.grow = 1.0F;
            valueStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
            if (auto status = storeNode(ui.createTextEdit(row->value, "n/a", valueStyle, false),
                                        section.fields[fieldIndex]);
                !status) {
                return status;
            }
        }
        if (withAssign) {
            if (auto status = storeNode(
                    ui.createButton(section.collapsible.content, "Assign Sprite From Selection",
                                 fillWidth(ui.productTheme.controls.buttonHeight), false),
                    section.assignButton);
                !status) {
                return status;
            }
        }
        if (!applyCaption.empty()) {
            if (auto status = storeNode(
                    ui.createButton(section.collapsible.content, applyCaption,
                                 fillWidth(ui.productTheme.controls.buttonHeight), false),
                    section.applyButton);
                !status) {
                return status;
            }
        }
        return Tina::Core::success();
    };
    {
        const std::array<std::string_view, 6> spriteCaptions{
            "Size X", "Size Y", "Pivot X", "Pivot Y", "Sort Layer", "Order"};
        if (auto status = createComponentSection(
                componentSections_[0], "SpriteRenderer2D", spriteCaptions, true,
                "Apply SpriteRenderer2D");
            !status) {
            return status;
        }
        const std::array<std::string_view, 3> cameraCaptions{
            "Height m", "Ref Px/m", "Ref Px H"};
        if (auto status = createComponentSection(
                componentSections_[1], "Camera2D", cameraCaptions, false,
                "Apply Camera2D");
            !status) {
            return status;
        }
        const std::array<std::string_view, 3> lightCaptions{
            "Intensity", "Radius", "Src Radius"};
        if (auto status = createComponentSection(
                componentSections_[2], "PointLight2D", lightCaptions, false,
                "Apply PointLight2D", true);
            !status) {
            return status;
        }
        const std::array<std::string_view, 4> occluderCaptions{
            "Start X", "Start Y", "End X", "End Y"};
        if (auto status = createComponentSection(
                componentSections_[3], "ShadowOccluder2D", occluderCaptions, false,
                "Apply ShadowOccluder2D");
            !status) {
            return status;
        }
        const std::array<std::string_view, 2> animationCaptions{"Clip", "Speed"};
        if (auto status = createComponentSection(
                componentSections_[4], "SpriteAnimation2D", animationCaptions, false,
                "Apply SpriteAnimation2D");
            !status) {
            return status;
        }
        const std::array<std::string_view, 2> meshCaptions{"Mesh", "Material"};
        if (auto status = createComponentSection(
                componentSections_[MeshRendererSectionIndex], "MeshRenderer3D",
                meshCaptions, false, {});
            !status) {
            return status;
        }
    }

    if (auto status = appendInspectorSectionHeader(
            "Hierarchy", &inspectorHierarchyHeaderUi_.root,
            &inspectorHierarchyHeaderUi_.layout);
        !status) {
        return status;
    }
    UI::UILayoutStyle parentRowStyle = fillWidth(ui.productTheme.controls.textEditHeight);
    parentRowStyle.flexItem.shrink = 0.0F;
    parentRowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    parentRowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    parentRowStyle.flexContainer.gap =
        UI::UILayoutGap::All(ui.productTheme.spacing.space3);
    auto parentRow = EditorPropertyRow::Build(
        ui.tree, inspectorContent, ui.productTheme, "Parent ID",
        ui.secondaryText, parentRowStyle);
    if (!parentRow) {
        return Tina::Core::failure(std::move(parentRow.error()));
    }
    inspectorHierarchyParentRowUi_ = {
        .root = parentRow->root,
        .layout = parentRowStyle,
    };
    UI::UILayoutStyle parentValueStyle = fixedSize(
        0.0F, ui.productTheme.controls.textEditHeight);
    parentValueStyle.size.width = UI::UILayoutLength::Auto();
    parentValueStyle.flexItem.grow = 1.0F;
    parentValueStyle.flexItem.shrink = 1.0F;
    parentValueStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    if (auto status = storeNode(
            ui.createTextEdit(parentRow->value, "0", parentValueStyle, false),
            inspectorParentStableId_);
        !status) {
        return status;
    }
    UI::UILayoutStyle applyParentLayout = fillWidth(
        ui.productTheme.controls.buttonHeight);
    applyParentLayout.flexContainer.justifyContent =
        UI::UIJustifyContent::Center;
    applyParentLayout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    auto applyParentButton = EditorIconButton::Build(
        ui.tree, inspectorContent, ui.productTheme, EditorIcon::Reparent,
        "Apply parent", applyParentLayout);
    if (!applyParentButton) {
        return Tina::Core::failure(std::move(applyParentButton.error()));
    }
    inspectorHierarchyApplyParentUi_ = {
        .root = applyParentButton->root,
        .layout = applyParentLayout,
    };
    reparentEntityButton_ = applyParentButton->button;

    if (auto status = appendInspectorSectionHeader(
            "TileMap", &inspectorTileMapHeaderUi_.root,
            &inspectorTileMapHeaderUi_.layout);
        !status) {
        return status;
    }
    inspectorTileMapStatusUi_.layout = fillWidth(42.0F);
    if (auto status = storeNode(ui.createLabel(inspectorContent, {},
                                            inspectorTileMapStatusUi_.layout,
                                            ui.secondaryText),
                                 tileMapStatus_);
        !status) {
        return status;
    }
    inspectorTileMapStatusUi_.root = tileMapStatus_;
    const auto createTileMapActionRow = [&](InspectorLayoutNodeUi& row)
        -> Tina::Core::Status {
        row.layout = fillWidth(ui.productTheme.controls.buttonHeight);
        row.layout.flexContainer.direction = UI::UIFlexDirection::Row;
        row.layout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        row.layout.flexContainer.gap.column = ui.productTheme.spacing.space3;
        return storeNode(ui.createPanel(inspectorContent, row.layout), row.root);
    };
    auto& tileMapBrushRow = inspectorTileMapActionRows_[0];
    if (auto status = createTileMapActionRow(tileMapBrushRow); !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    tileMapBrushRow.root, EditorIcon::Paint,
                                    "Paint cell"),
                                paintTileButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    tileMapBrushRow.root, EditorIcon::Erase,
                                    "Erase cell"),
                                eraseTileButton_);
        !status) {
        return status;
    }
    auto& tileMapLayerRow = inspectorTileMapActionRows_[1];
    if (auto status = createTileMapActionRow(tileMapLayerRow); !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    tileMapLayerRow.root, EditorIcon::World,
                                    "Toggle tile layer"),
                                toggleTileLayerButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    tileMapLayerRow.root, EditorIcon::Cook,
                                    "Cook tile map preview"),
                                cookTileMapButton_);
        !status) {
        return status;
    }
    auto& tileMapAddRow = inspectorTileMapActionRows_[2];
    if (auto status = createTileMapActionRow(tileMapAddRow); !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    tileMapAddRow.root, EditorIcon::Add,
                                    "Add tile layer"),
                                addTileLayerButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    tileMapAddRow.root, EditorIcon::Add,
                                    "Add object layer"),
                                addObjectLayerButton_);
        !status) {
        return status;
    }
    auto& tileMapGameplayRow = inspectorTileMapActionRows_[3];
    if (auto status = createTileMapActionRow(tileMapGameplayRow); !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(tileMapGameplayRow.root, "Generate Gameplay",
                                              fixedSize(104.0F, ui.productTheme.controls.buttonHeight)),
                                generateTileMapGameplayButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(tileMapGameplayRow.root, "Bake Navigation",
                                              fixedSize(104.0F, ui.productTheme.controls.buttonHeight)),
                                bakeNavigationButton_);
        !status) {
        return status;
    }

    if (auto status = appendInspectorSectionHeader(
            "Document", nullptr, nullptr);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(inspectorContent, {}, fillWidth(22.0F), ui.bodyText),
                                inspectorDocument_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(inspectorContent,
                                            workspaceMode_ == WorkspaceMode::World2D
                                                ? "World2D v1 + TileMap v3/v1 | canonical"
                                                : "Prefab schema v2 | canonical",
                                            fillWidth(20.0F), ui.secondaryText),
                                documentFormat_);
        !status) {
        return status;
    }

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildTimelineUi(
    UiBuildContext& ui, UI::UINodeId parent, UI::UINodeId& animationTimeline)
    -> Tina::Core::Status
{
    UI::UILayoutStyle animationTimelineStyle = growingRegion();
    animationTimelineStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    animationTimelineStyle.flexContainer.gap.row = ui.productTheme.spacing.space2;
    animationTimelineStyle.padding = UI::UIEdgeSpacing::All(ui.productTheme.spacing.space4);
    if (auto status = storeNode(ui.createSurface(parent, animationTimelineStyle,
                                              UI::UISurfaceVariant::Filled),
                                animationTimeline);
        !status) {
        return status;
    }

    UI::UILayoutStyle animationHeaderStyle = fillWidth(ui.productTheme.controls.buttonHeight);
    auto animationHeader = EditorPanelHeader::Build(
        ui.tree, animationTimeline, ui.productTheme, "Animation Timeline",
        ui.sectionText, animationHeaderStyle);
    if (!animationHeader) {
        return Tina::Core::failure(std::move(animationHeader.error()));
    }
    UI::UILayoutStyle animationStatusStyle = fixedSize(0.0F, 22.0F);
    animationStatusStyle.size.width = UI::UILayoutLength::Auto();
    animationStatusStyle.flexItem.grow = 1.0F;
    animationStatusStyle.flexItem.shrink = 1.0F;
    animationStatusStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    if (auto status = storeNode(ui.createLabel(animationHeader->actions, {},
                                            animationStatusStyle, ui.secondaryText),
                                animationStatus_);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setTextOverflow(
            animationStatus_, UI::UITextOverflow::Ellipsis);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationHeader->actions, "Mode: Loop",
                                             fixedSize(AnimationModeButtonWidth,
                                                       ui.productTheme.controls.buttonHeight)),
                                animationModeButton_);
        !status) {
        return status;
    }
    animationPlaybackButtons_.layout = fixedSize(
        ui.productTheme.controls.iconButtonExtent,
        ui.productTheme.controls.iconButtonExtent);
    auto playButton = EditorIconButton::Build(
        ui.tree, animationHeader->actions, ui.productTheme, EditorIcon::Play,
        "Play animation", animationPlaybackButtons_.layout);
    if (!playButton) {
        return Tina::Core::failure(std::move(playButton.error()));
    }
    animationPlaybackButtons_.play = *playButton;

    UI::UILayoutStyle pauseButtonLayout = animationPlaybackButtons_.layout;
    pauseButtonLayout.visibility = UI::UIVisibility::Collapsed;
    auto pauseButton = EditorIconButton::Build(
        ui.tree, animationHeader->actions, ui.productTheme, EditorIcon::Pause,
        "Pause animation", pauseButtonLayout, true,
        UI::UIButtonVariant::Primary);
    if (!pauseButton) {
        return Tina::Core::failure(std::move(pauseButton.error()));
    }
    animationPlaybackButtons_.pause = *pauseButton;
    if (auto status = storeNode(ui.createIconButton(
                                    animationHeader->actions, EditorIcon::Cook,
                                    "Cook animation preview"),
                                animationCookButton_);
        !status) {
        return status;
    }

    UI::UINodeId animationFrames{};
    UI::UILayoutStyle animationFramesStyle = fillWidth(
        ui.productTheme.controls.contextToolbarHeight);
    animationFramesStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    animationFramesStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    animationFramesStyle.flexContainer.gap.column = ui.productTheme.spacing.space2;
    if (auto status = storeNode(ui.createPanel(animationTimeline, animationFramesStyle),
                                animationFrames);
        !status) {
        return status;
    }

    UI::UILayoutStyle frameCommandLayout = fixedSize(
        ui.productTheme.controls.iconButtonExtent,
        ui.productTheme.controls.iconButtonExtent);
    frameCommandLayout.flexItem.shrink = 0.0F;
    if (auto status = storeNode(ui.createIconButton(
                                    animationFrames, EditorIcon::ChevronLeft,
                                    "Previous frame", frameCommandLayout),
                                animationPreviousButton_);
        !status) {
        return status;
    }
    for (u32 frameIndex = 0; frameIndex < animationFrameButtons_.size(); ++frameIndex) {
        UI::UILayoutStyle frameSlotLayout = fixedSize(
            AnimationFrameSlotWidth, ui.productTheme.controls.buttonHeight);
        frameSlotLayout.flexItem.shrink = 0.0F;
        if (auto status = storeNode(ui.createButton(animationFrames, "--",
                                                 frameSlotLayout, false,
                                                 UI::UIStyleRoleId::ButtonOutlined),
                                    animationFrameButtons_[frameIndex]);
            !status) {
            return status;
        }
    }
    if (auto status = storeNode(ui.createIconButton(
                                    animationFrames, EditorIcon::ChevronRight,
                                    "Next frame", frameCommandLayout),
                                animationNextButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    animationFrames, EditorIcon::Add,
                                    "Add frame", frameCommandLayout),
                                animationAddButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    animationFrames, EditorIcon::Duplicate,
                                    "Duplicate frame", frameCommandLayout),
                                animationDuplicateButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    animationFrames, EditorIcon::Delete,
                                    "Delete frame", frameCommandLayout, true,
                                    UI::UIButtonVariant::Danger),
                                animationDeleteButton_);
        !status) {
        return status;
    }

    UI::UINodeId animationEditRow{};
    UI::UILayoutStyle animationEditStyle = fillWidth(ui.productTheme.controls.buttonHeight);
    animationEditStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    animationEditStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    animationEditStyle.flexContainer.gap.column = ui.productTheme.spacing.space3;
    if (auto status = storeNode(ui.createPanel(animationTimeline, animationEditStyle),
                                animationEditRow);
        !status) {
        return status;
    }
    UI::UILayoutStyle animationSelectionStyle = fixedSize(0.0F, 22.0F);
    animationSelectionStyle.size.width = UI::UILayoutLength::Auto();
    animationSelectionStyle.flexItem.grow = 1.0F;
    animationSelectionStyle.flexItem.shrink = 1.0F;
    animationSelectionStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    if (auto status = storeNode(ui.createLabel(animationEditRow, {}, animationSelectionStyle,
                                            ui.accentText),
                                animationSelection_);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setTextOverflow(
            animationSelection_, UI::UITextOverflow::Ellipsis);
        !status) {
        return status;
    }
    if (auto status = appendVerticalDivider(
            ui.tree, animationEditRow, ui.productTheme.controls.iconButtonExtent);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    animationEditRow, EditorIcon::Refresh,
                                    "Use next sprite"),
                                animationCycleSpriteButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    animationEditRow, EditorIcon::MoveLeft,
                                    "Move frame left"),
                                animationMoveLeftButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    animationEditRow, EditorIcon::MoveRight,
                                    "Move frame right"),
                                animationMoveRightButton_);
        !status) {
        return status;
    }
    if (auto status = appendVerticalDivider(
            ui.tree, animationEditRow, ui.productTheme.controls.iconButtonExtent);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    animationEditRow, EditorIcon::ZoomOut,
                                    "Decrease frame duration"),
                                animationDurationDecreaseButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    animationEditRow, EditorIcon::ZoomIn,
                                    "Increase frame duration"),
                                animationDurationIncreaseButton_);
        !status) {
        return status;
    }
    UI::UINodeId animationEventRow{};
    UI::UILayoutStyle animationEventStyle = fillWidth(ui.productTheme.controls.textEditHeight);
    animationEventStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    animationEventStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    animationEventStyle.flexContainer.gap.column = ui.productTheme.spacing.space3;
    if (auto status = storeNode(ui.createPanel(animationTimeline, animationEventStyle),
                                animationEventRow);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(animationEventRow, "Notify 0/0",
                                            fixedSize(84.0F, 22.0F), ui.secondaryText),
                                animationEventPosition_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    animationEventRow, EditorIcon::ChevronLeft,
                                    "Previous event"),
                                animationEventPreviousButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    animationEventRow, EditorIcon::ChevronRight,
                                    "Next event"),
                                animationEventNextButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createTextEdit(animationEventRow, "footstep",
                                               fixedSize(116.0F, ui.productTheme.controls.textEditHeight), true),
                                animationEventTag_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createTextEdit(animationEventRow, "50%",
                                               fixedSize(72.0F, ui.productTheme.controls.textEditHeight), true),
                                animationEventOffset_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    animationEventRow, EditorIcon::Add,
                                    "Add event"),
                                animationEventAddButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    animationEventRow, EditorIcon::Apply,
                                    "Apply event", {}, false),
                                animationEventApplyButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    animationEventRow, EditorIcon::Delete,
                                    "Remove event", {}, false,
                                    UI::UIButtonVariant::Danger),
                                animationEventRemoveButton_);
        !status) {
        return status;
    }

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildStatusBarUi(UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    UI::UINodeId statusBar{};
    UI::UILayoutStyle statusBarStyle = fillWidth(ui.productTheme.controls.statusBarHeight);
    statusBarStyle.flexItem.shrink = 0.0F;
    statusBarStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    statusBarStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    statusBarStyle.flexContainer.gap.column = ui.productTheme.spacing.space5;
    statusBarStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(
        ui.productTheme.spacing.space5, ui.productTheme.spacing.space0);
    if (auto status = storeNode(ui.createSurface(parent, statusBarStyle, UI::UISurfaceVariant::Filled),
                                statusBar);
        !status) {
        return status;
    }
    UI::UILayoutStyle statusDocumentStyle = fixedSize(0.0F, 20.0F);
    statusDocumentStyle.size.width = UI::UILayoutLength::Auto();
    statusDocumentStyle.flexItem.grow = 0.8F;
    statusDocumentStyle.flexItem.shrink = 1.0F;
    statusDocumentStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    if (auto status = storeNode(ui.createLabel(statusBar, {}, statusDocumentStyle, ui.compactText),
                                statusDocument_);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setTextOverflow(statusDocument_, UI::UITextOverflow::Ellipsis);
        !status) {
        return status;
    }
    UI::UILayoutStyle statusPreviewStyle = statusDocumentStyle;
    statusPreviewStyle.flexItem.grow = 1.2F;
    if (auto status = storeNode(ui.createLabel(statusBar, {}, statusPreviewStyle, ui.secondaryText),
                                statusPreview_);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setTextOverflow(
            statusPreview_, UI::UITextOverflow::Ellipsis);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.tree.createElement(
                                    statusBar,
                                    UI::makeBadgeElement(
                                        {}, {.tone = UI::UIBadgeTone::Accent},
                                        fixedSize(190.0F, 20.0F))),
                                statusSelection_);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setTextOverflow(
            statusSelection_, UI::UITextOverflow::Ellipsis);
        !status) {
        return status;
    }

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildDirtyCloseModalUi(UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    if (auto status = storeNode(
            ui.tree.createElement(
                parent,
                UI::makeModalElement(
                    dirtyCloseModalLayout(UI::UIVisibility::Collapsed))),
            dirtyCloseModal_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createLabel(dirtyCloseModal_, "Save changes before closing?",
                        fillWidth(28.0F), ui.sectionText),
            dirtyCloseTitle_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createLabel(dirtyCloseModal_,
                        "The current canonical document has unsaved changes.",
                        fillWidth(24.0F), ui.bodyText),
            dirtyCloseMessage_);
        !status) {
        return status;
    }
    UI::UILayoutStyle dirtyClosePathFieldLayout{};
    dirtyClosePathFieldLayout.size.width = UI::UILayoutLength::Percent(100.0F);
    auto dirtyClosePathField = ui.tree.buildFormField(
        dirtyCloseModal_,
        UI::UIFormFieldConfig{
            .label = "Document path",
            .value = {},
            .layout = dirtyClosePathFieldLayout,
            .textEditLayout =
                fillWidth(ui.productTheme.controls.textEditHeight),
            .enabled = true,
        });
    if (!dirtyClosePathField) {
        return Tina::Core::failure(std::move(dirtyClosePathField.error()));
    }
    dirtyClosePathInput_ = dirtyClosePathField->textEdit;
    UI::UINodeId dirtyCloseActions{};
    UI::UILayoutStyle dirtyCloseActionsStyle = fillWidth(ui.productTheme.controls.buttonHeight);
    dirtyCloseActionsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    dirtyCloseActionsStyle.flexContainer.justifyContent =
        UI::UIJustifyContent::End;
    dirtyCloseActionsStyle.flexContainer.gap.column = ui.productTheme.spacing.space4;
    if (auto status = storeNode(
            ui.createPanel(dirtyCloseModal_, dirtyCloseActionsStyle),
            dirtyCloseActions);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(dirtyCloseActions, "Save",
                            fixedSize(86.0F, ui.productTheme.controls.buttonHeight), true,
                            UI::UIStyleRoleId::ButtonPrimary),
            dirtyCloseSaveButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(dirtyCloseActions, "Discard",
                            fixedSize(86.0F, ui.productTheme.controls.buttonHeight), true,
                            UI::UIStyleRoleId::ButtonDanger),
            dirtyCloseDiscardButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(dirtyCloseActions, "Cancel",
                            fixedSize(86.0F, ui.productTheme.controls.buttonHeight), true,
                            UI::UIStyleRoleId::ButtonText),
            dirtyCloseCancelButton_);
        !status) {
        return status;
    }

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildSceneDeleteDialogUi(
    UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    constexpr std::array actions{
        UI::UIDialogActionConfig{
            .text = "Cancel",
            .variant = UI::UIButtonVariant::Text,
        },
        UI::UIDialogActionConfig{
            .text = "Delete",
            .variant = UI::UIButtonVariant::Danger,
        },
    };
    auto dialog = ui.tree.buildDialog(
        parent,
        UI::UIDialogConfig{
            .title = "Delete scene subtree?",
            .body = "The selected item and its descendants will be deleted.\nThis action can be undone.",
            .actions = actions,
            .layout = sceneDeleteDialogLayout(UI::UIVisibility::Collapsed),
            .surfaceLayout = sceneDeleteDialogSurfaceLayout(ui.productTheme),
        });
    if (!dialog) {
        return Tina::Core::failure(std::move(dialog.error()));
    }
    sceneDeleteDialog_ = *dialog;
    return Tina::Core::success();
}

auto EditorWorkspaceState::buildSnackbarUi(
    UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    UI::UISnackbarHostConfig config{};
    config.layout = percentSize(100.0F, 100.0F);
    config.surfaceLayout.size.width = UI::UILayoutLength::Px(520.0F);
    config.surfaceLayout.minMax.maxWidth = UI::UILayoutLength::Percent(100.0F);
    config.viewportMargin = ui.productTheme.spacing.space6;

    auto host = UI::UISnackbarHost::Create(config);
    if (!host) {
        return Tina::Core::failure(std::move(host.error()));
    }
    auto parts = ui.tree.buildSnackbarHost(parent, config);
    if (!parts) {
        return Tina::Core::failure(std::move(parts.error()));
    }
    if (auto status = ui.tree.setTextOverflow(
            parts->message, UI::UITextOverflow::Ellipsis);
        !status) {
        return status;
    }

    snackbarRootLayout_ = config.layout;
    snackbarRootLayout_.placement = UI::UILayoutPlacement::Overlay;
    snackbarRootLayout_.overlay.horizontal = UI::UIAxisAlignment::Stretch;
    snackbarRootLayout_.overlay.vertical = UI::UIAxisAlignment::Stretch;
    snackbarRootLayout_.visibility = UI::UIVisibility::Collapsed;
    snackbarActionLayout_ = fixedSize(
        0.0F, ui.productTheme.controls.buttonHeight);
    snackbarActionLayout_.size.width = UI::UILayoutLength::Auto();
    snackbarActionLayout_.padding = UI::UIEdgeSpacing::HorizontalVertical(
        ui.productTheme.spacing.space4, ui.productTheme.spacing.space2);
    snackbarActionLayout_.visibility = UI::UIVisibility::Collapsed;
    snackbarToneColors_ = {
        ui.productTheme.colors.onSurfaceVariant,
        ui.productTheme.colors.success,
        ui.productTheme.colors.warning,
        ui.productTheme.colors.error,
    };
    snackbarHost_.emplace(std::move(*host));
    snackbarParts_ = *parts;

    const UI::UITransitionSpec opacitySnap{
        .property = UI::UIAnimatableProperty::Opacity,
        .duration = Tina::Core::Duration{0.0},
    };
    if (auto status = ui.tree.beginOpacityTransition(
            snackbarParts_.surface, 0.0F, opacitySnap);
        !status) {
        return status;
    }
    const UI::UITransitionSpec offsetSnap{
        .property = UI::UIAnimatableProperty::VisualOffset,
        .duration = Tina::Core::Duration{0.0},
    };
    return ui.tree.beginVisualOffsetTransition(
        snackbarParts_.surface, 0.0F, 8.0F, offsetSnap);
}

auto EditorWorkspaceState::registerUiCallbacks(UiBuildContext& ui) -> Tina::Core::Status
{
    if (auto status = ui.tree.setButtonAction(
            viewportMode_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::ViewportCyclePreset);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            applyTransformButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::ApplyTransform);
            }});
        !status) {
        return status;
    }
    for (Tina::Core::usize index = 0;
         index < inspectorTransformNumberFields_.size(); ++index) {
        const auto field = static_cast<InspectorTransformField>(index);
        const auto& controls = inspectorTransformNumberFields_[index];
        if (auto status = ui.tree.setButtonAction(
                controls.decrementButton,
                UI::UIButtonActionCallback{
                    [this, field](const UI::UIButtonActionEvent&) noexcept {
                        pendingInspectorTransformStep_ =
                            InspectorTransformStepRequest{field, -1};
                    }});
            !status) {
            return status;
        }
        if (auto status = ui.tree.setButtonAction(
                controls.incrementButton,
                UI::UIButtonActionCallback{
                    [this, field](const UI::UIButtonActionEvent&) noexcept {
                        pendingInspectorTransformStep_ =
                            InspectorTransformStepRequest{field, 1};
                    }});
            !status) {
            return status;
        }
    }
    {
        const std::array<std::array<EditorCommand, 3>, 6> sectionCommands{{
            {EditorCommand::ComponentAddSprite, EditorCommand::ComponentRemoveSprite,
             EditorCommand::ComponentToggleSpriteVisible},
            {EditorCommand::ComponentAddCamera, EditorCommand::ComponentRemoveCamera,
             EditorCommand::ComponentToggleCameraActive},
            {EditorCommand::ComponentAddPointLight, EditorCommand::ComponentRemovePointLight,
             EditorCommand::ComponentTogglePointLightActive},
            {EditorCommand::ComponentAddShadowOccluder, EditorCommand::ComponentRemoveShadowOccluder,
             EditorCommand::ComponentToggleShadowOccluderActive},
            {EditorCommand::ComponentAddSpriteAnimation, EditorCommand::ComponentRemoveSpriteAnimation,
             EditorCommand::ComponentToggleSpriteAnimationAutoPlay},
            {EditorCommand::ComponentAddMeshRenderer, EditorCommand::ComponentRemoveMeshRenderer,
             EditorCommand::ComponentToggleMeshVisible},
        }};
        const std::array<EditorCommand, 5> sectionApplyCommands{
            EditorCommand::ComponentApplySprite, EditorCommand::ComponentApplyCamera,
            EditorCommand::ComponentApplyPointLight,
            EditorCommand::ComponentApplyShadowOccluder,
            EditorCommand::ComponentApplySpriteAnimation};
        for (Tina::Core::usize sectionIndex = 0;
             sectionIndex < componentSections_.size(); ++sectionIndex) {
            const auto& section = componentSections_[sectionIndex];
            const auto commands = sectionCommands[sectionIndex];
            if (auto status = ui.tree.setCheckboxAction(
                    section.collapsible.header,
                    UI::UIButtonActionCallback{
                        [this, sectionIndex](const UI::UIButtonActionEvent&) noexcept {
                            componentSections_[sectionIndex]
                                .collapseUpdatePending = true;
                        }});
                !status) {
                return status;
            }
            if (auto status = ui.tree.setButtonAction(
                    section.addButton,
                    UI::UIButtonActionCallback{[this, command = commands[0]](
                                                   const UI::UIButtonActionEvent&) noexcept {
                        queueEditorCommand(command);
                    }});
                !status) {
                return status;
            }
            if (auto status = ui.tree.setButtonAction(
                    section.removeButton,
                    UI::UIButtonActionCallback{[this, command = commands[1]](
                                                   const UI::UIButtonActionEvent&) noexcept {
                        queueEditorCommand(command);
                    }});
                !status) {
                return status;
            }
            if (auto status = ui.tree.setCheckboxAction(
                    section.activeSwitch,
                    UI::UIButtonActionCallback{[this, command = commands[2]](
                                                   const UI::UIButtonActionEvent&) noexcept {
                        queueEditorCommand(command);
                    }});
                !status) {
                return status;
            }
            if (sectionIndex < sectionApplyCommands.size()) {
                if (auto status = ui.tree.setButtonAction(
                        section.applyButton,
                        UI::UIButtonActionCallback{[this, command = sectionApplyCommands[sectionIndex]](
                                                       const UI::UIButtonActionEvent&) noexcept {
                            queueEditorCommand(command);
                        }});
                    !status) {
                    return status;
                }
            }
        }
        if (auto status = ui.tree.setButtonAction(
                componentSections_[0].assignButton,
                UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::ComponentAssignSprite);
                }});
            !status) {
            return status;
        }
    }
    if (auto status = ui.tree.setButtonAction(
            undoButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::Undo);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            redoButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::Redo);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            saveButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::Save);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            saveAsButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::SaveAs);
            }});
        !status) {
        return status;
    }
    const auto bindEditorCommand = [&](UI::UINodeId button,
                                       EditorCommand command) -> Tina::Core::Status {
        return ui.tree.setButtonAction(
            button, UI::UIButtonActionCallback{[this, command](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(command);
            }});
    };
    const std::array sceneCommandBindings{
        std::pair{addEntityButton_, EditorCommand::SceneAdd},
        std::pair{duplicateEntityButton_, EditorCommand::SceneDuplicate},
        std::pair{deleteEntityButton_, EditorCommand::SceneDelete},
        std::pair{reparentEntityButton_, EditorCommand::SceneReparent},
        std::pair{focusEntityButton_, EditorCommand::SceneFocus},
    };
    for (const auto& [button, command] : sceneCommandBindings) {
        if (auto status = bindEditorCommand(button, command); !status) {
            return status;
        }
    }
    const std::array playCommandBindings{
        std::pair{playButton_, EditorCommand::PlayStartOrResume},
        std::pair{pauseButton_, EditorCommand::PlayPause},
        std::pair{stepButton_, EditorCommand::PlayStep},
        std::pair{stopButton_, EditorCommand::PlayStop},
    };
    for (const auto& [button, command] : playCommandBindings) {
        if (auto status = bindEditorCommand(button, command); !status) {
            return status;
        }
    }
    if (auto status = bindEditorCommand(paintTileButton_, EditorCommand::PaintTile); !status) {
        return status;
    }
    if (auto status = bindEditorCommand(eraseTileButton_, EditorCommand::EraseTile); !status) {
        return status;
    }
    if (auto status = bindEditorCommand(toggleTileLayerButton_, EditorCommand::ToggleTileLayer); !status) {
        return status;
    }
    if (auto status = bindEditorCommand(addTileLayerButton_, EditorCommand::AddTileLayer); !status) {
        return status;
    }
    if (auto status = bindEditorCommand(addObjectLayerButton_, EditorCommand::AddObjectLayer); !status) {
        return status;
    }
    if (auto status = bindEditorCommand(cookTileMapButton_, EditorCommand::CookTileMapPreview); !status) {
        return status;
    }
    if (auto status = bindEditorCommand(bakeNavigationButton_,
                                        EditorCommand::BakeNavigation2D);
        !status) {
        return status;
    }
    if (auto status = bindEditorCommand(generateTileMapGameplayButton_,
                                        EditorCommand::GenerateTileMapGameplay);
        !status) {
        return status;
    }
    const std::array animationCommandBindings{
        std::pair{animationPlaybackButtons_.play.button,
                  EditorCommand::AnimationTogglePlayback},
        std::pair{animationPlaybackButtons_.pause.button,
                  EditorCommand::AnimationTogglePlayback},
        std::pair{animationPreviousButton_, EditorCommand::AnimationPreviousFrame},
        std::pair{animationNextButton_, EditorCommand::AnimationNextFrame},
        std::pair{animationAddButton_, EditorCommand::AnimationAddFrame},
        std::pair{animationDuplicateButton_, EditorCommand::AnimationDuplicateFrame},
        std::pair{animationDeleteButton_, EditorCommand::AnimationDeleteFrame},
        std::pair{animationMoveLeftButton_, EditorCommand::AnimationMoveFrameLeft},
        std::pair{animationMoveRightButton_, EditorCommand::AnimationMoveFrameRight},
        std::pair{animationCycleSpriteButton_, EditorCommand::AnimationCycleSprite},
        std::pair{animationDurationDecreaseButton_, EditorCommand::AnimationDecreaseDuration},
        std::pair{animationDurationIncreaseButton_, EditorCommand::AnimationIncreaseDuration},
        std::pair{animationEventPreviousButton_, EditorCommand::AnimationPreviousEvent},
        std::pair{animationEventNextButton_, EditorCommand::AnimationNextEvent},
        std::pair{animationEventAddButton_, EditorCommand::AnimationAddEvent},
        std::pair{animationEventApplyButton_, EditorCommand::AnimationApplyEvent},
        std::pair{animationEventRemoveButton_, EditorCommand::AnimationRemoveEvent},
        std::pair{animationModeButton_, EditorCommand::AnimationCycleMode},
        std::pair{animationCookButton_, EditorCommand::AnimationCookPreview},
    };
    for (const auto& [button, command] : animationCommandBindings) {
        if (auto status = bindEditorCommand(button, command); !status) {
            return status;
        }
    }
    for (u32 frameIndex = 0; frameIndex < animationFrameButtons_.size(); ++frameIndex) {
        if (auto status = ui.tree.setButtonAction(
                animationFrameButtons_[frameIndex],
                UI::UIButtonActionCallback{
                    [this, frameIndex](const UI::UIButtonActionEvent&) noexcept {
                        animationPreview_.queueFrameSelection(frameIndex);
                    }});
            !status) {
            return status;
        }
    }
    for (const UI::UINodeId button : selectToolButtons_) {
        if (auto status = ui.tree.setButtonAction(
                button, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueViewportToolMode(ViewportToolMode::Select);
                }});
            !status) {
            return status;
        }
    }
    for (const UI::UINodeId button : translateToolButtons_) {
        if (auto status = ui.tree.setButtonAction(
                button, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueViewportToolMode(ViewportToolMode::Translate);
                }});
            !status) {
            return status;
        }
    }
    for (const UI::UINodeId button : rotateToolButtons_) {
        if (auto status = ui.tree.setButtonAction(
                button, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueViewportToolMode(ViewportToolMode::Rotate);
                }});
            !status) {
            return status;
        }
    }
    for (const UI::UINodeId button : scaleToolButtons_) {
        if (auto status = ui.tree.setButtonAction(
                button, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueViewportToolMode(ViewportToolMode::Scale);
                }});
            !status) {
            return status;
        }
    }
    if (auto status = ui.tree.setButtonAction(
            tilePaintToolButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueViewportToolMode(ViewportToolMode::TilePaint);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            tileEraseToolButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueViewportToolMode(ViewportToolMode::TileErase);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setSliderChangeCallback(
            zoomSlider_,
            UI::UISliderChangeCallback{
                [this](const UI::UISliderChangeEvent& event) noexcept {
                    pendingViewportZoomPercent_ =
                        std::clamp(event.value, 25.0F, 400.0F);
                }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            zoomOutButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueViewportZoomStep(-25.0F);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            zoomInButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueViewportZoomStep(25.0F);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            frameAllButton_,
            UI::UIButtonActionCallback{
                [this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::ViewportResetView);
                }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            orientationButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                pendingGizmoOrientationToggle_ = true;
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            snapButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                pendingGizmoSnapToggle_ = true;
            }});
        !status) {
        return status;
    }
    constexpr std::array marqueeModes{
        Tina::Editor::EditorMarqueeSelectionMode::Replace,
        Tina::Editor::EditorMarqueeSelectionMode::Add,
        Tina::Editor::EditorMarqueeSelectionMode::Toggle,
    };
    for (u32 index = 0; index < marqueeModeButtons_.size(); ++index) {
        if (auto status = ui.tree.setButtonAction(
                marqueeModeButtons_[index],
                UI::UIButtonActionCallback{
                    [this, mode = marqueeModes[index]](
                        const UI::UIButtonActionEvent&) noexcept {
                        pendingMarqueeSelectionMode_ = mode;
                    }});
            !status) {
            return status;
        }
    }
    const std::array projectFilterCommands{
        EditorCommand::ProjectFilterAll,
        EditorCommand::ProjectFilter2D,
        EditorCommand::ProjectFilter3D,
        EditorCommand::ProjectFilterMedia,
    };
    for (u32 index = 0; index < projectFilterButtons_.size(); ++index) {
        if (auto status = ui.tree.setButtonAction(
                projectFilterButtons_[index],
                UI::UIButtonActionCallback{
                    [this, command = projectFilterCommands[index]](
                        const UI::UIButtonActionEvent&) noexcept {
                        queueEditorCommand(command);
                    }});
            !status) {
            return status;
        }
    }
    if (auto status = ui.tree.setButtonAction(
            openProjectAssetButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::OpenSelectedProjectAsset);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            refreshProjectCatalogButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::RefreshProjectCatalog);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            createProjectButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::CreateProject);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            openProjectButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::OpenProject);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            importSourceButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::ImportSource);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            removeSourceImportButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::RemoveSelectedSourceImport);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            closeDocumentButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::CloseActiveDocument);
            }});
        !status) {
        return status;
    }
    const std::array dirtyCloseBindings{
        std::pair{dirtyCloseSaveButton_, EditorCommand::DirtyCloseSave},
        std::pair{dirtyCloseDiscardButton_, EditorCommand::DirtyCloseDiscard},
        std::pair{dirtyCloseCancelButton_, EditorCommand::DirtyCloseCancel},
    };
    for (const auto& [button, command] : dirtyCloseBindings) {
        if (auto status = ui.tree.setButtonAction(
                button,
                UI::UIButtonActionCallback{
                    [this, command](const UI::UIButtonActionEvent&) noexcept {
                        queueEditorCommand(command);
                    }});
            !status) {
            return status;
        }
        if (auto status = ui.tree.setButtonAction(
                pointLightColorField_.swatchButton,
                UI::UIButtonActionCallback{
                    [this](const UI::UIButtonActionEvent&) noexcept {
                        pendingPointLightColorPickerToggle_ = true;
                    }});
            !status) {
            return status;
        }
        for (Tina::Core::usize index = 0;
             index < pointLightColorPicker_.channelCount; ++index) {
            if (auto status = ui.tree.setSliderChangeCallback(
                    pointLightColorPicker_.channelSliders[index],
                    UI::UISliderChangeCallback{
                        [this, channel = static_cast<UI::UIColorPickerChannel>(index)](
                            const UI::UISliderChangeEvent& event) noexcept {
                            pendingPointLightColorChannel_ =
                                InspectorPointLightColorChannelRequest{
                                    .channel = channel,
                                    .value = event.value,
                                };
                        }});
                !status) {
                return status;
            }
        }
    }
    if (auto status = ui.tree.setButtonAction(
            sceneDeleteDialog_.actions[SceneDeleteConfirmActionIndex],
            UI::UIButtonActionCallback{
                [this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::SceneDeleteConfirm);
                }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            sceneDeleteDialog_.actions[SceneDeleteCancelActionIndex],
            UI::UIButtonActionCallback{
                [this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::SceneDeleteCancel);
                }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            snackbarParts_.action,
            UI::UIButtonActionCallback{
                [this](const UI::UIButtonActionEvent&) noexcept {
                    if (!snackbarHost_.has_value()) {
                        return;
                    }
                    const auto action = snackbarHost_->activateAction(
                        snackbarClock_.now());
                    if (action == SnackbarUndoActionToken) {
                        queueEditorCommand(EditorCommand::Undo);
                    }
                }});
        !status) {
        return status;
    }
    for (u32 index = 0; index < documentTabButtons_.size(); ++index) {
        if (auto status = ui.tree.setButtonAction(
                documentTabButtons_[index],
                UI::UIButtonActionCallback{
                    [this, index](const UI::UIButtonActionEvent&) noexcept {
                        pendingDocumentTabActivation_ = index;
                    }});
            !status) {
            return status;
        }
    }
    if (auto status = ui.tree.setPointerHitPolicy(viewportPreviewLayer_,
                                                UI::UIPointerHitPolicy::Targetable);
        !status) {
        return status;
    }
    if (auto status = registerViewportPointerListeners(ui.tree); !status) {
        return status;
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::onEnter(Tina::GameStateEnterContext& context) -> Tina::Core::Status{
    ++counters_.stateEnters;
    auto playSession = Tina::Editor::EditorPlaySession::Create();
    if (!playSession) {
        return Tina::Core::failure(std::move(playSession.error()));
    }
    playSession_.emplace(std::move(*playSession));
    if (auto status = initializePinnedDocumentSessions(); !status) {
        return status;
    }
    if (auto status = rebuildHierarchyModel(); !status) {
        return status;
    }
    auto assetRollback = Tina::Core::makeScopeExit([this]() noexcept {
        releasePreviewAssetBindings();
    });
    if (auto status = preparePreviewAssetBindings(); !status) {
        return status;
    }

    auto rootBuilder = context.primaryWindowUIRootBuilder();
    if (!rootBuilder) {
        return Tina::Core::failure(std::move(rootBuilder.error()));
    }

    Tina::Render::IRenderDevice* renderDevice = renderDeviceAccess_.get();
    if (renderDevice == nullptr) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Editor icon atlas requires the active render device");
    }
    if (auto status = iconResources_.initialize(*renderDevice); !status) {
        return status;
    }
    auto iconRollback = Tina::Core::makeScopeExit([this]() noexcept {
        iconResources_.release();
    });

    const UI::UITheme productTheme = makeEditorProductTheme();
    if (auto status = rootBuilder->setProductTheme(productTheme); !status) {
        return status;
    }

    auto root = rootBuilder->createRoot();
    if (!root) {
        return Tina::Core::failure(std::move(root.error()));
    }
    auto iconResolverRegistration = rootBuilder->bindImageResolver(
        *root, iconResources_.resolver());
    if (!iconResolverRegistration) {
        return Tina::Core::failure(
            std::move(iconResolverRegistration.error()));
    }
    auto tree = rootBuilder->treeUpdater(*root);
    if (!tree) {
        return Tina::Core::failure(std::move(tree.error()));
    }
    if (auto status = tree->setStyleBackgroundColorTransition(
            UI::UITransitionSpec{
                .property = UI::UIAnimatableProperty::BackgroundColor,
                .duration = Tina::Core::Duration{0.09},
                .easing = UI::UIEasing::EaseOut,
            });
        !status) {
        return status;
    }

    UiBuildContext ui{
        .tree = *tree,
        .productTheme = productTheme,
        .titleText = UI::makeTitleTextStyle(productTheme),
        .sectionText = UI::makeSectionTextStyle(productTheme),
        .bodyText = UI::makeBodyTextStyle(productTheme),
        .compactText = UI::makeBodyTextStyle(productTheme, productTheme.typography.control),
        .secondaryText = UI::makeCaptionTextStyle(productTheme),
        .accentText = makeEditorInfoTextStyle(
            productTheme, productTheme.typography.body),
    };
    UI::UILayoutStyle rootStyle = percentSize(100.0F, 100.0F);
    rootStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    rootStyle.flexContainer.gap = UI::UILayoutGap::All(0.0F);
    if (auto status = tree->setLayoutStyle(root->rootNodeId(), rootStyle); !status) {
        return status;
    }

    const UI::UINodeId rootNode = root->rootNodeId();
    if (auto status = buildCommandBarUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildDocumentTabsUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildWorkspaceUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildStatusBarUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildSnackbarUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildDirtyCloseModalUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildSceneDeleteDialogUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = registerUiCallbacks(ui); !status) {
        return status;
    }
    counters_.editorActionsReady = true;
    counters_.editorLayoutRegions = EditorLayoutRegionCount;
    counters_.viewportLayoutReady = true;
    counters_.projectAssetBrowserReady = true;
    counters_.documentTabsReady = true;
    counters_.projectAssetVisibleItems = projectAssets_.visibleItemCount();
    counters_.documentTabCount = documentTabs_.tabCount();
    auto initialSelection = tree->treeViewSelection(hierarchyTree_);
    if (!initialSelection) {
        return Tina::Core::failure(std::move(initialSelection.error()));
    }
    selectionKey_ = initialSelection->key;
    synchronizeViewportSelectionFromHierarchy();
    if (auto status = rebuildAnimationAnimator(); !status) {
        return status;
    }
    if (auto status = validateRuntimePreview(); !status) {
        return status;
    }
    if (auto status = refreshAuthoringUi(*tree); !status) {
        return status;
    }
    counters_.finalSelectionKey = selectionKey_;
    counters_.finalSelectionIndex = initialSelection->logicalIndex;
    counters_.hierarchyLogicalItems = hierarchyItemCount(this);
    counters_.selectionVerified = true;

    lastSnackbarFeedback_ = authoringFeedback_;
    iconResolverRegistration_ = std::move(*iconResolverRegistration);
    uiRoot_ = std::move(*root);
    ++counters_.uiRootsCreated;
    iconRollback.release();
    assetRollback.release();
    return Tina::Core::success();
}

} // namespace Tina::EditorApp::WorkspaceInternal
