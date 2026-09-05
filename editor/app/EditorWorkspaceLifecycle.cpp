#include "EditorWorkspaceState.hpp"

#include <tina/core/trace/Trace.hpp>
#include <tina/ui/UIErrors.hpp>

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

[[nodiscard]] constexpr char asciiLower(char value) noexcept
{
    return value >= 'A' && value <= 'Z'
               ? static_cast<char>(value + ('a' - 'A'))
               : value;
}

[[nodiscard]] bool containsAsciiCaseInsensitive(
    std::string_view text, std::string_view needle) noexcept
{
    if (needle.empty()) {
        return true;
    }
    if (needle.size() > text.size()) {
        return false;
    }
    for (Tina::Core::usize offset = 0;
         offset + needle.size() <= text.size(); ++offset) {
        bool matches = true;
        for (Tina::Core::usize index = 0; index < needle.size(); ++index) {
            if (asciiLower(text[offset + index]) != asciiLower(needle[index])) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] UI::UISnackbarTone snackbarToneFor(
    std::string_view feedback) noexcept
{
    for (const std::string_view word :
         {"failed", "rejected", "error", "invalid", "cannot"}) {
        if (containsAsciiCaseInsensitive(feedback, word)) {
            return UI::UISnackbarTone::Error;
        }
    }
    for (const std::string_view word :
         {"cancel", "unchanged", "preserved", "retry", "stop"}) {
        if (containsAsciiCaseInsensitive(feedback, word)) {
            return UI::UISnackbarTone::Warning;
        }
    }
    for (const std::string_view word : {"ready", "selected", "preview"}) {
        if (containsAsciiCaseInsensitive(feedback, word)) {
            return UI::UISnackbarTone::Neutral;
        }
    }
    return UI::UISnackbarTone::Success;
}

[[nodiscard]] bool logicalPointInRect(const UI::UILogicalRect& rect,
                                      double x, double y) noexcept
{
    return std::isfinite(x) && std::isfinite(y) &&
           x >= static_cast<double>(rect.x) &&
           y >= static_cast<double>(rect.y) &&
           x < static_cast<double>(rect.x + rect.width) &&
           y < static_cast<double>(rect.y + rect.height);
}

inline constexpr double FileDropFeedbackDismissSeconds = 4.0;

} // namespace

void EditorWorkspaceState::setFileDropFeedback(
    FileDropFeedbackState state, std::string_view message) noexcept
{
    fileDropFeedbackState_ = state;
    fileDropFeedbackStarted_ = fileDropFeedbackClock_.now();
    try {
        fileDropFeedbackDetail_.assign(message);
    } catch (const std::bad_alloc&) {
        fileDropFeedbackDetail_.clear();
    }
}

auto EditorWorkspaceState::refreshFileDropFeedbackUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!fileDropFeedbackRoot_.hasValue()) {
        return Tina::Core::success();
    }
    const Tina::Core::MonotonicTimePoint now = fileDropFeedbackClock_.now();
    const bool terminal =
        fileDropFeedbackState_ == FileDropFeedbackState::Committed ||
        fileDropFeedbackState_ == FileDropFeedbackState::Rejected ||
        fileDropFeedbackState_ == FileDropFeedbackState::Failed;
    if (terminal &&
        Tina::Core::durationBetween(fileDropFeedbackStarted_, now).count() >=
            FileDropFeedbackDismissSeconds) {
        fileDropFeedbackState_ = FileDropFeedbackState::Hidden;
        fileDropFeedbackDetail_.clear();
    }

    UI::UILayoutStyle rootLayout = fileDropFeedbackRootLayout_;
    rootLayout.visibility = fileDropFeedbackState_ == FileDropFeedbackState::Hidden
                                ? UI::UIVisibility::Collapsed
                                : UI::UIVisibility::Visible;
    if (auto status = tree.setLayoutStyle(fileDropFeedbackRoot_, rootLayout);
        !status) {
        return status;
    }
    if (fileDropFeedbackState_ == FileDropFeedbackState::Hidden) {
        return Tina::Core::success();
    }

    std::string_view stateText = "Drop accepted";
    float progress = 0.0F;
    UI::UILayoutStyle progressLayout = fileDropFeedbackProgressLayout_;
    progressLayout.visibility = UI::UIVisibility::Collapsed;
    switch (fileDropFeedbackState_) {
    case FileDropFeedbackState::Accepted:
        stateText = "Drop accepted";
        progress = 0.08F;
        break;
    case FileDropFeedbackState::Processing: {
        stateText = "Drop processing";
        progressLayout.visibility = UI::UIVisibility::Visible;
        progress = 0.35F;
        using ImportState =
            Tina::EditorApp::Detail::EditorSourceImportServiceState;
        if (sourceImportService_.state() == ImportState::Ready) {
            progress = 0.95F;
        } else if (sourceImportService_.state() == ImportState::Running) {
            using ImportPhase =
                Tina::EditorApp::Detail::EditorSourceImportPhase;
            switch (sourceImportService_.phase()) {
            case ImportPhase::Preparing:
                progress = 0.20F;
                break;
            case ImportPhase::Copying:
                progress = 0.50F;
                break;
            case ImportPhase::Cooking:
                progress = 0.75F;
                break;
            case ImportPhase::ReadyToCommit:
                progress = 0.95F;
                break;
            case ImportPhase::Idle:
            case ImportPhase::Failed:
                break;
            }
        }
        break;
    }
    case FileDropFeedbackState::Committed:
        stateText = "Drop committed";
        progress = 1.0F;
        break;
    case FileDropFeedbackState::Rejected:
        stateText = "Drop rejected";
        break;
    case FileDropFeedbackState::Failed:
        stateText = "Drop failed";
        break;
    case FileDropFeedbackState::Hidden:
        break;
    }

    const auto toneIndex = static_cast<Tina::Core::usize>(
        fileDropFeedbackState_);
    if (toneIndex >= fileDropFeedbackToneColors_.size()) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Editor file drop feedback resolved an invalid tone");
    }
    if (auto status = tree.setBoxPaint(
            fileDropFeedbackSurface_,
            UI::makeSolidBox(fileDropFeedbackToneColors_[toneIndex]));
        !status) {
        return status;
    }
    if (auto status = tree.setText(fileDropFeedbackStateText_, stateText);
        !status) {
        return status;
    }
    if (auto status = tree.setText(
            fileDropFeedbackMessage_, fileDropFeedbackDetail_);
        !status) {
        return status;
    }
    if (auto status = tree.setLayoutStyle(
            fileDropFeedbackProgress_, progressLayout);
        !status) {
        return status;
    }
    if (progressLayout.visibility == UI::UIVisibility::Visible) {
        if (auto status = tree.setProgressBarValue(
                fileDropFeedbackProgress_, progress);
            !status) {
            return status;
        }
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::onExit(Tina::GameStateExitContext& context) noexcept -> void{
    editorSettings_.leftDockFraction = leftDockVisibleFraction_;
    editorSettings_.inspectorFraction = inspectorVisibleFraction_;
    editorSettings_.bottomPanelFraction = bottomPanelVisibleFraction_;
    editorSettings_.leftDockVisible = leftDockVisible_;
    editorSettings_.inspectorVisible = inspectorVisible_;
    editorSettings_.bottomPanel = bottomPanel_;
    editorSettings_.layoutDebuggerVisible = layoutDebuggerVisible_;
    editorSettings_.snapEnabled = viewportTransformGizmo_.snap().enabled;
    (void)saveEditorSettings(editorSettings_);
    platformEventSubscription_.reset();
    pendingFileDrops_.clear();
    cancelSourceImportPending_ = false;
    sourceImportRetryUnits_.clear();
    sourceImportRetryPathsUtf8_.clear();
    previewAssetBindingsFailureFeedbackPending_ = false;
    fileDropFeedbackState_ = FileDropFeedbackState::Hidden;
    fileDropFeedbackDetail_.clear();
    using ImportState =
        Tina::EditorApp::Detail::EditorSourceImportServiceState;
    if (sourceImportService_.state() == ImportState::Running) {
        (void)sourceImportService_.cancel();
    } else if (sourceImportService_.state() == ImportState::Ready) {
        (void)sourceImportService_.discardReady();
    } else if (sourceImportService_.state() == ImportState::Failed) {
        (void)sourceImportService_.dismissFailure();
    }
    cleanupFailedSourceImportStage();
    (void)rollbackProjectAssetSourceRename();
    counters_.sourceImportRunning = false;
    if (playSession_.has_value()) {
        (void)playSession_->stop();
        playSession_.reset();
    }
    resetViewportInteractionState();
    viewportNavigation_.reset();
    viewport2DNavigationInitialized_ = false;
    viewport3DNavigationInitialized_ = false;
    // The tile stroke and its hover cursor are cleared by
    // resetViewportInteractionState() above, alongside the other gestures.
    for (auto& listener : viewportPointerListeners_) {
        listener.reset();
    }
    for (auto& listener : viewportOrientationCompassPointerBarrierListeners_) {
        listener.reset();
    }
    viewportNormalized_.reset();
    previewBindings_.clear();
    preview3DBindings_.clear();
    preview3DSkinnedPoses_.clear();
    previewCamera2D_ = {};
    previewCamera3D_ = {};
    previewTileMap_.reset();
    previewWorld_.reset();
    animationPreview_.resetAnimator();
    auto previewRelease = releasePreviewAssetBindings();
    if (!previewRelease) {
        Tina::Core::Error firstFailure = std::move(previewRelease.error());
        (void)context.renderDevice().drainGpuRetirements();
        if (assetResources_.system.has_value()) {
            (void)assetResources_.system->drainGpuRetirements();
        }
        previewRelease = releasePreviewAssetBindings();
        if (!previewRelease) {
            writeError(firstFailure);
            writeError(previewRelease.error());
            std::terminate();
        }
    }
    if (assetResources_.system.has_value()) {
        if (auto status = assetResources_.system->drainGpuRetirements(); !status) {
            writeError(status.error());
            std::terminate();
        }
    }
    // Keep the committed stage alive for AssetSystem; only retire superseded
    // request-owned stages during shutdown.
    cleanupOwnedSourceImportStage(sourceImportSupersededCatalogRootUtf8_);
    sourceImportSupersededCatalogRootUtf8_.clear();
    cleanupOwnedAuthoringStage(sourceImportSupersededAuthoringCatalogRootUtf8_);
    sourceImportSupersededAuthoringCatalogRootUtf8_.clear();
    imageResolverRegistration_.reset();
    if (uiRoot_) {
        uiRoot_.reset();
        ++counters_.uiRootsReleased;
    }
    iconResources_.release();
    snackbarHost_.reset();
    if (sourceImportCatalogCommitted_ || !temporaryProjectRootUtf8_.empty() ||
        !pendingTemporaryProjectCleanupRootUtf8_.empty()) {
        assetResources_.system.reset();
    }
    cleanupFailedSourceImportStage();
    pendingProjectSwitch_.reset();
    projectSwitchBlockedByDirty_ = false;
    activeProjectWorkspace_.reset();
    cleanupOwnedTemporaryProject(pendingTemporaryProjectCleanupRootUtf8_);
    cleanupOwnedTemporaryProject(temporaryProjectRootUtf8_);
    sourceImportCatalogCommitted_ = false;
    previewAssetBindingsFailureFeedbackPending_ = false;
    ++counters_.stateExits;
}

auto EditorWorkspaceState::initialPolicy() const noexcept -> Tina::GameStatePolicy{
    return {};
}

auto EditorWorkspaceState::updateFrame(Tina::FrameUpdateContext& context) -> Tina::Core::Status{
    ++counters_.frameUpdates;
    counters_.lastFrameSeconds =
        static_cast<double>(context.frameTiming().updateDelta.count());
    if (options_.profileUi) {
        const double frameSeconds =
            static_cast<double>(context.frameTiming().updateDelta.count());
        recordEditorFrameTiming(counters_.frameTimingOverall, frameSeconds);
        if (options_.profileUiLayoutDrag &&
            layoutDebugProfileMutationPendingCommit_) {
            recordEditorFrameTiming(
                counters_.layoutDebuggerDragFrameTiming, frameSeconds);
            // updateDelta covers the frame that committed the mutation
            // submitted by the previous updateUI call. Latch a separate flag
            // before updateUI can submit the next mutation.
            layoutDebugProfileCommittedStatisticsPending_ = true;
            layoutDebugProfileMutationPendingCommit_ = false;
        }
        if (sourceImportProfileActive_) {
            recordEditorFrameTiming(
                counters_.frameTimingDuringSourceImport, frameSeconds);
            const EditorProcessMemorySnapshot processMemory =
                queryEditorProcessMemory();
            recordEditorProcessMemory(counters_, processMemory);
            recordEditorProcessMemoryMaximum(
                counters_.sourceImportProcessPeak, processMemory);
            if (sourceImportProfileDeactivatePending_) {
                sourceImportProfileDeactivatePending_ = false;
                sourceImportProfileActive_ = false;
            }
        } else if (sourceImportProfileSeen_) {
            recordEditorFrameTiming(
                counters_.frameTimingAfterSourceImport, frameSeconds);
        } else {
            recordEditorFrameTiming(
                counters_.frameTimingBeforeSourceImport, frameSeconds);
        }
    }
    if (playSessionActive()) {
        auto steps = playSession_->advance(
            context.frameTiming().updateDelta.count());
        if (!steps) {
            return Tina::Core::failure(std::move(steps.error()));
        }
        counters_.playSimulationSteps += *steps;
        counters_.playMaximumSimulationTick = (std::max)(
            counters_.playMaximumSimulationTick,
            playSession_->snapshot().simulationTickCount);
        // The clock is only useful if something consumes it: drive the isolated
        // world's animators with the steps it just produced.
        if (auto status = advancePlayAnimators(*steps); !status) {
            return status;
        }
    }
    if (auto status = processEditorShortcuts(context.frameActions()); !status) {
        return status;
    }
    if (auto status = processPendingRecentProject(); !status) {
        return status;
    }
    if (pendingProjectSwitch_.has_value() && !projectSwitchBlockedByDirty_) {
        auto switched = switchLiveProjectCatalog(*pendingProjectSwitch_);
        if (!switched) {
            return Tina::Core::failure(std::move(switched.error()));
        }
        if (*switched) {
            pendingProjectSwitch_.reset();
        }
    }
    if (auto status = updateSourceImport(); !status) {
        return status;
    }
    if (catalogRefreshPending_) {
        catalogRefreshPending_ = false;
        if (auto status = refreshProjectCatalog(); !status) {
            return status;
        }
    }
    if (previewAssetBindingsRefreshPending_) {
        if (auto status = rebuildLiveCatalogPreview(
                "Catalog preview bindings refreshed at the frame boundary");
            !status) {
            if (status.error().code ==
                Tina::Asset::AssetErrorCode::CatalogReloadBusy) {
                return Tina::Core::success();
            }
            counters_.runtimePreviewValid = false;
            if (isFatalSourceImportError(status.error())) {
                return status;
            }
            if (!previewAssetBindingsFailureFeedbackPending_) {
                previewAssetBindingsFailureFeedbackPending_ = true;
                if (auto feedback = reportAuthoringFailure(
                        "Catalog is live, but preview refresh will retry: ",
                        status.error()); !feedback) {
                    return feedback;
                }
            }
            sourceImportUiRefreshPending_ = true;
            return Tina::Core::success();
        }
        previewAssetBindingsRefreshPending_ = false;
        if (previewAssetBindingsFailureFeedbackPending_) {
            previewAssetBindingsFailureFeedbackPending_ = false;
            sourceImportFailureMessageUtf8_.clear();
            sourceImportUiRefreshPending_ = true;
            authoringFeedback_ =
                "Catalog, Project Browser, documents, and runtime preview bindings refreshed";
            if (fileDropFeedbackState_ == FileDropFeedbackState::Failed &&
                !sourceImportLastFailed_) {
                setFileDropFeedback(
                    FileDropFeedbackState::Committed,
                    "Catalog committed; runtime preview bindings refreshed");
            }
        }
    }
    if (options_.autoDemo &&
        sourceImportService_.state() ==
            Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle) {
        if (automaticDemoStartFrame_ == 0U) {
            automaticDemoStartFrame_ = counters_.frameUpdates;
        }
        const u64 first =
            options_.targetFrameCount > AutomaticAuthoringMinimumFrameCount
                ? options_.targetFrameCount -
                      AutomaticAuthoringMinimumFrameCount
                : u64{1};
        if (!queuedFirstSelection_ && counters_.frameUpdates >= first) {
            const auto stableId = automaticHierarchyStableId(false);
            if (stableId.has_value()) {
                pendingSelectionStableId_ = *stableId;
                counters_.automaticTransformStableId = *stableId;
                pendingViewportToolMode_ = ViewportToolMode::Translate;
                queuedFirstSelection_ = true;
            }
        }
        if (queuedFirstSelection_) {
            const auto queueAutoCommand = [&](EditorCommand command) noexcept {
                if (!queueEditorCommand(command)) {
                    return false;
                }
                pendingAutoTransformInput_ = command == EditorCommand::ApplyTransform;
                ++autoAuthoringStage_;
                return true;
            };
            switch (autoAuthoringStage_) {
            case 0:
                (void)queueAutoCommand(EditorCommand::MoveSelectedPositiveX);
                break;
            case 1:
                (void)queueAutoCommand(EditorCommand::ApplyTransform);
                break;
            case 2:
                if (queueAutomaticViewportNavigation()) {
                    ++autoAuthoringStage_;
                }
                break;
            case 3:
                if (prepareAutomaticViewportGizmo(
                        Tina::Editor::EditorTransformGizmoMode::Translate)) {
                    ++autoAuthoringStage_;
                }
                break;
            case 4: {
                const auto point = automaticViewportGizmoPoint(0.5F);
                if (point.has_value() && updateViewportGizmo(
                                             Tina::Platform::PrimaryPointerId,
                                             *point)) {
                    ++autoAuthoringStage_;
                }
                break;
            }
            case 5: {
                const auto point = automaticViewportGizmoPoint(1.0F);
                if (point.has_value() && requestViewportGizmoCommit(
                                             Tina::Platform::PrimaryPointerId,
                                             *point)) {
                    ++autoAuthoringStage_;
                }
                break;
            }
            case 6:
                if (prepareAutomaticViewportMarquee(
                        Tina::Editor::EditorMarqueeSelectionMode::Replace)) {
                    ++autoAuthoringStage_;
                }
                break;
            case 7:
                if (prepareAutomaticViewportMarquee(
                        Tina::Editor::EditorMarqueeSelectionMode::Add)) {
                    ++autoAuthoringStage_;
                }
                break;
            case 8:
                if (prepareAutomaticViewportGizmo(
                        Tina::Editor::EditorTransformGizmoMode::Rotate)) {
                    ++autoAuthoringStage_;
                }
                break;
            case 9: {
                const auto point = automaticViewportGizmoPoint(0.5F);
                if (point.has_value() && updateViewportGizmo(
                                             Tina::Platform::PrimaryPointerId,
                                             *point)) {
                    ++autoAuthoringStage_;
                }
                break;
            }
            case 10: {
                const auto point = automaticViewportGizmoPoint(1.0F);
                if (point.has_value() && requestViewportGizmoCommit(
                                             Tina::Platform::PrimaryPointerId,
                                             *point)) {
                    ++autoAuthoringStage_;
                }
                break;
            }
            case 11:
                if (prepareAutomaticViewportGizmo(
                        Tina::Editor::EditorTransformGizmoMode::Scale)) {
                    ++autoAuthoringStage_;
                }
                break;
            case 12: {
                const auto point = automaticViewportGizmoPoint(0.5F);
                if (point.has_value() && updateViewportGizmo(
                                             Tina::Platform::PrimaryPointerId,
                                             *point)) {
                    ++autoAuthoringStage_;
                }
                break;
            }
            case 13: {
                const auto point = automaticViewportGizmoPoint(1.0F);
                if (point.has_value() && requestViewportGizmoCommit(
                                             Tina::Platform::PrimaryPointerId,
                                             *point)) {
                    ++autoAuthoringStage_;
                }
                break;
            }
            case 14:
                if (prepareAutomaticViewportMarquee(
                        Tina::Editor::EditorMarqueeSelectionMode::Toggle)) {
                    ++autoAuthoringStage_;
                }
                break;
            case 15:
                (void)queueAutoCommand(EditorCommand::Undo);
                break;
            case 16:
                (void)queueAutoCommand(EditorCommand::Redo);
                break;
            case 17:
                // SceneAdd only opens the picker, so this stage drives the
                // confirm too and advances once the entity actually exists.
                if (pendingSceneAddRequest_.has_value()) {
                    (void)queueEditorCommand(EditorCommand::SceneAddConfirm);
                } else if (counters_.automaticAddedStableId != 0U) {
                    ++autoAuthoringStage_;
                } else {
                    (void)queueEditorCommand(EditorCommand::SceneAdd);
                }
                break;
            case 18:
                if (counters_.automaticAddedStableId != 0U &&
                    stableEntityIdForHierarchyItem(selectionKey_) ==
                        counters_.automaticAddedStableId) {
                    (void)queueAutoCommand(EditorCommand::SceneDuplicate);
                }
                break;
            case 19:
                if (counters_.automaticDuplicatedStableId != 0U &&
                    stableEntityIdForHierarchyItem(selectionKey_) ==
                        counters_.automaticDuplicatedStableId) {
                    (void)queueAutoCommand(EditorCommand::SceneReparentRoot);
                }
                break;
            case 20:
                if (counters_.automaticAddedStableId != 0U &&
                    counters_.automaticDuplicatedStableId != 0U &&
                    stableEntityIdForHierarchyItem(selectionKey_) ==
                        counters_.automaticDuplicatedStableId) {
                    if (queueAutoCommand(EditorCommand::SceneReparent)) {
                        pendingAutoParentStableId_ =
                            counters_.automaticAddedStableId;
                    }
                }
                break;
            case 21: {
                const EditorHierarchyRow* duplicated =
                    hierarchyRow(counters_.automaticDuplicatedStableId);
                if (duplicated != nullptr &&
                    duplicated->parentStableId ==
                        counters_.automaticAddedStableId &&
                    stableEntityIdForHierarchyItem(selectionKey_) ==
                        counters_.automaticDuplicatedStableId) {
                    (void)queueAutoCommand(EditorCommand::SceneDelete);
                }
                break;
            }
            case 22:
                if (pendingSceneDeleteConfirmation_.has_value() &&
                    pendingSceneDeleteConfirmation_->stableId ==
                        counters_.automaticDuplicatedStableId) {
                    (void)queueEditorCommand(EditorCommand::SceneDeleteConfirm);
                } else if (stableEntityIdForHierarchyItem(selectionKey_) ==
                           counters_.automaticAddedStableId) {
                    (void)queueAutoCommand(EditorCommand::SceneDelete);
                }
                break;
            case 23:
                if (pendingSceneDeleteConfirmation_.has_value() &&
                    pendingSceneDeleteConfirmation_->stableId ==
                        counters_.automaticAddedStableId) {
                    (void)queueEditorCommand(EditorCommand::SceneDeleteConfirm);
                } else {
                    (void)queueAutoCommand(EditorCommand::PlayStartOrResume);
                }
                break;
            case 24:
                if (playSession_.has_value() &&
                    playSession_->snapshot().state ==
                        Tina::Editor::EditorPlayState::Playing) {
                    (void)queueAutoCommand(EditorCommand::PlayPause);
                }
                break;
            case 25:
                if (playSession_.has_value() &&
                    playSession_->snapshot().state ==
                        Tina::Editor::EditorPlayState::Paused) {
                    automaticPlayStepBaseline_ =
                        playSession_->snapshot().simulationTickCount;
                    (void)queueAutoCommand(EditorCommand::PlayStep);
                }
                break;
            case 26:
                if (playSession_.has_value() &&
                    playSession_->snapshot().state ==
                        Tina::Editor::EditorPlayState::Paused &&
                    !playSession_->snapshot().stepPending &&
                    playSession_->snapshot().simulationTickCount >
                        automaticPlayStepBaseline_) {
                    (void)queueAutoCommand(EditorCommand::PlayStartOrResume);
                }
                break;
            case 27:
                if (playSession_.has_value() &&
                    playSession_->snapshot().state ==
                        Tina::Editor::EditorPlayState::Playing) {
                    (void)queueAutoCommand(EditorCommand::PlayStop);
                }
                break;
            case 28:
                if (playSession_.has_value() &&
                    playSession_->snapshot().state ==
                        Tina::Editor::EditorPlayState::Editing &&
                    counters_.playStops != 0U) {
                    ++autoAuthoringStage_;
                }
                break;
            case 29:
                if (options_.initialWorkspace != WorkspaceMode::World2D) {
                    ++autoAuthoringStage_;
                } else if (tileMapEditingContext()) {
                    (void)queueAutoCommand(EditorCommand::GenerateTileMapGameplay);
                } else if (!pendingDocumentTabActivation_.has_value()) {
                    // Gameplay generation belongs to the TileMap tab. Queue the
                    // tab activation and retry this stage after the retained UI
                    // publishes it; the initial 2D tab is World2D.
                    pendingDocumentTabActivation_ = 2U;
                }
                break;
            case 30:
                if (options_.initialWorkspace == WorkspaceMode::World2D &&
                    tileMapEditingContext()) {
                    // Return to the World2D document before exercising Save;
                    // the TileMap tab owns a separate session/path.
                    (void)queueAutoCommand(EditorCommand::SwitchToWorld2D);
                } else if (!activeWorkspaceSession().hasDocumentPath()) {
                    ++autoAuthoringStage_;
                } else {
                    (void)queueAutoCommand(EditorCommand::Save);
                }
                break;
            case 31:
                (void)queueAutoCommand(options_.initialWorkspace == WorkspaceMode::World2D
                                           ? EditorCommand::SwitchToWorld3D
                                           : EditorCommand::SwitchToWorld2D);
                break;
            case 32: {
                const WorkspaceMode otherWorkspace =
                    options_.initialWorkspace == WorkspaceMode::World2D
                        ? WorkspaceMode::World3D
                        : WorkspaceMode::World2D;
                if (workspaceMode_ == otherWorkspace &&
                    !pendingEditorCommand_.has_value() &&
                    queueAutomaticViewportNavigation()) {
                    ++autoAuthoringStage_;
                }
                break;
            }
            case 33:
                if (workspaceMode_ == WorkspaceMode::World2D) {
                    ++autoAuthoringStage_;
                } else {
                    (void)queueAutoCommand(EditorCommand::SwitchToWorld2D);
                }
                break;
            case 34:
                if (workspaceMode_ == WorkspaceMode::World2D && tileMapEditingContext()) {
                    (void)queueAutoCommand(EditorCommand::BakeNavigation2D);
                } else if (workspaceMode_ == WorkspaceMode::World2D &&
                           !pendingDocumentTabActivation_.has_value()) {
                    pendingDocumentTabActivation_ = 2U;
                } else {
                    (void)queueAutoCommand(EditorCommand::SwitchToWorld2D);
                }
                break;
            case 35:
                if (workspaceMode_ == WorkspaceMode::World2D && tileMapEditingContext() &&
                    !pendingDocumentTabActivation_.has_value()) {
                    pendingDocumentTabActivation_ = 3U;
                } else {
                    (void)queueAutoCommand(EditorCommand::AnimationNextFrame);
                }
                break;
            case 36:
                (void)queueAutoCommand(EditorCommand::AnimationAddEvent);
                break;
            case 37:
                (void)queueAutoCommand(EditorCommand::AnimationRemoveEvent);
                break;
            case 38:
                (void)queueAutoCommand(EditorCommand::Undo);
                break;
            case 39:
                (void)queueAutoCommand(EditorCommand::Redo);
                break;
            case 40:
                (void)queueAutoCommand(EditorCommand::AnimationAddEvent);
                break;
            case 41:
                (void)queueAutoCommand(EditorCommand::AnimationCycleMode);
                break;
            case 42:
                (void)queueAutoCommand(EditorCommand::Undo);
                break;
            case 43:
                (void)queueAutoCommand(EditorCommand::Redo);
                break;
            case 44:
                (void)queueAutoCommand(EditorCommand::AnimationCookPreview);
                break;
            case 45:
                if (options_.initialWorkspace == WorkspaceMode::World2D) {
                    ++autoAuthoringStage_;
                } else {
                    (void)queueAutoCommand(EditorCommand::SwitchToWorld3D);
                }
                break;
            case 46:
                (void)queueAutoCommand(EditorCommand::OpenSelectedProjectAsset);
                break;
            case 47:
                if (counters_.tabOwnedDocumentLoads != 0U) {
                    const auto* activeTab = documentTabs_.activeTab();
                    if (activeTab == nullptr) {
                        break;
                    }
                    switch (activeTab->key.kind) {
                    case Tina::Editor::EditorDocumentKind::World3D:
                        pendingDocumentTabActivation_ = 1U;
                        break;
                    case Tina::Editor::EditorDocumentKind::TileMap2D:
                        pendingDocumentTabActivation_ = 2U;
                        break;
                    case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
                        pendingDocumentTabActivation_ = 3U;
                        break;
                    case Tina::Editor::EditorDocumentKind::World2D:
                    case Tina::Editor::EditorDocumentKind::AssetInspector:
                    default:
                        break;
                    }
                }
                ++autoAuthoringStage_;
                break;
            case 48:
                (void)queueAutoCommand(EditorCommand::SwitchToWorld2D);
                break;
            case 49:
                if (workspaceMode_ == WorkspaceMode::World2D &&
                    !pendingEditorCommand_.has_value()) {
                    pendingSelectionStableId_ = 6U;
                    ++autoAuthoringStage_;
                }
                break;
            case 50:
                if (!pendingSelectionStableId_.has_value() &&
                    stableEntityIdForHierarchyItem(selectionKey_) == 6U) {
                    ++autoAuthoringStage_;
                }
                break;
            case 51:
                if (stableEntityIdForHierarchyItem(selectionKey_) == 6U &&
                    !pointLightColorPickerVisible_) {
                    pendingPointLightColorPickerToggle_ = true;
                    ++autoAuthoringStage_;
                }
                break;
            case AutomaticColorPickerVisibleStage:
                if (pointLightColorPickerVisible_ &&
                    (options_.rgbaOutputUtf8.empty() ||
                     options_.rgbaStage != RgbaCaptureStage::ColorPicker ||
                     counters_.rgbaCaptureOk)) {
                    ++autoAuthoringStage_;
                }
                break;
            case 53:
                if (pointLightColorPickerVisible_) {
                    pendingPointLightColorPickerToggle_ = true;
                    ++autoAuthoringStage_;
                }
                break;
            case 54:
                (void)queueAutoCommand(options_.initialWorkspace == WorkspaceMode::World2D
                                           ? EditorCommand::SwitchToWorld2D
                                           : EditorCommand::SwitchToWorld3D);
                break;
            case 55:
                if (workspaceMode_ == options_.initialWorkspace &&
                    !pendingEditorCommand_.has_value()) {
                    const auto stableId = automaticHierarchyStableId(true);
                    if (stableId.has_value()) {
                        pendingSelectionStableId_ = *stableId;
                        counters_.automaticFinalSelectionStableId = *stableId;
                        ++autoAuthoringStage_;
                    }
                }
                break;
            case AutomaticFinalSelectionCommitStage:
                // The setter publishes through a retained-UI commit. Do not
                // report the demo as settled until the next UI update has
                // observed that committed selection.
                if (!pendingSelectionStableId_.has_value() &&
                    counters_.automaticFinalSelectionStableId != 0U &&
                    stableEntityIdForHierarchyItem(selectionKey_) ==
                        counters_.automaticFinalSelectionStableId) {
                    ++autoAuthoringStage_;
                }
                break;
            default:
                break;
            }
        }
    }
    if (workspaceMode_ == WorkspaceMode::World2D && animationPreview_.playing() &&
        animationPreview_.hasAnimator()) {
        auto update = animationPreview_.animator().update(context.frameTiming().updateDelta);
        if (!update) {
            return Tina::Core::failure(std::move(update.error()));
        }
        if (update->currentFrameChanged) {
            if (auto status = applyAnimationPreviewFrame(
                    static_cast<u32>(update->currentFrameIndex)); !status) {
                return status;
            }
            pendingAnimationTimelineRefresh_ = true;
        }
        if (animationPreview_.animator().isCompleted()) {
            animationPreview_.setPlaying(false);
            pendingAnimationTimelineRefresh_ = true;
        }
    }
    const bool sourceImportSettled =
        !sourceImportStartPending_ &&
        sourceImportService_.state() ==
            Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle;
    const bool automaticDemoSettled =
        !options_.autoDemo ||
        autoAuthoringStage_ >= AutomaticAuthoringStageCount;
    counters_.automaticAuthoringStage = autoAuthoringStage_;
    const bool automaticDemoDeadlineReached =
        options_.autoDemo && automaticDemoStartFrame_ != 0U &&
        counters_.frameUpdates >= automaticDemoStartFrame_ &&
        counters_.frameUpdates - automaticDemoStartFrame_ >=
            AutomaticAuthoringMinimumFrameCount - u64{1};
    if (options_.targetFrameCount != 0 &&
        counters_.frameUpdates >= options_.targetFrameCount &&
        sourceImportSettled &&
        (automaticDemoSettled || automaticDemoDeadlineReached)) {
        context.requestExitAfterFrame();
    }
    if (options_.frameDelayMilliseconds != 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds{options_.frameDelayMilliseconds});
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::extractRenderScene(Tina::RenderSceneExtractionContext& context) const -> Tina::Core::Status{
    ++counters_.renderExtractions;
    counters_.gpuViewportSprites = 0;
    counters_.gpuViewportMeshes = 0;
    if (pendingProjectSwitch_.has_value() ||
        previewAssetBindingsRefreshPending_ ||
        sourceImportService_.state() ==
            Tina::EditorApp::Detail::EditorSourceImportServiceState::Ready) {
        return Tina::Core::success();
    }
    if (!viewportNormalized_.has_value()) {
        return Tina::Core::success();
    }
    if (!previewWorld_.has_value()) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "editor GPU viewport has no canonical preview World");
    }
    if (workspaceMode_ == WorkspaceMode::World3D) {
        return extractWorld3DViewport(context);
    }

    if (!previewCamera2D_.hasValue()) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "editor GPU viewport is missing the independent Camera2D");
    }
    const Tina::Scene::Camera2D* authoredCamera =
        previewWorld_->camera2D(previewCamera2D_);
    if (authoredCamera == nullptr) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "editor GPU viewport is missing the Camera2D component");
    }
    Tina::Scene::Camera2D camera = *authoredCamera;
    camera.projection = Tina::Render::FixedWorldHeight2D{
        .heightMeters = viewportWorldHeight(),
    };
    camera.normalizedViewport = *viewportNormalized_;
    camera.pixelSnap = Tina::Render::RenderPixelSnapPolicy::Disabled;
    if (auto status = previewWorld_->setCamera2D(previewCamera2D_, camera); !status) {
        return status;
    }
    if (auto status = Tina::Scene::extractRenderSceneFromWorld(
            *previewWorld_, context.renderSceneWriter(), context.frameResourceSink(),
            Tina::Scene::ExtractRenderSceneParams{
                .surfaceViewport = {
                    .pixelWidth = surfacePixelWidth_,
                    .pixelHeight = surfacePixelHeight_,
                },
                .spriteBindingResolver = {
                    .userData = const_cast<EditorWorkspaceState*>(this),
                    .resolve = &EditorWorkspaceState::resolvePreviewSprite,
                },
                .normalTextureBindingResolver = {
                    .userData = const_cast<EditorWorkspaceState*>(this),
                    .resolve = &EditorWorkspaceState::resolvePreviewTexture,
                },
                // Sprite2D reaches a custom fragment stage only through the authored
                // component handle; there is no cooked-sprite default to fall back to.
                .shaderBindingResolver = {
                    .userData = const_cast<EditorWorkspaceState*>(this),
                    .resolve = &EditorWorkspaceState::resolvePreviewShader,
                },
                .shaderUniformBindingResolver = {
                    .userData = const_cast<EditorWorkspaceState*>(this),
                    .resolve = &EditorWorkspaceState::resolvePreviewShaderUniforms,
                },
            });
        !status) {
        return status;
    }
    u64 emittedTileSprites = 0;
    if (previewTileMap_.has_value() && !previewTileMapLayerIds_.empty() &&
        previewTilesetAsset_) {
        const Tina::Scene::WorldTransform* cameraTransform =
            previewWorld_->worldTransform(previewCamera2D_);
        if (cameraTransform == nullptr) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "editor TileMap preview is missing the Camera2D world transform");
        }
        constexpr u64 TileEntityKeyBase = 100'000U;
        constexpr u64 TileLayerEntityKeyStride =
            static_cast<u64>(Tina::AssetFormat::TileMapWire::MaxDimension) *
                Tina::AssetFormat::TileMapWire::MaxDimension +
            1U;
        const Tina::Asset::TileChunkCameraQuery cameraQuery{
            .centerX = cameraTransform->position.x,
            .centerY = cameraTransform->position.y,
            .halfWidth = viewportWorldWidth() * 0.5F,
            .halfHeight = viewportWorldHeight() * 0.5F,
        };
        std::pmr::vector<Tina::Render::RenderSprite2DInput> tileSprites{
            &assetResources_.memory};
        for (Tina::Core::usize layerIndex = 0;
             layerIndex < previewTileMapLayerIds_.size(); ++layerIndex) {
            auto emitted = Tina::Asset::emitVisibleTileMapSprites(
                *previewTileMap_, previewTileMapLayerIds_[layerIndex], cameraQuery,
                Tina::Asset::TileChunkSpriteEmitParams{
                    .tileset = previewTilesetAsset_,
                    .bindingResolver = {
                        .userData = const_cast<EditorWorkspaceState*>(this),
                        .resolve = &EditorWorkspaceState::resolvePreviewTileset,
                    },
                    .stableEntityKeyBase =
                        TileEntityKeyBase + layerIndex * TileLayerEntityKeyStride,
                    .sortingLayer = static_cast<Tina::Core::i16>(
                        static_cast<Tina::Core::i32>(layerIndex) -
                        static_cast<Tina::Core::i32>(previewTileMapLayerIds_.size())),
                },
                context.frameResourceSink(), tileSprites);
            if (!emitted) {
                return Tina::Core::failure(std::move(emitted.error()));
            }
            for (const auto& sprite : tileSprites) {
                if (auto status = context.renderSceneWriter().addSprite2D(sprite); !status) {
                    return status;
                }
            }
            emittedTileSprites += *emitted;
        }
    }
    counters_.tileMapEmittedSprites = emittedTileSprites;
    counters_.gpuViewportSprites = previewResolvedSpriteCount_ + emittedTileSprites;
    counters_.gpuViewportDocumentRevision = previewRevision_;
    return Tina::Core::success();
}

auto EditorWorkspaceState::updateUI(Tina::UIUpdateContext& context) -> Tina::Core::Status{
    TINA_TRACE_ZONE("Editor.UI.Update");
    std::optional<std::chrono::steady_clock::time_point> profileStart{};
    if (options_.profileUi) {
        profileStart = std::chrono::steady_clock::now();
    }
    auto profileScope = Tina::Core::makeScopeExit([&]() noexcept {
        if (!profileStart.has_value()) {
            return;
        }
        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - *profileStart).count();
        recordEditorFrameTiming(counters_.updateUiTiming, elapsed);
    });
    if (!context.hasPrimaryWindowUI() || !uiRoot_) {
        return Tina::Core::success();
    }
    auto tree = context.primaryWindowUITreeUpdater(uiRoot_);
    if (!tree) {
        return Tina::Core::failure(std::move(tree.error()));
    }
    // The palette projection depends on the resolved tileset payload and on the
    // editing context that gates each row. Both invalidate paths below dirty
    // Style unconditionally, which re-resolves all TilePaletteMaterializedCapacity
    // rows, so rebuild only when one of those inputs actually changed.
    const Tina::Core::ContentHash tilesetHash = [&]() noexcept {
        if (!previewTilesetAsset_ || !assetResources_.system.has_value()) {
            return Tina::Core::ContentHash{};
        }
        const auto* file = assetResources_.system->tryGet(previewTilesetAsset_);
        return file != nullptr ? file->header().contentHash : Tina::Core::ContentHash{};
    }();
    const bool tileMapEditing = tileMapEditingContext();
    if (!tilePaletteProjectionInitialized_ ||
        observedTilePaletteTilesetAsset_ != previewTilesetAsset_ ||
        observedTilePaletteTilesetHash_ != tilesetHash ||
        observedTilePaletteEditingContext_ != tileMapEditing) {
        observedTilePaletteTilesetAsset_ = previewTilesetAsset_;
        observedTilePaletteTilesetHash_ = tilesetHash;
        observedTilePaletteEditingContext_ = tileMapEditing;
        tilePaletteProjectionInitialized_ = true;
        tilePaletteTileCount_ = 0U;
        if (previewTilesetAsset_ && assetResources_.system.has_value()) {
            if (const auto* file = assetResources_.system->tryGet(previewTilesetAsset_);
                file != nullptr) {
                auto payload = Tina::AssetFormat::parseTilesetPayload(file->payload());
                if (payload) {
                    const u32 count = (std::min)(payload->tileCount,
                        static_cast<u32>(tilePaletteTiles_.size()));
                    for (u32 index = 0; index < count; ++index) {
                        const auto tile = payload->tile(index);
                        if (!tile.has_value()) continue;
                        tilePaletteTiles_[tilePaletteTileCount_] = *tile;
                        tilePaletteLabels_[tilePaletteTileCount_] =
                            "Tile " + std::to_string(tile->localId);
                        ++tilePaletteTileCount_;
                    }
                }
            }
        }
        if (auto status = tree->setVirtualGridViewDataSource(
                tilePaletteGrid_, tilePaletteDataSource()); !status) {
            return status;
        }
        if (auto status = tree->invalidateVirtualGridViewItems(tilePaletteGrid_); !status) {
            return status;
        }
    }
    if (auto selection = tree->virtualGridViewSelection(tilePaletteGrid_); selection) {
        if (selection->hasValue() && observedTilePaletteSelection_ != selection->logicalIndex &&
            selection->logicalIndex < tilePaletteTileCount_) {
            selectedTileId_ = tilePaletteTiles_[selection->logicalIndex].localId;
            observedTilePaletteSelection_ = selection->logicalIndex;
        }
        if (!selection->hasValue() && tilePaletteTileCount_ != 0U) {
            auto metrics = tree->virtualGridViewMetrics(tilePaletteGrid_);
            if (!metrics) {
                return Tina::Core::failure(std::move(metrics.error()));
            }
            if (metrics->logicalColumnCount != 0U &&
                metrics->logicalItemCount == static_cast<u64>(tilePaletteTileCount_)) {
                for (u32 index = 0; index < tilePaletteTileCount_; ++index) {
                    if (tilePaletteTiles_[index].localId == selectedTileId_) {
                        if (auto status = tree->setVirtualGridViewSelectedIndex(
                                tilePaletteGrid_, index);
                            !status) {
                            return status;
                        }
                        observedTilePaletteSelection_ = index;
                        break;
                    }
                }
            }
        }
    }
    if (auto rect = tree->committedLayoutRect(projectAssetList_); rect) {
        projectAssetListRect_ = *rect;
    }
    if (nodePropertySections_[0].resourceSlot.hasValue()) {
        if (auto rect = tree->committedLayoutRect(
                nodePropertySections_[0].resourceSlot); rect) {
            inspectorSpriteResourceRect_ = *rect;
        }
        auto theme = tree->productTheme();
        if (!theme) {
            return Tina::Core::failure(std::move(theme.error()));
        }
        if (auto status = tree->setBoxPaint(
                nodePropertySections_[0].resourceSlot,
                UI::makeSolidBox(projectAssetDragOverSpriteResource_
                                     ? theme->colors.primaryContainer
                                     : theme->colors.surfaceContainerLow));
            !status) {
            return status;
        }
    }
    const u32 inspectorStableId = stableEntityIdForHierarchyItem(selectionKey_);
    const EditorHierarchyRow* inspectorRow = hierarchyRow(inspectorStableId);
    const bool spriteInspectorSelection = inspectorRow != nullptr &&
        (inspectorRow->kindName == "Sprite2D" ||
         inspectorRow->kindName == "AnimatedSprite2D");
    if (assetInspectorActive_ || !sceneDocumentActive() ||
        !spriteInspectorSelection) {
        inspectorSpriteResourceRect_ = {};
        projectAssetDragOverSpriteResource_ = false;
    }
    if (auto rect = tree->committedLayoutRect(viewportPreviewLayer_); rect) {
        viewportLogicalRect_ = *rect;
    }
    if (viewportAssetDropIndicator_.hasValue()) {
        UI::UILayoutStyle dropStyle = fixedSize(
            (std::max)(1.0F, viewportLogicalRect_.width),
            (std::max)(1.0F, viewportLogicalRect_.height));
        dropStyle.placement = UI::UILayoutPlacement::Overlay;
        dropStyle.overlay.horizontal = UI::UIAxisAlignment::Start;
        dropStyle.overlay.vertical = UI::UIAxisAlignment::Start;
        dropStyle.overlay.offset.x = UI::UILayoutLength::Px(0.0F);
        dropStyle.overlay.offset.y = UI::UILayoutLength::Px(0.0F);
        dropStyle.visibility = projectAssetDragOverViewport_
            ? UI::UIVisibility::Visible
            : UI::UIVisibility::Collapsed;
        if (auto status = tree->setLayoutStyle(
                viewportAssetDropIndicator_, dropStyle); !status) {
            return status;
        }
    }
    if (auto metrics = tree->virtualGridViewMetrics(projectAssetList_); metrics) {
        projectAssetListMetrics_ = *metrics;
    } else {
        return Tina::Core::failure(std::move(metrics.error()));
    }
    if (auto style = tree->virtualGridViewStyle(projectAssetList_); style) {
        projectAssetListStyle_ = *style;
    } else {
        return Tina::Core::failure(std::move(style.error()));
    }
    if (auto status = processPendingMenuToggle(*tree); !status) {
        return status;
    }
    if (auto status = captureRequestedRgbaFrame(*tree); !status) {
        return status;
    }
    if (auto status = updateHierarchySearch(*tree); !status) {
        return status;
    }
    if (auto status = updateProjectAssetSearch(*tree); !status) {
        return status;
    }
    if (pendingProjectAssetViewMode_.has_value()) {
        const ProjectAssetViewMode mode = *pendingProjectAssetViewMode_;
        pendingProjectAssetViewMode_.reset();
        if (auto status = applyProjectAssetViewMode(*tree, mode); !status) {
            return status;
        }
    }
    if (auto status = updateSceneAddSearch(*tree); !status) {
        return status;
    }
    if (auto status = updateSpriteAssetPickerSearch(*tree); !status) {
        return status;
    }
    if (auto status = synchronizeSpriteAssetPickerSelection(*tree); !status) {
        return status;
    }
    if (auto status = processInspectorFieldCommit(*tree); !status) {
        return status;
    }
    if (auto status = processPendingFileDrops(*tree); !status) {
        return status;
    }
    if (auto status = processPendingProjectAssetDrop(*tree); !status) {
        return status;
    }
    if (!projectBrowserUiRefreshPending_) {
        if (auto status = synchronizePendingProjectAssetSelection(*tree); !status) {
            return status;
        }
    }
    if (auto status = processPendingSceneAddTemplate(*tree); !status) {
        return status;
    }
    if (auto status = processPendingHierarchyRename(*tree); !status) {
        return status;
    }
    if (auto status = processPendingHierarchyDrop(*tree); !status) {
        return status;
    }
    if (pendingDirtyCloseDialogFocus_) {
        const WorkspaceSessionState* session =
            pendingDirtyCloseKey_.has_value()
                ? findDocumentSession(*pendingDirtyCloseKey_)
                : nullptr;
        if (auto status = tree->requestFocus(
                session != nullptr && session->hasDocumentPath()
                    ? dirtyCloseDialog_.actions[DirtyCloseSaveActionIndex]
                    : dirtyClosePathInput_);
            !status) {
            return status;
        }
        pendingDirtyCloseDialogFocus_ = false;
    } else if (pendingSceneAddDialogFocus_) {
        if (auto status = tree->requestFocus(sceneAddSearchInput_); !status) {
            return status;
        }
        pendingSceneAddDialogFocus_ = false;
    } else if (spriteAssetPickerFocusPending_) {
        if (auto status = tree->requestFocus(spriteAssetPickerSearchInput_);
            !status) {
            return status;
        }
        spriteAssetPickerFocusPending_ = false;
    } else if (pendingSceneDeleteDialogFocus_) {
        if (auto status = tree->requestFocus(
                sceneDeleteDialog_.actions[SceneDeleteConfirmActionIndex]);
            !status) {
            return status;
        }
        pendingSceneDeleteDialogFocus_ = false;
    } else if (pendingProjectAssetRemoveDialogFocus_) {
        if (auto status = tree->requestFocus(
                projectAssetRemoveDialog_.actions[SceneDeleteConfirmActionIndex]);
            !status) {
            return status;
        }
        pendingProjectAssetRemoveDialogFocus_ = false;
    } else if (pendingProjectAssetRenameDialogFocus_) {
        if (auto status = tree->requestFocus(projectAssetRenameInput_); !status) {
            return status;
        }
        pendingProjectAssetRenameDialogFocus_ = false;
    } else if (pendingProjectAssetFolderDialogFocus_) {
        if (auto status = tree->requestFocus(projectAssetFolderInput_); !status) {
            return status;
        }
        pendingProjectAssetFolderDialogFocus_ = false;
    } else if (pendingAboutDialogFocus_) {
        if (auto status = tree->requestFocus(
                aboutDialog_.actions[AboutCloseActionIndex]);
            !status) {
            return status;
        }
        pendingAboutDialogFocus_ = false;
    } else if (pendingAboutDialogFocusRestore_) {
        if (auto status = tree->requestFocus(
                mainMenuAnchors_[HelpMainMenuIndex]);
            !status) {
            return status;
        }
        pendingAboutDialogFocusRestore_ = false;
    } else if (pendingHierarchyFocusRestore_) {
        if (auto status = tree->requestFocus(hierarchyTree_); !status) {
            return status;
        }
        pendingHierarchyFocusRestore_ = false;
    } else if (pendingHierarchyRenameFocus_) {
        if (hierarchyRenameFocusDeferralFrames_ != 0U) {
            --hierarchyRenameFocusDeferralFrames_;
        } else if (!hierarchyRenameVisible_ || !hierarchyRenameInput_.hasValue()) {
            pendingHierarchyRenameFocus_ = false;
        } else if (auto status = tree->requestFocus(hierarchyRenameInput_); !status) {
            // A layout rebuild can take one additional pass after a document
            // refresh. Keep the request pending instead of turning a transient
            // focusability state into an editor-fatal UI error.
            if (status.error().code != UI::UIErrorCode::InvalidFocusTarget) {
                return status;
            }
        } else {
            pendingHierarchyRenameFocus_ = false;
        }
    }
    if (auto status = processPendingInspectorSectionUpdates(*tree); !status) {
        return status;
    }
    if (auto status = processPendingPointLightColorUpdates(*tree); !status) {
        return status;
    }
    if (playSessionActive() &&
        observedPlaySessionRevision_ != playSession_->snapshot().revision) {
        observedPlaySessionRevision_ = playSession_->snapshot().revision;
        if (auto status = publishRuntimePreviewStatus(*tree); !status) {
            return status;
        }
    }
    if (auto status = updateGpuViewport(*tree); !status) {
        return status;
    }
    if (auto status = processViewportNavigation(); !status) {
        return status;
    }
    if (std::exchange(viewportViewModeRefreshPending_, false)) {
        if (auto status = refreshViewportViewModeUi(*tree); !status) {
            return status;
        }
    }
    if (auto status = updateViewportGrid(*tree); !status) {
        return status;
    }
    if (auto status = updateViewportOrientationCompass(*tree); !status) {
        return status;
    }
    if (auto status = updateViewportTransformGizmo(*tree); !status) {
        return status;
    }
    if (auto status = updateViewportMarqueeVisual(*tree); !status) {
        return status;
    }
    if (auto status = updateViewportCollisionShapeVisuals(*tree); !status) {
        return status;
    }
    if (auto status = updateViewportPreselectionVisual(*tree); !status) {
        return status;
    }
    if (auto status = updateViewportTileCursorVisual(*tree); !status) {
        return status;
    }
    if (pendingAnimationTimelineRefresh_) {
        pendingAnimationTimelineRefresh_ = false;
        if (auto status = refreshAnimationTimelineUi(*tree); !status) {
            return status;
        }
    }
    if (sourceImportVisibilityRefreshPending_) {
        sourceImportVisibilityRefreshPending_ = false;
        if (auto status = refreshProjectAssetUi(*tree); !status) {
            return status;
        }
    }
    if (sourceImportUiRefreshPending_) {
        sourceImportUiRefreshPending_ = false;
        if (auto status = tree->setDataGridDataSource(
                sourceImportGrid_, sourceImportGridDataSource());
            !status) {
            return status;
        }
        if (auto status = tree->invalidateDataGridItems(sourceImportGrid_); !status) {
            return status;
        }
        if (sourceImportUnits_.empty()) {
            if (auto status = tree->clearDataGridSelection(sourceImportGrid_); !status) {
                return status;
            }
            observedSourceImportSelectionIndex_.reset();
        } else {
            const u64 selectedIndex = std::min<u64>(
                observedSourceImportSelectionIndex_.value_or(0U),
                static_cast<u64>(sourceImportUnits_.size() - 1U));
            if (auto status = tree->setDataGridSelectedCell(
                    sourceImportGrid_, selectedIndex, 0U);
                !status) {
                return status;
            }
            observedSourceImportSelectionIndex_ = selectedIndex;
        }
        // SourceImport phase changes are published by the worker without a
        // Catalog snapshot change. Refresh the Project Assets activity row as
        // part of the same bounded UI transaction so Preparing/Copying/
        // Cooking/Ready and queued file-drop feedback never lags a frame.
        if (auto status = refreshProjectAssetUi(*tree); !status) {
            return status;
        }
        if (auto status = tree->invalidateVirtualGridViewItems(projectAssetList_);
            !status) {
            return status;
        }
    }
    if (projectBrowserUiRefreshPending_) {
        projectBrowserUiRefreshPending_ = false;
        if (auto status = tree->setVirtualGridViewDataSource(
                projectAssetList_, projectAssetDataSource());
            !status) {
            return status;
        }
        if (auto status = tree->invalidateVirtualGridViewItems(projectAssetList_); !status) {
            return status;
        }
        const auto selectedIndex = projectAssets_.selectedVisibleIndex();
        if (selectedIndex.has_value()) {
            projectAssetSelectionSyncPending_ = true;
        } else {
            projectAssetSelectionSyncPending_ = false;
            if (auto status = tree->clearVirtualGridViewSelection(projectAssetList_); !status) {
                return status;
            }
            observedProjectAssetSelectionIndex_.reset();
            assetInspectorActive_ = false;
        }
        synchronizeViewportSelectionFromHierarchy();
        if (auto status = refreshAuthoringUi(*tree); !status) {
            return status;
        }
    }
    if (auto status = processPendingAnimationFrameSelection(*tree); !status) {
        return status;
    }
    if (auto status = processViewportMarquee(*tree); !status) {
        return status;
    }
    if (auto status = processPendingOutputLocate(*tree); !status) {
        return status;
    }
    if (pendingSelectionStableId_.has_value()) {
        const u32 stableId = *pendingSelectionStableId_;
        pendingSelectionStableId_.reset();
        const auto index = visibleHierarchyIndex(stableId);
        if (!index.has_value()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::EntityNotFound,
                "Editor automatic hierarchy target is no longer visible");
        }
        if (auto status = tree->setTreeViewSelectedIndex(hierarchyTree_, *index); !status) {
            return status;
        }
    }
    auto selection = tree->treeViewSelection(hierarchyTree_);
    if (!selection) {
        return Tina::Core::failure(std::move(selection.error()));
    }
    if (selection->key != selectionKey_) {
        selectionKey_ = selection->key;
        assetInspectorActive_ = false;
        if (!preserveViewportSelectionOnHierarchyPublish_) {
            synchronizeViewportSelectionFromHierarchy();
        }
        ++counters_.hierarchySelectionChanges;
        if (auto status = refreshAuthoringUi(*tree); !status) {
            return status;
        }
    }
    preserveViewportSelectionOnHierarchyPublish_ = false;
    if (!projectAssetSelectionSyncPending_) {
        auto projectSelection = tree->virtualGridViewSelection(projectAssetList_);
        if (!projectSelection) {
            return Tina::Core::failure(std::move(projectSelection.error()));
        }
        if (projectSelection->hasValue() &&
            observedProjectAssetSelectionIndex_ != projectSelection->logicalIndex) {
            if (auto status = projectAssets_.selectVisibleIndex(
                    static_cast<Tina::Core::usize>(projectSelection->logicalIndex));
                !status) {
                return status;
            }
            observedProjectAssetSelectionIndex_ = projectSelection->logicalIndex;
            assetInspectorActive_ =
                preserveNodeInspectorOnProjectAssetSelection_ ? false : true;
            preserveNodeInspectorOnProjectAssetSelection_ = false;
            synchronizeViewportSelectionFromHierarchy();
            ++counters_.projectAssetSelectionChanges;
            if (auto status = refreshAuthoringUi(*tree); !status) {
                return status;
            }
        } else if (!projectSelection->hasValue() &&
                   observedProjectAssetSelectionIndex_.has_value()) {
            observedProjectAssetSelectionIndex_.reset();
            assetInspectorActive_ = false;
            synchronizeViewportSelectionFromHierarchy();
            if (auto status = refreshAuthoringUi(*tree); !status) {
                return status;
            }
        }
    }
    // A valid drag may preserve the node Inspector even when the dragged
    // asset was already the Project Assets selection, so the observer above
    // has no index transition through which to consume the one-shot intent.
    // Once selection synchronization has completed, never carry it into a
    // later, unrelated asset interaction.
    if (!projectAssetSelectionSyncPending_) {
        preserveNodeInspectorOnProjectAssetSelection_ = false;
    }
    auto sourceImportSelection = tree->dataGridSelection(sourceImportGrid_);
    if (!sourceImportSelection) {
        return Tina::Core::failure(std::move(sourceImportSelection.error()));
    }
    if (sourceImportSelection->hasValue() &&
        observedSourceImportSelectionIndex_ !=
            sourceImportSelection->logicalRow) {
        if (sourceImportSelection->logicalRow >= sourceImportUnits_.size()) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Source import DataGrid selected an unavailable intended unit");
        }
        observedSourceImportSelectionIndex_ =
            sourceImportSelection->logicalRow;
        if (auto status = refreshProjectAssetUi(*tree); !status) {
            return status;
        }
    } else if (!sourceImportSelection->hasValue() &&
               observedSourceImportSelectionIndex_.has_value()) {
        observedSourceImportSelectionIndex_.reset();
        if (auto status = refreshProjectAssetUi(*tree); !status) {
            return status;
        }
    }
    if (pendingDocumentTabActivation_.has_value()) {
        const u32 index = *pendingDocumentTabActivation_;
        pendingDocumentTabActivation_.reset();
        if (playSessionActive()) {
            authoringFeedback_ =
                "Stop the isolated play session before switching documents";
            if (auto status = refreshAuthoringUi(*tree); !status) {
                return status;
            }
    } else {
        if (auto status = activateDocumentTab(*tree, index); !status) {
            return status;
        }
    }
    }
    if (pendingViewportToolMode_.has_value()) {
        const ViewportToolMode requestedMode = *pendingViewportToolMode_;
        pendingViewportToolMode_.reset();
        if (requestedMode != viewportToolMode_ && viewportGizmo_.captured) {
            if (viewportTransformGizmo_.snapshot().dragging()) {
                (void)viewportTransformGizmo_.cancelDrag(
                    ViewportPrimaryPointerToken);
            }
            viewportGizmo_.cancelRequested = true;
            if (auto status = processViewportGizmo(*tree); !status) {
                return status;
            }
        }
        if (requestedMode != viewportToolMode_ && tileStroke_.captured) {
            // Changing tools mid-drag abandons the stroke. Without this the
            // gesture would stay captured under a tool that no longer paints,
            // and nothing would ever commit or release it.
            tileStroke_.cancelRequested = true;
            if (auto status = processViewportTileStroke(*tree); !status) {
                return status;
            }
        }
        viewportToolMode_ = requestedMode;
        if (requestedMode != ViewportToolMode::TilePaint &&
            requestedMode != ViewportToolMode::TileErase) {
            hoveredTileCell_.reset();
        }
        Tina::Editor::EditorTransformGizmoMode gizmoMode =
            Tina::Editor::EditorTransformGizmoMode::Translate;
        bool transformTool = true;
        if (viewportToolMode_ == ViewportToolMode::Rotate) {
            gizmoMode = Tina::Editor::EditorTransformGizmoMode::Rotate;
        } else if (viewportToolMode_ == ViewportToolMode::Scale) {
            gizmoMode = Tina::Editor::EditorTransformGizmoMode::Scale;
        } else if (viewportToolMode_ != ViewportToolMode::Translate) {
            transformTool = false;
        }
        if (transformTool && viewportTransformGizmo_.setMode(gizmoMode) !=
                                 Tina::Editor::EditorTransformGizmoOperation::Success) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidConfiguration,
                "editor could not change transform gizmo mode");
        }
        if (auto status = refreshViewportToolUi(*tree); !status) {
            return status;
        }
    }
    if (pendingGizmoOrientationToggle_) {
        pendingGizmoOrientationToggle_ = false;
        if (viewportGizmo_.captured) {
            if (viewportTransformGizmo_.snapshot().dragging()) {
                (void)viewportTransformGizmo_.cancelDrag(
                    ViewportPrimaryPointerToken);
            }
            viewportGizmo_.cancelRequested = true;
            if (auto status = processViewportGizmo(*tree); !status) {
                return status;
            }
        }
        const auto orientation = viewportTransformGizmo_.snapshot().orientation ==
                                         Tina::Editor::EditorTransformGizmoOrientation::World
                                     ? Tina::Editor::EditorTransformGizmoOrientation::Local
                                     : Tina::Editor::EditorTransformGizmoOrientation::World;
        if (viewportTransformGizmo_.setOrientation(orientation) !=
            Tina::Editor::EditorTransformGizmoOperation::Success) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidConfiguration,
                "editor could not change transform gizmo orientation");
        }
        if (auto status = refreshViewportToolUi(*tree); !status) {
            return status;
        }
    }
    if (pendingGizmoSnapToggle_) {
        pendingGizmoSnapToggle_ = false;
        if (viewportGizmo_.captured) {
            if (viewportTransformGizmo_.snapshot().dragging()) {
                (void)viewportTransformGizmo_.cancelDrag(
                    ViewportPrimaryPointerToken);
            }
            viewportGizmo_.cancelRequested = true;
            if (auto status = processViewportGizmo(*tree); !status) {
                return status;
            }
        }
        auto snap = viewportTransformGizmo_.snap();
        snap.enabled = !snap.enabled;
        if (viewportTransformGizmo_.setSnap(snap) !=
            Tina::Editor::EditorTransformGizmoOperation::Success) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidConfiguration,
                "editor could not change transform gizmo snapping");
        }
        if (auto status = refreshViewportToolUi(*tree); !status) {
            return status;
        }
    }
    if (pendingMarqueeSelectionMode_.has_value()) {
        marqueeSelectionMode_ = *pendingMarqueeSelectionMode_;
        pendingMarqueeSelectionMode_.reset();
        if (auto status = refreshViewportToolUi(*tree); !status) {
            return status;
        }
    }
    if (auto status = processViewportGizmo(*tree); !status) {
        return status;
    }
    if (auto status = processViewportTileStroke(*tree); !status) {
        return status;
    }
    if (pendingAutoTransformInput_ && pendingEditorCommand_ == EditorCommand::ApplyTransform) {
        pendingAutoTransformInput_ = false;
        const char* automaticScale =
            workspaceMode_ == WorkspaceMode::World3D ? "1.25" : "1.0";
        if (auto status = tree->setText(inspectorPositionX_, "2.5"); !status) {
            return status;
        }
        if (auto status = tree->setText(inspectorPositionY_, "-1.25"); !status) {
            return status;
        }
        if (auto status = tree->setText(inspectorRotationZ_, "30.0"); !status) {
            return status;
        }
        if (auto status = tree->setText(inspectorScaleX_, automaticScale); !status) {
            return status;
        }
        if (auto status = tree->setText(inspectorScaleY_, automaticScale); !status) {
            return status;
        }
        if (workspaceMode_ == WorkspaceMode::World3D) {
            if (auto status = tree->setText(inspectorPositionZ_, "1.5"); !status) {
                return status;
            }
            if (auto status = tree->setText(inspectorRotationX_, "15.0"); !status) {
                return status;
            }
            if (auto status = tree->setText(inspectorRotationY_, "25.0"); !status) {
                return status;
            }
            if (auto status = tree->setText(inspectorScaleZ_, automaticScale); !status) {
                return status;
            }
        }
    }
    if (pendingAutoParentStableId_.has_value() &&
        pendingEditorCommand_ == EditorCommand::SceneReparent) {
        const u32 parentStableId = *pendingAutoParentStableId_;
        pendingAutoParentStableId_.reset();
        if (auto status = tree->setText(
                inspectorParentStableId_, std::to_string(parentStableId));
            !status) {
            return status;
        }
    }
    if (pendingEditorCommand_.has_value()) {
        if (auto status = executeEditorCommand(*tree); !status) {
            return status;
        }
    }
    counters_.finalSelectionKey = selection->key;
    counters_.finalSelectionIndex = selection->logicalIndex;
    UI::UITreeViewItemDescriptor descriptor{};
    counters_.selectionVerified =
        resolveHierarchyItem(this, selection->logicalIndex, descriptor) &&
        descriptor.key == selection->key;
    if (options_.autoDemo &&
        autoAuthoringStage_ == AutomaticFinalSelectionCommitStage &&
        !pendingSelectionStableId_.has_value() &&
        counters_.automaticFinalSelectionStableId != 0U &&
        stableEntityIdForHierarchyItem(selection->key) ==
            counters_.automaticFinalSelectionStableId) {
        ++autoAuthoringStage_;
        counters_.automaticAuthoringStage = autoAuthoringStage_;
    }
    auto metrics = tree->treeViewMetrics(hierarchyTree_);
    if (!metrics) {
        return Tina::Core::failure(std::move(metrics.error()));
    }
    hierarchyTreeMetrics_ = *metrics;
    if (auto rect = tree->committedLayoutRect(hierarchyTree_); rect) {
        hierarchyTreeRect_ = *rect;
    }
    if (auto status = updateHierarchyPreselectionVisual(*tree); !status) {
        return status;
    }
    if (auto status = refreshHierarchyRenameLayout(*tree); !status) {
        return status;
    }
    counters_.hierarchyLogicalItems = metrics->logicalItemCount;
    if (auto status = refreshOutputAndStatusUi(*tree); !status) {
        return status;
    }
    if (auto status = refreshWorkspacePanelsUi(*tree); !status) {
        return status;
    }
    if (auto status = refreshLayoutDebuggerUi(context, *tree); !status) {
        return status;
    }
    if (auto status = updateSnackbarUi(*tree); !status) {
        return status;
    }
    if (auto status = refreshFileDropFeedbackUi(*tree); !status) {
        return status;
    }
    if (options_.profileUi) {
        auto statistics = context.primaryWindowUIStatistics();
        if (!statistics) {
            return Tina::Core::failure(std::move(statistics.error()));
        }
        if (counters_.uiStatisticsSamples == 0U) {
            counters_.uiStatisticsFirst = *statistics;
        }
        counters_.uiStatisticsLast = *statistics;
        counters_.uiStatisticsPeakPmrBytes =
            (std::max)(counters_.uiStatisticsPeakPmrBytes, statistics->pmrPeakBytes);
        if (options_.profileUiLayoutDrag &&
            layoutDebugProfileCommittedStatisticsPending_) {
            ++counters_.layoutDebugProfileCommittedSamples;
            if (statistics->lastLayoutPassCount != 0U) {
                ++counters_.layoutDebugProfileLayoutRebuildFrames;
            }
            counters_.layoutDebugProfileLayoutMeasuredNodes +=
                statistics->lastLayoutMeasuredNodeCount;
            counters_.layoutDebugProfileLayoutArrangedNodes +=
                statistics->lastLayoutArrangedNodeCount;
            counters_.layoutDebugProfileHitRebuildFrames +=
                statistics->lastHitRebuildCount;
            counters_.layoutDebugProfilePaintSnapshotRebuildFrames +=
                statistics->lastPaintSnapshotRebuildCount;
            layoutDebugProfileCommittedStatisticsPending_ = false;
            if (counters_.layoutDebugProfileMutationFrames ==
                    LayoutDebugProfileMutationFrameCount &&
                !layoutDebugProfileMutationPendingCommit_ &&
                !layoutDebugWindowDragActive_) {
                counters_.layoutDebugProfileCompleted = true;
            }
        }
        const EditorProcessMemorySnapshot processMemory = queryEditorProcessMemory();
        if (counters_.uiStatisticsSamples == 0U) {
            counters_.processFirstUiFrame = processMemory;
        }
        counters_.processLastUiFrame = processMemory;
        recordEditorProcessMemory(counters_, processMemory);
        ++counters_.uiStatisticsSamples;
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::captureRequestedRgbaFrame(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (options_.rgbaOutputUtf8.empty() || counters_.rgbaCaptureAttempted) {
        return Tina::Core::success();
    }

    bool stageReady = false;
    switch (options_.rgbaStage) {
    case RgbaCaptureStage::Workspace:
        stageReady = automaticDemoStartFrame_ != 0U &&
                     counters_.frameUpdates > automaticDemoStartFrame_;
        break;
    case RgbaCaptureStage::ColorPicker:
        stageReady = workspaceMode_ == WorkspaceMode::World2D &&
                     stableEntityIdForHierarchyItem(selectionKey_) == 6U &&
                     pointLightColorPickerVisible_;
        if (stageReady) {
            auto viewportRect = tree.committedLayoutRect(inspectorScroll_);
            if (!viewportRect) {
                return Tina::Core::failure(std::move(viewportRect.error()));
            }
            const auto isVerticallyVisible = [&](UI::UINodeId node)
                -> Tina::Core::Result<bool> {
                auto rect = tree.committedLayoutRect(node);
                if (!rect) {
                    return Tina::Core::failure(std::move(rect.error()));
                }
                return rect->width > 0.0F && rect->height > 0.0F &&
                       rect->y >= viewportRect->y &&
                       rect->y + rect->height <=
                           viewportRect->y + viewportRect->height;
            };
            const std::array<UI::UINodeId, 8> requiredNodes{
                pointLightColorField_.swatchButton,
                pointLightColorField_.textEdit,
                pointLightColorPicker_.channelSliders[0],
                pointLightColorPicker_.channelSliders[1],
                pointLightColorPicker_.channelSliders[2],
                pointLightColorPicker_.channelValueLabels[0],
                pointLightColorPicker_.channelValueLabels[1],
                pointLightColorPicker_.channelValueLabels[2],
            };
            for (const UI::UINodeId node : requiredNodes) {
                auto visible = isVerticallyVisible(node);
                if (!visible) {
                    return Tina::Core::failure(std::move(visible.error()));
                }
                if (!*visible) {
                    stageReady = false;
                    break;
                }
            }
        }
        break;
    case RgbaCaptureStage::DeleteDialog:
        stageReady = pendingSceneDeleteConfirmation_.has_value();
        break;
    }
    if (!stageReady) {
        return Tina::Core::success();
    }
    counters_.rgbaCaptureAttempted = true;
    Tina::Render::IRenderDevice* device = device_;
    if (device == nullptr) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Editor RGBA8 capture requires the active render device");
    }
    auto captured = device->capturePrimaryFrameRgba8();
    if (!captured) {
        return Tina::Core::failure(std::move(captured.error()));
    }
    const u64 expectedBytes =
        static_cast<u64>(captured->width) * captured->height * 4U;
    if (captured->empty() || captured->byteCount() != expectedBytes) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Editor capture returned an invalid RGBA8 frame");
    }
    if (auto status = Tina::Core::writeFile(
            options_.rgbaOutputUtf8,
            std::span<const std::byte>{captured->rgba8Pixels});
        !status) {
        return status;
    }
    counters_.rgbaCaptureWidth = captured->width;
    counters_.rgbaCaptureHeight = captured->height;
    counters_.rgbaCaptureBytes = expectedBytes;
    counters_.rgbaCaptureOutputWritten = true;
    counters_.rgbaCaptureOk = true;
    return Tina::Core::success();
}

auto EditorWorkspaceState::processPendingPointLightColorUpdates(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (std::exchange(pendingPointLightColorPickerToggle_, false)) {
        pointLightColorPickerVisible_ = !pointLightColorPickerVisible_;
        pointLightColorPickerLayout_.visibility =
            pointLightColorPickerVisible_ ? UI::UIVisibility::Visible
                                          : UI::UIVisibility::Collapsed;
        if (auto status = tree.setLayoutStyle(
                pointLightColorPicker_.root, pointLightColorPickerLayout_);
            !status) {
            return status;
        }
        if (options_.autoDemo) {
            if (!pointLightColorPickerVisible_) {
                if (auto status = tree.setScrollViewOffset(
                        inspectorScroll_, UI::UIScrollOffset{});
                    !status) {
                    return status;
                }
            } else {
                auto viewportRect = tree.committedLayoutRect(inspectorScroll_);
                if (!viewportRect) {
                    return Tina::Core::failure(std::move(viewportRect.error()));
                }
                auto colorFieldRect = tree.committedLayoutRect(
                    pointLightColorField_.root);
                if (!colorFieldRect) {
                    return Tina::Core::failure(std::move(colorFieldRect.error()));
                }
                auto metrics = tree.scrollViewMetrics(inspectorScroll_);
                if (!metrics) {
                    return Tina::Core::failure(std::move(metrics.error()));
                }
                const float requestedOffsetY = std::max(
                    0.0F,
                    metrics->offset.y + colorFieldRect->y - viewportRect->y -
                        AutomaticInspectorCaptureInset);
                if (auto status = tree.setScrollViewOffset(
                        inspectorScroll_,
                        UI::UIScrollOffset{.x = 0.0F, .y = requestedOffsetY});
                    !status) {
                    return status;
                }
            }
        }
    }
    if (!pendingPointLightColorChannel_.has_value()) {
        return Tina::Core::success();
    }
    const InspectorPointLightColorChannelRequest request =
        std::exchange(pendingPointLightColorChannel_, std::nullopt).value();
    auto synchronized = UI::synchronizeColorPickerChannel(
        pointLightColorValue_, false, request.channel, request.value);
    if (!synchronized) {
        return Tina::Core::failure(std::move(synchronized.error()));
    }
    pointLightColorMixed_ = false;
    pointLightColorValue_ = synchronized->value;
    if (auto status = tree.setText(
            pointLightColorField_.textEdit, synchronized->text.view()); !status) {
        return status;
    }
    for (Tina::Core::usize index = 0;
         index < synchronized->channelCount; ++index) {
        if (auto status = tree.setSliderValue(
                pointLightColorPicker_.channelSliders[index],
                synchronized->channelValues[index]); !status) {
            return status;
        }
        if (auto status = tree.setText(
                pointLightColorPicker_.channelValueLabels[index],
                synchronized->channelTexts[index].view()); !status) {
            return status;
        }
    }
    auto productTheme = tree.productTheme();
    if (!productTheme) {
        return Tina::Core::failure(std::move(productTheme.error()));
    }
    const UI::UIBoxPaint paint = UI::makePanelBoxPaint(
        *productTheme, pointLightColorValue_, UI::UIElevation::Flat);
    if (auto status = tree.setBoxPaint(
            pointLightColorField_.swatchButton, paint); !status) {
        return status;
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::synchronizePendingProjectAssetSelection(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    if (!projectAssetSelectionSyncPending_) {
        return Tina::Core::success();
    }
    const auto selectedIndex = projectAssets_.selectedVisibleIndex();
    if (!selectedIndex.has_value()) {
        projectAssetSelectionSyncPending_ = false;
        observedProjectAssetSelectionIndex_.reset();
        return tree.clearVirtualGridViewSelection(projectAssetList_);
    }
    auto metrics = tree.virtualGridViewMetrics(projectAssetList_);
    if (!metrics) {
        return Tina::Core::failure(std::move(metrics.error()));
    }
    const u64 visibleItemCount =
        static_cast<u64>(projectAssets_.visibleItemCount());
    if (metrics->logicalColumnCount == 0U ||
        metrics->logicalItemCount != visibleItemCount) {
        return Tina::Core::success();
    }
    if (*selectedIndex >= visibleItemCount) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Editor Project Asset Browser selected index is outside its committed item shape");
    }
    if (auto status = tree.setVirtualGridViewSelectedIndex(
            projectAssetList_, static_cast<u64>(*selectedIndex));
        !status) {
        return status;
    }
    observedProjectAssetSelectionIndex_ = static_cast<u64>(*selectedIndex);
    projectAssetSelectionSyncPending_ = false;
    return Tina::Core::success();
}

auto EditorWorkspaceState::processEditorShortcuts(
    const Tina::FrameActionSnapshot& actions) -> Tina::Core::Status{
    const bool control = actions.isActive(EditorShortcutActions::Control);
    const bool shift = actions.isActive(EditorShortcutActions::Shift);
    // Publish the held modifiers for the tile brush before any early return
    // below: a modal path returns early, and a stale chord would leave a live
    // stroke stuck in the mode it had when the dialog opened.
    tileBrushShiftActive_ = shift;
    tileBrushAltActive_ = actions.isActive(EditorShortcutActions::Alt);
    const auto queue = [this](EditorCommand command) noexcept {
        if (!pendingEditorCommand_.has_value()) {
            pendingEditorCommand_ = command;
        }
    };

    if (pendingSceneAddRequest_.has_value()) {
        if (editorShortcutStarted(actions, EditorShortcutActions::Escape)) {
            queue(EditorCommand::SceneAddCancel);
        }
        return Tina::Core::success();
    }
    if (spriteAssetPickerVisible_) {
        if (editorShortcutStarted(actions, EditorShortcutActions::Escape)) {
            queue(EditorCommand::SpriteAssetPickerCancel);
        } else if (editorShortcutStarted(actions,
                                         EditorShortcutActions::ConfirmRename)) {
            queue(EditorCommand::SpriteAssetPickerConfirm);
        }
        return Tina::Core::success();
    }
    if (pendingSceneDeleteConfirmation_.has_value()) {
        if (editorShortcutStarted(actions, EditorShortcutActions::Escape)) {
            queue(EditorCommand::SceneDeleteCancel);
        }
        return Tina::Core::success();
    }
    if (pendingProjectAssetRemoveConfirmation_.has_value()) {
        if (editorShortcutStarted(actions, EditorShortcutActions::Escape)) {
            queue(EditorCommand::ProjectAssetRemoveCancel);
        }
        return Tina::Core::success();
    }
    if (pendingDirtyCloseKey_.has_value()) {
        if (editorShortcutStarted(actions, EditorShortcutActions::Escape)) {
            queue(EditorCommand::DirtyCloseCancel);
        }
        return Tina::Core::success();
    }
    if (hierarchyRenameVisible_) {
        if (editorShortcutStarted(actions, EditorShortcutActions::Escape)) {
            pendingHierarchyRenameCancel_ = true;
        } else if (editorShortcutStarted(actions, EditorShortcutActions::ConfirmRename)) {
            pendingHierarchyRenameCommit_ = true;
        }
        return Tina::Core::success();
    }
    // Enter confirms the focused Inspector field in place, the same intent
    // boundary a gizmo gets on pointer-up. Blur commits the same edit, so this
    // is only the keyboard path, not the sole one.
    if (inspectorFieldEdit_.field.hasValue() &&
        editorShortcutStarted(actions, EditorShortcutActions::ConfirmRename)) {
        pendingInspectorFieldCommit_ = true;
        return Tina::Core::success();
    }

    // Chords are intentionally limited to control/function keys so text
    // entry in Inspector fields never changes the active viewport tool.
    if (!playSessionActive() && control &&
        editorShortcutStarted(actions, EditorShortcutActions::Save)) {
        queue(shift ? EditorCommand::SaveAs : EditorCommand::Save);
    } else if (!playSessionActive() && control &&
               editorShortcutStarted(actions, EditorShortcutActions::Undo)) {
        queue(EditorCommand::Undo);
    } else if (!playSessionActive() && control &&
               editorShortcutStarted(actions, EditorShortcutActions::Redo)) {
        queue(EditorCommand::Redo);
    } else if (!playSessionActive() && control &&
               editorShortcutStarted(actions, EditorShortcutActions::Duplicate) &&
               sceneDocumentActive() &&
               stableEntityIdForHierarchyItem(selectionKey_) != 0U) {
        queue(EditorCommand::SceneDuplicate);
    } else if (!playSessionActive() &&
               editorShortcutStarted(actions,
                                     EditorShortcutActions::DeleteSelection) &&
               sceneDocumentActive() &&
               stableEntityIdForHierarchyItem(selectionKey_) != 0U) {
        queue(EditorCommand::SceneDelete);
    } else if (!playSessionActive() && control &&
               editorShortcutStarted(actions, EditorShortcutActions::Switch2D)) {
        queue(EditorCommand::SwitchToWorld2D);
    } else if (!playSessionActive() && control &&
               editorShortcutStarted(actions, EditorShortcutActions::Switch3D)) {
        queue(EditorCommand::SwitchToWorld3D);
    } else if (control &&
               editorShortcutStarted(actions, EditorShortcutActions::FrameAll)) {
        queue(EditorCommand::ViewportResetView);
    } else if (control &&
               editorShortcutStarted(actions, EditorShortcutActions::FocusSelection) &&
               stableEntityIdForHierarchyItem(selectionKey_) != 0U) {
        queue(EditorCommand::SceneFocus);
    } else if (editorShortcutStarted(actions, EditorShortcutActions::Play)) {
        queue(EditorCommand::PlayStartOrResume);
    } else if (editorShortcutStarted(actions, EditorShortcutActions::Step) &&
               playSessionActive()) {
        queue(EditorCommand::PlayStep);
    } else if (editorShortcutStarted(actions, EditorShortcutActions::Stop) &&
               playSessionActive()) {
        queue(EditorCommand::PlayStop);
    } else if (editorShortcutStarted(actions, EditorShortcutActions::Escape)) {
        if (playSessionActive()) {
            queue(EditorCommand::PlayStop);
        } else {
            if (viewportGizmo_.captured) {
                if (viewportTransformGizmo_.snapshot().dragging()) {
                    (void)viewportTransformGizmo_.cancelDrag(
                        ViewportPrimaryPointerToken);
                }
                viewportGizmo_.cancelRequested = true;
            }
            if (viewportMarquee_.captured) {
                viewportMarquee_.cancelRequested = true;
            }
            viewportNavigationDrag_ = {};
        }
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::queueEditorCommand(EditorCommand command) noexcept -> bool{
    if (pendingEditorCommand_.has_value()) {
        return false;
    }
    pendingEditorCommand_ = command;
    return true;
}

auto EditorWorkspaceState::processPendingMenuToggle(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!pendingMainMenuToggle_.has_value()) {
        return Tina::Core::success();
    }
    const u32 requestedIndex = *pendingMainMenuToggle_;
    pendingMainMenuToggle_.reset();
    if (requestedIndex >= mainMenus_.size()) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Editor main-menu toggle index is outside the menu registry");
    }
    auto targetOpen = tree.isMenuOpen(mainMenus_[requestedIndex]);
    if (!targetOpen) {
        return Tina::Core::failure(std::move(targetOpen.error()));
    }
    for (u32 index = 0; index < mainMenus_.size(); ++index) {
        const bool open = index == requestedIndex && !*targetOpen;
        if (auto status = tree.setMenuOpen(mainMenus_[index], open); !status) {
            return status;
        }
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::processPendingInspectorSectionUpdates(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    for (NodePropertySectionUi& section : nodePropertySections_) {
        if (!section.collapseUpdatePending) {
            continue;
        }
        auto checked = tree.isChecked(section.collapsible.header);
        if (!checked) {
            return Tina::Core::failure(std::move(checked.error()));
        }
        const UI::UICollapsibleSectionState state =
            UI::synchronizeCollapsibleSectionState(*checked);
        if (auto status = tree.setChecked(
                section.collapsible.header, state.headerChecked);
            !status) {
            return status;
        }
        UI::UILayoutStyle collapsedIndicatorLayout = section.indicatorLayout;
        collapsedIndicatorLayout.visibility =
            state.collapsedIndicatorVisibility;
        if (auto status = tree.setLayoutStyle(
                section.collapsible.collapsedIndicator,
                collapsedIndicatorLayout);
            !status) {
            return status;
        }
        UI::UILayoutStyle expandedIndicatorLayout = section.indicatorLayout;
        expandedIndicatorLayout.visibility = state.expandedIndicatorVisibility;
        if (auto status = tree.setLayoutStyle(
                section.collapsible.expandedIndicator,
                expandedIndicatorLayout);
            !status) {
            return status;
        }
        UI::UILayoutStyle contentLayout = section.contentLayout;
        contentLayout.visibility = state.contentVisibility;
        if (auto status = tree.setLayoutStyle(
                section.collapsible.content, contentLayout);
            !status) {
            return status;
        }
        section.expanded = *checked;
        section.collapseUpdatePending = false;
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::updateHierarchySearch(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    auto text = tree.text(hierarchySearchInput_);
    if (!text) {
        return Tina::Core::failure(std::move(text.error()));
    }
    if (*text == hierarchyFilterUtf8_) {
        return Tina::Core::success();
    }

    const u32 selectedStableId = stableEntityIdForHierarchyItem(selectionKey_);
    try {
        hierarchyFilterUtf8_.assign(text->data(), text->size());
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Editor hierarchy search filter allocation failed");
    }
    if (auto status = tree.invalidateTreeViewItems(hierarchyTree_); !status) {
        return status;
    }

    u64 visibleEntityCount = 0U;
    for (const EditorHierarchyRow& row : hierarchyRows_) {
        if (row.stableId != 0U && hierarchyRowVisible(row)) {
            ++visibleEntityCount;
        }
    }
    std::string countText;
    try {
        countText = std::to_string(visibleEntityCount);
        countText += hierarchyFilterUtf8_.empty() ? " nodes" : " matches";
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Editor hierarchy search result count allocation failed");
    }
    if (auto status = tree.setText(hierarchyCount_, countText); !status) {
        return status;
    }

    const u64 selectedIndex = visibleHierarchyIndex(selectedStableId).value_or(0U);
    return tree.setTreeViewSelectedIndex(hierarchyTree_, selectedIndex);
}

auto EditorWorkspaceState::updateSnackbarUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!snackbarHost_.has_value()) {
        return Tina::Core::success();
    }
    const Tina::Core::MonotonicTimePoint now = snackbarClock_.now();
    if (authoringFeedback_ != lastSnackbarFeedback_) {
        const UI::UISnackbarTone tone = snackbarToneFor(authoringFeedback_);
        const bool offerUndo = tone == UI::UISnackbarTone::Success &&
                               authoringEnabled() && activeCanUndo();
        const UI::UISnackbarMessage message{
            .text = authoringFeedback_,
            .actionLabel = offerUndo
                               ? std::optional<std::string_view>{"Undo"}
                               : std::nullopt,
            .actionToken = offerUndo ? u64{1} : u64{0},
            .tone = tone,
        };
        const Tina::Core::Status enqueued = snackbarHost_->enqueue(message, now);
        try {
            lastSnackbarFeedback_ = authoringFeedback_;
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Editor Snackbar feedback tracking allocation failed");
        }
        if (!enqueued &&
            enqueued.error().code != UI::UIErrorCode::CapacityExceeded) {
            return enqueued;
        }
    }
    (void)snackbarHost_->update(now);

    const UI::UISnackbarPresentation presentation =
        snackbarHost_->presentation();
    if (presentation.revision == observedSnackbarRevision_) {
        return Tina::Core::success();
    }

    UI::UILayoutStyle rootLayout = snackbarRootLayout_;
    rootLayout.visibility = presentation.visible()
                                ? UI::UIVisibility::Visible
                                : UI::UIVisibility::Collapsed;
    if (auto status = tree.setLayoutStyle(snackbarParts_.root, rootLayout);
        !status) {
        return status;
    }
    if (!presentation.visible()) {
        observedSnackbarRevision_ = presentation.revision;
        return Tina::Core::success();
    }

    if (auto status = tree.setText(snackbarParts_.message, presentation.text);
        !status) {
        return status;
    }
    const auto toneIndex = static_cast<Tina::Core::usize>(presentation.tone);
    if (toneIndex >= snackbarToneColors_.size()) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Editor Snackbar resolved an invalid presentation tone");
    }
    if (auto status = tree.setBoxPaint(
            snackbarParts_.toneBar,
            UI::makeSolidBox(snackbarToneColors_[toneIndex], 2.0F));
        !status) {
        return status;
    }
    UI::UILayoutStyle actionLayout = snackbarActionLayout_;
    actionLayout.visibility = presentation.hasAction()
                                  ? UI::UIVisibility::Visible
                                  : UI::UIVisibility::Collapsed;
    if (auto status = tree.setLayoutStyle(snackbarParts_.action, actionLayout);
        !status) {
        return status;
    }
    if (presentation.hasAction()) {
        if (auto status = tree.setText(
                snackbarParts_.action, presentation.actionLabel);
            !status) {
            return status;
        }
    }

    if (presentation.phase == UI::UISnackbarPhase::Entering) {
        if (auto status = tree.beginOpacityTransition(
                snackbarParts_.surface, 1.0F,
                UI::UITransitionSpec{
                    .property = UI::UIAnimatableProperty::Opacity,
                    .duration = snackbarHost_->config().enterDuration,
                    .easing = UI::UIEasing::EaseOut,
                });
            !status) {
            return status;
        }
        if (auto status = tree.beginVisualOffsetTransition(
                snackbarParts_.surface, 0.0F, 0.0F,
                UI::UITransitionSpec{
                    .property = UI::UIAnimatableProperty::VisualOffset,
                    .duration = snackbarHost_->config().enterDuration,
                    .easing = UI::UIEasing::EaseOut,
                });
            !status) {
            return status;
        }
    } else if (presentation.phase == UI::UISnackbarPhase::Exiting) {
        if (auto status = tree.beginOpacityTransition(
                snackbarParts_.surface, 0.0F,
                UI::UITransitionSpec{
                    .property = UI::UIAnimatableProperty::Opacity,
                    .duration = snackbarHost_->config().exitDuration,
                    .easing = UI::UIEasing::EaseInOut,
                });
            !status) {
            return status;
        }
        if (auto status = tree.beginVisualOffsetTransition(
                snackbarParts_.surface, 0.0F, 8.0F,
                UI::UITransitionSpec{
                    .property = UI::UIAnimatableProperty::VisualOffset,
                    .duration = snackbarHost_->config().exitDuration,
                    .easing = UI::UIEasing::EaseInOut,
                });
            !status) {
            return status;
        }
    }
    observedSnackbarRevision_ = presentation.revision;
    return Tina::Core::success();
}

auto EditorWorkspaceState::tileMapEditingContext() const noexcept -> bool{
    const auto* tab = documentTabs_.activeTab();
    return workspaceMode_ == WorkspaceMode::World2D && tab != nullptr &&
           tab->key.kind == Tina::Editor::EditorDocumentKind::TileMap2D;
}

auto EditorWorkspaceState::playSessionActive() const noexcept -> bool{
    return playSession_.has_value() && playSession_->active();
}

auto EditorWorkspaceState::authoringEnabled() const noexcept -> bool{
    return !playSessionActive();
}

auto EditorWorkspaceState::playStartReady() const noexcept -> bool{
    using ImportState =
        Tina::EditorApp::Detail::EditorSourceImportServiceState;
    return sceneDocumentActive() && counters_.runtimePreviewValid &&
           !sourceImportStartPending_ &&
           pendingSourceImportPathsUtf8_.empty() &&
           sourceImportService_.state() == ImportState::Idle &&
           !sourceImportCatalogCommitted_ &&
           !pendingProjectSwitch_.has_value() &&
           !catalogRefreshPending_ &&
           !projectBrowserUiRefreshPending_ &&
           !previewAssetBindingsRefreshPending_;
}

auto EditorWorkspaceState::sceneDocumentActive() const noexcept -> bool{
    const auto* tab = documentTabs_.activeTab();
    return tab != nullptr &&
           (tab->key.kind == Tina::Editor::EditorDocumentKind::World2D ||
            tab->key.kind == Tina::Editor::EditorDocumentKind::World3D);
}

auto EditorWorkspaceState::previewEntityHasAncestorInSelection(
    Tina::Scene::EntityId entity,
    std::span<const u64> selection) const noexcept -> bool{
    if (!previewWorld_.has_value()) {
        return false;
    }
    Tina::Scene::EntityId parent = previewWorld_->parent(entity);
    for (Tina::Core::usize depth = 0;
         depth < ViewportTransformTargetCapacity && parent.hasValue();
         ++depth) {
        const u64 parentStableId = stableIdForPreviewEntity(parent);
        if (parentStableId != 0U &&
            std::find(selection.begin(), selection.end(), parentStableId) !=
                selection.end()) {
            return true;
        }
        const Tina::Scene::EntityId next = previewWorld_->parent(parent);
        if (next == parent) {
            break;
        }
        parent = next;
    }
    return false;
}

auto EditorWorkspaceState::previewEntityHasSelectedAncestor(
    Tina::Scene::EntityId entity) const noexcept -> bool{
    return previewEntityHasAncestorInSelection(
        entity,
        std::span<const u64>{viewportSelectedEntityIds_.data(),
                             viewportSelectedEntityCount_});
}

auto EditorWorkspaceState::processPendingFileDrops(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (fileDropQueueOverflowed_) {
        fileDropQueueOverflowed_ = false;
        authoringFeedback_ =
            "File drop rejected: the queue is full and excess files were not accepted";
        setFileDropFeedback(
            FileDropFeedbackState::Rejected,
            "The drop queue is full; excess files were not accepted");
        sourceImportUiRefreshPending_ = true;
    }
    if (pendingFileDrops_.empty()) {
        if (hierarchyRefreshPending_) {
            hierarchyRefreshPending_ = false;
            if (auto status = refreshHierarchyTree(
                    tree, pendingSelectionStableId_.value_or(0U)); !status) {
                return status;
            }
        }
        return Tina::Core::success();
    }
    PendingFileDrop request = std::move(pendingFileDrops_.front());
    pendingFileDrops_.erase(pendingFileDrops_.begin());

    using ImportState = Tina::EditorApp::Detail::EditorSourceImportServiceState;
    if (!activeProjectWorkspace_.has_value()) {
        authoringFeedback_ = "File drop rejected: open a Tina project first";
        setFileDropFeedback(
            FileDropFeedbackState::Rejected,
            "Open a Tina project before releasing files");
        return Tina::Core::success();
    }
    if (pendingProjectSwitch_.has_value() || playSessionActive() ||
        pendingSceneAddRequest_.has_value() || pendingDirtyCloseKey_.has_value() ||
        pendingSceneDeleteConfirmation_.has_value() ||
        sourceImportService_.state() != ImportState::Idle ||
        !authoringEnabled()) {
        authoringFeedback_ = "File drop queued; it will retry when the Editor is idle";
        setFileDropFeedback(
            FileDropFeedbackState::Accepted,
            "Drop accepted; processing will resume when the Editor is idle");
        sourceImportUiRefreshPending_ = true;
        try {
            pendingFileDrops_.insert(pendingFileDrops_.begin(), std::move(request));
        } catch (const std::bad_alloc&) {
            fileDropQueueOverflowed_ = true;
        }
        return Tina::Core::success();
    }

    const bool droppedOnProjectAssets = logicalPointInRect(
        projectAssetListRect_, request.logicalX, request.logicalY);
    const bool droppedOnViewport = logicalPointInRect(
        viewportLogicalRect_, request.logicalX, request.logicalY);
    if (droppedOnProjectAssets || droppedOnViewport) {
        if (auto status = importSelectedSourceFiles(std::move(request.pathsUtf8)); !status) {
            setFileDropFeedback(
                FileDropFeedbackState::Failed,
                status.error().message);
            if (isFatalSourceImportError(status.error())) {
                return status;
            }
            return reportAuthoringFailure("File drop rejected: ", status.error());
        }
        if (sourceImportService_.state() != ImportState::Running) {
            return Tina::Core::success();
        }
        authoringFeedback_ = "Dropped files are importing into Project Assets";
        setFileDropFeedback(
            FileDropFeedbackState::Processing,
            "Dropped files are validating and cooking into Project Assets");
        return Tina::Core::success();
    }

    authoringFeedback_ = "File drop rejected: use Project Assets or the active viewport";
    setFileDropFeedback(
        FileDropFeedbackState::Rejected,
        "Release files over Project Assets or the active viewport");
    return Tina::Core::success();
}

auto EditorWorkspaceState::importSelectedSourceFiles(
    std::vector<std::string> selectedPathsUtf8) -> Tina::Core::Status
{
    if (!activeProjectWorkspace_.has_value()) {
        authoringFeedback_ = "File import requires an open Tina project";
        return Tina::Core::success();
    }

    return startSourceImport(sourceImportUnits_, std::move(selectedPathsUtf8));
}

auto EditorWorkspaceState::importSourceFromDialog() -> Tina::Core::Status{
    using ImportState =
        Tina::EditorApp::Detail::EditorSourceImportServiceState;
    const bool importBusy =
        pendingProjectSwitch_.has_value() || catalogRefreshPending_ ||
        sourceImportCatalogCommitted_ || projectBrowserUiRefreshPending_ ||
        previewAssetBindingsRefreshPending_ ||
        !pendingSourceImportPathsUtf8_.empty() || sourceImportStartPending_ ||
        retrySourceImportPending_ ||
        sourceImportService_.state() != ImportState::Idle;
    if (importBusy) {
        authoringFeedback_ =
            "Finish the current resource import and refresh before selecting more files";
        return Tina::Core::success();
    }
    constexpr std::array filters{
        Tina::EditorApp::Detail::EditorFileDialogFilter{
            .labelUtf8 = "Tina importable sources",
            .patternUtf8 = "*.recipe;*.gltf;*.glb;*.png;*.jpg;*.jpeg;*.wav",
        },
        Tina::EditorApp::Detail::EditorFileDialogFilter{
            .labelUtf8 = "Images",
            .patternUtf8 = "*.png;*.jpg;*.jpeg",
        },
        Tina::EditorApp::Detail::EditorFileDialogFilter{
            .labelUtf8 = "Audio",
            .patternUtf8 = "*.wav",
        },
        Tina::EditorApp::Detail::EditorFileDialogFilter{
            .labelUtf8 = "All files",
            .patternUtf8 = "*.*",
        },
    };
    auto selected = fileDialog_.openExistingFiles({
        .titleUtf8 = "Select Files to Import",
        .initialDirectoryUtf8 = activeProjectWorkspace_.has_value()
                                    ? activeProjectWorkspace_->sourceRootUtf8()
                                    : std::string_view{},
        .filters = filters,
        .maxSelectedPaths = Tina::EditorApp::Detail::EditorSourceImportUnitCapacity,
    });
    if (!selected) {
        if (selected.error().code == Tina::Core::CoreErrorCode::Unsupported) {
            authoringFeedback_ =
                "Native source import selection is unavailable on this platform";
            return Tina::Core::success();
        }
        if (isFatalSourceImportError(selected.error())) {
            return Tina::Core::failure(std::move(selected.error()));
        }
        return reportAuthoringFailure(
            "Source import selection failed: ", selected.error());
    }
    if (!selected->selected()) {
        authoringFeedback_ = "Source import cancelled";
        return Tina::Core::success();
    }

    if (activeProjectWorkspace_.has_value()) {
        return importSelectedSourceFiles(std::move(selected->selectedPathsUtf8));
    }

    return createTemporaryProjectForImport(
        std::move(selected->selectedPathsUtf8));
}

auto EditorWorkspaceState::activateWorkspace(Tina::PrimaryWindowUITreeUpdater& tree, WorkspaceMode mode) -> Tina::Core::Status{
    if (workspaceMode_ == mode) {
        return Tina::Core::success();
    }
    resetViewportInteractionState();
    workspaceMode_ = mode;
    if (mode == WorkspaceMode::World3D) {
        animationPreview_.setPlaying(false);
        if (animationPreview_.hasAnimator()) {
            animationPreview_.animator().pause();
        }
    }
    if (auto status = refreshHierarchyTree(tree, 0); !status) {
        return status;
    }
    if (auto status = refreshWorkspaceChrome(tree); !status) {
        return status;
    }
    if (auto status = validateRuntimePreview(); !status) {
        return status;
    }
    ++counters_.workspaceSwitches;
    authoringFeedback_ = mode == WorkspaceMode::World2D
                             ? "World2D workspace active"
                             : "World3D workspace active";
    return Tina::Core::success();
}

auto EditorWorkspaceState::sourceImportGridDataSource() const noexcept
    -> UI::UIDataGridDataSource
{
    return UI::UIDataGridDataSource{
        .state = this,
        .rowCount = &EditorWorkspaceState::sourceImportRowCount,
        .columnCount = &EditorWorkspaceState::sourceImportColumnCount,
        .resolveRow = &EditorWorkspaceState::resolveSourceImportRow,
        .resolveColumn = &EditorWorkspaceState::resolveSourceImportColumn,
        .resolveCell = &EditorWorkspaceState::resolveSourceImportCell,
    };
}

auto EditorWorkspaceState::sourceImportRowCount(const void* state) noexcept -> u64
{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    return self != nullptr ? self->sourceImportUnits_.size() : 0U;
}

auto EditorWorkspaceState::sourceImportColumnCount(const void* state) noexcept -> u32
{
    return state != nullptr ? SourceImportColumnCapacity : 0U;
}

auto EditorWorkspaceState::projectAssetDataSource() const noexcept -> UI::UIVirtualGridViewDataSource{
    return UI::UIVirtualGridViewDataSource{
        .state = this,
        .itemCount = &EditorWorkspaceState::projectAssetItemCount,
        .resolveItem = &EditorWorkspaceState::resolveProjectAssetItem,
    };
}

auto EditorWorkspaceState::projectAssetItemCount(const void* state) noexcept -> u64{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    return self != nullptr ? self->projectAssets_.visibleItemCount() : 0U;
}

} // namespace Tina::EditorApp::WorkspaceInternal
