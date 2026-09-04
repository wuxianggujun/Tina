#include "EditorWorkspaceState.hpp"

#include "EditorSourceImportSelection.hpp"

#include <tina/render/RenderErrors.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/ui/UIErrors.hpp>

namespace Tina::EditorApp::WorkspaceInternal {
bool EditorWorkspaceState::isFatalSourceImportError(
    const Tina::Core::Error& error) noexcept
{
    const bool lifecycleInvariant =
        error.code == Tina::Core::CoreErrorCode::InvalidArgument &&
        (error.message.find("owner thread") != std::string::npos ||
         error.message.find("requires Idle state") != std::string::npos ||
         error.message.find("requires Ready state") != std::string::npos ||
         error.message.find("requires Failed state") != std::string::npos ||
         error.message.find("requires Running state") != std::string::npos);
    return error.code == Tina::Core::CoreErrorCode::OutOfMemory ||
           error.code == Tina::Core::CoreErrorCode::Internal ||
           error.code == Tina::Asset::AssetErrorCode::AllocationFailed ||
           error.code == Tina::Asset::AssetErrorCode::WrongOwnerThread ||
           error.code == Tina::Render::RenderErrorCode::WrongOwnerThread ||
           error.code == Tina::RuntimeErrorCode::WrongOwnerThread ||
           error.code == Tina::UI::UIErrorCode::WrongOwnerThread ||
           lifecycleInvariant;
}

EditorAssetImportStatus EditorWorkspaceState::assetImportStatusForPhase(
    Tina::EditorApp::Detail::EditorSourceImportPhase phase) noexcept
{
    using Phase = Tina::EditorApp::Detail::EditorSourceImportPhase;
    switch (phase) {
    case Phase::Preparing:
        return EditorAssetImportStatus::Preparing;
    case Phase::Copying:
        return EditorAssetImportStatus::Copying;
    case Phase::Cooking:
        return EditorAssetImportStatus::Cooking;
    case Phase::ReadyToCommit:
        return EditorAssetImportStatus::ReadyToCommit;
    case Phase::Failed:
        return EditorAssetImportStatus::Error;
    case Phase::Idle:
    default:
        return EditorAssetImportStatus::Ready;
    }
}

EditorAssetImportStatus EditorWorkspaceState::projectAssetImportStatus(
    Tina::Core::AssetId assetId) const noexcept
{
    if (!assetId) {
        return EditorAssetImportStatus::Ready;
    }
    const auto found = std::find_if(
        assetImportHistory_.begin(), assetImportHistory_.end(),
        [assetId](const EditorAssetImportHistoryEntry& entry) {
            return entry.assetId == assetId;
        });
    return found != assetImportHistory_.end()
               ? found->status
               : EditorAssetImportStatus::Ready;
}

std::string_view EditorWorkspaceState::projectAssetImportStatusLabel(
    Tina::Core::AssetId assetId) const noexcept
{
    switch (projectAssetImportStatus(assetId)) {
    case EditorAssetImportStatus::Queued:
        return "Queued";
    case EditorAssetImportStatus::Preparing:
        return "Preparing";
    case EditorAssetImportStatus::Copying:
        return "Copying";
    case EditorAssetImportStatus::Cooking:
        return "Cooking";
    case EditorAssetImportStatus::ReadyToCommit:
        return "Ready";
    case EditorAssetImportStatus::Committed:
        return "Ready";
    case EditorAssetImportStatus::Error:
        return "Error";
    case EditorAssetImportStatus::Ready:
    default:
        return "Ready";
    }
}

void EditorWorkspaceState::setAssetImportHistory(
    std::span<const Tina::Asset::SourceImportPipelineUnitOutput> outputs,
    EditorAssetImportStatus status) noexcept
{
    try {
        for (const auto& unitOutput : outputs) {
            for (const auto& output : unitOutput.outputs) {
                if (!output.assetId) {
                    continue;
                }
                const auto found = std::find_if(
                    assetImportHistory_.begin(), assetImportHistory_.end(),
                    [&output](const EditorAssetImportHistoryEntry& entry) {
                        return entry.assetId == output.assetId;
                    });
                if (found != assetImportHistory_.end()) {
                    found->status = status;
                } else {
                    assetImportHistory_.push_back({
                        .assetId = output.assetId,
                        .status = status,
                    });
                }
            }
        }
    } catch (const std::bad_alloc&) {
        // History is a UI projection. A failed best-effort update must not
        // interrupt the transactional import that owns the Catalog snapshot.
    }
}

void EditorWorkspaceState::setCurrentImportAssetHistory(
    EditorAssetImportStatus status) noexcept
{
    for (const Tina::Core::AssetId assetId : activeAssetImportIds_) {
        const auto found = std::find_if(
            assetImportHistory_.begin(), assetImportHistory_.end(),
            [assetId](const EditorAssetImportHistoryEntry& entry) {
                return entry.assetId == assetId;
            });
        if (found != assetImportHistory_.end()) {
            found->status = status;
        }
    }
}

void EditorWorkspaceState::clearAssetImportHistory() noexcept
{
    assetImportHistory_.clear();
    activeAssetImportIds_.clear();
}

void EditorWorkspaceState::pruneAssetImportHistoryToCatalog() noexcept
{
    if (!assetResources_.system.has_value() ||
        assetResources_.system->catalog() == nullptr) {
        clearAssetImportHistory();
        return;
    }
    const Tina::Asset::CatalogSnapshot& catalog =
        *assetResources_.system->catalog();
    assetImportHistory_.erase(
        std::remove_if(
            assetImportHistory_.begin(), assetImportHistory_.end(),
            [&catalog](const EditorAssetImportHistoryEntry& entry) {
                return !catalog.find(entry.assetId).has_value();
            }),
        assetImportHistory_.end());
}

auto EditorWorkspaceState::startSourceImport(
    std::span<const Tina::EditorApp::Detail::EditorSourceImportUnit> intendedUnits,
    std::vector<std::string> selectedPathsUtf8) -> Tina::Core::Status
{
    std::vector<Tina::EditorApp::Detail::EditorSourceImportUnit> retryUnits;
    std::vector<std::string> retryPathsUtf8;
    const auto recoverStartFailure =
        [this, &retryUnits, &retryPathsUtf8](
            std::string_view prefix, Tina::Core::Error error)
        -> Tina::Core::Status {
        if (isFatalSourceImportError(error)) {
            return Tina::Core::failure(std::move(error));
        }
        // A source-file rename is an ingress transaction owned by the import
        // request. If staging/cache/worker launch fails before the worker can
        // commit a new Catalog, restore the old path before exposing Retry.
        // Retry must target the restored unit list, otherwise the old Catalog
        // and the filesystem would disagree about the source path.
        bool retryMatchesFilesystem = true;
        if (pendingProjectAssetSourceRename_.has_value() &&
            !sourceImportCatalogCommitted_) {
            if (auto rollback = rollbackProjectAssetSourceRename(); !rollback) {
                if (isFatalSourceImportError(rollback.error())) {
                    return rollback;
                }
                retryMatchesFilesystem = false;
                if (auto feedback = reportAuthoringFailure(
                        "Source import failed and source-file rollback is pending: ",
                        rollback.error()); !feedback) {
                    return feedback;
                }
            } else {
                try {
                    retryUnits = sourceImportUnits_;
                } catch (const std::bad_alloc&) {
                    return Tina::Core::failure(
                        Tina::Core::CoreErrorCode::OutOfMemory,
                        "Editor could not retain the restored source-import retry request");
                }
            }
        }
        if (auto feedback = reportAuthoringFailure(prefix, error); !feedback) {
            return feedback;
        }
        ++counters_.sourceImportFailures;
        counters_.sourceImportRunning = false;
        counters_.sourceImportReady = false;
        sourceImportObservedPhase_ =
            Tina::EditorApp::Detail::EditorSourceImportPhase::Failed;
        sourceImportCatalogCommitted_ = false;
        sourceImportLastFailed_ = true;
        sourceImportUiRefreshPending_ = true;
        try {
            sourceImportFailureMessageUtf8_ = error.message;
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Editor could not retain source import failure feedback");
        }
        if (fileDropFeedbackState_ != FileDropFeedbackState::Hidden) {
            setFileDropFeedback(
                FileDropFeedbackState::Failed,
                sourceImportFailureMessageUtf8_.empty()
                    ? std::string_view{"Source import could not start"}
                    : std::string_view{sourceImportFailureMessageUtf8_});
        }
        try {
            if (retryMatchesFilesystem) {
                sourceImportRetryUnits_ = retryUnits;
                sourceImportRetryPathsUtf8_ = retryPathsUtf8;
            } else {
                sourceImportRetryUnits_.clear();
                sourceImportRetryPathsUtf8_.clear();
            }
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Editor could not retain the source-import retry request");
        }
        return Tina::Core::success();
    };
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
    activeAssetImportIds_.clear();
    sourceImportPointerPathUtf8_.clear();
    sourceImportPendingStageRootUtf8_.clear();
    try {
        retryUnits.assign(intendedUnits.begin(), intendedUnits.end());
        retryPathsUtf8 = selectedPathsUtf8;
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Editor could not retain the source-import retry request");
    }

    auto stagePaths = createSourceImportStagePaths(*activeProjectWorkspace_);
    if (!stagePaths) {
        return recoverStartFailure(
            "Source import staging failed: ", std::move(stagePaths.error()));
    }
    auto stageReservation = Tina::Core::makeScopeExit([this, &stagePaths]() noexcept {
        cleanupOwnedSourceImportStage(stagePaths->catalogRootUtf8);
        sourceImportPendingStageRootUtf8_.clear();
        sourceImportPointerPathUtf8_.clear();
    });
    auto cache = ensureSourceImportCache(*activeProjectWorkspace_);
    if (!cache) {
        return recoverStartFailure(
            "Source import cache preparation failed: ",
            std::move(cache.error()));
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

        // Existing unit mappings are the only authoritative way to know which
        // AssetIds a reimport will touch before the worker returns. New output
        // ids are added from the validated stage in updateSourceImport().
        for (const auto& intendedUnit : intendedUnits) {
            for (const auto& mapping : sourceImportUnitOutputs_) {
                if (mapping.sourceUtf8Path != intendedUnit.sourcePathUtf8) {
                    continue;
                }
                setAssetImportHistory(
                    std::span<const Tina::Asset::SourceImportPipelineUnitOutput>{
                        &mapping, 1U},
                    EditorAssetImportStatus::Queued);
                for (const auto& output : mapping.outputs) {
                    if (!output.assetId ||
                        std::find(activeAssetImportIds_.begin(),
                                  activeAssetImportIds_.end(), output.assetId) !=
                            activeAssetImportIds_.end()) {
                        continue;
                    }
                    activeAssetImportIds_.push_back(output.assetId);
                }
            }
        }
    } catch (const std::bad_alloc&) {
        activeAssetImportIds_.clear();
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Editor could not retain the source-import request");
    }

    if (auto status = sourceImportService_.start(std::move(request)); !status) {
        activeAssetImportIds_.clear();
        return recoverStartFailure(
            "Source import could not start: ", std::move(status.error()));
    }
    sourceImportRetryUnits_.swap(retryUnits);
    sourceImportRetryPathsUtf8_.swap(retryPathsUtf8);
    sourceImportUiRefreshPending_ = true;
    stageReservation.release();
    sourceImportCatalogCommitted_ = false;
    previewAssetBindingsFailureFeedbackPending_ = false;
    sourceImportLastFailed_ = false;
    sourceImportFailureMessageUtf8_.clear();
    counters_.sourceImportRunning = true;
    counters_.sourceImportReady = false;
    counters_.sourceImportStateCommitted = false;
    sourceImportObservedPhase_ =
        Tina::EditorApp::Detail::EditorSourceImportPhase::Preparing;
    setCurrentImportAssetHistory(EditorAssetImportStatus::Preparing);
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
    if (sourceImportService_.state() ==
        Tina::EditorApp::Detail::EditorSourceImportServiceState::Running) {
        authoringFeedback_ = removesFinalUnit
            ? "Removing the final source import and publishing an empty Catalog"
            : "Rebuilding the Catalog without the selected source import";
    }
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
    // After reloadPreparedCatalog() succeeds this stage is live. A later
    // pointer/state publish failure must not let generic failure cleanup delete
    // the Catalog that AssetSystem is currently serving.
    if (sourceImportCatalogCommitted_) {
        sourceImportPendingStageRootUtf8_.clear();
        return;
    }
    cleanupOwnedSourceImportStage(sourceImportPendingStageRootUtf8_);
    sourceImportPendingStageRootUtf8_.clear();
}

auto EditorWorkspaceState::rollbackProjectAssetSourceRename() noexcept
    -> Tina::Core::Status
{
    if (!pendingProjectAssetSourceRename_.has_value()) {
        return Tina::Core::success();
    }
    if (auto status = pendingProjectAssetSourceRename_->rollback(); !status) {
        return status;
    }
    pendingProjectAssetSourceRename_.reset();
    return Tina::Core::success();
}


auto EditorWorkspaceState::publishCommittedSourceImportState(
    const Tina::EditorApp::Detail::EditorSourceImportReadyStage& ready) -> Tina::Core::Status
{
    if (!ready.stageCreated) {
        return Tina::Core::success();
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
    if (sourceImportPointerPathUtf8_.empty()) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Source import committed without an active Catalog pointer path");
    }
    const auto pointerBytes = std::as_bytes(std::span{
        ready.stageRootUtf8.data(), ready.stageRootUtf8.size()});
    if (auto status = Tina::Core::writeFile(
            sourceImportPointerPathUtf8_, pointerBytes);
        !status) {
        return status;
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
    if (sourceImportSupersededCatalogRootUtf8_ !=
        assetResources_.sourceImportCatalogRootUtf8) {
        cleanupOwnedSourceImportStage(sourceImportSupersededCatalogRootUtf8_);
    }
    sourceImportSupersededCatalogRootUtf8_.clear();
    if (sourceImportSupersededAuthoringCatalogRootUtf8_ !=
        assetResources_.sourceImportCatalogRootUtf8) {
        cleanupOwnedAuthoringStage(sourceImportSupersededAuthoringCatalogRootUtf8_);
    }
    sourceImportSupersededAuthoringCatalogRootUtf8_.clear();
    return Tina::Core::success();
}

auto EditorWorkspaceState::commitSourceImportCatalog(
    Tina::EditorApp::Detail::EditorSourceImportReadyStage& ready) -> Tina::Core::Status{
    const auto recoverReadyFailure =
        [this](std::string_view prefix, Tina::Core::Error error)
        -> Tina::Core::Status {
        if (isFatalSourceImportError(error)) {
            return Tina::Core::failure(std::move(error));
        }
        if (auto status = reportAuthoringFailure(prefix, error); !status) {
            return status;
        }
        setCurrentImportAssetHistory(EditorAssetImportStatus::Error);
        ++counters_.sourceImportFailures;
        counters_.sourceImportRunning = false;
        counters_.sourceImportReady = false;
        sourceImportLastFailed_ = true;
        sourceImportCatalogCommitted_ = false;
        sourceImportUiRefreshPending_ = true;
        try {
            sourceImportFailureMessageUtf8_ = error.message;
        } catch (const std::bad_alloc&) {
            sourceImportFailureMessageUtf8_.clear();
        }
        if (fileDropFeedbackState_ == FileDropFeedbackState::Processing) {
            setFileDropFeedback(
                FileDropFeedbackState::Failed,
                sourceImportFailureMessageUtf8_.empty()
                    ? std::string_view{"Source import could not be committed; previous Catalog preserved"}
                    : std::string_view{sourceImportFailureMessageUtf8_});
        }
        cleanupFailedSourceImportStage();
        const bool hadSourceRename = pendingProjectAssetSourceRename_.has_value();
        bool sourceRenameRestored = !hadSourceRename;
        if (auto rollback = rollbackProjectAssetSourceRename(); !rollback) {
            if (isFatalSourceImportError(rollback.error())) {
                return rollback;
            }
            if (auto feedback = reportAuthoringFailure(
                    "Source import failed; source-file rollback is pending: ",
                    rollback.error()); !feedback) {
                return feedback;
            }
            sourceImportRetryUnits_.clear();
            sourceImportRetryPathsUtf8_.clear();
        } else {
            sourceRenameRestored = true;
        }
        if (hadSourceRename && sourceRenameRestored) {
            try {
                // The rename transaction restored the old filesystem path.
                // Retry must use the same path as the preserved Catalog.
                sourceImportRetryUnits_ = sourceImportUnits_;
                sourceImportRetryPathsUtf8_.clear();
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Editor could not retain the restored source-import retry request");
            }
        }
        if (auto discard = sourceImportService_.discardReady(); !discard) {
            return discard;
        }
        return Tina::Core::success();
    };
    if (!ready.stageCreated) {
        return Tina::Core::success();
    }
    if (previewAssetBindingsHaveActiveFrameBorrows()) {
        ++counters_.sourceImportBusyRetries;
        authoringFeedback_ =
            "Source import is ready; waiting for the previous preview frame to finish";
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
        return recoverReadyFailure(
            "Source import could not prepare Catalog documents: ",
            std::move(candidateTabs.error()));
    }

    const auto previousFilter = projectAssets_.typeFilter();
    const std::string_view previousSearchQuery = projectAssets_.searchQuery();
    std::optional<Tina::Core::AssetId> previousSelection{};
    previousSelection = projectAssets_.selectedAssetId();
    std::string nextCatalogRoot;
    std::string nextSourceImportCatalogRoot;
    std::string nextStatePath;
    std::string previousSourceImportCatalogRoot;
    std::string previousAuthoringCatalogRoot;
    std::vector<Tina::Asset::SourceImportPipelineUnitOutput> nextUnitOutputs;
    try {
        nextCatalogRoot = ready.stageRootUtf8;
        nextSourceImportCatalogRoot = ready.stageRootUtf8;
        nextStatePath = ready.statePathUtf8;
        previousSourceImportCatalogRoot = assetResources_.sourceImportCatalogRootUtf8;
        previousAuthoringCatalogRoot = assetResources_.authoringCatalogRootUtf8;
        nextUnitOutputs = ready.unitOutputs;
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
        ready.catalog, previousFilter, previousSelection, previousSearchQuery,
        nextUnitOutputs, assetMetadata_,
        activeProjectWorkspace_.has_value()
            ? activeProjectWorkspace_->sourceRootUtf8()
            : std::string_view{});
    if (!browser) {
        return recoverReadyFailure(
            "Source import Browser rebuild failed; previous Catalog preserved: ",
            std::move(browser.error()));
    }

    Tina::Asset::Sprite2DBindingRegistry* spriteParticipant =
        spriteBindings_.has_value() ? &*spriteBindings_ : nullptr;
    Tina::Asset::Mesh3DBindingRegistry* meshParticipant =
        mesh3DBindings_.has_value() ? &*mesh3DBindings_ : nullptr;
    Tina::Asset::ShaderBindingRegistry* shaderParticipant =
        shaderBindings_.has_value() ? &*shaderBindings_ : nullptr;
    Tina::Asset::CatalogReloadConfig reloadConfig{};
    if (spriteParticipant != nullptr) {
        reloadConfig.bindings.sprite2D =
            std::span<Tina::Asset::Sprite2DBindingRegistry*>{&spriteParticipant, 1U};
    }
    if (meshParticipant != nullptr) {
        reloadConfig.bindings.mesh3D =
            std::span<Tina::Asset::Mesh3DBindingRegistry*>{&meshParticipant, 1U};
    }
    if (shaderParticipant != nullptr) {
        reloadConfig.bindings.shader =
            std::span<Tina::Asset::ShaderBindingRegistry*>{&shaderParticipant, 1U};
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
        if (isFatalSourceImportError(reload.error())) {
            return Tina::Core::failure(std::move(reload.error()));
        }
        return recoverReadyFailure(
            "Source import reload failed; previous Catalog preserved: ",
            std::move(reload.error()));
    }
    sourceImportCatalogCommitted_ = true;

    const auto* committedCatalog = assetResources_.system->catalog();
    if (committedCatalog == nullptr) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Source import committed without an AssetSystem Catalog snapshot");
    }
    ++counters_.sourceImportCatalogReloads;
    bool documentRefreshPending = false;
    if (auto status = switchCatalogAuthoringOwnersToPinnedTabs(); !status) {
        if (isFatalSourceImportError(status.error())) {
            return status;
        }
        if (auto feedback = reportAuthoringFailure(
                "Catalog committed, but document ownership refresh failed: ",
                status.error()); !feedback) {
            return feedback;
        }
        documentRefreshPending = true;
    }
    if (auto status = refreshPinnedCatalogAuthoringDocuments(*browser); !status) {
        if (isFatalSourceImportError(status.error())) {
            return status;
        }
        if (auto feedback = reportAuthoringFailure(
                "Catalog committed, but open document refresh failed: ",
                status.error()); !feedback) {
            return feedback;
        }
        documentRefreshPending = true;
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
        counters_.runtimePreviewValid = false;
        previewAssetBindingsRefreshPending_ = true;
        previewAssetBindingsFailureFeedbackPending_ = true;
        if (isFatalSourceImportError(status.error())) {
            return status;
        }
        if (auto feedback = reportAuthoringFailure(
                "Catalog committed, but runtime preview refresh is pending: ",
                status.error()); !feedback) {
            return feedback;
        }
        authoringFeedback_ += " The Editor remains usable; preview will retry.";
    }
    projectAssets_ = std::move(*browser);
    sourceImportUnitOutputs_ = std::move(nextUnitOutputs);
    commitProjectSwitchDocumentTabs(std::move(*candidateTabs));
    observedProjectAssetSelectionIndex_.reset();
    projectBrowserUiRefreshPending_ = true;
    if (counters_.runtimePreviewValid) {
        previewAssetBindingsRefreshPending_ = false;
    }
    if (documentRefreshPending) {
        catalogRefreshPending_ = true;
        authoringFeedback_ = previewAssetBindingsRefreshPending_
            ? "Catalog committed; documents and runtime preview will retry"
            : "Catalog committed; open documents will retry against the live Catalog";
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::updateSourceImport() -> Tina::Core::Status{
    if (pendingProjectSwitch_.has_value()) {
        return Tina::Core::success();
    }
    if (sourceImportLastFailed_ &&
        pendingProjectAssetSourceRename_.has_value() &&
        sourceImportService_.state() ==
            Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle) {
        if (auto rollback = rollbackProjectAssetSourceRename(); !rollback) {
            if (isFatalSourceImportError(rollback.error())) {
                return rollback;
            }
            if (auto feedback = reportAuthoringFailure(
                    "Source import failed; source-file rollback will retry: ",
                    rollback.error()); !feedback) {
                return feedback;
            }
            sourceImportUiRefreshPending_ = true;
            return Tina::Core::success();
        }
        try {
            sourceImportRetryUnits_ = sourceImportUnits_;
            sourceImportRetryPathsUtf8_.clear();
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Editor could not retain the restored source-import retry request");
        }
        sourceImportUiRefreshPending_ = true;
    }
    if (cancelSourceImportPending_) {
        cancelSourceImportPending_ = false;
        using State =
            Tina::EditorApp::Detail::EditorSourceImportServiceState;
        if (sourceImportService_.state() == State::Running) {
            if (auto status = sourceImportService_.cancel(); !status) {
                return status;
            }
            cleanupOwnedSourceImportStage(sourceImportPendingStageRootUtf8_);
            sourceImportPendingStageRootUtf8_.clear();
            sourceImportPointerPathUtf8_.clear();
            bool sourceRenameRollbackPending = false;
            if (auto rollback = rollbackProjectAssetSourceRename(); !rollback) {
                if (isFatalSourceImportError(rollback.error())) {
                    return rollback;
                }
                if (auto feedback = reportAuthoringFailure(
                        "Import cancelled; source-file rollback is pending: ",
                        rollback.error()); !feedback) {
                    return feedback;
                }
                sourceRenameRollbackPending = true;
                sourceImportUiRefreshPending_ = true;
            }
            setCurrentImportAssetHistory(EditorAssetImportStatus::Committed);
            activeAssetImportIds_.clear();
            pruneAssetImportHistoryToCatalog();
            sourceImportRetryUnits_.clear();
            sourceImportRetryPathsUtf8_.clear();
            sourceImportObservedPhase_ =
                Tina::EditorApp::Detail::EditorSourceImportPhase::Idle;
            sourceImportCatalogCommitted_ = false;
            sourceImportLastFailed_ = sourceRenameRollbackPending;
            if (!sourceRenameRollbackPending) {
                sourceImportFailureMessageUtf8_.clear();
            }
            counters_.sourceImportRunning = false;
            counters_.sourceImportReady = false;
            sourceImportUiRefreshPending_ = true;
            authoringFeedback_ = sourceRenameRollbackPending
                ? "Import cancelled; source-file rollback is pending"
                : "Source import cancelled; previous Catalog preserved";
            if (fileDropFeedbackState_ ==
                FileDropFeedbackState::Processing) {
                setFileDropFeedback(
                    FileDropFeedbackState::Rejected,
                    "Import cancelled; previous Catalog preserved");
            }
        }
    }
    if (!pendingSourceImportPathsUtf8_.empty()) {
        retrySourceImportPending_ = false;
        if (auto status = importSelectedSourceFiles(
                std::move(pendingSourceImportPathsUtf8_));
            !status) {
            return status;
        }
        pendingSourceImportPathsUtf8_.clear();
    }
    if (retrySourceImportPending_) {
        retrySourceImportPending_ = false;
        if (sourceImportLastFailed_ &&
            (!sourceImportRetryUnits_.empty() ||
             !sourceImportRetryPathsUtf8_.empty())) {
            std::vector<Tina::EditorApp::Detail::EditorSourceImportUnit>
                retryUnits;
            std::vector<std::string> retryPathsUtf8;
            try {
                retryUnits = sourceImportRetryUnits_;
                retryPathsUtf8 = sourceImportRetryPathsUtf8_;
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Editor could not retain the source-import retry request");
            }
            if (auto status = startSourceImport(
                    retryUnits, std::move(retryPathsUtf8));
                !status) {
                return status;
            }
        }
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
            setCurrentImportAssetHistory(assetImportStatusForPhase(phase));
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
        setCurrentImportAssetHistory(EditorAssetImportStatus::Error);
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
        try {
            sourceImportFailureMessageUtf8_ = failure->message;
        } catch (const std::bad_alloc&) {
            // Failure cleanup and service dismissal must still complete. The
            // persistent callout falls back to its generic rollback text.
            sourceImportFailureMessageUtf8_.clear();
        }
        if (fileDropFeedbackState_ == FileDropFeedbackState::Processing) {
            setFileDropFeedback(
                FileDropFeedbackState::Failed,
                sourceImportFailureMessageUtf8_.empty()
                    ? std::string_view{"Source import failed; the previous Catalog was preserved"}
                    : std::string_view{sourceImportFailureMessageUtf8_});
        }
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
        const bool hadSourceRename = pendingProjectAssetSourceRename_.has_value();
        bool sourceRenameRestored = !hadSourceRename;
        if (auto rollback = rollbackProjectAssetSourceRename(); !rollback) {
            if (isFatalSourceImportError(rollback.error())) {
                return rollback;
            }
            if (auto feedback = reportAuthoringFailure(
                    "Source import failed; source-file rollback is pending: ",
                    rollback.error()); !feedback) {
                return feedback;
            }
            sourceImportRetryUnits_.clear();
            sourceImportRetryPathsUtf8_.clear();
        } else {
            sourceRenameRestored = true;
        }
        if (hadSourceRename && sourceRenameRestored) {
            try {
                sourceImportRetryUnits_ = sourceImportUnits_;
                sourceImportRetryPathsUtf8_.clear();
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Editor could not retain the restored source-import retry request");
            }
        }
        if (isFatalSourceImportError(*failure)) {
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
    setAssetImportHistory(
        std::span<const Tina::Asset::SourceImportPipelineUnitOutput>{
            ready->unitOutputs.data(), ready->unitOutputs.size()},
        EditorAssetImportStatus::ReadyToCommit);
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
            setCurrentImportAssetHistory(EditorAssetImportStatus::Error);
            setAssetImportHistory(
                std::span<const Tina::Asset::SourceImportPipelineUnitOutput>{
                    ready->unitOutputs.data(), ready->unitOutputs.size()},
                EditorAssetImportStatus::Error);
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
        setCurrentImportAssetHistory(EditorAssetImportStatus::Error);
        setAssetImportHistory(
            std::span<const Tina::Asset::SourceImportPipelineUnitOutput>{
                ready->unitOutputs.data(), ready->unitOutputs.size()},
            EditorAssetImportStatus::Error);
        if (fileDropFeedbackState_ == FileDropFeedbackState::Processing) {
            setFileDropFeedback(
                FileDropFeedbackState::Failed,
                status.error().message);
        }
        if (isFatalSourceImportError(status.error())) {
            return status;
        }
        if (auto feedback = reportAuthoringFailure(
                "Imported Catalog is live; import state commit will retry: ",
                status.error()); !feedback) {
            return feedback;
        }
        sourceImportLastFailed_ = true;
        sourceImportUiRefreshPending_ = true;
        try {
            sourceImportFailureMessageUtf8_ = status.error().message;
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Editor could not retain source import state-publish feedback");
        }
        return Tina::Core::success();
    }

    const bool recoveredStatePublishFailure = sourceImportLastFailed_;
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
    sourceImportFailureMessageUtf8_.clear();
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
    if (fileDropFeedbackState_ == FileDropFeedbackState::Processing ||
        (recoveredStatePublishFailure &&
         fileDropFeedbackState_ == FileDropFeedbackState::Failed)) {
        if (previewAssetBindingsRefreshPending_) {
            setFileDropFeedback(
                FileDropFeedbackState::Failed,
                "Catalog committed; runtime preview refresh is pending");
        } else {
            setFileDropFeedback(
                FileDropFeedbackState::Committed,
                completionFeedback);
        }
    }
    if (!previewAssetBindingsRefreshPending_) {
        authoringFeedback_.swap(completionFeedback);
    }
    if (!ready->stageCreated) {
        cleanupOwnedSourceImportStage(sourceImportPendingStageRootUtf8_);
    }
    try {
        sourceImportUnitOutputs_ = ready->unitOutputs;
    } catch (const std::bad_alloc&) {
        if (fileDropFeedbackState_ != FileDropFeedbackState::Hidden) {
            setFileDropFeedback(
                FileDropFeedbackState::Failed,
                "Catalog committed, but the Editor could not retain output mappings");
        }
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Editor could not retain source import output mappings");
    }
    setCurrentImportAssetHistory(EditorAssetImportStatus::Committed);
    setAssetImportHistory(
        std::span<const Tina::Asset::SourceImportPipelineUnitOutput>{
            ready->unitOutputs.data(), ready->unitOutputs.size()},
        EditorAssetImportStatus::Committed);
    auto committedUnits = sourceImportService_.commitReady();
    if (!committedUnits) {
        setCurrentImportAssetHistory(EditorAssetImportStatus::Error);
        if (fileDropFeedbackState_ == FileDropFeedbackState::Processing) {
            setFileDropFeedback(
                FileDropFeedbackState::Failed,
                committedUnits.error().message);
        }
        if (isFatalSourceImportError(committedUnits.error())) {
            return Tina::Core::failure(std::move(committedUnits.error()));
        }
        if (auto feedback = reportAuthoringFailure(
                "Imported Catalog is live; service finalization will retry: ",
                committedUnits.error()); !feedback) {
            return feedback;
        }
        sourceImportLastFailed_ = true;
        counters_.sourceImportReady = true;
        sourceImportUiRefreshPending_ = true;
        return Tina::Core::success();
    }
    sourceImportUnits_.swap(*committedUnits);
    if (pendingProjectAssetSourceRename_.has_value()) {
        pendingProjectAssetSourceRename_->commit();
        pendingProjectAssetSourceRename_.reset();
    }
    counters_.sourceImportIntendedUnits = sourceImportUnits_.size();
    observedSourceImportSelectionIndex_.reset();
    sourceImportObservedPhase_ =
        Tina::EditorApp::Detail::EditorSourceImportPhase::Idle;
    activeAssetImportIds_.clear();
    sourceImportRetryUnits_.clear();
    sourceImportRetryPathsUtf8_.clear();
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
        for (const auto& mapping : self->sourceImportUnitOutputs_) {
            if (mapping.sourceUtf8Path != unit.sourcePathUtf8) {
                continue;
            }
            for (const auto& imported : mapping.outputs) {
                const EditorAssetImportStatus assetStatus =
                    self->projectAssetImportStatus(imported.assetId);
                if (assetStatus == EditorAssetImportStatus::Committed) {
                    output = UI::UIDataGridCellDescriptor{.text = "Imported"};
                    return true;
                }
                if (assetStatus != EditorAssetImportStatus::Ready) {
                    output = UI::UIDataGridCellDescriptor{
                        .text = self->projectAssetImportStatusLabel(
                            imported.assetId)};
                    return true;
                }
            }
            break;
        }
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
