#include "EditorWorkspaceState.hpp"

#include <tina/editor_app/EditorApplication.hpp>

namespace Tina::EditorApp::WorkspaceInternal {

class EditorApplication final : public Tina::IGameApplication {
  public:
    EditorApplication(EditorLaunchOptions options, LifecycleCounters& counters,
                      EditorAssetResources& assetResources,
                      EditorRenderDeviceAccess& renderDeviceAccess) noexcept
        : options_(options), counters_(counters), assetResources_(assetResources),
          renderDeviceAccess_(renderDeviceAccess)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(Tina::GameStartupContext&) override
    {
        auto initialDocuments = createAuthoringDocuments(options_);
        if (!initialDocuments) {
            return Tina::Core::failure(std::move(initialDocuments.error()));
        }
        auto projectAssets = createProjectAssetBrowser(assetResources_);
        if (!projectAssets) {
            return Tina::Core::failure(std::move(projectAssets.error()));
        }
        auto documentTabs = createEditorDocumentTabs(options_.initialWorkspace);
        if (!documentTabs) {
            return Tina::Core::failure(std::move(documentTabs.error()));
        }
        try {
            std::unique_ptr<Tina::IGameState> state =
                std::make_unique<EditorWorkspaceState>(
                    options_, counters_, std::move(initialDocuments->world2D),
                    std::move(initialDocuments->world3D),
                    std::move(initialDocuments->tileMap),
                    std::move(initialDocuments->spriteAnimation),
                    std::move(initialDocuments->world2DSession),
                    std::move(initialDocuments->world3DSession),
                    std::move(*projectAssets), std::move(*documentTabs), assetResources_,
                    renderDeviceAccess_);
            return state;
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                       "Tina Editor could not create workspace sessions");
        }
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
        ++counters_.applicationShutdowns;
    }

  private:
    EditorLaunchOptions options_;
    LifecycleCounters& counters_;
    EditorAssetResources& assetResources_;
    EditorRenderDeviceAccess& renderDeviceAccess_;
};

[[nodiscard]] Tina::InputActionBinding editorShortcutBinding(
    Tina::Platform::Key key,
    Tina::InputActionId action) noexcept
{
    return Tina::InputActionBinding{
        .input = Tina::PrimaryWindowKeyBinding{.key = key},
        .action = action,
        .domain = Tina::InputActionDomain::Frame,
        .composition = Tina::ActionCompositionMode::StrongestMagnitude,
    };
}

[[nodiscard]] Tina::EngineConfig createEngineConfig()
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina Editor";
    config.primaryWindow.title = "Tina Editor - 2D / 3D";
    config.primaryWindow.initialLogicalExtent = {WindowLogicalWidth, WindowLogicalHeight};
    config.primaryWindow.initiallyVisible = true;
    config.renderSceneCapacities.spriteCapacity = EditorViewportSpriteCapacity;
    config.renderSceneCapacities.mesh3DItemCapacity = 8;
    config.renderSceneCapacities.mesh3DBatchCapacity = 4;
    // The editor grid and gizmo overlays use retained Line/Ellipse primitives
    // on top of the editor chrome, so their fixed UI paint/display-list
    // capacities are explicitly widened here.
    config.primaryWindowUICapacities.nodeCapacity = 12U * 1024U;
    config.primaryWindowUICapacities.paintSnapshotCapacity = 32U * 1024U;
    config.primaryWindowUIDisplayListCapacities.commandCapacity = 16U * 1024U;
    config.primaryWindowUIDisplayListCapacities.clipCapacity = 16U * 1024U;
    config.primaryWindowUIDisplayListCapacities.batchCapacity = 16U * 1024U;
    // Anti-alias the editor world pass (mesh edges, sprites); pixel-evidence
    // gates and samples keep the engine-wide 0 default.
    config.renderMsaaSamples = 8;
    using Key = Tina::Platform::Key;
    config.inputActions.bindings = {
        editorShortcutBinding(Key::LeftControl, EditorShortcutActions::Control),
        editorShortcutBinding(Key::RightControl, EditorShortcutActions::Control),
        editorShortcutBinding(Key::LeftShift, EditorShortcutActions::Shift),
        editorShortcutBinding(Key::RightShift, EditorShortcutActions::Shift),
        editorShortcutBinding(Key::S, EditorShortcutActions::Save),
        editorShortcutBinding(Key::Z, EditorShortcutActions::Undo),
        editorShortcutBinding(Key::Y, EditorShortcutActions::Redo),
        editorShortcutBinding(Key::D, EditorShortcutActions::Duplicate),
        editorShortcutBinding(Key::Delete, EditorShortcutActions::DeleteSelection),
        editorShortcutBinding(Key::Digit1, EditorShortcutActions::Switch2D),
        editorShortcutBinding(Key::Digit2, EditorShortcutActions::Switch3D),
        editorShortcutBinding(Key::Digit0, EditorShortcutActions::FrameAll),
        editorShortcutBinding(Key::F, EditorShortcutActions::FocusSelection),
        editorShortcutBinding(Key::F6, EditorShortcutActions::Play),
        editorShortcutBinding(Key::F7, EditorShortcutActions::Step),
        editorShortcutBinding(Key::F8, EditorShortcutActions::Stop),
        editorShortcutBinding(Key::Escape, EditorShortcutActions::Escape),
    };
    return config;
}

[[nodiscard]] std::string runExitReasonName(Tina::RunExitReason exitReason)
{
    switch (exitReason) {
    case Tina::RunExitReason::GameRequestedExitAfterCurrentFrame:
        return "GameRequestedExitAfterCurrentFrame";
    case Tina::RunExitReason::PrimaryWindowRequestedClose:
        return "PrimaryWindowRequestedClose";
    case Tina::RunExitReason::GameStateStackBecameEmpty:
        return "GameStateStackBecameEmpty";
    }
    return "Unknown";
}

[[nodiscard]] Tina::Core::Status verifyLifecycle(Tina::RunExitReason exitReason, const EditorLaunchOptions& options,
                                                 const LifecycleCounters& counters)
{
    if (options.targetFrameCount == 0) {
        if (exitReason != Tina::RunExitReason::PrimaryWindowRequestedClose ||
            counters.stateEnters != 1 || counters.stateExits != 1 ||
            counters.applicationShutdowns != 1 || counters.uiRootsCreated != 1 ||
            counters.uiRootsReleased != 1) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Interactive Tina Editor did not complete its window-owned lifecycle");
        }
        return Tina::Core::success();
    }
    if (!options.rgbaOutputUtf8.empty() &&
        (!counters.rgbaCaptureAttempted || !counters.rgbaCaptureOk ||
         !counters.rgbaCaptureOutputWritten || counters.rgbaCaptureWidth == 0U ||
         counters.rgbaCaptureHeight == 0U ||
         counters.rgbaCaptureBytes !=
             static_cast<u64>(counters.rgbaCaptureWidth) *
                 counters.rgbaCaptureHeight * 4U)) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Tina Editor did not capture the requested RGBA8 frame");
    }
    const bool world2D = options.initialWorkspace == WorkspaceMode::World2D;
    const bool world2DPathConfigured = !options.world2DDocumentPathUtf8.empty();
    const bool world3DPathConfigured = !options.world3DDocumentPathUtf8.empty();
    const bool projectCatalogConfigured =
        !options.catalogRootUtf8.empty() ||
        !options.sourceImport.projectRootUtf8.empty() ||
        counters.projectSwitches != 0U;
    const bool documentPathConfigured = world2D ? world2DPathConfigured
                                                : world3DPathConfigured;
    const bool activeDocumentLoaded = world2D ? counters.world2DDocumentLoaded
                                              : counters.world3DDocumentLoaded;
    const bool activeDocumentDirty = world2D ? counters.world2DDocumentDirty
                                             : counters.world3DDocumentDirty;
    const u64 activeSavedSnapshotBytes = world2D ? counters.world2DSavedSnapshotBytes
                                                 : counters.world3DSavedSnapshotBytes;
    const auto uneditedSessionMatches = [](bool pathConfigured, bool loaded, bool dirty,
                                           u64 savedBytes) noexcept {
        if (!pathConfigured) {
            return !loaded && dirty && savedBytes == 0;
        }
        return loaded ? !dirty && savedBytes > 0 : dirty && savedBytes == 0;
    };
    const u64 expectedCookBytes =
        world2D
            ? Tina::AssetFormat::World2DSnapshotWire::HeaderBytes +
                  InitialAuthoringEntityCount *
                      Tina::AssetFormat::World2DSnapshotWire::EntityBytes +
                  counters.tileMapGameplayBytes
            : Tina::AssetFormat::PrefabWire::HeaderBytes +
                  InitialAuthoringEntityCount * Tina::AssetFormat::PrefabWire::NodeBytes;
    if (exitReason != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Tina Editor stopped for an unexpected reason");
    }
    const bool frameCountMatches = options.sourceImport.importOnStart
                                       ? counters.frameUpdates >= options.targetFrameCount
                                       : counters.frameUpdates == options.targetFrameCount;
    const bool viewportBoundsValid =
        std::isfinite(counters.viewportLogicalX) &&
        std::isfinite(counters.viewportLogicalY) &&
        std::isfinite(counters.viewportLogicalWidth) &&
        std::isfinite(counters.viewportLogicalHeight) &&
        std::isfinite(counters.viewportNormalizedX) &&
        std::isfinite(counters.viewportNormalizedY) &&
        std::isfinite(counters.viewportNormalizedWidth) &&
        std::isfinite(counters.viewportNormalizedHeight) &&
        counters.viewportLogicalWidth > 0.0F &&
        counters.viewportLogicalHeight > 0.0F &&
        counters.viewportNormalizedX >= 0.0F &&
        counters.viewportNormalizedY >= 0.0F &&
        counters.viewportNormalizedWidth > 0.0F &&
        counters.viewportNormalizedHeight > 0.0F &&
        static_cast<double>(counters.viewportNormalizedX) +
                counters.viewportNormalizedWidth <=
            1.0 &&
        static_cast<double>(counters.viewportNormalizedY) +
                counters.viewportNormalizedHeight <=
            1.0;
    const bool selectedTransformFinite =
        std::isfinite(counters.selectedTransformPositionX) &&
        std::isfinite(counters.selectedTransformPositionY) &&
        std::isfinite(counters.selectedTransformPositionZ) &&
        std::isfinite(counters.selectedTransformRotationXDegrees) &&
        std::isfinite(counters.selectedTransformRotationYDegrees) &&
        std::isfinite(counters.selectedTransformRotationZDegrees) &&
        std::isfinite(counters.selectedTransformScaleX) &&
        std::isfinite(counters.selectedTransformScaleY) &&
        std::isfinite(counters.selectedTransformScaleZ) &&
        counters.selectedTransformScaleX > 0.0F &&
        counters.selectedTransformScaleY > 0.0F &&
        counters.selectedTransformScaleZ > 0.0F;
    if (!options.autoDemo) {
        const bool sourceImportFinished =
            !options.sourceImport.importOnStart ||
            (counters.sourceImportStarts == 1U &&
             counters.sourceImportCompletions == 1U &&
             counters.sourceImportFailures == 0U &&
             counters.sourceImportStateCommitted && !counters.sourceImportRunning &&
             !counters.sourceImportReady);
        if (!frameCountMatches || counters.stateEnters != 1U ||
            counters.stateExits != 1U || counters.applicationShutdowns != 1U ||
            counters.uiRootsCreated != 1U || counters.uiRootsReleased != 1U ||
            !counters.selectionVerified || counters.hierarchyLogicalItems == 0U ||
            counters.finalSelectionKey == UI::InvalidUITreeViewItemKey ||
            counters.finalSelectionIndex >= counters.hierarchyLogicalItems ||
            !counters.editorActionsReady || !counters.runtimePreviewValid ||
            counters.editorLayoutRegions == 0U || !counters.viewportLayoutReady ||
            !counters.gpuViewportReady || !counters.viewportGridReady ||
            counters.viewportGridRevision == 0U || counters.viewportGridSegments == 0U ||
            counters.viewportGridAxisLines == 0U ||
            !std::isfinite(counters.viewportZoomPercent) ||
            counters.viewportZoomPercent < 25.0F ||
            counters.viewportZoomPercent > 400.0F ||
            (world2D ? !counters.viewportGrid2DObserved
                     : !counters.viewportGrid3DObserved) ||
            !counters.inspectorScrollConfigured || !counters.projectAssetBrowserReady ||
            !counters.documentTabsReady || counters.documentTabCount == 0U ||
            counters.projectAssetVisibleItems == 0U || !counters.catalogReady ||
            counters.documentPathConfigured != documentPathConfigured ||
            counters.documentLoaded != activeDocumentLoaded ||
            counters.documentDirty != activeDocumentDirty ||
            counters.documentSaved != (documentPathConfigured && !activeDocumentDirty) ||
            counters.savedSnapshotBytes != activeSavedSnapshotBytes ||
            !uneditedSessionMatches(documentPathConfigured, activeDocumentLoaded,
                                    activeDocumentDirty, activeSavedSnapshotBytes) ||
            counters.finalWorkspaceWorld2D != world2D ||
            counters.projectCatalogConfigured != projectCatalogConfigured ||
            counters.runtimePreviewInstantiations == 0U ||
            counters.renderExtractions != counters.frameUpdates ||
            (world2D ? !counters.world2DWorkspaceReady
                     : !counters.world3DWorkspaceReady) ||
            !viewportBoundsValid || !selectedTransformFinite ||
            counters.documentRevision == 0U ||
            counters.gpuViewportDocumentRevision != counters.documentRevision ||
            counters.documentEntityCount == 0U || counters.cookPreviewBytes == 0U ||
            !sourceImportFinished) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Tina Editor finite-frame lifecycle did not satisfy its general invariants");
        }
        return Tina::Core::success();
    }
    if (!frameCountMatches || counters.stateEnters != 1 ||
        counters.stateExits != 1 || counters.applicationShutdowns != 1 || counters.uiRootsCreated != 1 ||
        counters.uiRootsReleased != 1 || !counters.selectionVerified || counters.hierarchyLogicalItems == 0 ||
        !counters.editorActionsReady || !counters.runtimePreviewValid ||
        counters.editorLayoutRegions != EditorLayoutRegionCount || !counters.viewportLayoutReady ||
        !counters.gpuViewportReady || !counters.viewportGridReady ||
        counters.viewportGridRevision == 0U || counters.viewportGridSegments == 0U ||
        counters.viewportGridAxisLines == 0U || counters.viewportZoomPercent < 25.0F ||
        counters.viewportZoomPercent > 400.0F ||
        (options.autoDemo &&
         (!counters.viewportGrid2DObserved || !counters.viewportGrid3DObserved)) ||
        (!options.autoDemo &&
         (world2D ? !counters.viewportGrid2DObserved
                  : !counters.viewportGrid3DObserved)) ||
        !counters.inspectorScrollConfigured ||
        !counters.projectAssetBrowserReady || !counters.documentTabsReady ||
        (!projectCatalogConfigured && counters.projectAssetVisibleItems == 0) ||
        counters.documentTabCount != (options.autoDemo ? 5U : 4U) ||
        (options.autoDemo && counters.projectAssetOpenCount != 1U) ||
        (options.autoDemo &&
         (counters.tabOwnedDocumentLoads != 1U ||
          counters.tabOwnedDocumentSwaps != 2U ||
          counters.previewAssetBindingRefreshes !=
              2U + counters.sourceImportCatalogReloads +
                  counters.navigationCatalogPublishes)) ||
        counters.documentPathConfigured != documentPathConfigured ||
        counters.documentLoaded != activeDocumentLoaded ||
        counters.documentDirty != activeDocumentDirty ||
        counters.documentSaved != (documentPathConfigured && !activeDocumentDirty) ||
        counters.savedSnapshotBytes != activeSavedSnapshotBytes ||
        counters.finalWorkspaceWorld2D != world2D ||
        counters.world2DDocumentPathConfigured != world2DPathConfigured ||
        counters.world3DDocumentPathConfigured != world3DPathConfigured ||
        !counters.catalogReady ||
        counters.projectCatalogConfigured != projectCatalogConfigured ||
        counters.builtInPreviewCatalog == projectCatalogConfigured ||
        counters.runtimePreviewInstantiations < 1 ||
        counters.renderExtractions != counters.frameUpdates ||
        (options.targetFrameCount > 1 &&
         (world2D ? counters.gpuViewportSprites !=
                        counters.catalogResolved2DSprites + counters.tileMapEmittedSprites
                  : counters.gpuViewportMeshes != counters.catalogResolved3DMeshes)) ||
        (world2D ? !counters.world2DWorkspaceReady : !counters.world3DWorkspaceReady) ||
        counters.viewportLogicalWidth <= 0.0F || counters.viewportLogicalHeight <= 0.0F ||
        counters.viewportNormalizedX < 0.0F || counters.viewportNormalizedY < 0.0F ||
        counters.viewportNormalizedWidth <= 0.0F || counters.viewportNormalizedHeight <= 0.0F ||
        static_cast<double>(counters.viewportNormalizedX) + counters.viewportNormalizedWidth > 1.0 ||
        static_cast<double>(counters.viewportNormalizedY) + counters.viewportNormalizedHeight > 1.0 ||
        counters.documentEntityCount != InitialAuthoringEntityCount ||
        counters.cookPreviewBytes != expectedCookBytes) {
        std::string message = "Tina Editor lifecycle counters did not match contract";
        if (options.autoDemo) {
            message += ": stage=";
            message += std::to_string(counters.automaticAuthoringStage);
            message += ", frames=";
            message += std::to_string(counters.frameUpdates);
            message += ", entities=";
            message += std::to_string(counters.documentEntityCount);
            message += ", gizmo(begin/preview/commit/reject)=";
            message += std::to_string(counters.viewportGizmoBegins);
            message += "/";
            message += std::to_string(counters.viewportGizmoPreviews);
            message += "/";
            message += std::to_string(counters.viewportGizmoCommits);
            message += "/";
            message += std::to_string(counters.viewportGizmoRejects);
        }
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   std::move(message));
    }
    if (options.sourceImport.importOnStart &&
        (counters.sourceImportStarts != 1U ||
         counters.sourceImportCompletions != 1U ||
         counters.sourceImportFailures != 0U ||
         !counters.sourceImportStateCommitted || counters.sourceImportRunning ||
         counters.sourceImportReady)) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Tina Editor startup source import did not complete and commit state");
    }
    if (!projectCatalogConfigured && counters.projectSwitches == 0U &&
        (counters.catalogEntryCount != 9U + counters.navigationCatalogPublishes ||
         counters.catalogAssetsLoaded != 7 ||
         counters.catalogGpuTextures != 1 || counters.catalogGpuMeshes != 1 ||
         counters.catalogSpriteBindings != 1 || counters.catalogMeshBindings != 1 ||
         counters.catalogMaterialBindings != 1 || counters.catalogUnresolvedReferences != 0 ||
         (world2D && counters.catalogResolved2DSprites != GpuViewportSpriteCount) ||
         (world2D &&
          (counters.tileMapLayerCount != 2 ||
           counters.tileMapChunkCount != InitialTileMapChunkCount ||
           counters.tileMapAuthoredCells != InitialTileMapCellCount ||
           counters.tileMapCookArtifacts != InitialTileMapChunkCount + 1U ||
           counters.tileMapCookPreviewBytes == 0 ||
           counters.tileMapEmittedSprites != InitialTileMapCellCount)) ||
         ((world2D || options.autoDemo) &&
          (counters.animationFrameCount != 4 ||
           counters.animationCookPreviewBytes == 0 ||
           counters.animationPreviewFrameIndex >= counters.animationFrameCount)) ||
         (!world2D && counters.catalogResolved3DMeshes != GpuViewportMeshCount))) {
        std::string message = "Tina Editor built-in Catalog counters mismatch: entries=";
        message += std::to_string(counters.catalogEntryCount);
        message += ", loaded=";
        message += std::to_string(counters.catalogAssetsLoaded);
        message += ", gpuTextures=";
        message += std::to_string(counters.catalogGpuTextures);
        message += ", gpuMeshes=";
        message += std::to_string(counters.catalogGpuMeshes);
        message += ", spriteBindings=";
        message += std::to_string(counters.catalogSpriteBindings);
        message += ", meshBindings=";
        message += std::to_string(counters.catalogMeshBindings);
        message += ", materialBindings=";
        message += std::to_string(counters.catalogMaterialBindings);
        message += ", unresolved=";
        message += std::to_string(counters.catalogUnresolvedReferences);
        message += ", resolved2D=";
        message += std::to_string(counters.catalogResolved2DSprites);
        message += ", resolved3D=";
        message += std::to_string(counters.catalogResolved3DMeshes);
        message += ", tileLayers=";
        message += std::to_string(counters.tileMapLayerCount);
        message += ", tileChunks=";
        message += std::to_string(counters.tileMapChunkCount);
        message += ", tileCells=";
        message += std::to_string(counters.tileMapAuthoredCells);
        message += ", tileSprites=";
        message += std::to_string(counters.tileMapEmittedSprites);
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   std::move(message));
    }
    if (options.autoDemo) {
        const bool gizmoDeltaFinite =
            std::isfinite(counters.viewportGizmoWorldDeltaX) &&
            std::isfinite(counters.viewportGizmoWorldDeltaY) &&
            std::isfinite(counters.viewportGizmoWorldDeltaZ) &&
            std::isfinite(counters.viewportGizmoRotationDegrees) &&
            std::isfinite(counters.viewportGizmoScaleFactorX) &&
            std::isfinite(counters.viewportGizmoScaleFactorY) &&
            std::isfinite(counters.viewportGizmoScaleFactorZ);
        constexpr float GizmoDeltaEpsilon = 1.0e-5F;
        const bool gizmoTranslated =
            std::abs(counters.viewportGizmoWorldDeltaX) > GizmoDeltaEpsilon ||
            std::abs(counters.viewportGizmoWorldDeltaY) > GizmoDeltaEpsilon ||
            std::abs(counters.viewportGizmoWorldDeltaZ) > GizmoDeltaEpsilon;
        const bool gizmoRotated =
            std::abs(counters.viewportGizmoRotationDegrees) > GizmoDeltaEpsilon;
        const bool gizmoScaled =
            std::abs(counters.viewportGizmoScaleFactorX - 1.0F) >
                GizmoDeltaEpsilon ||
            std::abs(counters.viewportGizmoScaleFactorY - 1.0F) >
                GizmoDeltaEpsilon ||
            std::abs(counters.viewportGizmoScaleFactorZ - 1.0F) >
                GizmoDeltaEpsilon;
        const bool gameplayGenerationMatches =
            world2D
                ? counters.tileMapGameplayGenerations != 0U &&
                      counters.tileMapGameplaySpawnRecords != 0U &&
                      counters.tileMapGameplayBytes != 0U &&
                      counters.tileMapGameplaySourceRevision != 0U
                : counters.tileMapGameplayGenerations == 0U &&
                      counters.tileMapGameplaySpawnRecords == 0U &&
                      counters.tileMapGameplayBytes == 0U &&
                      counters.tileMapGameplaySourceRevision == 0U;
        if (counters.automaticTransformStableId == 0U ||
            counters.hierarchySelectionChanges == 0U ||
            counters.authoringEdits < 3U || counters.inspectorTransactions != 1U ||
            counters.inspectorRejectedTransactions != 0U ||
            counters.viewportGizmoBegins != 3U || counters.viewportGizmoPreviews < 3U ||
            counters.viewportGizmoCommits != 3U ||
            counters.viewportTranslateGizmoCommits != 1U ||
            counters.viewportRotateGizmoCommits != 1U ||
            counters.viewportScaleGizmoCommits != 1U ||
            counters.viewportGroupGizmoCommits != 2U ||
            counters.viewportGroupRotateGizmoCommits != 1U ||
            counters.viewportGroupScaleGizmoCommits != 1U ||
            counters.viewportMaximumGizmoTargets < 2U ||
            counters.viewportGizmoCancels != 0U ||
            counters.viewportGizmoRejects != 0U || !gizmoDeltaFinite ||
            !gizmoTranslated || !gizmoRotated || !gizmoScaled ||
            counters.viewportNavigationBatches < 2U ||
            counters.viewportPan2DInputs == 0U ||
            counters.viewportZoom2DInputs == 0U ||
            counters.viewportOrbit3DInputs == 0U ||
            counters.viewportPan3DInputs == 0U ||
            counters.viewportDolly3DInputs == 0U ||
            counters.viewportMarqueeCommits != 3U ||
            counters.viewportMarqueeReplaceCommits != 1U ||
            counters.viewportMarqueeAddCommits != 1U ||
            counters.viewportMarqueeToggleCommits != 1U ||
            counters.viewportMarqueeSelectionChanges != 3U ||
            counters.viewportMarqueeAddedItems == 0U ||
            counters.viewportMarqueeRemovedItems == 0U ||
            counters.viewportMarqueeMaximumSelection < 2U ||
            counters.viewportMarqueeCancels != 0U ||
            counters.viewportMarqueeRejects != 0U ||
            counters.sceneAddCommands != 1U ||
            counters.sceneDuplicateCommands != 1U ||
            counters.sceneReparentRootCommands != 1U ||
            counters.sceneReparentCommands != 1U ||
            counters.sceneDeleteCommands != 2U ||
            counters.automaticAddedStableId == 0U ||
            counters.automaticDuplicatedStableId == 0U ||
            counters.playStarts != 1U || counters.playPauses != 1U ||
            counters.playStepRequests != 1U || counters.playResumes != 1U ||
            counters.playStops != 1U || counters.playSimulationSteps == 0U ||
            counters.playMaximumSimulationTick == 0U ||
            counters.authoringUndos == 0U ||
            counters.authoringUndos != counters.authoringRedos ||
            counters.animationEdits == 0U || counters.animationUndos == 0U ||
            counters.animationUndos != counters.animationRedos ||
            counters.animationEventEdits < 3U ||
            counters.animationEventRejectedEdits != 0U ||
            counters.animationEventCount == 0U ||
            counters.animationSelectedEventIndex >= counters.animationEventCount ||
            counters.animationDocumentRevision == 0U ||
            counters.animationFrameCount == 0U ||
            counters.animationPreviewFrameIndex >= counters.animationFrameCount ||
            counters.documentRevision == 0U ||
            counters.gpuViewportDocumentRevision != counters.documentRevision ||
            counters.documentUndoDepth == 0U || counters.documentRedoDepth != 0U ||
            !selectedTransformFinite || !gameplayGenerationMatches) {
            std::string message =
                "Tina Editor automatic authoring demo did not finish: stage=";
            message += std::to_string(counters.automaticAuthoringStage);
            message += "/";
            message += std::to_string(AutomaticAuthoringStageCount);
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       std::move(message));
        }
        if (counters.automaticFinalSelectionStableId == 0U ||
            counters.finalSelectionKey == UI::InvalidUITreeViewItemKey ||
            stableEntityIdForHierarchyItem(counters.finalSelectionKey) !=
                counters.automaticFinalSelectionStableId ||
            counters.finalSelectionIndex >= counters.hierarchyLogicalItems) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Tina Editor automatic hierarchy selection did not finish");
        }
        if (counters.workspaceSwitches < 2) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Tina Editor workspace round-trip did not execute two mode switches");
        }
        if (counters.runtimePreviewInstantiations < 8 ||
            !counters.world2DWorkspaceReady || !counters.world3DWorkspaceReady) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Tina Editor workspace round-trip did not validate both runtime previews");
        }
        if (documentPathConfigured) {
            if (counters.authoringSaves != 1 || !counters.documentSaved || counters.documentDirty ||
                counters.savedSnapshotBytes != counters.cookPreviewBytes ||
                activeSavedSnapshotBytes != counters.cookPreviewBytes) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "Tina Editor automatic save did not finish");
            }
        } else if (counters.authoringSaves != 0 || counters.documentSaved || !counters.documentDirty ||
                   counters.savedSnapshotBytes != 0) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "Tina Editor reported an unexpected saved document");
        }
        const bool inactiveSessionMatches =
            world2D ? uneditedSessionMatches(
                          world3DPathConfigured, counters.world3DDocumentLoaded,
                          counters.world3DDocumentDirty, counters.world3DSavedSnapshotBytes)
                    : uneditedSessionMatches(
                          world2DPathConfigured, counters.world2DDocumentLoaded,
                          counters.world2DDocumentDirty, counters.world2DSavedSnapshotBytes);
        if (!inactiveSessionMatches) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Tina Editor changed the inactive workspace session during mode round-trip");
        }
    } else if (!uneditedSessionMatches(
                   world2DPathConfigured, counters.world2DDocumentLoaded,
                   counters.world2DDocumentDirty, counters.world2DSavedSnapshotBytes) ||
               !uneditedSessionMatches(
                   world3DPathConfigured, counters.world3DDocumentLoaded,
                   counters.world3DDocumentDirty, counters.world3DSavedSnapshotBytes)) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Tina Editor initial workspace sessions did not preserve their open baselines");
    }
    return Tina::Core::success();
}

[[nodiscard]] int runEditor(int argumentCount, char** arguments)
{
    auto optionsResult = parseOptions(argumentCount, arguments);
    if (!optionsResult) {
        writeError(optionsResult.error());
        return 2;
    }
    const EditorLaunchOptions options = *optionsResult;

    LifecycleCounters counters{};
    EditorAssetResources assetResources{};
    if (auto status = prepareEditorCatalog(options, assetResources); !status) {
        writeError(status.error());
        return 1;
    }
    EditorRenderDeviceAccess renderDeviceAccess{};
    Tina::Desktop::CreateEngineOptions desktopOptions{};
    desktopOptions.wrapWindowSurfaceRenderDevice =
        [&renderDeviceAccess](std::unique_ptr<Tina::Render::IRenderDevice> device)
            -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
            renderDeviceAccess.set(device.get());
            return device;
        };
    auto hostResult = Tina::Desktop::CreateEngine(createEngineConfig(), std::move(desktopOptions));
    if (!hostResult) {
        writeError(hostResult.error());
        return 1;
    }

    EditorApplication application{options, counters, assetResources, renderDeviceAccess};
    auto runResult = (*hostResult)->run(application);
    if (!runResult) {
        writeError(runResult.error());
        return 1;
    }

    auto lifecycleStatus = verifyLifecycle(*runResult, options, counters);
    if (!lifecycleStatus) {
        writeError(lifecycleStatus.error());
        return 1;
    }

    std::cout << "{\"status\":\"ok\",\"application\":\"TinaEditor\",\"readOnly\":false"
              << ",\"editorModule\":true,\"supports2D\":true,\"supports3D\":true,\"initialWorkspace\":";
    writeJsonString(std::cout, options.initialWorkspace == WorkspaceMode::World2D ? "2d" : "3d");
    std::cout << ",\"finalWorkspace\":";
    writeJsonString(std::cout, counters.finalWorkspaceWorld2D ? "2d" : "3d");
    std::cout << ",\"frames\":" << counters.frameUpdates
              << ",\"targetFrames\":" << options.targetFrameCount
              << ",\"frameDelayMs\":" << options.frameDelayMilliseconds
              << ",\"autoDemo\":" << (options.autoDemo ? "true" : "false") << ",\"exit\":";
    writeJsonString(std::cout, runExitReasonName(*runResult));
    std::cout << ",\"documentPathConfigured\":"
              << (counters.documentPathConfigured ? "true" : "false")
              << ",\"world2DDocumentPath\":";
    writeJsonString(std::cout, options.world2DDocumentPathUtf8);
    std::cout << ",\"world3DDocumentPath\":";
    writeJsonString(std::cout, options.world3DDocumentPathUtf8);
    std::cout << ",\"catalogRoot\":";
    writeJsonString(std::cout, options.catalogRootUtf8);
    std::cout << ",\"projectRoot\":";
    writeJsonString(std::cout, options.sourceImport.projectRootUtf8);
    std::cout << ",\"sourceImportOnStart\":"
              << (options.sourceImport.importOnStart ? "true" : "false")
              << ",\"sourceImportIntendedUnits\":"
              << counters.sourceImportIntendedUnits;
    std::cout << ",\"activeCatalogRoot\":";
    writeJsonString(std::cout, assetResources.catalogRootUtf8);
    std::cout << ",\"documentLoaded\":" << (counters.documentLoaded ? "true" : "false")
              << ",\"world2DDocumentPathConfigured\":"
              << (counters.world2DDocumentPathConfigured ? "true" : "false")
              << ",\"world3DDocumentPathConfigured\":"
              << (counters.world3DDocumentPathConfigured ? "true" : "false")
              << ",\"world2DDocumentLoaded\":"
              << (counters.world2DDocumentLoaded ? "true" : "false")
              << ",\"world3DDocumentLoaded\":"
              << (counters.world3DDocumentLoaded ? "true" : "false")
              << ",\"world2DDocumentDirty\":"
              << (counters.world2DDocumentDirty ? "true" : "false")
              << ",\"world3DDocumentDirty\":"
              << (counters.world3DDocumentDirty ? "true" : "false")
              << ",\"stateEnters\":" << counters.stateEnters << ",\"stateExits\":" << counters.stateExits
              << ",\"applicationShutdowns\":" << counters.applicationShutdowns
              << ",\"uiRootsCreated\":" << counters.uiRootsCreated
              << ",\"uiRootsReleased\":" << counters.uiRootsReleased
              << ",\"hierarchySelectionChanges\":" << counters.hierarchySelectionChanges
              << ",\"hierarchyLogicalItems\":" << counters.hierarchyLogicalItems
              << ",\"projectAssetSelectionChanges\":"
              << counters.projectAssetSelectionChanges
              << ",\"projectAssetOpenCount\":" << counters.projectAssetOpenCount
              << ",\"projectAssetVisibleItems\":" << counters.projectAssetVisibleItems
              << ",\"projectAssetBrowserReady\":"
              << (counters.projectAssetBrowserReady ? "true" : "false")
              << ",\"documentTabCount\":" << counters.documentTabCount
              << ",\"documentTabSwitches\":" << counters.documentTabSwitches
              << ",\"tabOwnedDocumentLoads\":" << counters.tabOwnedDocumentLoads
              << ",\"tabOwnedDocumentSwaps\":" << counters.tabOwnedDocumentSwaps
              << ",\"previewAssetBindingRefreshes\":"
              << counters.previewAssetBindingRefreshes
              << ",\"documentTabsReady\":"
              << (counters.documentTabsReady ? "true" : "false")
              << ",\"editorActionsReady\":"
              << (counters.editorActionsReady ? "true" : "false")
              << ",\"authoringEdits\":" << counters.authoringEdits
              << ",\"authoringUndos\":" << counters.authoringUndos
              << ",\"authoringRedos\":" << counters.authoringRedos
              << ",\"authoringSaves\":" << counters.authoringSaves
              << ",\"inspectorTransactions\":" << counters.inspectorTransactions
              << ",\"inspectorRejectedTransactions\":" << counters.inspectorRejectedTransactions
              << ",\"viewportGizmoBegins\":" << counters.viewportGizmoBegins
              << ",\"viewportGizmoPreviews\":" << counters.viewportGizmoPreviews
              << ",\"viewportGizmoCommits\":" << counters.viewportGizmoCommits
              << ",\"viewportTranslateGizmoCommits\":"
              << counters.viewportTranslateGizmoCommits
              << ",\"viewportRotateGizmoCommits\":"
              << counters.viewportRotateGizmoCommits
              << ",\"viewportScaleGizmoCommits\":"
              << counters.viewportScaleGizmoCommits
              << ",\"viewportGroupGizmoCommits\":"
              << counters.viewportGroupGizmoCommits
              << ",\"viewportGroupRotateGizmoCommits\":"
              << counters.viewportGroupRotateGizmoCommits
              << ",\"viewportGroupScaleGizmoCommits\":"
              << counters.viewportGroupScaleGizmoCommits
              << ",\"viewportMaximumGizmoTargets\":"
              << counters.viewportMaximumGizmoTargets
              << ",\"viewportGizmoCancels\":" << counters.viewportGizmoCancels
              << ",\"viewportGizmoRejects\":" << counters.viewportGizmoRejects
              << ",\"viewportNavigationBatches\":"
              << counters.viewportNavigationBatches
              << ",\"viewportPan2DInputs\":" << counters.viewportPan2DInputs
              << ",\"viewportZoom2DInputs\":" << counters.viewportZoom2DInputs
              << ",\"viewportOrbit3DInputs\":" << counters.viewportOrbit3DInputs
              << ",\"viewportPan3DInputs\":" << counters.viewportPan3DInputs
              << ",\"viewportDolly3DInputs\":" << counters.viewportDolly3DInputs
              << ",\"viewportMarqueeCommits\":" << counters.viewportMarqueeCommits
              << ",\"viewportMarqueeReplaceCommits\":"
              << counters.viewportMarqueeReplaceCommits
              << ",\"viewportMarqueeAddCommits\":"
              << counters.viewportMarqueeAddCommits
              << ",\"viewportMarqueeToggleCommits\":"
              << counters.viewportMarqueeToggleCommits
              << ",\"viewportMarqueeSelectionChanges\":"
              << counters.viewportMarqueeSelectionChanges
              << ",\"viewportMarqueeAddedItems\":"
              << counters.viewportMarqueeAddedItems
              << ",\"viewportMarqueeRemovedItems\":"
              << counters.viewportMarqueeRemovedItems
              << ",\"viewportMarqueeMaximumSelection\":"
              << counters.viewportMarqueeMaximumSelection
              << ",\"viewportMarqueeCancels\":"
              << counters.viewportMarqueeCancels
              << ",\"viewportMarqueeRejects\":"
              << counters.viewportMarqueeRejects
              << ",\"sceneAddCommands\":" << counters.sceneAddCommands
              << ",\"sceneDuplicateCommands\":" << counters.sceneDuplicateCommands
              << ",\"sceneReparentRootCommands\":"
              << counters.sceneReparentRootCommands
              << ",\"sceneReparentCommands\":" << counters.sceneReparentCommands
              << ",\"sceneDeleteCommands\":" << counters.sceneDeleteCommands
              << ",\"rgbaCaptureAttempted\":"
              << (counters.rgbaCaptureAttempted ? "true" : "false")
              << ",\"rgbaCaptureOk\":"
              << (counters.rgbaCaptureOk ? "true" : "false")
              << ",\"rgbaCaptureOutputWritten\":"
              << (counters.rgbaCaptureOutputWritten ? "true" : "false")
              << ",\"rgbaCaptureWidth\":" << counters.rgbaCaptureWidth
              << ",\"rgbaCaptureHeight\":" << counters.rgbaCaptureHeight
              << ",\"rgbaCaptureBytes\":" << counters.rgbaCaptureBytes
              << ",\"rgbaOutput\":";
    writeJsonString(std::cout, options.rgbaOutputUtf8);
    std::cout << ",\"rgbaStage\":";
    writeJsonString(std::cout, rgbaCaptureStageName(options.rgbaStage));
    std::cout
              << ",\"automaticAddedStableId\":" << counters.automaticAddedStableId
              << ",\"automaticDuplicatedStableId\":"
              << counters.automaticDuplicatedStableId
              << ",\"automaticAuthoringStage\":"
              << counters.automaticAuthoringStage
              << ",\"playStarts\":" << counters.playStarts
              << ",\"playPauses\":" << counters.playPauses
              << ",\"playStepRequests\":" << counters.playStepRequests
              << ",\"playResumes\":" << counters.playResumes
              << ",\"playStops\":" << counters.playStops
              << ",\"playSimulationSteps\":" << counters.playSimulationSteps
              << ",\"playMaximumSimulationTick\":"
              << counters.playMaximumSimulationTick
              << ",\"viewportGridRevision\":" << counters.viewportGridRevision
              << ",\"viewportGridSegments\":" << counters.viewportGridSegments
              << ",\"viewportGridMinorLines\":" << counters.viewportGridMinorLines
              << ",\"viewportGridMajorLines\":" << counters.viewportGridMajorLines
              << ",\"viewportGridAxisLines\":" << counters.viewportGridAxisLines
              << ",\"viewportZoomPercent\":" << counters.viewportZoomPercent
              << ",\"viewportGridReady\":"
              << (counters.viewportGridReady ? "true" : "false")
              << ",\"viewportGrid2DObserved\":"
              << (counters.viewportGrid2DObserved ? "true" : "false")
              << ",\"viewportGrid3DObserved\":"
              << (counters.viewportGrid3DObserved ? "true" : "false")
              << ",\"savedSnapshotBytes\":" << counters.savedSnapshotBytes
              << ",\"world2DSavedSnapshotBytes\":" << counters.world2DSavedSnapshotBytes
              << ",\"world3DSavedSnapshotBytes\":" << counters.world3DSavedSnapshotBytes
              << ",\"editorLayoutRegions\":" << counters.editorLayoutRegions
              << ",\"viewportLayoutReady\":" << (counters.viewportLayoutReady ? "true" : "false")
              << ",\"inspectorScrollConfigured\":"
              << (counters.inspectorScrollConfigured ? "true" : "false")
              << ",\"renderExtractions\":" << counters.renderExtractions
              << ",\"gpuViewportSprites\":" << counters.gpuViewportSprites
              << ",\"gpuViewportMeshes\":" << counters.gpuViewportMeshes
              << ",\"catalogReady\":" << (counters.catalogReady ? "true" : "false")
              << ",\"projectCatalogConfigured\":"
              << (counters.projectCatalogConfigured ? "true" : "false")
              << ",\"builtInPreviewCatalog\":"
              << (counters.builtInPreviewCatalog ? "true" : "false")
              << ",\"projectSwitches\":" << counters.projectSwitches
              << ",\"sourceImportStarts\":" << counters.sourceImportStarts
              << ",\"sourceImportCompletions\":" << counters.sourceImportCompletions
              << ",\"sourceImportFailures\":" << counters.sourceImportFailures
              << ",\"sourceImportBusyRetries\":" << counters.sourceImportBusyRetries
              << ",\"sourceImportCatalogReloads\":" << counters.sourceImportCatalogReloads
              << ",\"sourceImportUnitsTotal\":" << counters.sourceImportUnitsTotal
              << ",\"sourceImportUnitsRecooked\":" << counters.sourceImportUnitsRecooked
              << ",\"sourceImportUnitsRemoved\":" << counters.sourceImportUnitsRemoved
              << ",\"sourceImportObjectsReused\":" << counters.sourceImportObjectsReused
              << ",\"sourceImportObjectsCooked\":" << counters.sourceImportObjectsCooked
              << ",\"sourceImportRunning\":"
              << (counters.sourceImportRunning ? "true" : "false")
              << ",\"sourceImportReady\":"
              << (counters.sourceImportReady ? "true" : "false")
              << ",\"sourceImportStateCommitted\":"
              << (counters.sourceImportStateCommitted ? "true" : "false")
              << ",\"catalogEntryCount\":" << counters.catalogEntryCount
              << ",\"catalogAssetsLoaded\":" << counters.catalogAssetsLoaded
              << ",\"catalogGpuTextures\":" << counters.catalogGpuTextures
              << ",\"catalogGpuMeshes\":" << counters.catalogGpuMeshes
              << ",\"catalogSpriteBindings\":" << counters.catalogSpriteBindings
              << ",\"catalogMeshBindings\":" << counters.catalogMeshBindings
              << ",\"catalogMaterialBindings\":" << counters.catalogMaterialBindings
              << ",\"catalogUnresolvedReferences\":"
              << counters.catalogUnresolvedReferences
              << ",\"catalogResolved2DSprites\":" << counters.catalogResolved2DSprites
              << ",\"catalogResolved3DMeshes\":" << counters.catalogResolved3DMeshes
              << ",\"tileMapDocumentRevision\":" << counters.tileMapDocumentRevision
              << ",\"tileMapLayerCount\":" << counters.tileMapLayerCount
              << ",\"tileMapChunkCount\":" << counters.tileMapChunkCount
              << ",\"tileMapAuthoredCells\":" << counters.tileMapAuthoredCells
              << ",\"tileMapCookArtifacts\":" << counters.tileMapCookArtifacts
              << ",\"tileMapCookPreviewBytes\":" << counters.tileMapCookPreviewBytes
              << ",\"tileMapEmittedSprites\":" << counters.tileMapEmittedSprites
              << ",\"tileMapEdits\":" << counters.tileMapEdits
              << ",\"tileMapUndos\":" << counters.tileMapUndos
              << ",\"tileMapRedos\":" << counters.tileMapRedos
              << ",\"tileMapGameplayGenerations\":" << counters.tileMapGameplayGenerations
              << ",\"tileMapGameplaySpawnRecords\":" << counters.tileMapGameplaySpawnRecords
              << ",\"tileMapGameplayBytes\":" << counters.tileMapGameplayBytes
              << ",\"tileMapGameplaySourceRevision\":"
              << counters.tileMapGameplaySourceRevision
              << ",\"navigationBakeRevision\":" << counters.navigationBakeRevision
              << ",\"navigationSourceTileMapRevision\":"
              << counters.navigationSourceTileMapRevision
              << ",\"navigationPayloadBytes\":" << counters.navigationPayloadBytes
              << ",\"navigationCatalogPublishes\":"
              << counters.navigationCatalogPublishes
              << ",\"navigationBakeReady\":"
              << (counters.navigationBakeReady ? "true" : "false")
              << ",\"navigationBakeDirty\":"
              << (counters.navigationBakeDirty ? "true" : "false")
              << ",\"animationDocumentRevision\":" << counters.animationDocumentRevision
              << ",\"animationFrameCount\":" << counters.animationFrameCount
              << ",\"animationCookPreviewBytes\":" << counters.animationCookPreviewBytes
              << ",\"animationPreviewFrameIndex\":" << counters.animationPreviewFrameIndex
              << ",\"animationEventCount\":" << counters.animationEventCount
              << ",\"animationSelectedEventIndex\":" << counters.animationSelectedEventIndex
              << ",\"animationEventEdits\":" << counters.animationEventEdits
              << ",\"animationEventRejectedEdits\":" << counters.animationEventRejectedEdits
              << ",\"animationEdits\":" << counters.animationEdits
              << ",\"animationUndos\":" << counters.animationUndos
              << ",\"animationRedos\":" << counters.animationRedos
              << ",\"animationPlaybackTransitions\":" << counters.animationPlaybackTransitions
              << ",\"workspaceSwitches\":" << counters.workspaceSwitches
              << ",\"world2DWorkspaceReady\":"
              << (counters.world2DWorkspaceReady ? "true" : "false")
              << ",\"world3DWorkspaceReady\":"
              << (counters.world3DWorkspaceReady ? "true" : "false")
              << ",\"gpuViewportDocumentRevision\":" << counters.gpuViewportDocumentRevision
              << ",\"gpuViewportReady\":" << (counters.gpuViewportReady ? "true" : "false")
              << ",\"viewportLogicalX\":" << counters.viewportLogicalX
              << ",\"viewportLogicalY\":" << counters.viewportLogicalY
              << ",\"viewportLogicalWidth\":" << counters.viewportLogicalWidth
              << ",\"viewportLogicalHeight\":" << counters.viewportLogicalHeight
              << ",\"viewportNormalizedX\":" << counters.viewportNormalizedX
              << ",\"viewportNormalizedY\":" << counters.viewportNormalizedY
              << ",\"viewportNormalizedWidth\":" << counters.viewportNormalizedWidth
              << ",\"viewportNormalizedHeight\":" << counters.viewportNormalizedHeight
              << ",\"viewportGizmoWorldDeltaX\":" << counters.viewportGizmoWorldDeltaX
              << ",\"viewportGizmoWorldDeltaY\":" << counters.viewportGizmoWorldDeltaY
              << ",\"viewportGizmoWorldDeltaZ\":" << counters.viewportGizmoWorldDeltaZ
              << ",\"viewportGizmoRotationDegrees\":"
              << counters.viewportGizmoRotationDegrees
              << ",\"viewportGizmoScaleFactorX\":"
              << counters.viewportGizmoScaleFactorX
              << ",\"viewportGizmoScaleFactorY\":"
              << counters.viewportGizmoScaleFactorY
              << ",\"viewportGizmoScaleFactorZ\":"
              << counters.viewportGizmoScaleFactorZ
              << ",\"runtimePreviewInstantiations\":" << counters.runtimePreviewInstantiations
              << ",\"runtimePreviewValid\":" << (counters.runtimePreviewValid ? "true" : "false")
              << ",\"documentRevision\":" << counters.documentRevision
              << ",\"documentEntityCount\":" << counters.documentEntityCount
              << ",\"documentUndoDepth\":" << counters.documentUndoDepth
              << ",\"documentRedoDepth\":" << counters.documentRedoDepth
              << ",\"documentSaved\":" << (counters.documentSaved ? "true" : "false")
              << ",\"documentDirty\":" << (counters.documentDirty ? "true" : "false")
              << ",\"cookPreviewBytes\":" << counters.cookPreviewBytes
              << ",\"selectedTransformPositionX\":" << counters.selectedTransformPositionX
              << ",\"selectedTransformPositionY\":" << counters.selectedTransformPositionY
              << ",\"selectedTransformPositionZ\":" << counters.selectedTransformPositionZ
              << ",\"selectedTransformRotationXDegrees\":"
              << counters.selectedTransformRotationXDegrees
              << ",\"selectedTransformRotationYDegrees\":"
              << counters.selectedTransformRotationYDegrees
              << ",\"selectedTransformRotationZDegrees\":"
              << counters.selectedTransformRotationZDegrees
              << ",\"selectedTransformScaleX\":" << counters.selectedTransformScaleX
              << ",\"selectedTransformScaleY\":" << counters.selectedTransformScaleY
              << ",\"selectedTransformScaleZ\":" << counters.selectedTransformScaleZ
              << ",\"finalSelectionKey\":" << counters.finalSelectionKey
              << ",\"finalSelectionIndex\":" << counters.finalSelectionIndex
              << ",\"selectionVerified\":" << (counters.selectionVerified ? "true" : "false") << "}\n";
    return 0;
}

} // namespace Tina::EditorApp::WorkspaceInternal

namespace Tina::EditorApp {

using namespace Tina::EditorApp::WorkspaceInternal;

int runEditorApplication(int argumentCount, char** arguments)
{
    try {
        return runEditor(argumentCount, arguments);
    } catch (const std::bad_alloc&) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::OutOfMemory,
                                "Tina Editor ran out of memory"};
        writeError(error);
        return 1;
    } catch (const std::exception& exception) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "An exception crossed the Tina Editor boundary"};
        error.addContext("Tina::EditorApp", exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    } catch (...) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "A non-standard exception crossed the Tina Editor boundary"};
        writeError(error);
        return 1;
    }
}

} // namespace Tina::EditorApp
