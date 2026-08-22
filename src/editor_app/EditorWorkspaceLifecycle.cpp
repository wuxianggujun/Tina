#include "EditorWorkspaceState.hpp"

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

} // namespace

auto EditorWorkspaceState::onExit(Tina::GameStateExitContext&) noexcept -> void{
    if (sourceImportService_.state() ==
        Tina::EditorApp::Detail::EditorSourceImportServiceState::Running) {
        (void)sourceImportService_.cancel();
    }
    counters_.sourceImportRunning = false;
    if (playSession_.has_value()) {
        (void)playSession_->stop();
        playSession_.reset();
    }
    resetViewportInteractionState();
    viewportNavigation_.reset();
    viewport2DNavigationInitialized_ = false;
    viewport3DNavigationInitialized_ = false;
    pendingTileCellEdit_.reset();
    pendingTileLayerId_ = 0;
    for (auto& listener : viewportPointerListeners_) {
        listener.reset();
    }
    for (auto& listener : viewportOrientationCompassPointerBarrierListeners_) {
        listener.reset();
    }
    viewportNormalized_.reset();
    previewBindings_.clear();
    preview3DBindings_.clear();
    previewCamera2D_ = {};
    previewCamera3D_ = {};
    previewTileMap_.reset();
    previewWorld_.reset();
    animationPreview_.resetAnimator();
    releasePreviewAssetBindings();
    iconResolverRegistration_.reset();
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
    ++counters_.stateExits;
}

auto EditorWorkspaceState::initialPolicy() const noexcept -> Tina::GameStatePolicy{
    return {};
}

auto EditorWorkspaceState::updateFrame(Tina::FrameUpdateContext& context) -> Tina::Core::Status{
    ++counters_.frameUpdates;
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
    }
    if (auto status = processEditorShortcuts(context.frameActions()); !status) {
        return status;
    }
    if (pendingProjectSwitch_.has_value() && !projectSwitchBlockedByDirty_) {
        auto workspace = std::move(*pendingProjectSwitch_);
        pendingProjectSwitch_.reset();
        if (auto status = switchLiveProjectCatalog(std::move(workspace)); !status) {
            return status;
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
        previewAssetBindingsRefreshPending_ = false;
        projectBrowserUiRefreshPending_ = true;
    }
    if (previewAssetBindingsRefreshPending_) {
        if (auto status = rebuildLiveCatalogPreview(
                "Catalog preview bindings refreshed at the frame boundary");
            !status) {
            return status;
        }
        previewAssetBindingsRefreshPending_ = false;
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
    if (!context.hasPrimaryWindowUI() || !uiRoot_) {
        return Tina::Core::success();
    }
    auto tree = context.primaryWindowUITreeUpdater(uiRoot_);
    if (!tree) {
        return Tina::Core::failure(std::move(tree.error()));
    }
    if (auto status = processPendingMainMenuToggle(*tree); !status) {
        return status;
    }
    if (auto status = captureRequestedRgbaFrame(*tree); !status) {
        return status;
    }
    if (auto status = updateHierarchySearch(*tree); !status) {
        return status;
    }
    if (auto status = updateSceneAddSearch(*tree); !status) {
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
    if (auto status = processPendingHierarchyReorder(*tree); !status) {
        return status;
    }
    if (pendingSceneAddDialogFocus_) {
        if (auto status = tree->requestFocus(sceneAddSearchInput_); !status) {
            return status;
        }
        pendingSceneAddDialogFocus_ = false;
    } else if (pendingSceneDeleteDialogFocus_) {
        if (auto status = tree->requestFocus(
                sceneDeleteDialog_.actions[SceneDeleteConfirmActionIndex]);
            !status) {
            return status;
        }
        pendingSceneDeleteDialogFocus_ = false;
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
    if (pendingAnimationTimelineRefresh_) {
        pendingAnimationTimelineRefresh_ = false;
        if (auto status = refreshAnimationTimelineUi(*tree); !status) {
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
            assetInspectorActive_ = true;
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
        viewportToolMode_ = requestedMode;
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
    if (auto status = processPendingTileBrush(*tree); !status) {
        return status;
    }
    if (auto status = processPendingInspectorTransformStep(*tree); !status) {
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
    counters_.hierarchyLogicalItems = metrics->logicalItemCount;
    if (auto status = refreshWorkspacePanelsUi(*tree); !status) {
        return status;
    }
    return updateSnackbarUi(*tree);
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
    Tina::Render::IRenderDevice* device = renderDeviceAccess_.get();
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
    if (pointLightColorMixed_) {
        return Tina::Core::success();
    }
    auto synchronized = UI::synchronizeColorPickerChannel(
        pointLightColorValue_, false, request.channel, request.value);
    if (!synchronized) {
        return Tina::Core::failure(std::move(synchronized.error()));
    }
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
    if (pendingSceneDeleteConfirmation_.has_value()) {
        if (editorShortcutStarted(actions, EditorShortcutActions::Escape)) {
            queue(EditorCommand::SceneDeleteCancel);
        }
        return Tina::Core::success();
    }
    if (pendingDirtyCloseKey_.has_value()) {
        if (editorShortcutStarted(actions, EditorShortcutActions::Escape)) {
            queue(EditorCommand::DirtyCloseCancel);
        }
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

auto EditorWorkspaceState::processPendingMainMenuToggle(
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

auto EditorWorkspaceState::processPendingInspectorTransformStep(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!pendingInspectorTransformStep_.has_value()) {
        return Tina::Core::success();
    }
    const InspectorTransformStepRequest request =
        *pendingInspectorTransformStep_;
    const std::array fields{
        inspectorPositionX_, inspectorPositionY_, inspectorPositionZ_,
        inspectorRotationX_, inspectorRotationY_, inspectorRotationZ_,
        inspectorScaleX_, inspectorScaleY_, inspectorScaleZ_,
    };
    const UI::UINodeId field =
        fields[inspectorTransformFieldIndex(request.field)];
    auto text = tree.text(field);
    if (!text) {
        return Tina::Core::failure(std::move(text.error()));
    }
    const UI::UINumberFieldValueSpec spec =
        inspectorTransformNumberSpec(request.field);
    auto current = UI::synchronizeNumberFieldText(*text, spec);
    if (!current) {
        pendingInspectorTransformStep_.reset();
        try {
            authoringFeedback_ =
                "Transform step rejected: enter one finite value instead of Mixed or n/a";
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Transform step rejection feedback allocation failed");
        }
        return Tina::Core::success();
    }
    auto stepped = UI::stepNumberFieldValue(
        current->value, request.stepCount, spec);
    if (!stepped) {
        return Tina::Core::failure(std::move(stepped.error()));
    }
    if (auto status = tree.setText(field, stepped->text.view()); !status) {
        return status;
    }
    pendingInspectorTransformStep_.reset();
    return Tina::Core::success();
}

auto EditorWorkspaceState::processPendingInspectorSectionUpdates(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    for (ComponentSectionUi& section : componentSections_) {
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

auto EditorWorkspaceState::importSelectedSourceFiles(
    std::span<const std::string> selectedPathsUtf8) -> Tina::Core::Status
{
    if (!activeProjectWorkspace_.has_value()) {
        authoringFeedback_ = "File import requires an open Tina project";
        return Tina::Core::success();
    }

    auto ingress = Tina::EditorApp::Detail::prepareEditorSourceImportIngress(
        activeProjectWorkspace_->sourceRootUtf8(), selectedPathsUtf8);
    if (!ingress) {
        return reportAuthoringFailure("File import preparation rejected: ",
                                      ingress.error());
    }

    auto merged = Tina::EditorApp::Detail::mergeEditorSourceImportSelection(
        activeProjectWorkspace_->sourceRootUtf8(),
        sourceImportUnits_,
        ingress->projectPathsUtf8());
    if (!merged) {
        return reportAuthoringFailure("File import selection rejected: ",
                                      merged.error());
    }
    if (auto status = startSourceImport(merged->intendedUnits); !status) {
        return status;
    }
    ingress->commit();
    return Tina::Core::success();
}

auto EditorWorkspaceState::importSourceFromDialog() -> Tina::Core::Status{
    if (sourceImportService_.state() !=
        Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle) {
        authoringFeedback_ =
            "Finish or dismiss the current source import before selecting more files";
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
        return Tina::Core::failure(std::move(selected.error()));
    }
    if (!selected->selected()) {
        authoringFeedback_ = "Source import cancelled";
        return Tina::Core::success();
    }

    if (activeProjectWorkspace_.has_value()) {
        return importSelectedSourceFiles(selected->selectedPathsUtf8);
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
