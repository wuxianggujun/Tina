#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {

auto EditorWorkspaceState::reportAuthoringFailure(
    std::string_view prefix, const Tina::Core::Error& error) -> Tina::Core::Status{
    try {
        authoringFeedback_.assign(prefix);
        authoringFeedback_ += error.message;
        return Tina::Core::success();
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Editor authoring failure feedback allocation failed");
    }
}

auto EditorWorkspaceState::refreshWorkspaceChrome(Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    const bool world2D = workspaceMode_ == WorkspaceMode::World2D;
    if (auto status = tree.setText(toolbarDocument_, world2D ? "World2D Scene" : "World3D Scene");
        !status) {
        return status;
    }
    std::string breadcrumb = world2D ? "Scene / World2D" : "Scene / World3D";
    if (stableEntityIdForHierarchyItem(selectionKey_) != 0U) {
        breadcrumb += " / ";
        breadcrumb += hierarchyDisplayLabel(selectionKey_);
    }
    if (auto status = tree.setText(breadcrumb_, breadcrumb);
        !status) {
        return status;
    }
    if (auto status = tree.setText(viewportTitle_, world2D ? "World2D Viewport"
                                                           : "World3D Viewport");
        !status) {
        return status;
    }
    if (auto status = refreshViewportViewModeUi(tree); !status) {
        return status;
    }
    if (auto status = tree.setText(gridStatus_, world2D ? "Tile Grid 1 m" : "Grid 1 m"); !status) {
        return status;
    }
    std::string assetStatus = assetResources_.projectCatalogConfigured
                                  ? "Project Catalog | "
                                  : "Built-in Catalog | ";
    assetStatus += world2D ? "2D " : "3D ";
    assetStatus += std::to_string(world2D ? previewResolvedSpriteCount_
                                          : previewResolvedMeshCount_);
    assetStatus += " resolved";
    if (auto status = tree.setText(previewAssetStatus_, assetStatus); !status) {
        return status;
    }
    if (auto status = tree.setText(
            cameraStatus_,
            world2D ? "Camera2D" : "Camera3D");
        !status) {
        return status;
    }
    if (auto status = tree.setText(documentFormat_,
                                   world2D ? "World2D v2 + TileMap v3/v1 | canonical"
                                           : "Prefab schema v2 | canonical");
        !status) {
        return status;
    }
    if (auto status = tree.setRadioButtonSelected(mode2DButton_, world2D); !status) {
        return status;
    }
    if (auto status = tree.setRadioButtonSelected(mode3DButton_, !world2D); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(mode2DButton_, true); !status) {
        return status;
    }
    return tree.setEnabled(mode3DButton_, true);
}

auto EditorWorkspaceState::refreshAuthoringUi(Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    publishWorkspaceSessionCounters();
    const bool dirty = counters_.documentDirty;
    const bool pathConfigured = counters_.documentPathConfigured;
    const bool selectionEditable = authoringEnabled() && !assetInspectorActive_ &&
                                   stableEntityIdForHierarchyItem(selectionKey_) != 0U &&
                                   !tileMapEditingContext();
    const bool selectionEditable3D = selectionEditable && workspaceMode_ == WorkspaceMode::World3D;

    if (auto status = refreshWorkspaceChrome(tree); !status) {
        return status;
    }
    if (auto status = refreshProjectAssetUi(tree); !status) {
        return status;
    }
    if (auto status = refreshDocumentTabsUi(tree); !status) {
        return status;
    }
    if (auto status = refreshViewportToolUi(tree); !status) {
        return status;
    }
    if (auto status = refreshAnimationTimelineUi(tree); !status) {
        return status;
    }
    if (auto status = publishInspector(tree, selectionKey_); !status) {
        return status;
    }
    if (auto status = refreshComponentSectionsUi(tree); !status) {
        return status;
    }
    std::string documentStatus;
    if (assetInspectorActive_) {
        const auto* asset = inspectedProjectAsset();
        if (asset != nullptr) {
            documentStatus = "Catalog asset | Type v";
            documentStatus += std::to_string(asset->assetTypeVersion);
            documentStatus += " | Dependencies ";
            documentStatus += std::to_string(asset->dependencyCount);
        } else {
            documentStatus = "Catalog asset unavailable in active project";
        }
    } else {
        documentStatus = "Revision ";
        documentStatus += std::to_string(activeDocumentRevision());
        documentStatus += " | Undo ";
        documentStatus += std::to_string(activeUndoDepth());
        documentStatus += " | Redo ";
        documentStatus += std::to_string(activeRedoDepth());
        documentStatus += pathConfigured ? (dirty ? " | Modified" : " | Saved")
                                         : " | Unsaved";
    }
    if (auto status = tree.setText(inspectorDocument_, documentStatus); !status) {
        return status;
    }
    std::string_view documentKindLabel = "No document";
    std::string_view documentItemLabel = "items";
    if (const auto* activeTab = documentTabs_.activeTab(); activeTab != nullptr) {
        switch (activeTab->key.kind) {
        case Tina::Editor::EditorDocumentKind::World2D:
            documentKindLabel = "World2D v1";
            documentItemLabel = "entities";
            break;
        case Tina::Editor::EditorDocumentKind::World3D:
            documentKindLabel = "Prefab v2";
            documentItemLabel = "nodes";
            break;
        case Tina::Editor::EditorDocumentKind::TileMap2D:
            documentKindLabel = "TileMap v3/v1";
            documentItemLabel = "layers";
            break;
        case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
            documentKindLabel = "SpriteAnimationClip v1";
            documentItemLabel = "frames";
            break;
        case Tina::Editor::EditorDocumentKind::AssetInspector:
            documentKindLabel = "Asset Inspector";
            documentItemLabel = "items";
            break;
        }
    }
    std::string statusDocument{documentKindLabel};
    statusDocument += "  |  ";
    statusDocument += std::to_string(activeDocumentItemCount());
    statusDocument += ' ';
    statusDocument += documentItemLabel;
    statusDocument += "  |  Revision ";
    statusDocument += std::to_string(activeDocumentRevision());
    statusDocument += pathConfigured ? (dirty ? "  |  Modified" : "  |  Saved") : "  |  Unsaved";
    if (auto status = tree.setText(statusDocument_, statusDocument); !status) {
        return status;
    }
    if (auto status = publishRuntimePreviewStatus(tree); !status) {
        return status;
    }
    std::string selectionSummary = "Selected: ";
    selectionSummary += hierarchyDisplayLabel(selectionKey_);
    const u32 selectedEntityId = stableEntityIdForHierarchyItem(selectionKey_);
    if (selectedEntityId != 0U) {
        selectionSummary += "  |  ID ";
        selectionSummary += std::to_string(selectedEntityId);
    }
    if (!assetInspectorActive_ && viewportSelectedEntityCount_ > 1U) {
        selectionSummary += "  |  ";
        selectionSummary += std::to_string(viewportSelectedEntityCount_);
        selectionSummary += " selected  |  Group pivot";
    }
    if (auto status = tree.setText(hierarchySelectionSummary_, selectionSummary); !status) {
        return status;
    }
    std::string statusSelection = "Selected: ";
    if (assetInspectorActive_) {
        const auto* asset = inspectedProjectAsset();
        statusSelection += asset != nullptr ? asset->displayName : "Unavailable Catalog asset";
    } else {
        statusSelection += hierarchyDisplayLabel(selectionKey_);
        if (viewportSelectedEntityCount_ > 1U) {
            statusSelection += "  |  ";
            statusSelection += std::to_string(viewportSelectedEntityCount_);
            statusSelection += " selected  |  Group pivot";
        }
    }
    if (auto status = tree.setText(statusSelection_, statusSelection); !status) {
        return status;
    }
    if (auto status = tree.setText(authoringHint_, authoringFeedback_); !status) {
        return status;
    }
    auto tileMapRoot = Tina::AssetFormat::parseTileMapPayload(
        tileMapDocument_.rootPayloadBytes());
    if (!tileMapRoot) {
        return Tina::Core::failure(std::move(tileMapRoot.error()));
    }
    std::string tileMapStatus = std::to_string(tileMapRoot->widthCells);
    tileMapStatus += " x ";
    tileMapStatus += std::to_string(tileMapRoot->heightCells);
    tileMapStatus += " | Layers ";
    tileMapStatus += std::to_string(tileMapDocument_.layerCount());
    tileMapStatus += " | Chunks ";
    tileMapStatus += std::to_string(tileMapDocument_.chunkCount());
    tileMapStatus += " | Cells ";
    tileMapStatus += std::to_string(tileMapDocument_.nonEmptyCellCount());
    tileMapStatus += " | Rev ";
    tileMapStatus += std::to_string(tileMapDocument_.revision());
    if (auto status = tree.setText(tileMapStatus_, tileMapStatus); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(inspectorPositionX_, selectionEditable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(inspectorPositionY_, selectionEditable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(inspectorPositionZ_, selectionEditable3D); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(inspectorRotationX_, selectionEditable3D); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(inspectorRotationY_, selectionEditable3D); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(inspectorRotationZ_, selectionEditable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(inspectorScaleX_, selectionEditable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(inspectorScaleY_, selectionEditable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(inspectorScaleZ_, selectionEditable3D); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(inspectorParentStableId_, selectionEditable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(applyTransformButton_, selectionEditable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(moveButton_, selectionEditable); !status) {
        return status;
    }
    const u32 selectedStableId = stableEntityIdForHierarchyItem(selectionKey_);
    const EditorHierarchyRow* selectedRow = hierarchyRow(selectedStableId);
    const bool sceneEditable = authoringEnabled() && sceneDocumentActive() &&
                               !assetInspectorActive_;
    const bool sceneItemSelected = selectedStableId != 0U && selectedRow != nullptr;
    if (auto status = tree.setEnabled(addEntityButton_, sceneEditable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            duplicateEntityButton_, sceneEditable && sceneItemSelected);
        !status) {
        return status;
    }
    const bool canDeleteSelection = sceneEditable && sceneItemSelected &&
        (workspaceMode_ == WorkspaceMode::World2D || document3D_.nodeCount() > 1U);
    if (auto status = tree.setEnabled(deleteEntityButton_, canDeleteSelection); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            reparentEntityRootButton_,
            sceneEditable && sceneItemSelected && selectedRow->parentStableId != 0U);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            reparentEntityButton_, sceneEditable && sceneItemSelected);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            focusEntityButton_, sceneDocumentActive() && sceneItemSelected &&
                                    counters_.runtimePreviewValid);
        !status) {
        return status;
    }
    const bool tileMapControlsEnabled = authoringEnabled() && tileMapEditingContext();
    if (auto status = tree.setEnabled(paintTileButton_, tileMapControlsEnabled); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(eraseTileButton_, tileMapControlsEnabled); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(toggleTileLayerButton_, tileMapControlsEnabled); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(addTileLayerButton_, tileMapControlsEnabled); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(addObjectLayerButton_, tileMapControlsEnabled); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(cookTileMapButton_, tileMapControlsEnabled); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(generateTileMapGameplayButton_, tileMapControlsEnabled);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(undoButton_, activeCanUndo()); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(redoButton_, activeCanRedo()); !status) {
        return status;
    }
    const bool documentCanSave = activeDocumentSession() != nullptr;
    if (auto status = tree.setEnabled(toolbarPath_, documentCanSave); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(saveAsButton_, documentCanSave); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            saveButton_, documentCanSave && pathConfigured && dirty);
        !status) {
        return status;
    }
    return refreshPlaySessionUi(tree);
}

auto EditorWorkspaceState::refreshPlaySessionUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    const Tina::Editor::EditorPlayState state = playSession_.has_value()
        ? playSession_->snapshot().state
        : Tina::Editor::EditorPlayState::Editing;
    const bool editing = state == Tina::Editor::EditorPlayState::Editing;
    const bool playing = state == Tina::Editor::EditorPlayState::Playing;
    const bool paused = state == Tina::Editor::EditorPlayState::Paused;

    if (auto status = tree.setText(playButton_, paused ? "Resume" : "Play"); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            playButton_,
            (editing && !pendingDirtyCloseKey_.has_value() &&
             sceneDocumentActive() && playStartReady()) || paused);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(pauseButton_, playing); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(stepButton_, paused); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(stopButton_, playing || paused); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(projectAssetList_, editing); !status) {
        return status;
    }
    const bool sourceImportSelectionEnabled =
        editing && activeProjectWorkspace_.has_value() &&
        !pendingProjectSwitch_.has_value() && !catalogRefreshPending_ &&
        sourceImportService_.state() ==
            Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle;
    if (auto status = tree.setEnabled(sourceImportList_,
                                      sourceImportSelectionEnabled);
        !status) {
        return status;
    }
    if (editing) {
        return Tina::Core::success();
    }

    const std::array lockedButtons{
        mode2DButton_, mode3DButton_, undoButton_, redoButton_, saveButton_,
        saveAsButton_, addEntityButton_, duplicateEntityButton_, deleteEntityButton_,
        reparentEntityRootButton_, reparentEntityButton_, inspectorParentStableId_,
        moveButton_,
        translateToolButtons_[0], translateToolButtons_[1], rotateToolButtons_[0],
        rotateToolButtons_[1], scaleToolButtons_[0], scaleToolButtons_[1],
        orientationButton_, snapButton_, tilePaintToolButton_, tileEraseToolButton_,
        paintTileButton_, eraseTileButton_, toggleTileLayerButton_, addTileLayerButton_,
        addObjectLayerButton_, cookTileMapButton_, generateTileMapGameplayButton_,
        animationModeButton_, animationPlayButton_, animationCookButton_,
        animationPreviousButton_, animationNextButton_, animationAddButton_,
        animationDuplicateButton_, animationDeleteButton_, animationMoveLeftButton_,
        animationMoveRightButton_, animationCycleSpriteButton_,
        animationDurationDecreaseButton_, animationDurationIncreaseButton_,
        animationUndoButton_, animationRedoButton_, openProjectAssetButton_,
        refreshProjectCatalogButton_, createProjectButton_, openProjectButton_,
        importSourceButton_, removeSourceImportButton_, closeDocumentButton_,
    };
    for (const UI::UINodeId button : lockedButtons) {
        if (auto status = tree.setEnabled(button, false); !status) {
            return status;
        }
    }
    for (const UI::UINodeId button : projectFilterButtons_) {
        if (auto status = tree.setEnabled(button, false); !status) {
            return status;
        }
    }
    for (const UI::UINodeId button : documentTabButtons_) {
        if (auto status = tree.setEnabled(button, false); !status) {
            return status;
        }
    }
    for (const UI::UINodeId button : animationFrameButtons_) {
        if (auto status = tree.setEnabled(button, false); !status) {
            return status;
        }
    }
    for (const ComponentSectionUi& section : componentSections_) {
        const std::array sectionControls{
            section.activeCheckbox, section.addButton, section.removeButton,
            section.assignButton, section.applyButton};
        for (const UI::UINodeId control : sectionControls) {
            if (!control.hasValue()) {
                continue;
            }
            if (auto status = tree.setEnabled(control, false); !status) {
                return status;
            }
        }
        for (Tina::Core::usize index = 0; index < section.fieldCount; ++index) {
            if (auto status = tree.setEnabled(section.fields[index], false);
                !status) {
                return status;
            }
        }
    }
    if (auto status = tree.setEnabled(toolbarPath_, false); !status) {
        return status;
    }
    return Tina::Core::success();
}

} // namespace Tina::EditorApp::WorkspaceInternal
