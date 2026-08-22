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
    if (auto status = refreshViewportViewModeUi(tree); !status) {
        return status;
    }
    if (auto status = tree.setText(documentFormat_,
                                   world2D ? "World2D v2 + TileMap v3/v1 | canonical"
                                           : "Prefab schema v2 | canonical");
        !status) {
        return status;
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::refreshWorkspacePanelsUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (pendingWorkspacePanelToggle_.has_value()) {
        const WorkspacePanelKind panel = *pendingWorkspacePanelToggle_;
        const bool isLeftDock = panel == WorkspacePanelKind::LeftDock;
        bool& visible = isLeftDock ? leftDockVisible_ : inspectorVisible_;
        float& visibleFraction = isLeftDock ? leftDockVisibleFraction_
                                            : inspectorVisibleFraction_;
        const UI::UINodeId splitView = isLeftDock ? leftDockSplitView_
                                                   : inspectorSplitView_;
        const UI::UINodeId dock = isLeftDock ? leftDock_ : inspectorDock_;
        const UI::UINodeId splitter = isLeftDock ? leftDockSplitter_
                                                  : inspectorSplitter_;
        UI::UILayoutStyle dockLayout = isLeftDock ? leftDockLayout_
                                                   : inspectorDockLayout_;
        UI::UILayoutStyle splitterLayout = isLeftDock
                                                ? leftDockSplitterLayout_
                                                : inspectorSplitterLayout_;
        const bool nextVisible = !visible;

        if (!nextVisible) {
            auto fraction = tree.splitViewFraction(splitView);
            if (!fraction) {
                return Tina::Core::failure(std::move(fraction.error()));
            }
            if (*fraction > 0.0F && *fraction < 1.0F) {
                visibleFraction = *fraction;
            }
        }

        dockLayout.visibility = nextVisible ? UI::UIVisibility::Visible
                                            : UI::UIVisibility::Collapsed;
        splitterLayout.visibility = nextVisible ? UI::UIVisibility::Visible
                                                : UI::UIVisibility::Collapsed;
        if (auto status = tree.setLayoutStyle(dock, dockLayout); !status) {
            return status;
        }
        if (auto status = tree.setLayoutStyle(splitter, splitterLayout); !status) {
            return status;
        }
        const float collapsedFraction = isLeftDock ? 0.0F : 1.0F;
        if (auto status = tree.setSplitViewFraction(
                splitView, nextVisible ? visibleFraction : collapsedFraction);
            !status) {
            return status;
        }
        if (auto status = tree.setMenuItemChecked(
                viewPanelMenuItems_[isLeftDock ? 0U : 1U], nextVisible);
            !status) {
            return status;
        }

        if (isLeftDock) {
            leftDockLayout_ = dockLayout;
            leftDockSplitterLayout_ = splitterLayout;
        } else {
            inspectorDockLayout_ = dockLayout;
            inspectorSplitterLayout_ = splitterLayout;
        }
        visible = nextVisible;
        pendingWorkspacePanelToggle_.reset();
    }

    if (pendingBottomPanelToggle_.has_value()) {
        const BottomPanelKind requested = *pendingBottomPanelToggle_;
        const BottomPanelKind next = requested == bottomPanel_
                                         ? BottomPanelKind::None
                                         : requested;
        const bool panelVisible = next != BottomPanelKind::None;
        const bool panelWasVisible = bottomPanel_ != BottomPanelKind::None;

        if (!panelVisible && panelWasVisible) {
            auto fraction = tree.splitViewFraction(bottomPanelSplitView_);
            if (!fraction) {
                return Tina::Core::failure(std::move(fraction.error()));
            }
            if (*fraction > 0.0F && *fraction < 1.0F) {
                bottomPanelVisibleFraction_ = *fraction;
            }
        }

        UI::UILayoutStyle splitterLayout = bottomPanelSplitterLayout_;
        splitterLayout.visibility = panelVisible ? UI::UIVisibility::Visible
                                                 : UI::UIVisibility::Collapsed;
        UI::UILayoutStyle hostLayout = bottomPanelHostLayout_;
        hostLayout.visibility = panelVisible ? UI::UIVisibility::Visible
                                             : UI::UIVisibility::Collapsed;
        UI::UILayoutStyle animationLayout = animationPanelLayout_;
        animationLayout.visibility = next == BottomPanelKind::Animation
                                         ? UI::UIVisibility::Visible
                                         : UI::UIVisibility::Collapsed;
        UI::UILayoutStyle outputLayout = outputPanelLayout_;
        outputLayout.visibility = next == BottomPanelKind::Output
                                      ? UI::UIVisibility::Visible
                                      : UI::UIVisibility::Collapsed;

        if (auto status = tree.setLayoutStyle(
                bottomPanelSplitter_, splitterLayout);
            !status) {
            return status;
        }
        if (auto status = tree.setLayoutStyle(bottomPanelHost_, hostLayout);
            !status) {
            return status;
        }
        if (auto status = tree.setLayoutStyle(animationPanel_, animationLayout);
            !status) {
            return status;
        }
        if (auto status = tree.setLayoutStyle(outputPanel_, outputLayout);
            !status) {
            return status;
        }
        if (auto status = tree.setSplitViewFraction(
                bottomPanelSplitView_,
                panelVisible ? bottomPanelVisibleFraction_ : 1.0F);
            !status) {
            return status;
        }
        for (u32 index = 0; index < bottomPanelButtons_.size(); ++index) {
            const BottomPanelKind buttonPanel = index == 0U
                                                    ? BottomPanelKind::Animation
                                                    : BottomPanelKind::Output;
            if (auto status = tree.setRadioButtonSelected(
                    bottomPanelButtons_[index], next == buttonPanel);
                !status) {
                return status;
            }
        }

        bottomPanelSplitterLayout_ = splitterLayout;
        bottomPanelHostLayout_ = hostLayout;
        animationPanelLayout_ = animationLayout;
        outputPanelLayout_ = outputLayout;
        bottomPanel_ = next;
        pendingBottomPanelToggle_.reset();
    }

    return tree.setText(outputMessage_, authoringFeedback_);
}

auto EditorWorkspaceState::refreshMainMenuUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    const bool world2D = workspaceMode_ == WorkspaceMode::World2D;
    const bool editing = authoringEnabled();
    const bool tileMapContext = tileMapEditingContext();
    for (u32 index = 0; index < workspaceModeButtons_.size(); ++index) {
        if (auto status = tree.setRadioButtonSelected(
                workspaceModeButtons_[index], index == (world2D ? 0U : 1U));
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(workspaceModeButtons_[index], editing);
            !status) {
            return status;
        }
    }
    if (auto status = tree.setMenuItemChecked(
            viewWorkspaceMenuItems_[world2D ? 0U : 1U], true);
        !status) {
        return status;
    }
    for (const UI::UINodeId item : viewWorkspaceMenuItems_) {
        if (auto status = tree.setEnabled(item, editing); !status) {
            return status;
        }
    }
    if (auto status = tree.setMenuItemChecked(
            viewPanelMenuItems_[0], leftDockVisible_);
        !status) {
        return status;
    }
    if (auto status = tree.setMenuItemChecked(
            viewPanelMenuItems_[1], inspectorVisible_);
        !status) {
        return status;
    }

    for (u32 index = 0; index < viewportContextButtons_.size(); ++index) {
        viewportContextButtonLayouts_[index].visibility =
            world2D ? UI::UIVisibility::Visible
                    : UI::UIVisibility::Collapsed;
        if (auto status = tree.setLayoutStyle(
                viewportContextButtons_[index],
                viewportContextButtonLayouts_[index]);
            !status) {
            return status;
        }
        if (auto status = tree.setRadioButtonSelected(
                viewportContextButtons_[index],
                index == (tileMapContext ? 1U : 0U));
            !status) {
            return status;
        }
    }
    if (auto status = tree.setEnabled(
            viewportContextButtons_[0], editing && world2D);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            viewportContextButtons_[1],
            editing && world2D &&
                documentTabs_.find(tileMapDocumentOwnerKey_).has_value());
        !status) {
        return status;
    }

    const auto mirrorEnabled = [&tree](
                                   UI::UINodeId source,
                                   UI::UINodeId target) -> Tina::Core::Status {
        auto enabled = tree.isEnabled(source);
        if (!enabled) {
            return Tina::Core::failure(std::move(enabled.error()));
        }
        return tree.setEnabled(target, *enabled);
    };
    const std::array mirroredItems{
        std::pair{createProjectButton_, fileCreateProjectMenuItem_},
        std::pair{openProjectButton_, fileOpenProjectMenuItem_},
        std::pair{importSourceButton_, fileImportSourceMenuItem_},
        std::pair{saveButton_, fileSaveMenuItem_},
        std::pair{saveAsButton_, fileSaveAsMenuItem_},
        std::pair{closeDocumentButton_, fileCloseDocumentMenuItem_},
        std::pair{undoButton_, editUndoMenuItem_},
        std::pair{redoButton_, editRedoMenuItem_},
        std::pair{duplicateEntityButton_, editDuplicateMenuItem_},
        std::pair{deleteEntityButton_, editDeleteMenuItem_},
        std::pair{frameAllButton_, viewFrameAllMenuItem_},
        std::pair{focusEntityButton_, viewFocusSelectionMenuItem_},
    };
    for (const auto& [source, target] : mirroredItems) {
        if (auto status = mirrorEnabled(source, target); !status) {
            return status;
        }
    }
    return tree.setEnabled(helpAboutMenuItem_, true);
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
            documentKindLabel = "SpriteAnimationClip v2";
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
    tileMapStatus += " | Nav ";
    tileMapStatus += navigationDocument_.isDirtyFor(tileMapDocument_.revision())
                         ? "Dirty"
                         : "Current";
    if (auto status = tree.setText(tileMapStatus_, tileMapStatus); !status) {
        return status;
    }
    const u32 selectedStableId = stableEntityIdForHierarchyItem(selectionKey_);
    const bool tileMapContextVisible = tileMapEditingContext();
    const bool entityContextVisible = !assetInspectorActive_ &&
                                      !tileMapContextVisible &&
                                      selectedStableId != 0U;
    const auto publishContextVisibility =
        [&](InspectorLayoutNodeUi& node, bool visible) -> Tina::Core::Status {
        node.layout.visibility = visible ? UI::UIVisibility::Visible
                                         : UI::UIVisibility::Collapsed;
        return tree.setLayoutStyle(node.root, node.layout);
    };
    if (auto status = publishContextVisibility(
            inspectorHierarchyHeaderUi_, entityContextVisible);
        !status) {
        return status;
    }
    if (auto status = publishContextVisibility(
            inspectorHierarchyParentRowUi_, entityContextVisible);
        !status) {
        return status;
    }
    if (auto status = publishContextVisibility(
            inspectorHierarchyApplyParentUi_, entityContextVisible);
        !status) {
        return status;
    }
    if (auto status = publishContextVisibility(
            inspectorTileMapHeaderUi_, tileMapContextVisible);
        !status) {
        return status;
    }
    if (auto status = publishContextVisibility(
            inspectorTileMapStatusUi_, tileMapContextVisible);
        !status) {
        return status;
    }
    for (auto& row : inspectorTileMapActionRows_) {
        if (auto status = publishContextVisibility(row, tileMapContextVisible);
            !status) {
            return status;
        }
    }
    const bool transformVisible = entityContextVisible;
    const UI::UIVisibility transformVisibility =
        transformVisible ? UI::UIVisibility::Visible
                         : UI::UIVisibility::Collapsed;
    inspectorTransformHeaderLayout_.visibility = transformVisibility;
    if (auto status = tree.setLayoutStyle(
            inspectorTransformHeader_, inspectorTransformHeaderLayout_);
        !status) {
        return status;
    }
    for (Tina::Core::usize index = 0;
         index < inspectorTransformNumberFields_.size(); ++index) {
        const auto field = static_cast<InspectorTransformField>(index);
        auto& controls = inspectorTransformNumberFields_[index];
        const bool fieldVisible =
            transformVisible &&
            (workspaceMode_ == WorkspaceMode::World3D ||
             !inspectorTransformFieldRequires3D(field));
        controls.layout.visibility = fieldVisible
                                         ? UI::UIVisibility::Visible
                                         : UI::UIVisibility::Collapsed;
        if (auto status = tree.setLayoutStyle(controls.root, controls.layout);
            !status) {
            return status;
        }
    }
    applyTransformButtonLayout_.visibility = transformVisibility;
    if (auto status = tree.setLayoutStyle(
            applyTransformButtonRoot_, applyTransformButtonLayout_);
        !status) {
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
    const std::array transformFieldEnabled{
        selectionEditable, selectionEditable, selectionEditable3D,
        selectionEditable3D, selectionEditable3D, selectionEditable,
        selectionEditable, selectionEditable, selectionEditable3D,
    };
    for (Tina::Core::usize index = 0;
         index < inspectorTransformNumberFields_.size(); ++index) {
        const auto& controls = inspectorTransformNumberFields_[index];
        if (auto status = tree.setEnabled(
                controls.decrementButton, transformFieldEnabled[index]);
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(
                controls.incrementButton, transformFieldEnabled[index]);
            !status) {
            return status;
        }
    }
    if (auto status = tree.setEnabled(inspectorParentStableId_, selectionEditable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(applyTransformButton_, selectionEditable); !status) {
        return status;
    }
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
    if (auto status = tree.setEnabled(bakeNavigationButton_, tileMapControlsEnabled); !status) {
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
    const bool temporaryProjectCanSave =
        temporaryProjectActive() && !pendingProjectSwitch_.has_value() &&
        pendingSourceImportPathsUtf8_.empty() &&
        sourceImportService_.state() ==
            Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle;
    if (auto status = tree.setEnabled(
            saveAsButton_, documentCanSave || temporaryProjectCanSave);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            saveButton_, temporaryProjectCanSave ||
                             (documentCanSave && pathConfigured && dirty));
        !status) {
        return status;
    }
    if (auto status = refreshPlaySessionUi(tree); !status) {
        return status;
    }
    return refreshMainMenuUi(tree);
}

auto EditorWorkspaceState::refreshPlaySessionUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    const Tina::Editor::EditorPlayState state = playSession_.has_value()
        ? playSession_->snapshot().state
        : Tina::Editor::EditorPlayState::Editing;
    const bool editing = state == Tina::Editor::EditorPlayState::Editing;
    const bool playing = state == Tina::Editor::EditorPlayState::Playing;
    const bool paused = state == Tina::Editor::EditorPlayState::Paused;

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
    if (auto status = tree.setEnabled(sourceImportGrid_,
                                      sourceImportSelectionEnabled);
        !status) {
        return status;
    }
    if (editing) {
        return Tina::Core::success();
    }

    const std::array lockedButtons{
        undoButton_, redoButton_, saveButton_,
        saveAsButton_, addEntityButton_, duplicateEntityButton_, deleteEntityButton_,
        reparentEntityButton_, inspectorParentStableId_,
        translateToolButtons_[0], rotateToolButtons_[0], scaleToolButtons_[0],
        orientationButton_, snapButton_, tilePaintToolButton_, tileEraseToolButton_,
        paintTileButton_, eraseTileButton_, toggleTileLayerButton_, addTileLayerButton_,
        addObjectLayerButton_, cookTileMapButton_, generateTileMapGameplayButton_,
        bakeNavigationButton_,
        animationModeButton_, animationPlaybackButtons_.play.button,
        animationPlaybackButtons_.pause.button, animationCookButton_,
        animationPreviousButton_, animationNextButton_, animationAddButton_,
        animationDuplicateButton_, animationDeleteButton_, animationMoveLeftButton_,
        animationMoveRightButton_, animationCycleSpriteButton_,
        animationDurationDecreaseButton_, animationDurationIncreaseButton_,
        animationEventPreviousButton_, animationEventNextButton_,
        animationEventAddButton_, animationEventApplyButton_,
        animationEventRemoveButton_, openProjectAssetButton_,
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
    if (auto status = tree.setEnabled(animationEventTag_, false); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(animationEventOffset_, false); !status) {
        return status;
    }
    for (const ComponentSectionUi& section : componentSections_) {
        const std::array sectionControls{
            section.activeSwitch, section.addButton, section.removeButton,
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
    return Tina::Core::success();
}

} // namespace Tina::EditorApp::WorkspaceInternal
