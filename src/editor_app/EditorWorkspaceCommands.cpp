#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {

auto EditorWorkspaceState::sceneAddTemplateCount() const noexcept
    -> Tina::Core::usize
{
    return workspaceMode_ == WorkspaceMode::World2D
               ? Tina::Editor::world2DNodeTemplateRegistry().size()
               : Tina::Editor::world3DNodeTemplateRegistry().size();
}

auto EditorWorkspaceState::showSceneAddModal(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (pendingSceneAddRequest_.has_value() ||
        pendingSceneDeleteConfirmation_.has_value() ||
        pendingDirtyCloseKey_.has_value() || !sceneDocumentActive()) {
        return Tina::Core::success();
    }
    const auto* activeTab = documentTabs_.activeTab();
    if (activeTab == nullptr) {
        return Tina::Core::success();
    }
    // The selected item becomes the parent, matching what SceneAdd did before
    // the picker existed.
    const u32 parentStableId =
        assetInspectorActive_ ? 0U : stableEntityIdForHierarchyItem(selectionKey_);
    pendingSceneAddRequest_ = SceneAddRequest{
        .documentKey = activeTab->key,
        .workspace = workspaceMode_,
        .parentStableId = parentStableId,
        .documentRevision = activeDocumentRevision(),
    };
    if (sceneAddTemplateIndex_ >= sceneAddTemplateCount()) {
        sceneAddTemplateIndex_ = 0;
    }
    if (auto status = refreshSceneAddModalUi(tree); !status) {
        pendingSceneAddRequest_.reset();
        return status;
    }
    if (auto status = tree.setLayoutStyle(
            sceneAddModal_, sceneAddModalLayout(UI::UIVisibility::Visible));
        !status) {
        pendingSceneAddRequest_.reset();
        return status;
    }
    authoringFeedback_ = "Choose the node kind to create";
    pendingSceneAddDialogFocus_ = true;
    sceneAddFilterUtf8_.clear();
    return Tina::Core::success();
}

auto EditorWorkspaceState::hideSceneAddModal(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!pendingSceneAddRequest_.has_value()) {
        return Tina::Core::success();
    }
    if (auto status = tree.setLayoutStyle(
            sceneAddModal_, sceneAddModalLayout(UI::UIVisibility::Collapsed));
        !status) {
        return status;
    }
    pendingSceneAddRequest_.reset();
    pendingSceneAddTemplateIndex_.reset();
    sceneAddFilterUtf8_.clear();
    pendingSceneAddDialogFocus_ = false;
    pendingHierarchyFocusRestore_ = true;
    return Tina::Core::success();
}

auto EditorWorkspaceState::createNodeFromSceneAddRequest(
    Tina::PrimaryWindowUITreeUpdater& tree,
    std::optional<u32>& hierarchyRefreshStableId,
    bool& requiresPreviewValidation) -> Tina::Core::Status
{
    if (!pendingSceneAddRequest_.has_value() ||
        pendingSceneAddRequest_->creating) {
        return Tina::Core::success();
    }
    pendingSceneAddRequest_->creating = true;
    const SceneAddRequest request = *pendingSceneAddRequest_;
    const auto* activeTab = documentTabs_.activeTab();
    // The picker is modal, but a queued command or an external reload can still
    // land first, so the captured target is re-checked before publishing.
    const bool targetIsCurrent =
        activeTab != nullptr && activeTab->key == request.documentKey &&
        workspaceMode_ == request.workspace && sceneDocumentActive() &&
        activeDocumentRevision() == request.documentRevision &&
        (request.parentStableId == 0U ||
         hierarchyRow(request.parentStableId) != nullptr);
    if (!targetIsCurrent) {
        authoringFeedback_ =
            "Create cancelled: the target or canonical document changed before confirmation";
        return hideSceneAddModal(tree);
    }
    const auto slot = static_cast<Tina::Core::usize>(sceneAddTemplateIndex_);
    if (slot >= sceneAddTemplateCount()) {
        authoringFeedback_ = "Create cancelled: the node kind is unavailable";
        return hideSceneAddModal(tree);
    }

    Tina::Core::Result<Tina::Editor::EditorSceneOperationResult> added =
        Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Editor scene node template is unknown");
    std::string_view createdKind{};
    if (request.workspace == WorkspaceMode::World2D) {
        const auto registry = Tina::Editor::world2DNodeTemplateRegistry();
        const auto& info = registry[slot];
        createdKind = info.displayName;
        Tina::Editor::World2DNodeTemplateAssets assets{};
        if (info.requiresSpriteAsset) {
            assets.spriteId = selectedProjectAssetIdOfKind(
                Tina::AssetFormat::AssetKind::Sprite);
            if (!assets.spriteId) {
                assets.spriteId = editorAssetId(0x22U);
            }
        }
        if (info.requiresAnimationClipAsset) {
            assets.animationClipId = selectedProjectAssetIdOfKind(
                Tina::AssetFormat::AssetKind::SpriteAnimationClip);
            if (!assets.animationClipId) {
                assets.animationClipId = editorAssetId(0x10U);
            }
        }
        added = Tina::Editor::addWorld2DEntityOfTemplate(
            document_,
            static_cast<Tina::Editor::World2DNodeTemplate>(slot),
            request.parentStableId, assets);
    } else {
        const auto registry = Tina::Editor::world3DNodeTemplateRegistry();
        const auto& info = registry[slot];
        createdKind = info.displayName;
        Tina::Editor::World3DNodeTemplateAssets assets{};
        if (info.requiresMeshAssets) {
            assets.meshId = selectedProjectAssetIdOfKind(
                Tina::AssetFormat::AssetKind::StaticMesh);
            if (!assets.meshId) {
                assets.meshId = editorAssetId(0x31U);
            }
            assets.materialId = selectedProjectAssetIdOfKind(
                Tina::AssetFormat::AssetKind::Material);
            if (!assets.materialId) {
                assets.materialId = editorAssetId(0x32U);
            }
        }
        added = Tina::Editor::addWorld3DNodeOfTemplate(
            document3D_, static_cast<Tina::Editor::World3DNodeTemplate>(slot),
            request.parentStableId, assets);
    }
    if (!added) {
        Tina::Core::Error createError = std::move(added.error());
        if (auto status = reportAuthoringFailure(
                "Create failed; scene hierarchy preserved: ", createError);
            !status) {
            return status;
        }
        return hideSceneAddModal(tree);
    }

    hierarchyRefreshStableId = added->primaryStableId;
    requiresPreviewValidation = true;
    ++counters_.authoringEdits;
    ++counters_.sceneAddCommands;
    if (options_.autoDemo) {
        counters_.automaticAddedStableId = added->primaryStableId;
    }
    try {
        authoringFeedback_ = createdKind;
        authoringFeedback_ += " added as one scene revision";
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "Create Node feedback allocation failed");
    }
    return hideSceneAddModal(tree);
}

auto EditorWorkspaceState::refreshSceneAddModalUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    const bool world2D = workspaceMode_ == WorkspaceMode::World2D;
    const auto registry = world2D
                              ? Tina::Editor::world2DNodeTemplateRegistry()
                              : Tina::Editor::world3DNodeTemplateRegistry();
    try {
        std::string parentText = "Parent: ";
        const u32 parentStableId = pendingSceneAddRequest_.has_value()
                                       ? pendingSceneAddRequest_->parentStableId
                                       : 0U;
        const EditorHierarchyRow* parentRow = hierarchyRow(parentStableId);
        parentText += parentStableId != 0U && parentRow != nullptr
                          ? std::string_view{parentRow->label}
                          : (world2D ? std::string_view{"World2D Scene"}
                                     : std::string_view{"World3D Scene"});
        if (auto status = tree.setText(sceneAddParentLabel_, parentText);
            !status) {
            return status;
        }
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "Create Node parent label allocation failed");
    }

    for (Tina::Core::usize slot = 0; slot < sceneAddTemplateButtons_.size();
         ++slot) {
        const UI::UINodeId button = sceneAddTemplateButtons_[slot];
        // Slots beyond the active registry are collapsed, so switching
        // workspaces never leaves a stale row behind.
        if (slot >= registry.size()) {
            if (auto status = tree.setLayoutStyle(
                    button,
                    sceneAddTemplateRowLayout(UI::UIVisibility::Collapsed));
                !status) {
                return status;
            }
            continue;
        }
        const auto matchesFilter = [&](std::string_view text) {
            if (sceneAddFilterUtf8_.empty()) {
                return true;
            }
            if (sceneAddFilterUtf8_.size() > text.size()) {
                return false;
            }
            for (Tina::Core::usize offset = 0;
                 offset + sceneAddFilterUtf8_.size() <= text.size(); ++offset) {
                bool matches = true;
                for (Tina::Core::usize index = 0;
                     index < sceneAddFilterUtf8_.size(); ++index) {
                    const char left = text[offset + index];
                    const char right = sceneAddFilterUtf8_[index];
                    const char lowerLeft = left >= 'A' && left <= 'Z'
                        ? static_cast<char>(left + ('a' - 'A')) : left;
                    const char lowerRight = right >= 'A' && right <= 'Z'
                        ? static_cast<char>(right + ('a' - 'A')) : right;
                    if (lowerLeft != lowerRight) {
                        matches = false;
                        break;
                    }
                }
                if (matches) {
                    return true;
                }
            }
            return false;
        };
        const bool visible = matchesFilter(registry[slot].displayName) ||
                             matchesFilter(registry[slot].description);
        if (auto status = tree.setLayoutStyle(
                button, sceneAddTemplateRowLayout(visible
                    ? UI::UIVisibility::Visible : UI::UIVisibility::Collapsed));
            !status) {
            return status;
        }
        if (auto status = tree.setText(button, registry[slot].displayName);
            !status) {
            return status;
        }
        if (auto status = tree.setRadioButtonSelected(
                button, slot == static_cast<Tina::Core::usize>(
                                    sceneAddTemplateIndex_));
            !status) {
            return status;
        }
    }

    // Asset-backed templates resolve the same way the Inspector's Add
    // Component does (Project Assets selection, else the built-in preview
    // asset), so no row is ever unreachable.
    const Tina::Core::usize selected =
        static_cast<Tina::Core::usize>(sceneAddTemplateIndex_) < registry.size()
            ? static_cast<Tina::Core::usize>(sceneAddTemplateIndex_)
            : 0U;
    return tree.setText(
        sceneAddDescription_,
        registry.empty() ? std::string_view{} : registry[selected].description);
}

auto EditorWorkspaceState::processPendingSceneAddTemplate(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!pendingSceneAddTemplateIndex_.has_value()) {
        return Tina::Core::success();
    }
    const u32 requested = *pendingSceneAddTemplateIndex_;
    pendingSceneAddTemplateIndex_.reset();
    if (!pendingSceneAddRequest_.has_value() ||
        requested >= sceneAddTemplateCount()) {
        return Tina::Core::success();
    }
    sceneAddTemplateIndex_ = requested;
    return refreshSceneAddModalUi(tree);
}

auto EditorWorkspaceState::updateSceneAddSearch(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    auto text = tree.text(sceneAddSearchInput_);
    if (!text) {
        return Tina::Core::failure(std::move(text.error()));
    }
    if (*text == sceneAddFilterUtf8_) {
        return Tina::Core::success();
    }
    try {
        sceneAddFilterUtf8_.assign(text->data(), text->size());
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "Scene add search filter allocation failed");
    }
    return refreshSceneAddModalUi(tree);
}

auto EditorWorkspaceState::showHierarchyRename(
    Tina::PrimaryWindowUITreeUpdater& tree, u32 stableId) -> Tina::Core::Status
{
    const EditorHierarchyRow* row = hierarchyRow(stableId);
    if (row == nullptr || stableId == 0U || !sceneDocumentActive()) {
        return Tina::Core::success();
    }
    hierarchyRenameStableId_ = stableId;
    hierarchyRenameDocumentRevision_ = activeDocumentRevision();
    hierarchyRenameVisible_ = true;
    if (auto status = tree.setLayoutStyle(
            hierarchyRenameRoot_, hierarchyRenameLayout(UI::UIVisibility::Visible));
        !status) {
        return status;
    }
    if (auto status = tree.setText(hierarchyRenameInput_, row->label); !status) {
        return status;
    }
    if (auto status = tree.setTextSelection(
            hierarchyRenameInput_, UI::UITextSelection{
                .anchorCodepoint = 0,
                .caretCodepoint = static_cast<u32>(row->label.size())}); !status) {
        return status;
    }
    return tree.requestFocus(hierarchyRenameInput_);
}

auto EditorWorkspaceState::hideHierarchyRename(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    hierarchyRenameVisible_ = false;
    hierarchyRenameStableId_ = 0;
    hierarchyRenameDocumentRevision_ = 0;
    pendingHierarchyRenameCancel_ = false;
    pendingHierarchyRenameCommit_ = false;
    return tree.setLayoutStyle(
        hierarchyRenameRoot_, hierarchyRenameLayout(UI::UIVisibility::Collapsed));
}

auto EditorWorkspaceState::applyHierarchyRename(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!hierarchyRenameVisible_ || hierarchyRenameStableId_ == 0U) {
        return Tina::Core::success();
    }
    if (activeDocumentRevision() != hierarchyRenameDocumentRevision_ ||
        hierarchyRow(hierarchyRenameStableId_) == nullptr) {
        authoringFeedback_ = "Rename cancelled: the scene document changed";
        return hideHierarchyRename(tree);
    }
    auto text = tree.text(hierarchyRenameInput_);
    if (!text) {
        return Tina::Core::failure(std::move(text.error()));
    }
    if (text->empty() || text->size() > 128U ||
        !Tina::Core::isStrictUtf8WithoutNul(*text)) {
        authoringFeedback_ = "Rename rejected: enter a non-empty UTF-8 name";
        return Tina::Core::success();
    }
    try {
        const auto existing = std::find_if(
            hierarchyNameOverrides_.begin(), hierarchyNameOverrides_.end(),
            [this](const EditorHierarchyNameOverride& candidate) {
                return candidate.stableId == hierarchyRenameStableId_;
            });
        if (existing != hierarchyNameOverrides_.end()) {
            existing->name.assign(text->data(), text->size());
        } else {
            hierarchyNameOverrides_.push_back({
                .stableId = hierarchyRenameStableId_,
                .name = std::string{text->data(), text->size()},
            });
        }
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "Hierarchy rename allocation failed");
    }
    const u32 renamedStableId = hierarchyRenameStableId_;
    if (auto status = rebuildHierarchyModel(); !status) {
        return status;
    }
    if (auto status = tree.invalidateTreeViewItems(hierarchyTree_); !status) {
        return status;
    }
    authoringFeedback_ = "Scene node renamed for this Editor session";
    if (auto status = hideHierarchyRename(tree); !status) {
        return status;
    }
    return tree.setTreeViewSelectedIndex(
        hierarchyTree_, visibleHierarchyIndex(renamedStableId).value_or(0U));
}

auto EditorWorkspaceState::processPendingHierarchyRename(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (pendingHierarchyRenameCancel_) {
        return hideHierarchyRename(tree);
    }
    if (pendingHierarchyRenameCommit_) {
        pendingHierarchyRenameCommit_ = false;
        pendingHierarchyRenameStableId_.reset();
        return applyHierarchyRename(tree);
    }
    if (!pendingHierarchyRenameStableId_.has_value()) {
        return Tina::Core::success();
    }
    const u32 stableId = *pendingHierarchyRenameStableId_;
    pendingHierarchyRenameStableId_.reset();
    return showHierarchyRename(tree, stableId);
}

auto EditorWorkspaceState::processPendingHierarchyReorder(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!pendingHierarchyReorderStableId_.has_value() ||
        !pendingHierarchyReorderBeforeStableId_.has_value()) {
        return Tina::Core::success();
    }
    const u32 stableId = *pendingHierarchyReorderStableId_;
    const u32 beforeStableId = *pendingHierarchyReorderBeforeStableId_;
    pendingHierarchyReorderStableId_.reset();
    pendingHierarchyReorderBeforeStableId_.reset();
    if (!sceneDocumentActive() || hierarchyRow(stableId) == nullptr ||
        hierarchyRow(beforeStableId) == nullptr) {
        return Tina::Core::success();
    }
    const EditorHierarchyRow* source = hierarchyRow(stableId);
    const EditorHierarchyRow* destination = hierarchyRow(beforeStableId);
    if (source == nullptr || destination == nullptr ||
        source->parentStableId != destination->parentStableId) {
        return Tina::Core::success();
    }
    Tina::Core::Status status = workspaceMode_ == WorkspaceMode::World2D
        ? Tina::Editor::reorderWorld2DEntity(document_, stableId, beforeStableId)
        : Tina::Editor::reorderWorld3DNode(document3D_, stableId, beforeStableId);
    if (!status) {
        return status;
    }
    ++counters_.authoringEdits;
    authoringFeedback_ = "Scene sibling order updated as one canonical revision";
    if (auto refresh = refreshHierarchyTree(tree, stableId); !refresh) {
        return refresh;
    }
    return refreshAuthoringUi(tree);
}

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
        if (temporaryProjectActive()) {
            status = saveTemporaryProjectFromDialog();
            if (!status) {
                try {
                    authoringFeedback_ = "Project save failed; temporary project preserved: ";
                    authoringFeedback_ += status.error().message;
                } catch (const std::bad_alloc&) {
                    return Tina::Core::failure(
                        Tina::Core::CoreErrorCode::OutOfMemory,
                        "Project save failure message allocation failed");
                }
                status = Tina::Core::success();
            }
            break;
        }
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
        projectSwitchBlockedByDirty_ = false;
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
        // Creation is a two-step command now: pick the node kind, then confirm.
        // The kind is applied at Confirm so the whole node, components
        // included, publishes as one canonical revision.
        status = showSceneAddModal(tree);
        break;
    }
    case EditorCommand::SceneAddConfirm:
        status = createNodeFromSceneAddRequest(tree, hierarchyRefreshStableId,
                                               requiresPreviewValidation);
        break;
    case EditorCommand::SceneAddCancel:
        if (pendingSceneAddRequest_.has_value()) {
            authoringFeedback_ = "Create cancelled; scene hierarchy preserved";
            status = hideSceneAddModal(tree);
        }
        break;
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
