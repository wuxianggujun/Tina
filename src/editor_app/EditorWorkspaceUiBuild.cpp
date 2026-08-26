#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

inline constexpr float LeftDockMinWidth = 200.0F;
inline constexpr float ViewportMinWidth = 480.0F;
inline constexpr float InspectorMinWidth = 360.0F;
inline constexpr float ViewportMinHeight = 320.0F;
inline constexpr float TimelineMinHeight = 160.0F;
inline constexpr float AnimationModeButtonWidth = 120.0F;
inline constexpr float AnimationFrameSlotWidth = 44.0F;
inline constexpr float InspectorValueInputMaxWidth = 132.0F;
inline constexpr float InspectorTransformLabelWidth = 52.0F;
inline constexpr float InspectorTransformAxisMinWidth = 52.0F;
inline constexpr float InspectorTransformAxisMaxWidth = 96.0F;
inline constexpr float InspectorTransformAxisLabelWidth = 12.0F;
inline constexpr u64 SnackbarUndoActionToken = 1U;

struct InspectorNodePropertyFieldRow final {
    std::string_view caption{};
    std::array<std::string_view, 2> axisLabels{};
    std::array<std::string_view, 2> accessibleNames{};
    Tina::Core::usize valueCount = 1U;
    float axisLabelWidth = 0.0F;
};

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
    theme.controls.splitterHitExtent = 10.0F;
    theme.controls.splitterLineThickness = theme.controls.splitterHitExtent;
    theme.controls.panelShadowOffsetX = 0.0F;
    theme.controls.panelShadowOffsetY = 2.0F;
    theme.elevations.raisedOffsetY = 1.0F;
    theme.elevations.floatingOffsetY = 3.0F;
    theme.elevations.modalOffsetY = 5.0F;
    return theme;
}

[[nodiscard]] UI::UILayoutStyle editorDialogSurfaceLayout(
    const UI::UITheme& theme) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(theme.controls.dialogMinWidth);
    style.minMax.maxWidth = UI::UILayoutLength::Percent(100.0F);
    style.padding = UI::UIEdgeSpacing::All(theme.spacing.space8);
    style.flexContainer.gap = UI::UILayoutGap::All(theme.spacing.space6);
    return style;
}

[[nodiscard]] UI::UILayoutStyle inspectorValueInputLayout(
    const UI::UITheme& theme) noexcept
{
    UI::UILayoutStyle layout{};
    layout.size.width = UI::UILayoutLength::Auto();
    layout.size.height = UI::UILayoutLength::Px(
        theme.controls.textEditHeight);
    layout.minMax.maxWidth = UI::UILayoutLength::Px(
        InspectorValueInputMaxWidth);
    layout.flexItem.grow = 1.0F;
    layout.flexItem.shrink = 1.0F;
    layout.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    return layout;
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

[[nodiscard]] Tina::Core::Status setSingleLineEllipsis(
    Tina::PrimaryWindowUITreeUpdater& tree, UI::UINodeId node)
{
    if (auto status = tree.setTextWrapMode(node, UI::UITextWrapMode::NoWrap);
        !status)
    {
        return status;
    }
    return tree.setTextOverflow(node, UI::UITextOverflow::Ellipsis);
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

[[nodiscard]] constexpr UI::UIElementDescriptor makeEditorSplitter(
    std::string_view accessibleName) noexcept
{
    UI::UIElementDescriptor descriptor = UI::makeSplitterElement({});
    descriptor.semantics.name = accessibleName;
    return descriptor;
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

    UI::UILayoutStyle sideRegionStyle{};
    sideRegionStyle.flexItem.grow = 1.0F;
    sideRegionStyle.flexItem.shrink = 1.0F;
    sideRegionStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    sideRegionStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    sideRegionStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    sideRegionStyle.flexContainer.gap.column = ui.productTheme.spacing.space4;

    UI::UINodeId leftRegion{};
    if (auto status = storeNode(
            ui.createPanel(toolbar, sideRegionStyle), leftRegion);
        !status) {
        return status;
    }
    constexpr std::array<std::string_view, MainMenuCount> menuLabels{
        "File", "Edit", "View", "Help"};
    constexpr std::array<float, MainMenuCount> menuWidths{44.0F, 44.0F, 48.0F, 48.0F};
    for (u32 index = 0; index < MainMenuCount; ++index) {
        if (auto status = storeNode(
                ui.createButton(
                    leftRegion, menuLabels[index],
                    fixedSize(menuWidths[index], ui.productTheme.controls.buttonHeight),
                    true, UI::UIStyleRoleId::ButtonText),
                mainMenuAnchors_[index]);
            !status) {
            return status;
        }
    }

    UI::UINodeId workspaceGroup{};
    if (auto status = storeNode(
            EditorToolbarGroup::Build(ui.tree, toolbar, ui.productTheme),
            workspaceGroup);
        !status) {
        return status;
    }
    constexpr std::array<std::string_view, 2> workspaceLabels{"2D", "3D"};
    for (u32 index = 0; index < workspaceModeButtons_.size(); ++index) {
        if (auto status = storeNode(
                ui.createSegmentedButton(
                    workspaceGroup, workspaceLabels[index],
                    fixedSize(48.0F, ui.productTheme.controls.buttonHeight)),
                workspaceModeButtons_[index]);
            !status) {
            return status;
        }
        if (auto status = ui.tree.setRadioButtonSelected(
                workspaceModeButtons_[index],
                index == (workspaceMode_ == WorkspaceMode::World2D ? 0U : 1U));
            !status) {
            return status;
        }
    }

    UI::UILayoutStyle rightRegionStyle = sideRegionStyle;
    rightRegionStyle.flexContainer.justifyContent = UI::UIJustifyContent::End;
    UI::UINodeId rightRegion{};
    if (auto status = storeNode(
            ui.createPanel(toolbar, rightRegionStyle), rightRegion);
        !status) {
        return status;
    }
    UI::UINodeId historyGroup{};
    if (auto status = storeNode(EditorToolbarGroup::Build(
                                    ui.tree, rightRegion, ui.productTheme),
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
    const WorkspaceSessionState& initialSession = activeWorkspaceSession();
    if (auto status = storeNode(ui.createIconButton(
                                    historyGroup, EditorIcon::Save, "Save", {},
                                    initialSession.hasDocumentPath()),
                                saveButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    historyGroup, EditorIcon::SaveAs, "Save As"),
                                saveAsButton_);
        !status) {
        return status;
    }
    UI::UINodeId playGroup{};
    if (auto status = storeNode(EditorToolbarGroup::Build(
                                    ui.tree, rightRegion, ui.productTheme),
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

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildDocumentTabsUi(UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    bool hasExternalDocumentTab = false;
    for (u32 index = 0; index < DocumentTabSlots; ++index) {
        const auto* tab = documentTabs_.tab(index);
        hasExternalDocumentTab = hasExternalDocumentTab ||
                                 (tab != nullptr && !isWorkspaceContextDocumentTab(*tab));
    }
    documentTabsBarLayout_ = fillWidth(ui.productTheme.controls.tabHeight);
    documentTabsBarLayout_.flexItem.shrink = 0.0F;
    documentTabsBarLayout_.flexContainer.direction = UI::UIFlexDirection::Row;
    documentTabsBarLayout_.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    documentTabsBarLayout_.flexContainer.gap.column = ui.productTheme.spacing.space1;
    documentTabsBarLayout_.padding = UI::UIEdgeSpacing::HorizontalVertical(
        ui.productTheme.spacing.space3, ui.productTheme.spacing.space0);
    documentTabsBarLayout_.visibility = hasExternalDocumentTab
                                            ? UI::UIVisibility::Visible
                                            : UI::UIVisibility::Collapsed;
    if (auto status = storeNode(ui.createSurface(parent, documentTabsBarLayout_,
                                              UI::UISurfaceVariant::Filled),
                                documentTabsBar_);
        !status) {
        return status;
    }
    for (u32 index = 0; index < DocumentTabSlots; ++index) {
        const auto* tab = documentTabs_.tab(index);
        const bool visible = tab != nullptr && !isWorkspaceContextDocumentTab(*tab);
        if (auto status = storeNode(ui.createSegmentedButton(documentTabsBar_,
                                                          tab != nullptr ? tab->title : std::string_view{},
                                                          editorDocumentTabLayout(
                                                              ui.productTheme,
                                                              visible
                                                                  ? UI::UIVisibility::Visible
                                                                  : UI::UIVisibility::Collapsed),
                                                          visible),
                                    documentTabButtons_[index]);
            !status) {
            return status;
        }
        if (auto status = setSingleLineEllipsis(ui.tree, documentTabButtons_[index]);
            !status) {
            return status;
        }
    }
    closeDocumentButtonLayout_ = fixedSize(
        ui.productTheme.controls.iconButtonExtent,
        ui.productTheme.controls.iconButtonExtent);
    closeDocumentButtonLayout_.flexContainer.justifyContent =
        UI::UIJustifyContent::Center;
    closeDocumentButtonLayout_.flexContainer.alignItems =
        UI::UIAxisAlignment::Center;
    closeDocumentButtonLayout_.visibility = UI::UIVisibility::Collapsed;
    auto closeDocumentParts = EditorIconButton::Build(
        ui.tree, documentTabsBar_, ui.productTheme, EditorIcon::Close,
        "Close document", closeDocumentButtonLayout_, false);
    if (!closeDocumentParts) {
        return Tina::Core::failure(std::move(closeDocumentParts.error()));
    }
    closeDocumentButtonRoot_ = closeDocumentParts->root;
    closeDocumentButton_ = closeDocumentParts->button;

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildWorkspaceUi(UiBuildContext& ui, UI::UINodeId parent)
    -> Tina::Core::Status
{
    const float splitterExtent = ui.productTheme.controls.splitterHitExtent;
    const UI::UILayoutStyle workspaceStyle = growingRegion();

    const UI::UISplitViewConfig splitAConfig{
        .orientation = UI::UISplitViewOrientation::Horizontal,
        .initialFraction = LeftDockInitialFraction,
        .minPrimarySize = LeftDockMinWidth,
        .minSecondarySize = ViewportMinWidth + InspectorMinWidth,
        .splitterExtent = splitterExtent,
    };
    if (auto status = storeNode(
            ui.tree.createElement(parent, UI::makeSplitViewElement(splitAConfig, workspaceStyle)),
            leftDockSplitView_);
        !status) {
        return status;
    }

    if (auto status = buildLeftDockUi(ui, leftDockSplitView_, leftDock_); !status) {
        return status;
    }
    UI::UIElementDescriptor leftDockSplitterDescriptor =
        makeEditorSplitter("Resize left dock");
    leftDockSplitterLayout_ = leftDockSplitterDescriptor.layout;
    if (auto status = storeNode(
            ui.tree.createElement(
                leftDockSplitView_, leftDockSplitterDescriptor), leftDockSplitter_);
        !status) {
        return status;
    }
    UI::UINodeId main{};
    if (auto status = storeNode(
            ui.createPanel(leftDockSplitView_, growingRegion()), main);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setSplitViewParts(
            leftDockSplitView_, leftDock_, leftDockSplitter_, main);
        !status) {
        return status;
    }

    const UI::UISplitViewConfig splitBConfig{
        .orientation = UI::UISplitViewOrientation::Horizontal,
        .initialFraction = MainCenterInitialFraction,
        .minPrimarySize = ViewportMinWidth,
        .minSecondarySize = InspectorMinWidth,
        .splitterExtent = splitterExtent,
    };
    if (auto status = storeNode(
            ui.tree.createElement(main, UI::makeSplitViewElement(splitBConfig, growingRegion())),
            inspectorSplitView_);
        !status) {
        return status;
    }

    UI::UINodeId center{};
    if (auto status = storeNode(
            ui.createPanel(inspectorSplitView_, growingRegion()), center);
        !status) {
        return status;
    }
    UI::UIElementDescriptor inspectorSplitterDescriptor =
        makeEditorSplitter("Resize Inspector");
    inspectorSplitterLayout_ = inspectorSplitterDescriptor.layout;
    if (auto status = storeNode(
            ui.tree.createElement(
                inspectorSplitView_, inspectorSplitterDescriptor), inspectorSplitter_);
        !status) {
        return status;
    }
    if (auto status = buildInspectorUi(ui, inspectorSplitView_, inspectorDock_); !status) {
        return status;
    }
    if (auto status = ui.tree.setSplitViewParts(
            inspectorSplitView_, center, inspectorSplitter_, inspectorDock_);
        !status) {
        return status;
    }

    const UI::UISplitViewConfig splitCConfig{
        .orientation = UI::UISplitViewOrientation::Vertical,
        .initialFraction = 1.0F,
        .minPrimarySize = ViewportMinHeight,
        .minSecondarySize = TimelineMinHeight,
        .splitterExtent = splitterExtent,
    };
    if (auto status = storeNode(
            ui.tree.createElement(center, UI::makeSplitViewElement(splitCConfig, growingRegion())),
            bottomPanelSplitView_);
        !status) {
        return status;
    }

    UI::UINodeId viewport{};
    if (auto status = buildViewportUi(ui, bottomPanelSplitView_, viewport); !status) {
        return status;
    }
    bottomPanelSplitterLayout_.visibility = UI::UIVisibility::Collapsed;
    UI::UIElementDescriptor bottomPanelSplitterDescriptor =
        makeEditorSplitter("Resize bottom panel");
    bottomPanelSplitterDescriptor.layout = bottomPanelSplitterLayout_;
    if (auto status = storeNode(
            ui.tree.createElement(
                bottomPanelSplitView_, bottomPanelSplitterDescriptor), bottomPanelSplitter_);
        !status) {
        return status;
    }

    bottomPanelHostLayout_ = growingRegion();
    bottomPanelHostLayout_.flexContainer.direction = UI::UIFlexDirection::Column;
    bottomPanelHostLayout_.flexContainer.gap = UI::UILayoutGap::All(0.0F);
    bottomPanelHostLayout_.visibility = UI::UIVisibility::Collapsed;
    if (auto status = storeNode(
            ui.createPanel(bottomPanelSplitView_, bottomPanelHostLayout_), bottomPanelHost_);
        !status) {
        return status;
    }
    if (auto status = buildTimelineUi(ui, bottomPanelHost_, animationPanel_); !status) {
        return status;
    }
    if (auto status = buildOutputPanelUi(ui, bottomPanelHost_, outputPanel_); !status) {
        return status;
    }
    return ui.tree.setSplitViewParts(
        bottomPanelSplitView_, viewport, bottomPanelSplitter_, bottomPanelHost_);
}

auto EditorWorkspaceState::buildLeftDockUi(
    UiBuildContext& ui, UI::UINodeId parent, UI::UINodeId& left) -> Tina::Core::Status
{
    leftDockLayout_ = growingRegion();
    leftDockLayout_.flexContainer.direction = UI::UIFlexDirection::Column;
    leftDockLayout_.flexContainer.gap.row = ui.productTheme.spacing.space3;
    leftDockLayout_.padding = UI::UIEdgeSpacing::All(ui.productTheme.spacing.space4);
    if (auto status = storeNode(
            ui.createSurface(parent, leftDockLayout_, UI::UISurfaceVariant::Filled), left);
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
    hierarchyTreeRowHeight_ = ui.productTheme.controls.treeRowHeight;
    if (auto status = ui.tree.setTreeViewPaint(hierarchyTree_, UI::makeTreeViewPaint(ui.productTheme)); !status) {
        return status;
    }
    if (auto status = ui.tree.setTreeViewDataSource(hierarchyTree_, hierarchyDataSource()); !status) {
        return status;
    }
    if (auto status = ui.tree.setTreeViewSelectedIndex(hierarchyTree_, 0); !status) {
        return status;
    }
    hierarchyDropIndicatorLayout_ = hierarchyRenameLayout(UI::UIVisibility::Collapsed);
    UI::UIElementDescriptor hierarchyDropIndicatorDescriptor = UI::makeLabelElement(
        "[Inside]", fixedSize(96.0F, 22.0F));
    hierarchyDropIndicatorDescriptor.textStyle = ui.accentText;
    hierarchyDropIndicatorDescriptor.visual.boxPaint =
        UI::makeSolidBox(UI::scaleColorAlpha(ui.productTheme.colors.primaryContainer, 235));
    hierarchyDropIndicatorDescriptor.layout = hierarchyDropIndicatorLayout_;
    hierarchyDropIndicatorDescriptor.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    hierarchyDropIndicatorDescriptor.semantics.mode = UI::UISemanticsMode::Exclude;
    if (auto status = storeNode(
            ui.tree.createElement(left, hierarchyDropIndicatorDescriptor),
            hierarchyDropIndicator_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, hierarchyDropIndicator_);
        !status) {
        return status;
    }
    UI::UILayoutStyle hierarchyPreselectionLayout =
        hierarchyRenameLayout(UI::UIVisibility::Collapsed);
    UI::UIElementDescriptor hierarchyPreselectionDescriptor =
        UI::makePanelElement(hierarchyPreselectionLayout);
    hierarchyPreselectionDescriptor.visual.boxPaint = UI::makeSolidBox(
        UI::rgb(0x000000, 0));
    hierarchyPreselectionDescriptor.visual.boxPaint->borderLight =
        ui.productTheme.colors.primary;
    hierarchyPreselectionDescriptor.visual.boxPaint->borderDark =
        ui.productTheme.colors.primary;
    hierarchyPreselectionDescriptor.visual.boxPaint->borderWidth = 1.0F;
    hierarchyPreselectionDescriptor.pointerHitPolicy =
        UI::UIPointerHitPolicy::Ignore;
    hierarchyPreselectionDescriptor.semantics.mode = UI::UISemanticsMode::Exclude;
    hierarchyPreselectionDescriptor.layout = hierarchyPreselectionLayout;
    if (auto status = storeNode(
            ui.tree.createElement(left, hierarchyPreselectionDescriptor),
            hierarchyPreselectionNode_);
        !status) {
        return status;
    }
    // Godot-style inline rename editor. It is a sibling overlay of the
    // virtualized TreeView, so the input covers only the active row label and
    // never changes the hierarchy panel's flow height.
    hierarchyRenameRootLayout_ = hierarchyRenameLayout(UI::UIVisibility::Collapsed);
    if (auto status = storeNode(
            ui.createTextEdit(left, {}, hierarchyRenameRootLayout_, true),
            hierarchyRenameRoot_);
        !status) {
        return status;
    }
    hierarchyRenameInput_ = hierarchyRenameRoot_;

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
    if (auto status = storeNode(ui.createIconButton(
                                    projectHeader->actions, EditorIcon::Add,
                                    "Import files", {}, true),
                                importSourceButton_);
        !status) {
        return status;
    }
    UI::UILayoutStyle projectBreadcrumbStyle = fillWidth(20.0F);
    projectBreadcrumbStyle.flexItem.shrink = 0.0F;
    const std::string_view initialProjectBreadcrumb =
        temporaryProjectActive()
            ? "Temporary Project / Assets"
            : assetResources_.projectCatalogConfigured
                  ? "Project / Assets"
                  : assetResources_.testFixtureCatalog ? "Test Data / Assets"
                                                       : "Project / Assets";
    if (auto status = storeNode(
            ui.createLabel(left, initialProjectBreadcrumb,
                           projectBreadcrumbStyle, ui.secondaryText),
            projectAssetBreadcrumb_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, projectAssetBreadcrumb_);
        !status) {
        return status;
    }
    UI::UILayoutStyle projectAssetToolsStyle =
        fillWidth(ui.productTheme.controls.textEditHeight);
    projectAssetToolsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    projectAssetToolsStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    projectAssetToolsStyle.flexContainer.gap.column = ui.productTheme.spacing.space2;
    UI::UINodeId projectAssetTools{};
    if (auto status = storeNode(
            ui.createPanel(left, projectAssetToolsStyle), projectAssetTools);
        !status) {
        return status;
    }
    UI::UILayoutStyle projectSearchStyle{};
    projectSearchStyle.size.height = UI::UILayoutLength::Px(
        ui.productTheme.controls.textEditHeight);
    projectSearchStyle.flexItem.grow = 1.0F;
    projectSearchStyle.flexItem.shrink = 1.0F;
    projectSearchStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    auto projectSearch = EditorSearchField::Build(
        ui.tree, projectAssetTools, ui.productTheme,
        projectAssets_.searchQuery(), "Search project assets",
        projectSearchStyle, true);
    if (!projectSearch) {
        return Tina::Core::failure(std::move(projectSearch.error()));
    }
    projectAssetSearchInput_ = projectSearch->textEdit;

    UI::UILayoutStyle typeFilterLayout = fixedSize(
        122.0F, ui.productTheme.controls.textEditHeight);
    typeFilterLayout.flexItem.shrink = 0.0F;
    typeFilterLayout.padding = UI::UIEdgeSpacing::HorizontalVertical(
        ui.productTheme.spacing.space3, ui.productTheme.spacing.space2);
    auto typeFilter = ui.tree.createElement(
        projectAssetTools,
        UI::makeDropdownElement(
            Tina::Editor::projectAssetTypeFilterLabel(projectAssets_.typeFilter()),
            typeFilterLayout));
    if (!typeFilter) {
        return Tina::Core::failure(std::move(typeFilter.error()));
    }
    projectAssetTypeDropdown_ = *typeFilter;
    UI::UILayoutStyle typeFilterPopupLayout = fixedSize(
        122.0F,
        ui.productTheme.controls.menuItemHeight *
            static_cast<float>(ProjectAssetTypeFilterCount));
    typeFilterPopupLayout.placement = UI::UILayoutPlacement::Overlay;
    auto typeFilterPopup = ui.tree.createElement(
        projectAssetTypeDropdown_, UI::makePopupElement(typeFilterPopupLayout));
    if (!typeFilterPopup) {
        return Tina::Core::failure(std::move(typeFilterPopup.error()));
    }
    projectAssetTypeDropdownPopup_ = *typeFilterPopup;
    if (auto status = ui.tree.setPopupStyle(
            projectAssetTypeDropdownPopup_,
            UI::UIPopupStyle{
                .placement = UI::UIPopupPlacement::Below,
                .anchorGap = ui.productTheme.spacing.space1,
                .matchAnchorWidth = true,
            }); !status) {
        return status;
    }
    for (u32 index = 0U; index < projectAssetTypeDropdownItems_.size(); ++index) {
        const auto filter = static_cast<Tina::Editor::ProjectAssetTypeFilter>(index);
        auto item = ui.tree.createElement(
            projectAssetTypeDropdownPopup_,
            UI::makeDropdownItemElement(
                Tina::Editor::projectAssetTypeFilterLabel(filter),
                fixedSize(122.0F, ui.productTheme.controls.menuItemHeight)));
        if (!item) {
            return Tina::Core::failure(std::move(item.error()));
        }
        projectAssetTypeDropdownItems_[index] = *item;
    }
    if (auto status = ui.tree.setDropdownSelectedItem(
            projectAssetTypeDropdown_,
            projectAssetTypeDropdownItems_[static_cast<u32>(
                projectAssets_.typeFilter())]); !status) {
        return status;
    }
    if (auto status = ui.tree.setDropdownOpen(projectAssetTypeDropdown_, false);
        !status) {
        return status;
    }
    const UI::UIDropdownChrome dropdownChrome =
        UI::makeDropdownChrome(ui.productTheme);
    if (auto status = ui.tree.setBoxPaint(
            projectAssetTypeDropdown_, dropdownChrome.box); !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonPaint(
            projectAssetTypeDropdown_, dropdownChrome.states); !status) {
        return status;
    }
    if (auto status = ui.tree.setDropdownPaint(
            projectAssetTypeDropdown_, dropdownChrome.dropdown); !status) {
        return status;
    }
    if (auto status = ui.tree.setTextStyle(
            projectAssetTypeDropdown_, dropdownChrome.label); !status) {
        return status;
    }
    if (auto status = ui.tree.setBoxPaint(
            projectAssetTypeDropdownPopup_,
            UI::makePopupBoxPaint(ui.productTheme)); !status) {
        return status;
    }
    const UI::UIButtonChrome dropdownItemChrome =
        UI::makeDropdownItemChrome(ui.productTheme);
    for (const UI::UINodeId item : projectAssetTypeDropdownItems_) {
        if (auto status = ui.tree.setBoxPaint(item, dropdownItemChrome.box); !status) {
            return status;
        }
        if (auto status = ui.tree.setButtonPaint(
                item, dropdownItemChrome.states); !status) {
            return status;
        }
        if (auto status = ui.tree.setTextStyle(
                item, dropdownItemChrome.label); !status) {
            return status;
        }
    }

    const std::array<std::string_view, 2> projectAssetViewLabels{"Grid", "List"};
    for (u32 index = 0; index < projectAssetViewButtons_.size(); ++index) {
        if (auto status = storeNode(
                ui.createSegmentedButton(
                    projectAssetTools, projectAssetViewLabels[index],
                    fixedSize(46.0F, ui.productTheme.controls.buttonHeight)),
                projectAssetViewButtons_[index]);
            !status) {
            return status;
        }
        if (auto status = ui.tree.setRadioButtonSelected(
                projectAssetViewButtons_[index],
                index == static_cast<u32>(projectAssetViewMode_));
            !status) {
            return status;
        }
    }

    projectAssetStartCenterLayout_ = fillWidth(0.0F);
    projectAssetStartCenterLayout_.size.height = UI::UILayoutLength::Auto();
    projectAssetStartCenterLayout_.flexItem.shrink = 0.0F;
    projectAssetStartCenterLayout_.flexContainer.direction =
        UI::UIFlexDirection::Column;
    projectAssetStartCenterLayout_.flexContainer.gap.row =
        ui.productTheme.spacing.space3;
    projectAssetStartCenterLayout_.padding =
        UI::UIEdgeSpacing::All(ui.productTheme.spacing.space5);
    projectAssetStartCenterLayout_.visibility = UI::UIVisibility::Collapsed;
    UI::UIElementDescriptor startCenterDescriptor = UI::makeSurfaceElement(
        {.variant = UI::UISurfaceVariant::Elevated},
        projectAssetStartCenterLayout_);
    startCenterDescriptor.semantics.mode = UI::UISemanticsMode::Publish;
    startCenterDescriptor.semantics.role = UI::UISemanticsRole::Group;
    startCenterDescriptor.semantics.name = "Tina Studio Start Center";
    startCenterDescriptor.semantics.description =
        "Create or open a project before using Project Assets";
    if (auto status = storeNode(
            ui.tree.createElement(left, startCenterDescriptor),
            projectAssetStartCenter_);
        !status) {
        return status;
    }
    UI::UILayoutStyle startCenterTitleLayout = fillWidth(24.0F);
    UI::UIElementDescriptor startCenterTitle = UI::makeLabelElement(
        "Tina Studio", startCenterTitleLayout);
    startCenterTitle.textStyle = ui.titleText;
    startCenterTitle.semantics.name = "Tina Studio";
    if (auto status = storeNode(
            ui.tree.createElement(projectAssetStartCenter_, startCenterTitle),
            projectAssetStartCenterTitle_);
        !status) {
        return status;
    }
    UI::UIElementDescriptor startCenterSubtitle = UI::makeLabelElement(
        "Create a project, open an existing project, or import files to begin.",
        fillWidth(40.0F));
    startCenterSubtitle.textStyle = ui.secondaryText;
    startCenterSubtitle.semantics.description =
        "Project authoring is unavailable until a project is opened or created";
    if (auto status = storeNode(
            ui.tree.createElement(projectAssetStartCenter_, startCenterSubtitle),
            projectAssetStartCenterSubtitle_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, projectAssetStartCenterSubtitle_);
        !status) {
        return status;
    }
    projectAssetStartCenterActions_ = UI::UINodeId{};
    UI::UILayoutStyle startCenterActionsLayout = fillWidth(
        ui.productTheme.controls.buttonHeight);
    startCenterActionsLayout.flexContainer.direction = UI::UIFlexDirection::Row;
    startCenterActionsLayout.flexContainer.gap.column =
        ui.productTheme.spacing.space2;
    if (auto status = storeNode(
            ui.createPanel(projectAssetStartCenter_, startCenterActionsLayout),
            projectAssetStartCenterActions_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(projectAssetStartCenterActions_, "New Project",
                            fixedSize(112.0F, ui.productTheme.controls.buttonHeight),
                            true, UI::UIStyleRoleId::ButtonPrimary),
            projectAssetStartCenterNewButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(projectAssetStartCenterActions_, "Open Project",
                            fixedSize(112.0F, ui.productTheme.controls.buttonHeight)),
            projectAssetStartCenterOpenButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(projectAssetStartCenterActions_, "Import Files",
                            fixedSize(104.0F, ui.productTheme.controls.buttonHeight)),
            projectAssetStartCenterImportButton_);
        !status) {
        return status;
    }
    UI::UILayoutStyle recentProjectsLayout = growingRegion();
    recentProjectsLayout.flexContainer.direction = UI::UIFlexDirection::Column;
    recentProjectsLayout.flexContainer.gap.row = ui.productTheme.spacing.space1;
    if (auto status = storeNode(
            ui.createPanel(projectAssetStartCenter_, recentProjectsLayout),
            projectAssetStartCenterRecent_);
        !status) {
        return status;
    }
    for (u32 index = 0; index < RecentProjectCapacity; ++index) {
        if (auto status = storeNode(
                ui.createButton(projectAssetStartCenterRecent_, "", fillWidth(ui.productTheme.controls.buttonHeight)),
                recentProjectButtons_[index]); !status) {
            return status;
        }
    }

    UI::UILayoutStyle projectListStyle = growingRegion();
    projectListStyle.minMax.minHeight = UI::UILayoutLength::Px(96.0F);
    projectAssetListLayout_ = projectListStyle;
    auto projectList = ui.tree.createElement(
        left, UI::makeVirtualGridViewElement(
                  {.materializedItemCapacity = AssetBrowserMaterializedCapacity},
                  projectListStyle));
    if (!projectList) {
        return Tina::Core::failure(std::move(projectList.error()));
    }
    projectAssetList_ = *projectList;
    projectAssetListStyle_ = UI::UIVirtualGridViewStyle{
                .minimumItemWidth = projectAssetViewMode_ == ProjectAssetViewMode::Grid
                                        ? ProjectAssetMinimumItemWidth
                                        : 320.0F,
                .itemHeight = projectAssetViewMode_ == ProjectAssetViewMode::Grid
                                  ? 84.0F
                                  : 60.0F,
                .stretchLastRow = projectAssetViewMode_ != ProjectAssetViewMode::Grid,
                .overscanRows = 1,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = ui.productTheme.controls.listRowHeight,
                .itemTextOverflow = UI::UITextOverflow::Ellipsis,
            };
    if (auto status = ui.tree.setVirtualGridViewStyle(
            projectAssetList_, projectAssetListStyle_);
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

    projectAssetEmptyStateLayout_ = growingRegion();
    projectAssetEmptyStateLayout_.minMax.minHeight =
        UI::UILayoutLength::Px(96.0F);
    projectAssetEmptyStateLayout_.flexContainer.direction =
        UI::UIFlexDirection::Column;
    projectAssetEmptyStateLayout_.flexContainer.alignItems =
        UI::UIAxisAlignment::Center;
    projectAssetEmptyStateLayout_.flexContainer.justifyContent =
        UI::UIJustifyContent::Center;
    projectAssetEmptyStateLayout_.flexContainer.gap.row =
        ui.productTheme.spacing.space3;
    projectAssetEmptyStateLayout_.padding =
        UI::UIEdgeSpacing::All(ui.productTheme.spacing.space5);
    projectAssetEmptyStateLayout_.visibility = UI::UIVisibility::Collapsed;
    UI::UIElementDescriptor emptyStateDescriptor =
        UI::makePanelElement(projectAssetEmptyStateLayout_);
    emptyStateDescriptor.semantics.mode = UI::UISemanticsMode::Publish;
    emptyStateDescriptor.semantics.role = UI::UISemanticsRole::Group;
    emptyStateDescriptor.semantics.name = "Project Assets empty state";
    if (auto status = storeNode(ui.tree.createElement(left, emptyStateDescriptor),
                                projectAssetEmptyState_);
        !status) {
        return status;
    }
    UI::UIElementDescriptor emptyStateTitleDescriptor = UI::makeLabelElement(
        "No resources yet", fillWidth(24.0F));
    emptyStateTitleDescriptor.textStyle = ui.sectionText;
    emptyStateTitleDescriptor.contentAlignment.horizontal =
        UI::UIAxisAlignment::Center;
    if (auto status = storeNode(
            ui.tree.createElement(projectAssetEmptyState_, emptyStateTitleDescriptor),
            projectAssetEmptyStateTitle_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, projectAssetEmptyStateTitle_);
        !status) {
        return status;
    }
    UI::UIElementDescriptor emptyStateTextDescriptor = UI::makeLabelElement(
        "Import files to add resources.", fillWidth(22.0F));
    emptyStateTextDescriptor.textStyle = ui.secondaryText;
    emptyStateTextDescriptor.contentAlignment.horizontal =
        UI::UIAxisAlignment::Center;
    if (auto status = storeNode(
            ui.tree.createElement(projectAssetEmptyState_, emptyStateTextDescriptor),
            projectAssetEmptyStateText_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, projectAssetEmptyStateText_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(
                projectAssetEmptyState_, "Import Files...",
                fixedSize(108.0F, ui.productTheme.controls.buttonHeight)),
            projectAssetEmptyStateImportButton_);
        !status) {
        return status;
    }

    projectAssetActivityLayout_ = fillWidth(38.0F);
    projectAssetActivityLayout_.flexItem.shrink = 0.0F;
    projectAssetActivityLayout_.flexContainer.direction =
        UI::UIFlexDirection::Column;
    projectAssetActivityLayout_.flexContainer.gap.row =
        ui.productTheme.spacing.space1;
    projectAssetActivityLayout_.visibility = UI::UIVisibility::Collapsed;
    if (auto status = storeNode(
            ui.createPanel(left, projectAssetActivityLayout_),
            projectAssetActivity_);
        !status) {
        return status;
    }
    UI::UILayoutStyle activityRowLayout = fillWidth(28.0F);
    activityRowLayout.flexContainer.direction = UI::UIFlexDirection::Row;
    activityRowLayout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    activityRowLayout.flexContainer.gap.column = ui.productTheme.spacing.space2;
    UI::UINodeId activityRow{};
    if (auto status = storeNode(
            ui.createPanel(projectAssetActivity_, activityRowLayout),
            activityRow);
        !status) {
        return status;
    }
    UI::UILayoutStyle activityTextLayout = fillWidth(22.0F);
    activityTextLayout.flexItem.grow = 1.0F;
    activityTextLayout.flexItem.shrink = 1.0F;
    activityTextLayout.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    UI::UIElementDescriptor activityDescriptor = UI::makeLabelElement(
        activeProjectWorkspace_.has_value()
            ? "Drop files here to import resources"
            : "Open a project to import resources",
        activityTextLayout);
    activityDescriptor.textStyle = ui.accentText;
    activityDescriptor.semantics.description =
        "Shows source import progress and the Project Assets file drop target";
    activityDescriptor.semantics.liveSetting = UI::UISemanticsLiveSetting::Polite;
    activityDescriptor.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    if (auto status = storeNode(
            ui.tree.createElement(activityRow, activityDescriptor),
            projectAssetActivityText_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(
                activityRow, "Cancel",
                fixedSize(72.0F, ui.productTheme.controls.buttonHeight),
                false, UI::UIStyleRoleId::ButtonText),
            cancelSourceImportButton_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, projectAssetActivityText_);
        !status) {
        return status;
    }
    UI::UILayoutStyle activityProgressLayout = fillWidth(6.0F);
    activityProgressLayout.flexItem.shrink = 0.0F;
    UI::UIElementDescriptor activityProgressDescriptor =
        UI::makeProgressBarElement(activityProgressLayout);
    activityProgressDescriptor.semantics.name = "Project Assets import progress";
    activityProgressDescriptor.semantics.description =
        "Shows the current phase of the bounded source import transaction";
    if (auto status = storeNode(
            ui.tree.createElement(projectAssetActivity_, activityProgressDescriptor),
            projectAssetActivityProgress_);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setProgressBarRange(
            projectAssetActivityProgress_, 0.0F, 1.0F);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setProgressBarValue(
            projectAssetActivityProgress_, 0.0F);
        !status) {
        return status;
    }

    projectAssetImportCalloutLayout_ = fillWidth(
        22.0F + ui.productTheme.controls.buttonHeight +
        ui.productTheme.spacing.space2 + ui.productTheme.spacing.space3 * 2.0F);
    projectAssetImportCalloutLayout_.flexItem.shrink = 0.0F;
    projectAssetImportCalloutLayout_.flexContainer.direction =
        UI::UIFlexDirection::Column;
    projectAssetImportCalloutLayout_.flexContainer.gap.row =
        ui.productTheme.spacing.space2;
    projectAssetImportCalloutLayout_.padding =
        UI::UIEdgeSpacing::All(ui.productTheme.spacing.space3);
    projectAssetImportCalloutLayout_.visibility = UI::UIVisibility::Collapsed;
    UI::UIElementDescriptor importFailureCallout =
        UI::makePanelElement(projectAssetImportCalloutLayout_);
    importFailureCallout.visual.boxPaint =
        UI::makeSolidBox(ui.productTheme.colors.errorContainer);
    if (auto status = storeNode(
            ui.tree.createElement(left, importFailureCallout),
            projectAssetImportCallout_);
        !status) {
        return status;
    }
    UI::UITextStyle importFailureTextStyle = ui.secondaryText;
    importFailureTextStyle.color = ui.productTheme.colors.onErrorContainer;
    UI::UIElementDescriptor failureDescriptor = UI::makeLabelElement(
        "Import failed. Previous Catalog preserved.", fillWidth(22.0F));
    failureDescriptor.textStyle = importFailureTextStyle;
    failureDescriptor.semantics.description =
        "The source import failed and the previous Catalog remains active";
    failureDescriptor.semantics.liveSetting = UI::UISemanticsLiveSetting::Assertive;
    failureDescriptor.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    if (auto status = storeNode(
            ui.tree.createElement(projectAssetImportCallout_, failureDescriptor),
            projectAssetImportFailureText_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, projectAssetImportFailureText_);
        !status) {
        return status;
    }
    UI::UINodeId importFailureActions{};
    UI::UILayoutStyle importFailureActionsStyle =
        fillWidth(ui.productTheme.controls.buttonHeight);
    importFailureActionsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    importFailureActionsStyle.flexContainer.gap.column =
        ui.productTheme.spacing.space2;
    if (auto status = storeNode(
            ui.createPanel(projectAssetImportCallout_,
                           importFailureActionsStyle),
            importFailureActions);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(
                importFailureActions, "Retry",
                fixedSize(72.0F, ui.productTheme.controls.buttonHeight), false),
            retrySourceImportButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(
                importFailureActions, "Open Output",
                fixedSize(112.0F, ui.productTheme.controls.buttonHeight)),
            openImportOutputButton_);
        !status) {
        return status;
    }

    UI::UILayoutStyle projectAssetSummaryStyle = fillWidth(22.0F);
    projectAssetSummaryStyle.flexItem.shrink = 0.0F;
    const std::string_view initialProjectAssetSummary =
        projectAssets_.selectedItem() != nullptr
            ? "Selected project asset"
            : (assetResources_.projectCatalogConfigured
                   ? "No assets match this filter"
                   : (assetResources_.testFixtureCatalog
                          ? "No test assets match this filter"
                          : "No project open"));
    if (auto status = storeNode(
            ui.createLabel(left, initialProjectAssetSummary,
                           projectAssetSummaryStyle, ui.secondaryText),
            projectAssetSummary_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    hierarchyHeader->actions, EditorIcon::ChevronLeft,
                                    "Hide left dock"),
                                leftDockCollapseButton_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, projectAssetSummary_);
        !status) {
        return status;
    }

    sourceImportSectionLayout_ = fillWidth(ui.productTheme.controls.buttonHeight);
    sourceImportSectionLayout_.size.height = UI::UILayoutLength::Auto();
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
    sourceImportExpandButtonRootLayout_ = fixedSize(
        ui.productTheme.controls.iconButtonExtent,
        ui.productTheme.controls.iconButtonExtent);
    sourceImportExpandButtonRootLayout_.visibility =
        sourceImportExpanded_ ? UI::UIVisibility::Collapsed
                              : UI::UIVisibility::Visible;
    auto sourceImportExpandButton = EditorIconButton::Build(
        ui.tree, sourceImportHeader->actions, ui.productTheme,
        EditorIcon::ChevronRight, "Show source import records",
        sourceImportExpandButtonRootLayout_, true);
    if (!sourceImportExpandButton) {
        return Tina::Core::failure(std::move(sourceImportExpandButton.error()));
    }
    sourceImportExpandButtonRoot_ = sourceImportExpandButton->root;
    sourceImportExpandButton_ = sourceImportExpandButton->button;

    sourceImportCollapseButtonRootLayout_ = sourceImportExpandButtonRootLayout_;
    sourceImportCollapseButtonRootLayout_.visibility =
        sourceImportExpanded_ ? UI::UIVisibility::Visible
                              : UI::UIVisibility::Collapsed;
    auto sourceImportCollapseButton = EditorIconButton::Build(
        ui.tree, sourceImportHeader->actions, ui.productTheme,
        EditorIcon::ChevronDown, "Hide source import records",
        sourceImportCollapseButtonRootLayout_, true);
    if (!sourceImportCollapseButton) {
        return Tina::Core::failure(std::move(sourceImportCollapseButton.error()));
    }
    sourceImportCollapseButtonRoot_ = sourceImportCollapseButton->root;
    sourceImportCollapseButton_ = sourceImportCollapseButton->button;
    if (auto status = storeNode(ui.createIconButton(
                                    sourceImportHeader->actions, EditorIcon::Delete,
                                    "Remove source import", {},
                                    activeProjectWorkspace_.has_value() &&
                                        !sourceImportUnits_.empty()),
                                removeSourceImportButton_);
        !status) {
        return status;
    }

    sourceImportDetailsLayout_ = fillWidth(128.0F);
    sourceImportDetailsLayout_.visibility =
        sourceImportExpanded_ ? UI::UIVisibility::Visible
                              : UI::UIVisibility::Collapsed;
    if (auto status = storeNode(
            ui.createPanel(sourceImportSection_, sourceImportDetailsLayout_),
            sourceImportDetails_);
        !status) {
        return status;
    }

    // Keep the records viewport bounded. A percentage-height DataGrid inside
    // the Auto-sized import section can resolve against the whole left dock,
    // exceeding the fixed materialized-row pool when the section is expanded.
    UI::UILayoutStyle sourceImportGridStyle = fillWidth(128.0F);
    sourceImportGridStyle.minMax.minHeight = UI::UILayoutLength::Px(96.0F);
    auto sourceImportGrid = ui.tree.createElement(
        sourceImportDetails_, UI::makeDataGridElement(
                  {
                      .columnCapacity = SourceImportColumnCapacity,
                      .materializedRowCapacity = SourceImportMaterializedCapacity,
                  },
                  sourceImportGridStyle));
    if (!sourceImportGrid) {
        return Tina::Core::failure(std::move(sourceImportGrid.error()));
    }
    sourceImportGrid_ = *sourceImportGrid;
    if (auto status = ui.tree.setDataGridStyle(
            sourceImportGrid_,
            UI::UIDataGridStyle{
                .columnHeaderHeight = ui.productTheme.controls.buttonHeight,
                .rowHeight = ui.productTheme.controls.listRowHeight,
                .overscanRows = 1,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = ui.productTheme.controls.listRowHeight,
            });
        !status) {
        return status;
    }
    if (auto status = ui.tree.setDataGridPaint(
            sourceImportGrid_, UI::makeDataGridPaint(ui.productTheme));
        !status) {
        return status;
    }
    if (auto status = ui.tree.setDataGridDataSource(
            sourceImportGrid_, sourceImportGridDataSource());
        !status) {
        return status;
    }
    if (!sourceImportUnits_.empty()) {
        if (auto status = ui.tree.setDataGridSelectedCell(
                sourceImportGrid_, 0U, 0U);
            !status) {
            return status;
        }
        observedSourceImportSelectionIndex_ = 0U;
    }

    UI::UINodeId projectCatalogActions{};
    UI::UILayoutStyle projectCatalogActionsStyle = fillWidth(
        ui.productTheme.controls.buttonHeight);
    projectCatalogActionsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    projectCatalogActionsStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    projectCatalogActionsStyle.flexContainer.gap.column = ui.productTheme.spacing.space3;
    if (auto status = storeNode(ui.createPanel(left, projectCatalogActionsStyle),
                                projectCatalogActions);
        !status) {
        return status;
    }
    if (auto status = appendVerticalDivider(
            ui.tree, projectCatalogActions,
            ui.productTheme.controls.iconButtonExtent);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    projectCatalogActions, EditorIcon::MoveRight,
                                    "Open asset", {},
                                             projectAssets_.visibleItemCount() != 0U),
                                openProjectAssetButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    projectCatalogActions, EditorIcon::Refresh,
                                    "Refresh catalog", {},
                                    assetResources_.projectCatalogConfigured),
                                refreshProjectCatalogButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(projectCatalogActions, "Catalog",
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
    centerStyle.flexContainer.gap.row = ui.productTheme.spacing.space2;
    centerStyle.padding = UI::UIEdgeSpacing::All(ui.productTheme.spacing.space3);
    if (auto status = storeNode(ui.createPanel(parent, centerStyle), center);
        !status) {
        return status;
    }

    UI::UINodeId viewportToolbar{};
    UI::UILayoutStyle viewportToolbarStyle = fillWidth(
        ui.productTheme.controls.contextToolbarHeight);
    viewportToolbarStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    viewportToolbarStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    viewportToolbarStyle.flexContainer.gap.column = ui.productTheme.spacing.space2;
    viewportToolbarStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(
        ui.productTheme.spacing.space2, ui.productTheme.spacing.space0);
    if (auto status = storeNode(
            ui.createSurface(center, viewportToolbarStyle,
                             UI::UISurfaceVariant::Filled),
            viewportToolbar);
        !status) {
        return status;
    }
    viewportContextButtonLayouts_[0] = fixedSize(
        58.0F, ui.productTheme.controls.buttonHeight);
    viewportContextButtonLayouts_[1] = fixedSize(
        72.0F, ui.productTheme.controls.buttonHeight);
    for (UI::UILayoutStyle& layout : viewportContextButtonLayouts_) {
        layout.visibility = workspaceMode_ == WorkspaceMode::World2D
                                ? UI::UIVisibility::Visible
                                : UI::UIVisibility::Collapsed;
    }
    if (auto status = storeNode(
            ui.createSegmentedButton(
                viewportToolbar, "Scene", viewportContextButtonLayouts_[0]),
            viewportContextButtons_[0]);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createSegmentedButton(
                viewportToolbar, "TileMap", viewportContextButtonLayouts_[1]),
            viewportContextButtons_[1]);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setRadioButtonSelected(
            viewportContextButtons_[0], !tileMapEditingContext());
        !status) {
        return status;
    }
    if (auto status = ui.tree.setRadioButtonSelected(
            viewportContextButtons_[1], tileMapEditingContext());
        !status) {
        return status;
    }
    if (auto status = appendVerticalDivider(
            ui.tree, viewportToolbar,
            ui.productTheme.controls.iconButtonExtent);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconToggleButton(
                                    viewportToolbar, EditorIcon::Select, "Select tool"),
                                selectToolButtons_[0]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconToggleButton(
                                    viewportToolbar, EditorIcon::Move, "Move tool"),
                                translateToolButtons_[0]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconToggleButton(
                                    viewportToolbar, EditorIcon::Rotate, "Rotate tool"),
                                rotateToolButtons_[0]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconToggleButton(
                                    viewportToolbar, EditorIcon::Scale, "Scale tool"),
                                scaleToolButtons_[0]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconToggleButton(
                                    viewportToolbar, EditorIcon::Paint,
                                    "Paint tiles", {}, false),
                                tilePaintToolButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconToggleButton(
                                    viewportToolbar, EditorIcon::Erase,
                                    "Erase tiles", {}, false),
                                tileEraseToolButton_);
        !status) {
        return status;
    }
    if (auto status = appendVerticalDivider(
            ui.tree, viewportToolbar, ui.productTheme.controls.iconButtonExtent);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconToggleButton(
                                    viewportToolbar, EditorIcon::World,
                                    "World or local transform orientation"),
                                orientationButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconToggleButton(
                                    viewportToolbar, EditorIcon::Snap,
                                    "Transform snapping"),
                                snapButton_);
        !status) {
        return status;
    }
    if (auto status = appendVerticalDivider(
            ui.tree, viewportToolbar, ui.productTheme.controls.iconButtonExtent);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(
                                    viewportToolbar, "Replace",
                                    fixedSize(58.0F, ui.productTheme.controls.buttonHeight)),
                                marqueeModeButtons_[0]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(
                                    viewportToolbar, "Add",
                                    fixedSize(40.0F, ui.productTheme.controls.buttonHeight)),
                                marqueeModeButtons_[1]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(
                                    viewportToolbar, "Toggle",
                                    fixedSize(52.0F, ui.productTheme.controls.buttonHeight)),
                                marqueeModeButtons_[2]);
        !status) {
        return status;
    }
    if (auto status = appendVerticalDivider(
            ui.tree, viewportToolbar, ui.productTheme.controls.iconButtonExtent);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    viewportToolbar, EditorIcon::FrameAll,
                                    "Frame all"),
                                frameAllButton_);
        !status) {
        return status;
    }
    UI::UILayoutStyle viewportToolbarSpacerStyle{};
    viewportToolbarSpacerStyle.flexItem.grow = 1.0F;
    viewportToolbarSpacerStyle.flexItem.shrink = 1.0F;
    viewportToolbarSpacerStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    UI::UINodeId viewportToolbarSpacer{};
    if (auto status = storeNode(
            ui.createPanel(viewportToolbar, viewportToolbarSpacerStyle),
            viewportToolbarSpacer);
        !status) {
        return status;
    }
    viewportModeLayout_ = fixedSize(
        136.0F, ui.productTheme.controls.buttonHeight);
    viewportModeLayout_.visibility = workspaceMode_ == WorkspaceMode::World3D
                                         ? UI::UIVisibility::Visible
                                         : UI::UIVisibility::Collapsed;
    if (auto status = storeNode(
            ui.createButton(viewportToolbar, "View: Custom",
                            viewportModeLayout_,
                            workspaceMode_ == WorkspaceMode::World3D),
            viewportMode_);
        !status) {
        return status;
    }
    UI::UINodeId viewportCanvas{};
    UI::UILayoutStyle viewportCanvasStyle = growingRegion();
    viewportCanvasStyle.minMax.minHeight = UI::UILayoutLength::Px(220.0F);
    viewportCanvasStyle.padding = UI::UIEdgeSpacing::All(ui.productTheme.spacing.space2);
    if (auto status = storeNode(ui.createPanel(center, viewportCanvasStyle),
                                viewportCanvas);
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
    UI::UILayoutStyle viewportDropStyle = fixedSize(1.0F, 1.0F);
    viewportDropStyle.placement = UI::UILayoutPlacement::Overlay;
    viewportDropStyle.visibility = UI::UIVisibility::Collapsed;
    UI::UIElementDescriptor viewportDrop =
        UI::makePanelElement(viewportDropStyle);
    viewportDrop.visual.boxPaint = UI::makeSolidBox(UI::rgb(0x000000, 0));
    viewportDrop.visual.boxPaint->borderLight = ui.productTheme.colors.primary;
    viewportDrop.visual.boxPaint->borderDark = ui.productTheme.colors.primary;
    viewportDrop.visual.boxPaint->borderWidth = 2.0F;
    viewportDrop.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    viewportDrop.semantics.mode = UI::UISemanticsMode::Exclude;
    if (auto status = storeNode(
            ui.tree.createElement(viewportPreviewLayer_, viewportDrop),
            viewportAssetDropIndicator_);
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
    for (UI::UINodeId& preselectionNode : viewportPreselectionVisualNodes_) {
        UI::UILayoutStyle preselectionStyle = fixedSize(1.0F, 1.0F);
        preselectionStyle.placement = UI::UILayoutPlacement::Overlay;
        preselectionStyle.visibility = UI::UIVisibility::Collapsed;
        UI::UIElementDescriptor preselection =
            UI::makePanelElement(preselectionStyle);
        preselection.visual.boxPaint = UI::makeSolidBox(UI::rgb(0x000000, 0));
        preselection.visual.boxPaint->borderLight =
            ui.productTheme.colors.primary;
        preselection.visual.boxPaint->borderDark =
            ui.productTheme.colors.primary;
        preselection.visual.boxPaint->borderWidth = 1.5F;
        preselection.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
        preselection.semantics.mode = UI::UISemanticsMode::Exclude;
        if (auto status = storeNode(
                ui.tree.createElement(viewportPreviewLayer_, preselection),
                preselectionNode);
            !status) {
            return status;
        }
    }
    UI::UILayoutStyle compassStyle = fixedSize(
        ViewportOrientationCompassExtent, ViewportOrientationCompassExtent);
    compassStyle.placement = UI::UILayoutPlacement::Overlay;
    compassStyle.visibility = UI::UIVisibility::Collapsed;
    UI::UIElementDescriptor compass = UI::makePanelElement(compassStyle);
    compass.visual.boxPaint = UI::makeSolidEllipse(UI::rgb(0x11171B, 218));
    compass.pointerHitPolicy = UI::UIPointerHitPolicy::Targetable;
    compass.semantics.mode = UI::UISemanticsMode::Automatic;
    if (auto status = storeNode(
            ui.tree.createElement(viewportPreviewLayer_, compass),
            viewportOrientationCompass_);
        !status) {
        return status;
    }

    const auto makeOrbLayerLayout = [](float width, float height, float x,
                                       float y) noexcept {
        UI::UILayoutStyle layout = fixedSize(width, height);
        layout.placement = UI::UILayoutPlacement::Overlay;
        layout.overlay.horizontal = UI::UIAxisAlignment::Start;
        layout.overlay.vertical = UI::UIAxisAlignment::Start;
        layout.overlay.offset.x = UI::UILayoutLength::Px(x);
        layout.overlay.offset.y = UI::UILayoutLength::Px(y);
        return layout;
    };
    viewportOrientationOrbLayerLayouts_ = {
        makeOrbLayerLayout(70.0F, 70.0F, 6.0F, 6.0F),
        makeOrbLayerLayout(32.0F, 68.0F, 25.0F, 7.0F),
        makeOrbLayerLayout(68.0F, 30.0F, 7.0F, 26.0F),
        makeOrbLayerLayout(26.0F, 16.0F, 15.0F, 13.0F),
        makeOrbLayerLayout(
            ViewportOrientationCompassExtent,
            ViewportOrientationCompassExtent, 0.0F, 0.0F),
    };
    const std::array<UI::UIBoxPaint, ViewportOrientationOrbLayerCount>
        orbLayerPaints{
            UI::makeSolidEllipse(UI::rgb(0x1C2A33, 236)),
            UI::makeEllipseOutline(UI::rgb(0xA6B5BF, 72), 1.0F),
            UI::makeEllipseOutline(UI::rgb(0xA6B5BF, 62), 1.0F),
            UI::makeSolidEllipse(UI::rgb(0xDCE7EC, 56)),
            UI::makeEllipseOutline(UI::rgb(0x82939D, 190), 1.0F),
        };
    for (Tina::Core::usize layer = 0;
         layer < ViewportOrientationOrbLayerCount; ++layer) {
        UI::UIElementDescriptor orbLayer = UI::makePanelElement(
            viewportOrientationOrbLayerLayouts_[layer]);
        orbLayer.visual.boxPaint = orbLayerPaints[layer];
        orbLayer.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
        orbLayer.semantics.mode = UI::UISemanticsMode::Exclude;
        if (auto status = storeNode(
                ui.tree.createElement(viewportOrientationCompass_, orbLayer),
                viewportOrientationOrbLayers_[layer]);
            !status) {
            return status;
        }
    }

    UI::UILayoutStyle compassFillStyle = fixedSize(
        ViewportOrientationCompassExtent, ViewportOrientationCompassExtent);
    compassFillStyle.placement = UI::UILayoutPlacement::Overlay;

    UI::UITextStyle axisLabelStyle = ui.compactText;
    axisLabelStyle.logicalSize = 10.0F;
    axisLabelStyle.color = UI::rgb(0xF7FAFB);
    constexpr std::array<std::string_view, ViewportOrientationAxisCount>
        AxisLabels{"X", "Y", "Z"};
    constexpr std::array<std::string_view, ViewportOrientationAxisCount>
        AxisAccessibleNames{"Right view", "Top view", "Front view"};
    for (Tina::Core::usize axis = 0; axis < ViewportOrientationAxisCount;
         ++axis) {
        UI::UILayoutStyle lineStyle = compassFillStyle;
        lineStyle.visibility = UI::UIVisibility::Collapsed;
        UI::UIElementDescriptor line = UI::makePanelElement(lineStyle);
        line.visual.boxPaint = UI::makeSolidLine(
            UI::rgb(0x9AA8B8, 180), {}, {}, 2.0F);
        line.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
        line.semantics.mode = UI::UISemanticsMode::Exclude;
        if (auto status = storeNode(
                ui.tree.createElement(viewportOrientationCompass_, line),
                viewportOrientationAxisLines_[axis]);
            !status) {
            return status;
        }

        UI::UILayoutStyle endpointStyle = fixedSize(
            ViewportOrientationEndpointExtent,
            ViewportOrientationEndpointExtent);
        endpointStyle.placement = UI::UILayoutPlacement::Overlay;
        endpointStyle.visibility = UI::UIVisibility::Collapsed;
        UI::UIElementDescriptor endpoint = UI::makeButtonElement({}, endpointStyle);
        endpoint.visual.styleRole = UI::UIStyleRoleId::ButtonText;
        endpoint.visual.boxPaint = UI::makeSolidEllipse(
            UI::rgb(0x9AA8B8, 230));
        endpoint.pointerHitPolicy = UI::UIPointerHitPolicy::Targetable;
        endpoint.semantics.name = AxisAccessibleNames[axis];
        endpoint.semantics.useContentAsName = false;
        if (auto status = storeNode(
                ui.tree.createElement(viewportOrientationCompass_, endpoint),
                viewportOrientationAxisEndpoints_[axis]);
            !status) {
            return status;
        }

        UI::UIElementDescriptor label = UI::makeLabelElement(
            AxisLabels[axis], endpointStyle);
        label.textStyle = axisLabelStyle;
        label.contentAlignment.horizontal = UI::UIAxisAlignment::Center;
        label.contentAlignment.vertical = UI::UIAxisAlignment::Center;
        label.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
        label.semantics.mode = UI::UISemanticsMode::Exclude;
        if (auto status = storeNode(
                ui.tree.createElement(viewportOrientationCompass_, label),
                viewportOrientationAxisLabels_[axis]);
            !status) {
            return status;
        }
    }

    UI::UILayoutStyle compassCenterStyle = fixedSize(
        ViewportOrientationCenterExtent, ViewportOrientationCenterExtent);
    compassCenterStyle.placement = UI::UILayoutPlacement::Overlay;
    compassCenterStyle.overlay.horizontal = UI::UIAxisAlignment::Center;
    compassCenterStyle.overlay.vertical = UI::UIAxisAlignment::Center;
    UI::UIElementDescriptor compassCenter = UI::makePanelElement(
        compassCenterStyle);
    compassCenter.visual.boxPaint = UI::makeSolidEllipse(
        UI::rgb(0xE8EDF3, 235));
    compassCenter.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    compassCenter.semantics.mode = UI::UISemanticsMode::Exclude;
    auto compassCenterNode = ui.tree.createElement(
        viewportOrientationCompass_, compassCenter);
    if (!compassCenterNode) {
        return Tina::Core::failure(std::move(compassCenterNode.error()));
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

    viewportStatusOverlayLayout_ = fixedSize(276.0F, 24.0F);
    viewportStatusOverlayLayout_.placement = UI::UILayoutPlacement::Overlay;
    viewportStatusOverlayLayout_.overlay.horizontal = UI::UIAxisAlignment::End;
    viewportStatusOverlayLayout_.overlay.vertical = UI::UIAxisAlignment::Start;
    viewportStatusOverlayLayout_.overlay.offset.x =
        UI::UILayoutLength::Px(ui.productTheme.spacing.space3);
    viewportStatusOverlayLayout_.overlay.offset.y =
        UI::UILayoutLength::Px(ui.productTheme.spacing.space3);
    UI::UIElementDescriptor viewportStatusDescriptor =
        UI::makePanelElement(viewportStatusOverlayLayout_);
    viewportStatusDescriptor.visual.boxPaint =
        UI::makeSolidBox(UI::scaleColorAlpha(
            ui.productTheme.colors.surfaceContainer, 220));
    viewportStatusDescriptor.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    viewportStatusDescriptor.semantics.mode = UI::UISemanticsMode::Publish;
    viewportStatusDescriptor.semantics.name = "Viewport status";
    if (auto status = storeNode(
            ui.tree.createElement(viewportPreviewLayer_, viewportStatusDescriptor),
            viewportStatusOverlay_);
        !status) {
        return status;
    }
    UI::UILayoutStyle viewportStatusTextLayout = fillWidth(24.0F);
    viewportStatusTextLayout.padding = UI::UIEdgeSpacing::HorizontalVertical(
        ui.productTheme.spacing.space2, ui.productTheme.spacing.space0);
    UI::UIElementDescriptor viewportStatusTextDescriptor = UI::makeLabelElement(
        "World2D | Camera2D | Zoom 100% | Grid On | Snap On | 0 selected",
        viewportStatusTextLayout);
    viewportStatusTextDescriptor.textStyle = ui.compactText;
    viewportStatusTextDescriptor.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    viewportStatusTextDescriptor.semantics.mode = UI::UISemanticsMode::Exclude;
    if (auto status = storeNode(
            ui.tree.createElement(viewportStatusOverlay_, viewportStatusTextDescriptor),
            viewportStatusText_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, viewportStatusText_);
        !status) {
        return status;
    }

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildInspectorUi(
    UiBuildContext& ui, UI::UINodeId parent, UI::UINodeId& right) -> Tina::Core::Status
{
    inspectorDockLayout_ = growingRegion();
    inspectorDockLayout_.flexContainer.direction = UI::UIFlexDirection::Column;
    inspectorDockLayout_.flexContainer.gap.row = ui.productTheme.spacing.space3;
    inspectorDockLayout_.padding = UI::UIEdgeSpacing::All(ui.productTheme.spacing.space4);
    if (auto status = storeNode(ui.createSurface(parent, inspectorDockLayout_, UI::UISurfaceVariant::Filled),
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
    inspectorDirtyBadgeLayout_ = fixedSize(72.0F, 20.0F);
    inspectorDirtyBadgeLayout_.visibility = UI::UIVisibility::Collapsed;
    if (auto status = storeNode(
            ui.createBadge(inspectorHeader->actions, "Modified",
                           inspectorDirtyBadgeLayout_,
                           UI::UIBadgeTone::Accent),
            inspectorDirtyBadge_);
        !status) {
        return status;
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
    if (auto status = storeNode(ui.createIconButton(
                                    inspectorHeader->actions, EditorIcon::ChevronRight,
                                    "Hide Inspector"),
                                inspectorCollapseButton_);
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

    UI::UINodeId& inspectorContent = inspectorContent_;
    inspectorContentLayout_ = {};
    inspectorContentLayout_.size.width = UI::UILayoutLength::Percent(100.0F);
    inspectorContentLayout_.flexItem.shrink = 0.0F;
    inspectorContentLayout_.padding = UI::UIEdgeSpacing::All(ui.productTheme.spacing.space4);
    inspectorContentLayout_.flexContainer.gap.row = ui.productTheme.spacing.space3;
    inspectorContentLayout_.visibility = UI::UIVisibility::Collapsed;
    if (auto status = storeNode(ui.createSurface(inspectorScroll_, inspectorContentLayout_,
                                              UI::UISurfaceVariant::Filled),
                                inspectorContent_);
        !status) {
        return status;
    }

    inspectorEmptyStateLayout_ = fillWidth(180.0F);
    inspectorEmptyStateLayout_.flexItem.grow = 1.0F;
    inspectorEmptyStateLayout_.flexItem.shrink = 1.0F;
    inspectorEmptyStateLayout_.flexContainer.direction = UI::UIFlexDirection::Column;
    inspectorEmptyStateLayout_.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    inspectorEmptyStateLayout_.flexContainer.justifyContent = UI::UIJustifyContent::Center;
    inspectorEmptyStateLayout_.flexContainer.gap.row = ui.productTheme.spacing.space2;
    inspectorEmptyStateLayout_.padding = UI::UIEdgeSpacing::All(ui.productTheme.spacing.space6);
    UI::UIElementDescriptor inspectorEmptyStateDescriptor =
        UI::makePanelElement(inspectorEmptyStateLayout_);
    inspectorEmptyStateDescriptor.visual.boxPaint =
        UI::makeSolidBox(ui.productTheme.colors.surfaceContainerLow);
    inspectorEmptyStateDescriptor.semantics.name = "Inspector empty state";
    inspectorEmptyStateDescriptor.semantics.description =
        "Select a scene node or project asset to inspect its properties";
    if (auto status = storeNode(
            ui.tree.createElement(inspectorScroll_, inspectorEmptyStateDescriptor),
            inspectorEmptyState_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createLabel(inspectorEmptyState_, "No selection", fillWidth(24.0F),
                           ui.sectionText),
            inspectorEmptyStateTitle_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createLabel(inspectorEmptyState_,
                           "Select a scene node or project asset to inspect its properties.",
                           fillWidth(44.0F), ui.secondaryText),
            inspectorEmptyStateText_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, inspectorEmptyStateText_);
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
    if (auto status = setSingleLineEllipsis(ui.tree, inspectorName_);
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
    if (auto status = setSingleLineEllipsis(ui.tree, inspectorKind_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(inspectorContent, {}, fillWidth(24.0F), ui.compactText),
                                inspectorNote_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, inspectorNote_);
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
    inspectorAssetRowLayout_ = assetRow->rootLayout;
    if (auto status = storeNode(ui.createLabel(assetRow->value, {}, fillWidth(42.0F), ui.secondaryText),
                                inspectorAssetPath_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, inspectorAssetPath_);
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
    if (auto status = storeNode(
            ui.createIconButton(inspectorTransformHeader_, EditorIcon::Refresh,
                                "Reset transform", {}, false),
            resetTransformButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createIconButton(inspectorTransformHeader_, EditorIcon::Apply,
                                "Apply transform", {}, false),
            applyTransformButton_);
        !status) {
        return status;
    }
    inspectorTransformFieldsLayout_.size.width =
        UI::UILayoutLength::Percent(100.0F);
    inspectorTransformFieldsLayout_.flexItem.shrink = 0.0F;
    inspectorTransformFieldsLayout_.flexContainer.direction =
        UI::UIFlexDirection::Column;
    inspectorTransformFieldsLayout_.flexContainer.gap =
        UI::UILayoutGap::All(ui.productTheme.spacing.space2);
    if (auto status = storeNode(
            ui.createPanel(inspectorContent, inspectorTransformFieldsLayout_),
            inspectorTransformFields_);
        !status) {
        return status;
    }
    const auto createTransformVector =
        [&](Tina::Core::usize rowIndex, std::string_view caption,
            const std::array<InspectorTransformField, 3>& fields,
            const std::array<std::string_view, 3>& values,
            const std::array<std::string_view, 3>& accessibleNames,
            const std::array<UI::UINodeId*, 3>& valueNodes)
        -> Tina::Core::Status {
        UI::UILayoutStyle rowLayout = fillWidth(
            ui.productTheme.controls.textEditHeight);
        rowLayout.flexItem.shrink = 0.0F;

        constexpr std::array<std::string_view, 3> AxisLabels{"X", "Y", "Z"};
        const Tina::Core::usize visibleValueCount =
            workspaceMode_ == WorkspaceMode::World3D
                ? 3U
                : rowIndex == 1U ? 1U : 2U;
        UI::UIGridTrackList valueColumns{};
        if (visibleValueCount == 1U) {
            valueColumns = UI::UIGridTrackList::Of({UI::UIGridTrack::Fr()});
        } else if (visibleValueCount == 2U) {
            valueColumns = UI::UIGridTrackList::Of(
                {UI::UIGridTrack::Fr(), UI::UIGridTrack::Fr()});
        } else {
            valueColumns = UI::UIGridTrackList::Of({
                UI::UIGridTrack::Fr(), UI::UIGridTrack::Fr(),
                UI::UIGridTrack::Fr(),
            });
        }
        auto row = EditorPropertyRow::Build(
            ui.tree, inspectorTransformFields_, ui.productTheme, caption,
            ui.secondaryText, rowLayout, InspectorTransformLabelWidth,
            valueColumns);
        if (!row) {
            return Tina::Core::failure(std::move(row.error()));
        }
        inspectorTransformValueGrids_[rowIndex] = {
            .root = row->value,
            .layout = row->valueLayout,
        };

        for (Tina::Core::usize axisIndex = 0;
             axisIndex < fields.size(); ++axisIndex) {
            const InspectorTransformField field = fields[axisIndex];
            const bool visible = workspaceMode_ == WorkspaceMode::World3D ||
                !inspectorTransformFieldRequires3D(field);
            UI::UILayoutStyle groupLayout{};
            groupLayout.size.height = UI::UILayoutLength::Px(
                ui.productTheme.controls.textEditHeight);
            groupLayout.minMax.minWidth = UI::UILayoutLength::Px(
                InspectorTransformAxisMinWidth);
            groupLayout.minMax.maxWidth = UI::UILayoutLength::Px(
                InspectorTransformAxisMaxWidth);
            groupLayout.containerLayout = UI::UIContainerLayout::Grid;
            groupLayout.gridContainer.columns = UI::UIGridTrackList::Of({
                UI::UIGridTrack::Px(InspectorTransformAxisLabelWidth),
                UI::UIGridTrack::Fr(),
            });
            groupLayout.gridContainer.rows = UI::UIGridTrackList::Of(
                {UI::UIGridTrack::Fr()});
            groupLayout.gridContainer.alignItems = UI::UIAxisAlignment::Center;
            groupLayout.gridContainer.gap.column = ui.productTheme.spacing.space1;
            groupLayout.visibility = visible ? UI::UIVisibility::Visible
                                             : UI::UIVisibility::Collapsed;
            auto group = ui.tree.createElement(
                row->value, UI::makePanelElement(groupLayout));
            if (!group) {
                return Tina::Core::failure(std::move(group.error()));
            }
            inspectorTransformAxisFields_[
                inspectorTransformFieldIndex(field)] = {
                    .root = *group,
                    .layout = groupLayout,
                };

            UI::UILayoutStyle axisLabelLayout{};
            axisLabelLayout.gridItem.row = 0U;
            axisLabelLayout.gridItem.column = 0U;
            axisLabelLayout.gridItem.alignSelf = UI::UIAlignSelf::Stretch;
            UI::UIElementDescriptor axisLabel = UI::makeLabelElement(
                AxisLabels[axisIndex], axisLabelLayout);
            axisLabel.textStyle = ui.compactText;
            axisLabel.contentAlignment.horizontal = UI::UIAxisAlignment::Center;
            axisLabel.contentAlignment.vertical = UI::UIAxisAlignment::Center;
            axisLabel.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
            axisLabel.semantics.mode = UI::UISemanticsMode::Exclude;
            auto axisLabelNode = ui.tree.createElement(*group, axisLabel);
            if (!axisLabelNode) {
                return Tina::Core::failure(std::move(axisLabelNode.error()));
            }

            UI::UILayoutStyle inputLayout{};
            inputLayout.size.height = UI::UILayoutLength::Px(
                ui.productTheme.controls.textEditHeight);
            inputLayout.gridItem.row = 0U;
            inputLayout.gridItem.column = 1U;
            inputLayout.gridItem.alignSelf = UI::UIAlignSelf::Stretch;
            UI::UIElementDescriptor input = UI::makeTextEditElement(
                values[axisIndex], inputLayout);
            input.textStyle = ui.compactText;
            input.semantics.name = accessibleNames[axisIndex];
            input.semantics.useContentAsName = false;
            input.enabled = visible;
            auto inputNode = ui.tree.createElement(*group, input);
            if (!inputNode) {
                return Tina::Core::failure(std::move(inputNode.error()));
            }
            *valueNodes[axisIndex] = *inputNode;
        }
        return Tina::Core::success();
    };
    if (auto status = createTransformVector(
            0U, "Position",
            {InspectorTransformField::PositionX,
             InspectorTransformField::PositionY,
             InspectorTransformField::PositionZ},
            {"0", "0", "0"},
            {"Position X", "Position Y", "Position Z"},
            {&inspectorPositionX_, &inspectorPositionY_,
             &inspectorPositionZ_});
        !status) {
        return status;
    }
    if (auto status = createTransformVector(
            1U, "Rotation",
            {InspectorTransformField::RotationX,
             InspectorTransformField::RotationY,
             InspectorTransformField::RotationZ},
            {"0", "0", "0"},
            {"Rotation X", "Rotation Y", "Rotation Z"},
            {&inspectorRotationX_, &inspectorRotationY_,
             &inspectorRotationZ_});
        !status) {
        return status;
    }
    if (auto status = createTransformVector(
            2U, "Scale",
            {InspectorTransformField::ScaleX,
             InspectorTransformField::ScaleY,
             InspectorTransformField::ScaleZ},
            {"1", "1", "1"},
            {"Scale X", "Scale Y", "Scale Z"},
            {&inspectorScaleX_, &inspectorScaleY_, &inspectorScaleZ_});
        !status) {
        return status;
    }
    inspectorTransformErrorLayout_ = fillWidth(30.0F);
    inspectorTransformErrorLayout_.flexItem.shrink = 0.0F;
    inspectorTransformErrorLayout_.padding =
        UI::UIEdgeSpacing::HorizontalVertical(
            ui.productTheme.spacing.space2, 0.0F);
    inspectorTransformErrorLayout_.visibility = UI::UIVisibility::Collapsed;
    UI::UITextStyle transformErrorText = ui.secondaryText;
    transformErrorText.color = ui.productTheme.colors.error;
    UI::UIElementDescriptor transformError = UI::makeLabelElement(
        {}, inspectorTransformErrorLayout_);
    transformError.textStyle = transformErrorText;
    transformError.semantics.name = "Transform input error";
    transformError.semantics.useContentAsName = false;
    transformError.semantics.liveSetting =
        UI::UISemanticsLiveSetting::Assertive;
    transformError.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    if (auto status = storeNode(
            ui.tree.createElement(inspectorTransformFields_, transformError),
            inspectorTransformError_);
        !status) {
        return status;
    }
    const auto createNodePropertySection =
        [&](NodePropertySectionUi& section, std::string_view name,
            std::string_view activeCaption,
            std::span<const InspectorNodePropertyFieldRow> fieldRows,
            bool withAssign,
            std::string_view applyCaption,
            bool withPointLightColor = false) -> Tina::Core::Status {
        section.rootLayout.size.width = UI::UILayoutLength::Percent(100.0F);
        section.rootLayout.flexItem.shrink = 0.0F;
        section.rootLayout.flexContainer.direction =
            UI::UIFlexDirection::Column;
        section.rootLayout.flexContainer.gap =
            UI::UILayoutGap::All(ui.productTheme.spacing.space2);
        section.rootLayout.visibility = UI::UIVisibility::Collapsed;
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
        UI::UINodeId activeLabel{};
        if (auto status = storeNode(
                ui.createLabel(headerRow, activeCaption, growingRegion(),
                               ui.secondaryText),
                activeLabel);
            !status) {
            return status;
        }
        UI::UIElementDescriptor switchDesc = UI::makeSwitchElement({
            .accessibleName = activeCaption,
            .size = UI::UISwitchSize::Compact,
        });
        switchDesc.enabled = false;
        if (auto status = storeNode(ui.tree.createElement(headerRow, switchDesc),
                                    section.activeSwitch);
            !status) {
            return status;
        }
        if (withAssign) {
            // Sprite2D/AnimatedSprite2D use one canonical resource slot. The
            // slot is both the visible binding and the Project Assets drop
            // target; the assign action only reads the current Project Assets
            // selection through NodeAssignSprite.
            UI::UILayoutStyle resourceRowLayout = fillWidth(
                ui.productTheme.controls.textEditHeight);
            resourceRowLayout.flexItem.shrink = 0.0F;
            auto resourceRow = EditorPropertyRow::Build(
                ui.tree, section.collapsible.content, ui.productTheme,
                "Texture", ui.secondaryText, resourceRowLayout,
                EditorPropertyLabelWidth,
                UI::UIGridTrackList::Of({UI::UIGridTrack::Fr()}));
            if (!resourceRow) {
                return Tina::Core::failure(std::move(resourceRow.error()));
            }
            UI::UILayoutStyle resourceSlotLayout = fillWidth(
                ui.productTheme.controls.textEditHeight);
            resourceSlotLayout.flexContainer.direction = UI::UIFlexDirection::Row;
            resourceSlotLayout.flexContainer.alignItems = UI::UIAxisAlignment::Center;
            resourceSlotLayout.flexContainer.gap.column = ui.productTheme.spacing.space2;
            resourceSlotLayout.padding = UI::UIEdgeSpacing::HorizontalVertical(
                ui.productTheme.spacing.space2, 0.0F);
            UI::UIElementDescriptor resourceSlotDescriptor =
                UI::makePanelElement(resourceSlotLayout);
            resourceSlotDescriptor.visual.boxPaint = UI::makeSolidBox(
                ui.productTheme.colors.surfaceContainerLow);
            resourceSlotDescriptor.semantics.name = "Sprite resource drop target";
            resourceSlotDescriptor.semantics.description =
                "Drop a Sprite or Texture2D from Project Assets here";
            auto resourceSlot = ui.tree.createElement(
                resourceRow->value, resourceSlotDescriptor);
            if (!resourceSlot) {
                return Tina::Core::failure(std::move(resourceSlot.error()));
            }
            section.resourceSlot = *resourceSlot;
            if (auto status = storeNode(
                    ui.createLabel(section.resourceSlot,
                                   "Drop Sprite or Texture2D",
                                   growingRegion(), ui.compactText),
                    section.resourceLabel);
                !status) {
                return status;
            }
            if (auto status = storeNode(
                    ui.createIconButton(section.resourceSlot, EditorIcon::Apply,
                                        "Assign selected Project Asset", {}, false),
                    section.resourceAssignButton);
                !status) {
                return status;
            }
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
                    .textEditLayout = inspectorValueInputLayout(
                        ui.productTheme),
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
        section.fieldCount = 0U;
        for (const InspectorNodePropertyFieldRow& fieldRow : fieldRows) {
            UI::UILayoutStyle rowStyle = fillWidth(ui.productTheme.controls.textEditHeight);
            const UI::UIGridTrackList valueColumns =
                fieldRow.valueCount == 1U
                    ? UI::UIGridTrackList::Of({UI::UIGridTrack::Fr()})
                    : UI::UIGridTrackList::Of(
                          {UI::UIGridTrack::Fr(), UI::UIGridTrack::Fr()});
            auto row = EditorPropertyRow::Build(
                ui.tree, section.collapsible.content, ui.productTheme,
                fieldRow.caption, ui.secondaryText, rowStyle,
                EditorPropertyLabelWidth, valueColumns);
            if (!row) {
                return Tina::Core::failure(std::move(row.error()));
            }
            for (Tina::Core::usize valueIndex = 0U;
                 valueIndex < fieldRow.valueCount; ++valueIndex) {
                UI::UINodeId valueParent = row->value;
                if (fieldRow.valueCount > 1U) {
                    UI::UILayoutStyle groupLayout{};
                    groupLayout.size.height = UI::UILayoutLength::Px(
                        ui.productTheme.controls.textEditHeight);
                    groupLayout.containerLayout = UI::UIContainerLayout::Grid;
                    groupLayout.gridContainer.columns =
                        UI::UIGridTrackList::Of({
                            UI::UIGridTrack::Px(fieldRow.axisLabelWidth),
                            UI::UIGridTrack::Fr(),
                        });
                    groupLayout.gridContainer.rows =
                        UI::UIGridTrackList::Of({UI::UIGridTrack::Fr()});
                    groupLayout.gridContainer.alignItems =
                        UI::UIAxisAlignment::Center;
                    groupLayout.gridContainer.gap.column =
                        ui.productTheme.spacing.space1;
                    auto group = ui.tree.createElement(
                        row->value, UI::makePanelElement(groupLayout));
                    if (!group) {
                        return Tina::Core::failure(std::move(group.error()));
                    }
                    valueParent = *group;

                    UI::UILayoutStyle axisLayout{};
                    axisLayout.gridItem.row = 0U;
                    axisLayout.gridItem.column = 0U;
                    axisLayout.gridItem.alignSelf = UI::UIAlignSelf::Stretch;
                    UI::UIElementDescriptor axis = UI::makeLabelElement(
                        fieldRow.axisLabels[valueIndex], axisLayout);
                    axis.textStyle = ui.compactText;
                    axis.contentAlignment.horizontal =
                        UI::UIAxisAlignment::Center;
                    axis.contentAlignment.vertical = UI::UIAxisAlignment::Center;
                    axis.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
                    axis.semantics.mode = UI::UISemanticsMode::Exclude;
                    auto axisNode = ui.tree.createElement(valueParent, axis);
                    if (!axisNode) {
                        return Tina::Core::failure(std::move(axisNode.error()));
                    }
                }

                UI::UILayoutStyle valueStyle =
                    inspectorValueInputLayout(ui.productTheme);
                if (fieldRow.valueCount > 1U) {
                    valueStyle.gridItem.row = 0U;
                    valueStyle.gridItem.column = 1U;
                    valueStyle.gridItem.alignSelf = UI::UIAlignSelf::Stretch;
                }
                UI::UIElementDescriptor input = UI::makeTextEditElement(
                    "n/a", valueStyle);
                input.textStyle = ui.compactText;
                input.semantics.name = fieldRow.accessibleNames[valueIndex];
                input.semantics.useContentAsName = false;
                input.enabled = false;
                auto inputNode = ui.tree.createElement(valueParent, input);
                if (!inputNode) {
                    return Tina::Core::failure(std::move(inputNode.error()));
                }
                section.fields[section.fieldCount++] = *inputNode;
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
        const std::array<InspectorNodePropertyFieldRow, 4> spriteFields{{
            {.caption = "Size", .axisLabels = {"X", "Y"},
             .accessibleNames = {"Sprite size X", "Sprite size Y"},
             .valueCount = 2U, .axisLabelWidth = InspectorTransformAxisLabelWidth},
            {.caption = "Pivot", .axisLabels = {"X", "Y"},
             .accessibleNames = {"Sprite pivot X", "Sprite pivot Y"},
             .valueCount = 2U, .axisLabelWidth = InspectorTransformAxisLabelWidth},
            {.caption = "Sort Layer", .accessibleNames = {"Sprite sort layer", ""},
             .valueCount = 1U},
            {.caption = "Order", .accessibleNames = {"Sprite order", ""},
             .valueCount = 1U},
        }};
        if (auto status = createNodePropertySection(
                nodePropertySections_[0], "Rendering", "Visible", spriteFields,
                true, "Apply Rendering");
            !status) {
            return status;
        }
        const std::array<InspectorNodePropertyFieldRow, 3> cameraFields{{
            {.caption = "Height m", .accessibleNames = {"Camera height", ""}},
            {.caption = "Ref Px/m", .accessibleNames = {"Camera reference pixels per meter", ""}},
            {.caption = "Ref Px H", .accessibleNames = {"Camera reference pixel height", ""}},
        }};
        if (auto status = createNodePropertySection(
                nodePropertySections_[1], "Camera", "Active", cameraFields,
                false, "Apply Camera");
            !status) {
            return status;
        }
        const std::array<InspectorNodePropertyFieldRow, 3> lightFields{{
            {.caption = "Intensity", .accessibleNames = {"Light intensity", ""}},
            {.caption = "Radius", .accessibleNames = {"Light radius", ""}},
            {.caption = "Src Radius", .accessibleNames = {"Light source radius", ""}},
        }};
        if (auto status = createNodePropertySection(
                nodePropertySections_[2], "Light", "Active", lightFields,
                false, "Apply Light", true);
            !status) {
            return status;
        }
        const std::array<InspectorNodePropertyFieldRow, 2> occluderFields{{
            {.caption = "Start", .axisLabels = {"X", "Y"},
             .accessibleNames = {"Occluder start X", "Occluder start Y"},
             .valueCount = 2U, .axisLabelWidth = InspectorTransformAxisLabelWidth},
            {.caption = "End", .axisLabels = {"X", "Y"},
             .accessibleNames = {"Occluder end X", "Occluder end Y"},
             .valueCount = 2U, .axisLabelWidth = InspectorTransformAxisLabelWidth},
        }};
        if (auto status = createNodePropertySection(
                nodePropertySections_[3], "Occlusion", "Active",
                occluderFields, false, "Apply Occlusion");
            !status) {
            return status;
        }
        const std::array<InspectorNodePropertyFieldRow, 2> animationFields{{
            {.caption = "Clip", .accessibleNames = {"Animation clip", ""}},
            {.caption = "Speed", .accessibleNames = {"Animation speed", ""}},
        }};
        if (auto status = createNodePropertySection(
                nodePropertySections_[4], "Animation", "Auto Play",
                animationFields, false, "Apply Animation");
            !status) {
            return status;
        }
        const std::array<InspectorNodePropertyFieldRow, 2> meshFields{{
            {.caption = "Mesh", .accessibleNames = {"Mesh asset", ""}},
            {.caption = "Material", .accessibleNames = {"Material asset", ""}},
        }};
        if (auto status = createNodePropertySection(
                nodePropertySections_[MeshPropertiesSectionIndex], "Rendering",
                "Visible", meshFields, false, {});
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
    auto parentRow = EditorPropertyRow::Build(
        ui.tree, inspectorContent, ui.productTheme, "Parent ID",
        ui.secondaryText, parentRowStyle);
    if (!parentRow) {
        return Tina::Core::failure(std::move(parentRow.error()));
    }
    inspectorHierarchyParentRowUi_ = {
        .root = parentRow->root,
        .layout = parentRow->rootLayout,
    };
    const UI::UILayoutStyle parentValueStyle =
        inspectorValueInputLayout(ui.productTheme);
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
    tilePaletteGridLayout_ = growingRegion();
    tilePaletteGridLayout_.minMax.minHeight = UI::UILayoutLength::Px(96.0F);
    auto tilePalette = ui.tree.createElement(
        inspectorContent,
        UI::makeVirtualGridViewElement(
            {.materializedItemCapacity = TilePaletteMaterializedCapacity},
            tilePaletteGridLayout_));
    if (!tilePalette) {
        return Tina::Core::failure(std::move(tilePalette.error()));
    }
    tilePaletteGrid_ = *tilePalette;
    if (auto status = ui.tree.setVirtualGridViewStyle(
            tilePaletteGrid_, UI::UIVirtualGridViewStyle{
                .minimumItemWidth = TilePaletteMinimumItemWidth,
                .itemHeight = TilePaletteItemHeight,
                .overscanRows = 1,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = TilePaletteItemHeight,
                .itemTextOverflow = UI::UITextOverflow::Ellipsis,
            }); !status) {
        return status;
    }
    if (auto status = ui.tree.setVirtualGridViewPaint(
            tilePaletteGrid_, UI::makeVirtualGridViewPaint(ui.productTheme)); !status) {
        return status;
    }
    if (auto status = ui.tree.setVirtualGridViewDataSource(
            tilePaletteGrid_, tilePaletteDataSource()); !status) {
        return status;
    }
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

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildTimelineUi(
    UiBuildContext& ui, UI::UINodeId parent, UI::UINodeId& animationTimeline)
    -> Tina::Core::Status
{
    animationPanelLayout_ = growingRegion();
    animationPanelLayout_.flexContainer.direction = UI::UIFlexDirection::Column;
    animationPanelLayout_.flexContainer.gap.row = ui.productTheme.spacing.space2;
    animationPanelLayout_.padding = UI::UIEdgeSpacing::All(ui.productTheme.spacing.space4);
    animationPanelLayout_.visibility = UI::UIVisibility::Collapsed;
    if (auto status = storeNode(ui.createSurface(parent, animationPanelLayout_,
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
    if (auto status = setSingleLineEllipsis(ui.tree, animationStatus_);
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
    if (auto status = storeNode(ui.createIconButton(
                                    animationHeader->actions, EditorIcon::ChevronDown,
                                    "Hide Animation panel"),
                                animationCollapseButton_);
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

    UI::UINodeId animationEventMarkerRow{};
    UI::UILayoutStyle animationEventMarkerRowStyle = fillWidth(18.0F);
    animationEventMarkerRowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    animationEventMarkerRowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    animationEventMarkerRowStyle.flexContainer.gap.column = ui.productTheme.spacing.space2;
    if (auto status = storeNode(
            ui.createPanel(animationTimeline, animationEventMarkerRowStyle),
            animationEventMarkerRow);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createLabel(animationEventMarkerRow, "Events",
                           fixedSize(48.0F, 18.0F), ui.secondaryText),
            animationEventMarkerLabel_);
        !status) {
        return status;
    }
    for (u32 slot = 0; slot < animationEventMarkerButtons_.size(); ++slot) {
        UI::UILayoutStyle markerLayout = fixedSize(AnimationFrameSlotWidth, 18.0F);
        markerLayout.flexItem.shrink = 0.0F;
        if (auto status = storeNode(
                ui.createLabel(animationEventMarkerRow, "-", markerLayout,
                               ui.secondaryText),
                animationEventMarkerButtons_[slot]);
            !status) {
            return status;
        }
    }

    UI::UINodeId animationRulerRow{};
    UI::UILayoutStyle animationRulerStyle = fillWidth(20.0F);
    animationRulerStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    animationRulerStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    animationRulerStyle.flexContainer.gap.column = ui.productTheme.spacing.space3;
    if (auto status = storeNode(ui.createPanel(animationTimeline, animationRulerStyle),
                                animationRulerRow);
        !status) {
        return status;
    }
    UI::UILayoutStyle animationTimelineScaleStyle = fixedSize(0.0F, 20.0F);
    animationTimelineScaleStyle.size.width = UI::UILayoutLength::Auto();
    animationTimelineScaleStyle.flexItem.grow = 1.0F;
    animationTimelineScaleStyle.flexItem.shrink = 1.0F;
    animationTimelineScaleStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    if (auto status = storeNode(ui.createLabel(animationRulerRow, {},
                                               animationTimelineScaleStyle,
                                               ui.secondaryText),
                                animationTimelineScale_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, animationTimelineScale_);
        !status) {
        return status;
    }
    animationHoverTimeLayout_ = fixedSize(176.0F, 20.0F);
    animationHoverTimeLayout_.flexItem.shrink = 0.0F;
    animationHoverTimeLayout_.visibility = UI::UIVisibility::Collapsed;
    if (auto status = storeNode(ui.createLabel(
                                    animationRulerRow, {},
                                    animationHoverTimeLayout_, ui.accentText),
                                animationHoverTime_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, animationHoverTime_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(animationRulerRow, {},
                                               fixedSize(188.0F, 20.0F),
                                               ui.accentText),
                                animationPlayhead_);
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
    if (auto status = setSingleLineEllipsis(ui.tree, animationSelection_);
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

auto EditorWorkspaceState::buildOutputPanelUi(
    UiBuildContext& ui, UI::UINodeId parent, UI::UINodeId& outputPanel)
    -> Tina::Core::Status
{
    outputPanelLayout_ = growingRegion();
    outputPanelLayout_.flexContainer.direction = UI::UIFlexDirection::Column;
    outputPanelLayout_.flexContainer.gap.row = ui.productTheme.spacing.space3;
    outputPanelLayout_.padding = UI::UIEdgeSpacing::All(ui.productTheme.spacing.space4);
    outputPanelLayout_.visibility = UI::UIVisibility::Collapsed;
    if (auto status = storeNode(
            ui.createSurface(parent, outputPanelLayout_, UI::UISurfaceVariant::Filled),
            outputPanel);
        !status) {
        return status;
    }

    auto outputHeader = EditorPanelHeader::Build(
        ui.tree, outputPanel, ui.productTheme, "Output", ui.sectionText,
        fillWidth(ui.productTheme.controls.buttonHeight));
    if (!outputHeader) {
        return Tina::Core::failure(std::move(outputHeader.error()));
    }
    if (auto status = storeNode(ui.createIconButton(
                                    outputHeader->actions, EditorIcon::ChevronDown,
                                    "Hide Output panel"),
                                outputCollapseButton_);
        !status) {
        return status;
    }

    UI::UINodeId outputToolbar{};
    UI::UILayoutStyle outputToolbarStyle = fillWidth(24.0F);
    outputToolbarStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    outputToolbarStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    outputToolbarStyle.flexContainer.gap.column = ui.productTheme.spacing.space1;
    if (auto status = storeNode(ui.createPanel(outputPanel, outputToolbarStyle),
                                outputToolbar);
        !status) {
        return status;
    }
    constexpr std::array<std::string_view, 4> outputFilterLabels{
        "All 0", "Info 0", "Warn 0", "Error 0"};
    for (u32 index = 0; index < outputFilterButtons_.size(); ++index) {
        if (auto status = storeNode(
                ui.createSegmentedButton(outputToolbar, outputFilterLabels[index],
                                         fixedSize(72.0F, 24.0F)),
                outputFilterButtons_[index]);
            !status) {
            return status;
        }
    }
    UI::UILayoutStyle outputSummaryStyle = fixedSize(0.0F, 20.0F);
    outputSummaryStyle.size.width = UI::UILayoutLength::Auto();
    outputSummaryStyle.flexItem.grow = 1.0F;
    outputSummaryStyle.flexItem.shrink = 1.0F;
    outputSummaryStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    if (auto status = storeNode(ui.createLabel(outputToolbar, {}, outputSummaryStyle,
                                               ui.secondaryText),
                                outputSummary_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, outputSummary_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(outputToolbar, "Details", fixedSize(72.0F, 24.0F),
                            false, UI::UIStyleRoleId::ButtonOutlined),
            outputDetailsToggleButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    outputToolbar, EditorIcon::Focus,
                                    "Locate selected output target",
                                    fixedSize(24.0F, 24.0F)),
                                outputLocateButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createIconButton(
                                    outputToolbar, EditorIcon::Delete,
                                    "Clear output messages", fixedSize(24.0F, 24.0F)),
                                outputClearButton_);
        !status) {
        return status;
    }

    UI::UILayoutStyle outputGridStyle = growingRegion();
    outputGridStyle.minMax.minHeight = UI::UILayoutLength::Px(72.0F);
    auto outputGrid = ui.tree.createElement(
        outputPanel,
        UI::makeDataGridElement(
            {
                .columnCapacity = OutputColumnCapacity,
                .materializedRowCapacity = OutputMaterializedCapacity,
            },
            outputGridStyle));
    if (!outputGrid) {
        return Tina::Core::failure(std::move(outputGrid.error()));
    }
    outputGrid_ = *outputGrid;
    outputGridStyle_ = UI::UIDataGridStyle{
        .columnHeaderHeight = 24.0F,
        .rowHeight = 24.0F,
        .overscanRows = 2U,
        .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
        .wheelStep = 48.0F,
        .headerTextOverflow = UI::UITextOverflow::Ellipsis,
        .cellTextOverflow = UI::UITextOverflow::Ellipsis,
    };
    if (auto status = ui.tree.setDataGridStyle(
            outputGrid_, outputGridStyle_);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setDataGridPaint(
            outputGrid_, UI::makeDataGridPaint(ui.productTheme));
        !status) {
        return status;
    }
    if (auto status = ui.tree.setDataGridDataSource(
            outputGrid_, outputGridDataSource());
        !status) {
        return status;
    }

    outputDetailsLayout_ = fillWidth(72.0F);
    outputDetailsLayout_.flexItem.shrink = 0.0F;
    outputDetailsLayout_.padding =
        UI::UIEdgeSpacing::All(ui.productTheme.spacing.space3);
    outputDetailsLayout_.visibility = UI::UIVisibility::Collapsed;
    if (auto status = storeNode(
            ui.createLabel(outputPanel, {}, outputDetailsLayout_, ui.bodyText),
            outputDetails_);
        !status) {
        return status;
    }
    return setSingleLineEllipsis(ui.tree, outputDetails_);
}

auto EditorWorkspaceState::buildLayoutDebuggerUi(
    UiBuildContext& ui, UI::UINodeId parent, UI::UINodeId& layoutPanel)
    -> Tina::Core::Status
{
    layoutDebugPanelLayout_ = fixedSize(760.0F, 560.0F);
    // Keep the initial composition inside the root viewport; the interaction
    // state machine further clamps every drag/resize update to that same rect.
    layoutDebugPanelLayout_.minMax.maxWidth = UI::UILayoutLength::Percent(100.0F);
    layoutDebugPanelLayout_.minMax.maxHeight = UI::UILayoutLength::Percent(100.0F);
    layoutDebugPanelLayout_.flexItem.shrink = 0.0F;
    layoutDebugPanelLayout_.flexContainer.direction = UI::UIFlexDirection::Column;
    layoutDebugPanelLayout_.flexContainer.gap.row = ui.productTheme.spacing.space3;
    layoutDebugPanelLayout_.padding =
        UI::UIEdgeSpacing::All(ui.productTheme.spacing.space4);
    layoutDebugPanelLayout_.placement = UI::UILayoutPlacement::Overlay;
    // Keep the floating window origin in logical top-left coordinates so
    // pointer dragging can apply deltas directly without reversing axes.
    layoutDebugPanelLayout_.overlay.horizontal = UI::UIAxisAlignment::Start;
    layoutDebugPanelLayout_.overlay.vertical = UI::UIAxisAlignment::Start;
    layoutDebugPanelLayout_.overlay.offset.x =
        UI::UILayoutLength::Px(ui.productTheme.spacing.space4);
    layoutDebugPanelLayout_.overlay.offset.y =
        UI::UILayoutLength::Px(ui.productTheme.controls.commandBarHeight +
                               ui.productTheme.controls.tabHeight +
                               ui.productTheme.spacing.space4);
    layoutDebugPanelLayout_.visibility = UI::UIVisibility::Collapsed;
    if (auto status = storeNode(
            ui.createSurface(parent, layoutDebugPanelLayout_,
                             UI::UISurfaceVariant::Filled),
            layoutPanel);
        !status) {
        return status;
    }

    auto header = EditorPanelHeader::Build(
        ui.tree, layoutPanel, ui.productTheme, "Layout Debugger", ui.sectionText,
        fillWidth(ui.productTheme.controls.buttonHeight));
    if (!header) {
        return Tina::Core::failure(std::move(header.error()));
    }
    layoutDebugHeader_ = header->root;
    // The header surface is the drag target when the pointer does not land on
    // one of its action buttons. Capture-phase routing keeps those buttons'
    // normal activation behavior intact.
    if (auto status = ui.tree.setPointerHitPolicy(
            layoutDebugHeader_, UI::UIPointerHitPolicy::Targetable);
        !status) {
        return status;
    }
    UI::UILayoutStyle summaryLayout = growingRegion();
    summaryLayout.size.height = UI::UILayoutLength::Px(22.0F);
    if (auto status = storeNode(
            ui.createLabel(header->actions, "Waiting for committed layout",
                           summaryLayout, ui.secondaryText),
            layoutDebugSummary_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, layoutDebugSummary_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(header->actions, "All Bounds",
                            fixedSize(92.0F, 24.0F), true,
                            UI::UIStyleRoleId::ButtonOutlined),
            layoutDebugShowAllButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(header->actions, "Pick", fixedSize(72.0F, 24.0F),
                            true, UI::UIStyleRoleId::ButtonOutlined),
            layoutDebugPickButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createIconButton(header->actions, EditorIcon::ChevronDown,
                                "Hide Layout Debugger panel"),
            layoutDebugCollapseButton_);
        !status) {
        return status;
    }

    UI::UINodeId content{};
    UI::UILayoutStyle contentLayout = growingRegion();
    contentLayout.minMax.minHeight = UI::UILayoutLength::Px(96.0F);
    contentLayout.containerLayout = UI::UIContainerLayout::Grid;
    contentLayout.gridContainer.columns = UI::UIGridTrackList::Of({
        UI::UIGridTrack::Fr(1.05F), UI::UIGridTrack::Fr(1.95F)});
    contentLayout.gridContainer.rows =
        UI::UIGridTrackList::Of({UI::UIGridTrack::Fr()});
    contentLayout.gridContainer.gap.column = ui.productTheme.spacing.space4;
    if (auto status = storeNode(ui.createPanel(layoutPanel, contentLayout), content);
        !status) {
        return status;
    }

    UI::UILayoutStyle treeLayout = growingRegion();
    treeLayout.gridItem.row = 0U;
    treeLayout.gridItem.column = 0U;
    auto tree = ui.tree.createElement(
        content,
        UI::makeTreeViewElement(
            {.materializedItemCapacity = LayoutDebugTreeMaterializedCapacity},
            treeLayout));
    if (!tree) {
        return Tina::Core::failure(std::move(tree.error()));
    }
    layoutDebugTree_ = *tree;
    if (auto status = ui.tree.setTreeViewStyle(
            layoutDebugTree_, UI::UITreeViewStyle{
                .rowHeight = ui.productTheme.controls.listRowHeight,
                .overscanRows = 3U,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = ui.productTheme.controls.listRowHeight * 2.0F,
                .indentation = 16.0F,
                // TreeView metrics require a positive disclosure extent; layout
                // debugging uses the slot for real expand/collapse affordances.
                .disclosureExtent = 10.0F,
                .disclosureGap = 0.0F,
            });
        !status) {
        return status;
    }
    if (auto status = ui.tree.setTreeViewPaint(
            layoutDebugTree_, UI::makeTreeViewPaint(ui.productTheme));
        !status) {
        return status;
    }
    if (auto status = ui.tree.setTreeViewDataSource(
            layoutDebugTree_, layoutDebugTreeDataSource());
        !status) {
        return status;
    }

    UI::UINodeId detailsScroll{};
    UI::UILayoutStyle detailsScrollLayout = growingRegion();
    detailsScrollLayout.gridItem.row = 0U;
    detailsScrollLayout.gridItem.column = 1U;
    if (auto status = storeNode(
            ui.tree.createElement(
                content, UI::makeScrollViewElement(detailsScrollLayout)),
            detailsScroll);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setScrollViewStyle(
            detailsScroll, UI::UIScrollViewStyle{
                .axes = UI::UIScrollAxes::Vertical,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = ui.productTheme.controls.listRowHeight,
            });
        !status) {
        return status;
    }
    UI::UINodeId details{};
    UI::UILayoutStyle detailsLayout{};
    detailsLayout.size.width = UI::UILayoutLength::Percent(100.0F);
    detailsLayout.flexItem.shrink = 0.0F;
    detailsLayout.flexContainer.direction = UI::UIFlexDirection::Column;
    detailsLayout.flexContainer.gap.row = ui.productTheme.spacing.space2;
    if (auto status = storeNode(ui.createPanel(detailsScroll, detailsLayout), details);
        !status) {
        return status;
    }
    constexpr std::array<std::string_view, LayoutDebugDetailRowCount>
        InitialDetails{
        "Select a committed node",
        "Local geometry",
        "World geometry",
        "Effective clip",
        "Content box",
        "Content intrinsic",
        "Measured size",
        "Intrinsic limits",
        "Parent basis",
        "Content basis",
        "Authored sizing",
        "Authored box",
        "Authored flex container",
        "Authored flex item",
        "Authored grid container",
        "Authored grid item",
        "Authored overlay",
        "Resolved sizing",
        "Resolved box",
        "Resolved flex container",
        "Resolved flex item",
        "Resolved grid container",
        "Resolved grid item",
        "Resolved overlay",
        "State",
    };
    for (u32 index = 0U; index < layoutDebugDetailLabels_.size(); ++index) {
        if (auto status = storeNode(
                ui.createLabel(details, InitialDetails[index], fillWidth(22.0F),
                               index == 0U ? ui.accentText : ui.compactText),
                layoutDebugDetailLabels_[index]);
            !status) {
            return status;
        }
        if (auto status = setSingleLineEllipsis(
                ui.tree, layoutDebugDetailLabels_[index]); !status) {
            return status;
        }
    }

    // Create the handle last so its resize glyph remains above the tree and
    // details scroll regions in paint and hit order.
    layoutDebugResizeHandleLayout_ = fixedSize(18.0F, 18.0F);
    layoutDebugResizeHandleLayout_.placement = UI::UILayoutPlacement::Overlay;
    layoutDebugResizeHandleLayout_.overlay.horizontal = UI::UIAxisAlignment::End;
    layoutDebugResizeHandleLayout_.overlay.vertical = UI::UIAxisAlignment::End;
    UI::UIElementDescriptor resizeHandle =
        UI::makePanelElement(layoutDebugResizeHandleLayout_);
    resizeHandle.visual.boxPaint = UI::makeSolidLine(
        ui.productTheme.colors.onSurfaceVariant,
        UI::UILogicalPoint{.x = 3.0F, .y = 15.0F},
        UI::UILogicalPoint{.x = 15.0F, .y = 3.0F}, 2.0F);
    resizeHandle.pointerHitPolicy = UI::UIPointerHitPolicy::Targetable;
    resizeHandle.semantics.mode = UI::UISemanticsMode::Exclude;
    if (auto status = storeNode(
            ui.tree.createElement(layoutPanel, resizeHandle),
            layoutDebugResizeHandle_);
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
    if (auto status = setSingleLineEllipsis(ui.tree, statusDocument_);
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
    UI::UILayoutStyle statusTaskStyle = statusDocumentStyle;
    statusTaskStyle.flexItem.grow = 0.9F;
    if (auto status = storeNode(ui.createLabel(statusBar, {}, statusTaskStyle, ui.secondaryText),
                                statusTask_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, statusTask_);
        !status) {
        return status;
    }
    UI::UILayoutStyle statusCatalogStyle = statusDocumentStyle;
    statusCatalogStyle.flexItem.grow = 0.8F;
    if (auto status = storeNode(ui.createLabel(statusBar, {}, statusCatalogStyle, ui.secondaryText),
                                statusCatalog_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, statusCatalog_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, statusPreview_);
        !status) {
        return status;
    }
    UI::UILayoutStyle statusActivityStyle = statusDocumentStyle;
    statusActivityStyle.flexItem.grow = 1.0F;
    statusActivityStyle.flexItem.shrink = 1.0F;
    if (auto status = storeNode(ui.createLabel(statusBar, {}, statusActivityStyle,
                                               ui.secondaryText),
                                statusActivity_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, statusActivity_);
        !status) {
        return status;
    }
    UI::UINodeId statusPanelControls{};
    UI::UILayoutStyle statusPanelControlsStyle{};
    statusPanelControlsStyle.size.height = UI::UILayoutLength::Px(24.0F);
    statusPanelControlsStyle.flexItem.shrink = 0.0F;
    statusPanelControlsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    statusPanelControlsStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    statusPanelControlsStyle.flexContainer.gap.column = ui.productTheme.spacing.space1;
    if (auto status = storeNode(
            ui.createPanel(statusBar, statusPanelControlsStyle),
            statusPanelControls);
        !status) {
        return status;
    }
    constexpr std::array<std::string_view, 2> BottomPanelLabels{
        "Animation", "Output"};
    for (u32 index = 0; index < bottomPanelButtons_.size(); ++index) {
        if (auto status = storeNode(
                ui.createSegmentedButton(
                    statusPanelControls, BottomPanelLabels[index],
                    fixedSize(index == 0U ? 82.0F : 72.0F, 24.0F)),
                bottomPanelButtons_[index]);
            !status) {
            return status;
        }
    }
    if (auto status = storeNode(
            ui.createSegmentedButton(
                statusPanelControls, "Layout",
                fixedSize(72.0F, 24.0F)),
            layoutDebugStatusButton_);
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
    if (auto status = setSingleLineEllipsis(ui.tree, statusSelection_);
        !status) {
        return status;
    }

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildMenuOverlaysUi(
    UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    const auto createMenu = [&](u32 index, float width) -> Tina::Core::Status {
        UI::UILayoutStyle layout{};
        layout.size.width = UI::UILayoutLength::Px(width);
        auto menu = ui.tree.createElement(
            parent,
            UI::makeMenuElement(
                {.placement = UI::UIMenuPlacement::Below,
                 .anchorGap = ui.productTheme.spacing.space1},
                layout));
        if (!menu) {
            return Tina::Core::failure(std::move(menu.error()));
        }
        mainMenus_[index] = *menu;
        return ui.tree.setMenuAnchor(mainMenus_[index], mainMenuAnchors_[index]);
    };
    constexpr std::array<float, MainMenuCount> menuWidths{
        236.0F, 220.0F, 220.0F, 210.0F};
    for (u32 index = 0; index < MainMenuCount; ++index) {
        if (auto status = createMenu(index, menuWidths[index]); !status) {
            return status;
        }
    }

    const auto createMenuItem = [&](
                                    UI::UINodeId menu, std::string_view text,
                                    UI::UIMenuItemKind kind, UI::UINodeId& output,
                                    bool enabled = true) -> Tina::Core::Status {
        UI::UILayoutStyle layout{};
        layout.size.height = UI::UILayoutLength::Px(
            kind == UI::UIMenuItemKind::Separator
                ? ui.productTheme.spacing.space4
                : ui.productTheme.controls.menuItemHeight);
        UI::UIElementDescriptor descriptor = UI::makeMenuItemElement(
            text, {.kind = kind}, layout);
        descriptor.enabled = enabled;
        return storeNode(ui.tree.createElement(menu, descriptor), output);
    };
    const auto appendSeparator = [&](UI::UINodeId menu) -> Tina::Core::Status {
        UI::UINodeId separator{};
        return createMenuItem(
            menu, {}, UI::UIMenuItemKind::Separator, separator, false);
    };

    constexpr u32 FileMenu = 0U;
    constexpr u32 EditMenu = 1U;
    constexpr u32 ViewMenu = 2U;
    constexpr u32 HelpMenu = HelpMainMenuIndex;
    if (auto status = createMenuItem(
            mainMenus_[FileMenu], "New Project", UI::UIMenuItemKind::Command,
            fileCreateProjectMenuItem_);
        !status) {
        return status;
    }
    if (auto status = createMenuItem(
            mainMenus_[FileMenu], "Open Project", UI::UIMenuItemKind::Command,
            fileOpenProjectMenuItem_);
        !status) {
        return status;
    }
    UI::UINodeId openRecentItem{};
    if (auto status = createMenuItem(
            mainMenus_[FileMenu], "Open Recent", UI::UIMenuItemKind::Submenu,
            openRecentItem);
        !status) {
        return status;
    }
    UI::UILayoutStyle recentMenuLayout{};
    recentMenuLayout.size.width = UI::UILayoutLength::Px(300.0F);
    auto recentMenu = ui.tree.createElement(
        parent, UI::makeMenuElement(
                    {.placement = UI::UIMenuPlacement::Below,
                     .anchorGap = ui.productTheme.spacing.space1},
                    recentMenuLayout));
    if (!recentMenu) {
        return Tina::Core::failure(std::move(recentMenu.error()));
    }
    fileOpenRecentSubmenu_ = *recentMenu;
    if (auto status = ui.tree.setMenuItemSubmenu(openRecentItem, fileOpenRecentSubmenu_); !status) {
        return status;
    }
    for (u32 index = 0; index < RecentProjectCapacity; ++index) {
        if (auto status = createMenuItem(fileOpenRecentSubmenu_, "", UI::UIMenuItemKind::Command,
                                         recentProjectMenuItems_[index], false); !status) {
            return status;
        }
    }
    if (auto status = createMenuItem(
            mainMenus_[FileMenu], "Import Files...", UI::UIMenuItemKind::Command,
            fileImportSourceMenuItem_);
        !status) {
        return status;
    }
    if (auto status = appendSeparator(mainMenus_[FileMenu]); !status) {
        return status;
    }
    if (auto status = createMenuItem(
            mainMenus_[FileMenu], "Save  Ctrl+S", UI::UIMenuItemKind::Command,
            fileSaveMenuItem_);
        !status) {
        return status;
    }
    if (auto status = createMenuItem(
            mainMenus_[FileMenu], "Save As  Ctrl+Shift+S",
            UI::UIMenuItemKind::Command, fileSaveAsMenuItem_);
        !status) {
        return status;
    }
    if (auto status = appendSeparator(mainMenus_[FileMenu]); !status) {
        return status;
    }
    if (auto status = createMenuItem(
            mainMenus_[FileMenu], "Close Document",
            UI::UIMenuItemKind::Command, fileCloseDocumentMenuItem_);
        !status) {
        return status;
    }

    if (auto status = createMenuItem(
            mainMenus_[EditMenu], "Undo  Ctrl+Z", UI::UIMenuItemKind::Command,
            editUndoMenuItem_);
        !status) {
        return status;
    }
    if (auto status = createMenuItem(
            mainMenus_[EditMenu], "Redo  Ctrl+Y", UI::UIMenuItemKind::Command,
            editRedoMenuItem_);
        !status) {
        return status;
    }
    if (auto status = appendSeparator(mainMenus_[EditMenu]); !status) {
        return status;
    }
    if (auto status = createMenuItem(
            mainMenus_[EditMenu], "Duplicate  Ctrl+D",
            UI::UIMenuItemKind::Command, editDuplicateMenuItem_);
        !status) {
        return status;
    }
    if (auto status = createMenuItem(
            mainMenus_[EditMenu], "Delete", UI::UIMenuItemKind::Command,
            editDeleteMenuItem_);
        !status) {
        return status;
    }

    UI::UINodeId workspaceSubmenuItem{};
    if (auto status = createMenuItem(
            mainMenus_[ViewMenu], "Workspace",
            UI::UIMenuItemKind::Submenu, workspaceSubmenuItem);
        !status) {
        return status;
    }
    UI::UILayoutStyle workspaceMenuLayout{};
    workspaceMenuLayout.size.width = UI::UILayoutLength::Px(150.0F);
    auto workspaceMenu = ui.tree.createElement(
        parent,
        UI::makeMenuElement(
            {.placement = UI::UIMenuPlacement::Right,
             .anchorGap = ui.productTheme.spacing.space1},
            workspaceMenuLayout));
    if (!workspaceMenu) {
        return Tina::Core::failure(std::move(workspaceMenu.error()));
    }
    viewWorkspaceSubmenu_ = *workspaceMenu;
    if (auto status = ui.tree.setMenuItemSubmenu(
            workspaceSubmenuItem, viewWorkspaceSubmenu_);
        !status) {
        return status;
    }
    if (auto status = createMenuItem(
            viewWorkspaceSubmenu_, "2D", UI::UIMenuItemKind::Radio,
            viewWorkspaceMenuItems_[0]);
        !status) {
        return status;
    }
    if (auto status = createMenuItem(
            viewWorkspaceSubmenu_, "3D", UI::UIMenuItemKind::Radio,
            viewWorkspaceMenuItems_[1]);
        !status) {
        return status;
    }
    if (auto status = createMenuItem(
            mainMenus_[ViewMenu], "Left Dock", UI::UIMenuItemKind::Check,
            viewPanelMenuItems_[0]);
        !status) {
        return status;
    }
    if (auto status = createMenuItem(
            mainMenus_[ViewMenu], "Inspector", UI::UIMenuItemKind::Check,
            viewPanelMenuItems_[1]);
        !status) {
        return status;
    }
    if (auto status = createMenuItem(
            mainMenus_[ViewMenu], "Layout Debugger", UI::UIMenuItemKind::Check,
            viewLayoutDebuggerMenuItem_);
        !status) {
        return status;
    }
    if (auto status = appendSeparator(mainMenus_[ViewMenu]); !status) {
        return status;
    }
    if (auto status = createMenuItem(
            mainMenus_[ViewMenu], "Frame All  Ctrl+0",
            UI::UIMenuItemKind::Command, viewFrameAllMenuItem_);
        !status) {
        return status;
    }
    if (auto status = createMenuItem(
            mainMenus_[ViewMenu], "Focus Selection  Ctrl+F",
            UI::UIMenuItemKind::Command, viewFocusSelectionMenuItem_);
        !status) {
        return status;
    }

    if (auto status = createMenuItem(
            mainMenus_[HelpMenu], "About Tina Editor",
            UI::UIMenuItemKind::Command, helpAboutMenuItem_);
        !status) {
        return status;
    }

    // The hierarchy owns a real context menu, so node operations stay close to
    // the list item and do not depend on the inspector selection.
    UI::UILayoutStyle hierarchyContextMenuLayout{};
    hierarchyContextMenuLayout.size.width = UI::UILayoutLength::Px(210.0F);
    auto hierarchyContextMenu = ui.tree.createElement(
        parent,
        UI::makeMenuElement(
            {.placement = UI::UIMenuPlacement::Auto,
             .anchorGap = ui.productTheme.spacing.space1},
            hierarchyContextMenuLayout));
    if (!hierarchyContextMenu) {
        return Tina::Core::failure(std::move(hierarchyContextMenu.error()));
    }
    hierarchyContextMenu_ = *hierarchyContextMenu;
    if (auto status = ui.tree.setMenuAnchor(hierarchyContextMenu_, hierarchyTree_);
        !status) {
        return status;
    }
    if (auto status = createMenuItem(
            hierarchyContextMenu_, "Rename", UI::UIMenuItemKind::Command,
            hierarchyContextRenameItem_);
        !status) {
        return status;
    }
    if (auto status = createMenuItem(
            hierarchyContextMenu_, "Move Up", UI::UIMenuItemKind::Command,
            hierarchyContextMoveUpItem_);
        !status) {
        return status;
    }
    if (auto status = createMenuItem(
            hierarchyContextMenu_, "Move Down", UI::UIMenuItemKind::Command,
            hierarchyContextMoveDownItem_);
        !status) {
        return status;
    }
    if (auto status = createMenuItem(
            hierarchyContextMenu_, "Move to Root", UI::UIMenuItemKind::Command,
            hierarchyContextMoveToRootItem_);
        !status) {
        return status;
    }
    if (auto status = appendSeparator(hierarchyContextMenu_); !status) {
        return status;
    }
    if (auto status = createMenuItem(
            hierarchyContextMenu_, "Delete", UI::UIMenuItemKind::Command,
            hierarchyContextDeleteItem_);
        !status) {
        return status;
    }

    // Project Assets uses the same retained Menu recipe as the hierarchy. The
    // selected item is carried separately as a stable AssetId, so filtering or
    // virtualization cannot retarget an action to a different row.
    UI::UILayoutStyle projectAssetContextMenuLayout{};
    projectAssetContextMenuLayout.size.width = UI::UILayoutLength::Px(280.0F);
    auto projectAssetContextMenu = ui.tree.createElement(
        parent,
        UI::makeMenuElement(
            {.placement = UI::UIMenuPlacement::Auto,
             .anchorGap = ui.productTheme.spacing.space1},
            projectAssetContextMenuLayout));
    if (!projectAssetContextMenu) {
        return Tina::Core::failure(std::move(projectAssetContextMenu.error()));
    }
    projectAssetContextMenu_ = *projectAssetContextMenu;
    if (auto status = ui.tree.setMenuAnchor(
            projectAssetContextMenu_, projectAssetList_); !status) {
        return status;
    }
    if (auto status = createMenuItem(
            projectAssetContextMenu_, "Select in Inspector", UI::UIMenuItemKind::Command,
            projectAssetContextOpenItem_); !status) {
        return status;
    }
    if (auto status = createMenuItem(
            projectAssetContextMenu_, "Rename Source File", UI::UIMenuItemKind::Command,
            projectAssetContextRenameItem_); !status) {
        return status;
    }
    if (auto status = createMenuItem(
            projectAssetContextMenu_, "New Folder", UI::UIMenuItemKind::Command,
            projectAssetContextNewFolderItem_); !status) {
        return status;
    }
    if (auto status = appendSeparator(projectAssetContextMenu_); !status) {
        return status;
    }
    if (auto status = createMenuItem(
            projectAssetContextMenu_, "Reimport", UI::UIMenuItemKind::Command,
            projectAssetContextReimportItem_); !status) {
        return status;
    }
    if (auto status = createMenuItem(
            projectAssetContextMenu_, "Locate Source",
            UI::UIMenuItemKind::Command, projectAssetContextLocateSourceItem_);
        !status) {
        return status;
    }
    if (auto status = createMenuItem(
            projectAssetContextMenu_, "Copy AssetId",
            UI::UIMenuItemKind::Command, projectAssetContextCopyAssetIdItem_);
        !status) {
        return status;
    }
    if (auto status = createMenuItem(
            projectAssetContextMenu_, "Copy Source Path",
            UI::UIMenuItemKind::Command, projectAssetContextCopySourcePathItem_);
        !status) {
        return status;
    }
    if (auto status = createMenuItem(
            projectAssetContextMenu_, "Reveal Dependencies",
            UI::UIMenuItemKind::Command,
            projectAssetContextRevealDependenciesItem_); !status) {
        return status;
    }
    if (auto status = appendSeparator(projectAssetContextMenu_); !status) {
        return status;
    }
    if (auto status = createMenuItem(
            projectAssetContextMenu_, "Remove from intended set",
            UI::UIMenuItemKind::Command, projectAssetContextRemoveItem_);
        !status) {
        return status;
    }

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildSceneAddModalUi(
    UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    constexpr std::array actions{
        UI::UIDialogActionConfig{
            .text = "Close",
            .variant = UI::UIButtonVariant::Text,
        },
        UI::UIDialogActionConfig{
            .text = "Create",
            .variant = UI::UIButtonVariant::Primary,
        },
    };
    UI::UILayoutStyle surfaceLayout = editorDialogSurfaceLayout(ui.productTheme);
    surfaceLayout.size.width = UI::UILayoutLength::Px(720.0F);
    auto dialog = ui.tree.buildDialog(
        parent,
        UI::UIDialogConfig{
            .title = "Add Node",
            .actions = actions,
            .style = UI::UIDialogStyle{
                .contentOverflow = UI::UIDialogContentOverflow::Scroll,
            },
            .layout = editorDialogOverlayLayout(),
            .surfaceLayout = surfaceLayout,
        });
    if (!dialog) {
        return Tina::Core::failure(std::move(dialog.error()));
    }
    sceneAddDialog_ = *dialog;
    sceneAddCancelButton_ = sceneAddDialog_.actions[0];
    sceneAddCreateButton_ = sceneAddDialog_.actions[1];
    if (auto status = storeNode(
            ui.createLabel(sceneAddDialog_.content, "Parent: World2D Scene",
                           fillWidth(20.0F), ui.secondaryText),
            sceneAddParentLabel_);
        !status) {
        return status;
    }
    auto search = EditorSearchField::Build(
        ui.tree, sceneAddDialog_.content, ui.productTheme, {}, "Search node type",
        fillWidth(ui.productTheme.controls.textEditHeight), true);
    if (!search) {
        return Tina::Core::failure(std::move(search.error()));
    }
    sceneAddSearchInput_ = search->textEdit;
    const float catalogHeight =
        ui.productTheme.controls.buttonHeight * 8.0F +
        ui.productTheme.spacing.space2 * 7.0F;
    UI::UILayoutStyle catalogLayout = fillWidth(catalogHeight);
    catalogLayout.flexItem.shrink = 0.0F;
    catalogLayout.containerLayout = UI::UIContainerLayout::Grid;
    catalogLayout.gridContainer.columns = UI::UIGridTrackList::Of(
        {UI::UIGridTrack::Fr(), UI::UIGridTrack::Fr()});
    catalogLayout.gridContainer.rows = UI::UIGridTrackList::Of({
        UI::UIGridTrack::Px(ui.productTheme.controls.buttonHeight),
        UI::UIGridTrack::Px(ui.productTheme.controls.buttonHeight),
        UI::UIGridTrack::Px(ui.productTheme.controls.buttonHeight),
        UI::UIGridTrack::Px(ui.productTheme.controls.buttonHeight),
        UI::UIGridTrack::Px(ui.productTheme.controls.buttonHeight),
        UI::UIGridTrack::Px(ui.productTheme.controls.buttonHeight),
        UI::UIGridTrack::Px(ui.productTheme.controls.buttonHeight),
        UI::UIGridTrack::Px(ui.productTheme.controls.buttonHeight),
    });
    catalogLayout.gridContainer.gap =
        UI::UILayoutGap::All(ui.productTheme.spacing.space2);
    auto catalog = ui.createPanel(sceneAddDialog_.content, catalogLayout);
    if (!catalog) {
        return Tina::Core::failure(std::move(catalog.error()));
    }
    // Rows past the active workspace registry are collapsed at refresh time.
    for (Tina::Core::usize slot = 0; slot < sceneAddTemplateButtons_.size();
         ++slot) {
        if (auto status = storeNode(
                ui.createSegmentedButton(
                    *catalog, "",
                    sceneAddTemplateRowLayout(UI::UIVisibility::Visible, slot)),
                sceneAddTemplateButtons_[slot]);
            !status) {
            return status;
        }
    }
    if (auto status = storeNode(
            ui.createLabel(sceneAddDialog_.content, "", fillWidth(30.0F),
                           ui.secondaryText),
            sceneAddDescription_);
        !status) {
        return status;
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::buildDirtyCloseModalUi(UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    constexpr std::array actions{
        UI::UIDialogActionConfig{
            .text = "Save",
            .variant = UI::UIButtonVariant::Primary,
        },
        UI::UIDialogActionConfig{
            .text = "Discard",
            .variant = UI::UIButtonVariant::Danger,
        },
        UI::UIDialogActionConfig{
            .text = "Cancel",
            .variant = UI::UIButtonVariant::Text,
        },
    };
    UI::UILayoutStyle surfaceLayout = editorDialogSurfaceLayout(ui.productTheme);
    surfaceLayout.size.width = UI::UILayoutLength::Px(520.0F);
    auto dialog = ui.tree.buildDialog(
        parent,
        UI::UIDialogConfig{
            .title = "Save changes before closing?",
            .actions = actions,
            .layout = editorDialogOverlayLayout(),
            .surfaceLayout = surfaceLayout,
        });
    if (!dialog) {
        return Tina::Core::failure(std::move(dialog.error()));
    }
    dirtyCloseDialog_ = *dialog;
    dirtyCloseTitle_ = dirtyCloseDialog_.title;
    if (auto status = storeNode(
            ui.createLabel(dirtyCloseDialog_.content,
                        "The current canonical document has unsaved changes.",
                        fillWidth(24.0F), ui.bodyText),
            dirtyCloseMessage_);
        !status) {
        return status;
    }
    UI::UILayoutStyle dirtyClosePathFieldLayout{};
    dirtyClosePathFieldLayout.size.width = UI::UILayoutLength::Percent(100.0F);
    auto dirtyClosePathField = ui.tree.buildFormField(
        dirtyCloseDialog_.content,
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
            .layout = editorDialogOverlayLayout(),
            .surfaceLayout = editorDialogSurfaceLayout(ui.productTheme),
        });
    if (!dialog) {
        return Tina::Core::failure(std::move(dialog.error()));
    }
    sceneDeleteDialog_ = *dialog;
    return Tina::Core::success();
}

auto EditorWorkspaceState::buildProjectAssetRemoveDialogUi(
    UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    constexpr std::array actions{
        UI::UIDialogActionConfig{
            .text = "Cancel",
            .variant = UI::UIButtonVariant::Text,
        },
        UI::UIDialogActionConfig{
            .text = "Remove",
            .variant = UI::UIButtonVariant::Danger,
        },
    };
    auto dialog = ui.tree.buildDialog(
        parent,
        UI::UIDialogConfig{
            .title = "Remove asset source?",
            .body = "The source import unit and its generated Catalog assets will be removed.\nThis action can be undone by importing the source again.",
            .actions = actions,
            .layout = editorDialogOverlayLayout(),
            .surfaceLayout = editorDialogSurfaceLayout(ui.productTheme),
        });
    if (!dialog) {
        return Tina::Core::failure(std::move(dialog.error()));
    }
    projectAssetRemoveDialog_ = *dialog;
    return Tina::Core::success();
}

auto EditorWorkspaceState::buildProjectAssetRenameDialogUi(
    UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    constexpr std::array actions{
        UI::UIDialogActionConfig{
            .text = "Cancel",
            .variant = UI::UIButtonVariant::Text,
        },
        UI::UIDialogActionConfig{
            .text = "Rename",
            .variant = UI::UIButtonVariant::Primary,
        },
    };
    auto dialog = ui.tree.buildDialog(
        parent,
        UI::UIDialogConfig{
            .title = "Rename Source File",
            .body = "Rename the imported file and rebuild its Project Asset records. AssetId remains stable when the importer supports it.",
            .actions = actions,
            .layout = editorDialogOverlayLayout(),
            .surfaceLayout = editorDialogSurfaceLayout(ui.productTheme),
        });
    if (!dialog) {
        return Tina::Core::failure(std::move(dialog.error()));
    }
    projectAssetRenameDialog_ = *dialog;
    auto field = ui.tree.buildFormField(
        projectAssetRenameDialog_.content,
        UI::UIFormFieldConfig{
            .label = "File name",
            .value = {},
            .layout = fillWidth(48.0F),
            .textEditLayout = fillWidth(ui.productTheme.controls.textEditHeight),
            .enabled = true,
        });
    if (!field) {
        return Tina::Core::failure(std::move(field.error()));
    }
    projectAssetRenameInput_ = field->textEdit;
    return Tina::Core::success();
}

auto EditorWorkspaceState::buildProjectAssetFolderDialogUi(
    UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    constexpr std::array actions{
        UI::UIDialogActionConfig{
            .text = "Cancel",
            .variant = UI::UIButtonVariant::Text,
        },
        UI::UIDialogActionConfig{
            .text = "Create Folder",
            .variant = UI::UIButtonVariant::Primary,
        },
    };
    auto dialog = ui.tree.buildDialog(
        parent,
        UI::UIDialogConfig{
            .title = "New Project Folder",
            .body = "Create a folder inside the project Source directory.",
            .actions = actions,
            .layout = editorDialogOverlayLayout(),
            .surfaceLayout = editorDialogSurfaceLayout(ui.productTheme),
        });
    if (!dialog) {
        return Tina::Core::failure(std::move(dialog.error()));
    }
    projectAssetFolderDialog_ = *dialog;
    auto field = ui.tree.buildFormField(
        projectAssetFolderDialog_.content,
        UI::UIFormFieldConfig{
            .label = "Folder name",
            .value = {},
            .layout = fillWidth(48.0F),
            .textEditLayout = fillWidth(ui.productTheme.controls.textEditHeight),
            .enabled = true,
        });
    if (!field) {
        return Tina::Core::failure(std::move(field.error()));
    }
    projectAssetFolderInput_ = field->textEdit;
    return Tina::Core::success();
}

auto EditorWorkspaceState::buildAboutDialogUi(
    UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    constexpr std::array actions{
        UI::UIDialogActionConfig{
            .text = "Close",
            .variant = UI::UIButtonVariant::Primary,
        },
    };
    auto dialog = ui.tree.buildDialog(
        parent,
        UI::UIDialogConfig{
            .title = "About Tina Editor",
            .body = "Tina Editor\nC++23 game runtime and authoring environment\n2D and 3D scene editing",
            .actions = actions,
            .layout = editorDialogOverlayLayout(),
            .surfaceLayout = editorDialogSurfaceLayout(ui.productTheme),
        });
    if (!dialog) {
        return Tina::Core::failure(std::move(dialog.error()));
    }
    aboutDialog_ = *dialog;
    return Tina::Core::success();
}

auto EditorWorkspaceState::buildFileDropFeedbackUi(
    UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    fileDropFeedbackRootLayout_ = percentSize(100.0F, 100.0F);
    fileDropFeedbackRootLayout_.placement = UI::UILayoutPlacement::Overlay;
    fileDropFeedbackRootLayout_.overlay.horizontal = UI::UIAxisAlignment::Stretch;
    fileDropFeedbackRootLayout_.overlay.vertical = UI::UIAxisAlignment::Stretch;
    fileDropFeedbackRootLayout_.visibility = UI::UIVisibility::Collapsed;
    UI::UIElementDescriptor rootDescriptor =
        UI::makePanelElement(fileDropFeedbackRootLayout_);
    rootDescriptor.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    if (auto status = storeNode(
            ui.tree.createElement(parent, rootDescriptor),
            fileDropFeedbackRoot_);
        !status) {
        return status;
    }

    fileDropFeedbackSurfaceLayout_ = fixedSize(520.0F, 86.0F);
    fileDropFeedbackSurfaceLayout_.minMax.maxWidth =
        UI::UILayoutLength::Percent(100.0F);
    fileDropFeedbackSurfaceLayout_.placement = UI::UILayoutPlacement::Overlay;
    fileDropFeedbackSurfaceLayout_.overlay.horizontal = UI::UIAxisAlignment::Center;
    fileDropFeedbackSurfaceLayout_.overlay.vertical = UI::UIAxisAlignment::Start;
    fileDropFeedbackSurfaceLayout_.overlay.offset.y =
        UI::UILayoutLength::Px(ui.productTheme.controls.commandBarHeight +
                               ui.productTheme.spacing.space3);
    fileDropFeedbackSurfaceLayout_.padding =
        UI::UIEdgeSpacing::All(ui.productTheme.spacing.space3);
    fileDropFeedbackSurfaceLayout_.flexContainer.direction =
        UI::UIFlexDirection::Column;
    fileDropFeedbackSurfaceLayout_.flexContainer.gap.row =
        ui.productTheme.spacing.space1;
    UI::UIElementDescriptor surfaceDescriptor = UI::makeSurfaceElement(
        {.variant = UI::UISurfaceVariant::Elevated},
        fileDropFeedbackSurfaceLayout_);
    surfaceDescriptor.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    surfaceDescriptor.semantics.mode = UI::UISemanticsMode::Publish;
    surfaceDescriptor.semantics.role = UI::UISemanticsRole::Group;
    surfaceDescriptor.semantics.name = "File drop status";
    surfaceDescriptor.semantics.liveSetting = UI::UISemanticsLiveSetting::Polite;
    if (auto status = storeNode(
            ui.tree.createElement(fileDropFeedbackRoot_, surfaceDescriptor),
            fileDropFeedbackSurface_);
        !status) {
        return status;
    }

    UI::UIElementDescriptor stateDescriptor = UI::makeLabelElement(
        "Drop accepted", fillWidth(20.0F));
    stateDescriptor.textStyle = ui.accentText;
    stateDescriptor.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    if (auto status = storeNode(
            ui.tree.createElement(fileDropFeedbackSurface_, stateDescriptor),
            fileDropFeedbackStateText_);
        !status) {
        return status;
    }

    UI::UIElementDescriptor messageDescriptor = UI::makeLabelElement(
        "Files are queued for import", fillWidth(22.0F));
    messageDescriptor.textStyle = ui.secondaryText;
    messageDescriptor.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    if (auto status = storeNode(
            ui.tree.createElement(fileDropFeedbackSurface_, messageDescriptor),
            fileDropFeedbackMessage_);
        !status) {
        return status;
    }
    if (auto status = setSingleLineEllipsis(ui.tree, fileDropFeedbackMessage_);
        !status) {
        return status;
    }

    fileDropFeedbackProgressLayout_ = fillWidth(6.0F);
    fileDropFeedbackProgressLayout_.flexItem.shrink = 0.0F;
    UI::UIElementDescriptor progressDescriptor =
        UI::makeProgressBarElement(fileDropFeedbackProgressLayout_);
    progressDescriptor.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
    progressDescriptor.semantics.mode = UI::UISemanticsMode::Exclude;
    if (auto status = storeNode(
            ui.tree.createElement(fileDropFeedbackSurface_, progressDescriptor),
            fileDropFeedbackProgress_);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setProgressBarRange(
            fileDropFeedbackProgress_, 0.0F, 1.0F);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setProgressBarValue(
            fileDropFeedbackProgress_, 0.0F);
        !status) {
        return status;
    }

    fileDropFeedbackToneColors_ = {
        ui.productTheme.colors.surfaceContainerHigh,
        ui.productTheme.colors.primaryContainer,
        ui.productTheme.colors.primaryContainer,
        ui.productTheme.colors.successContainer,
        ui.productTheme.colors.errorContainer,
        ui.productTheme.colors.errorContainer,
    };
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
    if (auto status = setSingleLineEllipsis(ui.tree, parts->message);
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

auto EditorWorkspaceState::registerUiCallbacks(
    UiBuildContext& ui, UI::UINodeId rootNode) -> Tina::Core::Status
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
    if (auto status = ui.tree.setButtonAction(
            resetTransformButton_,
            UI::UIButtonActionCallback{
                [this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::ResetTransform);
                }});
        !status) {
        return status;
    }
    {
        constexpr std::array sectionToggleCommands{
            EditorCommand::NodeToggleSpriteVisible,
            EditorCommand::NodeToggleCameraActive,
            EditorCommand::NodeTogglePointLightActive,
            EditorCommand::NodeToggleShadowOccluderActive,
            EditorCommand::NodeToggleSpriteAnimationAutoPlay,
            EditorCommand::NodeToggleMeshVisible,
        };
        const std::array<EditorCommand, 5> sectionApplyCommands{
            EditorCommand::NodeApplySprite, EditorCommand::NodeApplyCamera,
            EditorCommand::NodeApplyPointLight,
            EditorCommand::NodeApplyShadowOccluder,
            EditorCommand::NodeApplyAnimationProperties};
        for (Tina::Core::usize sectionIndex = 0;
             sectionIndex < nodePropertySections_.size(); ++sectionIndex) {
            const auto& section = nodePropertySections_[sectionIndex];
            if (auto status = ui.tree.setCheckboxAction(
                    section.collapsible.header,
                    UI::UIButtonActionCallback{
                        [this, sectionIndex](const UI::UIButtonActionEvent&) noexcept {
                            nodePropertySections_[sectionIndex]
                                .collapseUpdatePending = true;
                        }});
                !status) {
                return status;
            }
            if (auto status = ui.tree.setCheckboxAction(
                    section.activeSwitch,
                    UI::UIButtonActionCallback{[this, command = sectionToggleCommands[sectionIndex]](
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
                nodePropertySections_[0].resourceAssignButton,
                UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::NodeAssignSprite);
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
    for (u32 index = 0; index < mainMenuAnchors_.size(); ++index) {
        if (auto status = ui.tree.setButtonAction(
                mainMenuAnchors_[index],
                UI::UIButtonActionCallback{
                    [this, index](const UI::UIButtonActionEvent&) noexcept {
                        pendingMainMenuToggle_ = index;
                    }});
            !status) {
            return status;
        }
    }
    const std::array workspaceCommandBindings{
        std::pair{workspaceModeButtons_[0], EditorCommand::SwitchToWorld2D},
        std::pair{workspaceModeButtons_[1], EditorCommand::SwitchToWorld3D},
        std::pair{viewWorkspaceMenuItems_[0], EditorCommand::SwitchToWorld2D},
        std::pair{viewWorkspaceMenuItems_[1], EditorCommand::SwitchToWorld3D},
    };
    for (const auto& [button, command] : workspaceCommandBindings) {
        if (auto status = bindEditorCommand(button, command); !status) {
            return status;
        }
    }
    const std::array workspacePanelBindings{
        std::pair{leftDockCollapseButton_, WorkspacePanelKind::LeftDock},
        std::pair{viewPanelMenuItems_[0], WorkspacePanelKind::LeftDock},
        std::pair{inspectorCollapseButton_, WorkspacePanelKind::Inspector},
        std::pair{viewPanelMenuItems_[1], WorkspacePanelKind::Inspector},
    };
    for (const auto& [button, panel] : workspacePanelBindings) {
        if (auto status = ui.tree.setButtonAction(
                button,
                UI::UIButtonActionCallback{
                    [this, panel](const UI::UIButtonActionEvent&) noexcept {
                        pendingWorkspacePanelToggle_ = panel;
                    }});
            !status) {
            return status;
        }
    }
    if (auto status = ui.tree.setButtonAction(
            viewLayoutDebuggerMenuItem_,
            UI::UIButtonActionCallback{
                [this](const UI::UIButtonActionEvent&) noexcept {
                    pendingLayoutDebuggerToggle_ = true;
                }});
        !status) {
        return status;
    }
    if (auto status = bindEditorCommand(
            viewportContextButtons_[0], EditorCommand::SwitchToWorld2D);
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            viewportContextButtons_[1],
            UI::UIButtonActionCallback{
                [this](const UI::UIButtonActionEvent&) noexcept {
                    const auto tileMapIndex =
                        documentTabs_.find(tileMapDocumentOwnerKey_);
                    if (tileMapIndex.has_value()) {
                        pendingDocumentTabActivation_ =
                            static_cast<u32>(*tileMapIndex);
                    }
                }});
        !status) {
        return status;
    }
    const std::array menuCommandBindings{
        std::pair{fileCreateProjectMenuItem_, EditorCommand::CreateProject},
        std::pair{fileOpenProjectMenuItem_, EditorCommand::OpenProject},
        std::pair{fileImportSourceMenuItem_, EditorCommand::ImportSource},
        std::pair{fileSaveMenuItem_, EditorCommand::Save},
        std::pair{fileSaveAsMenuItem_, EditorCommand::SaveAs},
        std::pair{fileCloseDocumentMenuItem_, EditorCommand::CloseActiveDocument},
        std::pair{editUndoMenuItem_, EditorCommand::Undo},
        std::pair{editRedoMenuItem_, EditorCommand::Redo},
        std::pair{editDuplicateMenuItem_, EditorCommand::SceneDuplicate},
        std::pair{editDeleteMenuItem_, EditorCommand::SceneDelete},
        std::pair{viewFrameAllMenuItem_, EditorCommand::ViewportResetView},
        std::pair{viewFocusSelectionMenuItem_, EditorCommand::SceneFocus},
        std::pair{helpAboutMenuItem_, EditorCommand::ShowAbout},
    };
    for (const auto& [item, command] : menuCommandBindings) {
        if (auto status = bindEditorCommand(item, command); !status) {
            return status;
        }
    }
    const std::array hierarchyContextCommandBindings{
        std::pair{hierarchyContextRenameItem_, EditorCommand::SceneRenameContext},
        std::pair{hierarchyContextMoveUpItem_, EditorCommand::SceneMoveUpContext},
        std::pair{hierarchyContextMoveDownItem_, EditorCommand::SceneMoveDownContext},
        std::pair{hierarchyContextMoveToRootItem_, EditorCommand::SceneMoveToRootContext},
        std::pair{hierarchyContextDeleteItem_, EditorCommand::SceneDeleteContext},
    };
    for (const auto& [item, command] : hierarchyContextCommandBindings) {
        if (auto status = bindEditorCommand(item, command); !status) {
            return status;
        }
    }
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
    constexpr std::array BottomPanels{
        BottomPanelKind::Animation,
        BottomPanelKind::Output,
    };
    for (u32 index = 0; index < bottomPanelButtons_.size(); ++index) {
        if (auto status = ui.tree.setButtonAction(
                bottomPanelButtons_[index],
                UI::UIButtonActionCallback{
                    [this, panel = BottomPanels[index]](
                        const UI::UIButtonActionEvent&) noexcept {
                        pendingBottomPanelToggle_ = panel;
                        animationHoveredFrameSlot_.reset();
                        pendingAnimationTimelineRefresh_ = true;
                    }});
            !status) {
            return status;
        }
    }
    const std::array bottomPanelCollapseBindings{
        std::pair{animationCollapseButton_, BottomPanelKind::Animation},
        std::pair{outputCollapseButton_, BottomPanelKind::Output},
    };
    for (const auto& [button, panel] : bottomPanelCollapseBindings) {
        if (auto status = ui.tree.setButtonAction(
                button,
                UI::UIButtonActionCallback{
                    [this, panel](const UI::UIButtonActionEvent&) noexcept {
                        pendingBottomPanelToggle_ = panel;
                        animationHoveredFrameSlot_.reset();
                        pendingAnimationTimelineRefresh_ = true;
                    }});
            !status) {
            return status;
        }
    }
    if (auto status = ui.tree.setButtonAction(
            layoutDebugStatusButton_,
            UI::UIButtonActionCallback{
                [this](const UI::UIButtonActionEvent&) noexcept {
                    pendingLayoutDebuggerToggle_ = true;
                }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            layoutDebugCollapseButton_,
            UI::UIButtonActionCallback{
                [this](const UI::UIButtonActionEvent&) noexcept {
                    pendingLayoutDebuggerToggle_ = true;
                }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            layoutDebugShowAllButton_,
            UI::UIButtonActionCallback{
                [this](const UI::UIButtonActionEvent&) noexcept {
                    layoutDebugShowAllVisibleBounds_ =
                        !layoutDebugShowAllVisibleBounds_;
                    layoutDebugDetailsRefreshPending_ = true;
                }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            layoutDebugPickButton_,
            UI::UIButtonActionCallback{
                [this](const UI::UIButtonActionEvent&) noexcept {
                    layoutDebugPickArmed_ = !layoutDebugPickArmed_;
                    layoutDebugDetailsRefreshPending_ = true;
                }});
        !status) {
        return status;
    }
    auto layoutDebugPickPointerListener = ui.tree.addRoutedPointerListener(
        {
            .node = rootNode,
            .kind = UI::UIRoutedPointerEventKind::ButtonDown,
            .phases = UI::UIEventPhaseMask::Capture,
        },
        UI::UIRoutedPointerCallback{
            [this](UI::UIRoutedPointerEvent& event) noexcept {
                handleLayoutDebugPickPointerDown(event);
            }});
    if (!layoutDebugPickPointerListener) {
        return Tina::Core::failure(
            std::move(layoutDebugPickPointerListener.error()));
    }
    layoutDebugPickPointerListener_ =
        std::move(*layoutDebugPickPointerListener);
    constexpr std::array layoutDebugWindowPointerKinds{
        UI::UIRoutedPointerEventKind::ButtonDown,
        UI::UIRoutedPointerEventKind::Move,
        UI::UIRoutedPointerEventKind::ButtonUp,
        UI::UIRoutedPointerEventKind::PointerCancel,
    };
    for (u32 index = 0U; index < layoutDebugWindowPointerKinds.size(); ++index) {
        auto listener = ui.tree.addRoutedPointerListener(
            {
                .node = layoutDebugPanel_,
                .kind = layoutDebugWindowPointerKinds[index],
                .phases = UI::UIEventPhaseMask::Capture,
            },
            UI::UIRoutedPointerCallback{
                [this, index](UI::UIRoutedPointerEvent& event) noexcept {
                    if (index == 0U) {
                        handleLayoutDebugWindowPointerDown(event);
                    } else if (index == 1U) {
                        handleLayoutDebugWindowPointerMove(event);
                    } else if (index == 2U) {
                        handleLayoutDebugWindowPointerUp(event);
                    } else {
                        handleLayoutDebugWindowPointerCancel(event);
                    }
                }});
        if (!listener) {
            return Tina::Core::failure(std::move(listener.error()));
        }
        layoutDebugWindowPointerListeners_[index] = std::move(*listener);
    }
    constexpr std::array outputFilters{
        OutputFilter::All,
        OutputFilter::Info,
        OutputFilter::Warning,
        OutputFilter::Error,
    };
    for (u32 index = 0; index < outputFilterButtons_.size(); ++index) {
        if (auto status = ui.tree.setButtonAction(
                outputFilterButtons_[index],
                UI::UIButtonActionCallback{
                    [this, filter = outputFilters[index]](
                        const UI::UIButtonActionEvent&) noexcept {
                        outputFilter_ = filter;
                        outputGridRefreshPending_ = true;
                    }});
            !status) {
            return status;
        }
    }
    auto outputPointerListener = ui.tree.addRoutedPointerListener(
        {
            .node = outputGrid_,
            .kind = UI::UIRoutedPointerEventKind::ButtonDown,
            .phases = UI::UIEventPhaseMask::Capture,
        },
        UI::UIRoutedPointerCallback{
            [this](UI::UIRoutedPointerEvent& event) noexcept {
                handleOutputPointerDown(event);
            }});
    if (!outputPointerListener) {
        return Tina::Core::failure(std::move(outputPointerListener.error()));
    }
    outputPointerListener_ = std::move(*outputPointerListener);
    if (auto status = ui.tree.setButtonAction(
            outputDetailsToggleButton_,
            UI::UIButtonActionCallback{
                [this](const UI::UIButtonActionEvent&) noexcept {
                    outputDetailsExpanded_ = !outputDetailsExpanded_;
                }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            outputLocateButton_,
            UI::UIButtonActionCallback{
                [this](const UI::UIButtonActionEvent&) noexcept {
                    if (outputSelectedSequence_.has_value()) {
                        pendingOutputLocateSequence_ = outputSelectedSequence_;
                    }
                }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
        outputClearButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                authoringFeedback_.clear();
                outputHistoryCount_ = 0U;
                outputVisibleHistoryCount_ = 0U;
                outputSelectedSequence_.reset();
                pendingOutputLocateSequence_.reset();
                outputHistoryObservedFeedback_.clear();
                for (EditorOutputHistoryEntry& entry : outputHistory_) {
                    entry = EditorOutputHistoryEntry{};
                }
                outputGridRefreshPending_ = true;
                outputDetailsExpanded_ = false;
            }});
        !status) {
        return status;
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
    auto animationTimelinePointerListener = ui.tree.addRoutedPointerListener(
        {
            .node = rootNode,
            .kind = UI::UIRoutedPointerEventKind::Move,
            .phases = UI::UIEventPhaseMask::Capture,
        },
        UI::UIRoutedPointerCallback{
            [this](UI::UIRoutedPointerEvent& event) noexcept {
                handleAnimationTimelinePointerMove(event);
            }});
    if (!animationTimelinePointerListener) {
        return Tina::Core::failure(
            std::move(animationTimelinePointerListener.error()));
    }
    animationTimelinePointerListener_ =
        std::move(*animationTimelinePointerListener);
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
    if (auto status = ui.tree.setButtonAction(
            frameAllButton_,
            UI::UIButtonActionCallback{
                [this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::ViewportResetView);
                }});
        !status) {
        return status;
    }
    constexpr std::array OrientationCommands{
        EditorCommand::ViewportPresetRight,
        EditorCommand::ViewportPresetTop,
        EditorCommand::ViewportPresetFront,
    };
    for (Tina::Core::usize axis = 0; axis < ViewportOrientationAxisCount;
         ++axis) {
        if (auto status = ui.tree.setButtonAction(
                viewportOrientationAxisEndpoints_[axis],
                UI::UIButtonActionCallback{
                    [this, command = OrientationCommands[axis]](
                        const UI::UIButtonActionEvent&) noexcept {
                        queueEditorCommand(command);
                    }});
            !status) {
            return status;
        }
    }
    constexpr std::array OrientationBarrierKinds{
        UI::UIRoutedPointerEventKind::Move,
        UI::UIRoutedPointerEventKind::ButtonDown,
        UI::UIRoutedPointerEventKind::ButtonUp,
        UI::UIRoutedPointerEventKind::Wheel,
    };
    for (Tina::Core::usize index = 0; index < OrientationBarrierKinds.size();
         ++index) {
        auto listener = ui.tree.addRoutedPointerListener(
            {
                .node = viewportOrientationCompass_,
                .kind = OrientationBarrierKinds[index],
                .phases = UI::UIEventPhaseMask::Capture |
                          UI::UIEventPhaseMask::Target,
            },
            UI::UIRoutedPointerCallback{
                [](UI::UIRoutedPointerEvent& event) noexcept {
                    event.consumeInputTransition();
                    event.stopPropagation();
                }});
        if (!listener) {
            return Tina::Core::failure(std::move(listener.error()));
        }
        viewportOrientationCompassPointerBarrierListeners_[index] =
            std::move(*listener);
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
    const std::array projectTypeFilterCommands{
        EditorCommand::ProjectTypeFilterAll,
        EditorCommand::ProjectTypeFilterImages,
        EditorCommand::ProjectTypeFilterModels,
        EditorCommand::ProjectTypeFilterScenes,
        EditorCommand::ProjectTypeFilterAudio,
        EditorCommand::ProjectTypeFilterAnimation,
        EditorCommand::ProjectTypeFilterOther,
    };
    for (u32 index = 0; index < projectAssetTypeDropdownItems_.size(); ++index) {
        if (auto status = ui.tree.setButtonAction(
                projectAssetTypeDropdownItems_[index],
                UI::UIButtonActionCallback{
                    [this, command = projectTypeFilterCommands[index]](
                        const UI::UIButtonActionEvent&) noexcept {
                        queueEditorCommand(command);
                    }});
            !status) {
            return status;
        }
    }
    constexpr std::array projectAssetViewModes{
        ProjectAssetViewMode::Grid,
        ProjectAssetViewMode::List,
    };
    for (u32 index = 0; index < projectAssetViewButtons_.size(); ++index) {
        if (auto status = ui.tree.setButtonAction(
                projectAssetViewButtons_[index],
                UI::UIButtonActionCallback{
                    [this, mode = projectAssetViewModes[index]](
                        const UI::UIButtonActionEvent&) noexcept {
                        pendingProjectAssetViewMode_ = mode;
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
    const std::array projectAssetContextBindings{
        std::pair{projectAssetContextOpenItem_, EditorCommand::OpenSelectedProjectAsset},
        std::pair{projectAssetContextRenameItem_, EditorCommand::RenameSelectedProjectAsset},
        std::pair{projectAssetContextNewFolderItem_, EditorCommand::NewProjectAssetFolder},
        std::pair{projectAssetContextReimportItem_, EditorCommand::ReimportSelectedProjectAsset},
        std::pair{projectAssetContextLocateSourceItem_, EditorCommand::LocateProjectAssetSource},
        std::pair{projectAssetContextCopyAssetIdItem_, EditorCommand::CopyProjectAssetId},
        std::pair{projectAssetContextCopySourcePathItem_, EditorCommand::CopyProjectAssetSourcePath},
        std::pair{projectAssetContextRevealDependenciesItem_, EditorCommand::RevealProjectAssetDependencies},
        std::pair{projectAssetContextRemoveItem_, EditorCommand::RemoveSelectedProjectAsset},
    };
    for (const auto& [item, command] : projectAssetContextBindings) {
        if (auto status = ui.tree.setButtonAction(
                item,
                UI::UIButtonActionCallback{
                    [this, command](const UI::UIButtonActionEvent&) noexcept {
                        if (command == EditorCommand::OpenSelectedProjectAsset) {
                            pendingProjectAssetOpen_ = projectAssetContextAssetId_;
                        }
                        queueEditorCommand(command);
                    }});
            !status) {
            return status;
        }
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
            importSourceButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::ImportSource);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            projectAssetEmptyStateImportButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::ImportSource);
            }});
        !status) {
        return status;
    }
    const std::array startCenterCommandBindings{
        std::pair{projectAssetStartCenterNewButton_, EditorCommand::CreateProject},
        std::pair{projectAssetStartCenterOpenButton_, EditorCommand::OpenProject},
        std::pair{projectAssetStartCenterImportButton_, EditorCommand::ImportSource},
    };
    for (const auto& [button, command] : startCenterCommandBindings) {
        if (auto status = bindEditorCommand(button, command); !status) {
            return status;
        }
    }
    for (u32 index = 0; index < RecentProjectCapacity; ++index) {
        if (auto status = ui.tree.setButtonAction(
                recentProjectButtons_[index],
                UI::UIButtonActionCallback{[this, index](const UI::UIButtonActionEvent&) noexcept {
                    if (index < editorSettings_.recentProjectCount) {
                        pendingRecentProjectIndex_ = index;
                    }
                }}); !status) {
            return status;
        }
    }
    for (u32 index = 0; index < RecentProjectCapacity; ++index) {
        if (auto status = ui.tree.setButtonAction(
                recentProjectMenuItems_[index],
                UI::UIButtonActionCallback{[this, index](const UI::UIButtonActionEvent&) noexcept {
                    if (index < editorSettings_.recentProjectCount) {
                        pendingRecentProjectIndex_ = index;
                    }
                }}); !status) {
            return status;
        }
    }
    if (auto status = ui.tree.setButtonAction(
            retrySourceImportButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                retrySourceImportPending_ = true;
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            cancelSourceImportButton_,
            UI::UIButtonActionCallback{
                [this](const UI::UIButtonActionEvent&) noexcept {
                    cancelSourceImportPending_ = true;
                }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            openImportOutputButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                pendingBottomPanelOpen_ = BottomPanelKind::Output;
                animationHoveredFrameSlot_.reset();
                pendingAnimationTimelineRefresh_ = true;
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            sourceImportExpandButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                sourceImportExpanded_ = true;
                sourceImportVisibilityRefreshPending_ = true;
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            sourceImportCollapseButton_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                sourceImportExpanded_ = false;
                sourceImportVisibilityRefreshPending_ = true;
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
        std::pair{dirtyCloseDialog_.actions[DirtyCloseSaveActionIndex],
                  EditorCommand::DirtyCloseSave},
        std::pair{dirtyCloseDialog_.actions[DirtyCloseDiscardActionIndex],
                  EditorCommand::DirtyCloseDiscard},
        std::pair{dirtyCloseDialog_.actions[DirtyCloseCancelActionIndex],
                  EditorCommand::DirtyCloseCancel},
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
    for (Tina::Core::usize slot = 0; slot < sceneAddTemplateButtons_.size();
         ++slot) {
        if (auto status = ui.tree.setButtonAction(
                sceneAddTemplateButtons_[slot],
                UI::UIButtonActionCallback{
                    [this, slot](const UI::UIButtonActionEvent&) noexcept {
                        // Deferred like pendingMainMenuToggle_ so the callback
                        // stays allocation-free and side-effect free.
                        pendingSceneAddTemplateIndex_ = static_cast<u32>(slot);
                    }});
            !status) {
            return status;
        }
    }
    constexpr std::array hierarchyPointerKinds{
        UI::UIRoutedPointerEventKind::ButtonDown,
        UI::UIRoutedPointerEventKind::Move,
        UI::UIRoutedPointerEventKind::ButtonUp,
        UI::UIRoutedPointerEventKind::PointerCancel,
    };
    for (Tina::Core::usize index = 0; index < hierarchyPointerKinds.size(); ++index) {
        auto listener = ui.tree.addRoutedPointerListener(
            {
                .node = hierarchyTree_,
                .kind = hierarchyPointerKinds[index],
                // Capture keeps hierarchy interaction ahead of TreeView's
                // item button default action and preserves pointer capture for
                // a drag that leaves the original row.
                .phases = UI::UIEventPhaseMask::Capture,
            },
            UI::UIRoutedPointerCallback{
                [this, index](UI::UIRoutedPointerEvent& event) noexcept {
                    if (index == 0U) {
                        handleHierarchyPointerDown(event);
                    } else if (index == 1U) {
                        handleHierarchyPointerMove(event);
                    } else if (index == 2U) {
                        handleHierarchyPointerUp(event);
                    } else {
                        handleHierarchyPointerCancel(event);
                    }
                }});
        if (!listener) {
            return Tina::Core::failure(std::move(listener.error()));
        }
        hierarchyPointerListeners_[index] = std::move(*listener);
    }
    constexpr std::array projectAssetPointerKinds{
        UI::UIRoutedPointerEventKind::ButtonDown,
        UI::UIRoutedPointerEventKind::Move,
        UI::UIRoutedPointerEventKind::ButtonUp,
        UI::UIRoutedPointerEventKind::PointerCancel,
    };
    for (Tina::Core::usize index = 0;
         index < projectAssetPointerKinds.size(); ++index) {
        auto listener = ui.tree.addRoutedPointerListener(
            {
                .node = projectAssetList_,
                .kind = projectAssetPointerKinds[index],
                .phases = UI::UIEventPhaseMask::Capture,
            },
            UI::UIRoutedPointerCallback{
                [this, index](UI::UIRoutedPointerEvent& event) noexcept {
                    if (index == 0U) {
                        handleProjectAssetPointerDown(event);
                    } else if (index == 1U) {
                        handleProjectAssetPointerMove(event);
                    } else if (index == 2U) {
                        handleProjectAssetPointerUp(event);
                    } else {
                        handleProjectAssetPointerCancel(event);
                    }
                }});
        if (!listener) {
            return Tina::Core::failure(std::move(listener.error()));
        }
        projectAssetPointerListeners_[index] = std::move(*listener);
    }
    if (auto status = ui.tree.setButtonAction(
            sceneAddCreateButton_,
            UI::UIButtonActionCallback{
                [this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::SceneAddConfirm);
                }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            sceneAddCancelButton_,
            UI::UIButtonActionCallback{
                [this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::SceneAddCancel);
                }});
        !status) {
        return status;
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
            projectAssetRemoveDialog_.actions[SceneDeleteConfirmActionIndex],
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::ProjectAssetRemoveConfirm);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            projectAssetRemoveDialog_.actions[SceneDeleteCancelActionIndex],
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::ProjectAssetRemoveCancel);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            projectAssetRenameDialog_.actions[ProjectAssetDialogConfirmActionIndex],
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::ProjectAssetRenameConfirm);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            projectAssetRenameDialog_.actions[ProjectAssetDialogCancelActionIndex],
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::ProjectAssetRenameCancel);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            projectAssetFolderDialog_.actions[ProjectAssetDialogConfirmActionIndex],
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::ProjectAssetFolderConfirm);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            projectAssetFolderDialog_.actions[ProjectAssetDialogCancelActionIndex],
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::ProjectAssetFolderCancel);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            aboutDialog_.actions[AboutCloseActionIndex],
            UI::UIButtonActionCallback{
                [this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::HideAbout);
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
    editorSettings_ = loadEditorSettings();
    leftDockVisibleFraction_ = editorSettings_.leftDockFraction;
    inspectorVisibleFraction_ = editorSettings_.inspectorFraction;
    bottomPanelVisibleFraction_ = editorSettings_.bottomPanelFraction;
    leftDockVisible_ = editorSettings_.leftDockVisible;
    inspectorVisible_ = editorSettings_.inspectorVisible;
    bottomPanel_ = BottomPanelKind::None;
    layoutDebuggerVisible_ = false;
    layoutDebugProfileDragInitialized_ = false;
    layoutDebugProfileMutationPendingCommit_ = false;
    layoutDebugProfileCommittedStatisticsPending_ = false;
    layoutDebugProfileMutationIndex_ = 0;
    pendingBottomPanelOpen_.reset();
    pendingLayoutDebuggerOpen_ = editorSettings_.layoutDebuggerVisible;
    if (options_.profileUiLayoutDrag) {
        // The deterministic profile workload must not depend on persisted user
        // settings. It opens the debugger and drives a bounded panel trajectory
        // from the owner thread after the first UI tree is built.
        pendingLayoutDebuggerOpen_ = true;
        layoutDebugShowAllVisibleBounds_ = true;
    }
    if (editorSettings_.bottomPanel != BottomPanelKind::None) {
        pendingBottomPanelOpen_ = editorSettings_.bottomPanel;
    }
    auto initialSnap = viewportTransformGizmo_.snap();
    initialSnap.enabled = editorSettings_.snapEnabled;
    (void)viewportTransformGizmo_.setSnap(initialSnap);
    auto fileDropSubscription = context.platformEventSubscriptions().subscribe(
        [this](const Tina::PlatformEventNotification& notification) {
            const auto* fileDrop = std::get_if<Tina::Platform::FileDropEvent>(
                &notification.event().payload);
            if (fileDrop == nullptr || fileDrop->paths.empty()) {
                return;
            }
            constexpr Tina::Core::usize MaximumQueuedFileDrops = 32U;
            if (pendingFileDrops_.size() >= MaximumQueuedFileDrops ||
                fileDrop->paths.size() >
                    Tina::EditorApp::Detail::EditorSourceImportUnitCapacity) {
                fileDropQueueOverflowed_ = true;
                setFileDropFeedback(
                    FileDropFeedbackState::Rejected,
                    "The drop batch exceeds the Editor queue capacity");
                return;
            }
            try {
                PendingFileDrop request{
                    .window = fileDrop->window,
                    .logicalX = fileDrop->logicalX,
                    .logicalY = fileDrop->logicalY,
                };
                request.pathsUtf8.reserve(fileDrop->paths.size());
                for (const std::string_view path : fileDrop->paths) {
                    request.pathsUtf8.emplace_back(path);
                }
                pendingFileDrops_.push_back(std::move(request));
                setFileDropFeedback(
                    FileDropFeedbackState::Accepted,
                    "Files accepted and queued for target validation");
            } catch (const std::bad_alloc&) {
                fileDropQueueOverflowed_ = true;
                setFileDropFeedback(
                    FileDropFeedbackState::Rejected,
                    "The Editor could not retain the dropped file paths");
            }
        });
    if (!fileDropSubscription) {
        return Tina::Core::failure(std::move(fileDropSubscription.error()));
    }
    platformEventSubscription_.emplace(std::move(*fileDropSubscription));
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
        (void)releasePreviewAssetBindings();
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
    imageResolver_.setIconResolver(iconResources_.resolver());
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
    auto imageResolverRegistration = rootBuilder->bindImageResolver(
        *root, imageResolver_.resolver());
    if (!imageResolverRegistration) {
        return Tina::Core::failure(
            std::move(imageResolverRegistration.error()));
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
    if (auto status = buildMenuOverlaysUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildSnackbarUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildFileDropFeedbackUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildSceneAddModalUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildDirtyCloseModalUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildSceneDeleteDialogUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildProjectAssetRemoveDialogUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildProjectAssetRenameDialogUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildProjectAssetFolderDialogUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildAboutDialogUi(ui, rootNode); !status) {
        return status;
    }
    // Built last on purpose. Paint order is layer first, then tree preorder
    // within a layer, and there is no per-node z-index. This panel lives in the
    // Content layer, so being the final root-level sibling is what keeps other
    // chrome from painting over it. Modal, Popup, Menu and Tooltip are higher
    // layers and still promote above it.
    if (auto status = buildLayoutDebuggerUi(ui, rootNode, layoutDebugPanel_); !status) {
        return status;
    }
    if (auto status = registerUiCallbacks(ui, rootNode); !status) {
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
    counters_.finalSelectionKey = selectionKey_;
    counters_.finalSelectionIndex = initialSelection->logicalIndex;
    counters_.hierarchyLogicalItems = hierarchyItemCount(this);
    counters_.selectionVerified = true;

    lastSnackbarFeedback_ = authoringFeedback_;
    imageResolverRegistration_ = std::move(*imageResolverRegistration);
    uiRoot_ = std::move(*root);
    ++counters_.uiRootsCreated;
    iconRollback.release();
    assetRollback.release();
    return Tina::Core::success();
}

} // namespace Tina::EditorApp::WorkspaceInternal
