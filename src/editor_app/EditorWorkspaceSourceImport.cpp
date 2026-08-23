#include "EditorWorkspaceState.hpp"

#include "EditorSourceImportSelection.hpp"

namespace Tina::EditorApp::WorkspaceInternal {

auto EditorWorkspaceState::startSourceImport(
    std::span<const Tina::EditorApp::Detail::EditorSourceImportUnit> intendedUnits,
    std::vector<std::string> selectedPathsUtf8) -> Tina::Core::Status{
    const bool hasSelectedPaths = !selectedPathsUtf8.empty();
    if (!activeProjectWorkspace_.has_value()) {
        authoringFeedback_ = "Source import requires an open Tina project";
        return Tina::Core::success();
    }
    if (sourceImportService_.state() !=
        Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle) {
        authoringFeedback_ = "Source import is already running or awaiting Catalog commit";
        return Tina::Core::success();
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
        request.units.assign(intendedUnits.begin(), intendedUnits.end());
        request.selectedPathsUtf8 = std::move(selectedPathsUtf8);
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
    sourceImportUiRefreshPending_ = true;
    stageReservation.release();
    sourceImportCatalogCommitted_ = false;
    sourceImportLastFailed_ = false;
    counters_.sourceImportRunning = true;
    counters_.sourceImportReady = false;
    counters_.sourceImportStateCommitted = false;
    sourceImportObservedPhase_ =
        Tina::EditorApp::Detail::EditorSourceImportPhase::Preparing;
    ++counters_.sourceImportStarts;
    if (options_.profileUi) {
        const EditorProcessMemorySnapshot processMemory =
            queryEditorProcessMemory();
        if (!sourceImportProfileSeen_) {
            counters_.sourceImportProcessBefore = processMemory;
            counters_.sourceImportResidentCookedFileBytesBefore =
                assetResources_.system.has_value()
                    ? assetResources_.system->store().residentCookedFileBytes()
                    : 0U;
        }
        recordEditorProcessMemory(counters_, processMemory);
        recordEditorProcessMemoryMaximum(
            counters_.sourceImportProcessPeak, processMemory);
        sourceImportProfileSeen_ = true;
        sourceImportProfileActive_ = true;
        sourceImportProfileWorkerSampled_ = false;
        sourceImportProfileDeactivatePending_ = false;
    }
    authoringFeedback_ = hasSelectedPaths
                              ? "Source import is preparing the selected files in the background"
                              : "Source import is preparing the intended resource set";
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
    if (auto status = startSourceImport(*intendedUnits, {}); !status) {
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
    Tina::EditorApp::Detail::EditorSourceImportReadyStage& ready) -> Tina::Core::Status{
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

    if (!ready.catalog) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Source import worker did not retain its validated Catalog snapshot");
    }
    auto browser = prepareProjectBrowserForSnapshot(
        ready.catalog, previousFilter, previousSelection);
    if (!browser) {
        return Tina::Core::failure(std::move(browser.error()));
    }

    Tina::Asset::Sprite2DBindingRegistry* spriteParticipant =
        spriteBindings_.has_value() ? &*spriteBindings_ : nullptr;
    Tina::Asset::Mesh3DBindingRegistry* meshParticipant =
        mesh3DBindings_.has_value() ? &*mesh3DBindings_ : nullptr;
    Tina::Asset::CatalogReloadConfig reloadConfig{};
    if (spriteParticipant != nullptr) {
        reloadConfig.bindings.sprite2D =
            std::span<Tina::Asset::Sprite2DBindingRegistry*>{&spriteParticipant, 1U};
    }
    if (meshParticipant != nullptr) {
        reloadConfig.bindings.mesh3D =
            std::span<Tina::Asset::Mesh3DBindingRegistry*>{&meshParticipant, 1U};
    }
    auto reload = assetResources_.system->reloadPreparedCatalog(
        ready.stageRootUtf8, std::move(ready.catalog), reloadConfig);
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
        if (auto discard = sourceImportService_.discardReady(); !discard) {
            return discard;
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
    if (pendingProjectSwitch_.has_value()) {
        return Tina::Core::success();
    }
    if (!pendingSourceImportPathsUtf8_.empty()) {
        if (auto status = importSelectedSourceFiles(
                std::move(pendingSourceImportPathsUtf8_));
            !status) {
            return status;
        }
        pendingSourceImportPathsUtf8_.clear();
    }
    if (sourceImportStartPending_) {
        sourceImportStartPending_ = false;
        if (auto status = startSourceImport(sourceImportUnits_, {}); !status) {
            return status;
        }
    }
    if (auto status = sourceImportService_.poll(); !status) {
        return status;
    }
    using State = Tina::EditorApp::Detail::EditorSourceImportServiceState;
    counters_.sourceImportRunning = sourceImportService_.state() == State::Running;
    if (sourceImportService_.state() == State::Running) {
        using Phase = Tina::EditorApp::Detail::EditorSourceImportPhase;
        const Phase phase = sourceImportService_.phase();
        if (phase != sourceImportObservedPhase_) {
            sourceImportObservedPhase_ = phase;
            switch (phase) {
            case Phase::Preparing:
                authoringFeedback_ =
                    "Source import is validating and planning the selected batch";
                break;
            case Phase::Copying:
                authoringFeedback_ =
                    "Source import is copying external files into the Project in the background";
                break;
            case Phase::Cooking:
                authoringFeedback_ =
                    "Source import is cooking and validating a fresh Catalog stage";
                break;
            case Phase::Idle:
            case Phase::ReadyToCommit:
            case Phase::Failed:
                break;
            }
        }
    }
    if (sourceImportService_.state() != State::Idle) {
        sourceImportUiRefreshPending_ = true;
    }
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
        sourceImportObservedPhase_ =
            Tina::EditorApp::Detail::EditorSourceImportPhase::Failed;
        sourceImportLastFailed_ = true;
        sourceImportUiRefreshPending_ = true;
        if (options_.profileUi) {
            const EditorProcessMemorySnapshot processMemory =
                queryEditorProcessMemory();
            counters_.sourceImportProcessAfterCommit = processMemory;
            recordEditorProcessMemory(counters_, processMemory);
            recordEditorProcessMemoryMaximum(
                counters_.sourceImportProcessPeak, processMemory);
            sourceImportProfileDeactivatePending_ = true;
        }
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

    auto* ready = sourceImportService_.readyStage();
    if (ready == nullptr) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Source import service is Ready without a stage");
    }
    counters_.sourceImportRunning = false;
    counters_.sourceImportReady = true;
    sourceImportObservedPhase_ =
        Tina::EditorApp::Detail::EditorSourceImportPhase::ReadyToCommit;
    if (options_.profileUi && !sourceImportProfileWorkerSampled_) {
        const EditorProcessMemorySnapshot processMemory =
            queryEditorProcessMemory();
        counters_.sourceImportProcessAfterWorker = processMemory;
        recordEditorProcessMemory(counters_, processMemory);
        recordEditorProcessMemoryMaximum(
            counters_.sourceImportProcessPeak, processMemory);
        counters_.sourceImportCookedPayloadBytes =
            ready->statistics.cookedPayloadBytes;
        counters_.sourceImportTransientMemoryPeakBytes =
            (std::max)(counters_.sourceImportTransientMemoryPeakBytes,
                       ready->statistics.transientMemoryPeakBytes);
        counters_.sourceImportTransientMemoryBytesAfterRelease =
            ready->statistics.transientMemoryBytesAfterRelease;
        sourceImportProfileWorkerSampled_ = true;
    }
    if (!sourceImportCatalogCommitted_) {
        if (auto status = commitSourceImportCatalog(*ready); !status) {
            return status;
        }
        if (sourceImportService_.state() != State::Ready ||
            (ready->stageCreated && !sourceImportCatalogCommitted_)) {
            return Tina::Core::success();
        }
    }
    std::string completionFeedback;
    try {
        completionFeedback = "Source import complete: ";
        completionFeedback += std::to_string(ready->addedUnitCount);
        completionFeedback += " added, ";
        completionFeedback += std::to_string(ready->copiedFileCount);
        completionFeedback += " copied, ";
        completionFeedback += std::to_string(ready->reusedFileCount);
        completionFeedback += " reused, ";
        completionFeedback += std::to_string(ready->statistics.unitsRecooked);
        completionFeedback += " recooked unit(s)";
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Editor source import completion state allocation failed");
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
    counters_.sourceImportCookedPayloadBytes =
        ready->statistics.cookedPayloadBytes;
    counters_.sourceImportStateCommitted = true;
    counters_.sourceImportReady = false;
    sourceImportLastFailed_ = false;
    sourceImportUiRefreshPending_ = true;
    if (options_.profileUi) {
        const EditorProcessMemorySnapshot processMemory =
            queryEditorProcessMemory();
        counters_.sourceImportProcessAfterCommit = processMemory;
        counters_.sourceImportResidentCookedFileBytesAfterCommit =
            assetResources_.system.has_value()
                ? assetResources_.system->store().residentCookedFileBytes()
                : 0U;
        recordEditorProcessMemory(counters_, processMemory);
        recordEditorProcessMemoryMaximum(
            counters_.sourceImportProcessPeak, processMemory);
        sourceImportProfileDeactivatePending_ = true;
    }
    ++counters_.sourceImportCompletions;
    authoringFeedback_.swap(completionFeedback);
    if (!ready->stageCreated) {
        cleanupOwnedSourceImportStage(sourceImportPendingStageRootUtf8_);
    }
    auto committedUnits = sourceImportService_.commitReady();
    if (!committedUnits) {
        return Tina::Core::failure(std::move(committedUnits.error()));
    }
    sourceImportUnits_.swap(*committedUnits);
    counters_.sourceImportIntendedUnits = sourceImportUnits_.size();
    observedSourceImportSelectionIndex_.reset();
    sourceImportObservedPhase_ =
        Tina::EditorApp::Detail::EditorSourceImportPhase::Idle;
    sourceImportCatalogCommitted_ = false;
    sourceImportPendingStageRootUtf8_.clear();
    cleanupOwnedTemporaryProject(pendingTemporaryProjectCleanupRootUtf8_);
    return Tina::Core::success();
}

auto EditorWorkspaceState::resolveSourceImportRow(
    const void* state, u64 logicalRow,
    UI::UIDataGridRowDescriptor& output) noexcept -> bool
{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    if (self == nullptr || logicalRow >= self->sourceImportUnits_.size()) {
        return false;
    }
    output = UI::UIDataGridRowDescriptor{
        .key = 30'000U + logicalRow,
        .enabled = true,
    };
    return true;
}

auto EditorWorkspaceState::resolveSourceImportColumn(
    const void* state, u32 logicalColumn,
    UI::UIDataGridColumnDescriptor& output) noexcept -> bool
{
    if (state == nullptr || logicalColumn >= SourceImportColumnCapacity) {
        return false;
    }
    if (logicalColumn == 0U) {
        output = UI::UIDataGridColumnDescriptor{
            .key = 1U,
            .header = "Kind",
            .width = SourceImportKindColumnWidth,
        };
    } else if (logicalColumn == 1U) {
        output = UI::UIDataGridColumnDescriptor{
            .key = 2U,
            .header = "Source",
            .width = SourceImportSourceColumnWidth,
        };
    } else {
        output = UI::UIDataGridColumnDescriptor{
            .key = 3U,
            .header = "Status",
            .width = SourceImportStatusColumnWidth,
        };
    }
    return true;
}

auto EditorWorkspaceState::resolveSourceImportCell(
    const void* state, u64 logicalRow, u32 logicalColumn,
    UI::UIDataGridCellDescriptor& output) noexcept -> bool
{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    if (self == nullptr || logicalRow >= self->sourceImportUnits_.size() ||
        logicalColumn >= SourceImportColumnCapacity) {
        return false;
    }
    const auto& unit = self->sourceImportUnits_[
        static_cast<Tina::Core::usize>(logicalRow)];
    if (logicalColumn == 1U) {
        output = UI::UIDataGridCellDescriptor{.text = unit.sourcePathUtf8};
        return true;
    }
    if (logicalColumn == 2U) {
        using State = Tina::EditorApp::Detail::EditorSourceImportServiceState;
        std::string_view statusText = "Imported";
        if (self->pendingProjectSwitch_.has_value() ||
            !self->pendingSourceImportPathsUtf8_.empty() ||
            self->sourceImportStartPending_) {
            statusText = "Queued";
        } else if (self->sourceImportService_.state() == State::Running) {
            using Phase = Tina::EditorApp::Detail::EditorSourceImportPhase;
            switch (self->sourceImportService_.phase()) {
            case Phase::Preparing:
                statusText = "Preparing";
                break;
            case Phase::Copying:
                statusText = "Copying";
                break;
            case Phase::Cooking:
                statusText = "Cooking";
                break;
            case Phase::Idle:
            case Phase::ReadyToCommit:
            case Phase::Failed:
                statusText = "Importing";
                break;
            }
        } else if (self->sourceImportService_.state() == State::Ready) {
            statusText = "Committing";
        } else if (self->sourceImportLastFailed_) {
            statusText = "Failed";
        } else if (!self->assetResources_.projectCatalogConfigured &&
                   !self->temporaryProjectActive()) {
            statusText = "Waiting";
        }
        output = UI::UIDataGridCellDescriptor{.text = statusText};
        return true;
    }

    std::string_view kind{};
    switch (unit.kind) {
    case Tina::EditorApp::Detail::EditorSourceImportUnitKind::CatalogRecipe:
        kind = "Catalog";
        break;
    case Tina::EditorApp::Detail::EditorSourceImportUnitKind::Gltf:
        kind = "glTF";
        break;
    case Tina::EditorApp::Detail::EditorSourceImportUnitKind::Texture:
        kind = "Texture";
        break;
    case Tina::EditorApp::Detail::EditorSourceImportUnitKind::Audio:
        kind = "Audio";
        break;
    }
    output = UI::UIDataGridCellDescriptor{.text = kind};
    return !kind.empty();
}

} // namespace Tina::EditorApp::WorkspaceInternal
