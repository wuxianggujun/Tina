#include "EditorWorkspaceState.hpp"

#include <tina/core/diagnostics/CrashHandler.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/desktop/UiFontFile.hpp>
#include <tina/editor_app/EditorApplication.hpp>

#include <algorithm>
#include <array>
#include <charconv>

namespace Tina::EditorApp::WorkspaceInternal {

class EditorApplication final : public Tina::IGameApplication {
  public:
    EditorApplication(EditorLaunchOptions options, LifecycleCounters& counters,
                      EditorAssetResources& assetResources) noexcept
        : options_(options), counters_(counters), assetResources_(assetResources)
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
                    std::move(*projectAssets), std::move(*documentTabs), assetResources_);
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

[[nodiscard]] usize effectiveNodeDerivedCapacity(
    usize nodeCapacity,
    usize configuredCapacity) noexcept
{
    return configuredCapacity == 0U ? nodeCapacity : configuredCapacity;
}

[[nodiscard]] usize effectiveComponentStateCapacity(
    usize nodeCapacity,
    usize configuredCapacity,
    usize defaultCapacity) noexcept
{
    return configuredCapacity == 0U
               ? (std::min)(nodeCapacity, defaultCapacity)
               : configuredCapacity;
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
    // Both workspace modes are authored into one retained tree. Their fixed
    // toolbar chrome owns roughly 56 icon tooltips before form/dialog actions,
    // so the general-purpose default of 64 is too small for the Editor.
    config.primaryWindowUICapacities.componentStates.tooltipCapacity = 128U;
    config.primaryWindowUICapacities.layoutDebuggerSnapshotCapacity =
        LayoutDebugProjectionCapacity;
    config.primaryWindowUIDisplayListCapacities.commandCapacity =
        LayoutDebugDisplayListEntryCapacity;
    config.primaryWindowUIDisplayListCapacities.batchCapacity =
        LayoutDebugDisplayListEntryCapacity;
    // The UI DisplayList may consume its full budget, so the scene pass gets its own
    // headroom block on top rather than sharing the same ceiling.
    config.renderDrawCallCapacity = EditorRenderDrawCallCapacity;
    using Key = Tina::Platform::Key;
    config.inputActions.bindings = {
        editorShortcutBinding(Key::LeftControl, EditorShortcutActions::Control),
        editorShortcutBinding(Key::RightControl, EditorShortcutActions::Control),
        editorShortcutBinding(Key::LeftShift, EditorShortcutActions::Shift),
        editorShortcutBinding(Key::RightShift, EditorShortcutActions::Shift),
        editorShortcutBinding(Key::LeftAlt, EditorShortcutActions::Alt),
        editorShortcutBinding(Key::RightAlt, EditorShortcutActions::Alt),
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
        editorShortcutBinding(Key::Enter, EditorShortcutActions::ConfirmRename),
        editorShortcutBinding(Key::KeypadEnter, EditorShortcutActions::ConfirmRename),
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

// Emits `after - before` as a signed decimal. A shrinking counter has to print a
// leading '-' followed by an unsigned magnitude, which no single arithmetic value
// can express once the difference exceeds the signed range, so that branch renders
// the digits itself and hands them over verbatim.
void writeUnsignedDelta(Tina::Core::JsonWriter& writer, std::string_view key, u64 before, u64 after)
{
    if (after >= before) {
        writer.member(key, after - before);
        return;
    }
    // 20 digits for the widest u64 magnitude, plus the sign.
    std::array<char, 24> text{};
    text[0] = '-';
    const auto conversion =
        std::to_chars(text.data() + 1, text.data() + text.size(), before - after);
    writer.rawMember(
        key, std::string_view(text.data(), static_cast<usize>(conversion.ptr - text.data())));
}

void writeProcessMemorySnapshot(
    Tina::Core::JsonWriter& writer,
    std::string_view key,
    const EditorProcessMemorySnapshot& snapshot)
{
    writer.beginObjectMember(key);
    writer.member("sampled", snapshot.sampled);
    writer.member("workingSetBytes", snapshot.workingSetBytes);
    writer.member("peakWorkingSetBytes", snapshot.peakWorkingSetBytes);
    writer.member("privateBytes", snapshot.privateBytes);
    writer.endObject();
}

void writeProcessMemoryDelta(
    Tina::Core::JsonWriter& writer,
    std::string_view key,
    const EditorProcessMemorySnapshot& before,
    const EditorProcessMemorySnapshot& after)
{
    const bool sampled = before.sampled && after.sampled;
    writer.beginObjectMember(key);
    writer.member("sampled", sampled);
    writeUnsignedDelta(writer, "workingSetBytes", before.workingSetBytes,
                       after.workingSetBytes);
    writeUnsignedDelta(writer, "peakWorkingSetBytes", before.peakWorkingSetBytes,
                       after.peakWorkingSetBytes);
    writeUnsignedDelta(writer, "privateBytes", before.privateBytes, after.privateBytes);
    writer.endObject();
}

void writeFrameTimingStatistics(
    Tina::Core::JsonWriter& writer,
    std::string_view key,
    const EditorFrameTimingStatistics& statistics)
{
    const double averageFrameMilliseconds =
        statistics.sampleCount != 0U
            ? statistics.totalSeconds * 1000.0 /
                  static_cast<double>(statistics.sampleCount)
            : 0.0;
    const double averageFramesPerSecond =
        statistics.totalSeconds > 0.0
            ? static_cast<double>(statistics.sampleCount) /
                  statistics.totalSeconds
            : 0.0;
    writer.beginObjectMember(key);
    writer.member("samples", statistics.sampleCount);
    writer.member("averageFrameMilliseconds", averageFrameMilliseconds);
    writer.member("worstFrameMilliseconds", statistics.maximumSeconds * 1000.0);
    writer.member("averageFramesPerSecond", averageFramesPerSecond);
    writer.endObject();
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
    const bool emptySessionCatalog =
        !options.autoDemo && !projectCatalogConfigured;
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
            !counters.catalogReady ||
            counters.documentPathConfigured != documentPathConfigured ||
            counters.documentLoaded != activeDocumentLoaded ||
            counters.documentDirty != activeDocumentDirty ||
            counters.documentSaved != (documentPathConfigured && !activeDocumentDirty) ||
            counters.savedSnapshotBytes != activeSavedSnapshotBytes ||
            !uneditedSessionMatches(documentPathConfigured, activeDocumentLoaded,
                                    activeDocumentDirty, activeSavedSnapshotBytes) ||
            counters.finalWorkspaceWorld2D != world2D ||
            counters.projectCatalogConfigured != projectCatalogConfigured ||
            counters.testFixtureCatalog ||
            (emptySessionCatalog &&
             (counters.catalogEntryCount != 0U ||
              counters.projectAssetVisibleItems != 0U ||
              counters.catalogAssetsLoaded != 0U ||
              counters.catalogGpuTextures != 0U ||
              counters.catalogGpuMeshes != 0U ||
              counters.catalogResolved2DSprites != 0U ||
              counters.catalogResolved3DMeshes != 0U)) ||
            counters.runtimePreviewInstantiations == 0U ||
            counters.renderExtractions != counters.frameUpdates ||
            (world2D ? !counters.world2DWorkspaceReady
                     : !counters.world3DWorkspaceReady) ||
            !viewportBoundsValid || !selectedTransformFinite ||
            counters.documentRevision == 0U ||
            counters.gpuViewportDocumentRevision != counters.documentRevision ||
            counters.cookPreviewBytes == 0U ||
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
        counters.testFixtureCatalog !=
            (options.autoDemo && !projectCatalogConfigured) ||
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
    if (counters.testFixtureCatalog &&
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
        std::string message = "Tina Editor auto-demo fixture Catalog counters mismatch: entries=";
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
    if (options.profileUi) {
        counters.processAfterOptions = queryEditorProcessMemory();
        recordEditorProcessMemory(counters, counters.processAfterOptions);
    }
    EditorAssetResources assetResources{};
    if (auto status = prepareEditorCatalog(options, assetResources); !status) {
        writeError(status.error());
        return 1;
    }
    if (options.profileUi) {
        counters.processAfterCatalog = queryEditorProcessMemory();
        recordEditorProcessMemory(counters, counters.processAfterCatalog);
    }
    Tina::Desktop::CreateEngineOptions desktopOptions{};
    desktopOptions.acceptFileDropEvents = true;
    // The Editor is a shipped product, so its font travels beside TinaEditor.exe rather
    // than being compiled in as a path to whichever machine built it.
    auto uiFont = Tina::Desktop::resolveUiFontBytes();
    if (!uiFont)
    {
        writeError(uiFont.error());
        return 1;
    }
    desktopOptions.uiFontBytes = std::move(uiFont->bytes);
    const Tina::EngineConfig engineConfig = createEngineConfig();
    auto hostResult = Tina::Desktop::CreateEngine(engineConfig, std::move(desktopOptions));
    if (!hostResult) {
        writeError(hostResult.error());
        return 1;
    }
    if (options.profileUi) {
        counters.processAfterEngineCreate = queryEditorProcessMemory();
        recordEditorProcessMemory(counters, counters.processAfterEngineCreate);
    }

    EditorApplication application{options, counters, assetResources};
    auto runResult = (*hostResult)->run(application);
    if (options.profileUi) {
        counters.processAfterRun = queryEditorProcessMemory();
        recordEditorProcessMemory(counters, counters.processAfterRun);
    }
    if (!runResult) {
        writeError(runResult.error());
        return 1;
    }
    if (options.profileUi) {
        hostResult->reset();
        counters.processAfterEngineDestroy = queryEditorProcessMemory();
        recordEditorProcessMemory(counters, counters.processAfterEngineDestroy);
    }

    auto lifecycleStatus = verifyLifecycle(*runResult, options, counters);
    if (!lifecycleStatus) {
        writeError(lifecycleStatus.error());
        return 1;
    }

    const Tina::UI::UIContextCapacityConfig& uiCapacities =
        engineConfig.primaryWindowUICapacities;
    const Tina::UI::UIComponentStateCapacityConfig& componentStateCapacities =
        uiCapacities.componentStates;

    Tina::Core::JsonWriter writer(std::cout);
    writer.beginObject();
    writer.member("status", "ok");
    writer.member("application", "TinaEditor");
    writer.member("readOnly", false);
    writer.member("editorModule", true);
    writer.member("supports2D", true);
    writer.member("supports3D", true);
    writer.member("initialWorkspace",
                  options.initialWorkspace == WorkspaceMode::World2D ? "2d" : "3d");
    writer.member("finalWorkspace", counters.finalWorkspaceWorld2D ? "2d" : "3d");
    writer.member("frames", counters.frameUpdates);
    writer.member("targetFrames", options.targetFrameCount);
    writer.member("frameDelayMs", options.frameDelayMilliseconds);
    writer.member("autoDemo", options.autoDemo);
    writer.member("profileUi", options.profileUi);
    writer.member("profileUiLayoutDrag", options.profileUiLayoutDrag);
    writer.member("configuredUiNodeCapacity", uiCapacities.nodeCapacity);
    writer.member("configuredUiPaintCapacity",
                  effectiveNodeDerivedCapacity(uiCapacities.nodeCapacity,
                                               uiCapacities.paintSnapshotCapacity));
    writer.member("configuredVirtualGridViewStateCapacity",
                  effectiveComponentStateCapacity(
                      uiCapacities.nodeCapacity,
                      componentStateCapacities.virtualGridViewCapacity,
                      Tina::UI::UIComponentStateCapacityConfig::
                          DefaultVirtualGridViewCapacity));
    writer.member("configuredVirtualGridItemStateCapacity",
                  effectiveComponentStateCapacity(
                      uiCapacities.nodeCapacity,
                      componentStateCapacities.virtualGridItemCapacity,
                      Tina::UI::UIComponentStateCapacityConfig::
                          DefaultVirtualGridItemCapacity));
    writer.member("configuredDataGridStateCapacity",
                  effectiveComponentStateCapacity(
                      uiCapacities.nodeCapacity,
                      componentStateCapacities.dataGridCapacity,
                      Tina::UI::UIComponentStateCapacityConfig::
                          DefaultDataGridCapacity));
    writer.member("configuredDataGridColumnStateCapacity",
                  effectiveComponentStateCapacity(
                      uiCapacities.nodeCapacity,
                      componentStateCapacities.dataGridColumnCapacity,
                      Tina::UI::UIComponentStateCapacityConfig::
                          DefaultDataGridColumnCapacity));
    writer.member("configuredDataGridRowStateCapacity",
                  effectiveComponentStateCapacity(
                      uiCapacities.nodeCapacity,
                      componentStateCapacities.dataGridRowCapacity,
                      Tina::UI::UIComponentStateCapacityConfig::
                          DefaultDataGridRowCapacity));
    writer.member("configuredDataGridCellStateCapacity",
                  effectiveComponentStateCapacity(
                      uiCapacities.nodeCapacity,
                      componentStateCapacities.dataGridCellCapacity,
                      Tina::UI::UIComponentStateCapacityConfig::
                          DefaultDataGridCellCapacity));
    writer.member("configuredDisplayListCommandCapacity",
                  engineConfig.primaryWindowUIDisplayListCapacities.commandCapacity);
    writer.member("configuredDisplayListClipCapacity",
                  engineConfig.primaryWindowUIDisplayListCapacities.clipCapacity);
    writer.member("configuredDisplayListBatchCapacity",
                  engineConfig.primaryWindowUIDisplayListCapacities.batchCapacity);
    writer.member("configuredRenderDrawCallCapacity", engineConfig.renderDrawCallCapacity);
    writer.member("configuredRenderMsaaSamples",
                  static_cast<u32>(engineConfig.renderMsaaSamples));
    writer.member("uiStatisticsSamples", counters.uiStatisticsSamples);
    writer.member("uiPmrFirstBytes", counters.uiStatisticsFirst.pmrCurrentBytes);
    writer.member("uiPmrLastBytes", counters.uiStatisticsLast.pmrCurrentBytes);
    writer.member("uiPmrPeakBytes", counters.uiStatisticsPeakPmrBytes);
    writeUnsignedDelta(writer, "uiPmrCurrentDeltaBytes",
                       static_cast<u64>(counters.uiStatisticsFirst.pmrCurrentBytes),
                       static_cast<u64>(counters.uiStatisticsLast.pmrCurrentBytes));
    writer.member("uiPmrFirstAllocationCount",
                  counters.uiStatisticsFirst.pmrAllocationCount);
    writer.member("uiPmrLastAllocationCount",
                  counters.uiStatisticsLast.pmrAllocationCount);
    writeUnsignedDelta(writer, "uiPmrAllocationCountDelta",
                       counters.uiStatisticsFirst.pmrAllocationCount,
                       counters.uiStatisticsLast.pmrAllocationCount);
    writer.member("uiPmrFirstDeallocationCount",
                  counters.uiStatisticsFirst.pmrDeallocationCount);
    writer.member("uiPmrLastDeallocationCount",
                  counters.uiStatisticsLast.pmrDeallocationCount);
    writeUnsignedDelta(writer, "uiPmrDeallocationCountDelta",
                       counters.uiStatisticsFirst.pmrDeallocationCount,
                       counters.uiStatisticsLast.pmrDeallocationCount);
    writer.member("uiPmrFailedAllocationCount",
                  counters.uiStatisticsLast.pmrFailedAllocationCount);
    writer.member("uiPmrInvalidDeallocationCount",
                  counters.uiStatisticsLast.pmrInvalidDeallocationCount);
    writer.member("uiPmrNodePoolBytes", counters.uiStatisticsLast.pmrNodePoolBytes);
    writer.member("uiPmrStateStorageBytes",
                  counters.uiStatisticsLast.pmrStateStorageBytes);
    writer.member("uiPmrScratchReserveBytes",
                  counters.uiStatisticsLast.pmrScratchReserveBytes);
    writer.member("uiPmrIndexAlignedStorageBytes",
                  counters.uiStatisticsLast.pmrIndexAlignedStorageBytes);
    writer.member("uiPmrSnapshotBufferBytes",
                  counters.uiStatisticsLast.pmrSnapshotBufferBytes);
    writer.member("uiPmrGlyphAtlasBytes", counters.uiStatisticsLast.pmrGlyphAtlasBytes);
    writer.member("processWorkingSetBytes", counters.processWorkingSetBytes);
    writer.member("processPrivateBytes", counters.processPrivateBytes);
    writer.member("processPeakWorkingSetBytes", counters.processPeakWorkingSetBytes);
    writer.member("processPeakPrivateBytes", counters.processPeakPrivateBytes);
    writer.beginObjectMember("processMemoryStages");
    writeProcessMemorySnapshot(writer, "afterOptions", counters.processAfterOptions);
    writeProcessMemorySnapshot(writer, "afterCatalog", counters.processAfterCatalog);
    writeProcessMemorySnapshot(writer, "afterEngineCreate",
                               counters.processAfterEngineCreate);
    writeProcessMemorySnapshot(writer, "firstUiFrame", counters.processFirstUiFrame);
    writeProcessMemorySnapshot(writer, "lastUiFrame", counters.processLastUiFrame);
    writeProcessMemorySnapshot(writer, "afterRun", counters.processAfterRun);
    writeProcessMemorySnapshot(writer, "afterEngineDestroy",
                               counters.processAfterEngineDestroy);
    writer.endObject();
    writer.beginObjectMember("processMemoryDeltas");
    writeProcessMemoryDelta(writer, "catalog", counters.processAfterOptions,
                            counters.processAfterCatalog);
    writeProcessMemoryDelta(writer, "engineCreate", counters.processAfterCatalog,
                            counters.processAfterEngineCreate);
    writeProcessMemoryDelta(writer, "uiStartup", counters.processAfterEngineCreate,
                            counters.processFirstUiFrame);
    writeProcessMemoryDelta(writer, "steadyState", counters.processFirstUiFrame,
                            counters.processLastUiFrame);
    writeProcessMemoryDelta(writer, "runTeardown", counters.processLastUiFrame,
                            counters.processAfterRun);
    writeProcessMemoryDelta(writer, "engineDestroy", counters.processAfterRun,
                            counters.processAfterEngineDestroy);
    writer.endObject();
    writer.beginObjectMember("sourceImportProfile");
    writer.member("cookedPayloadBytes", counters.sourceImportCookedPayloadBytes);
    writer.member("transientPoolPeakBytes",
                  counters.sourceImportTransientMemoryPeakBytes);
    writer.member("transientPoolBytesAfterRelease",
                  counters.sourceImportTransientMemoryBytesAfterRelease);
    writer.member("residentCookedFileBytesBefore",
                  counters.sourceImportResidentCookedFileBytesBefore);
    writer.member("residentCookedFileBytesAfterCommit",
                  counters.sourceImportResidentCookedFileBytesAfterCommit);
    writer.beginObjectMember("processStages");
    writeProcessMemorySnapshot(writer, "before", counters.sourceImportProcessBefore);
    writeProcessMemorySnapshot(writer, "afterWorker",
                               counters.sourceImportProcessAfterWorker);
    writeProcessMemorySnapshot(writer, "afterCommit",
                               counters.sourceImportProcessAfterCommit);
    writeProcessMemorySnapshot(writer, "sampledPeak", counters.sourceImportProcessPeak);
    writer.endObject();
    writer.beginObjectMember("processDeltas");
    writeProcessMemoryDelta(writer, "worker", counters.sourceImportProcessBefore,
                            counters.sourceImportProcessAfterWorker);
    writeProcessMemoryDelta(writer, "commit", counters.sourceImportProcessAfterWorker,
                            counters.sourceImportProcessAfterCommit);
    writeProcessMemoryDelta(writer, "total", counters.sourceImportProcessBefore,
                            counters.sourceImportProcessAfterCommit);
    writeProcessMemoryDelta(writer, "sampledPeak", counters.sourceImportProcessBefore,
                            counters.sourceImportProcessPeak);
    writer.endObject();
    writer.beginObjectMember("frameTiming");
    writeFrameTimingStatistics(writer, "overall", counters.frameTimingOverall);
    writeFrameTimingStatistics(writer, "before",
                               counters.frameTimingBeforeSourceImport);
    writeFrameTimingStatistics(writer, "during",
                               counters.frameTimingDuringSourceImport);
    writeFrameTimingStatistics(writer, "after", counters.frameTimingAfterSourceImport);
    writer.endObject();
    // Close sourceImportProfile before emitting the Editor-wide timing fields.
    // This keeps import-only and orchestration measurements unambiguous for
    // profile consumers.
    writer.endObject();
    writer.beginObjectMember("frameTiming");
    writeFrameTimingStatistics(writer, "overall", counters.frameTimingOverall);
    writeFrameTimingStatistics(writer, "updateUi", counters.updateUiTiming);
    writeFrameTimingStatistics(writer, "layoutDebuggerUi", counters.layoutDebuggerUiTiming);
    writeFrameTimingStatistics(writer, "layoutDebuggerDrag",
                               counters.layoutDebuggerDragFrameTiming);
    writer.endObject();
    writer.beginObjectMember("layoutDebugger");
    writer.member("snapshotChangedFrames", counters.layoutDebugSnapshotChangedFrames);
    writer.member("projectionChangedFrames",
                  counters.layoutDebugProjectionChangedFrames);
    writer.member("dragFrames", counters.layoutDebugDragFrames);
    writer.member("allBoundsSuppressedFrames",
                  counters.layoutDebugAllBoundsSuppressedFrames);
    writer.member("profileMutationFrames", counters.layoutDebugProfileMutationFrames);
    writer.member("profileCommittedSamples",
                  counters.layoutDebugProfileCommittedSamples);
    writer.member("profileWarmupFrames", LayoutDebugProfileWarmupFrameCount);
    writer.member("profileRequestedMutationFrames", LayoutDebugProfileMutationFrameCount);
    writer.member("profileCooldownFrames", LayoutDebugProfileCooldownFrameCount);
    writer.member("profileCompleted", counters.layoutDebugProfileCompleted);
    writer.member("profileLayoutRebuildFrames",
                  counters.layoutDebugProfileLayoutRebuildFrames);
    writer.member("profileLayoutMeasuredNodes",
                  counters.layoutDebugProfileLayoutMeasuredNodes);
    writer.member("profileLayoutArrangedNodes",
                  counters.layoutDebugProfileLayoutArrangedNodes);
    writer.member("profileHitRebuildFrames", counters.layoutDebugProfileHitRebuildFrames);
    writer.member("profilePaintSnapshotRebuildFrames",
                  counters.layoutDebugProfilePaintSnapshotRebuildFrames);
    writer.endObject();
    writer.member("uiNodeCapacity", counters.uiStatisticsLast.nodeCapacity);
    writer.member("uiLiveNodeCount", counters.uiStatisticsLast.liveNodeCount);
    writer.member("uiCommittedNodeCount", counters.uiStatisticsLast.committedNodeCount);
    writer.member("uiLayoutRebuilds", counters.uiStatisticsLast.lastLayoutPassCount);
    writer.member("uiHitRebuilds", counters.uiStatisticsLast.lastHitRebuildCount);
    writer.member("uiPaintCacheRebuilds",
                  counters.uiStatisticsLast.lastPaintCacheRebuildCount);
    writer.member("uiPaintSnapshotRebuilds",
                  counters.uiStatisticsLast.lastPaintSnapshotRebuildCount);
    writer.member("uiDirtyQueuePending", counters.uiStatisticsLast.dirtyQueuePendingCount);
    writer.member("exit", runExitReasonName(*runResult));
    writer.member("documentPathConfigured", counters.documentPathConfigured);
    writer.member("world2DDocumentPath", options.world2DDocumentPathUtf8);
    writer.member("world3DDocumentPath", options.world3DDocumentPathUtf8);
    writer.member("catalogRoot", options.catalogRootUtf8);
    writer.member("projectRoot", options.sourceImport.projectRootUtf8);
    writer.member("sourceImportOnStart", options.sourceImport.importOnStart);
    writer.member("sourceImportIntendedUnits", counters.sourceImportIntendedUnits);
    writer.member("activeCatalogRoot", assetResources.catalogRootUtf8);
    writer.member("documentLoaded", counters.documentLoaded);
    writer.member("world2DDocumentPathConfigured",
                  counters.world2DDocumentPathConfigured);
    writer.member("world3DDocumentPathConfigured",
                  counters.world3DDocumentPathConfigured);
    writer.member("world2DDocumentLoaded", counters.world2DDocumentLoaded);
    writer.member("world3DDocumentLoaded", counters.world3DDocumentLoaded);
    writer.member("world2DDocumentDirty", counters.world2DDocumentDirty);
    writer.member("world3DDocumentDirty", counters.world3DDocumentDirty);
    writer.member("stateEnters", counters.stateEnters);
    writer.member("stateExits", counters.stateExits);
    writer.member("applicationShutdowns", counters.applicationShutdowns);
    writer.member("uiRootsCreated", counters.uiRootsCreated);
    writer.member("uiRootsReleased", counters.uiRootsReleased);
    writer.member("hierarchySelectionChanges", counters.hierarchySelectionChanges);
    writer.member("hierarchyLogicalItems", counters.hierarchyLogicalItems);
    writer.member("projectAssetSelectionChanges", counters.projectAssetSelectionChanges);
    writer.member("projectAssetOpenCount", counters.projectAssetOpenCount);
    writer.member("projectAssetVisibleItems", counters.projectAssetVisibleItems);
    writer.member("projectAssetBrowserReady", counters.projectAssetBrowserReady);
    writer.member("documentTabCount", counters.documentTabCount);
    writer.member("documentTabSwitches", counters.documentTabSwitches);
    writer.member("tabOwnedDocumentLoads", counters.tabOwnedDocumentLoads);
    writer.member("tabOwnedDocumentSwaps", counters.tabOwnedDocumentSwaps);
    writer.member("previewAssetBindingRefreshes", counters.previewAssetBindingRefreshes);
    writer.member("documentTabsReady", counters.documentTabsReady);
    writer.member("editorActionsReady", counters.editorActionsReady);
    writer.member("authoringEdits", counters.authoringEdits);
    writer.member("authoringUndos", counters.authoringUndos);
    writer.member("authoringRedos", counters.authoringRedos);
    writer.member("authoringSaves", counters.authoringSaves);
    writer.member("inspectorTransactions", counters.inspectorTransactions);
    writer.member("inspectorRejectedTransactions",
                  counters.inspectorRejectedTransactions);
    writer.member("viewportGizmoBegins", counters.viewportGizmoBegins);
    writer.member("viewportGizmoPreviews", counters.viewportGizmoPreviews);
    writer.member("viewportGizmoCommits", counters.viewportGizmoCommits);
    writer.member("viewportTranslateGizmoCommits",
                  counters.viewportTranslateGizmoCommits);
    writer.member("viewportRotateGizmoCommits", counters.viewportRotateGizmoCommits);
    writer.member("viewportScaleGizmoCommits", counters.viewportScaleGizmoCommits);
    writer.member("viewportGroupGizmoCommits", counters.viewportGroupGizmoCommits);
    writer.member("viewportGroupRotateGizmoCommits",
                  counters.viewportGroupRotateGizmoCommits);
    writer.member("viewportGroupScaleGizmoCommits",
                  counters.viewportGroupScaleGizmoCommits);
    writer.member("viewportMaximumGizmoTargets", counters.viewportMaximumGizmoTargets);
    writer.member("viewportGizmoCancels", counters.viewportGizmoCancels);
    writer.member("viewportGizmoRejects", counters.viewportGizmoRejects);
    writer.member("viewportNavigationBatches", counters.viewportNavigationBatches);
    writer.member("viewportPan2DInputs", counters.viewportPan2DInputs);
    writer.member("viewportZoom2DInputs", counters.viewportZoom2DInputs);
    writer.member("viewportOrbit3DInputs", counters.viewportOrbit3DInputs);
    writer.member("viewportPan3DInputs", counters.viewportPan3DInputs);
    writer.member("viewportDolly3DInputs", counters.viewportDolly3DInputs);
    writer.member("viewportMarqueeCommits", counters.viewportMarqueeCommits);
    writer.member("viewportMarqueeReplaceCommits",
                  counters.viewportMarqueeReplaceCommits);
    writer.member("viewportMarqueeAddCommits", counters.viewportMarqueeAddCommits);
    writer.member("viewportMarqueeToggleCommits", counters.viewportMarqueeToggleCommits);
    writer.member("viewportMarqueeSelectionChanges",
                  counters.viewportMarqueeSelectionChanges);
    writer.member("viewportMarqueeAddedItems", counters.viewportMarqueeAddedItems);
    writer.member("viewportMarqueeRemovedItems", counters.viewportMarqueeRemovedItems);
    writer.member("viewportMarqueeMaximumSelection",
                  counters.viewportMarqueeMaximumSelection);
    writer.member("viewportMarqueeCancels", counters.viewportMarqueeCancels);
    writer.member("viewportMarqueeRejects", counters.viewportMarqueeRejects);
    writer.member("sceneAddCommands", counters.sceneAddCommands);
    writer.member("sceneDuplicateCommands", counters.sceneDuplicateCommands);
    writer.member("sceneReparentRootCommands", counters.sceneReparentRootCommands);
    writer.member("sceneReparentCommands", counters.sceneReparentCommands);
    writer.member("sceneDeleteCommands", counters.sceneDeleteCommands);
    writer.member("rgbaCaptureAttempted", counters.rgbaCaptureAttempted);
    writer.member("rgbaCaptureOk", counters.rgbaCaptureOk);
    writer.member("rgbaCaptureOutputWritten", counters.rgbaCaptureOutputWritten);
    writer.member("rgbaCaptureWidth", counters.rgbaCaptureWidth);
    writer.member("rgbaCaptureHeight", counters.rgbaCaptureHeight);
    writer.member("rgbaCaptureBytes", counters.rgbaCaptureBytes);
    writer.member("rgbaOutput", options.rgbaOutputUtf8);
    writer.member("rgbaStage", rgbaCaptureStageName(options.rgbaStage));
    writer.member("automaticAddedStableId", counters.automaticAddedStableId);
    writer.member("automaticDuplicatedStableId", counters.automaticDuplicatedStableId);
    writer.member("automaticAuthoringStage", counters.automaticAuthoringStage);
    writer.member("playStarts", counters.playStarts);
    writer.member("playPauses", counters.playPauses);
    writer.member("playStepRequests", counters.playStepRequests);
    writer.member("playResumes", counters.playResumes);
    writer.member("playStops", counters.playStops);
    writer.member("playSimulationSteps", counters.playSimulationSteps);
    writer.member("playMaximumSimulationTick", counters.playMaximumSimulationTick);
    writer.member("viewportGridRevision", counters.viewportGridRevision);
    writer.member("viewportGridSegments", counters.viewportGridSegments);
    writer.member("viewportGridMinorLines", counters.viewportGridMinorLines);
    writer.member("viewportGridMajorLines", counters.viewportGridMajorLines);
    writer.member("viewportGridAxisLines", counters.viewportGridAxisLines);
    writer.member("viewportZoomPercent", counters.viewportZoomPercent);
    writer.member("viewportGridReady", counters.viewportGridReady);
    writer.member("viewportGrid2DObserved", counters.viewportGrid2DObserved);
    writer.member("viewportGrid3DObserved", counters.viewportGrid3DObserved);
    writer.member("savedSnapshotBytes", counters.savedSnapshotBytes);
    writer.member("world2DSavedSnapshotBytes", counters.world2DSavedSnapshotBytes);
    writer.member("world3DSavedSnapshotBytes", counters.world3DSavedSnapshotBytes);
    writer.member("editorLayoutRegions", counters.editorLayoutRegions);
    writer.member("viewportLayoutReady", counters.viewportLayoutReady);
    writer.member("inspectorScrollConfigured", counters.inspectorScrollConfigured);
    writer.member("renderExtractions", counters.renderExtractions);
    writer.member("gpuViewportSprites", counters.gpuViewportSprites);
    writer.member("gpuViewportMeshes", counters.gpuViewportMeshes);
    writer.member("catalogReady", counters.catalogReady);
    writer.member("projectCatalogConfigured", counters.projectCatalogConfigured);
    writer.member("testFixtureCatalog", counters.testFixtureCatalog);
    writer.member("projectSwitches", counters.projectSwitches);
    writer.member("sourceImportStarts", counters.sourceImportStarts);
    writer.member("sourceImportCompletions", counters.sourceImportCompletions);
    writer.member("sourceImportFailures", counters.sourceImportFailures);
    writer.member("sourceImportBusyRetries", counters.sourceImportBusyRetries);
    writer.member("sourceImportCatalogReloads", counters.sourceImportCatalogReloads);
    writer.member("sourceImportUnitsTotal", counters.sourceImportUnitsTotal);
    writer.member("sourceImportUnitsRecooked", counters.sourceImportUnitsRecooked);
    writer.member("sourceImportUnitsRemoved", counters.sourceImportUnitsRemoved);
    writer.member("sourceImportObjectsReused", counters.sourceImportObjectsReused);
    writer.member("sourceImportObjectsCooked", counters.sourceImportObjectsCooked);
    writer.member("sourceImportRunning", counters.sourceImportRunning);
    writer.member("sourceImportReady", counters.sourceImportReady);
    writer.member("sourceImportStateCommitted", counters.sourceImportStateCommitted);
    writer.member("catalogEntryCount", counters.catalogEntryCount);
    writer.member("catalogAssetsLoaded", counters.catalogAssetsLoaded);
    writer.member("catalogGpuTextures", counters.catalogGpuTextures);
    writer.member("catalogGpuMeshes", counters.catalogGpuMeshes);
    writer.member("catalogSpriteBindings", counters.catalogSpriteBindings);
    writer.member("catalogMeshBindings", counters.catalogMeshBindings);
    writer.member("catalogMaterialBindings", counters.catalogMaterialBindings);
    writer.member("catalogShaderBindings", counters.catalogShaderBindings);
    writer.member("catalogUnresolvedReferences", counters.catalogUnresolvedReferences);
    writer.member("catalogResolved2DSprites", counters.catalogResolved2DSprites);
    writer.member("catalogResolved3DMeshes", counters.catalogResolved3DMeshes);
    writer.member("tileMapDocumentRevision", counters.tileMapDocumentRevision);
    writer.member("tileMapLayerCount", counters.tileMapLayerCount);
    writer.member("tileMapChunkCount", counters.tileMapChunkCount);
    writer.member("tileMapAuthoredCells", counters.tileMapAuthoredCells);
    writer.member("tileMapCookArtifacts", counters.tileMapCookArtifacts);
    writer.member("tileMapCookPreviewBytes", counters.tileMapCookPreviewBytes);
    writer.member("tileMapEmittedSprites", counters.tileMapEmittedSprites);
    writer.member("tileMapEdits", counters.tileMapEdits);
    writer.member("tileMapUndos", counters.tileMapUndos);
    writer.member("tileMapRedos", counters.tileMapRedos);
    writer.member("tileMapGameplayGenerations", counters.tileMapGameplayGenerations);
    writer.member("tileMapGameplaySpawnRecords", counters.tileMapGameplaySpawnRecords);
    writer.member("tileMapGameplayBytes", counters.tileMapGameplayBytes);
    writer.member("tileMapGameplaySourceRevision",
                  counters.tileMapGameplaySourceRevision);
    writer.member("navigationBakeRevision", counters.navigationBakeRevision);
    writer.member("navigationSourceTileMapRevision",
                  counters.navigationSourceTileMapRevision);
    writer.member("navigationPayloadBytes", counters.navigationPayloadBytes);
    writer.member("navigationCatalogPublishes", counters.navigationCatalogPublishes);
    writer.member("navigationBakeReady", counters.navigationBakeReady);
    writer.member("navigationBakeDirty", counters.navigationBakeDirty);
    writer.member("animationDocumentRevision", counters.animationDocumentRevision);
    writer.member("animationFrameCount", counters.animationFrameCount);
    writer.member("animationCookPreviewBytes", counters.animationCookPreviewBytes);
    writer.member("animationPreviewFrameIndex", counters.animationPreviewFrameIndex);
    writer.member("animationEventCount", counters.animationEventCount);
    writer.member("animationSelectedEventIndex", counters.animationSelectedEventIndex);
    writer.member("animationEventEdits", counters.animationEventEdits);
    writer.member("animationEventRejectedEdits", counters.animationEventRejectedEdits);
    writer.member("animationEdits", counters.animationEdits);
    writer.member("animationUndos", counters.animationUndos);
    writer.member("animationRedos", counters.animationRedos);
    writer.member("animationPlaybackTransitions", counters.animationPlaybackTransitions);
    writer.member("workspaceSwitches", counters.workspaceSwitches);
    writer.member("world2DWorkspaceReady", counters.world2DWorkspaceReady);
    writer.member("world3DWorkspaceReady", counters.world3DWorkspaceReady);
    writer.member("gpuViewportDocumentRevision", counters.gpuViewportDocumentRevision);
    writer.member("gpuViewportReady", counters.gpuViewportReady);
    writer.member("viewportLogicalX", counters.viewportLogicalX);
    writer.member("viewportLogicalY", counters.viewportLogicalY);
    writer.member("viewportLogicalWidth", counters.viewportLogicalWidth);
    writer.member("viewportLogicalHeight", counters.viewportLogicalHeight);
    writer.member("viewportNormalizedX", counters.viewportNormalizedX);
    writer.member("viewportNormalizedY", counters.viewportNormalizedY);
    writer.member("viewportNormalizedWidth", counters.viewportNormalizedWidth);
    writer.member("viewportNormalizedHeight", counters.viewportNormalizedHeight);
    writer.member("viewportGizmoWorldDeltaX", counters.viewportGizmoWorldDeltaX);
    writer.member("viewportGizmoWorldDeltaY", counters.viewportGizmoWorldDeltaY);
    writer.member("viewportGizmoWorldDeltaZ", counters.viewportGizmoWorldDeltaZ);
    writer.member("viewportGizmoRotationDegrees", counters.viewportGizmoRotationDegrees);
    writer.member("viewportGizmoScaleFactorX", counters.viewportGizmoScaleFactorX);
    writer.member("viewportGizmoScaleFactorY", counters.viewportGizmoScaleFactorY);
    writer.member("viewportGizmoScaleFactorZ", counters.viewportGizmoScaleFactorZ);
    writer.member("runtimePreviewInstantiations", counters.runtimePreviewInstantiations);
    writer.member("runtimePreviewValid", counters.runtimePreviewValid);
    writer.member("documentRevision", counters.documentRevision);
    writer.member("documentEntityCount", counters.documentEntityCount);
    writer.member("documentUndoDepth", counters.documentUndoDepth);
    writer.member("documentRedoDepth", counters.documentRedoDepth);
    writer.member("documentSaved", counters.documentSaved);
    writer.member("documentDirty", counters.documentDirty);
    writer.member("cookPreviewBytes", counters.cookPreviewBytes);
    writer.member("selectedTransformPositionX", counters.selectedTransformPositionX);
    writer.member("selectedTransformPositionY", counters.selectedTransformPositionY);
    writer.member("selectedTransformPositionZ", counters.selectedTransformPositionZ);
    writer.member("selectedTransformRotationXDegrees",
                  counters.selectedTransformRotationXDegrees);
    writer.member("selectedTransformRotationYDegrees",
                  counters.selectedTransformRotationYDegrees);
    writer.member("selectedTransformRotationZDegrees",
                  counters.selectedTransformRotationZDegrees);
    writer.member("selectedTransformScaleX", counters.selectedTransformScaleX);
    writer.member("selectedTransformScaleY", counters.selectedTransformScaleY);
    writer.member("selectedTransformScaleZ", counters.selectedTransformScaleZ);
    writer.member("finalSelectionKey", counters.finalSelectionKey);
    writer.member("finalSelectionIndex", counters.finalSelectionIndex);
    writer.member("selectionVerified", counters.selectionVerified);
    writer.endObject();
    TINA_ASSERT(writer.balanced(), "Editor report JSON left a scope open");
    std::cout << '\n';
    return 0;
}

} // namespace Tina::EditorApp::WorkspaceInternal

namespace Tina::EditorApp {

using namespace Tina::EditorApp::WorkspaceInternal;

int runEditorApplication(int argumentCount, char** arguments)
{
    // Installed before any other work: the try/catch below only sees exceptions,
    // while std::terminate (the engine calls it in many invariant checks), an
    // access violation, or a stack overflow would otherwise kill the process
    // without printing anything at all.
    //
    // TinaEditor is a GUI subsystem binary, so stderr is usually not visible.
    // The report file is what the user can actually read afterwards; writeError()
    // appends fatal non-crash exits to the same file.
    // The result is not ignorable here, even though nothing can be done to recover.
    // A false return means the report file could not be opened, so every crash from
    // this point on would reach stderr only -- and this is a GUI-subsystem binary
    // whose stderr nobody sees. Saying so on stderr at least gives an operator who
    // did launch from a console the reason the file they were told to read is
    // missing; silently continuing made an unopenable path indistinguishable from a
    // process that died before it could write.
    if (!Tina::Core::Diagnostics::installCrashHandler(
            Tina::Core::Diagnostics::CrashHandlerConfig{
                .applicationName = "TinaEditor",
                .reportPathUtf8 = editorDiagnosticReportPathUtf8(),
                .captureBacktrace = true,
            })) {
        {
            Tina::Core::JsonWriter writer(std::cerr);
            writer.beginObject();
            writer.member("status", "warning");
            writer.member("application", "TinaEditor");
            writer.member("message",
                          "crash report file could not be opened; fatal errors will reach "
                          "stderr only");
            writer.member("reportPath", editorDiagnosticReportPathUtf8());
            writer.endObject();
        }
        std::cerr << '\n';
    }
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
