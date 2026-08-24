#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {

auto EditorWorkspaceState::inspectorTransformValueDiffers(float current,
                                                         float requested) noexcept -> bool{
    const float scale = (std::max)({1.0F, std::abs(current),
                                    std::abs(requested)});
    return std::abs(current - requested) > 1.0e-4F * scale;
}

auto EditorWorkspaceState::inspectorRotationEquivalent(
    const std::array<float, 4>& requested,
    float currentX, float currentY, float currentZ,
    float currentW) noexcept -> bool{
    const bool same =
        !inspectorTransformValueDiffers(currentX, requested[0]) &&
        !inspectorTransformValueDiffers(currentY, requested[1]) &&
        !inspectorTransformValueDiffers(currentZ, requested[2]) &&
        !inspectorTransformValueDiffers(currentW, requested[3]);
    const bool opposite =
        !inspectorTransformValueDiffers(currentX, -requested[0]) &&
        !inspectorTransformValueDiffers(currentY, -requested[1]) &&
        !inspectorTransformValueDiffers(currentZ, -requested[2]) &&
        !inspectorTransformValueDiffers(currentW, -requested[3]);
    return same || opposite;
}

auto EditorWorkspaceState::applySelectedTransform(
    const InspectorTransformInput& input) -> Tina::Core::Status{
    const auto finiteOptional = [](const std::optional<float>& value) noexcept {
        return !value.has_value() || std::isfinite(*value);
    };
    if (!finiteOptional(input.positionX) || !finiteOptional(input.positionY) ||
        !finiteOptional(input.positionZ) || !finiteOptional(input.rotationX) ||
        !finiteOptional(input.rotationY) || !finiteOptional(input.rotationZ) ||
        !finiteOptional(input.scaleX) || !finiteOptional(input.scaleY) ||
        !finiteOptional(input.scaleZ)) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Inspector transform contains a non-finite value");
    }
    const auto invalidScale = [](const std::optional<float>& value) noexcept {
        return value.has_value() && *value <= 0.0F;
    };
    if (invalidScale(input.scaleX) || invalidScale(input.scaleY) ||
        (workspaceMode_ == WorkspaceMode::World3D &&
         invalidScale(input.scaleZ))) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Inspector scale values must be greater than zero");
    }
    if (viewportSelectedEntityCount_ == 0U) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::EntityNotFound,
            "Inspector transform requires a viewport scene selection");
    }

    const std::span<const u64> selectedIds{
        viewportSelectedEntityIds_.data(), viewportSelectedEntityCount_};
    for (const u64 stableId : selectedIds) {
        if (stableId == 0U ||
            stableId > (std::numeric_limits<u32>::max)()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::EntityNotFound,
                "Inspector transform selection contains an invalid stable ID");
        }
    }
    std::array<bool, Tina::Editor::EditorMarqueeSelectionCapacity>
        matched{};
    const auto selectedIndex = [&](u32 stableId) noexcept
        -> std::optional<Tina::Core::usize> {
        const auto selected = std::find(
            selectedIds.begin(), selectedIds.end(), static_cast<u64>(stableId));
        if (selected == selectedIds.end()) {
            return std::nullopt;
        }
        return static_cast<Tina::Core::usize>(
            std::distance(selectedIds.begin(), selected));
    };

    if (workspaceMode_ == WorkspaceMode::World3D) {
        std::vector<Tina::AssetFormat::PrefabNodeView> views;
        auto prefab = document3D_.parseCurrentPrefab(views);
        if (!prefab) {
            return Tina::Core::failure(std::move(prefab.error()));
        }
        std::vector<Tina::AssetFormat::PrefabNodeDesc> staged;
        bool changed = false;
        try {
            staged.reserve(views.size());
            for (const auto& node : views) {
                Tina::AssetFormat::PrefabNodeDesc edited{
                    .stableNodeId = node.stableNodeId,
                    .parentIndex = node.parentIndex,
                    .nodeKind = node.nodeKind,
                    .name = node.name,
                    .positionX = node.positionX,
                    .positionY = node.positionY,
                    .positionZ = node.positionZ,
                    .rotationX = node.rotationX,
                    .rotationY = node.rotationY,
                    .rotationZ = node.rotationZ,
                    .rotationW = node.rotationW,
                    .scaleX = node.scaleX,
                    .scaleY = node.scaleY,
                    .scaleZ = node.scaleZ,
                    .meshId = node.meshId,
                    .materialId = node.materialId,
                    .visible = node.visible,
                    .camera = node.camera,
                    .light = node.light,
                };
                const auto index = selectedIndex(node.stableNodeId);
                if (index.has_value()) {
                    matched[*index] = true;
                    const auto applyValue = [&](const std::optional<float>& value,
                                                float& field) {
                        if (value.has_value() &&
                            inspectorTransformValueDiffers(field, *value)) {
                            field = *value;
                            changed = true;
                        }
                    };
                    applyValue(input.positionX, edited.positionX);
                    applyValue(input.positionY, edited.positionY);
                    applyValue(input.positionZ, edited.positionZ);

                    if (input.rotationX.has_value() ||
                        input.rotationY.has_value() ||
                        input.rotationZ.has_value()) {
                        EulerDegrees rotation = eulerDegreesFromQuaternion(
                            node.rotationX, node.rotationY,
                            node.rotationZ, node.rotationW);
                        rotation.x = input.rotationX.value_or(rotation.x);
                        rotation.y = input.rotationY.value_or(rotation.y);
                        rotation.z = input.rotationZ.value_or(rotation.z);
                        const std::array requested =
                            quaternionFromEulerDegrees(rotation);
                        if (!inspectorRotationEquivalent(
                                requested, node.rotationX, node.rotationY,
                                node.rotationZ, node.rotationW)) {
                            edited.rotationX = requested[0];
                            edited.rotationY = requested[1];
                            edited.rotationZ = requested[2];
                            edited.rotationW = requested[3];
                            changed = true;
                        }
                    }
                    applyValue(input.scaleX, edited.scaleX);
                    applyValue(input.scaleY, edited.scaleY);
                    applyValue(input.scaleZ, edited.scaleZ);
                }
                staged.push_back(edited);
            }
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Inspector World3D batch staging allocation failed");
        }
        if (!std::all_of(matched.begin(),
                         matched.begin() + static_cast<std::ptrdiff_t>(
                             selectedIds.size()),
                         [](bool value) { return value; })) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::EntityNotFound,
                "Inspector selection is absent from the World3D document");
        }
        if (!changed) {
            return Tina::Core::success();
        }
        return document3D_.replace({
            .nodes = std::span<const Tina::AssetFormat::PrefabNodeDesc>{staged},
        });
    }

    std::vector<Tina::AssetFormat::World2DEntityDesc> staged;
    auto snapshot = document_.parseCurrentSnapshot(staged);
    if (!snapshot) {
        return Tina::Core::failure(std::move(snapshot.error()));
    }
    bool changed = false;
    for (auto& entity : staged) {
        const auto index = selectedIndex(entity.stableEntityId);
        if (!index.has_value()) {
            continue;
        }
        matched[*index] = true;
        const auto applyValue = [&](const std::optional<float>& value,
                                    float& field) {
            if (value.has_value() &&
                inspectorTransformValueDiffers(field, *value)) {
                field = *value;
                changed = true;
            }
        };
        applyValue(input.positionX, entity.positionX);
        applyValue(input.positionY, entity.positionY);
        if (input.rotationZ.has_value()) {
            const float normalizedDegrees =
                std::remainder(*input.rotationZ, 360.0F);
            const float halfRadians =
                normalizedDegrees * DegreesToRadians * 0.5F;
            const std::array requested{
                0.0F,
                0.0F,
                std::sin(halfRadians),
                std::cos(halfRadians),
            };
            if (!inspectorRotationEquivalent(
                    requested, entity.rotationX, entity.rotationY,
                    entity.rotationZ, entity.rotationW)) {
                entity.rotationX = requested[0];
                entity.rotationY = requested[1];
                entity.rotationZ = requested[2];
                entity.rotationW = requested[3];
                changed = true;
            }
        }
        applyValue(input.scaleX, entity.scaleX);
        applyValue(input.scaleY, entity.scaleY);
    }
    if (!std::all_of(matched.begin(),
                     matched.begin() + static_cast<std::ptrdiff_t>(
                         selectedIds.size()),
                     [](bool value) { return value; })) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::EntityNotFound,
            "Inspector selection is absent from the World2D document");
    }
    if (!changed) {
        return Tina::Core::success();
    }
    return document_.replace({
        .entities =
            std::span<const Tina::AssetFormat::World2DEntityDesc>{staged},
        .gameplaySchema = snapshot->gameplaySchema,
        .gameplayVersion = snapshot->gameplayVersion,
        .gameplayBytes = snapshot->gameplayBytes,
    });
}

auto EditorWorkspaceState::inspectorMixedTransformFlags(u32 primaryStableId) const -> Tina::Core::Result<InspectorMixedTransformFlags>{
    InspectorMixedTransformFlags mixed{};
    if (viewportSelectedEntityCount_ <= 1U || primaryStableId == 0U) {
        return mixed;
    }
    const auto differs = [](float lhs, float rhs) noexcept {
        constexpr float Epsilon = 1.0e-4F;
        return std::abs(lhs - rhs) > Epsilon;
    };
    const auto angleDiffers = [](float lhs, float rhs) noexcept {
        constexpr float Epsilon = 1.0e-4F;
        return std::abs(std::remainder(lhs - rhs, 360.0F)) > Epsilon;
    };
    if (workspaceMode_ == WorkspaceMode::World2D) {
        std::vector<Tina::AssetFormat::World2DEntityDesc> entities;
        auto snapshot = document_.parseCurrentSnapshot(entities);
        if (!snapshot) {
            return Tina::Core::failure(std::move(snapshot.error()));
        }
        const auto primary = std::find_if(
            entities.begin(), entities.end(),
            [primaryStableId](const auto& entity) {
                return entity.stableEntityId == primaryStableId;
            });
        if (primary == entities.end()) {
            return mixed;
        }
        const float primaryRotation = eulerDegreesFromQuaternion(
            primary->rotationX, primary->rotationY, primary->rotationZ,
            primary->rotationW).z;
        for (Tina::Core::usize index = 0;
             index < viewportSelectedEntityCount_; ++index) {
            const u32 stableId = static_cast<u32>(
                viewportSelectedEntityIds_[index]);
            const auto selected = std::find_if(
                entities.begin(), entities.end(),
                [stableId](const auto& entity) {
                    return entity.stableEntityId == stableId;
                });
            if (selected == entities.end()) {
                mixed[inspectorTransformFieldIndex(
                    InspectorTransformField::PositionX)] = true;
                mixed[inspectorTransformFieldIndex(
                    InspectorTransformField::PositionY)] = true;
                mixed[inspectorTransformFieldIndex(
                    InspectorTransformField::RotationZ)] = true;
                mixed[inspectorTransformFieldIndex(
                    InspectorTransformField::ScaleX)] = true;
                mixed[inspectorTransformFieldIndex(
                    InspectorTransformField::ScaleY)] = true;
                continue;
            }
            const float rotation = eulerDegreesFromQuaternion(
                selected->rotationX, selected->rotationY,
                selected->rotationZ, selected->rotationW).z;
            mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionX)] =
                mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionX)] ||
                differs(selected->positionX, primary->positionX);
            mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionY)] =
                mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionY)] ||
                differs(selected->positionY, primary->positionY);
            mixed[inspectorTransformFieldIndex(InspectorTransformField::RotationZ)] =
                mixed[inspectorTransformFieldIndex(InspectorTransformField::RotationZ)] ||
                angleDiffers(rotation, primaryRotation);
            mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleX)] =
                mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleX)] ||
                differs(selected->scaleX, primary->scaleX);
            mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleY)] =
                mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleY)] ||
                differs(selected->scaleY, primary->scaleY);
        }
        return mixed;
    }

    std::vector<Tina::AssetFormat::PrefabNodeView> nodes;
    auto prefab = document3D_.parseCurrentPrefab(nodes);
    if (!prefab) {
        return Tina::Core::failure(std::move(prefab.error()));
    }
    const auto primary = std::find_if(
        nodes.begin(), nodes.end(), [primaryStableId](const auto& node) {
            return node.stableNodeId == primaryStableId;
        });
    if (primary == nodes.end()) {
        return mixed;
    }
    const EulerDegrees primaryRotation = eulerDegreesFromQuaternion(
        primary->rotationX, primary->rotationY, primary->rotationZ,
        primary->rotationW);
    for (Tina::Core::usize index = 0;
         index < viewportSelectedEntityCount_; ++index) {
        const u32 stableId = static_cast<u32>(
            viewportSelectedEntityIds_[index]);
        const auto selected = std::find_if(
            nodes.begin(), nodes.end(), [stableId](const auto& node) {
                return node.stableNodeId == stableId;
            });
        if (selected == nodes.end()) {
            mixed.fill(true);
            continue;
        }
        const EulerDegrees rotation = eulerDegreesFromQuaternion(
            selected->rotationX, selected->rotationY, selected->rotationZ,
            selected->rotationW);
        mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionX)] =
            mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionX)] ||
            differs(selected->positionX, primary->positionX);
        mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionY)] =
            mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionY)] ||
            differs(selected->positionY, primary->positionY);
        mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionZ)] =
            mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionZ)] ||
            differs(selected->positionZ, primary->positionZ);
        mixed[inspectorTransformFieldIndex(InspectorTransformField::RotationX)] =
            mixed[inspectorTransformFieldIndex(InspectorTransformField::RotationX)] ||
            angleDiffers(rotation.x, primaryRotation.x);
        mixed[inspectorTransformFieldIndex(InspectorTransformField::RotationY)] =
            mixed[inspectorTransformFieldIndex(InspectorTransformField::RotationY)] ||
            angleDiffers(rotation.y, primaryRotation.y);
        mixed[inspectorTransformFieldIndex(InspectorTransformField::RotationZ)] =
            mixed[inspectorTransformFieldIndex(InspectorTransformField::RotationZ)] ||
            angleDiffers(rotation.z, primaryRotation.z);
        mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleX)] =
            mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleX)] ||
            differs(selected->scaleX, primary->scaleX);
        mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleY)] =
            mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleY)] ||
            differs(selected->scaleY, primary->scaleY);
        mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleZ)] =
            mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleZ)] ||
            differs(selected->scaleZ, primary->scaleZ);
    }
    return mixed;
}

auto EditorWorkspaceState::publishInspector(Tina::PrimaryWindowUITreeUpdater& tree,
                                                  UI::UITreeViewItemKey key) -> Tina::Core::Status{
    const auto setInspectorSelectionVisibility =
        [&](bool hasSelection) -> Tina::Core::Status {
        inspectorContentLayout_.visibility = hasSelection
                                             ? UI::UIVisibility::Visible
                                             : UI::UIVisibility::Collapsed;
        inspectorEmptyStateLayout_.visibility = hasSelection
                                                    ? UI::UIVisibility::Collapsed
                                                    : UI::UIVisibility::Visible;
        if (auto status = tree.setLayoutStyle(
                inspectorContent_, inspectorContentLayout_);
            !status) {
            return status;
        }
        return tree.setLayoutStyle(inspectorEmptyState_, inspectorEmptyStateLayout_);
    };
    const auto setAssetMetadataVisibility =
        [&](UI::UIVisibility visibility) -> Tina::Core::Status {
        UI::UILayoutStyle assetRowStyle = inspectorAssetRowLayout_;
        assetRowStyle.visibility = visibility;
        if (auto status = tree.setLayoutStyle(inspectorAssetRow_, assetRowStyle);
            !status) {
            return status;
        }
        UI::UILayoutStyle assignStyle = fillWidth(42.0F);
        assignStyle.flexItem.shrink = 0.0F;
        assignStyle.visibility = visibility;
        if (auto status = tree.setLayoutStyle(
                inspectorAssignSpriteButton_, assignStyle); !status) {
            return status;
        }
        UI::UILayoutStyle summaryStyle = fillWidth(22.0F);
        summaryStyle.flexItem.shrink = 0.0F;
        summaryStyle.visibility = visibility;
        if (auto status = tree.setLayoutStyle(inspectorDependencySummary_, summaryStyle);
            !status) {
            return status;
        }
        UI::UILayoutStyle listStyle = fillWidth(156.0F);
        listStyle.flexItem.shrink = 0.0F;
        listStyle.visibility = visibility;
        return tree.setLayoutStyle(inspectorDependencyList_, listStyle);
    };
    if (assetInspectorActive_) {
        if (auto status = setInspectorSelectionVisibility(true); !status) {
            return status;
        }
        if (auto status = setAssetMetadataVisibility(UI::UIVisibility::Visible);
            !status) {
            return status;
        }
        if (auto status = tree.setText(inspectorMode_, "Asset"); !status) {
            return status;
        }
        const auto* asset = inspectedProjectAsset();
        if (asset == nullptr) {
            if (auto status = tree.setText(inspectorName_, "Asset unavailable"); !status) {
                return status;
            }
            if (auto status = tree.setText(inspectorKind_, "Missing from active Catalog");
                !status) {
                return status;
            }
            std::string note = "AssetId: ";
            const auto* activeTab = documentTabs_.activeTab();
            if (activeTab != nullptr && activeTab->key.assetId) {
                const auto idText = activeTab->key.assetId.canonicalText();
                note.append(idText.data(), idText.size());
            } else {
                note += "none";
            }
            note += " | The active project no longer contains this asset";
            if (auto status = tree.setText(inspectorNote_, note); !status) {
                return status;
            }
            if (auto status = tree.setText(inspectorAssetPath_, "Cooked: unavailable");
                !status) {
                return status;
            }
            if (auto status = tree.setText(inspectorDependencySummary_, "Dependencies 0");
                !status) {
                return status;
            }
            if (auto status = tree.setEnabled(inspectorAssignSpriteButton_, false);
                !status) {
                return status;
            }
            inspectorDependencyLabels_.clear();
            if (auto status = tree.invalidateListViewItems(inspectorDependencyList_); !status) {
                return status;
            }
            for (const UI::UINodeId field : {
                     inspectorParentStableId_,
                     inspectorPositionX_, inspectorPositionY_, inspectorPositionZ_,
                     inspectorRotationX_, inspectorRotationY_, inspectorRotationZ_,
                     inspectorScaleX_, inspectorScaleY_, inspectorScaleZ_}) {
                if (auto status = tree.setText(field, "n/a"); !status) {
                    return status;
                }
            }
            return Tina::Core::success();
        }
        if (auto status = tree.setText(inspectorName_, asset->displayName); !status) {
            return status;
        }
        if (auto status = tree.setText(
                inspectorKind_,
                Tina::Editor::projectAssetKindLabel(asset->assetKind));
            !status) {
            return status;
        }
        const auto idText = asset->assetId.canonicalText();
        std::string note = "AssetId: ";
        note.append(idText.data(), idText.size());
        note += " | v";
        note += std::to_string(asset->assetTypeVersion);
        note += " | deps ";
        note += std::to_string(asset->dependencyCount);
        note += " | ";
        note += std::to_string(asset->cookedFileBytes);
        note += " B";
        if (auto status = tree.setText(inspectorNote_, note); !status) {
            return status;
        }
        std::string path = "Cooked: ";
        path += asset->canonicalRelativeCookedPath;
        if (!asset->sourcePathUtf8.empty()) {
            path += " | Source: ";
            path += asset->sourcePathUtf8;
        }
        if (!asset->folderPathUtf8.empty()) {
            path += " | Folder: ";
            path += asset->folderPathUtf8;
        }
        if (auto status = tree.setText(inspectorAssetPath_, path); !status) {
            return status;
        }
        std::string dependencySummary = "Dependencies ";
        dependencySummary += std::to_string(asset->dependencies.size());
        if (auto status = tree.setText(inspectorDependencySummary_, dependencySummary);
            !status) {
            return status;
        }
        const bool canAssignSprite =
            authoringEnabled() && !playSessionActive() &&
            workspaceMode_ == WorkspaceMode::World2D && sceneDocumentActive() &&
            viewportSelectedEntityCount_ != 0U && selectedProjectSpriteAssetId();
        if (auto status = tree.setEnabled(
                inspectorAssignSpriteButton_, canAssignSprite); !status) {
            return status;
        }
        try {
            std::vector<std::string> dependencyLabels;
            dependencyLabels.reserve(asset->dependencies.size());
            for (const Tina::AssetFormat::AssetDependency& dependency :
                 asset->dependencies) {
                const auto dependencyId = dependency.assetId.canonicalText();
                std::string label{
                    Tina::Editor::projectAssetKindLabel(dependency.expectedKind)};
                label += "  ";
                label.append(dependencyId.data(), 8U);
                label += "  | Required";
                if (Tina::AssetFormat::hasDependencyFlag(
                        dependency.flags,
                        Tina::AssetFormat::DependencyFlags::Deferred)) {
                    label += ", deferred load";
                }
                dependencyLabels.push_back(std::move(label));
            }
            inspectorDependencyLabels_ = std::move(dependencyLabels);
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Project Asset Inspector dependency label allocation failed");
        }
        if (auto status = tree.invalidateListViewItems(inspectorDependencyList_); !status) {
            return status;
        }
        for (const UI::UINodeId field : {
                 inspectorParentStableId_,
                 inspectorPositionX_, inspectorPositionY_, inspectorPositionZ_,
                 inspectorRotationX_, inspectorRotationY_, inspectorRotationZ_,
                 inspectorScaleX_, inspectorScaleY_, inspectorScaleZ_}) {
            if (auto status = tree.setText(field, "n/a"); !status) {
                return status;
            }
        }
        return Tina::Core::success();
    }
    if (auto status = setAssetMetadataVisibility(UI::UIVisibility::Collapsed);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(inspectorAssignSpriteButton_, false);
        !status) {
        return status;
    }
    inspectorDependencyLabels_.clear();
    if (auto status = tree.setText(inspectorAssetPath_, {}); !status) {
        return status;
    }
    if (auto status = tree.setText(inspectorDependencySummary_, {}); !status) {
        return status;
    }
    if (auto status = tree.invalidateListViewItems(inspectorDependencyList_); !status) {
        return status;
    }
    if (key == UI::InvalidUITreeViewItemKey) {
        if (auto status = setInspectorSelectionVisibility(false); !status) {
            return status;
        }
        if (auto status = tree.setText(inspectorMode_, "No selection"); !status) {
            return status;
        }
        if (auto status = tree.setText(inspectorName_, "Select an item in Hierarchy");
            !status) {
            return status;
        }
        if (auto status = tree.setText(inspectorKind_, "No item selected"); !status) {
            return status;
        }
        if (auto status = tree.setText(
                inspectorNote_,
                "Choose a scene node or asset to inspect its properties.");
            !status) {
            return status;
        }
        for (const UI::UINodeId field : {
                 inspectorParentStableId_,
                 inspectorPositionX_, inspectorPositionY_, inspectorPositionZ_,
                 inspectorRotationX_, inspectorRotationY_, inspectorRotationZ_,
                 inspectorScaleX_, inspectorScaleY_, inspectorScaleZ_}) {
            if (auto status = tree.setText(field, "n/a"); !status) {
                return status;
            }
        }
        return Tina::Core::success();
    }
    if (auto status = setInspectorSelectionVisibility(true); !status) {
        return status;
    }
    const bool mixedSelection = !tileMapEditingContext() &&
                                stableEntityIdForHierarchyItem(key) != 0U &&
                                viewportSelectedEntityCount_ > 1U;
    if (auto status = tree.setText(
            inspectorMode_,
            tileMapEditingContext()
                ? std::string_view{"Document"}
                : (mixedSelection ? std::string_view{"Mixed values"}
                                  : std::string_view{"Selection"}));
        !status) {
        return status;
    }
    if (auto status = tree.setText(
            inspectorName_,
            tileMapEditingContext() ? std::string_view{"TileMap2D"}
                                    : hierarchyDisplayLabel(key));
        !status) {
        return status;
    }
    if (auto status = tree.setText(
            inspectorKind_,
            tileMapEditingContext() ? std::string_view{"TileMap document"}
                                    : hierarchyDisplayKind(key));
        !status) {
        return status;
    }
    std::string note{
        tileMapEditingContext()
            ? std::string_view{
                  "Root and streamed chunks publish one canonical revision."}
            : hierarchyDisplayNote(key)};
    if (tileMapEditingContext()) {
        note += " Layers=";
        note += std::to_string(tileMapDocument_.layerCount());
        note += ", chunks=";
        note += std::to_string(tileMapDocument_.chunkCount());
        note += ", cells=";
        note += std::to_string(tileMapDocument_.nonEmptyCellCount());
    }
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    EulerDegrees rotationDegrees{};
    float scaleX = 1.0F;
    float scaleY = 1.0F;
    float scaleZ = 1.0F;
    bool hasEntity = false;
    const u32 stableEntityId = tileMapEditingContext()
                                   ? 0U
                                   : stableEntityIdForHierarchyItem(key);
    if (stableEntityId != 0U) {
        if (workspaceMode_ == WorkspaceMode::World2D) {
            std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
            auto snapshot = document_.parseCurrentSnapshot(storage);
            if (!snapshot) {
                return Tina::Core::failure(std::move(snapshot.error()));
            }
            const auto entity = std::find_if(
                storage.begin(), storage.end(), [stableEntityId](const auto& candidate) {
                    return candidate.stableEntityId == stableEntityId;
                });
            if (entity != storage.end()) {
                hasEntity = true;
                positionX = entity->positionX;
                positionY = entity->positionY;
                positionZ = entity->positionZ;
                rotationDegrees = eulerDegreesFromQuaternion(
                    entity->rotationX, entity->rotationY, entity->rotationZ,
                    entity->rotationW);
                scaleX = entity->scaleX;
                scaleY = entity->scaleY;
                scaleZ = entity->scaleZ;
            }
        } else {
            std::vector<Tina::AssetFormat::PrefabNodeView> storage;
            auto prefab = document3D_.parseCurrentPrefab(storage);
            if (!prefab) {
                return Tina::Core::failure(std::move(prefab.error()));
            }
            const auto node = std::find_if(
                storage.begin(), storage.end(), [stableEntityId](const auto& candidate) {
                    return candidate.stableNodeId == stableEntityId;
                });
            if (node != storage.end()) {
                hasEntity = true;
                positionX = node->positionX;
                positionY = node->positionY;
                positionZ = node->positionZ;
                rotationDegrees = eulerDegreesFromQuaternion(
                    node->rotationX, node->rotationY, node->rotationZ,
                    node->rotationW);
                scaleX = node->scaleX;
                scaleY = node->scaleY;
                scaleZ = node->scaleZ;
            }
        }
    }
    if (auto status = tree.setText(inspectorNote_, note); !status) {
        return status;
    }
    InspectorMixedTransformFlags mixed{};
    if (hasEntity && mixedSelection) {
        auto mixedResult = inspectorMixedTransformFlags(stableEntityId);
        if (!mixedResult) {
            return Tina::Core::failure(std::move(mixedResult.error()));
        }
        mixed = *mixedResult;
    }
    const auto setTransformField = [&](UI::UINodeId node,
                                       InspectorTransformField field,
                                       float value) -> Tina::Core::Status {
        if (!hasEntity) {
            return tree.setText(node, "n/a");
        }
        if (mixed[inspectorTransformFieldIndex(field)]) {
            return tree.setText(node, "Mixed");
        }
        auto text = formatEditorNumber(value);
        if (!text) {
            return Tina::Core::failure(std::move(text.error()));
        }
        return tree.setText(node, text->view());
    };
    const EditorHierarchyRow* selectedRow = hierarchyRow(stableEntityId);
    if (auto status = tree.setText(
            inspectorParentStableId_,
            hasEntity && selectedRow != nullptr
                ? std::to_string(selectedRow->parentStableId)
                : "n/a");
        !status) {
        return status;
    }
    if (auto status = setTransformField(
            inspectorPositionX_, InspectorTransformField::PositionX, positionX);
        !status) {
        return status;
    }
    if (auto status = setTransformField(
            inspectorPositionY_, InspectorTransformField::PositionY, positionY);
        !status) {
        return status;
    }
    if (auto status = setTransformField(
            inspectorPositionZ_, InspectorTransformField::PositionZ, positionZ);
        !status) {
        return status;
    }
    if (auto status = setTransformField(
            inspectorRotationX_, InspectorTransformField::RotationX,
            rotationDegrees.x);
        !status) {
        return status;
    }
    if (auto status = setTransformField(
            inspectorRotationY_, InspectorTransformField::RotationY,
            rotationDegrees.y);
        !status) {
        return status;
    }
    if (auto status = setTransformField(
            inspectorRotationZ_, InspectorTransformField::RotationZ,
            rotationDegrees.z);
        !status) {
        return status;
    }
    if (auto status = setTransformField(
            inspectorScaleX_, InspectorTransformField::ScaleX, scaleX);
        !status) {
        return status;
    }
    if (auto status = setTransformField(
            inspectorScaleY_, InspectorTransformField::ScaleY, scaleY);
        !status) {
        return status;
    }
    return setTransformField(inspectorScaleZ_, InspectorTransformField::ScaleZ,
                             scaleZ);
}

auto EditorWorkspaceState::inspectorDependencyDataSource() const noexcept -> UI::UIListViewDataSource{
    return UI::UIListViewDataSource{
        .state = this,
        .itemCount = &EditorWorkspaceState::inspectorDependencyItemCount,
        .resolveItem = &EditorWorkspaceState::resolveInspectorDependencyItem,
    };
}

auto EditorWorkspaceState::inspectorDependencyItemCount(const void* state) noexcept -> u64{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    return self != nullptr ? self->inspectorDependencyLabels_.size() : 0U;
}

auto EditorWorkspaceState::resolveInspectorDependencyItem(
    const void* state, u64 logicalIndex,
    UI::UIListViewItemDescriptor& output) noexcept -> bool{
    const auto* self = static_cast<const EditorWorkspaceState*>(state);
    if (self == nullptr || logicalIndex >= self->inspectorDependencyLabels_.size()) {
        return false;
    }
    output = UI::UIListViewItemDescriptor{
        .key = 20'000U + logicalIndex,
        .label = self->inspectorDependencyLabels_[
            static_cast<Tina::Core::usize>(logicalIndex)],
        .enabled = false,
    };
    return true;
}

} // namespace Tina::EditorApp::WorkspaceInternal
