#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {

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
    viewportNormalized_.reset();
    previewBindings_.clear();
    preview3DBindings_.clear();
    previewCamera2D_ = {};
    previewCamera3D_ = {};
    previewTileMap_.reset();
    previewWorld_.reset();
    animationAnimator_.reset();
    releasePreviewAssetBindings();
    if (uiRoot_) {
        uiRoot_.reset();
        ++counters_.uiRootsReleased;
    }
    if (sourceImportCatalogCommitted_) {
        assetResources_.system.reset();
    }
    cleanupFailedSourceImportStage();
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
    if (pendingProjectSwitch_.has_value()) {
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
        releasePreviewAssetBindings();
        if (auto status = preparePreviewAssetBindings(); !status) {
            return status;
        }
        if (auto status = validateRuntimePreview(); !status) {
            return status;
        }
        ++counters_.previewAssetBindingRefreshes;
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
        const u64 second = (std::max)(
            first + u64{1},
            options_.targetFrameCount - options_.targetFrameCount / u64{6});
        if (!queuedFirstSelection_ && counters_.frameUpdates >= first) {
            const auto stableId = automaticHierarchyStableId(false);
            if (stableId.has_value()) {
                pendingSelectionStableId_ = *stableId;
                counters_.automaticTransformStableId = *stableId;
                pendingViewportTokenColor_ = UI::rgb(0x1A3348);
                pendingViewportToolMode_ = ViewportToolMode::Translate;
                queuedFirstSelection_ = true;
            }
        } else if (queuedFirstSelection_ && !queuedSecondStyleUpdate_ &&
                   counters_.frameUpdates >= second) {
            pendingViewportTokenColor_ = UI::rgb(0x0C141E);
            queuedSecondStyleUpdate_ = true;
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
                (void)queueAutoCommand(EditorCommand::SceneAdd);
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
                if (stableEntityIdForHierarchyItem(selectionKey_) ==
                    counters_.automaticAddedStableId) {
                    (void)queueAutoCommand(EditorCommand::SceneDelete);
                }
                break;
            case 23:
                (void)queueAutoCommand(EditorCommand::PlayStartOrResume);
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
                if (workspaceMode_ == WorkspaceMode::World2D) {
                    ++autoAuthoringStage_;
                } else {
                    (void)queueAutoCommand(EditorCommand::SwitchToWorld2D);
                }
                break;
            case 35:
                (void)queueAutoCommand(EditorCommand::AnimationNextFrame);
                break;
            case 36:
                (void)queueAutoCommand(EditorCommand::AnimationCycleMode);
                break;
            case 37:
                (void)queueAutoCommand(EditorCommand::AnimationUndo);
                break;
            case 38:
                (void)queueAutoCommand(EditorCommand::AnimationRedo);
                break;
            case 39:
                (void)queueAutoCommand(EditorCommand::AnimationCookPreview);
                break;
            case 40:
                if (options_.initialWorkspace == WorkspaceMode::World2D) {
                    ++autoAuthoringStage_;
                } else {
                    (void)queueAutoCommand(EditorCommand::SwitchToWorld3D);
                }
                break;
            case 41:
                (void)queueAutoCommand(EditorCommand::OpenSelectedProjectAsset);
                break;
            case 42:
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
            case 43:
                (void)queueAutoCommand(options_.initialWorkspace == WorkspaceMode::World2D
                                           ? EditorCommand::SwitchToWorld2D
                                           : EditorCommand::SwitchToWorld3D);
                break;
            case 44:
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
            case 45:
                // Allow the final workspace/selection mutations to publish
                // through one complete retained-UI frame before shutdown.
                ++autoAuthoringStage_;
                break;
            default:
                break;
            }
        }
    }
    if (workspaceMode_ == WorkspaceMode::World2D && animationPlaying_ &&
        animationAnimator_.has_value()) {
        auto update = animationAnimator_->update(context.frameTiming().updateDelta);
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
        if (animationAnimator_->isCompleted()) {
            animationPlaying_ = false;
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
    if (playSessionActive() &&
        observedPlaySessionRevision_ != playSession_->snapshot().revision) {
        observedPlaySessionRevision_ = playSession_->snapshot().revision;
        if (auto status = publishRuntimePreviewStatus(*tree); !status) {
            return status;
        }
    }
    if (pendingViewportSliderValue_.has_value()) {
        const float value = std::exchange(pendingViewportSliderValue_, std::nullopt).value();
        if (auto status = tree->setSliderValue(zoomSlider_, value); !status) {
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
        if (auto status = tree->setListViewDataSource(
                sourceImportList_, sourceImportDataSource());
            !status) {
            return status;
        }
        if (auto status = tree->invalidateListViewItems(sourceImportList_); !status) {
            return status;
        }
        if (sourceImportUnits_.empty()) {
            if (auto status = tree->clearListViewSelection(sourceImportList_); !status) {
                return status;
            }
            observedSourceImportSelectionIndex_.reset();
        } else {
            const u64 selectedIndex = std::min<u64>(
                observedSourceImportSelectionIndex_.value_or(0U),
                static_cast<u64>(sourceImportUnits_.size() - 1U));
            if (auto status = tree->setListViewSelectedIndex(
                    sourceImportList_, selectedIndex);
                !status) {
                return status;
            }
            observedSourceImportSelectionIndex_ = selectedIndex;
        }
    }
    if (projectBrowserUiRefreshPending_) {
        projectBrowserUiRefreshPending_ = false;
        if (auto status = tree->setListViewDataSource(
                projectAssetList_, projectAssetDataSource());
            !status) {
            return status;
        }
        if (auto status = tree->invalidateListViewItems(projectAssetList_); !status) {
            return status;
        }
        const auto selectedIndex = projectAssets_.selectedVisibleIndex();
        if (selectedIndex.has_value()) {
            if (auto status = tree->setListViewSelectedIndex(
                    projectAssetList_, *selectedIndex);
                !status) {
                return status;
            }
            observedProjectAssetSelectionIndex_ = *selectedIndex;
        } else {
            if (auto status = tree->clearListViewSelection(projectAssetList_); !status) {
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
    if (pendingViewportTokenColor_.has_value()) {
        const UI::UIStraightSrgba8Color color = *pendingViewportTokenColor_;
        pendingViewportTokenColor_.reset();
        if (auto status = tree->setStyleColorToken(viewportToken_, color); !status) {
            return status;
        }
        ++counters_.styleTokenUpdates;
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
    auto projectSelection = tree->listViewSelection(projectAssetList_);
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
    auto sourceImportSelection = tree->listViewSelection(sourceImportList_);
    if (!sourceImportSelection) {
        return Tina::Core::failure(std::move(sourceImportSelection.error()));
    }
    if (sourceImportSelection->hasValue() &&
        observedSourceImportSelectionIndex_ !=
            sourceImportSelection->logicalIndex) {
        if (sourceImportSelection->logicalIndex >= sourceImportUnits_.size()) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Source import ListView selected an unavailable intended unit");
        }
        observedSourceImportSelectionIndex_ =
            sourceImportSelection->logicalIndex;
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
    auto metrics = tree->treeViewMetrics(hierarchyTree_);
    if (!metrics) {
        return Tina::Core::failure(std::move(metrics.error()));
    }
    counters_.hierarchyLogicalItems = metrics->logicalItemCount;
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
        if (pendingDirtyCloseKey_.has_value()) {
            queue(EditorCommand::DirtyCloseCancel);
        } else if (playSessionActive()) {
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

auto EditorWorkspaceState::importSourceFromDialog() -> Tina::Core::Status{
    if (!activeProjectWorkspace_.has_value()) {
        authoringFeedback_ = "Open or create a project before importing source assets";
        return Tina::Core::success();
    }
    if (sourceImportService_.state() !=
        Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle) {
        authoringFeedback_ =
            "Finish or dismiss the current source import before selecting more files";
        return Tina::Core::success();
    }
    constexpr std::array filters{
        Tina::EditorApp::Detail::EditorFileDialogFilter{
            .labelUtf8 = "Tina recipe or glTF",
            .patternUtf8 = "*.recipe;*.gltf;*.glb",
        },
        Tina::EditorApp::Detail::EditorFileDialogFilter{
            .labelUtf8 = "All files",
            .patternUtf8 = "*.*",
        },
    };
    auto selected = fileDialog_.openExistingFiles({
        .titleUtf8 = "Import Source into Tina Project",
        .initialDirectoryUtf8 = activeProjectWorkspace_->sourceRootUtf8(),
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

    auto merged = Tina::EditorApp::Detail::mergeEditorSourceImportSelection(
        activeProjectWorkspace_->sourceRootUtf8(),
        sourceImportUnits_,
        selected->selectedPathsUtf8);
    if (!merged) {
        return reportAuthoringFailure("Source import selection rejected: ",
                                      merged.error());
    }
    return startSourceImport(merged->intendedUnits);
}

auto EditorWorkspaceState::activateWorkspace(Tina::PrimaryWindowUITreeUpdater& tree, WorkspaceMode mode) -> Tina::Core::Status{
    if (workspaceMode_ == mode) {
        return Tina::Core::success();
    }
    resetViewportInteractionState();
    workspaceMode_ = mode;
    if (mode == WorkspaceMode::World3D) {
        animationPlaying_ = false;
        if (animationAnimator_.has_value()) {
            animationAnimator_->pause();
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

auto EditorWorkspaceState::sourceImportDataSource() const noexcept -> UI::UIListViewDataSource{
    return UI::UIListViewDataSource{
        .state = this,
        .itemCount = &EditorWorkspaceState::sourceImportItemCount,
        .resolveItem = &EditorWorkspaceState::resolveSourceImportItem,
    };
}

auto EditorWorkspaceState::sourceImportItemCount(const void* state) noexcept -> u64{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    return self != nullptr ? self->sourceImportUnits_.size() : 0U;
}

auto EditorWorkspaceState::projectAssetDataSource() const noexcept -> UI::UIListViewDataSource{
    return UI::UIListViewDataSource{
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
