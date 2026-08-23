#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {

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
    const bool importPending =
        pendingProjectSwitch_.has_value() ||
        !pendingSourceImportPathsUtf8_.empty() ||
        sourceImportStartPending_ || importState == ImportState::Running ||
        importState == ImportState::Ready;
    std::string_view importProgressSummary = "Preparing project import...";
    if (importState == ImportState::Ready) {
        importProgressSummary = "Import ready; committing Catalog...";
    } else if (importState == ImportState::Running) {
        using ImportPhase =
            Tina::EditorApp::Detail::EditorSourceImportPhase;
        switch (sourceImportService_.phase()) {
        case ImportPhase::Preparing:
            importProgressSummary = "Preparing resource batch...";
            break;
        case ImportPhase::Copying:
            importProgressSummary = "Copying resources in background...";
            break;
        case ImportPhase::Cooking:
            importProgressSummary = "Cooking and validating resources...";
            break;
        case ImportPhase::Idle:
        case ImportPhase::ReadyToCommit:
        case ImportPhase::Failed:
            importProgressSummary = "Importing resources...";
            break;
        }
    }
    if (const auto* asset = projectAssets_.selectedItem(); asset != nullptr) {
        selectedAssetSummary.assign(
            Tina::Editor::projectAssetKindLabel(asset->assetKind));
        selectedAssetSummary += "  |  ";
        const auto idText = asset->assetId.canonicalText();
        selectedAssetSummary.append(idText.data(), idText.size());
    } else {
        selectedAssetSummary = importPending
                                   ? importProgressSummary
                                   : sourceImportLastFailed_
                                         ? "Import failed; previous Catalog preserved"
                                         : assetResources_.projectCatalogConfigured
                                   ? "No assets match this filter"
                                   : (assetResources_.testFixtureCatalog
                                          ? "No test assets match this filter"
                                          : "No project open");
    }
    if (auto status = tree.setText(projectAssetSummary_, selectedAssetSummary);
        !status) {
        return status;
    }
    if (auto status = tree.setText(
            projectAssetSource_,
            importPending
                ? "Importing"
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
        pendingSourceImportPathsUtf8_.empty() &&
        sourceImportIdle;
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
    if (auto status = tree.setEnabled(importSourceButton_,
                                      sourceImportAvailable);
        !status) {
        return status;
    }
    counters_.projectAssetVisibleItems = projectAssets_.visibleItemCount();
    return Tina::Core::success();
}

auto EditorWorkspaceState::prepareProjectBrowserForSnapshot(
    const Tina::Asset::CatalogSnapshot& catalog,
    Tina::Editor::ProjectAssetFilter filter,
    std::optional<Tina::Core::AssetId> selectedAsset) -> Tina::Core::Result<Tina::Editor::ProjectAssetBrowserModel>{
    auto browser = createProjectAssetBrowser(catalog);
    if (!browser) {
        return Tina::Core::failure(std::move(browser.error()));
    }
    if (auto status = browser->setFilter(filter); !status) {
        return Tina::Core::failure(std::move(status.error()));
    }
    bool restoredSelection = false;
    if (selectedAsset.has_value()) {
        restoredSelection = static_cast<bool>(browser->selectAsset(*selectedAsset));
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
    counters_.runtimePreviewValid = false;
    counters_.catalogReady = false;
    animationPreview_.resetAnimator();
    releasePreviewAssetBindings();
    if (auto status = preparePreviewAssetBindings(); !status) {
        auto error = std::move(status.error());
        releasePreviewAssetBindings();
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
        releasePreviewAssetBindings();
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
        releasePreviewAssetBindings();
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
    Tina::Editor::EditorProjectWorkspace workspace) -> Tina::Core::Status{
    if (!assetResources_.system.has_value()) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Project switch requires the Editor AssetSystem");
    }

    auto candidateTabs = prepareProjectSwitchDocumentTabs();
    if (!candidateTabs) {
        if (candidateTabs.error().code ==
            Tina::Editor::EditorErrorCode::DirtyDocumentRequiresConfirmation) {
            pendingProjectSwitch_ = std::move(workspace);
            projectSwitchBlockedByDirty_ = true;
            return reportAuthoringFailure(
                "Project switch blocked; previous Catalog preserved: ",
                candidateTabs.error());
        }
        return Tina::Core::failure(std::move(candidateTabs.error()));
    }

    std::optional<Tina::Core::AssetId> previousSelection{};
    if (const auto* selected = projectAssets_.selectedItem(); selected != nullptr) {
        previousSelection = selected->assetId;
    }
    const Tina::Editor::ProjectAssetFilter previousFilter = projectAssets_.filter();

    auto resolvedCatalog = resolveProjectCatalog(workspace);
    if (!resolvedCatalog) {
        return reportAuthoringFailure(
            "Project switch could not resolve its active Catalog: ",
            resolvedCatalog.error());
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
        return reportAuthoringFailure(
            "Project switch failed; previous Catalog preserved: ", reload.error());
    }

    const Tina::Asset::CatalogSnapshot* committedCatalog =
        assetResources_.system->catalog();
    if (committedCatalog == nullptr) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Project switch committed without an AssetSystem Catalog snapshot");
    }
    auto candidateBrowser = prepareProjectBrowserForSnapshot(
        *committedCatalog, previousFilter, previousSelection);
    if (!candidateBrowser) {
        auto error = std::move(candidateBrowser.error());
        if (auto feedback = reportAuthoringFailure(
                "Project Catalog committed, but Browser rebuild failed: ", error);
            !feedback) {
            return feedback;
        }
        return Tina::Core::failure(std::move(error));
    }
    if (auto status = switchCatalogAuthoringOwnersToPinnedTabs(); !status) {
        return status;
    }
    if (auto status = refreshPinnedCatalogAuthoringDocuments(*candidateBrowser);
        !status) {
        return status;
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
        return status;
    }

    projectAssets_ = std::move(*candidateBrowser);
    commitProjectSwitchDocumentTabs(std::move(*candidateTabs));
    activeProjectWorkspace_ = std::move(workspace);
    sourceImportUnits_ = std::move(resolvedCatalog->sourceImportUnits);
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
    return Tina::Core::success();
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
    std::optional<Tina::Core::AssetId> previousSelection{};
    if (const auto* selected = projectAssets_.selectedItem(); selected != nullptr) {
        previousSelection = selected->assetId;
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
        *committedCatalog, previousFilter, previousSelection);
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

auto EditorWorkspaceState::inspectedProjectAsset() const noexcept -> const Tina::Editor::ProjectAssetDescriptor*{
    const auto* activeTab = documentTabs_.activeTab();
    if (activeTab != nullptr &&
        activeTab->key.kind == Tina::Editor::EditorDocumentKind::AssetInspector &&
        activeTab->key.assetId) {
        return projectAssets_.inspectorSnapshot(activeTab->key.assetId);
    }
    return projectAssets_.selectedInspectorSnapshot();
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
    output = UI::UIVirtualGridViewItemDescriptor{
        .key = 10'000U + logicalIndex,
        .label = asset->displayName,
        .enabled = true,
    };
    return true;
}

} // namespace Tina::EditorApp::WorkspaceInternal
