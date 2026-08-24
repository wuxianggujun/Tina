#include "EditorWorkspaceState.hpp"

#include <tina/asset/AssetTypedViews.hpp>

#include <cmath>

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

constexpr float ImportQueuedProgress = 0.05F;
constexpr float ImportPreparingProgress = 0.20F;
constexpr float ImportCopyingProgress = 0.50F;
constexpr float ImportCookingProgress = 0.75F;
constexpr float ImportReadyToCommitProgress = 1.0F;

[[nodiscard]] constexpr EditorIcon projectAssetIcon(
    Tina::AssetFormat::AssetKind kind) noexcept
{
    switch (kind) {
    case Tina::AssetFormat::AssetKind::Texture2D:
        return EditorIcon::Paint;
    case Tina::AssetFormat::AssetKind::Shader:
        return EditorIcon::Cook;
    case Tina::AssetFormat::AssetKind::Font:
        return EditorIcon::Apply;
    case Tina::AssetFormat::AssetKind::Sprite:
        return EditorIcon::Import;
    case Tina::AssetFormat::AssetKind::Tileset:
        return EditorIcon::Paint;
    case Tina::AssetFormat::AssetKind::TileMap:
        return EditorIcon::World;
    case Tina::AssetFormat::AssetKind::StaticMesh:
    case Tina::AssetFormat::AssetKind::SkinnedMesh:
        return EditorIcon::Root;
    case Tina::AssetFormat::AssetKind::Material:
        return EditorIcon::Apply;
    case Tina::AssetFormat::AssetKind::Prefab:
        return EditorIcon::Root;
    case Tina::AssetFormat::AssetKind::AudioClip:
        return EditorIcon::Play;
    case Tina::AssetFormat::AssetKind::SpriteAnimationClip:
        return EditorIcon::Play;
    case Tina::AssetFormat::AssetKind::TileMapChunk:
        return EditorIcon::Paint;
    case Tina::AssetFormat::AssetKind::EnvironmentMap:
        return EditorIcon::World;
    case Tina::AssetFormat::AssetKind::NavigationGrid2D:
        return EditorIcon::Snap;
    case Tina::AssetFormat::AssetKind::Fx2D:
        return EditorIcon::Paint;
    case Tina::AssetFormat::AssetKind::Invalid:
    default:
        return EditorIcon::Open;
    }
}

} // namespace

auto EditorWorkspaceState::updateProjectAssetSearch(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    auto text = tree.text(projectAssetSearchInput_);
    if (!text) {
        return Tina::Core::failure(std::move(text.error()));
    }
    if (*text == projectAssets_.searchQuery()) {
        return Tina::Core::success();
    }
    if (auto status = projectAssets_.setSearchQuery(*text); !status) {
        return status;
    }
    lastProjectAssetPointerDownAssetId_ = {};
    pendingProjectAssetOpen_.reset();
    observedProjectAssetSelectionIndex_.reset();
    if (auto status = tree.setVirtualGridViewDataSource(
            projectAssetList_, projectAssetDataSource());
        !status) {
        return status;
    }
    if (auto status = tree.invalidateVirtualGridViewItems(projectAssetList_); !status) {
        return status;
    }
    if (projectAssets_.visibleItemCount() != 0U) {
        projectAssetSelectionSyncPending_ = true;
        assetInspectorActive_ = true;
    } else {
        projectAssetSelectionSyncPending_ = false;
        assetInspectorActive_ = false;
        if (auto status = tree.clearVirtualGridViewSelection(projectAssetList_);
            !status) {
            return status;
        }
    }
    if (auto status = refreshProjectAssetUi(tree); !status) {
        return status;
    }
    return refreshAuthoringUi(tree);
}

auto EditorWorkspaceState::refreshProjectAssetUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    std::string count = std::to_string(projectAssets_.visibleItemCount());
    count += " / ";
    count += std::to_string(projectAssets_.itemCount());
    if (auto status = tree.setText(projectAssetCount_, count); !status) {
        return status;
    }
    std::string selectedAssetSummary;
    using ImportState =
        Tina::EditorApp::Detail::EditorSourceImportServiceState;
    const ImportState importState = sourceImportService_.state();
    const bool fileDropPending = !pendingFileDrops_.empty();
    const bool importPending =
        pendingProjectSwitch_.has_value() ||
        sourceImportCatalogCommitted_ ||
        projectBrowserUiRefreshPending_ ||
        previewAssetBindingsRefreshPending_ ||
        !pendingSourceImportPathsUtf8_.empty() ||
        sourceImportStartPending_ || retrySourceImportPending_ ||
        fileDropPending ||
        importState == ImportState::Running ||
        importState == ImportState::Ready;
    std::string_view importProgressSummary = "Preparing project import...";
    std::string_view importStatusLabel = "Preparing";
    float importProgressValue = ImportQueuedProgress;
    if (fileDropPending && importState == ImportState::Idle) {
        importProgressSummary = "File drop queued; waiting for the Editor to become idle...";
        importStatusLabel = "Drop queued";
    } else if (importState == ImportState::Ready) {
        importProgressSummary = "Import ready; committing Catalog...";
        importStatusLabel = "Committing";
        importProgressValue = ImportReadyToCommitProgress;
    } else if (importState == ImportState::Running) {
        using ImportPhase =
            Tina::EditorApp::Detail::EditorSourceImportPhase;
        switch (sourceImportService_.phase()) {
        case ImportPhase::Preparing:
            importProgressSummary = "Preparing resource batch...";
            importStatusLabel = "Preparing";
            importProgressValue = ImportPreparingProgress;
            break;
        case ImportPhase::Copying:
            importProgressSummary = "Copying resources in background...";
            importStatusLabel = "Copying";
            importProgressValue = ImportCopyingProgress;
            break;
        case ImportPhase::Cooking:
            importProgressSummary = "Cooking and validating resources...";
            importStatusLabel = "Cooking";
            importProgressValue = ImportCookingProgress;
            break;
        case ImportPhase::ReadyToCommit:
            importProgressSummary = "Import ready; committing Catalog...";
            importStatusLabel = "Committing";
            importProgressValue = ImportReadyToCommitProgress;
            break;
        case ImportPhase::Idle:
        case ImportPhase::Failed:
            importProgressSummary = "Importing resources...";
            importStatusLabel = "Importing";
            break;
        }
    }
    if (importPending) {
        selectedAssetSummary = importProgressSummary;
    } else if (sourceImportLastFailed_) {
        selectedAssetSummary = "Import failed";
        if (!sourceImportFailureMessageUtf8_.empty()) {
            selectedAssetSummary += ": ";
            selectedAssetSummary += sourceImportFailureMessageUtf8_;
        }
        selectedAssetSummary += ". Previous Catalog preserved.";
    } else if (projectAssets_.selectedAssetId().has_value() &&
               projectAssets_.selectedItem() == nullptr) {
        selectedAssetSummary =
            "Selected asset is hidden by current search/filter";
    } else if (projectAssets_.visibleItemCount() == 0U) {
        if (!projectAssets_.searchQuery().empty()) {
            selectedAssetSummary = "No assets match this search";
        } else if (projectAssets_.itemCount() == 0U &&
                   (assetResources_.projectCatalogConfigured ||
                    assetResources_.testFixtureCatalog)) {
            selectedAssetSummary = "Catalog is empty; import files to add resources";
        } else if (assetResources_.projectCatalogConfigured ||
                   assetResources_.testFixtureCatalog) {
            selectedAssetSummary = "No assets match this filter";
        } else {
            selectedAssetSummary = "No project open";
        }
    } else if (const auto* asset = projectAssets_.selectedItem(); asset != nullptr) {
        selectedAssetSummary = asset->displayName;
        selectedAssetSummary += "  |  ";
        selectedAssetSummary += Tina::Editor::projectAssetKindLabel(asset->assetKind);
        selectedAssetSummary += "  |  Ready  |  ";
        const auto idText = asset->assetId.canonicalText();
        selectedAssetSummary.append(idText.data(), idText.size());
    } else {
        selectedAssetSummary = "Select an asset to inspect it";
    }
    if (auto status = tree.setText(projectAssetSummary_, selectedAssetSummary);
        !status) {
        return status;
    }
    if (auto status = tree.setText(
            projectAssetSource_,
            importPending
                ? importStatusLabel
                : sourceImportLastFailed_
                      ? "Error"
                      : temporaryProjectActive()
                ? "Temp"
                : assetResources_.projectCatalogConfigured
                ? "Project"
                : (assetResources_.testFixtureCatalog ? "Test Data" : "No Project"));
        !status) {
        return status;
    }
    if (auto status = tree.setText(
            sourceImportCount_, std::to_string(sourceImportUnits_.size()));
        !status) {
        return status;
    }
    const std::string_view projectBreadcrumb =
        temporaryProjectActive()
            ? "Temporary Project / Assets"
            : assetResources_.projectCatalogConfigured
                  ? "Project / Assets"
                  : assetResources_.testFixtureCatalog ? "Test Data / Assets"
                                                       : "Project / Assets";
    if (auto status = tree.setText(projectAssetBreadcrumb_, projectBreadcrumb);
        !status) {
        return status;
    }
    if (auto status = tree.setText(
            projectAssetDropHint_,
            activeProjectWorkspace_.has_value()
                ? "Drop files here or onto the viewport to import resources"
                : "Open or create a project before dropping files");
        !status) {
        return status;
    }
    // Auto-demo uses an in-memory fixture Catalog without a project workspace;
    // keep its authored asset surface available while real sessions use the
    // Start Center until a project is opened or created.
    const bool hasActiveProject = activeProjectWorkspace_.has_value();
    const bool showProjectAssets = hasActiveProject || assetResources_.testFixtureCatalog;
    projectAssetStartCenterLayout_.visibility =
        showProjectAssets ? UI::UIVisibility::Collapsed : UI::UIVisibility::Visible;
    if (auto status = tree.setLayoutStyle(
            projectAssetStartCenter_, projectAssetStartCenterLayout_);
        !status) {
        return status;
    }
    if (auto status = refreshRecentProjectsUi(tree); !status) {
        return status;
    }
    projectAssetListLayout_.visibility =
        showProjectAssets ? UI::UIVisibility::Visible : UI::UIVisibility::Collapsed;
    if (auto status = tree.setLayoutStyle(
            projectAssetList_, projectAssetListLayout_);
        !status) {
        return status;
    }
    projectAssetEmptyStateLayout_.visibility =
        showProjectAssets && projectAssets_.itemCount() == 0U && !importPending &&
                !sourceImportLastFailed_
            ? UI::UIVisibility::Visible
            : UI::UIVisibility::Collapsed;
    if (auto status = tree.setLayoutStyle(
            projectAssetEmptyState_, projectAssetEmptyStateLayout_);
        !status) {
        return status;
    }
    const std::string_view emptyStateText = activeProjectWorkspace_.has_value()
                                                ? "Catalog is empty. Import files to add resources."
                                                : "Open or create a project to import resources.";
    if (auto status = tree.setText(projectAssetEmptyStateText_, emptyStateText);
        !status) {
        return status;
    }
    projectAssetActivityLayout_.visibility =
        importPending ? UI::UIVisibility::Visible : UI::UIVisibility::Collapsed;
    if (auto status = tree.setLayoutStyle(
            projectAssetActivity_, projectAssetActivityLayout_);
        !status) {
        return status;
    }
    if (auto status = tree.setText(projectAssetActivityText_, importProgressSummary);
        !status) {
        return status;
    }
    if (projectAssetActivityProgress_.hasValue()) {
        if (auto status = tree.setProgressBarRange(
                projectAssetActivityProgress_, 0.0F, 1.0F);
            !status) {
            return status;
        }
        if (auto status = tree.setProgressBarValue(
                projectAssetActivityProgress_, importProgressValue);
            !status) {
            return status;
        }
    }
    if (auto status = tree.setEnabled(
            cancelSourceImportButton_,
            sourceImportService_.state() ==
                    Tina::EditorApp::Detail::EditorSourceImportServiceState::Running &&
                !cancelSourceImportPending_);
        !status) {
        return status;
    }
    std::string importFailureText = "Import failed";
    if (!sourceImportFailureMessageUtf8_.empty()) {
        importFailureText += ": ";
        importFailureText += sourceImportFailureMessageUtf8_;
    }
    importFailureText += ". Previous Catalog preserved.";
    projectAssetImportCalloutLayout_.visibility =
        sourceImportLastFailed_ && !importPending ? UI::UIVisibility::Visible
                                                  : UI::UIVisibility::Collapsed;
    if (auto status = tree.setLayoutStyle(
            projectAssetImportCallout_, projectAssetImportCalloutLayout_);
        !status) {
        return status;
    }
    if (auto status = tree.setText(
            projectAssetImportFailureText_, importFailureText);
        !status) {
        return status;
    }
    sourceImportSectionLayout_.visibility = sourceImportUnits_.empty()
                                                ? UI::UIVisibility::Collapsed
                                                : UI::UIVisibility::Visible;
    if (auto status = tree.setLayoutStyle(
            sourceImportSection_, sourceImportSectionLayout_);
        !status) {
        return status;
    }
    const u32 activeFilter = static_cast<u32>(projectAssets_.filter());
    for (u32 index = 0; index < projectFilterButtons_.size(); ++index) {
        if (auto status = tree.setRadioButtonSelected(projectFilterButtons_[index], index == activeFilter);
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(projectFilterButtons_[index], true); !status) {
            return status;
        }
    }
    if (auto status = tree.setEnabled(openProjectAssetButton_,
                                      projectAssets_.selectedItem() != nullptr);
        !status) {
        return status;
    }
    const bool sourceImportIdle =
        sourceImportService_.state() ==
        Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle;
    const bool sourceImportAvailable =
        !pendingProjectSwitch_.has_value() && !catalogRefreshPending_ &&
        !sourceImportCatalogCommitted_ && !projectBrowserUiRefreshPending_ &&
        !previewAssetBindingsRefreshPending_ &&
        pendingSourceImportPathsUtf8_.empty() &&
        !sourceImportStartPending_ && !retrySourceImportPending_ && sourceImportIdle;
    const bool sourceImportEditable =
        activeProjectWorkspace_.has_value() && sourceImportAvailable;
    if (auto status = tree.setEnabled(sourceImportGrid_, sourceImportEditable);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            removeSourceImportButton_,
            sourceImportEditable &&
                observedSourceImportSelectionIndex_.has_value() &&
                *observedSourceImportSelectionIndex_ < sourceImportUnits_.size());
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(refreshProjectCatalogButton_,
                                      assetResources_.projectCatalogConfigured &&
                                          !catalogRefreshPending_ &&
                                          !pendingProjectSwitch_.has_value() &&
                                          sourceImportIdle);
        !status) {
        return status;
    }
    const bool projectLifecycleAvailable =
        authoringEnabled() && !pendingProjectSwitch_.has_value() &&
        sourceImportIdle;
    if (auto status = tree.setEnabled(fileCreateProjectMenuItem_,
                                      projectLifecycleAvailable);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(fileOpenProjectMenuItem_,
                                      projectLifecycleAvailable);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(projectAssetStartCenterNewButton_,
                                      projectLifecycleAvailable);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(projectAssetStartCenterOpenButton_,
                                      projectLifecycleAvailable);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(importSourceButton_,
                                      sourceImportAvailable);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(projectAssetStartCenterImportButton_,
                                      sourceImportAvailable);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(projectAssetEmptyStateImportButton_,
                                      sourceImportAvailable);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            retrySourceImportButton_,
            activeProjectWorkspace_.has_value() && sourceImportLastFailed_ &&
                (!sourceImportRetryUnits_.empty() ||
                 !sourceImportRetryPathsUtf8_.empty()) &&
                sourceImportAvailable);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            openImportOutputButton_, sourceImportLastFailed_ && !importPending);
        !status) {
        return status;
    }
    counters_.projectAssetVisibleItems = projectAssets_.visibleItemCount();
    return Tina::Core::success();
}

auto EditorWorkspaceState::refreshRecentProjectsUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    for (u32 index = 0; index < RecentProjectCapacity; ++index) {
        UI::UILayoutStyle layout{};
        layout.size.width = UI::UILayoutLength::Percent(100.0F);
        layout.size.height = UI::UILayoutLength::Px(28.0F);
        layout.visibility = index < editorSettings_.recentProjectCount
            ? UI::UIVisibility::Visible : UI::UIVisibility::Collapsed;
        if (auto status = tree.setLayoutStyle(recentProjectButtons_[index], layout); !status) {
            return status;
        }
        if (index < editorSettings_.recentProjectCount) {
            if (auto status = tree.setText(
                    recentProjectButtons_[index], editorSettings_.recentProjects[index]); !status) {
                return status;
            }
        }
        if (auto status = tree.setEnabled(
                recentProjectMenuItems_[index], index < editorSettings_.recentProjectCount); !status) {
            return status;
        }
        if (index < editorSettings_.recentProjectCount) {
            if (auto status = tree.setText(
                    recentProjectMenuItems_[index], editorSettings_.recentProjects[index]); !status) {
                return status;
            }
        }
    }
    return Tina::Core::success();
}

void EditorWorkspaceState::rememberRecentProject(std::string_view projectRootUtf8) noexcept
{
    if (projectRootUtf8.empty() || !Tina::Core::isStrictUtf8WithoutNul(projectRootUtf8)) {
        return;
    }
    try {
        u32 existing = editorSettings_.recentProjectCount;
        for (u32 index = 0; index < existing; ++index) {
            if (editorSettings_.recentProjects[index] == projectRootUtf8) {
                existing = index;
                break;
            }
        }
        const u32 limit = (std::min)(editorSettings_.recentProjectCount, RecentProjectCapacity - 1U);
        const u32 shiftStart = existing < editorSettings_.recentProjectCount ? existing : limit;
        for (u32 index = shiftStart; index > 0U; --index) {
            editorSettings_.recentProjects[index] = std::move(editorSettings_.recentProjects[index - 1U]);
        }
        editorSettings_.recentProjects[0] = std::string(projectRootUtf8);
        editorSettings_.recentProjectCount = (std::min)(limit + 1U, RecentProjectCapacity);
        (void)saveEditorSettings(editorSettings_);
    } catch (...) {
    }
}

auto EditorWorkspaceState::processPendingRecentProject() -> Tina::Core::Status
{
    if (!pendingRecentProjectIndex_.has_value()) {
        return Tina::Core::success();
    }
    const u32 index = *pendingRecentProjectIndex_;
    pendingRecentProjectIndex_.reset();
    if (index >= editorSettings_.recentProjectCount) {
        return Tina::Core::success();
    }
    auto workspace = openExistingEditorProjectWorkspace(editorSettings_.recentProjects[index]);
    if (!workspace) {
        editorSettings_.recentProjects[index].clear();
        for (u32 cursor = index; cursor + 1U < editorSettings_.recentProjectCount; ++cursor) {
            editorSettings_.recentProjects[cursor] = std::move(editorSettings_.recentProjects[cursor + 1U]);
        }
        if (editorSettings_.recentProjectCount > 0U) --editorSettings_.recentProjectCount;
        (void)saveEditorSettings(editorSettings_);
        return reportAuthoringFailure("Recent project is unavailable: ", workspace.error());
    }
    pendingProjectSwitch_ = std::move(*workspace);
    projectSwitchBlockedByDirty_ = false;
    authoringFeedback_ = "Recent project open scheduled";
    return Tina::Core::success();
}

auto EditorWorkspaceState::prepareProjectBrowserForSnapshot(
    const Tina::Asset::CatalogSnapshot& catalog,
    Tina::Editor::ProjectAssetFilter filter,
    std::optional<Tina::Core::AssetId> selectedAsset,
    std::string_view searchQuery,
    std::span<const Tina::Asset::SourceImportPipelineUnitOutput> sourceMappings,
    std::span<const EditorAssetMetadataRecord> metadata,
    std::string_view sourceRootUtf8)
    -> Tina::Core::Result<Tina::Editor::ProjectAssetBrowserModel>{
    if (sourceRootUtf8.empty() && activeProjectWorkspace_.has_value()) {
        sourceRootUtf8 = activeProjectWorkspace_->sourceRootUtf8();
    }
    auto browser = createProjectAssetBrowser(
        catalog, sourceMappings, metadata, sourceRootUtf8);
    if (!browser) {
        return Tina::Core::failure(std::move(browser.error()));
    }
    if (auto status = browser->setFilter(filter); !status) {
        return Tina::Core::failure(std::move(status.error()));
    }
    if (auto status = browser->setSearchQuery(searchQuery); !status) {
        return Tina::Core::failure(std::move(status.error()));
    }
    bool restoredSelection = false;
    if (selectedAsset.has_value()) {
        restoredSelection = static_cast<bool>(browser->restoreAssetSelection(*selectedAsset));
    }
    if (!restoredSelection && browser->visibleItemCount() != 0U) {
        if (auto status = browser->selectVisibleIndex(0U); !status) {
            return Tina::Core::failure(std::move(status.error()));
        }
    }
    return std::move(*browser);
}

auto EditorWorkspaceState::prepareProjectSwitchDocumentTabs() -> Tina::Core::Result<Tina::Editor::EditorDocumentTabs>{
    if (auto status = synchronizeActiveTabDirty(); !status) {
        return Tina::Core::failure(std::move(status.error()));
    }

    std::vector<Tina::Editor::EditorDocumentTabDesc> retainedTabs;
    try {
        retainedTabs.reserve(documentTabs_.tabCount());
        for (Tina::Core::usize index = 0; index < documentTabs_.tabCount(); ++index) {
            const auto* tab = documentTabs_.tab(index);
            if (tab == nullptr) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "Editor document tab disappeared during project switch staging");
            }
            if (tab->key.assetId && tab->dirty) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::DirtyDocumentRequiresConfirmation,
                    "Save or discard modified Catalog documents before switching projects");
            }
            if (tab->pinned || !tab->key.assetId) {
                retainedTabs.push_back(*tab);
            }
        }
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Editor project switch tab staging allocation failed");
    }

    auto candidate = Tina::Editor::EditorDocumentTabs::Create(
        retainedTabs, documentTabs_.config());
    if (!candidate) {
        return Tina::Core::failure(std::move(candidate.error()));
    }
    const Tina::Editor::EditorDocumentKey fallbackKey{
        .kind = workspaceMode_ == WorkspaceMode::World2D
                    ? Tina::Editor::EditorDocumentKind::World2D
                    : Tina::Editor::EditorDocumentKind::World3D,
    };
    const auto fallback = candidate->find(fallbackKey);
    if (!fallback.has_value()) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Editor project switch has no workspace document fallback");
    }
    if (auto status = candidate->activate(*fallback); !status) {
        return Tina::Core::failure(std::move(status.error()));
    }
    return std::move(*candidate);
}

auto EditorWorkspaceState::switchCatalogAuthoringOwnersToPinnedTabs() -> Tina::Core::Status{
    constexpr std::array catalogDocumentKinds{
        Tina::Editor::EditorDocumentKind::World3D,
        Tina::Editor::EditorDocumentKind::TileMap2D,
        Tina::Editor::EditorDocumentKind::SpriteAnimation2D,
    };
    for (const auto kind : catalogDocumentKinds) {
        auto* owner = activeAuthoringDocumentOwner(kind);
        if (owner == nullptr) {
            continue;
        }
        const Tina::Editor::EditorDocumentTabDesc* pinned = nullptr;
        for (Tina::Core::usize index = 0; index < documentTabs_.tabCount(); ++index) {
            const auto* candidate = documentTabs_.tab(index);
            if (candidate != nullptr && candidate->pinned &&
                candidate->key.kind == kind) {
                pinned = candidate;
                break;
            }
        }
        if (pinned == nullptr) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Editor project switch has no pinned authoring document owner");
        }
        if (*owner != pinned->key) {
            if (auto status = switchActiveAuthoringDocument(pinned->key); !status) {
                return status;
            }
        }
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::refreshPinnedCatalogAuthoringDocuments(
    const Tina::Editor::ProjectAssetBrowserModel& browser) -> Tina::Core::Status{
    constexpr std::array reloadableKinds{
        Tina::Editor::EditorDocumentKind::TileMap2D,
        Tina::Editor::EditorDocumentKind::SpriteAnimation2D,
    };
    for (const auto kind : reloadableKinds) {
        const Tina::Editor::EditorDocumentTabDesc* pinned = nullptr;
        for (Tina::Core::usize index = 0; index < documentTabs_.tabCount(); ++index) {
            const auto* candidate = documentTabs_.tab(index);
            if (candidate != nullptr && candidate->pinned &&
                candidate->key.kind == kind) {
                pinned = candidate;
                break;
            }
        }
        if (pinned == nullptr || !pinned->key.assetId) {
            continue;
        }
        const auto* asset = browser.inspectorSnapshot(pinned->key.assetId);
        if (asset == nullptr ||
            Tina::Editor::editorDocumentKindForAsset(asset->assetKind) != kind) {
            continue;
        }
        auto loaded = loadProjectAssetDocument(*asset);
        if (!loaded) {
            return Tina::Core::failure(std::move(loaded.error()));
        }
        if (!loaded->document.has_value()) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Pinned Catalog authoring asset did not produce a document");
        }
        if (kind == Tina::Editor::EditorDocumentKind::TileMap2D) {
            auto* document = std::get_if<Tina::Editor::TileMapAuthoringDocument>(
                &*loaded->document);
            if (document == nullptr) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "Pinned TileMap Catalog asset produced the wrong document kind");
            }
            tileMapDocument_ = std::move(*document);
        } else {
            auto* document =
                std::get_if<Tina::Editor::SpriteAnimationAuthoringDocument>(
                    &*loaded->document);
            if (document == nullptr) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "Pinned animation Catalog asset produced the wrong document kind");
            }
            spriteAnimationDocument_ = std::move(*document);
        }
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::commitProjectSwitchDocumentTabs(
    Tina::Editor::EditorDocumentTabs candidateTabs) noexcept -> void{
    documentTabs_ = std::move(candidateTabs);
    documentSessions_.resetTabState();
    pendingDirtyCloseKey_.reset();
    assetInspectorActive_ = false;
    synchronizeViewportSelectionFromHierarchy();
    counters_.documentTabCount = documentTabs_.tabCount();
}

auto EditorWorkspaceState::rebuildLiveCatalogPreview(
    std::string successFeedback) -> Tina::Core::Status{
    if (previewAssetBindingsHaveActiveFrameBorrows()) {
        return Tina::Core::failure(
            Tina::Asset::AssetErrorCode::CatalogReloadBusy,
            "Catalog preview rebuild is waiting for active frame resources");
    }
    counters_.runtimePreviewValid = false;
    counters_.catalogReady = false;
    animationPreview_.resetAnimator();
    if (auto status = releasePreviewAssetBindingsDraining(); !status) {
        return status;
    }
    if (auto status = preparePreviewAssetBindings(); !status) {
        auto error = std::move(status.error());
        (void)releasePreviewAssetBindingsDraining();
        if (auto feedback = reportAuthoringFailure(
                "Catalog committed, but runtime preview binding rebuild failed: ",
                error);
            !feedback) {
            return feedback;
        }
        return Tina::Core::failure(std::move(error));
    }
    if (auto status = rebuildAnimationAnimator(); !status) {
        auto error = std::move(status.error());
        (void)releasePreviewAssetBindingsDraining();
        counters_.runtimePreviewValid = false;
        if (auto feedback = reportAuthoringFailure(
                "Catalog committed, but animation preview rebuild failed: ",
                error);
            !feedback) {
            return feedback;
        }
        return Tina::Core::failure(std::move(error));
    }
    if (auto status = validateRuntimePreview(); !status) {
        auto error = std::move(status.error());
        (void)releasePreviewAssetBindingsDraining();
        counters_.runtimePreviewValid = false;
        if (auto feedback = reportAuthoringFailure(
                "Catalog committed, but runtime preview validation failed: ",
                error);
            !feedback) {
            return feedback;
        }
        return Tina::Core::failure(std::move(error));
    }
    ++counters_.previewAssetBindingRefreshes;
    authoringFeedback_.swap(successFeedback);
    return Tina::Core::success();
}

auto EditorWorkspaceState::switchLiveProjectCatalog(
    Tina::Editor::EditorProjectWorkspace& workspace) -> Tina::Core::Result<bool>{
    if (!assetResources_.system.has_value()) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Project switch requires the Editor AssetSystem");
    }

    auto candidateTabs = prepareProjectSwitchDocumentTabs();
    if (!candidateTabs) {
        if (candidateTabs.error().code ==
            Tina::Editor::EditorErrorCode::DirtyDocumentRequiresConfirmation) {
            projectSwitchBlockedByDirty_ = true;
            if (auto status = reportAuthoringFailure(
                "Project switch blocked; previous Catalog preserved: ",
                candidateTabs.error()); !status) {
                return Tina::Core::failure(std::move(status.error()));
            }
            return false;
        }
        return Tina::Core::failure(std::move(candidateTabs.error()));
    }

    std::optional<Tina::Core::AssetId> previousSelection{};
    previousSelection = projectAssets_.selectedAssetId();
    const Tina::Editor::ProjectAssetFilter previousFilter = projectAssets_.filter();
    const std::string_view previousSearchQuery = projectAssets_.searchQuery();

    if (previewAssetBindingsHaveActiveFrameBorrows()) {
        authoringFeedback_ =
            "Project switch is waiting for the previous preview frame to finish";
        return false;
    }

    auto resolvedCatalog = resolveProjectCatalog(workspace);
    if (!resolvedCatalog) {
        if (auto status = reportAuthoringFailure(
                "Project switch could not resolve its active Catalog: ",
                resolvedCatalog.error()); !status) {
            return Tina::Core::failure(std::move(status.error()));
        }
        return true;
    }
    std::string nextCatalogRoot;
    std::string nextSourceImportCatalogRoot;
    std::string nextSourceImportStatePath;
    std::string nextAuthoringCatalogRoot;
    std::string successFeedback;
    try {
        nextCatalogRoot = resolvedCatalog->catalogRootUtf8;
        nextSourceImportCatalogRoot = resolvedCatalog->sourceImportCatalogRootUtf8;
        nextSourceImportStatePath = resolvedCatalog->sourceImportStatePathUtf8;
        nextAuthoringCatalogRoot = resolvedCatalog->authoringCatalogRootUtf8;
        successFeedback = "Project switched: ";
        successFeedback += workspace.projectRootUtf8();
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Project switch path staging allocation failed");
    }

    Tina::Asset::Sprite2DBindingRegistry* spriteParticipant =
        spriteBindings_.has_value() ? &*spriteBindings_ : nullptr;
    Tina::Asset::Mesh3DBindingRegistry* meshParticipant =
        mesh3DBindings_.has_value() ? &*mesh3DBindings_ : nullptr;
    Tina::Asset::CatalogReloadConfig reloadConfig{};
    reloadConfig.package.manifest.catalog.maxEntries = 1024;
    reloadConfig.package.manifest.catalog.maxDependencies = 4096;
    reloadConfig.package.manifest.catalog.maxDependenciesPerAsset = 64;
    reloadConfig.package.validation.verifyTypedPayload = true;
    if (spriteParticipant != nullptr) {
        reloadConfig.bindings.sprite2D =
            std::span<Tina::Asset::Sprite2DBindingRegistry*>{&spriteParticipant, 1U};
    }
    if (meshParticipant != nullptr) {
        reloadConfig.bindings.mesh3D =
            std::span<Tina::Asset::Mesh3DBindingRegistry*>{&meshParticipant, 1U};
    }
    auto reload = assetResources_.system->reloadCatalog(
        resolvedCatalog->catalogRootUtf8, reloadConfig);
    if (!reload) {
        if (reload.error().code == Tina::Asset::AssetErrorCode::CatalogReloadBusy) {
            authoringFeedback_ =
                "Project switch is waiting for the previous preview frame to finish";
            return false;
        }
        if (auto status = reportAuthoringFailure(
                "Project switch failed; previous Catalog preserved: ", reload.error());
            !status) {
            return Tina::Core::failure(std::move(status.error()));
        }
        return true;
    }

    const Tina::Asset::CatalogSnapshot* committedCatalog =
        assetResources_.system->catalog();
    if (committedCatalog == nullptr) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Project switch committed without an AssetSystem Catalog snapshot");
    }
    auto candidateBrowser = prepareProjectBrowserForSnapshot(
        *committedCatalog, previousFilter, previousSelection, previousSearchQuery,
        resolvedCatalog->sourceImportUnitOutputs, resolvedCatalog->assetMetadata,
        workspace.sourceRootUtf8());
    if (!candidateBrowser) {
        auto error = std::move(candidateBrowser.error());
        if (auto feedback = reportAuthoringFailure(
                "Project Catalog committed, but Browser rebuild failed: ", error);
            !feedback) {
            return Tina::Core::failure(std::move(feedback.error()));
        }
        return Tina::Core::failure(std::move(error));
    }
    if (auto status = switchCatalogAuthoringOwnersToPinnedTabs(); !status) {
        return Tina::Core::failure(std::move(status.error()));
    }
    if (auto status = refreshPinnedCatalogAuthoringDocuments(*candidateBrowser);
        !status) {
        return Tina::Core::failure(std::move(status.error()));
    }

    assetResources_.catalogRootUtf8.swap(nextCatalogRoot);
    assetResources_.sourceImportCatalogRootUtf8.swap(nextSourceImportCatalogRoot);
    assetResources_.sourceImportStatePathUtf8.swap(nextSourceImportStatePath);
    assetResources_.authoringCatalogRootUtf8.swap(nextAuthoringCatalogRoot);
    assetResources_.catalogEntryCount =
        static_cast<u32>(candidateBrowser->itemCount());
    assetResources_.projectCatalogConfigured = true;
    assetResources_.testFixtureCatalog = false;
    counters_.catalogEntryCount = assetResources_.catalogEntryCount;
    counters_.projectCatalogConfigured = true;
    counters_.testFixtureCatalog = false;
    if (auto status = rebuildLiveCatalogPreview(std::move(successFeedback)); !status) {
        return Tina::Core::failure(std::move(status.error()));
    }

    projectAssets_ = std::move(*candidateBrowser);
    lastProjectAssetPointerDownAssetId_ = {};
    pendingProjectAssetOpen_.reset();
    projectAssetContextAssetId_ = {};
    projectAssetDragAssetId_ = {};
    pendingProjectAssetDrop_.reset();
    commitProjectSwitchDocumentTabs(std::move(*candidateTabs));
    activeProjectWorkspace_ = std::move(workspace);
    rememberRecentProject(activeProjectWorkspace_->projectRootUtf8());
    sourceImportUnits_ = std::move(resolvedCatalog->sourceImportUnits);
    sourceImportUnitOutputs_ = std::move(resolvedCatalog->sourceImportUnitOutputs);
    assetMetadata_ = std::move(resolvedCatalog->assetMetadata);
    clearAssetImportHistory();
    sourceImportRetryUnits_.clear();
    sourceImportRetryPathsUtf8_.clear();
    sourceImportLastFailed_ = false;
    sourceImportFailureMessageUtf8_.clear();
    counters_.sourceImportIntendedUnits = sourceImportUnits_.size();
    observedSourceImportSelectionIndex_.reset();
    sourceImportUiRefreshPending_ = true;
    sourceImportPointerPathUtf8_.clear();
    sourceImportPendingStageRootUtf8_.clear();
    sourceImportSupersededCatalogRootUtf8_.clear();
    sourceImportSupersededAuthoringCatalogRootUtf8_.clear();
    sourceImportCatalogCommitted_ = false;
    projectSwitchBlockedByDirty_ = false;
    ++counters_.projectSwitches;
    observedProjectAssetSelectionIndex_.reset();
    previewAssetBindingsRefreshPending_ = false;
    projectBrowserUiRefreshPending_ = true;
    if (!temporaryProjectSaveTargetRootUtf8_.empty() &&
        activeProjectWorkspace_->projectRootUtf8() ==
            temporaryProjectSaveTargetRootUtf8_) {
        pendingTemporaryProjectCleanupRootUtf8_ =
            std::move(temporaryProjectRootUtf8_);
        temporaryProjectSaveTargetRootUtf8_.clear();
        if (pendingSourceImportPathsUtf8_.empty()) {
            cleanupOwnedTemporaryProject(
                pendingTemporaryProjectCleanupRootUtf8_);
            authoringFeedback_ = "Temporary project saved to its permanent location";
        }
    }
    return true;
}

auto EditorWorkspaceState::initializeNewProjectAt(
    std::string_view projectRootUtf8)
    -> Tina::Core::Result<Tina::Editor::EditorProjectWorkspace>
{
    auto workspace = Tina::Editor::CreateNewEditorProject({
        .projectRootUtf8 = projectRootUtf8,
        .targetPlatform = editorTargetPlatform(),
    });
    if (!workspace) {
        return Tina::Core::failure(std::move(workspace.error()));
    }

    if (auto status = publishEmptyEditorCatalog(
            workspace->cookedCatalogRootUtf8(), workspace->targetPlatform());
        !status) {
        return Tina::Core::failure(std::move(status.error()));
    }
    std::pmr::unsynchronized_pool_resource validationMemory;
    Tina::Asset::CatalogPackageOpenConfig openConfig{};
    openConfig.manifest.catalog = {
        .maxEntries = 1,
        .maxDependencies = 0,
        .maxDependenciesPerAsset = 0,
        .memoryResource = &validationMemory,
    };
    openConfig.validation.verifyTypedPayload = true;
    auto validatedCatalog = Tina::Asset::openCatalogPackage(
        workspace->cookedCatalogRootUtf8(), openConfig);
    if (!validatedCatalog) {
        return Tina::Core::failure(std::move(validatedCatalog.error()));
    }
    return std::move(*workspace);
}

auto EditorWorkspaceState::scheduleNewProjectAt(
    std::string_view projectRootUtf8,
    std::vector<std::string> pendingSourceImportPathsUtf8) -> Tina::Core::Status
{
    auto workspace = initializeNewProjectAt(projectRootUtf8);
    if (!workspace) {
        return reportAuthoringFailure("Project creation failed: ", workspace.error());
    }

    try {
        std::string feedback = pendingSourceImportPathsUtf8.empty()
                                   ? "Project created; live Catalog switch scheduled: "
                                   : "Project created; selected files will import after the Catalog switch: ";
        feedback += workspace->projectRootUtf8();
        pendingSourceImportPathsUtf8_ = std::move(pendingSourceImportPathsUtf8);
        pendingProjectSwitch_ = std::move(*workspace);
        projectSwitchBlockedByDirty_ = false;
        authoringFeedback_.swap(feedback);
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Project creation success message allocation failed");
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::createTemporaryProjectForImport(
    std::vector<std::string> pendingSourceImportPathsUtf8) -> Tina::Core::Status
{
    auto projectRoot = createUniqueEditorTempDirectory("tina_editor_project_");
    if (!projectRoot) {
        return reportAuthoringFailure(
            "Temporary project creation failed: ", projectRoot.error());
    }

    try {
        temporaryProjectRootUtf8_ = pathToUtf8(*projectRoot);
    } catch (const std::bad_alloc&) {
        std::error_code cleanupError;
        (void)std::filesystem::remove_all(*projectRoot, cleanupError);
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Temporary project path allocation failed");
    }

    auto status = scheduleNewProjectAt(
        temporaryProjectRootUtf8_, std::move(pendingSourceImportPathsUtf8));
    if (!status || !pendingProjectSwitch_.has_value()) {
        cleanupOwnedTemporaryProject(temporaryProjectRootUtf8_);
        return status;
    }
    authoringFeedback_ =
        "Temporary project created; selected files will import without choosing a project folder";
    return Tina::Core::success();
}

auto EditorWorkspaceState::saveTemporaryProjectFromDialog() -> Tina::Core::Status
{
    using ImportState =
        Tina::EditorApp::Detail::EditorSourceImportServiceState;
    if (!temporaryProjectActive()) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "No temporary Editor project is active");
    }
    if (pendingProjectSwitch_.has_value() ||
        !pendingSourceImportPathsUtf8_.empty() ||
        sourceImportService_.state() != ImportState::Idle) {
        authoringFeedback_ =
            "Wait for the current resource import to finish before saving the project";
        return Tina::Core::success();
    }

    auto selected = fileDialog_.pickFolder({
        .titleUtf8 = "Save Tina Project in Empty Folder",
    });
    if (!selected) {
        if (selected.error().code == Tina::Core::CoreErrorCode::Unsupported) {
            authoringFeedback_ =
                "Native project folder selection is unavailable on this platform";
            return Tina::Core::success();
        }
        return Tina::Core::failure(std::move(selected.error()));
    }
    if (!selected->selected()) {
        authoringFeedback_ = "Project save cancelled; temporary project preserved";
        return Tina::Core::success();
    }

    auto workspace = initializeNewProjectAt(selected->selectedPathUtf8);
    if (!workspace) {
        return reportAuthoringFailure(
            "Project save failed; temporary project preserved: ",
            workspace.error());
    }

    try {
        std::vector<std::string> temporarySourcePaths;
        temporarySourcePaths.reserve(sourceImportUnits_.size());
        for (const auto& unit : sourceImportUnits_) {
            temporarySourcePaths.push_back(unit.sourcePathUtf8);
        }

        std::string targetRoot{workspace->projectRootUtf8()};
        std::string feedback =
            temporarySourcePaths.empty()
                ? "Temporary project save scheduled: "
                : "Temporary project save scheduled; resources will migrate in the background at: ";
        feedback += targetRoot;

        pendingSourceImportPathsUtf8_ = std::move(temporarySourcePaths);
        temporaryProjectSaveTargetRootUtf8_ = std::move(targetRoot);
        pendingProjectSwitch_ = std::move(*workspace);
        projectSwitchBlockedByDirty_ = false;
        authoringFeedback_.swap(feedback);
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Temporary project save staging allocation failed");
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::temporaryProjectActive() const noexcept -> bool
{
    return !temporaryProjectRootUtf8_.empty() &&
           activeProjectWorkspace_.has_value() &&
           activeProjectWorkspace_->projectRootUtf8() == temporaryProjectRootUtf8_;
}

auto EditorWorkspaceState::cleanupOwnedTemporaryProject(
    std::string& projectRootUtf8) noexcept -> void
{
    if (projectRootUtf8.empty()) {
        return;
    }
    try {
        std::error_code tempError;
        const auto tempRoot = std::filesystem::temp_directory_path(tempError);
        const auto projectRoot = std::filesystem::u8path(
            projectRootUtf8.begin(), projectRootUtf8.end()).lexically_normal();
        const std::string fileName = pathToUtf8(projectRoot.filename());
        if (tempError ||
            !pathsReferToSameLocation(projectRoot.parent_path(),
                                      tempRoot.lexically_normal()) ||
            !fileName.starts_with("tina_editor_project_")) {
            return;
        }

        std::error_code cleanupError;
        if (validatePhysicalProjectDirectory(projectRoot, "temporaryProjectRoot")) {
            (void)std::filesystem::remove_all(projectRoot, cleanupError);
        } else {
            (void)std::filesystem::remove(projectRoot, cleanupError);
        }
        if (!cleanupError) {
            projectRootUtf8.clear();
        }
    } catch (...) {
    }
}

auto EditorWorkspaceState::createNewProjectFromDialog() -> Tina::Core::Status{
    if (temporaryProjectActive()) {
        authoringFeedback_ =
            "Save the temporary project before creating another project";
        return Tina::Core::success();
    }
    auto location = makeSaveDialogLocation(
        assetResources_.catalogRootUtf8, {}, true);
    if (!location) {
        return Tina::Core::failure(std::move(location.error()));
    }
    auto selected = fileDialog_.pickFolder({
        .titleUtf8 = "Create Tina Project in Empty Folder",
        .initialDirectoryUtf8 = location->initialDirectoryUtf8,
    });
    if (!selected) {
        if (selected.error().code == Tina::Core::CoreErrorCode::Unsupported) {
            authoringFeedback_ =
                "Native project folder selection is unavailable on this platform";
            return Tina::Core::success();
        }
        return Tina::Core::failure(std::move(selected.error()));
    }
    if (!selected->selected()) {
        authoringFeedback_ = "New project creation cancelled";
        return Tina::Core::success();
    }
    return scheduleNewProjectAt(selected->selectedPathUtf8);
}

auto EditorWorkspaceState::openProjectFromDialog() -> Tina::Core::Status{
    if (temporaryProjectActive()) {
        authoringFeedback_ =
            "Save the temporary project before opening another project";
        return Tina::Core::success();
    }
    const std::string_view currentRoot = activeProjectWorkspace_.has_value()
                                             ? activeProjectWorkspace_->projectRootUtf8()
                                             : std::string_view{assetResources_.catalogRootUtf8};
    auto location = makeSaveDialogLocation(currentRoot, {}, true);
    if (!location) {
        return Tina::Core::failure(std::move(location.error()));
    }
    auto selected = fileDialog_.pickFolder({
        .titleUtf8 = "Open Tina Project",
        .initialDirectoryUtf8 = location->initialDirectoryUtf8,
    });
    if (!selected) {
        if (selected.error().code == Tina::Core::CoreErrorCode::Unsupported) {
            authoringFeedback_ =
                "Native project folder selection is unavailable on this platform";
            return Tina::Core::success();
        }
        return Tina::Core::failure(std::move(selected.error()));
    }
    if (!selected->selected()) {
        authoringFeedback_ = "Open project cancelled";
        return Tina::Core::success();
    }

    auto workspace = openExistingEditorProjectWorkspace(selected->selectedPathUtf8);
    if (!workspace) {
        return reportAuthoringFailure("Project open failed: ", workspace.error());
    }
    try {
        std::string feedback = "Project open scheduled: ";
        feedback += workspace->projectRootUtf8();
        pendingProjectSwitch_ = std::move(*workspace);
        projectSwitchBlockedByDirty_ = false;
        authoringFeedback_.swap(feedback);
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Project open success feedback allocation failed");
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::refreshProjectCatalog() -> Tina::Core::Status{
    if (!assetResources_.projectCatalogConfigured ||
        !assetResources_.system.has_value()) {
        authoringFeedback_ = "Catalog refresh requires a configured project Catalog";
        return Tina::Core::success();
    }

    auto candidateTabs = prepareProjectSwitchDocumentTabs();
    if (!candidateTabs) {
        return reportAuthoringFailure(
            "Catalog refresh blocked; previous Catalog preserved: ",
            candidateTabs.error());
    }

    const Tina::Editor::ProjectAssetFilter previousFilter =
        projectAssets_.filter();
    const std::string_view previousSearchQuery = projectAssets_.searchQuery();
    std::optional<Tina::Core::AssetId> previousSelection{};
    previousSelection = projectAssets_.selectedAssetId();

    Tina::Asset::Sprite2DBindingRegistry* spriteParticipant =
        spriteBindings_.has_value() ? &*spriteBindings_ : nullptr;
    Tina::Asset::Mesh3DBindingRegistry* meshParticipant =
        mesh3DBindings_.has_value() ? &*mesh3DBindings_ : nullptr;
    Tina::Asset::CatalogReloadConfig reloadConfig{};
    reloadConfig.package.manifest.catalog.maxEntries = 1024;
    reloadConfig.package.manifest.catalog.maxDependencies = 4096;
    reloadConfig.package.manifest.catalog.maxDependenciesPerAsset = 64;
    reloadConfig.package.validation.verifyTypedPayload = true;
    if (spriteParticipant != nullptr) {
        reloadConfig.bindings.sprite2D =
            std::span<Tina::Asset::Sprite2DBindingRegistry*>{&spriteParticipant, 1U};
    }
    if (meshParticipant != nullptr) {
        reloadConfig.bindings.mesh3D =
            std::span<Tina::Asset::Mesh3DBindingRegistry*>{&meshParticipant, 1U};
    }
    auto reload = assetResources_.system->reloadCatalog(
        assetResources_.catalogRootUtf8, reloadConfig);
    if (!reload) {
        return reportAuthoringFailure(
            "Catalog refresh failed; previous Catalog preserved: ", reload.error());
    }

    const Tina::Asset::CatalogSnapshot* committedCatalog =
        assetResources_.system->catalog();
    if (committedCatalog == nullptr) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Catalog refresh committed without an AssetSystem Catalog snapshot");
    }
    auto refreshedBrowser = prepareProjectBrowserForSnapshot(
        *committedCatalog, previousFilter, previousSelection, previousSearchQuery,
        sourceImportUnitOutputs_, assetMetadata_,
        activeProjectWorkspace_.has_value()
            ? activeProjectWorkspace_->sourceRootUtf8()
            : std::string_view{});
    if (!refreshedBrowser) {
        auto error = std::move(refreshedBrowser.error());
        if (auto feedback = reportAuthoringFailure(
                "Catalog refresh committed, but Browser rebuild failed: ", error);
            !feedback) {
            return feedback;
        }
        return Tina::Core::failure(std::move(error));
    }
    if (auto status = switchCatalogAuthoringOwnersToPinnedTabs(); !status) {
        return status;
    }
    if (auto status = refreshPinnedCatalogAuthoringDocuments(*refreshedBrowser);
        !status) {
        return status;
    }

    assetResources_.catalogEntryCount =
        static_cast<u32>(refreshedBrowser->itemCount());
    counters_.catalogEntryCount = assetResources_.catalogEntryCount;
    if (auto status = rebuildLiveCatalogPreview(
            "Catalog, Project Browser, documents, and preview bindings refreshed");
        !status) {
        return status;
    }

    projectAssets_ = std::move(*refreshedBrowser);
    pruneAssetImportHistoryToCatalog();
    lastProjectAssetPointerDownAssetId_ = {};
    pendingProjectAssetOpen_.reset();
    projectAssetDragAssetId_ = {};
    pendingProjectAssetDrop_.reset();
    commitProjectSwitchDocumentTabs(std::move(*candidateTabs));
    observedProjectAssetSelectionIndex_.reset();
    projectBrowserUiRefreshPending_ = true;
    previewAssetBindingsRefreshPending_ = false;
    return Tina::Core::success();
}

auto EditorWorkspaceState::applyProjectAssetFilter(
    Tina::PrimaryWindowUITreeUpdater& tree,
    Tina::Editor::ProjectAssetFilter filter) -> Tina::Core::Status{
    if (auto status = projectAssets_.setFilter(filter); !status) {
        return status;
    }
    lastProjectAssetPointerDownAssetId_ = {};
    pendingProjectAssetOpen_.reset();
    observedProjectAssetSelectionIndex_.reset();
    if (auto status = tree.setVirtualGridViewDataSource(projectAssetList_,
                                                 projectAssetDataSource());
        !status) {
        return status;
    }
    if (projectAssets_.visibleItemCount() != 0U) {
        projectAssetSelectionSyncPending_ = true;
        assetInspectorActive_ = true;
    } else {
        projectAssetSelectionSyncPending_ = false;
        if (auto status = tree.clearVirtualGridViewSelection(projectAssetList_);
            !status) {
            return status;
        }
        assetInspectorActive_ = false;
    }
    synchronizeViewportSelectionFromHierarchy();
    authoringFeedback_ = "Project Asset Browser filter changed";
    if (auto status = refreshProjectAssetUi(tree); !status) {
        return status;
    }
    return refreshAuthoringUi(tree);
}

auto EditorWorkspaceState::applyProjectAssetViewMode(
    Tina::PrimaryWindowUITreeUpdater& tree,
    ProjectAssetViewMode mode) -> Tina::Core::Status
{
    projectAssetViewMode_ = mode;
    // A committed presentation change invalidates any in-flight pointer
    // gesture; the next click must establish a fresh stable AssetId candidate.
    lastProjectAssetPointerDownAssetId_ = {};
    pendingProjectAssetOpen_.reset();
    projectAssetListStyle_.minimumItemWidth =
        mode == ProjectAssetViewMode::Grid ? ProjectAssetMinimumItemWidth + 12.0F : 320.0F;
    projectAssetListStyle_.itemHeight =
        mode == ProjectAssetViewMode::Grid ? 96.0F : 68.0F;
    if (auto status = tree.setVirtualGridViewStyle(
            projectAssetList_, projectAssetListStyle_); !status) {
        return status;
    }
    for (u32 index = 0; index < projectAssetViewButtons_.size(); ++index) {
        if (auto status = tree.setRadioButtonSelected(
                projectAssetViewButtons_[index],
                index == static_cast<u32>(projectAssetViewMode_));
            !status) {
            return status;
        }
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::inspectedProjectAsset() const noexcept -> const Tina::Editor::ProjectAssetDescriptor*{
    return projectAssets_.selectedInspectorSnapshot();
}

auto EditorWorkspaceState::sourceImportUnitForAsset(
    Tina::Core::AssetId assetId) const noexcept
    -> const Tina::EditorApp::Detail::EditorSourceImportUnit*
{
    if (!assetId) {
        return nullptr;
    }
    for (const auto& mapping : sourceImportUnitOutputs_) {
        const bool ownsAsset = std::any_of(
            mapping.outputs.begin(), mapping.outputs.end(),
            [assetId](const Tina::Asset::SourceImportPipelineOutput& output) {
                return output.assetId == assetId;
            });
        if (!ownsAsset) {
            continue;
        }
        const auto unit = std::find_if(
            sourceImportUnits_.begin(), sourceImportUnits_.end(),
            [&mapping](const auto& candidate) {
                return candidate.sourcePathUtf8 == mapping.sourceUtf8Path;
            });
        return unit != sourceImportUnits_.end() ? &*unit : nullptr;
    }
    return nullptr;
}

auto EditorWorkspaceState::resolveProjectAssetItem(const void* state, u64 logicalIndex,
                                    UI::UIVirtualGridViewItemDescriptor& output) noexcept -> bool{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    if (self == nullptr) {
        return false;
    }
    const auto* asset = self->projectAssets_.visibleItem(
        static_cast<Tina::Core::usize>(logicalIndex));
    if (asset == nullptr) {
        return false;
    }
    const auto iconSource = editorIconContent(projectAssetIcon(asset->assetKind)).source;
    const EditorAssetImportStatus assetImportStatus =
        self->projectAssetImportStatus(asset->assetId);
    const bool assetImportPending =
        assetImportStatus == EditorAssetImportStatus::Queued ||
        assetImportStatus == EditorAssetImportStatus::Preparing ||
        assetImportStatus == EditorAssetImportStatus::Copying ||
        assetImportStatus == EditorAssetImportStatus::Cooking ||
        assetImportStatus == EditorAssetImportStatus::ReadyToCommit;
    const bool assetImportError =
        assetImportStatus == EditorAssetImportStatus::Error;
    UI::UIVirtualGridViewItemPresentation presentation{
        .secondaryLabel = Tina::Editor::projectAssetKindLabel(asset->assetKind),
        .statusLabel = self->projectAssetImportStatusLabel(asset->assetId),
        .status = assetImportPending
                      ? UI::UIVirtualGridViewItemStatus::Importing
                      : assetImportError
                            ? UI::UIVirtualGridViewItemStatus::Error
                            : UI::UIVirtualGridViewItemStatus::Ready,
        .preview = std::nullopt,
        .icon = iconSource,
    };

    const auto makeTextureSource = [&](Tina::Core::AssetId textureId,
                                       Tina::Core::u32 left,
                                       Tina::Core::u32 top,
                                       Tina::Core::u32 width,
                                       Tina::Core::u32 height)
        -> std::optional<UI::UIImageSource> {
        if (!textureId || width == 0U || height == 0U ||
            !self->assetResources_.system.has_value()) {
            return std::nullopt;
        }
        const Tina::Asset::AssetHandle textureHandle = self->loadedAsset(
            textureId, Tina::AssetFormat::AssetKind::Texture2D);
        if (!textureHandle) {
            return std::nullopt;
        }
        const Tina::Asset::CookedAssetFile* textureFile =
            self->assetResources_.system->tryGet(textureHandle);
        if (textureFile == nullptr) {
            return std::nullopt;
        }
        const auto texture = Tina::Asset::parseTexture2DFromCooked(*textureFile);
        if (!texture || texture->width == 0U || texture->height == 0U ||
            left > texture->width || top > texture->height ||
            width > texture->width - left || height > texture->height - top) {
            return std::nullopt;
        }
        return UI::UIImageSource{
            .texture = textureId,
            .sourcePixels = {.x = left, .y = top, .width = width, .height = height},
            .texturePixelExtent = {.width = texture->width, .height = texture->height},
            .intrinsicLogicalSize = {
                .width = static_cast<float>(width),
                .height = static_cast<float>(height),
            },
        };
    };

    if (asset->assetKind == Tina::AssetFormat::AssetKind::Texture2D) {
        const Tina::Asset::AssetHandle textureHandle = self->loadedAsset(
            asset->assetId, Tina::AssetFormat::AssetKind::Texture2D);
        const Tina::Asset::CookedAssetFile* textureFile = textureHandle
            ? self->assetResources_.system->tryGet(textureHandle)
            : nullptr;
        if (textureFile != nullptr) {
            const auto texture = Tina::Asset::parseTexture2DFromCooked(*textureFile);
            if (texture && texture->width != 0U && texture->height != 0U) {
                presentation.preview = makeTextureSource(
                    asset->assetId, 0U, 0U,
                    static_cast<Tina::Core::u32>(texture->width),
                    static_cast<Tina::Core::u32>(texture->height));
            }
        }
        if (!presentation.preview.has_value() && !assetImportPending &&
            !assetImportError) {
            presentation.status = UI::UIVirtualGridViewItemStatus::Missing;
            presentation.statusLabel = "Preview unavailable";
        }
    } else if (asset->assetKind == Tina::AssetFormat::AssetKind::Sprite &&
               self->assetResources_.system.has_value()) {
        const Tina::Asset::AssetHandle spriteHandle =
            self->loadedAsset(asset->assetId, Tina::AssetFormat::AssetKind::Sprite);
        const Tina::Asset::CookedAssetFile* spriteFile = spriteHandle
            ? self->assetResources_.system->tryGet(spriteHandle)
            : nullptr;
        std::optional<Tina::Core::AssetId> textureId;
        if (spriteFile != nullptr) {
            for (Tina::Core::u32 dependencyIndex = 0;
                 dependencyIndex < spriteFile->header().dependencyCount;
                 ++dependencyIndex) {
                const auto dependency = spriteFile->dependency(dependencyIndex);
                if (dependency.has_value() &&
                    dependency->expectedKind == Tina::AssetFormat::AssetKind::Texture2D) {
                    textureId = dependency->assetId;
                    break;
                }
            }
        }
        std::optional<Tina::AssetFormat::SpritePayloadView> sprite;
        if (spriteFile != nullptr) {
            if (const auto parsed = Tina::Asset::parseSpriteFromCooked(*spriteFile);
                parsed) {
                sprite = *parsed;
            }
        }
        if (textureId.has_value() && sprite.has_value() &&
            std::isfinite(sprite->u0) && std::isfinite(sprite->v0) &&
            std::isfinite(sprite->u1) && std::isfinite(sprite->v1) &&
            sprite->u0 >= 0.0F && sprite->u0 <= 1.0F &&
            sprite->v0 >= 0.0F && sprite->v0 <= 1.0F &&
            sprite->u1 >= 0.0F && sprite->u1 <= 1.0F &&
            sprite->v1 >= 0.0F && sprite->v1 <= 1.0F &&
            sprite->u1 > sprite->u0 && sprite->v1 > sprite->v0) {
            const Tina::Asset::AssetHandle textureHandle = self->loadedAsset(
                *textureId, Tina::AssetFormat::AssetKind::Texture2D);
            const Tina::Asset::CookedAssetFile* textureFile = textureHandle
                ? self->assetResources_.system->tryGet(textureHandle)
                : nullptr;
            std::optional<Tina::AssetFormat::Texture2DPayloadView> texture;
            if (textureFile != nullptr) {
                if (const auto parsed = Tina::Asset::parseTexture2DFromCooked(*textureFile);
                    parsed && parsed->width != 0U && parsed->height != 0U) {
                    texture = *parsed;
                }
            }
            if (texture.has_value()) {
                const auto toPixel = [](float uv, Tina::Core::u16 extent) noexcept {
                    return static_cast<Tina::Core::u32>(std::lround(
                        static_cast<double>(uv) * static_cast<double>(extent)));
                };
                const Tina::Core::u32 left = toPixel(sprite->u0, texture->width);
                const Tina::Core::u32 top = toPixel(sprite->v0, texture->height);
                const Tina::Core::u32 right = toPixel(sprite->u1, texture->width);
                const Tina::Core::u32 bottom = toPixel(sprite->v1, texture->height);
                if (right > left && bottom > top &&
                    right <= texture->width && bottom <= texture->height) {
                    presentation.preview = makeTextureSource(
                        *textureId, left, top, right - left, bottom - top);
                }
            }
        }
        if (!presentation.preview.has_value() && !assetImportPending &&
            !assetImportError) {
            presentation.status = UI::UIVirtualGridViewItemStatus::Missing;
            presentation.statusLabel = "Preview unavailable";
        }
    }

    output = UI::UIVirtualGridViewItemDescriptor{
        .key = self->projectAssets_.visibleItemStableKey(
            static_cast<Tina::Core::usize>(logicalIndex)),
        .label = asset->displayName,
        .enabled = true,
        .presentation = presentation,
    };
    return true;
}

} // namespace Tina::EditorApp::WorkspaceInternal
