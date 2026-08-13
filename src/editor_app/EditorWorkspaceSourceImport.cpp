#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {

auto EditorWorkspaceState::startSourceImport(
    std::span<const Tina::EditorApp::Detail::EditorSourceImportUnit> intendedUnits) -> Tina::Core::Status{
    if (!activeProjectWorkspace_.has_value()) {
        authoringFeedback_ = "Source import requires an open Tina project";
        return Tina::Core::success();
    }
    if (sourceImportService_.state() !=
        Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle) {
        authoringFeedback_ = "Source import is already running or awaiting Catalog commit";
        return Tina::Core::success();
    }

    auto validatedUnits =
        Tina::EditorApp::Detail::validateEditorSourceImportIntendedSet(
            activeProjectWorkspace_->sourceRootUtf8(), intendedUnits);
    if (!validatedUnits) {
        return Tina::Core::failure(std::move(validatedUnits.error()));
    }

    auto stagePaths = createSourceImportStagePaths(*activeProjectWorkspace_);
    if (!stagePaths) {
        return Tina::Core::failure(std::move(stagePaths.error()));
    }
    auto stageReservation = Tina::Core::makeScopeExit([this, &stagePaths]() noexcept {
        cleanupOwnedSourceImportStage(stagePaths->catalogRootUtf8);
        sourceImportPendingStageRootUtf8_.clear();
        sourceImportPointerPathUtf8_.clear();
    });
    auto cache = ensureSourceImportCache(*activeProjectWorkspace_);
    if (!cache) {
        return Tina::Core::failure(std::move(cache.error()));
    }

    Tina::EditorApp::Detail::EditorSourceImportRequest request{};
    try {
        request.sourceRootUtf8.assign(activeProjectWorkspace_->sourceRootUtf8());
        request.baselineCatalogRootUtf8 = assetResources_.sourceImportCatalogRootUtf8;
        request.baselineStatePathUtf8 = assetResources_.sourceImportStatePathUtf8;
        request.freshStageRootUtf8 = stagePaths->catalogRootUtf8;
        request.freshStageStatePathUtf8 = stagePaths->statePathUtf8;
        request.targetPlatform = activeProjectWorkspace_->targetPlatform();
        request.units = *validatedUnits;
        sourceImportPointerPathUtf8_ = pathToUtf8(cache->activeCatalogPointer);
        sourceImportPendingStageRootUtf8_ = request.freshStageRootUtf8;
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Editor could not retain the source-import request");
    }

    if (auto status = sourceImportService_.start(std::move(request)); !status) {
        return status;
    }
    sourceImportUnits_.swap(*validatedUnits);
    counters_.sourceImportIntendedUnits = sourceImportUnits_.size();
    sourceImportUiRefreshPending_ = true;
    stageReservation.release();
    sourceImportCatalogCommitted_ = false;
    counters_.sourceImportRunning = true;
    counters_.sourceImportReady = false;
    counters_.sourceImportStateCommitted = false;
    ++counters_.sourceImportStarts;
    authoringFeedback_ = "Source import is cooking a fully validated fresh stage";
    return Tina::Core::success();
}

auto EditorWorkspaceState::removeSelectedSourceImport() -> Tina::Core::Status{
    if (!activeProjectWorkspace_.has_value()) {
        authoringFeedback_ = "Source import removal requires an open Tina project";
        return Tina::Core::success();
    }
    if (sourceImportService_.state() !=
        Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle) {
        authoringFeedback_ =
            "Finish or dismiss the current source import before removing a unit";
        return Tina::Core::success();
    }
    if (!observedSourceImportSelectionIndex_.has_value()) {
        authoringFeedback_ = "Select a source import unit to remove";
        return Tina::Core::success();
    }

    auto intendedUnits =
        Tina::EditorApp::Detail::removeEditorSourceImportUnit(
            sourceImportUnits_,
            static_cast<Tina::Core::usize>(
                *observedSourceImportSelectionIndex_));
    if (!intendedUnits) {
        return reportAuthoringFailure(
            "Source import removal rejected: ", intendedUnits.error());
    }
    const bool removesFinalUnit = intendedUnits->empty();
    if (auto status = startSourceImport(*intendedUnits); !status) {
        return status;
    }
    authoringFeedback_ = removesFinalUnit
        ? "Removing the final source import and publishing an empty Catalog"
        : "Rebuilding the Catalog without the selected source import";
    return Tina::Core::success();
}

auto EditorWorkspaceState::cleanupOwnedSourceImportStage(std::string_view catalogRootUtf8) noexcept -> void{
    if (catalogRootUtf8.empty() || !activeProjectWorkspace_.has_value()) {
        return;
    }
    try {
        const auto cache = sourceImportCachePaths(*activeProjectWorkspace_);
        const auto catalogRoot = std::filesystem::u8path(
            catalogRootUtf8.begin(), catalogRootUtf8.end());
        const auto stageRoot = catalogRoot.parent_path().lexically_normal();
        if (stageRoot.empty()) {
            return;
        }
        if (!validatePhysicalProjectDirectory(cache.stages, "sourceImportStages") ||
            !validatePhysicalProjectDirectory(stageRoot, "sourceImportStage")) {
            return;
        }
        std::error_code canonicalError;
        const auto projectRoot = std::filesystem::u8path(
            activeProjectWorkspace_->projectRootUtf8().begin(),
            activeProjectWorkspace_->projectRootUtf8().end());
        const auto physicalProject =
            std::filesystem::weakly_canonical(projectRoot, canonicalError);
        if (canonicalError) {
            return;
        }
        const auto physicalStages =
            std::filesystem::weakly_canonical(cache.stages, canonicalError);
        if (canonicalError) {
            return;
        }
        const auto physicalStage =
            std::filesystem::weakly_canonical(stageRoot, canonicalError);
        if (canonicalError ||
            !pathIsSameOrDescendant(physicalStages, physicalProject) ||
            !pathsReferToSameLocation(physicalStage.parent_path(), physicalStages)) {
            return;
        }
        std::error_code cleanupError;
        (void)std::filesystem::remove_all(stageRoot, cleanupError);
    } catch (...) {
    }
}

auto EditorWorkspaceState::cleanupOwnedAuthoringStage(std::string_view catalogRootUtf8) noexcept -> void{
    if (catalogRootUtf8.empty() || !activeProjectWorkspace_.has_value()) {
        return;
    }
    try {
        const auto cache = authoringCachePaths(*activeProjectWorkspace_);
        const auto catalogRoot = std::filesystem::u8path(
            catalogRootUtf8.begin(), catalogRootUtf8.end());
        const auto stageRoot = catalogRoot.parent_path().lexically_normal();
        if (stageRoot.empty() ||
            !validatePhysicalProjectDirectory(cache.stages, "authoringStages") ||
            !validatePhysicalProjectDirectory(stageRoot, "authoringStage")) {
            return;
        }
        std::error_code canonicalError;
        const auto projectRoot = std::filesystem::u8path(
            activeProjectWorkspace_->projectRootUtf8().begin(),
            activeProjectWorkspace_->projectRootUtf8().end());
        const auto physicalProject =
            std::filesystem::weakly_canonical(projectRoot, canonicalError);
        const auto physicalStages =
            std::filesystem::weakly_canonical(cache.stages, canonicalError);
        const auto physicalStage =
            std::filesystem::weakly_canonical(stageRoot, canonicalError);
        if (canonicalError ||
            !pathIsSameOrDescendant(physicalStages, physicalProject) ||
            !pathsReferToSameLocation(physicalStage.parent_path(), physicalStages)) {
            return;
        }
        std::error_code cleanupError;
        (void)std::filesystem::remove_all(stageRoot, cleanupError);
    } catch (...) {
    }
}

auto EditorWorkspaceState::cleanupFailedSourceImportStage() noexcept -> void{
    cleanupOwnedSourceImportStage(sourceImportPendingStageRootUtf8_);
    sourceImportPendingStageRootUtf8_.clear();
}

auto EditorWorkspaceState::publishCommittedSourceImportState(
    const Tina::EditorApp::Detail::EditorSourceImportReadyStage& ready) -> Tina::Core::Status{
    if (!ready.stageCreated) {
        return Tina::Core::success();
    }
    if (activeProjectWorkspace_.has_value()) {
        const auto authoringPointer =
            authoringCachePaths(*activeProjectWorkspace_).activeCatalogPointer;
        std::error_code pointerError;
        (void)std::filesystem::remove(authoringPointer, pointerError);
        if (pointerError && pointerError != std::errc::no_such_file_or_directory) {
            Tina::Core::Error error{
                Tina::Core::CoreErrorCode::Io,
                "Editor could not retire the active authoring Catalog pointer"};
            error.setNativeCode(pointerError.value());
            return Tina::Core::failure(std::move(error));
        }
    }
    try {
        std::error_code stateError;
        const auto stateStatus = std::filesystem::symlink_status(
            std::filesystem::u8path(ready.statePathUtf8.begin(), ready.statePathUtf8.end()),
            stateError);
        if (stateError || !std::filesystem::is_regular_file(stateStatus)) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::NotFound,
                "Imported Catalog stage state disappeared before commit");
        }
    } catch (const std::filesystem::filesystem_error& exception) {
        Tina::Core::Error error{
            Tina::Core::CoreErrorCode::Io,
            "Editor could not inspect the imported Catalog stage state"};
        error.setNativeCode(exception.code().value());
        return Tina::Core::failure(std::move(error));
    }
    const auto pointerBytes = std::as_bytes(std::span{
        ready.stageRootUtf8.data(), ready.stageRootUtf8.size()});
    if (auto status = Tina::Core::writeFile(
            sourceImportPointerPathUtf8_, pointerBytes);
        !status) {
        return status;
    }
    cleanupOwnedSourceImportStage(sourceImportSupersededCatalogRootUtf8_);
    sourceImportSupersededCatalogRootUtf8_.clear();
    cleanupOwnedAuthoringStage(sourceImportSupersededAuthoringCatalogRootUtf8_);
    sourceImportSupersededAuthoringCatalogRootUtf8_.clear();
    return Tina::Core::success();
}

auto EditorWorkspaceState::commitSourceImportCatalog(
    const Tina::EditorApp::Detail::EditorSourceImportReadyStage& ready) -> Tina::Core::Status{
    if (!ready.stageCreated) {
        return Tina::Core::success();
    }
    auto candidateTabs = prepareProjectSwitchDocumentTabs();
    if (!candidateTabs) {
        if (candidateTabs.error().code ==
            Tina::Editor::EditorErrorCode::DirtyDocumentRequiresConfirmation) {
            return reportAuthoringFailure(
                "Source import ready; save or discard modified Catalog documents: ",
                candidateTabs.error());
        }
        return Tina::Core::failure(std::move(candidateTabs.error()));
    }

    const auto previousFilter = projectAssets_.filter();
    std::optional<Tina::Core::AssetId> previousSelection{};
    if (const auto* selected = projectAssets_.selectedItem(); selected != nullptr) {
        previousSelection = selected->assetId;
    }
    std::string nextCatalogRoot;
    std::string nextSourceImportCatalogRoot;
    std::string nextStatePath;
    std::string previousSourceImportCatalogRoot;
    std::string previousAuthoringCatalogRoot;
    try {
        nextCatalogRoot = ready.stageRootUtf8;
        nextSourceImportCatalogRoot = ready.stageRootUtf8;
        nextStatePath = ready.statePathUtf8;
        previousSourceImportCatalogRoot = assetResources_.sourceImportCatalogRootUtf8;
        previousAuthoringCatalogRoot = assetResources_.authoringCatalogRootUtf8;
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Editor could not stage imported Catalog ownership");
    }

    Tina::Asset::CatalogPackageOpenConfig candidateConfig{};
    candidateConfig.manifest.catalog.maxEntries = 4096;
    candidateConfig.manifest.catalog.maxDependencies = 16384;
    candidateConfig.manifest.catalog.maxDependenciesPerAsset = 4096;
    candidateConfig.manifest.catalog.memoryResource = &sourceImportMemory_;
    candidateConfig.validation.file.memoryResource = &sourceImportMemory_;
    candidateConfig.validation.verifyTypedPayload = true;
    auto candidateCatalog = Tina::Asset::openCatalogPackage(
        ready.stageRootUtf8, candidateConfig);
    if (!candidateCatalog) {
        return Tina::Core::failure(std::move(candidateCatalog.error()));
    }
    auto browser = prepareProjectBrowserForSnapshot(
        *candidateCatalog, previousFilter, previousSelection);
    if (!browser) {
        return Tina::Core::failure(std::move(browser.error()));
    }

    Tina::Asset::Sprite2DBindingRegistry* spriteParticipant =
        spriteBindings_.has_value() ? &*spriteBindings_ : nullptr;
    Tina::Asset::Mesh3DBindingRegistry* meshParticipant =
        mesh3DBindings_.has_value() ? &*mesh3DBindings_ : nullptr;
    Tina::Asset::CatalogReloadConfig reloadConfig{};
    reloadConfig.package.manifest.catalog.maxEntries = 4096;
    reloadConfig.package.manifest.catalog.maxDependencies = 16384;
    reloadConfig.package.manifest.catalog.maxDependenciesPerAsset = 4096;
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
        ready.stageRootUtf8, reloadConfig);
    if (!reload) {
        if (reload.error().code == Tina::Asset::AssetErrorCode::CatalogReloadBusy) {
            ++counters_.sourceImportBusyRetries;
            authoringFeedback_ = "Source import stage is ready; Catalog reload will retry on "
                                 "the next safe frame";
            return Tina::Core::success();
        }
        auto status = reportAuthoringFailure(
            "Source import reload failed; previous Catalog preserved: ", reload.error());
        if (!status) {
            return status;
        }
        ++counters_.sourceImportFailures;
        counters_.sourceImportRunning = false;
        counters_.sourceImportReady = false;
        if (auto acknowledge = sourceImportService_.acknowledgeReady(); !acknowledge) {
            return acknowledge;
        }
        cleanupFailedSourceImportStage();
        return Tina::Core::success();
    }
    sourceImportCatalogCommitted_ = true;

    const auto* committedCatalog = assetResources_.system->catalog();
    if (committedCatalog == nullptr) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Source import committed without an AssetSystem Catalog snapshot");
    }
    ++counters_.sourceImportCatalogReloads;
    if (auto status = switchCatalogAuthoringOwnersToPinnedTabs(); !status) {
        return status;
    }
    if (auto status = refreshPinnedCatalogAuthoringDocuments(*browser); !status) {
        return status;
    }
    assetResources_.catalogRootUtf8.swap(nextCatalogRoot);
    assetResources_.sourceImportCatalogRootUtf8.swap(nextSourceImportCatalogRoot);
    assetResources_.sourceImportStatePathUtf8.swap(nextStatePath);
    assetResources_.authoringCatalogRootUtf8.clear();
    navigationDocument_.markCatalogDirty();
    sourceImportSupersededCatalogRootUtf8_ =
        std::move(previousSourceImportCatalogRoot);
    sourceImportSupersededAuthoringCatalogRootUtf8_ =
        std::move(previousAuthoringCatalogRoot);
    assetResources_.catalogEntryCount = static_cast<u32>(browser->itemCount());
    counters_.catalogEntryCount = assetResources_.catalogEntryCount;
    if (auto status = rebuildLiveCatalogPreview(
            "Source import Catalog, Browser, documents, and previews committed");
        !status) {
        return status;
    }
    projectAssets_ = std::move(*browser);
    commitProjectSwitchDocumentTabs(std::move(*candidateTabs));
    observedProjectAssetSelectionIndex_.reset();
    projectBrowserUiRefreshPending_ = true;
    previewAssetBindingsRefreshPending_ = false;
    return Tina::Core::success();
}

auto EditorWorkspaceState::updateSourceImport() -> Tina::Core::Status{
    if (sourceImportStartPending_) {
        sourceImportStartPending_ = false;
        if (auto status = startSourceImport(sourceImportUnits_); !status) {
            return status;
        }
    }
    if (auto status = sourceImportService_.poll(); !status) {
        return status;
    }
    using State = Tina::EditorApp::Detail::EditorSourceImportServiceState;
    counters_.sourceImportRunning = sourceImportService_.state() == State::Running;
    if (sourceImportService_.state() == State::Failed) {
        const auto* failure = sourceImportService_.failure();
        if (failure == nullptr) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Source import service failed without an error");
        }
        if (auto status = reportAuthoringFailure("Source import failed: ", *failure);
            !status) {
            return status;
        }
        ++counters_.sourceImportFailures;
        counters_.sourceImportRunning = false;
        counters_.sourceImportReady = false;
        cleanupFailedSourceImportStage();
        if (options_.sourceImport.importOnStart &&
            options_.targetFrameCount != 0U) {
            return Tina::Core::failure(*failure);
        }
        return sourceImportService_.dismissFailure();
    }
    if (sourceImportService_.state() != State::Ready) {
        return Tina::Core::success();
    }

    const auto* ready = sourceImportService_.readyStage();
    if (ready == nullptr) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Source import service is Ready without a stage");
    }
    counters_.sourceImportRunning = false;
    counters_.sourceImportReady = true;
    if (!sourceImportCatalogCommitted_) {
        if (auto status = commitSourceImportCatalog(*ready); !status) {
            return status;
        }
        if (sourceImportService_.state() != State::Ready ||
            (ready->stageCreated && !sourceImportCatalogCommitted_)) {
            return Tina::Core::success();
        }
    }
    if (auto status = publishCommittedSourceImportState(*ready); !status) {
        return reportAuthoringFailure(
            "Imported Catalog is live; import state commit will retry: ",
            status.error());
    }

    counters_.sourceImportUnitsTotal = ready->statistics.unitsTotal;
    counters_.sourceImportUnitsRecooked = ready->statistics.unitsRecooked;
    counters_.sourceImportUnitsRemoved = ready->statistics.unitsRemoved;
    counters_.sourceImportObjectsReused = ready->statistics.objectsReused;
    counters_.sourceImportObjectsCooked = ready->statistics.objectsCooked;
    counters_.sourceImportStateCommitted = true;
    counters_.sourceImportReady = false;
    ++counters_.sourceImportCompletions;
    try {
        authoringFeedback_ = "Source import complete: ";
        authoringFeedback_ += std::to_string(ready->statistics.unitsRecooked);
        authoringFeedback_ += " recooked unit(s), ";
        authoringFeedback_ += std::to_string(ready->statistics.objectsReused);
        authoringFeedback_ += " reused object(s)";
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Editor source import completion feedback allocation failed");
    }
    if (!ready->stageCreated) {
        cleanupOwnedSourceImportStage(sourceImportPendingStageRootUtf8_);
    }
    if (auto status = sourceImportService_.acknowledgeReady(); !status) {
        return status;
    }
    sourceImportCatalogCommitted_ = false;
    sourceImportPendingStageRootUtf8_.clear();
    return Tina::Core::success();
}

auto EditorWorkspaceState::resolveSourceImportItem(
    const void* state, u64 logicalIndex,
    UI::UIListViewItemDescriptor& output) noexcept -> bool{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    if (self == nullptr || logicalIndex >= self->sourceImportUnits_.size()) {
        return false;
    }
    const auto& unit = self->sourceImportUnits_[
        static_cast<Tina::Core::usize>(logicalIndex)];
    output = UI::UIListViewItemDescriptor{
        .key = 30'000U + logicalIndex,
        .label = unit.sourcePathUtf8,
        .enabled = true,
    };
    return true;
}

} // namespace Tina::EditorApp::WorkspaceInternal
