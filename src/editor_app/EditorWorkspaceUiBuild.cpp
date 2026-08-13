#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

template <typename NodeResult>
[[nodiscard]] Tina::Core::Status storeNode(NodeResult&& result, UI::UINodeId& output)
{
    if (!result) {
        return Tina::Core::failure(std::move(result.error()));
    }
    output = *result;
    return Tina::Core::success();
}

} // namespace

auto EditorWorkspaceState::UiBuildContext::createPanel(
    UI::UINodeId parent, UI::UILayoutStyle layout, UI::UIStyleRoleId role,
    UI::UIStyleClassId styleClass) -> Tina::Core::Result<UI::UINodeId>
{
    UI::UIElementDescriptor descriptor = UI::makePanelElement(layout);
    descriptor.visual.styleRole = role;
    if (styleClass.hasValue()) {
        descriptor.visual.styleClasses = std::span(&styleClass, 1);
    }
    return tree.createElement(parent, descriptor);
}

auto EditorWorkspaceState::UiBuildContext::createLabel(
    UI::UINodeId parent, std::string_view text, UI::UILayoutStyle layout,
    const UI::UITextStyle& style) -> Tina::Core::Result<UI::UINodeId>
{
    UI::UIElementDescriptor descriptor = UI::makeLabelElement(text, layout);
    descriptor.textStyle = style;
    return tree.createElement(parent, descriptor);
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

auto EditorWorkspaceState::UiBuildContext::createTextEdit(
    UI::UINodeId parent, std::string_view text, UI::UILayoutStyle layout,
    bool enabled) -> Tina::Core::Result<UI::UINodeId>
{
    UI::UIElementDescriptor descriptor = UI::makeTextEditElement(text, layout);
    descriptor.textStyle = compactText;
    descriptor.enabled = enabled;
    return tree.createElement(parent, descriptor);
}

auto EditorWorkspaceState::buildToolbarUi(UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    UI::UINodeId toolbar{};
    UI::UILayoutStyle toolbarStyle = fillWidth(48.0F);
    toolbarStyle.flexItem.shrink = 0.0F;
    toolbarStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    toolbarStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    toolbarStyle.flexContainer.gap.column = 8.0F;
    toolbarStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(10.0F, 7.0F);
    if (auto status = storeNode(ui.createPanel(parent, toolbarStyle, UI::UIStyleRoleId::PanelSurface,
                                            dockClass_),
                                toolbar);
        !status) {
        return status;
    }

    UI::UINodeId toolbarBrand{};
    if (auto status = storeNode(ui.createLabel(toolbar, "TINA EDITOR", fixedSize(132.0F, 26.0F), ui.titleText),
                                toolbarBrand);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(toolbar,
                                            workspaceMode_ == WorkspaceMode::World2D
                                                ? "World2D Scene"
                                                : "World3D Scene",
                                            fixedSize(138.0F, 24.0F), ui.bodyText),
                                toolbarDocument_);
        !status) {
        return status;
    }
    UI::UILayoutStyle pathStyle = fixedSize(0.0F, 30.0F);
    pathStyle.size.width = UI::UILayoutLength::Auto();
    pathStyle.flexItem.grow = 1.0F;
    pathStyle.flexItem.shrink = 1.0F;
    pathStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    const WorkspaceSessionState& initialSession = activeWorkspaceSession();
    if (auto status = storeNode(ui.createTextEdit(toolbar,
                                               initialSession.documentPathUtf8,
                                               pathStyle, true),
                                toolbarPath_);
        !status) {
        return status;
    }

    if (auto status = storeNode(ui.createSegmentedButton(toolbar, "2D", fixedSize(46.0F, 30.0F)),
                                mode2DButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(toolbar, "3D", fixedSize(46.0F, 30.0F)),
                                mode3DButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(toolbar, "Play", fixedSize(52.0F, 30.0F), true,
                                             UI::UIStyleRoleId::ButtonPrimary),
                                playButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(toolbar, "Pause", fixedSize(56.0F, 30.0F), false),
                                pauseButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(toolbar, "Step", fixedSize(48.0F, 30.0F), false),
                                stepButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(toolbar, "Stop", fixedSize(48.0F, 30.0F), false),
                                stopButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(toolbar, "Undo", fixedSize(60.0F, 30.0F), true,
                                             UI::UIStyleRoleId::ButtonText), undoButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(toolbar, "Redo", fixedSize(60.0F, 30.0F), true,
                                             UI::UIStyleRoleId::ButtonText), redoButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(toolbar, "Save", fixedSize(58.0F, 30.0F),
                                             initialSession.hasDocumentPath(),
                                             UI::UIStyleRoleId::ButtonOutlined),
                                saveButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(toolbar, "Save As", fixedSize(72.0F, 30.0F), true,
                                             UI::UIStyleRoleId::ButtonOutlined),
                                saveAsButton_);
        !status) {
        return status;
    }

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildContextBarUi(UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    UI::UINodeId contextBar{};
    UI::UILayoutStyle contextBarStyle = fillWidth(34.0F);
    contextBarStyle.flexItem.shrink = 0.0F;
    contextBarStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    contextBarStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    contextBarStyle.flexContainer.gap.column = 6.0F;
    contextBarStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(10.0F, 3.0F);
    if (auto status = storeNode(ui.createPanel(parent, contextBarStyle, UI::UIStyleRoleId::PanelElevated,
                                            viewportClass_),
                                contextBar);
        !status) {
        return status;
    }
    UI::UILayoutStyle breadcrumbStyle = fixedSize(0.0F, 22.0F);
    breadcrumbStyle.size.width = UI::UILayoutLength::Auto();
    breadcrumbStyle.flexItem.grow = 1.0F;
    breadcrumbStyle.flexItem.shrink = 1.0F;
    breadcrumbStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    if (auto status = storeNode(ui.createLabel(contextBar,
                                            workspaceMode_ == WorkspaceMode::World2D
                                                ? "Scene / World2D"
                                                : "Scene / World3D",
                                            breadcrumbStyle, ui.secondaryText),
                                breadcrumb_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(contextBar, "Select", fixedSize(58.0F, 26.0F)),
                                selectToolButtons_[0]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(contextBar, "Move", fixedSize(58.0F, 26.0F)),
                                translateToolButtons_[0]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(contextBar, "Rotate", fixedSize(62.0F, 26.0F)),
                                rotateToolButtons_[0]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(contextBar, "Scale", fixedSize(56.0F, 26.0F)),
                                scaleToolButtons_[0]);
        !status) {
        return status;
    }
    UI::UINodeId contextFrameButton{};
    if (auto status = storeNode(ui.createButton(contextBar, "Frame", fixedSize(58.0F, 26.0F), false),
                                contextFrameButton);
        !status) {
        return status;
    }
    UI::UINodeId snapStatus{};
    if (auto status = storeNode(ui.createLabel(contextBar, "Free Move", fixedSize(72.0F, 20.0F), ui.accentText),
                                snapStatus);
        !status) {
        return status;
    }

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildDocumentTabsUi(UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    UI::UINodeId documentTabsBar{};
    UI::UILayoutStyle documentTabsBarStyle = fillWidth(34.0F);
    documentTabsBarStyle.flexItem.shrink = 0.0F;
    documentTabsBarStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    documentTabsBarStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    documentTabsBarStyle.flexContainer.gap.column = 6.0F;
    documentTabsBarStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(8.0F, 3.0F);
    if (auto status = storeNode(ui.createPanel(parent, documentTabsBarStyle,
                                            UI::UIStyleRoleId::PanelSurface, dockClass_),
                                documentTabsBar);
        !status) {
        return status;
    }
    UI::UINodeId documentTabsTitle{};
    if (auto status = storeNode(ui.createLabel(documentTabsBar, "Documents",
                                            fixedSize(78.0F, 22.0F), ui.secondaryText),
                                documentTabsTitle);
        !status) {
        return status;
    }
    UI::UILayoutStyle documentTabStyle = fixedSize(0.0F, 28.0F);
    documentTabStyle.size.width = UI::UILayoutLength::Auto();
    documentTabStyle.flexItem.grow = 1.0F;
    documentTabStyle.flexItem.shrink = 1.0F;
    documentTabStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    for (u32 index = 0; index < DocumentTabSlots; ++index) {
        const auto* tab = documentTabs_.tab(index);
        if (auto status = storeNode(ui.createSegmentedButton(documentTabsBar,
                                                          tab != nullptr ? tab->title : "Empty",
                                                          documentTabStyle,
                                                          tab != nullptr),
                                    documentTabButtons_[index]);
            !status) {
            return status;
        }
    }
    if (auto status = storeNode(ui.createButton(documentTabsBar, "Close",
                                             fixedSize(58.0F, 28.0F), false),
                                closeDocumentButton_);
        !status) {
        return status;
    }

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildLeftDockUi(UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    UI::UINodeId left{};
    UI::UILayoutStyle leftStyle = boundedDock(22.0F, 220.0F, 300.0F);
    leftStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    leftStyle.flexContainer.gap.row = 7.0F;
    leftStyle.padding = UI::UIEdgeSpacing::All(8.0F);
    if (auto status = storeNode(ui.createPanel(parent, leftStyle, UI::UIStyleRoleId::PanelSurface, dockClass_), left);
        !status) {
        return status;
    }

    UI::UINodeId hierarchyHeader{};
    UI::UILayoutStyle hierarchyHeaderStyle = fillWidth(28.0F);
    hierarchyHeaderStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    hierarchyHeaderStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    if (auto status = storeNode(ui.createPanel(left, hierarchyHeaderStyle, UI::UIStyleRoleId::None),
                                hierarchyHeader);
        !status) {
        return status;
    }
    UI::UILayoutStyle hierarchyTitleStyle = fixedSize(0.0F, 24.0F);
    hierarchyTitleStyle.size.width = UI::UILayoutLength::Auto();
    hierarchyTitleStyle.flexItem.grow = 1.0F;
    hierarchyTitleStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    UI::UINodeId hierarchyTitle{};
    if (auto status = storeNode(ui.createLabel(hierarchyHeader, "Hierarchy", hierarchyTitleStyle, ui.sectionText),
                                hierarchyTitle);
        !status) {
        return status;
    }
    std::string hierarchyCountText =
        std::to_string(hierarchyRows_.empty() ? 0U : hierarchyRows_.size() - 1U);
    hierarchyCountText += " nodes";
    if (auto status = storeNode(ui.createLabel(hierarchyHeader, hierarchyCountText,
                                            fixedSize(72.0F, 20.0F),
                                            ui.secondaryText),
                                hierarchyCount_);
        !status) {
        return status;
    }

    UI::UINodeId hierarchyFilter{};
    if (auto status = storeNode(ui.createTextEdit(left, "Filter hierarchy", fillWidth(32.0F), false),
                                hierarchyFilter);
        !status) {
        return status;
    }
    UI::UINodeId hierarchyActions{};
    UI::UILayoutStyle hierarchyActionsStyle = fillWidth(66.0F);
    hierarchyActionsStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    hierarchyActionsStyle.flexContainer.gap.row = 6.0F;
    if (auto status = storeNode(ui.createPanel(left, hierarchyActionsStyle,
                                            UI::UIStyleRoleId::None),
                                hierarchyActions);
        !status) {
        return status;
    }
    UI::UINodeId hierarchyPrimaryActions{};
    UI::UILayoutStyle hierarchyActionRowStyle = fillWidth(30.0F);
    hierarchyActionRowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    hierarchyActionRowStyle.flexContainer.gap.column = 6.0F;
    if (auto status = storeNode(ui.createPanel(hierarchyActions,
                                            hierarchyActionRowStyle,
                                            UI::UIStyleRoleId::None),
                                hierarchyPrimaryActions);
        !status) {
        return status;
    }
    UI::UINodeId hierarchySecondaryActions{};
    if (auto status = storeNode(ui.createPanel(hierarchyActions,
                                            hierarchyActionRowStyle,
                                            UI::UIStyleRoleId::None),
                                hierarchySecondaryActions);
        !status) {
        return status;
    }
    UI::UILayoutStyle hierarchyActionStyle = fixedSize(0.0F, 30.0F);
    hierarchyActionStyle.size.width = UI::UILayoutLength::Auto();
    hierarchyActionStyle.flexItem.grow = 1.0F;
    hierarchyActionStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    if (auto status = storeNode(ui.createButton(hierarchyPrimaryActions, "+",
                                             hierarchyActionStyle),
                                addEntityButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(hierarchyPrimaryActions, "Duplicate",
                                             hierarchyActionStyle, false),
                                duplicateEntityButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(hierarchyPrimaryActions, "Delete",
                                             hierarchyActionStyle, false,
                                             UI::UIStyleRoleId::ButtonDanger),
                                deleteEntityButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(hierarchySecondaryActions, "To Root",
                                             hierarchyActionStyle, false),
                                reparentEntityRootButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(hierarchySecondaryActions, "Focus",
                                             hierarchyActionStyle, false),
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
                .rowHeight = 32.0F,
                .overscanRows = 1,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = 32.0F,
                .indentation = 16.0F,
                .disclosureExtent = 10.0F,
                .disclosureGap = 4.0F,
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

    UI::UINodeId hierarchyFooter{};
    UI::UILayoutStyle hierarchyFooterStyle = fillWidth(48.0F);
    hierarchyFooterStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(8.0F, 5.0F);
    hierarchyFooterStyle.flexContainer.gap.row = 2.0F;
    if (auto status = storeNode(ui.createPanel(left, hierarchyFooterStyle, UI::UIStyleRoleId::PanelElevated),
                                hierarchyFooter);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(hierarchyFooter, {}, fillWidth(20.0F), ui.bodyText),
                                hierarchySelectionSummary_);
        !status) {
        return status;
    }
    UI::UINodeId hierarchyFooterHint{};
    if (auto status = storeNode(ui.createLabel(hierarchyFooter, "Stable authoring identity", fillWidth(18.0F),
                                            ui.secondaryText),
                                hierarchyFooterHint);
        !status) {
        return status;
    }

    UI::UINodeId projectHeader{};
    UI::UILayoutStyle projectHeaderStyle = fillWidth(28.0F);
    projectHeaderStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    projectHeaderStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    projectHeaderStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
    if (auto status = storeNode(ui.createPanel(left, projectHeaderStyle, UI::UIStyleRoleId::None),
                                projectHeader);
        !status) {
        return status;
    }
    UI::UINodeId projectTitle{};
    if (auto status = storeNode(ui.createLabel(projectHeader, "Project Assets",
                                            fixedSize(118.0F, 24.0F), ui.sectionText),
                                projectTitle);
        !status) {
        return status;
    }
    std::string initialProjectCount = std::to_string(projectAssets_.visibleItemCount());
    initialProjectCount += " / ";
    initialProjectCount += std::to_string(projectAssets_.itemCount());
    if (auto status = storeNode(ui.createLabel(projectHeader, initialProjectCount,
                                            fixedSize(58.0F, 20.0F), ui.secondaryText),
                                projectAssetCount_);
        !status) {
        return status;
    }

    UI::UINodeId projectFilters{};
    UI::UILayoutStyle projectFiltersStyle = fillWidth(28.0F);
    projectFiltersStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    projectFiltersStyle.flexContainer.gap.column = 4.0F;
    if (auto status = storeNode(ui.createPanel(left, projectFiltersStyle,
                                            UI::UIStyleRoleId::None),
                                projectFilters);
        !status) {
        return status;
    }
    const std::array<std::string_view, 4> projectFilterLabels{"All", "2D", "3D", "Media"};
    for (u32 index = 0; index < projectFilterLabels.size(); ++index) {
        UI::UILayoutStyle filterStyle = fixedSize(0.0F, 28.0F);
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
        left, UI::makeListViewElement(
                  {.materializedItemCapacity = AssetBrowserMaterializedCapacity},
                  projectListStyle));
    if (!projectList) {
        return Tina::Core::failure(std::move(projectList.error()));
    }
    projectAssetList_ = *projectList;
    if (auto status = ui.tree.setListViewStyle(
            projectAssetList_,
            UI::UIListViewStyle{
                .rowHeight = 32.0F,
                .overscanRows = 1,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = 32.0F,
            });
        !status) {
        return status;
    }
    if (auto status = ui.tree.setListViewPaint(projectAssetList_,
                                             UI::makeListViewPaint(ui.productTheme));
        !status) {
        return status;
    }
    if (auto status = ui.tree.setListViewDataSource(projectAssetList_,
                                                  projectAssetDataSource());
        !status) {
        return status;
    }
    if (projectAssets_.visibleItemCount() != 0U) {
        if (auto status = ui.tree.setListViewSelectedIndex(projectAssetList_, 0U); !status) {
            return status;
        }
        observedProjectAssetSelectionIndex_ = 0U;
    }

    UI::UINodeId sourceImportHeader{};
    UI::UILayoutStyle sourceImportHeaderStyle = fillWidth(28.0F);
    sourceImportHeaderStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    sourceImportHeaderStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    sourceImportHeaderStyle.flexContainer.gap.column = 4.0F;
    if (auto status = storeNode(ui.createPanel(left, sourceImportHeaderStyle,
                                            UI::UIStyleRoleId::None),
                                sourceImportHeader);
        !status) {
        return status;
    }
    UI::UILayoutStyle sourceImportTitleStyle = fixedSize(0.0F, 24.0F);
    sourceImportTitleStyle.size.width = UI::UILayoutLength::Auto();
    sourceImportTitleStyle.flexItem.grow = 1.0F;
    sourceImportTitleStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    UI::UINodeId sourceImportTitle{};
    if (auto status = storeNode(ui.createLabel(sourceImportHeader, "Source Imports",
                                            sourceImportTitleStyle, ui.sectionText),
                                sourceImportTitle);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(
                                    sourceImportHeader,
                                    std::to_string(sourceImportUnits_.size()),
                                    fixedSize(36.0F, 20.0F), ui.secondaryText),
                                sourceImportCount_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(
                                    sourceImportHeader, "Remove",
                                    fixedSize(62.0F, 26.0F),
                                    activeProjectWorkspace_.has_value() &&
                                        !sourceImportUnits_.empty()),
                                removeSourceImportButton_);
        !status) {
        return status;
    }

    UI::UILayoutStyle sourceImportListStyle = fillWidth(80.0F);
    sourceImportListStyle.minMax.minHeight = UI::UILayoutLength::Px(64.0F);
    auto sourceImportList = ui.tree.createElement(
        left, UI::makeListViewElement(
                  {.materializedItemCapacity = SourceImportMaterializedCapacity},
                  sourceImportListStyle));
    if (!sourceImportList) {
        return Tina::Core::failure(std::move(sourceImportList.error()));
    }
    sourceImportList_ = *sourceImportList;
    if (auto status = ui.tree.setListViewStyle(
            sourceImportList_,
            UI::UIListViewStyle{
                .rowHeight = 28.0F,
                .overscanRows = 1,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = 28.0F,
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

    UI::UINodeId projectLifecycleActions{};
    UI::UILayoutStyle projectLifecycleActionsStyle = fillWidth(30.0F);
    projectLifecycleActionsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    projectLifecycleActionsStyle.flexContainer.gap.column = 6.0F;
    if (auto status = storeNode(ui.createPanel(left, projectLifecycleActionsStyle,
                                            UI::UIStyleRoleId::None),
                                projectLifecycleActions);
        !status) {
        return status;
    }
    UI::UILayoutStyle projectLifecycleActionStyle = fixedSize(0.0F, 30.0F);
    projectLifecycleActionStyle.size.width = UI::UILayoutLength::Auto();
    projectLifecycleActionStyle.flexItem.grow = 1.0F;
    projectLifecycleActionStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    if (auto status = storeNode(ui.createButton(projectLifecycleActions, "New Project",
                                             projectLifecycleActionStyle),
                                createProjectButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(projectLifecycleActions, "Open Project",
                                             projectLifecycleActionStyle),
                                openProjectButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(
                                    projectLifecycleActions, "Import Source",
                                    projectLifecycleActionStyle,
                                    activeProjectWorkspace_.has_value()),
                                importSourceButton_);
        !status) {
        return status;
    }

    UI::UINodeId projectActions{};
    UI::UILayoutStyle projectActionsStyle = fillWidth(30.0F);
    projectActionsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    projectActionsStyle.flexContainer.gap.column = 6.0F;
    if (auto status = storeNode(ui.createPanel(left, projectActionsStyle,
                                            UI::UIStyleRoleId::None),
                                projectActions);
        !status) {
        return status;
    }
    UI::UILayoutStyle projectActionStyle = fixedSize(0.0F, 30.0F);
    projectActionStyle.size.width = UI::UILayoutLength::Auto();
    projectActionStyle.flexItem.grow = 1.0F;
    projectActionStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    if (auto status = storeNode(ui.createButton(projectActions, "Open Asset",
                                             projectActionStyle,
                                             projectAssets_.visibleItemCount() != 0U),
                                openProjectAssetButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(
                                    projectActions, "Refresh", projectActionStyle,
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

auto EditorWorkspaceState::buildViewportUi(UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    UI::UINodeId center{};
    UI::UILayoutStyle centerStyle = growingRegion();
    centerStyle.size.width = UI::UILayoutLength::Percent(52.0F);
    centerStyle.minMax.minWidth = UI::UILayoutLength::Px(360.0F);
    centerStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    centerStyle.flexContainer.gap.row = 7.0F;
    centerStyle.padding = UI::UIEdgeSpacing::All(8.0F);
    if (auto status = storeNode(ui.createPanel(parent, centerStyle, UI::UIStyleRoleId::None), center);
        !status) {
        return status;
    }

    UI::UINodeId viewportHeader{};
    UI::UILayoutStyle viewportHeaderStyle = fillWidth(30.0F);
    viewportHeaderStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    viewportHeaderStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    viewportHeaderStyle.flexContainer.gap.column = 8.0F;
    if (auto status = storeNode(ui.createPanel(center, viewportHeaderStyle, UI::UIStyleRoleId::None),
                                viewportHeader);
        !status) {
        return status;
    }
    UI::UILayoutStyle viewportTitleStyle = fixedSize(0.0F, 24.0F);
    viewportTitleStyle.size.width = UI::UILayoutLength::Auto();
    viewportTitleStyle.flexItem.grow = 1.0F;
    viewportTitleStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    if (auto status = storeNode(ui.createLabel(viewportHeader,
                                            workspaceMode_ == WorkspaceMode::World2D
                                                ? "World2D Viewport"
                                                : "World3D Viewport",
                                            viewportTitleStyle, ui.sectionText),
                                viewportTitle_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(viewportHeader,
                         workspaceMode_ == WorkspaceMode::World2D
                             ? "Orthographic"
                             : "View: Custom",
                         fixedSize(136.0F, 28.0F),
                         workspaceMode_ == WorkspaceMode::World3D),
            viewportMode_);
        !status) {
        return status;
    }

    UI::UINodeId viewportTools{};
    UI::UILayoutStyle viewportToolsStyle = fillWidth(32.0F);
    viewportToolsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    viewportToolsStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    viewportToolsStyle.flexContainer.gap.column = 6.0F;
    if (auto status = storeNode(ui.createPanel(center, viewportToolsStyle, UI::UIStyleRoleId::PanelSurface),
                                viewportTools);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(viewportTools, "Select", fixedSize(64.0F, 28.0F)),
                                selectToolButtons_[1]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(viewportTools, "Move", fixedSize(64.0F, 28.0F)),
                                translateToolButtons_[1]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(viewportTools, "Rotate", fixedSize(62.0F, 28.0F)),
                                rotateToolButtons_[1]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(viewportTools, "Scale", fixedSize(56.0F, 28.0F)),
                                scaleToolButtons_[1]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(viewportTools, "Paint", fixedSize(60.0F, 28.0F), false),
                                tilePaintToolButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(viewportTools, "Erase", fixedSize(60.0F, 28.0F), false),
                                tileEraseToolButton_);
        !status) {
        return status;
    }
    UI::UINodeId viewportOptions{};
    UI::UILayoutStyle viewportOptionsStyle = fillWidth(32.0F);
    viewportOptionsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    viewportOptionsStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    viewportOptionsStyle.flexContainer.gap.column = 6.0F;
    if (auto status = storeNode(ui.createPanel(center, viewportOptionsStyle,
                                            UI::UIStyleRoleId::PanelSurface),
                                viewportOptions);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(viewportOptions, "World", fixedSize(56.0F, 28.0F)),
                                orientationButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(viewportOptions, "Snap Off", fixedSize(64.0F, 28.0F)),
                                snapButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(viewportOptions, "Replace", fixedSize(58.0F, 28.0F)),
                                marqueeModeButtons_[0]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(viewportOptions, "Add", fixedSize(40.0F, 28.0F)),
                                marqueeModeButtons_[1]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createSegmentedButton(viewportOptions, "Toggle", fixedSize(52.0F, 28.0F)),
                                marqueeModeButtons_[2]);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(viewportOptions, "Frame All", fixedSize(64.0F, 28.0F)),
                                frameAllButton_);
        !status) {
        return status;
    }
    UI::UINodeId viewportCanvas{};
    UI::UILayoutStyle viewportCanvasStyle = growingRegion();
    viewportCanvasStyle.minMax.minHeight = UI::UILayoutLength::Px(220.0F);
    viewportCanvasStyle.padding = UI::UIEdgeSpacing::All(5.0F);
    viewportCanvasStyle.flexContainer.gap.row = 4.0F;
    if (auto status = storeNode(ui.createPanel(center, viewportCanvasStyle, UI::UIStyleRoleId::None),
                                viewportCanvas);
        !status) {
        return status;
    }
    UI::UINodeId viewportCanvasTop{};
    UI::UILayoutStyle viewportCanvasTopStyle = fillWidth(22.0F);
    viewportCanvasTopStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    viewportCanvasTopStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
    if (auto status = storeNode(ui.createPanel(viewportCanvas, viewportCanvasTopStyle, UI::UIStyleRoleId::None),
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
    if (auto status = storeNode(ui.createPanel(viewportCanvas, viewportSceneAreaStyle,
                                            UI::UIStyleRoleId::None),
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
    if (auto status = storeNode(ui.createPanel(viewportSceneArea, previewFrameStyle,
                                            UI::UIStyleRoleId::None),
                                previewFrame);
        !status) {
        return status;
    }
    UI::UILayoutStyle previewWorldLayerStyle = growingRegion();
    previewWorldLayerStyle.minMax.minHeight = UI::UILayoutLength::Px(180.0F);
    previewWorldLayerStyle.placement = UI::UILayoutPlacement::Flow;
    previewWorldLayerStyle.clipDescendants = true;
    if (auto status = storeNode(ui.createPanel(previewFrame, previewWorldLayerStyle,
                                            UI::UIStyleRoleId::None),
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
    if (auto status = storeNode(ui.createPanel(viewportCanvas, viewportCanvasBottomStyle,
                                            UI::UIStyleRoleId::None),
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
    UI::UILayoutStyle viewportFooterStyle = fillWidth(32.0F);
    viewportFooterStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    viewportFooterStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
    viewportFooterStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    viewportFooterStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(8.0F, 3.0F);
    if (auto status = storeNode(ui.createPanel(center, viewportFooterStyle, UI::UIStyleRoleId::PanelSurface),
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
    UI::UILayoutStyle zoomControlsStyle = fixedSize(208.0F, 26.0F);
    zoomControlsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    zoomControlsStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    zoomControlsStyle.flexContainer.gap.column = 4.0F;
    if (auto status = storeNode(ui.createPanel(viewportFooter, zoomControlsStyle,
                                            UI::UIStyleRoleId::None),
                                zoomControls);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(zoomControls, "-", fixedSize(28.0F, 26.0F)),
                                zoomOutButton_);
        !status) {
        return status;
    }
    UI::UIElementDescriptor zoomSliderDesc =
        UI::makeSliderElement(fixedSize(86.0F, 24.0F));
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
    if (auto status = storeNode(ui.createButton(zoomControls, "+", fixedSize(28.0F, 26.0F)),
                                zoomInButton_);
        !status) {
        return status;
    }

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildInspectorUi(UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    UI::UINodeId right{};
    UI::UILayoutStyle rightStyle = boundedDock(26.0F, 280.0F, 360.0F);
    rightStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    rightStyle.flexContainer.gap.row = 7.0F;
    rightStyle.padding = UI::UIEdgeSpacing::All(8.0F);
    if (auto status = storeNode(ui.createPanel(parent, rightStyle, UI::UIStyleRoleId::PanelSurface, dockClass_),
                                right);
        !status) {
        return status;
    }

    UI::UINodeId inspectorHeader{};
    UI::UILayoutStyle inspectorHeaderStyle = fillWidth(30.0F);
    inspectorHeaderStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    inspectorHeaderStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    inspectorHeaderStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
    if (auto status = storeNode(ui.createPanel(right, inspectorHeaderStyle, UI::UIStyleRoleId::None),
                                inspectorHeader);
        !status) {
        return status;
    }
    UI::UINodeId inspectorTitle{};
    if (auto status = storeNode(ui.createLabel(inspectorHeader, "Inspector", fixedSize(110.0F, 24.0F),
                                            ui.sectionText),
                                inspectorTitle);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(inspectorHeader, "Selection", fixedSize(88.0F, 20.0F),
                                            ui.accentText),
                                inspectorMode_);
        !status) {
        return status;
    }

    UI::UILayoutStyle inspectorScrollStyle = growingRegion();
    inspectorScrollStyle.minMax.minHeight = UI::UILayoutLength::Px(180.0F);
    auto inspectorScroll = ui.tree.createElement(right, UI::makeScrollViewElement(inspectorScrollStyle));
    if (!inspectorScroll) {
        return Tina::Core::failure(std::move(inspectorScroll.error()));
    }
    if (auto status = ui.tree.setScrollViewStyle(
            *inspectorScroll,
            UI::UIScrollViewStyle{
                .axes = UI::UIScrollAxes::Vertical,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = 36.0F,
            });
        !status) {
        return status;
    }
    counters_.inspectorScrollConfigured = true;

    UI::UINodeId inspectorContent{};
    UI::UILayoutStyle inspectorContentStyle = fillWidth(1980.0F);
    inspectorContentStyle.padding = UI::UIEdgeSpacing::All(8.0F);
    inspectorContentStyle.flexContainer.gap.row = 7.0F;
    if (auto status = storeNode(ui.createPanel(*inspectorScroll, inspectorContentStyle,
                                            UI::UIStyleRoleId::PanelSurface),
                                inspectorContent);
        !status) {
        return status;
    }

    UI::UINodeId identityTitle{};
    if (auto status = storeNode(ui.createLabel(inspectorContent, "Identity", fillWidth(22.0F), ui.sectionText),
                                identityTitle);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(inspectorContent, {}, fillWidth(22.0F), ui.bodyText),
                                inspectorName_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(inspectorContent, {}, fillWidth(22.0F), ui.secondaryText),
                                inspectorKind_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(inspectorContent, {}, fillWidth(42.0F), ui.secondaryText),
                                inspectorNote_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(inspectorContent, {}, fillWidth(42.0F), ui.secondaryText),
                                inspectorAssetPath_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(inspectorContent, {}, fillWidth(22.0F), ui.sectionText),
                                inspectorDependencySummary_);
        !status) {
        return status;
    }
    auto inspectorDependencies = ui.tree.createElement(
        inspectorContent,
        UI::makeListViewElement(
            {.materializedItemCapacity = 6}, fillWidth(156.0F)));
    if (!inspectorDependencies) {
        return Tina::Core::failure(std::move(inspectorDependencies.error()));
    }
    inspectorDependencyList_ = *inspectorDependencies;
    if (auto status = ui.tree.setListViewStyle(
            inspectorDependencyList_,
            UI::UIListViewStyle{
                .rowHeight = 36.0F,
                .overscanRows = 1,
                .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                .wheelStep = 36.0F,
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

    UI::UINodeId transformTitle{};
    if (auto status = storeNode(ui.createLabel(inspectorContent, "Transform", fillWidth(22.0F), ui.sectionText),
                                transformTitle);
        !status) {
        return status;
    }
    const auto createTransformRow = [&](std::string_view caption, std::string_view value,
                                        bool enabled, UI::UINodeId& valueNode) -> Tina::Core::Status {
        UI::UINodeId row{};
        UI::UILayoutStyle rowStyle = fillWidth(30.0F);
        rowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        rowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        rowStyle.flexContainer.gap.column = 8.0F;
        if (auto status = storeNode(ui.createPanel(inspectorContent, rowStyle, UI::UIStyleRoleId::None), row);
            !status) {
            return status;
        }
        UI::UINodeId captionNode{};
        if (auto status = storeNode(ui.createLabel(row, caption, fixedSize(74.0F, 20.0F), ui.secondaryText),
                                    captionNode);
            !status) {
            return status;
        }
        UI::UILayoutStyle valueStyle = fixedSize(0.0F, 30.0F);
        valueStyle.size.width = UI::UILayoutLength::Auto();
        valueStyle.flexItem.grow = 1.0F;
        valueStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        return storeNode(ui.createTextEdit(row, value, valueStyle, enabled), valueNode);
    };
    if (auto status = createTransformRow("Position X", "0.000", true, inspectorPositionX_); !status) {
        return status;
    }
    if (auto status = createTransformRow("Position Y", "0.000", true, inspectorPositionY_); !status) {
        return status;
    }
    if (auto status = createTransformRow("Position Z", "0.000", false, inspectorPositionZ_); !status) {
        return status;
    }
    if (auto status = createTransformRow("Rotation X", "0.000", false, inspectorRotationX_); !status) {
        return status;
    }
    if (auto status = createTransformRow("Rotation Y", "0.000", false, inspectorRotationY_); !status) {
        return status;
    }
    if (auto status = createTransformRow("Rotation Z", "0.000", true, inspectorRotationZ_); !status) {
        return status;
    }
    if (auto status = createTransformRow("Scale X", "1.000", true, inspectorScaleX_); !status) {
        return status;
    }
    if (auto status = createTransformRow("Scale Y", "1.000", true, inspectorScaleY_); !status) {
        return status;
    }
    if (auto status = createTransformRow("Scale Z", "1.000", false, inspectorScaleZ_); !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(inspectorContent, "Apply Transform", fillWidth(34.0F)),
                                applyTransformButton_);
        !status) {
        return status;
    }

    UI::UINodeId componentsTitle{};
    if (auto status = storeNode(ui.createLabel(inspectorContent, "Components", fillWidth(22.0F), ui.sectionText),
                                componentsTitle);
        !status) {
        return status;
    }
    const auto createComponentSection =
        [&](ComponentSectionUi& section, std::string_view name,
            std::span<const std::string_view> captions, bool withAssign,
            std::string_view applyCaption) -> Tina::Core::Status {
        UI::UINodeId headerRow{};
        UI::UILayoutStyle headerRowStyle = fillWidth(30.0F);
        headerRowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        headerRowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        headerRowStyle.flexContainer.gap.column = 8.0F;
        if (auto status = storeNode(ui.createPanel(inspectorContent, headerRowStyle,
                                                UI::UIStyleRoleId::None),
                                    headerRow);
            !status) {
            return status;
        }
        UI::UIElementDescriptor checkboxDesc = UI::makeCheckboxElement(fixedSize(20.0F, 20.0F));
        checkboxDesc.enabled = false;
        if (auto status = storeNode(ui.tree.createElement(headerRow, checkboxDesc),
                                    section.activeCheckbox);
            !status) {
            return status;
        }
        if (auto status = storeNode(ui.createLabel(headerRow, name, fixedSize(150.0F, 20.0F), ui.bodyText),
                                    section.headerLabel);
            !status) {
            return status;
        }
        if (auto status = storeNode(ui.createButton(headerRow, "Add", fixedSize(58.0F, 26.0F), false),
                                    section.addButton);
            !status) {
            return status;
        }
        if (auto status = storeNode(ui.createButton(headerRow, "Remove", fixedSize(76.0F, 26.0F), false),
                                    section.removeButton);
            !status) {
            return status;
        }
        section.fieldCount = captions.size();
        for (Tina::Core::usize fieldIndex = 0; fieldIndex < captions.size(); ++fieldIndex) {
            UI::UINodeId row{};
            UI::UILayoutStyle rowStyle = fillWidth(30.0F);
            rowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
            rowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
            rowStyle.flexContainer.gap.column = 8.0F;
            if (auto status = storeNode(ui.createPanel(inspectorContent, rowStyle,
                                                    UI::UIStyleRoleId::None),
                                        row);
                !status) {
                return status;
            }
            UI::UINodeId caption{};
            if (auto status = storeNode(ui.createLabel(row, captions[fieldIndex],
                                                    fixedSize(88.0F, 20.0F), ui.secondaryText),
                                        caption);
                !status) {
                return status;
            }
            UI::UILayoutStyle valueStyle = fixedSize(0.0F, 30.0F);
            valueStyle.size.width = UI::UILayoutLength::Auto();
            valueStyle.flexItem.grow = 1.0F;
            valueStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
            if (auto status = storeNode(ui.createTextEdit(row, "n/a", valueStyle, false),
                                        section.fields[fieldIndex]);
                !status) {
                return status;
            }
        }
        if (withAssign) {
            if (auto status = storeNode(
                    ui.createButton(inspectorContent, "Assign Sprite From Selection",
                                 fillWidth(30.0F), false),
                    section.assignButton);
                !status) {
                return status;
            }
        }
        if (!applyCaption.empty()) {
            if (auto status = storeNode(
                    ui.createButton(inspectorContent, applyCaption, fillWidth(30.0F), false),
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
        const std::array<std::string_view, 6> lightCaptions{
            "Color R", "Color G", "Color B", "Intensity", "Radius", "Src Radius"};
        if (auto status = createComponentSection(
                componentSections_[2], "PointLight2D", lightCaptions, false,
                "Apply PointLight2D");
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

    UI::UINodeId hierarchyInspectorTitle{};
    if (auto status = storeNode(
            ui.createLabel(inspectorContent, "Hierarchy", fillWidth(22.0F), ui.sectionText),
            hierarchyInspectorTitle);
        !status) {
        return status;
    }
    UI::UINodeId parentRow{};
    UI::UILayoutStyle parentRowStyle = fillWidth(30.0F);
    parentRowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    parentRowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    parentRowStyle.flexContainer.gap.column = 8.0F;
    if (auto status = storeNode(
            ui.createPanel(inspectorContent, parentRowStyle, UI::UIStyleRoleId::None),
            parentRow);
        !status) {
        return status;
    }
    UI::UINodeId parentCaption{};
    if (auto status = storeNode(
            ui.createLabel(parentRow, "Parent Stable ID", fixedSize(116.0F, 20.0F), ui.secondaryText),
            parentCaption);
        !status) {
        return status;
    }
    UI::UILayoutStyle parentValueStyle = fixedSize(0.0F, 30.0F);
    parentValueStyle.size.width = UI::UILayoutLength::Auto();
    parentValueStyle.flexItem.grow = 1.0F;
    parentValueStyle.flexItem.shrink = 1.0F;
    parentValueStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    if (auto status = storeNode(
            ui.createTextEdit(parentRow, "0", parentValueStyle, false),
            inspectorParentStableId_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(inspectorContent, "Apply Parent", fillWidth(34.0F)),
            reparentEntityButton_);
        !status) {
        return status;
    }

    UI::UINodeId authoringTitle{};
    if (auto status = storeNode(ui.createLabel(inspectorContent, "Authoring", fillWidth(22.0F), ui.sectionText),
                                authoringTitle);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(inspectorContent, "Move X +1", fillWidth(34.0F)), moveButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(inspectorContent, "One validated revision per command",
                                            fillWidth(20.0F), ui.secondaryText),
                                authoringHint_);
        !status) {
        return status;
    }

    UI::UINodeId tileMapTitle{};
    if (auto status = storeNode(ui.createLabel(inspectorContent, "TileMap", fillWidth(22.0F), ui.sectionText),
                                tileMapTitle);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(inspectorContent, {}, fillWidth(42.0F), ui.secondaryText),
                                tileMapStatus_);
        !status) {
        return status;
    }
    const auto createTileMapActionRow = [&](UI::UINodeId& row) -> Tina::Core::Status {
        UI::UILayoutStyle rowStyle = fillWidth(34.0F);
        rowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        rowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        rowStyle.flexContainer.gap.column = 6.0F;
        return storeNode(ui.createPanel(inspectorContent, rowStyle, UI::UIStyleRoleId::None), row);
    };
    UI::UINodeId tileMapBrushRow{};
    if (auto status = createTileMapActionRow(tileMapBrushRow); !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(tileMapBrushRow, "Paint Cell", fixedSize(104.0F, 30.0F)),
                                paintTileButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(tileMapBrushRow, "Erase Cell", fixedSize(104.0F, 30.0F)),
                                eraseTileButton_);
        !status) {
        return status;
    }
    UI::UINodeId tileMapLayerRow{};
    if (auto status = createTileMapActionRow(tileMapLayerRow); !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(tileMapLayerRow, "Toggle Layer", fixedSize(104.0F, 30.0F)),
                                toggleTileLayerButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(tileMapLayerRow, "Cook Preview", fixedSize(104.0F, 30.0F)),
                                cookTileMapButton_);
        !status) {
        return status;
    }
    UI::UINodeId tileMapAddRow{};
    if (auto status = createTileMapActionRow(tileMapAddRow); !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(tileMapAddRow, "+ Tile Layer", fixedSize(104.0F, 30.0F)),
                                addTileLayerButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(tileMapAddRow, "+ Object Layer", fixedSize(104.0F, 30.0F)),
                                addObjectLayerButton_);
        !status) {
        return status;
    }
    UI::UINodeId tileMapGameplayRow{};
    if (auto status = createTileMapActionRow(tileMapGameplayRow); !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(tileMapGameplayRow, "Generate Gameplay",
                                             fixedSize(104.0F, 30.0F)),
                                generateTileMapGameplayButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(tileMapGameplayRow, "Bake Navigation",
                                             fixedSize(104.0F, 30.0F)),
                                bakeNavigationButton_);
        !status) {
        return status;
    }

    UI::UINodeId documentTitle{};
    if (auto status = storeNode(ui.createLabel(inspectorContent, "Document", fillWidth(22.0F), ui.sectionText),
                                documentTitle);
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

auto EditorWorkspaceState::buildTimelineUi(UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    UI::UINodeId animationTimeline{};
    UI::UILayoutStyle animationTimelineStyle = fillWidth(174.0F);
    animationTimelineStyle.flexItem.shrink = 0.0F;
    animationTimelineStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    animationTimelineStyle.flexContainer.gap.row = 5.0F;
    animationTimelineStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(10.0F, 6.0F);
    if (auto status = storeNode(ui.createPanel(parent, animationTimelineStyle,
                                            UI::UIStyleRoleId::PanelSurface, dockClass_),
                                animationTimeline);
        !status) {
        return status;
    }

    UI::UINodeId animationHeader{};
    UI::UILayoutStyle animationHeaderStyle = fillWidth(30.0F);
    animationHeaderStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    animationHeaderStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    animationHeaderStyle.flexContainer.gap.column = 7.0F;
    if (auto status = storeNode(ui.createPanel(animationTimeline, animationHeaderStyle,
                                            UI::UIStyleRoleId::None),
                                animationHeader);
        !status) {
        return status;
    }
    UI::UINodeId animationTitle{};
    if (auto status = storeNode(ui.createLabel(animationHeader, "SpriteAnimationClip Timeline",
                                            fixedSize(210.0F, 24.0F), ui.sectionText),
                                animationTitle);
        !status) {
        return status;
    }
    UI::UILayoutStyle animationStatusStyle = fixedSize(0.0F, 22.0F);
    animationStatusStyle.size.width = UI::UILayoutLength::Auto();
    animationStatusStyle.flexItem.grow = 1.0F;
    animationStatusStyle.flexItem.shrink = 1.0F;
    animationStatusStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    if (auto status = storeNode(ui.createLabel(animationHeader, {}, animationStatusStyle,
                                            ui.secondaryText),
                                animationStatus_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationHeader, "Mode: Loop",
                                             fixedSize(96.0F, 28.0F)),
                                animationModeButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationHeader, "Play",
                                             fixedSize(58.0F, 28.0F)),
                                animationPlayButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationHeader, "Cook",
                                             fixedSize(58.0F, 28.0F)),
                                animationCookButton_);
        !status) {
        return status;
    }

    UI::UINodeId animationFrames{};
    UI::UILayoutStyle animationFramesStyle = fillWidth(32.0F);
    animationFramesStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    animationFramesStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    animationFramesStyle.flexContainer.gap.column = 6.0F;
    if (auto status = storeNode(ui.createPanel(animationTimeline, animationFramesStyle,
                                            UI::UIStyleRoleId::None),
                                animationFrames);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationFrames, "Prev",
                                             fixedSize(54.0F, 28.0F)),
                                animationPreviousButton_);
        !status) {
        return status;
    }
    for (u32 frameIndex = 0; frameIndex < animationFrameButtons_.size(); ++frameIndex) {
        if (auto status = storeNode(ui.createButton(animationFrames, "--",
                                                 fixedSize(74.0F, 28.0F), false),
                                    animationFrameButtons_[frameIndex]);
            !status) {
            return status;
        }
    }
    if (auto status = storeNode(ui.createButton(animationFrames, "Next",
                                             fixedSize(54.0F, 28.0F)),
                                animationNextButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationFrames, "Add",
                                             fixedSize(50.0F, 28.0F)),
                                animationAddButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationFrames, "Duplicate",
                                             fixedSize(76.0F, 28.0F)),
                                animationDuplicateButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationFrames, "Delete",
                                             fixedSize(58.0F, 28.0F), true,
                                             UI::UIStyleRoleId::ButtonDanger),
                                animationDeleteButton_);
        !status) {
        return status;
    }

    UI::UINodeId animationEditRow{};
    UI::UILayoutStyle animationEditStyle = fillWidth(30.0F);
    animationEditStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    animationEditStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    animationEditStyle.flexContainer.gap.column = 6.0F;
    if (auto status = storeNode(ui.createPanel(animationTimeline, animationEditStyle,
                                            UI::UIStyleRoleId::None),
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
    if (auto status = storeNode(ui.createButton(animationEditRow, "Next Sprite",
                                             fixedSize(82.0F, 28.0F)),
                                animationCycleSpriteButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationEditRow, "Move Left",
                                             fixedSize(78.0F, 28.0F)),
                                animationMoveLeftButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationEditRow, "Move Right",
                                             fixedSize(82.0F, 28.0F)),
                                animationMoveRightButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationEditRow, "Duration -",
                                             fixedSize(82.0F, 28.0F)),
                                animationDurationDecreaseButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationEditRow, "Duration +",
                                             fixedSize(82.0F, 28.0F)),
                                animationDurationIncreaseButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationEditRow, "Undo",
                                             fixedSize(54.0F, 28.0F), true,
                                             UI::UIStyleRoleId::ButtonText),
                                animationUndoButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationEditRow, "Redo",
                                             fixedSize(54.0F, 28.0F), true,
                                             UI::UIStyleRoleId::ButtonText),
                                animationRedoButton_);
        !status) {
        return status;
    }

    UI::UINodeId animationEventRow{};
    UI::UILayoutStyle animationEventStyle = fillWidth(30.0F);
    animationEventStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    animationEventStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    animationEventStyle.flexContainer.gap.column = 6.0F;
    if (auto status = storeNode(ui.createPanel(animationTimeline, animationEventStyle,
                                            UI::UIStyleRoleId::None),
                                animationEventRow);
        !status) {
        return status;
    }
    UI::UINodeId animationEventLabel{};
    if (auto status = storeNode(ui.createLabel(animationEventRow, "Notify",
                                            fixedSize(44.0F, 22.0F), ui.secondaryText),
                                animationEventLabel);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationEventRow, "Prev Event",
                                             fixedSize(78.0F, 28.0F)),
                                animationEventPreviousButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationEventRow, "Next Event",
                                             fixedSize(78.0F, 28.0F)),
                                animationEventNextButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createTextEdit(animationEventRow, "footstep",
                                               fixedSize(116.0F, 28.0F), true),
                                animationEventTag_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createTextEdit(animationEventRow, "50%",
                                               fixedSize(72.0F, 28.0F), true),
                                animationEventOffset_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationEventRow, "Add Event",
                                             fixedSize(74.0F, 28.0F)),
                                animationEventAddButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationEventRow, "Apply",
                                             fixedSize(58.0F, 28.0F), false),
                                animationEventApplyButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createButton(animationEventRow, "Remove",
                                             fixedSize(66.0F, 28.0F), false,
                                             UI::UIStyleRoleId::ButtonDanger),
                                animationEventRemoveButton_);
        !status) {
        return status;
    }

    return Tina::Core::success();
}

auto EditorWorkspaceState::buildStatusBarUi(UiBuildContext& ui, UI::UINodeId parent) -> Tina::Core::Status
{
    UI::UINodeId statusBar{};
    UI::UILayoutStyle statusBarStyle = fillWidth(30.0F);
    statusBarStyle.flexItem.shrink = 0.0F;
    statusBarStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    statusBarStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
    statusBarStyle.flexContainer.gap.column = 12.0F;
    statusBarStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(10.0F, 4.0F);
    if (auto status = storeNode(ui.createPanel(parent, statusBarStyle, UI::UIStyleRoleId::PanelSurface,
                                            dockClass_),
                                statusBar);
        !status) {
        return status;
    }
    UI::UILayoutStyle statusSegmentStyle = fixedSize(0.0F, 20.0F);
    statusSegmentStyle.size.width = UI::UILayoutLength::Auto();
    statusSegmentStyle.flexItem.grow = 1.0F;
    statusSegmentStyle.flexItem.shrink = 1.0F;
    statusSegmentStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    if (auto status = storeNode(ui.createLabel(statusBar, {}, statusSegmentStyle, ui.compactText),
                                statusDocument_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(statusBar, {}, statusSegmentStyle, ui.secondaryText),
                                statusPreview_);
        !status) {
        return status;
    }
    if (auto status = storeNode(ui.createLabel(statusBar, {}, fixedSize(190.0F, 20.0F), ui.accentText),
                                statusSelection_);
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
    if (auto status = storeNode(
            ui.createTextEdit(dirtyCloseModal_, {}, fillWidth(32.0F), true),
            dirtyClosePath_);
        !status) {
        return status;
    }
    UI::UINodeId dirtyCloseActions{};
    UI::UILayoutStyle dirtyCloseActionsStyle = fillWidth(34.0F);
    dirtyCloseActionsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    dirtyCloseActionsStyle.flexContainer.justifyContent =
        UI::UIJustifyContent::End;
    dirtyCloseActionsStyle.flexContainer.gap.column = 8.0F;
    if (auto status = storeNode(
            ui.createPanel(dirtyCloseModal_, dirtyCloseActionsStyle,
                        UI::UIStyleRoleId::None),
            dirtyCloseActions);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(dirtyCloseActions, "Save", fixedSize(86.0F, 32.0F), true,
                            UI::UIStyleRoleId::ButtonPrimary),
            dirtyCloseSaveButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(dirtyCloseActions, "Discard", fixedSize(86.0F, 32.0F), true,
                            UI::UIStyleRoleId::ButtonDanger),
            dirtyCloseDiscardButton_);
        !status) {
        return status;
    }
    if (auto status = storeNode(
            ui.createButton(dirtyCloseActions, "Cancel", fixedSize(86.0F, 32.0F), true,
                            UI::UIStyleRoleId::ButtonText),
            dirtyCloseCancelButton_);
        !status) {
        return status;
    }

    return Tina::Core::success();
}

auto EditorWorkspaceState::registerUiCallbacks(UiBuildContext& ui) -> Tina::Core::Status
{
    if (auto status = ui.tree.setButtonAction(
            mode2DButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::SwitchToWorld2D);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            mode3DButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::SwitchToWorld3D);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            viewportMode_,
            UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::ViewportCyclePreset);
            }});
        !status) {
        return status;
    }
    if (auto status = ui.tree.setButtonAction(
            moveButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                queueEditorCommand(EditorCommand::MoveSelectedPositiveX);
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
                    section.activeCheckbox,
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
        std::pair{reparentEntityRootButton_, EditorCommand::SceneReparentRoot},
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
        std::pair{animationPlayButton_, EditorCommand::AnimationTogglePlayback},
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
        std::pair{animationUndoButton_, EditorCommand::AnimationUndo},
        std::pair{animationRedoButton_, EditorCommand::AnimationRedo},
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

    // Startup-only StyleClass/ColorToken/sheet must run before createRoot().
    auto dockClass = rootBuilder->registerStyleClass();
    if (!dockClass) {
        return Tina::Core::failure(std::move(dockClass.error()));
    }
    auto viewportClass = rootBuilder->registerStyleClass();
    if (!viewportClass) {
        return Tina::Core::failure(std::move(viewportClass.error()));
    }
    auto dockToken = rootBuilder->registerStyleColorToken(UI::rgb(0x152232));
    if (!dockToken) {
        return Tina::Core::failure(std::move(dockToken.error()));
    }
    auto viewportToken = rootBuilder->registerStyleColorToken(UI::rgb(0x0C141E));
    if (!viewportToken) {
        return Tina::Core::failure(std::move(viewportToken.error()));
    }
    dockClass_ = *dockClass;
    viewportClass_ = *viewportClass;
    viewportToken_ = *viewportToken;

    const std::array rules{
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelSurface,
            .styleClass = dockClass_,
            .colorToken = *dockToken,
        },
        UI::UIStyleBoxFillRule{
            .role = UI::UIStyleRoleId::PanelElevated,
            .styleClass = viewportClass_,
            .colorToken = viewportToken_,
        },
    };
    if (auto status = rootBuilder->installStyleSheet(std::span(rules)); !status) {
        return status;
    }
    counters_.stylesheetInstalled = true;
    counters_.styleRegisteredClasses = 2;
    counters_.styleRegisteredTokens = 2;
    counters_.styleActiveRules = rules.size();
    counters_.styleRevision = 1;

    auto root = rootBuilder->createRoot();
    if (!root) {
        return Tina::Core::failure(std::move(root.error()));
    }
    auto tree = rootBuilder->treeUpdater(*root);
    if (!tree) {
        return Tina::Core::failure(std::move(tree.error()));
    }

    UI::UITheme productTheme = UI::makeDefaultProductTheme();
    productTheme.buttonTextSize = EditorTypographyMetrics::Compact;
    productTheme.bodyTextSize = EditorTypographyMetrics::Body;
    productTheme.titleTextSize = EditorTypographyMetrics::Title;
    if (auto status = tree->setProductTheme(productTheme); !status) {
        return status;
    }

    UiBuildContext ui{
        .tree = *tree,
        .productTheme = productTheme,
        .titleText = UI::makeTitleTextStyle(productTheme, EditorTypographyMetrics::Title),
        .sectionText = UI::makeTitleTextStyle(productTheme, EditorTypographyMetrics::Section),
        .bodyText = UI::makeBodyTextStyle(productTheme, EditorTypographyMetrics::Body),
        .compactText = UI::makeBodyTextStyle(productTheme, EditorTypographyMetrics::Compact),
        .secondaryText = UI::makeSecondaryTextStyle(productTheme, EditorTypographyMetrics::Secondary),
        .accentText = UI::makeAccentTextStyle(productTheme, EditorTypographyMetrics::Accent),
    };
    UI::UILayoutStyle rootStyle = percentSize(100.0F, 100.0F);
    rootStyle.flexContainer.direction = UI::UIFlexDirection::Column;
    rootStyle.flexContainer.gap.row = 6.0F;
    rootStyle.padding = UI::UIEdgeSpacing::All(10.0F);
    if (auto status = tree->setLayoutStyle(root->rootNodeId(), rootStyle); !status) {
        return status;
    }

    const UI::UINodeId rootNode = root->rootNodeId();
    if (auto status = buildToolbarUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildContextBarUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildDocumentTabsUi(ui, rootNode); !status) {
        return status;
    }
    UI::UINodeId body{};
    UI::UILayoutStyle bodyStyle = growingRegion();
    bodyStyle.minMax.minHeight = UI::UILayoutLength::Px(320.0F);
    bodyStyle.flexContainer.direction = UI::UIFlexDirection::Row;
    bodyStyle.flexContainer.gap.column = 8.0F;
    if (auto status = storeNode(ui.createPanel(rootNode, bodyStyle, UI::UIStyleRoleId::None), body); !status) {
        return status;
    }

    if (auto status = buildLeftDockUi(ui, body); !status) {
        return status;
    }
    if (auto status = buildViewportUi(ui, body); !status) {
        return status;
    }
    if (auto status = buildInspectorUi(ui, body); !status) {
        return status;
    }
    if (auto status = buildTimelineUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildStatusBarUi(ui, rootNode); !status) {
        return status;
    }
    if (auto status = buildDirtyCloseModalUi(ui, rootNode); !status) {
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

    uiRoot_ = std::move(*root);
    ++counters_.uiRootsCreated;
    assetRollback.release();
    return Tina::Core::success();
}

} // namespace Tina::EditorApp::WorkspaceInternal
