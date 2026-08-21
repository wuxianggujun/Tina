#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {

auto EditorWorkspaceState::showSceneDeleteConfirmation(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (pendingSceneDeleteConfirmation_.has_value() ||
        pendingDirtyCloseKey_.has_value() || !sceneDocumentActive()) {
        return Tina::Core::success();
    }
    const auto* activeTab = documentTabs_.activeTab();
    const u32 stableId = stableEntityIdForHierarchyItem(selectionKey_);
    const EditorHierarchyRow* target = hierarchyRow(stableId);
    if (activeTab == nullptr || stableId == 0U || target == nullptr) {
        return Tina::Core::success();
    }
    if (workspaceMode_ == WorkspaceMode::World3D) {
        const bool hasSiblingRoot = target->parentStableId != 0U ||
            std::any_of(
                hierarchyRows_.begin(), hierarchyRows_.end(),
                [stableId](const EditorHierarchyRow& row) noexcept {
                    return row.stableId != 0U && row.stableId != stableId &&
                           row.parentStableId == 0U;
                });
        if (document3D_.nodeCount() <= 1U || !hasSiblingRoot) {
            authoringFeedback_ =
                "Delete rejected: a World3D document must retain at least one node";
            return Tina::Core::success();
        }
    }

    pendingSceneDeleteConfirmation_ = SceneDeleteConfirmation{
        .documentKey = activeTab->key,
        .workspace = workspaceMode_,
        .stableId = stableId,
        .documentRevision = activeDocumentRevision(),
    };
    if (auto status = tree.setLayoutStyle(
            sceneDeleteDialog_.modal,
            editorDialogOverlayLayout(UI::UIVisibility::Visible));
        !status) {
        pendingSceneDeleteConfirmation_.reset();
        return status;
    }
    authoringFeedback_ =
        "Delete confirmation opened for the selected scene subtree";
    pendingSceneDeleteDialogFocus_ = true;
    return Tina::Core::success();
}

auto EditorWorkspaceState::hideSceneDeleteConfirmation(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!pendingSceneDeleteConfirmation_.has_value()) {
        return Tina::Core::success();
    }
    if (auto status = tree.setLayoutStyle(
            sceneDeleteDialog_.modal,
            editorDialogOverlayLayout(UI::UIVisibility::Collapsed));
        !status) {
        return status;
    }
    pendingSceneDeleteConfirmation_.reset();
    pendingSceneDeleteDialogFocus_ = false;
    pendingHierarchyFocusRestore_ = true;
    return Tina::Core::success();
}

auto EditorWorkspaceState::executeEditorCommand(Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    const EditorCommand command = *pendingEditorCommand_;
    pendingEditorCommand_.reset();

    Tina::Core::Status status = Tina::Core::success();
    bool requiresPreviewValidation = false;
    bool animationDocumentChanged = false;
    std::optional<u32> hierarchyRefreshStableId{};
    const bool playCommand =
        command >= EditorCommand::PlayStartOrResume &&
        command <= EditorCommand::PlayStop;
    const bool previewNavigationCommand =
        command == EditorCommand::SceneFocus ||
        command == EditorCommand::ViewportCyclePreset ||
        command == EditorCommand::ViewportPresetTop ||
        command == EditorCommand::ViewportPresetFront ||
        command == EditorCommand::ViewportPresetRight ||
        command == EditorCommand::ViewportResetView;
    const bool informationalCommand =
        command == EditorCommand::ShowAbout || command == EditorCommand::HideAbout;
    if (playSessionActive() && !playCommand && !previewNavigationCommand &&
        !informationalCommand) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Editor authoring commands are locked while the isolated play session is active");
    }
    if (command >= EditorCommand::AnimationTogglePlayback &&
        command <= EditorCommand::AnimationCookPreview &&
        workspaceMode_ != WorkspaceMode::World2D) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "SpriteAnimationClip authoring is available in the 2D workspace");
    }
    switch (command) {
    case EditorCommand::SwitchToWorld2D:
        status = activateDocumentTab(tree, 0U);
        break;
    case EditorCommand::SwitchToWorld3D:
        status = activateDocumentTab(tree, 1U);
        break;
    case EditorCommand::MoveSelectedPositiveX:
        status = moveSelectedPositiveX();
        if (status) {
            ++counters_.authoringEdits;
            requiresPreviewValidation = true;
            authoringFeedback_ = "Move X +1 applied as one document revision";
        }
        break;
    case EditorCommand::ApplyTransform: {
        InspectorTransformInput input{};
        const auto parseField = [&](UI::UINodeId field,
                                    std::string_view fieldName)
            -> Tina::Core::Result<std::optional<float>> {
            auto text = tree.text(field);
            if (!text) {
                return Tina::Core::failure(std::move(text.error()));
            }
            return parseInspectorTransformValue(*text, fieldName);
        };
        const auto parseInto = [&](UI::UINodeId field,
                                   std::string_view fieldName,
                                   std::optional<float>& output)
            -> Tina::Core::Status {
            auto parsed = parseField(field, fieldName);
            if (!parsed) {
                return Tina::Core::failure(std::move(parsed.error()));
            }
            output = *parsed;
            return Tina::Core::success();
        };
        const auto rejectInput = [&](const Tina::Core::Error& error)
            -> Tina::Core::Status {
            try {
                authoringFeedback_ = "Transform rejected: ";
                authoringFeedback_ += error.message;
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Inspector rejection feedback allocation failed");
            }
            ++counters_.inspectorRejectedTransactions;
            return refreshAuthoringUi(tree);
        };

        status = parseInto(inspectorPositionX_, "Position X",
                           input.positionX);
        if (status) {
            status = parseInto(inspectorPositionY_, "Position Y",
                               input.positionY);
        }
        if (status && workspaceMode_ == WorkspaceMode::World3D) {
            status = parseInto(inspectorPositionZ_, "Position Z",
                               input.positionZ);
        }
        if (status && workspaceMode_ == WorkspaceMode::World3D) {
            status = parseInto(inspectorRotationX_, "Rotation X",
                               input.rotationX);
        }
        if (status && workspaceMode_ == WorkspaceMode::World3D) {
            status = parseInto(inspectorRotationY_, "Rotation Y",
                               input.rotationY);
        }
        if (status) {
            status = parseInto(inspectorRotationZ_, "Rotation Z",
                               input.rotationZ);
        }
        if (status) {
            status = parseInto(inspectorScaleX_, "Scale X", input.scaleX);
        }
        if (status) {
            status = parseInto(inspectorScaleY_, "Scale Y", input.scaleY);
        }
        if (status && workspaceMode_ == WorkspaceMode::World3D) {
            status = parseInto(inspectorScaleZ_, "Scale Z", input.scaleZ);
        }
        if (!status) {
            if (status.error().code ==
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation) {
                return rejectInput(status.error());
            }
            return status;
        }

        const u64 revisionBefore = activeDocumentRevision();
        status = applySelectedTransform(input);
        if (!status) {
            if (status.error().code ==
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation ||
                status.error().code ==
                    Tina::Editor::EditorErrorCode::EntityNotFound) {
                return rejectInput(status.error());
            }
            break;
        }
        const u64 revisionAfter = activeDocumentRevision();
        if (revisionAfter == revisionBefore) {
            authoringFeedback_ =
                "Transform unchanged; no document revision published";
            break;
        }
        if (revisionAfter < revisionBefore ||
            revisionAfter - revisionBefore != 1U) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Inspector batch transform did not publish exactly one document revision");
        }
        ++counters_.authoringEdits;
        ++counters_.inspectorTransactions;
        requiresPreviewValidation = true;
        if (viewportSelectedEntityCount_ > 1U) {
            try {
                authoringFeedback_ = "Transform applied to ";
                authoringFeedback_ +=
                    std::to_string(viewportSelectedEntityCount_);
                authoringFeedback_ +=
                    " selected entities as one document revision";
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Inspector transaction feedback allocation failed");
            }
        } else {
            authoringFeedback_ =
                "Transform applied as one document revision";
        }
        break;
    }
    case EditorCommand::ComponentAddSprite:
    case EditorCommand::ComponentAddCamera:
    case EditorCommand::ComponentAddPointLight:
    case EditorCommand::ComponentAddShadowOccluder:
    case EditorCommand::ComponentAddSpriteAnimation:
    case EditorCommand::ComponentAddMeshRenderer:
    case EditorCommand::ComponentRemoveSprite:
    case EditorCommand::ComponentRemoveCamera:
    case EditorCommand::ComponentRemovePointLight:
    case EditorCommand::ComponentRemoveShadowOccluder:
    case EditorCommand::ComponentRemoveSpriteAnimation:
    case EditorCommand::ComponentRemoveMeshRenderer:
    case EditorCommand::ComponentApplySprite:
    case EditorCommand::ComponentApplyCamera:
    case EditorCommand::ComponentApplyPointLight:
    case EditorCommand::ComponentApplyShadowOccluder:
    case EditorCommand::ComponentApplySpriteAnimation:
    case EditorCommand::ComponentToggleSpriteVisible:
    case EditorCommand::ComponentToggleCameraActive:
    case EditorCommand::ComponentTogglePointLightActive:
    case EditorCommand::ComponentToggleShadowOccluderActive:
    case EditorCommand::ComponentToggleSpriteAnimationAutoPlay:
    case EditorCommand::ComponentToggleMeshVisible:
    case EditorCommand::ComponentAssignSprite: {
        bool published = false;
        status = runComponentCommand(tree, command, published);
        if (status && published) {
            requiresPreviewValidation = true;
            // Hierarchy labels encode component presence, so structural
            // component changes rebuild the tree at the current selection.
            hierarchyRefreshStableId =
                stableEntityIdForHierarchyItem(selectionKey_);
        }
        break;
    }
    case EditorCommand::Undo: {
        const auto* activeTab = documentTabs_.activeTab();
        if (activeTab == nullptr) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::DocumentTabNotFound,
                "Editor has no active document to undo");
        }
        const Tina::Editor::EditorDocumentKind activeKind = activeTab->key.kind;
        switch (activeKind) {
        case Tina::Editor::EditorDocumentKind::TileMap2D:
            status = tileMapDocument_.undo();
            break;
        case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
            status = spriteAnimationDocument_.undo();
            break;
        case Tina::Editor::EditorDocumentKind::World3D:
            status = document3D_.undo();
            break;
        case Tina::Editor::EditorDocumentKind::World2D:
        default:
            status = document_.undo();
            break;
        }
        if (status) {
            ++counters_.authoringUndos;
            if (activeKind == Tina::Editor::EditorDocumentKind::TileMap2D) {
                ++counters_.tileMapUndos;
            } else if (activeKind ==
                       Tina::Editor::EditorDocumentKind::SpriteAnimation2D) {
                animationPreview_.clampSelection(static_cast<u32>(spriteAnimationDocument_.frameCount()));
                ++counters_.animationUndos;
                animationDocumentChanged = true;
            }
            requiresPreviewValidation = true;
            if (activeKind == Tina::Editor::EditorDocumentKind::World2D ||
                activeKind == Tina::Editor::EditorDocumentKind::World3D) {
                hierarchyRefreshStableId =
                    stableEntityIdForHierarchyItem(selectionKey_);
            }
            authoringFeedback_ = "Undo restored the previous canonical snapshot";
        }
        break;
    }
    case EditorCommand::Redo: {
        const auto* activeTab = documentTabs_.activeTab();
        if (activeTab == nullptr) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::DocumentTabNotFound,
                "Editor has no active document to redo");
        }
        const Tina::Editor::EditorDocumentKind activeKind = activeTab->key.kind;
        switch (activeKind) {
        case Tina::Editor::EditorDocumentKind::TileMap2D:
            status = tileMapDocument_.redo();
            break;
        case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
            status = spriteAnimationDocument_.redo();
            break;
        case Tina::Editor::EditorDocumentKind::World3D:
            status = document3D_.redo();
            break;
        case Tina::Editor::EditorDocumentKind::World2D:
        default:
            status = document_.redo();
            break;
        }
        if (status) {
            ++counters_.authoringRedos;
            if (activeKind == Tina::Editor::EditorDocumentKind::TileMap2D) {
                ++counters_.tileMapRedos;
            } else if (activeKind ==
                       Tina::Editor::EditorDocumentKind::SpriteAnimation2D) {
                animationPreview_.clampSelection(static_cast<u32>(spriteAnimationDocument_.frameCount()));
                ++counters_.animationRedos;
                animationDocumentChanged = true;
            }
            requiresPreviewValidation = true;
            if (activeKind == Tina::Editor::EditorDocumentKind::World2D ||
                activeKind == Tina::Editor::EditorDocumentKind::World3D) {
                hierarchyRefreshStableId =
                    stableEntityIdForHierarchyItem(selectionKey_);
            }
            authoringFeedback_ = "Redo restored the next canonical snapshot";
        }
        break;
    }
    case EditorCommand::Save:
    case EditorCommand::SaveAs: {
        if (command == EditorCommand::SaveAs) {
            const WorkspaceSessionState* session = activeDocumentSession();
            const std::string_view currentPath = session != nullptr
                                                     ? session->documentPathUtf8
                                                     : std::string_view{};
            auto selectedPath = requestNativeSaveAsPath(currentPath);
            if (!selectedPath) {
                status = Tina::Core::failure(std::move(selectedPath.error()));
            } else if (!selectedPath->has_value()) {
                authoringFeedback_ = "Save As cancelled; document preserved";
                status = Tina::Core::success();
                break;
            } else {
                status = saveActiveDocument(**selectedPath);
            }
        } else {
            status = saveActiveDocument();
        }
        if (!status) {
            try {
                authoringFeedback_ = "Save failed: ";
                authoringFeedback_ += status.error().message;
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Save failure message allocation failed");
            }
            status = Tina::Core::success();
            break;
        }
        switch (documentTabs_.activeTab()->key.kind) {
        case Tina::Editor::EditorDocumentKind::World2D:
            authoringFeedback_ = "Canonical World2D document saved atomically";
            break;
        case Tina::Editor::EditorDocumentKind::World3D:
            authoringFeedback_ = "Canonical Prefab v2 document saved atomically";
            break;
        case Tina::Editor::EditorDocumentKind::TileMap2D:
            authoringFeedback_ = "Canonical TileMap root and chunks saved root-last";
            break;
        case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
            authoringFeedback_ = "Canonical SpriteAnimationClip saved atomically";
            break;
        case Tina::Editor::EditorDocumentKind::AssetInspector:
        default:
            break;
        }
        break;
    }
    case EditorCommand::SceneAdd: {
        if (!sceneDocumentActive()) {
            status = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Scene hierarchy commands require an active World2D or World3D document");
            break;
        }
        const u32 parentStableId =
            assetInspectorActive_ ? 0U : stableEntityIdForHierarchyItem(selectionKey_);
        auto added = workspaceMode_ == WorkspaceMode::World2D
                         ? Tina::Editor::addWorld2DEntity(document_, parentStableId)
                         : Tina::Editor::addWorld3DNode(document3D_, parentStableId);
        if (!added) {
            status = Tina::Core::failure(std::move(added.error()));
            break;
        }
        hierarchyRefreshStableId = added->primaryStableId;
        requiresPreviewValidation = true;
        ++counters_.authoringEdits;
        ++counters_.sceneAddCommands;
        if (options_.autoDemo) {
            counters_.automaticAddedStableId = added->primaryStableId;
        }
        authoringFeedback_ = workspaceMode_ == WorkspaceMode::World2D
                                 ? "Entity2D added as one scene revision"
                                 : "Node3D added as one scene revision";
        break;
    }
    case EditorCommand::SceneDuplicate: {
        if (!sceneDocumentActive()) {
            status = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Scene hierarchy commands require an active World2D or World3D document");
            break;
        }
        const u32 stableId = stableEntityIdForHierarchyItem(selectionKey_);
        if (stableId == 0U) {
            status = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Select a scene item before duplicating it");
            break;
        }
        auto duplicated = workspaceMode_ == WorkspaceMode::World2D
                              ? Tina::Editor::duplicateWorld2DEntitySubtree(
                                    document_, stableId)
                              : Tina::Editor::duplicateWorld3DNodeSubtree(
                                    document3D_, stableId);
        if (!duplicated) {
            status = Tina::Core::failure(std::move(duplicated.error()));
            break;
        }
        hierarchyRefreshStableId = duplicated->primaryStableId;
        requiresPreviewValidation = true;
        ++counters_.authoringEdits;
        ++counters_.sceneDuplicateCommands;
        if (options_.autoDemo) {
            counters_.automaticDuplicatedStableId =
                duplicated->primaryStableId;
        }
        authoringFeedback_ = "Scene subtree duplicated as one canonical revision";
        break;
    }
    case EditorCommand::SceneDelete: {
        status = showSceneDeleteConfirmation(tree);
        break;
    }
    case EditorCommand::SceneDeleteConfirm: {
        if (!pendingSceneDeleteConfirmation_.has_value() ||
            pendingSceneDeleteConfirmation_->confirming) {
            break;
        }
        pendingSceneDeleteConfirmation_->confirming = true;
        const SceneDeleteConfirmation confirmation =
            *pendingSceneDeleteConfirmation_;
        const auto* activeTab = documentTabs_.activeTab();
        const bool targetIsCurrent =
            activeTab != nullptr && activeTab->key == confirmation.documentKey &&
            workspaceMode_ == confirmation.workspace && sceneDocumentActive() &&
            activeDocumentRevision() == confirmation.documentRevision &&
            hierarchyRow(confirmation.stableId) != nullptr;
        if (!targetIsCurrent) {
            authoringFeedback_ =
                "Delete cancelled: the target or canonical document changed before confirmation";
            status = hideSceneDeleteConfirmation(tree);
            break;
        }
        auto deleted = confirmation.workspace == WorkspaceMode::World2D
                           ? Tina::Editor::deleteWorld2DEntitySubtree(
                                 document_, confirmation.stableId)
                           : Tina::Editor::deleteWorld3DNodeSubtree(
                                 document3D_, confirmation.stableId);
        if (!deleted) {
            Tina::Core::Error deleteError = std::move(deleted.error());
            status = reportAuthoringFailure("Delete failed; scene hierarchy preserved: ",
                                            deleteError);
            if (status) {
                status = hideSceneDeleteConfirmation(tree);
            }
            break;
        }
        hierarchyRefreshStableId = deleted->primaryStableId;
        requiresPreviewValidation = true;
        ++counters_.authoringEdits;
        ++counters_.sceneDeleteCommands;
        authoringFeedback_ = "Scene subtree deleted as one canonical revision";
        status = hideSceneDeleteConfirmation(tree);
        break;
    }
    case EditorCommand::SceneDeleteCancel:
        if (pendingSceneDeleteConfirmation_.has_value()) {
            authoringFeedback_ =
                "Delete cancelled; scene hierarchy and selection preserved";
            status = hideSceneDeleteConfirmation(tree);
        }
        break;
    case EditorCommand::SceneReparentRoot: {
        if (!sceneDocumentActive()) {
            status = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Scene hierarchy commands require an active World2D or World3D document");
            break;
        }
        const u32 stableId = stableEntityIdForHierarchyItem(selectionKey_);
        if (stableId == 0U) {
            status = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Select a scene item before reparenting it");
            break;
        }
        const EditorHierarchyRow* selectedRow = hierarchyRow(stableId);
        if (selectedRow != nullptr && selectedRow->parentStableId == 0U) {
            authoringFeedback_ = "Scene item is already at the document root";
            break;
        }
        status = workspaceMode_ == WorkspaceMode::World2D
                     ? Tina::Editor::reparentWorld2DEntity(
                           document_, stableId, 0U)
                     : Tina::Editor::reparentWorld3DNode(
                           document3D_, stableId, 0U);
        if (status) {
            hierarchyRefreshStableId = stableId;
            requiresPreviewValidation = true;
            ++counters_.authoringEdits;
            ++counters_.sceneReparentRootCommands;
            authoringFeedback_ = "Scene item reparented to the document root";
        }
        break;
    }
    case EditorCommand::SceneReparent: {
        if (!sceneDocumentActive()) {
            status = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Scene hierarchy commands require an active World2D or World3D document");
            break;
        }
        const u32 stableId = stableEntityIdForHierarchyItem(selectionKey_);
        if (stableId == 0U) {
            status = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Select a scene item before reparenting it");
            break;
        }
        auto parentText = tree.text(inspectorParentStableId_);
        if (!parentText) {
            return Tina::Core::failure(std::move(parentText.error()));
        }
        u32 parentStableId = 0U;
        if (!parseUnsigned(*parentText, parentStableId)) {
            ++counters_.inspectorRejectedTransactions;
            authoringFeedback_ = "Parent rejected: enter an unsigned stable ID (0 means scene root)";
            return refreshAuthoringUi(tree);
        }
        const EditorHierarchyRow* selectedRow = hierarchyRow(stableId);
        if (selectedRow != nullptr && selectedRow->parentStableId == parentStableId) {
            authoringFeedback_ = "Parent unchanged; no document revision was published";
            break;
        }
        status = workspaceMode_ == WorkspaceMode::World2D
                     ? Tina::Editor::reparentWorld2DEntity(
                           document_, stableId, parentStableId)
                     : Tina::Editor::reparentWorld3DNode(
                           document3D_, stableId, parentStableId);
        if (status) {
            hierarchyRefreshStableId = stableId;
            requiresPreviewValidation = true;
            ++counters_.authoringEdits;
            ++counters_.sceneReparentCommands;
            authoringFeedback_ = "Scene item reparented as one canonical revision";
        }
        break;
    }
    case EditorCommand::SceneFocus:
        status = focusViewportOnSelection();
        if (status) {
            authoringFeedback_ = "Viewport focused on the selected scene item";
        }
        break;
    case EditorCommand::PlayStartOrResume:
        if (!playSession_.has_value()) {
            status = Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Editor play session owner is unavailable");
            break;
        }
        if (playSession_->snapshot().state == Tina::Editor::EditorPlayState::Paused) {
            status = playSession_->resume();
            if (status) {
                ++counters_.playResumes;
                authoringFeedback_ = "Isolated scene simulation resumed";
            }
            break;
        }
        if (playSession_->snapshot().state == Tina::Editor::EditorPlayState::Playing) {
            authoringFeedback_ = "Isolated scene simulation is already running";
            break;
        }
        if (!sceneDocumentActive()) {
            status = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Play requires an active World2D or World3D scene document");
            break;
        }
        if (!playStartReady()) {
            authoringFeedback_ =
                "Play is waiting for project, import, Catalog, and preview updates to settle";
            break;
        }
        resetViewportInteractionState();
        viewportToolMode_ = ViewportToolMode::Select;
        animationPreview_.setPlaying(false);
        if (animationPreview_.hasAnimator()) {
            animationPreview_.animator().pause();
        }
        if (workspaceMode_ == WorkspaceMode::World2D) {
            status = playSession_->start(
                Tina::Editor::EditorPlayWorkspace::TwoD,
                document_.revision(), document_.snapshotBytes());
        } else {
            status = playSession_->start(
                Tina::Editor::EditorPlayWorkspace::ThreeD,
                document3D_.revision(), document3D_.payloadBytes());
        }
        if (status) {
            requiresPreviewValidation = true;
            ++counters_.playStarts;
            authoringFeedback_ = "Isolated scene simulation started from a canonical snapshot";
        }
        break;
    case EditorCommand::PlayPause:
        if (!playSession_.has_value()) {
            status = Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Editor play session owner is unavailable");
            break;
        }
        status = playSession_->pause();
        if (status) {
            ++counters_.playPauses;
            authoringFeedback_ = "Isolated scene simulation paused";
        }
        break;
    case EditorCommand::PlayStep: {
        if (!playSession_.has_value()) {
            status = Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Editor play session owner is unavailable");
            break;
        }
        const bool stepWasPending = playSession_->snapshot().stepPending;
        status = playSession_->requestStep();
        if (status) {
            if (!stepWasPending) {
                ++counters_.playStepRequests;
            }
            authoringFeedback_ = "One fixed simulation step queued";
        }
        break;
    }
    case EditorCommand::PlayStop: {
        if (!playSession_.has_value()) {
            status = Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Editor play session owner is unavailable");
            break;
        }
        const bool playWasActive = playSession_->active();
        counters_.playMaximumSimulationTick = (std::max)(
            counters_.playMaximumSimulationTick,
            playSession_->snapshot().simulationTickCount);
        status = playSession_->stop();
        if (status) {
            if (playWasActive) {
                ++counters_.playStops;
            }
            requiresPreviewValidation = true;
            authoringFeedback_ = "Play session stopped; canonical authoring scene restored";
        }
        break;
    }
    case EditorCommand::PaintTile:
        status = editTileMapBrushCell(false);
        if (status) {
            ++counters_.authoringEdits;
            ++counters_.tileMapEdits;
            requiresPreviewValidation = true;
            authoringFeedback_ = "Tile painted as one root/chunk revision";
        }
        break;
    case EditorCommand::EraseTile:
        status = editTileMapBrushCell(true);
        if (status) {
            ++counters_.authoringEdits;
            ++counters_.tileMapEdits;
            requiresPreviewValidation = true;
            authoringFeedback_ = "Tile erased as one root/chunk revision";
        }
        break;
    case EditorCommand::ToggleTileLayer:
        status = toggleActiveTileMapLayer();
        if (status) {
            ++counters_.authoringEdits;
            ++counters_.tileMapEdits;
            requiresPreviewValidation = true;
            authoringFeedback_ = "Tile layer visibility committed";
        }
        break;
    case EditorCommand::AddTileLayer:
        status = addTileMapLayer(Tina::AssetFormat::TileMapLayerKind::Tile);
        if (status) {
            ++counters_.authoringEdits;
            ++counters_.tileMapEdits;
            requiresPreviewValidation = true;
            authoringFeedback_ = "Tile layer added to the TileMap document";
        }
        break;
    case EditorCommand::AddObjectLayer:
        status = addTileMapLayer(Tina::AssetFormat::TileMapLayerKind::Object);
        if (status) {
            ++counters_.authoringEdits;
            ++counters_.tileMapEdits;
            requiresPreviewValidation = true;
            authoringFeedback_ = "Object layer added to the TileMap document";
        }
        break;
    case EditorCommand::CookTileMapPreview: {
        if (!tileMapEditingContext()) {
            status = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "TileMap cook preview requires the 2D TileMap selection");
            break;
        }
        auto preview = tileMapDocument_.cookPreview();
        if (!preview) {
            status = Tina::Core::failure(std::move(preview.error()));
            break;
        }
        counters_.tileMapCookArtifacts = preview->artifacts.size();
        counters_.tileMapCookPreviewBytes = 0;
        for (const auto& artifact : preview->artifacts) {
            counters_.tileMapCookPreviewBytes += artifact.cookedBytes.size();
        }
        authoringFeedback_ = "TileMap root and chunk cook preview rebuilt";
        break;
    }
    case EditorCommand::BakeNavigation2D:
        status = bakeAndPublishNavigation2D();
        break;
    case EditorCommand::GenerateTileMapGameplay: {
        if (!tileMapEditingContext()) {
            status = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Gameplay generation requires the 2D TileMap selection");
            break;
        }
        constexpr std::array Archetypes{
            Tina::Editor::TileMapGameplayArchetypeBinding{
                .archetype = "player", .gameArchetypeId = EditorPlayerArchetypeId},
            Tina::Editor::TileMapGameplayArchetypeBinding{
                .archetype = "crate", .gameArchetypeId = EditorCrateArchetypeId},
        };
        auto generated = Tina::Editor::generateTileMapGameplay(
            tileMapDocument_, document_, Archetypes,
            {
                .objectLayerId = InitialGameplayObjectLayerId,
                .archetypePropertyKey = "archetype",
                .recordCapacity = tileMapDocument_.config().objectCapacity,
            },
            {
                .gameplaySchema = EditorGameplaySpawnSchema,
                .gameplayVersion = EditorGameplaySpawnVersion,
            },
            encodeEditorGameplaySpawns);
        if (!generated) {
            status = Tina::Core::failure(std::move(generated.error()));
            break;
        }
        ++counters_.tileMapGameplayGenerations;
        counters_.tileMapGameplaySpawnRecords = generated->records().size();
        counters_.tileMapGameplayBytes = document_.gameplayByteCount();
        counters_.tileMapGameplaySourceRevision = generated->sourceDocumentRevision();
        ++counters_.authoringEdits;
        requiresPreviewValidation = true;
        authoringFeedback_ = "Gameplay spawn plan generated as one World2D revision";
        break;
    }
    case EditorCommand::AnimationTogglePlayback:
        if (!animationPreview_.hasAnimator() || !animationPreview_.previewAvailable()) {
            status = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Animation preview requires resolved Sprite frames");
            break;
        }
        if (animationPreview_.playing()) {
            animationPreview_.animator().pause();
            animationPreview_.setPlaying(false);
            authoringFeedback_ = "Animation preview paused";
        } else {
            animationPreview_.animator().play();
            animationPreview_.setPlaying(true);
            status = applyAnimationPreviewFrame(
                static_cast<u32>(animationPreview_.animator().frameIndex()));
            authoringFeedback_ = "Animation preview playing";
        }
        ++counters_.animationPlaybackTransitions;
        break;
    case EditorCommand::AnimationPreviousFrame:
        animationPreview_.setPlaying(false);
        if (animationPreview_.hasAnimator()) {
            animationPreview_.animator().pause();
        }
        if (animationPreview_.selectedFrameIndex() > 0U) {
            status = applyAnimationPreviewFrame(animationPreview_.selectedFrameIndex() - 1U);
        }
        authoringFeedback_ = "Animation playhead stepped backward";
        break;
    case EditorCommand::AnimationNextFrame:
        animationPreview_.setPlaying(false);
        if (animationPreview_.hasAnimator()) {
            animationPreview_.animator().pause();
        }
        if (animationPreview_.selectedFrameIndex() + 1U < spriteAnimationDocument_.frameCount()) {
            status = applyAnimationPreviewFrame(animationPreview_.selectedFrameIndex() + 1U);
        }
        authoringFeedback_ = "Animation playhead stepped forward";
        break;
    case EditorCommand::AnimationAddFrame: {
        const auto selected = spriteAnimationDocument_.frameAt(animationPreview_.selectedFrameIndex());
        if (!selected) {
            status = Tina::Core::failure(Tina::Editor::EditorErrorCode::FrameNotFound,
                                         "Animation selected frame does not exist");
            break;
        }
        status = spriteAnimationDocument_.appendFrame(*selected);
        if (status) {
            animationPreview_.setSelectedFrameIndex(static_cast<u32>(spriteAnimationDocument_.frameCount() - 1U));
            animationDocumentChanged = true;
            ++counters_.animationEdits;
            ++counters_.authoringEdits;
            authoringFeedback_ = "Animation frame appended as one clip revision";
        }
        break;
    }
    case EditorCommand::AnimationDuplicateFrame:
        status = spriteAnimationDocument_.duplicateFrame(animationPreview_.selectedFrameIndex());
        if (status) {
            animationPreview_.setSelectedFrameIndex(animationPreview_.selectedFrameIndex() + 1U);
            animationDocumentChanged = true;
            ++counters_.animationEdits;
            ++counters_.authoringEdits;
            authoringFeedback_ = "Animation frame duplicated as one clip revision";
        }
        break;
    case EditorCommand::AnimationDeleteFrame:
        status = spriteAnimationDocument_.eraseFrame(animationPreview_.selectedFrameIndex());
        if (status) {
            animationPreview_.clampSelection(static_cast<u32>(spriteAnimationDocument_.frameCount()));
            animationDocumentChanged = true;
            ++counters_.animationEdits;
            ++counters_.authoringEdits;
            authoringFeedback_ = "Animation frame deleted as one clip revision";
        }
        break;
    case EditorCommand::AnimationMoveFrameLeft:
        if (animationPreview_.selectedFrameIndex() > 0U) {
            status = spriteAnimationDocument_.moveFrame(
                animationPreview_.selectedFrameIndex(), animationPreview_.selectedFrameIndex() - 1U);
            if (status) {
                animationPreview_.setSelectedFrameIndex(animationPreview_.selectedFrameIndex() - 1U);
                animationDocumentChanged = true;
                ++counters_.animationEdits;
                ++counters_.authoringEdits;
                authoringFeedback_ = "Animation frame moved left";
            }
        }
        break;
    case EditorCommand::AnimationMoveFrameRight:
        if (animationPreview_.selectedFrameIndex() + 1U < spriteAnimationDocument_.frameCount()) {
            status = spriteAnimationDocument_.moveFrame(
                animationPreview_.selectedFrameIndex(), animationPreview_.selectedFrameIndex() + 1U);
            if (status) {
                animationPreview_.setSelectedFrameIndex(animationPreview_.selectedFrameIndex() + 1U);
                animationDocumentChanged = true;
                ++counters_.animationEdits;
                ++counters_.authoringEdits;
                authoringFeedback_ = "Animation frame moved right";
            }
        }
        break;
    case EditorCommand::AnimationCycleSprite: {
        const auto selected = spriteAnimationDocument_.frameAt(animationPreview_.selectedFrameIndex());
        if (!selected) {
            status = Tina::Core::failure(Tina::Editor::EditorErrorCode::FrameNotFound,
                                         "Animation selected frame does not exist");
            break;
        }
        Tina::Core::AssetId nextSprite = selected->spriteId;
        for (u32 offset = 1; offset < spriteAnimationDocument_.frameCount(); ++offset) {
            const u32 candidateIndex = static_cast<u32>(
                (animationPreview_.selectedFrameIndex() + offset) %
                spriteAnimationDocument_.frameCount());
            const auto candidate = spriteAnimationDocument_.frameAt(candidateIndex);
            if (candidate && candidate->spriteId != selected->spriteId) {
                nextSprite = candidate->spriteId;
                break;
            }
        }
        if (nextSprite != selected->spriteId) {
            auto updated = *selected;
            updated.spriteId = nextSprite;
            status = spriteAnimationDocument_.setFrame(animationPreview_.selectedFrameIndex(), updated);
            if (status) {
                animationDocumentChanged = true;
                ++counters_.animationEdits;
                ++counters_.authoringEdits;
                authoringFeedback_ = "Animation frame Sprite binding changed";
            }
        }
        break;
    }
    case EditorCommand::AnimationDecreaseDuration:
    case EditorCommand::AnimationIncreaseDuration: {
        const auto selected = spriteAnimationDocument_.frameAt(animationPreview_.selectedFrameIndex());
        if (!selected) {
            status = Tina::Core::failure(Tina::Editor::EditorErrorCode::FrameNotFound,
                                         "Animation selected frame does not exist");
            break;
        }
        const float delta = command == EditorCommand::AnimationIncreaseDuration
                                ? 0.05F
                                : -0.05F;
        const float duration = (std::max)(0.01F, selected->durationSeconds + delta);
        status = spriteAnimationDocument_.setFrameDuration(
            animationPreview_.selectedFrameIndex(), duration);
        if (status && duration != selected->durationSeconds) {
            animationDocumentChanged = true;
            ++counters_.animationEdits;
            ++counters_.authoringEdits;
            authoringFeedback_ = "Animation frame duration changed";
        }
        break;
    }
    case EditorCommand::AnimationPreviousEvent:
        if (animationSelectedEventIndex_ > 0U) {
            --animationSelectedEventIndex_;
            authoringFeedback_ = "Animation notify selection moved backward";
        }
        break;
    case EditorCommand::AnimationNextEvent: {
        const auto selected = spriteAnimationDocument_.frameAt(
            animationPreview_.selectedFrameIndex());
        if (!selected) {
            status = Tina::Core::failure(Tina::Editor::EditorErrorCode::FrameNotFound,
                                         "Animation selected frame does not exist");
            break;
        }
        if (animationSelectedEventIndex_ + 1U < selected->events.size()) {
            ++animationSelectedEventIndex_;
            authoringFeedback_ = "Animation notify selection moved forward";
        }
        break;
    }
    case EditorCommand::AnimationAddEvent:
    case EditorCommand::AnimationApplyEvent: {
        const auto selected = spriteAnimationDocument_.frameAt(
            animationPreview_.selectedFrameIndex());
        if (!selected) {
            status = Tina::Core::failure(Tina::Editor::EditorErrorCode::FrameNotFound,
                                         "Animation selected frame does not exist");
            break;
        }
        auto input = readAnimationEventInput(tree);
        if (!input) {
            ++counters_.animationEventRejectedEdits;
            return reportAuthoringFailure("Animation notify rejected: ", input.error());
        }
        if (command == EditorCommand::AnimationAddEvent &&
            selected->events.size() >=
                Tina::AssetFormat::SpriteAnimationClipWire::MaxEventsPerFrame) {
            ++counters_.animationEventRejectedEdits;
            return reportAuthoringFailure(
                "Animation notify rejected: ",
                Tina::Core::Error{
                    Tina::Editor::EditorErrorCode::DocumentCapacityExceeded,
                    "selected frame already contains 64 notify events"});
        }
        if (command == EditorCommand::AnimationApplyEvent &&
            animationSelectedEventIndex_ >= selected->events.size()) {
            ++counters_.animationEventRejectedEdits;
            return reportAuthoringFailure(
                "Animation notify rejected: ",
                Tina::Core::Error{
                    Tina::Editor::EditorErrorCode::FrameNotFound,
                    "selected notify event does not exist"});
        }
        std::vector<Tina::AssetFormat::SpriteAnimationEventDesc> events{
            selected->events.begin(), selected->events.end()};
        if (command == EditorCommand::AnimationAddEvent) {
            events.push_back(*input);
        } else {
            events[animationSelectedEventIndex_] = *input;
        }
        std::stable_sort(
            events.begin(), events.end(),
            [](const auto& left, const auto& right) noexcept {
                return left.normalizedOffset < right.normalizedOffset;
            });
        const auto selectedEvent = std::find_if(
            events.begin(), events.end(), [&input](const auto& event) noexcept {
                return event.eventTag == input->eventTag &&
                       event.normalizedOffset == input->normalizedOffset;
            });
        status = spriteAnimationDocument_.setFrameEvents(
            animationPreview_.selectedFrameIndex(), events);
        if (status) {
            animationSelectedEventIndex_ = static_cast<u32>(
                std::distance(events.begin(), selectedEvent));
            animationDocumentChanged = true;
            ++counters_.animationEdits;
            ++counters_.animationEventEdits;
            ++counters_.authoringEdits;
            authoringFeedback_ = command == EditorCommand::AnimationAddEvent
                                     ? "Animation notify added as one clip revision"
                                     : "Animation notify edited as one clip revision";
        }
        break;
    }
    case EditorCommand::AnimationRemoveEvent: {
        const auto selected = spriteAnimationDocument_.frameAt(
            animationPreview_.selectedFrameIndex());
        if (!selected || animationSelectedEventIndex_ >= selected->events.size()) {
            status = Tina::Core::failure(Tina::Editor::EditorErrorCode::FrameNotFound,
                                         "Animation selected notify event does not exist");
            break;
        }
        std::vector<Tina::AssetFormat::SpriteAnimationEventDesc> events{
            selected->events.begin(), selected->events.end()};
        events.erase(events.begin() +
                     static_cast<std::ptrdiff_t>(animationSelectedEventIndex_));
        status = spriteAnimationDocument_.setFrameEvents(
            animationPreview_.selectedFrameIndex(), events);
        if (status) {
            animationSelectedEventIndex_ = events.empty()
                                               ? 0U
                                               : (std::min)(animationSelectedEventIndex_,
                                                            static_cast<u32>(events.size() - 1U));
            animationDocumentChanged = true;
            ++counters_.animationEdits;
            ++counters_.animationEventEdits;
            ++counters_.authoringEdits;
            authoringFeedback_ = "Animation notify removed as one clip revision";
        }
        break;
    }
    case EditorCommand::AnimationCycleMode: {
        Tina::AssetFormat::SpriteAnimationPlaybackMode nextMode =
            Tina::AssetFormat::SpriteAnimationPlaybackMode::Loop;
        switch (spriteAnimationDocument_.playbackMode()) {
        case Tina::AssetFormat::SpriteAnimationPlaybackMode::Once:
            nextMode = Tina::AssetFormat::SpriteAnimationPlaybackMode::Loop;
            break;
        case Tina::AssetFormat::SpriteAnimationPlaybackMode::Loop:
            nextMode = Tina::AssetFormat::SpriteAnimationPlaybackMode::PingPong;
            break;
        case Tina::AssetFormat::SpriteAnimationPlaybackMode::PingPong:
            nextMode = Tina::AssetFormat::SpriteAnimationPlaybackMode::Once;
            break;
        }
        status = spriteAnimationDocument_.setPlaybackMode(nextMode);
        if (status) {
            animationDocumentChanged = true;
            ++counters_.animationEdits;
            ++counters_.authoringEdits;
            authoringFeedback_ = "Animation playback mode changed";
        }
        break;
    }
    case EditorCommand::AnimationCookPreview: {
        auto preview = spriteAnimationDocument_.cookPreview();
        if (!preview) {
            status = Tina::Core::failure(std::move(preview.error()));
            break;
        }
        counters_.animationCookPreviewBytes = preview->cookedBytes.size();
        authoringFeedback_ = "SpriteAnimationClip Cook preview rebuilt";
        break;
    }
    case EditorCommand::ViewportCyclePreset:
    case EditorCommand::ViewportPresetTop:
    case EditorCommand::ViewportPresetFront:
    case EditorCommand::ViewportPresetRight: {
        if (workspaceMode_ != WorkspaceMode::World3D) {
            authoringFeedback_ = "2D viewport uses a fixed Orthographic view";
            break;
        }
        if (auto navigationStatus = ensureViewportNavigation(); !navigationStatus) {
            return navigationStatus;
        }
        using Preset = Tina::Editor::EditorViewport3DViewPreset;
        const Preset preset =
            command == EditorCommand::ViewportPresetTop
                ? Preset::Top
                : command == EditorCommand::ViewportPresetFront
                      ? Preset::Front
                      : command == EditorCommand::ViewportPresetRight
                            ? Preset::Right
                            : nextViewportViewPreset(viewport3DViewPreset_);
        status = viewportNavigation_->set3DViewPreset(preset);
        if (status) {
            viewport3DViewPreset_ = preset;
            viewportViewModeRefreshPending_ = true;
            status = applyViewportNavigationToPreview();
            authoringFeedback_ = "3D view: ";
            authoringFeedback_ += viewportViewPresetName(preset);
        }
        break;
    }
    case EditorCommand::ViewportResetView:
        status = frameViewportContents();
        viewport3DViewPreset_.reset();
        if (status) {
            viewportViewModeRefreshPending_ = true;
            authoringFeedback_ = "Viewport framed to the authored content";
        }
        break;
    case EditorCommand::ProjectFilterAll:
        status = applyProjectAssetFilter(tree, Tina::Editor::ProjectAssetFilter::All);
        break;
    case EditorCommand::ProjectFilter2D:
        status = applyProjectAssetFilter(tree, Tina::Editor::ProjectAssetFilter::TwoD);
        break;
    case EditorCommand::ProjectFilter3D:
        status = applyProjectAssetFilter(tree, Tina::Editor::ProjectAssetFilter::ThreeD);
        break;
    case EditorCommand::ProjectFilterMedia:
        status = applyProjectAssetFilter(tree, Tina::Editor::ProjectAssetFilter::Media);
        break;
    case EditorCommand::RefreshProjectCatalog:
        catalogRefreshPending_ = true;
        authoringFeedback_ = "Catalog refresh scheduled before the next render packet";
        break;
    case EditorCommand::CreateProject:
        status = createNewProjectFromDialog();
        break;
    case EditorCommand::OpenProject:
        status = openProjectFromDialog();
        break;
    case EditorCommand::ImportSource:
        status = importSourceFromDialog();
        break;
    case EditorCommand::RemoveSelectedSourceImport:
        status = removeSelectedSourceImport();
        break;
    case EditorCommand::OpenSelectedProjectAsset:
        status = openSelectedProjectAsset(tree);
        break;
    case EditorCommand::CloseActiveDocument:
        status = closeActiveDocument(tree);
        break;
    case EditorCommand::DirtyCloseSave:
        status = confirmDirtyCloseSave(tree);
        break;
    case EditorCommand::DirtyCloseDiscard:
        status = confirmDirtyCloseDiscard(tree);
        break;
    case EditorCommand::DirtyCloseCancel:
        status = cancelDirtyClose(tree);
        break;
    case EditorCommand::ShowAbout:
        if (!aboutDialogVisible_) {
            status = tree.setLayoutStyle(
                aboutDialog_.modal,
                editorDialogOverlayLayout(UI::UIVisibility::Visible));
            if (status) {
                aboutDialogVisible_ = true;
                pendingAboutDialogFocus_ = true;
                pendingAboutDialogFocusRestore_ = false;
            }
        }
        break;
    case EditorCommand::HideAbout:
        if (aboutDialogVisible_) {
            status = tree.setLayoutStyle(
                aboutDialog_.modal,
                editorDialogOverlayLayout(UI::UIVisibility::Collapsed));
            if (status) {
                aboutDialogVisible_ = false;
                pendingAboutDialogFocus_ = false;
                pendingAboutDialogFocusRestore_ = true;
            }
        }
        break;
    }
    if (!status) {
        return status;
    }
    if (animationDocumentChanged) {
        if (auto animationStatus = rebuildAnimationAnimator(); !animationStatus) {
            animationPreview_.resetAnimator();
            animationPreview_.setPlaying(false);
            animationPreview_.setPreviewAvailable(false);
            authoringFeedback_ += " | Runtime preview unavailable: ";
            authoringFeedback_ += animationStatus.error().message;
        }
        auto preview = spriteAnimationDocument_.cookPreview();
        counters_.animationDocumentRevision = spriteAnimationDocument_.revision();
        counters_.animationFrameCount = spriteAnimationDocument_.frameCount();
        if (preview) {
            counters_.animationCookPreviewBytes = preview->cookedBytes.size();
        } else {
            counters_.animationCookPreviewBytes = 0;
            authoringFeedback_ += " | Cook preview unavailable: ";
            authoringFeedback_ += preview.error().message;
        }
    }
    if (requiresPreviewValidation) {
        if (auto previewStatus = validateRuntimePreview(); !previewStatus) {
            return previewStatus;
        }
    }
    if (hierarchyRefreshStableId.has_value()) {
        if (auto hierarchyStatus = refreshHierarchyTree(
                tree, *hierarchyRefreshStableId);
            !hierarchyStatus) {
            return hierarchyStatus;
        }
    }
    return refreshAuthoringUi(tree);
}

auto EditorWorkspaceState::moveSelectedPositiveX() -> Tina::Core::Status{
    const u32 stableEntityId = stableEntityIdForHierarchyItem(selectionKey_);
    if (stableEntityId == 0U) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::NotFound,
                                   "editor selection has no authoring entity");
    }

    if (workspaceMode_ == WorkspaceMode::World3D) {
        std::vector<Tina::AssetFormat::PrefabNodeView> views;
        auto prefab = document3D_.parseCurrentPrefab(views);
        if (!prefab) {
            return Tina::Core::failure(std::move(prefab.error()));
        }
        const auto node = std::find_if(views.begin(), views.end(), [stableEntityId](const auto& candidate) {
            return candidate.stableNodeId == stableEntityId;
        });
        if (node == views.end()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::NotFound,
                                       "editor selection is absent from the World3D document");
        }
        Tina::AssetFormat::PrefabNodeDesc edited{
            .stableNodeId = node->stableNodeId,
            .parentIndex = node->parentIndex,
            .positionX = node->positionX + 1.0F,
            .positionY = node->positionY,
            .positionZ = node->positionZ,
            .rotationX = node->rotationX,
            .rotationY = node->rotationY,
            .rotationZ = node->rotationZ,
            .rotationW = node->rotationW,
            .scaleX = node->scaleX,
            .scaleY = node->scaleY,
            .scaleZ = node->scaleZ,
            .meshId = node->meshId,
            .materialId = node->materialId,
            .visible = node->visible,
        };
        return document3D_.upsertNode(edited);
    }

    std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
    auto snapshot = document_.parseCurrentSnapshot(storage);
    if (!snapshot) {
        return Tina::Core::failure(std::move(snapshot.error()));
    }
    const auto entity = std::find_if(storage.begin(), storage.end(), [stableEntityId](const auto& candidate) {
        return candidate.stableEntityId == stableEntityId;
    });
    if (entity == storage.end()) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::NotFound,
                                   "editor selection is absent from the authoring document");
    }
    auto edited = *entity;
    edited.positionX += 1.0F;
    return document_.upsertEntity(edited);
}

} // namespace Tina::EditorApp::WorkspaceInternal
