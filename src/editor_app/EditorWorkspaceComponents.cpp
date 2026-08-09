#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {

auto EditorWorkspaceState::componentSelectionStableIds() const
 -> Tina::Core::Result<std::vector<u32>>
try{
    if (viewportSelectedEntityCount_ == 0U) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::EntityNotFound,
            "Component commands require a viewport scene selection");
    }
    std::vector<u32> ids;
    ids.reserve(viewportSelectedEntityCount_);
    for (Tina::Core::usize index = 0; index < viewportSelectedEntityCount_;
         ++index) {
        const u64 stableId = viewportSelectedEntityIds_[index];
        if (stableId == 0U || stableId > (std::numeric_limits<u32>::max)()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::EntityNotFound,
                "Component selection contains an invalid stable ID");
        }
        ids.push_back(static_cast<u32>(stableId));
    }
    return ids;
}
catch (const std::bad_alloc&)
{
    return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                               "Component selection staging allocation failed");
}

auto EditorWorkspaceState::selectedProjectAssetIdOfKind(Tina::AssetFormat::AssetKind kind) const noexcept -> Tina::Core::AssetId{
    const auto* asset = projectAssets_.selectedInspectorSnapshot();
    if (asset != nullptr && asset->assetKind == kind) {
        return asset->assetId;
    }
    return {};
}

auto EditorWorkspaceState::runComponentCommand(
    Tina::PrimaryWindowUITreeUpdater& tree, EditorCommand command,
    bool& published) -> Tina::Core::Status{
    published = false;
    const auto reject = [&](const Tina::Core::Error& error) -> Tina::Core::Status {
        try {
            authoringFeedback_ = "Component command rejected: ";
            authoringFeedback_ += error.message;
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Component rejection feedback allocation failed");
        }
        ++counters_.inspectorRejectedTransactions;
        return Tina::Core::success();
    };
    const auto isRejectable = [](const Tina::Core::Error& error) noexcept {
        return error.code == Tina::Editor::EditorErrorCode::InvalidAuthoringOperation ||
               error.code == Tina::Editor::EditorErrorCode::EntityNotFound ||
               error.code == Tina::Editor::EditorErrorCode::ComponentAlreadyPresent ||
               error.code == Tina::Editor::EditorErrorCode::ComponentNotFound ||
               error.code.domain == Tina::Core::ErrorDomain::Asset;
    };

    const bool meshCommand =
        command == EditorCommand::ComponentAddMeshRenderer ||
        command == EditorCommand::ComponentRemoveMeshRenderer ||
        command == EditorCommand::ComponentToggleMeshVisible;
    if (!authoringEnabled() || assetInspectorActive_ || !sceneDocumentActive() ||
        tileMapEditingContext()) {
        return reject(Tina::Core::Error{
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Component commands require an editable scene document"});
    }
    if (meshCommand != (workspaceMode_ == WorkspaceMode::World3D)) {
        return reject(Tina::Core::Error{
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Component command does not match the active workspace"});
    }

    auto idsResult = componentSelectionStableIds();
    if (!idsResult) {
        if (isRejectable(idsResult.error())) {
            return reject(idsResult.error());
        }
        return Tina::Core::failure(std::move(idsResult.error()));
    }
    const std::span<const u32> ids{*idsResult};

    const auto parseFloatField = [&](UI::UINodeId field, std::string_view fieldName)
        -> Tina::Core::Result<std::optional<float>> {
        auto text = tree.text(field);
        if (!text) {
            return Tina::Core::failure(std::move(text.error()));
        }
        if (*text == "n/a") {
            return std::optional<float>{};
        }
        return parseInspectorTransformValue(*text, fieldName);
    };
    const auto parseIntField = [&](UI::UINodeId field, std::string_view fieldName,
                                   long minimum, long maximum)
        -> Tina::Core::Result<std::optional<long>> {
        auto text = tree.text(field);
        if (!text) {
            return Tina::Core::failure(std::move(text.error()));
        }
        if (*text == "Mixed" || *text == "n/a") {
            return std::optional<long>{};
        }
        errno = 0;
        char* end = nullptr;
        const std::string buffer{*text};
        const long value = std::strtol(buffer.c_str(), &end, 10);
        if (errno != 0 || end == buffer.c_str() || *end != '\0' ||
            value < minimum || value > maximum) {
            try {
                std::string message{fieldName};
                message += " must be an integer or Mixed";
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    message);
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Component field validation message allocation failed");
            }
        }
        return std::optional<long>{value};
    };

    // The primary (first selected) entity anchors toggle semantics: the
    // toggled value is the primary's negation applied to every selection.
    const auto primaryWorld2DEntity = [&]()
        -> Tina::Core::Result<Tina::AssetFormat::World2DEntityDesc> {
        std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
        auto snapshot = document_.parseCurrentSnapshot(storage);
        if (!snapshot) {
            return Tina::Core::failure(std::move(snapshot.error()));
        }
        const auto entity = std::find_if(
            storage.begin(), storage.end(), [&](const auto& candidate) {
                return candidate.stableEntityId == ids.front();
            });
        if (entity == storage.end()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::EntityNotFound,
                "Component selection is absent from the World2D document");
        }
        return *entity;
    };

    using Tina::Editor::World2DComponentKind;
    Tina::Core::Result<Tina::Editor::EditorSceneOperationResult> result =
        Tina::Editor::EditorSceneOperationResult{};
    std::string_view successVerb{};
    std::string_view componentName{};

    switch (command) {
    case EditorCommand::ComponentAddSprite: {
        Tina::Core::AssetId spriteId = selectedProjectAssetIdOfKind(
            Tina::AssetFormat::AssetKind::Sprite);
        if (!spriteId) {
            spriteId = editorAssetId(0x22U);
        }
        result = Tina::Editor::addWorld2DComponent(
            document_, ids, World2DComponentKind::Sprite, spriteId);
        successVerb = "added";
        componentName = "SpriteRenderer2D";
        break;
    }
    case EditorCommand::ComponentAddCamera:
        result = Tina::Editor::addWorld2DComponent(
            document_, ids, World2DComponentKind::Camera);
        successVerb = "added";
        componentName = "Camera2D";
        break;
    case EditorCommand::ComponentAddPointLight:
        result = Tina::Editor::addWorld2DComponent(
            document_, ids, World2DComponentKind::PointLight);
        successVerb = "added";
        componentName = "PointLight2D";
        break;
    case EditorCommand::ComponentAddShadowOccluder:
        result = Tina::Editor::addWorld2DComponent(
            document_, ids, World2DComponentKind::ShadowOccluder);
        successVerb = "added";
        componentName = "ShadowOccluder2D";
        break;
    case EditorCommand::ComponentAddSpriteAnimation: {
        Tina::Core::AssetId clipId = selectedProjectAssetIdOfKind(
            Tina::AssetFormat::AssetKind::SpriteAnimationClip);
        if (!clipId) {
            clipId = editorAssetId(0x10U);
        }
        result = Tina::Editor::addWorld2DComponent(
            document_, ids, World2DComponentKind::SpriteAnimation, clipId);
        successVerb = "added";
        componentName = "SpriteAnimation2D";
        break;
    }
    case EditorCommand::ComponentAddMeshRenderer: {
        Tina::Core::AssetId meshId = selectedProjectAssetIdOfKind(
            Tina::AssetFormat::AssetKind::StaticMesh);
        Tina::Core::AssetId materialId = selectedProjectAssetIdOfKind(
            Tina::AssetFormat::AssetKind::Material);
        if (!meshId) {
            meshId = editorAssetId(0x31U);
        }
        if (!materialId) {
            materialId = editorAssetId(0x32U);
        }
        result = Tina::Editor::addWorld3DMeshRenderer(document3D_, ids,
                                                      meshId, materialId);
        successVerb = "added";
        componentName = "MeshRenderer3D";
        break;
    }
    case EditorCommand::ComponentRemoveSprite:
        result = Tina::Editor::removeWorld2DComponent(
            document_, ids, World2DComponentKind::Sprite);
        successVerb = "removed";
        componentName = "SpriteRenderer2D";
        break;
    case EditorCommand::ComponentRemoveCamera:
        result = Tina::Editor::removeWorld2DComponent(
            document_, ids, World2DComponentKind::Camera);
        successVerb = "removed";
        componentName = "Camera2D";
        break;
    case EditorCommand::ComponentRemovePointLight:
        result = Tina::Editor::removeWorld2DComponent(
            document_, ids, World2DComponentKind::PointLight);
        successVerb = "removed";
        componentName = "PointLight2D";
        break;
    case EditorCommand::ComponentRemoveShadowOccluder:
        result = Tina::Editor::removeWorld2DComponent(
            document_, ids, World2DComponentKind::ShadowOccluder);
        successVerb = "removed";
        componentName = "ShadowOccluder2D";
        break;
    case EditorCommand::ComponentRemoveSpriteAnimation:
        result = Tina::Editor::removeWorld2DComponent(
            document_, ids, World2DComponentKind::SpriteAnimation);
        successVerb = "removed";
        componentName = "SpriteAnimation2D";
        break;
    case EditorCommand::ComponentRemoveMeshRenderer:
        result = Tina::Editor::removeWorld3DMeshRenderer(document3D_, ids);
        successVerb = "removed";
        componentName = "MeshRenderer3D";
        break;
    case EditorCommand::ComponentToggleSpriteVisible: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->sprite) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::ComponentNotFound,
                "Toggle requires a SpriteRenderer2D on the primary selection");
            break;
        }
        result = Tina::Editor::applyWorld2DSpriteEdit(
            document_, ids, {.visible = !primary->sprite->visible});
        successVerb = "toggled";
        componentName = "SpriteRenderer2D visibility";
        break;
    }
    case EditorCommand::ComponentToggleCameraActive: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->camera) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::ComponentNotFound,
                "Toggle requires a Camera2D on the primary selection");
            break;
        }
        result = Tina::Editor::applyWorld2DCameraEdit(
            document_, ids, {.active = !primary->camera->active});
        successVerb = "toggled";
        componentName = "Camera2D active";
        break;
    }
    case EditorCommand::ComponentTogglePointLightActive: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->pointLight) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::ComponentNotFound,
                "Toggle requires a PointLight2D on the primary selection");
            break;
        }
        result = Tina::Editor::applyWorld2DPointLightEdit(
            document_, ids, {.active = !primary->pointLight->active});
        successVerb = "toggled";
        componentName = "PointLight2D active";
        break;
    }
    case EditorCommand::ComponentToggleShadowOccluderActive: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->shadowOccluder) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::ComponentNotFound,
                "Toggle requires a ShadowOccluder2D on the primary selection");
            break;
        }
        result = Tina::Editor::applyWorld2DShadowOccluderEdit(
            document_, ids, {.active = !primary->shadowOccluder->active});
        successVerb = "toggled";
        componentName = "ShadowOccluder2D active";
        break;
    }
    case EditorCommand::ComponentToggleSpriteAnimationAutoPlay: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->spriteAnimation) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::ComponentNotFound,
                "Toggle requires a SpriteAnimation2D on the primary selection");
            break;
        }
        result = Tina::Editor::applyWorld2DSpriteAnimationEdit(
            document_, ids, {.autoPlay = !primary->spriteAnimation->autoPlay});
        successVerb = "toggled";
        componentName = "SpriteAnimation2D autoPlay";
        break;
    }
    case EditorCommand::ComponentApplySpriteAnimation: {
        const auto& section = componentSections_[4];
        auto clipText = tree.text(section.fields[0]);
        if (!clipText) {
            return Tina::Core::failure(std::move(clipText.error()));
        }
        Tina::Editor::World2DSpriteAnimationEditInput input{};
        if (*clipText != "Mixed" && *clipText != "n/a") {
            const auto parsedClip =
                Tina::Core::AssetId::parseCanonical(*clipText);
            if (!parsedClip.has_value()) {
                return reject(Tina::Core::Error{
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Clip must be a 32-hex canonical AssetId or Mixed"});
            }
            input.clipId = *parsedClip;
        }
        auto speed = parseFloatField(section.fields[1], "Speed");
        if (!speed) {
            if (isRejectable(speed.error())) {
                return reject(speed.error());
            }
            return Tina::Core::failure(std::move(speed.error()));
        }
        input.playbackSpeed = *speed;
        result = Tina::Editor::applyWorld2DSpriteAnimationEdit(document_, ids,
                                                               input);
        successVerb = "applied";
        componentName = "SpriteAnimation2D";
        break;
    }
    case EditorCommand::ComponentToggleMeshVisible: {
        std::vector<Tina::AssetFormat::PrefabNodeView> storage;
        auto prefab = document3D_.parseCurrentPrefab(storage);
        if (!prefab) {
            return Tina::Core::failure(std::move(prefab.error()));
        }
        const auto node = std::find_if(
            storage.begin(), storage.end(), [&](const auto& candidate) {
                return candidate.stableNodeId == ids.front();
            });
        if (node == storage.end() ||
            !Tina::Editor::hasWorld3DMeshRenderer(*node)) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::ComponentNotFound,
                "Toggle requires a MeshRenderer3D on the primary selection");
            break;
        }
        result = Tina::Editor::applyWorld3DMeshRendererEdit(
            document3D_, ids, {.visible = !node->visible});
        successVerb = "toggled";
        componentName = "MeshRenderer3D visibility";
        break;
    }
    case EditorCommand::ComponentAssignSprite: {
        const Tina::Core::AssetId spriteId = selectedProjectAssetIdOfKind(
            Tina::AssetFormat::AssetKind::Sprite);
        if (!spriteId) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Assign requires a Sprite asset selected in Project Assets");
            break;
        }
        result = Tina::Editor::applyWorld2DSpriteEdit(document_, ids,
                                                      {.spriteId = spriteId});
        successVerb = "assigned";
        componentName = "SpriteRenderer2D sprite";
        break;
    }
    case EditorCommand::ComponentApplySprite: {
        const auto& section = componentSections_[0];
        Tina::Editor::World2DSpriteEditInput input{};
        auto sizeX = parseFloatField(section.fields[0], "Size X");
        auto sizeY = parseFloatField(section.fields[1], "Size Y");
        auto pivotX = parseFloatField(section.fields[2], "Pivot X");
        auto pivotY = parseFloatField(section.fields[3], "Pivot Y");
        auto sortingLayer = sizeX && sizeY && pivotX && pivotY
            ? parseIntField(section.fields[4], "Sort Layer", -32768L, 32767L)
            : Tina::Core::Result<std::optional<long>>{std::optional<long>{}};
        auto orderInLayer = sortingLayer
            ? parseIntField(section.fields[5], "Order", -2147483647L, 2147483647L)
            : Tina::Core::Result<std::optional<long>>{std::optional<long>{}};
        if (!sizeX || !sizeY || !pivotX || !pivotY || !sortingLayer ||
            !orderInLayer) {
            const Tina::Core::Error error =
                !sizeX ? sizeX.error() : !sizeY ? sizeY.error()
                : !pivotX ? pivotX.error() : !pivotY ? pivotY.error()
                : !sortingLayer ? sortingLayer.error() : orderInLayer.error();
            if (isRejectable(error)) {
                return reject(error);
            }
            return Tina::Core::failure(error);
        }
        input.sizeX = *sizeX;
        input.sizeY = *sizeY;
        input.pivotX = *pivotX;
        input.pivotY = *pivotY;
        if (sortingLayer->has_value()) {
            input.sortingLayer = static_cast<Tina::Core::i16>(**sortingLayer);
        }
        if (orderInLayer->has_value()) {
            input.orderInLayer = static_cast<Tina::Core::i32>(**orderInLayer);
        }
        result = Tina::Editor::applyWorld2DSpriteEdit(document_, ids, input);
        successVerb = "applied";
        componentName = "SpriteRenderer2D";
        break;
    }
    case EditorCommand::ComponentApplyCamera: {
        const auto& section = componentSections_[1];
        auto height = parseFloatField(section.fields[0], "Height m");
        auto pixelsPerMeter = parseFloatField(section.fields[1], "Ref Px/m");
        auto heightPixels = height && pixelsPerMeter
            ? parseIntField(section.fields[2], "Ref Px H", 1L, 16384L)
            : Tina::Core::Result<std::optional<long>>{std::optional<long>{}};
        if (!height || !pixelsPerMeter || !heightPixels) {
            const Tina::Core::Error error = !height ? height.error()
                : !pixelsPerMeter ? pixelsPerMeter.error()
                                  : heightPixels.error();
            if (isRejectable(error)) {
                return reject(error);
            }
            return Tina::Core::failure(error);
        }
        Tina::Editor::World2DCameraEditInput input{};
        input.fixedWorldHeightMeters = *height;
        input.referencePixelsPerMeter = *pixelsPerMeter;
        if (heightPixels->has_value()) {
            input.referenceHeightPixels = static_cast<u32>(**heightPixels);
        }
        result = Tina::Editor::applyWorld2DCameraEdit(document_, ids, input);
        successVerb = "applied";
        componentName = "Camera2D";
        break;
    }
    case EditorCommand::ComponentApplyPointLight: {
        const auto& section = componentSections_[2];
        Tina::Editor::World2DPointLightEditInput input{};
        const std::array<std::pair<UI::UINodeId, std::optional<float>*>, 6>
            fieldBindings{{
                {section.fields[0], &input.colorRed},
                {section.fields[1], &input.colorGreen},
                {section.fields[2], &input.colorBlue},
                {section.fields[3], &input.intensity},
                {section.fields[4], &input.radiusMeters},
                {section.fields[5], &input.sourceRadiusMeters},
            }};
        const std::array<std::string_view, 6> fieldNames{
            "Color R", "Color G", "Color B", "Intensity", "Radius",
            "Src Radius"};
        Tina::Core::Status parseStatus = Tina::Core::success();
        for (Tina::Core::usize index = 0; index < fieldBindings.size();
             ++index) {
            auto parsed = parseFloatField(fieldBindings[index].first,
                                          fieldNames[index]);
            if (!parsed) {
                parseStatus = Tina::Core::failure(std::move(parsed.error()));
                break;
            }
            *fieldBindings[index].second = *parsed;
        }
        if (!parseStatus) {
            if (isRejectable(parseStatus.error())) {
                return reject(parseStatus.error());
            }
            return parseStatus;
        }
        result = Tina::Editor::applyWorld2DPointLightEdit(document_, ids, input);
        successVerb = "applied";
        componentName = "PointLight2D";
        break;
    }
    case EditorCommand::ComponentApplyShadowOccluder: {
        const auto& section = componentSections_[3];
        auto startX = parseFloatField(section.fields[0], "Start X");
        auto startY = parseFloatField(section.fields[1], "Start Y");
        auto endX = parseFloatField(section.fields[2], "End X");
        auto endY = parseFloatField(section.fields[3], "End Y");
        if (!startX || !startY || !endX || !endY) {
            const Tina::Core::Error error = !startX ? startX.error()
                : !startY ? startY.error()
                : !endX ? endX.error() : endY.error();
            if (isRejectable(error)) {
                return reject(error);
            }
            return Tina::Core::failure(error);
        }
        result = Tina::Editor::applyWorld2DShadowOccluderEdit(
            document_, ids,
            {.localStartX = *startX, .localStartY = *startY,
             .localEndX = *endX, .localEndY = *endY});
        successVerb = "applied";
        componentName = "ShadowOccluder2D";
        break;
    }
    default:
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::InvalidArgument,
            "Unknown Inspector component command");
    }

    if (!result) {
        if (isRejectable(result.error())) {
            return reject(result.error());
        }
        return Tina::Core::failure(std::move(result.error()));
    }
    try {
        authoringFeedback_ = "Component ";
        authoringFeedback_ += componentName;
        authoringFeedback_ += ' ';
        authoringFeedback_ += successVerb;
        if (result->affectedItemCount == 0U) {
            authoringFeedback_ =
                "Component values unchanged; no document revision published";
        } else {
            authoringFeedback_ += " on ";
            authoringFeedback_ += std::to_string(result->affectedItemCount);
            authoringFeedback_ +=
                result->affectedItemCount == 1U
                    ? " entity as one document revision"
                    : " entities as one document revision";
        }
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Component feedback allocation failed");
    }
    if (result->affectedItemCount > 0U) {
        published = true;
        ++counters_.authoringEdits;
        ++counters_.inspectorTransactions;
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::refreshComponentSectionsUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    const u32 primaryId = stableEntityIdForHierarchyItem(selectionKey_);
    const bool selectionEditable = authoringEnabled() && !assetInspectorActive_ &&
                                   primaryId != 0U && !tileMapEditingContext() &&
                                   sceneDocumentActive();
    const bool world2D = workspaceMode_ == WorkspaceMode::World2D;

    const auto resetSection = [&](const ComponentSectionUi& section)
        -> Tina::Core::Status {
        if (auto status = tree.setEnabled(section.activeCheckbox, false); !status) {
            return status;
        }
        if (auto status = tree.setChecked(section.activeCheckbox, false); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(section.addButton, false); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(section.removeButton, false); !status) {
            return status;
        }
        for (Tina::Core::usize index = 0; index < section.fieldCount; ++index) {
            if (auto status = tree.setText(section.fields[index], "n/a"); !status) {
                return status;
            }
            if (auto status = tree.setEnabled(section.fields[index], false); !status) {
                return status;
            }
        }
        if (section.assignButton.hasValue()) {
            if (auto status = tree.setEnabled(section.assignButton, false); !status) {
                return status;
            }
        }
        if (section.applyButton.hasValue()) {
            if (auto status = tree.setEnabled(section.applyButton, false); !status) {
                return status;
            }
        }
        return Tina::Core::success();
    };

    // A field publishes the primary value, or "Mixed" when any other
    // selected entity that has the component disagrees.
    const auto fieldText = [](bool mixedValue, std::string value) {
        return mixedValue ? std::string{"Mixed"} : std::move(value);
    };

    if (world2D) {
        if (auto status = resetSection(
                componentSections_[MeshRendererSectionIndex]);
            !status) {
            return status;
        }
        std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
        auto snapshot = document_.parseCurrentSnapshot(storage);
        if (!snapshot) {
            return Tina::Core::failure(std::move(snapshot.error()));
        }
        const auto findEntity = [&](u32 stableId)
            -> const Tina::AssetFormat::World2DEntityDesc* {
            const auto found = std::find_if(
                storage.begin(), storage.end(), [stableId](const auto& entity) {
                    return entity.stableEntityId == stableId;
                });
            return found == storage.end() ? nullptr : &*found;
        };
        const auto* primary = primaryId != 0U ? findEntity(primaryId) : nullptr;
        std::vector<const Tina::AssetFormat::World2DEntityDesc*> selected;
        try {
            if (viewportSelectedEntityCount_ > 0U) {
                selected.reserve(viewportSelectedEntityCount_);
                for (Tina::Core::usize index = 0;
                     index < viewportSelectedEntityCount_; ++index) {
                    const u64 stableId = viewportSelectedEntityIds_[index];
                    if (stableId == 0U ||
                        stableId > (std::numeric_limits<u32>::max)()) {
                        continue;
                    }
                    const auto* entity = findEntity(static_cast<u32>(stableId));
                    if (entity != nullptr) {
                        selected.push_back(entity);
                    }
                }
            } else if (primary != nullptr) {
                selected.push_back(primary);
            }
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Component section staging allocation failed");
        }
        if (primary == nullptr && !selected.empty()) {
            primary = selected.front();
        }

        for (Tina::Core::usize sectionIndex = 0; sectionIndex < 5U;
             ++sectionIndex) {
            const auto& section = componentSections_[sectionIndex];
            const auto kind = static_cast<Tina::Editor::World2DComponentKind>(
                sectionIndex);
            if (primary == nullptr || selected.empty()) {
                if (auto status = resetSection(section); !status) {
                    return status;
                }
                continue;
            }
            bool anyHas = false;
            bool anyLacks = false;
            for (const auto* entity : selected) {
                if (Tina::Editor::hasWorld2DComponent(*entity, kind)) {
                    anyHas = true;
                } else {
                    anyLacks = true;
                }
            }
            const bool primaryHas =
                Tina::Editor::hasWorld2DComponent(*primary, kind);
            if (auto status = tree.setEnabled(section.addButton,
                                              selectionEditable && anyLacks);
                !status) {
                return status;
            }
            if (auto status = tree.setEnabled(section.removeButton,
                                              selectionEditable && anyHas);
                !status) {
                return status;
            }
            if (auto status = tree.setEnabled(section.activeCheckbox,
                                              selectionEditable && primaryHas);
                !status) {
                return status;
            }
            const bool fieldsEditable = selectionEditable && primaryHas;
            for (Tina::Core::usize index = 0; index < section.fieldCount;
                 ++index) {
                if (auto status = tree.setEnabled(section.fields[index],
                                                  fieldsEditable);
                    !status) {
                    return status;
                }
            }
            if (section.assignButton.hasValue()) {
                if (auto status = tree.setEnabled(section.assignButton,
                                                  fieldsEditable);
                    !status) {
                    return status;
                }
            }
            if (section.applyButton.hasValue()) {
                if (auto status = tree.setEnabled(section.applyButton,
                                                  fieldsEditable);
                    !status) {
                    return status;
                }
            }
            if (!primaryHas) {
                if (auto status = tree.setChecked(section.activeCheckbox, false);
                    !status) {
                    return status;
                }
                for (Tina::Core::usize index = 0; index < section.fieldCount;
                     ++index) {
                    if (auto status = tree.setText(section.fields[index], "n/a");
                        !status) {
                        return status;
                    }
                }
                continue;
            }

            const auto mixedFloat = [&](auto&& accessor) {
                const float primaryValue = accessor(*primary);
                for (const auto* entity : selected) {
                    if (!Tina::Editor::hasWorld2DComponent(*entity, kind)) {
                        continue;
                    }
                    if (std::abs(accessor(*entity) - primaryValue) > 1.0e-4F) {
                        return true;
                    }
                }
                return false;
            };
            const auto mixedBool = [&](auto&& accessor) {
                const bool primaryValue = accessor(*primary);
                for (const auto* entity : selected) {
                    if (!Tina::Editor::hasWorld2DComponent(*entity, kind)) {
                        continue;
                    }
                    if (accessor(*entity) != primaryValue) {
                        return true;
                    }
                }
                return false;
            };
            const auto setField = [&](Tina::Core::usize index, bool mixedValue,
                                      std::string value) -> Tina::Core::Status {
                return tree.setText(section.fields[index],
                                    fieldText(mixedValue, std::move(value)));
            };

            Tina::Core::Status status = Tina::Core::success();
            switch (kind) {
            case Tina::Editor::World2DComponentKind::Sprite: {
                const auto& sprite = *primary->sprite;
                status = tree.setChecked(
                    section.activeCheckbox,
                    sprite.visible &&
                        !mixedBool([](const auto& e) { return e.sprite->visible; }));
                if (status) {
                    status = setField(
                        0,
                        mixedFloat([](const auto& e) { return e.sprite->sizeX; }),
                        std::to_string(sprite.sizeX));
                }
                if (status) {
                    status = setField(
                        1,
                        mixedFloat([](const auto& e) { return e.sprite->sizeY; }),
                        std::to_string(sprite.sizeY));
                }
                if (status) {
                    status = setField(
                        2,
                        mixedFloat([](const auto& e) { return e.sprite->pivotX; }),
                        std::to_string(sprite.pivotX));
                }
                if (status) {
                    status = setField(
                        3,
                        mixedFloat([](const auto& e) { return e.sprite->pivotY; }),
                        std::to_string(sprite.pivotY));
                }
                if (status) {
                    status = setField(
                        4,
                        mixedFloat([](const auto& e) {
                            return static_cast<float>(e.sprite->sortingLayer);
                        }),
                        std::to_string(sprite.sortingLayer));
                }
                if (status) {
                    status = setField(
                        5,
                        mixedFloat([](const auto& e) {
                            return static_cast<float>(e.sprite->orderInLayer);
                        }),
                        std::to_string(sprite.orderInLayer));
                }
                break;
            }
            case Tina::Editor::World2DComponentKind::Camera: {
                const auto& camera = *primary->camera;
                status = tree.setChecked(
                    section.activeCheckbox,
                    camera.active &&
                        !mixedBool([](const auto& e) { return e.camera->active; }));
                if (status) {
                    status = setField(
                        0,
                        mixedFloat([](const auto& e) {
                            return e.camera->fixedWorldHeightMeters;
                        }),
                        std::to_string(camera.fixedWorldHeightMeters));
                }
                if (status) {
                    status = setField(
                        1,
                        mixedFloat([](const auto& e) {
                            return e.camera->referencePixelsPerMeter;
                        }),
                        std::to_string(camera.referencePixelsPerMeter));
                }
                if (status) {
                    status = setField(
                        2,
                        mixedFloat([](const auto& e) {
                            return static_cast<float>(
                                e.camera->referenceHeightPixels);
                        }),
                        std::to_string(camera.referenceHeightPixels));
                }
                break;
            }
            case Tina::Editor::World2DComponentKind::PointLight: {
                const auto& light = *primary->pointLight;
                status = tree.setChecked(
                    section.activeCheckbox,
                    light.active &&
                        !mixedBool([](const auto& e) {
                            return e.pointLight->active;
                        }));
                const std::array<std::pair<float, bool>, 6> values{{
                    {light.colorRed, mixedFloat([](const auto& e) {
                         return e.pointLight->colorRed;
                     })},
                    {light.colorGreen, mixedFloat([](const auto& e) {
                         return e.pointLight->colorGreen;
                     })},
                    {light.colorBlue, mixedFloat([](const auto& e) {
                         return e.pointLight->colorBlue;
                     })},
                    {light.intensity, mixedFloat([](const auto& e) {
                         return e.pointLight->intensity;
                     })},
                    {light.radiusMeters, mixedFloat([](const auto& e) {
                         return e.pointLight->radiusMeters;
                     })},
                    {light.sourceRadiusMeters, mixedFloat([](const auto& e) {
                         return e.pointLight->sourceRadiusMeters;
                     })},
                }};
                for (Tina::Core::usize index = 0;
                     status && index < values.size(); ++index) {
                    status = setField(index, values[index].second,
                                      std::to_string(values[index].first));
                }
                break;
            }
            case Tina::Editor::World2DComponentKind::ShadowOccluder: {
                const auto& occluder = *primary->shadowOccluder;
                status = tree.setChecked(
                    section.activeCheckbox,
                    occluder.active &&
                        !mixedBool([](const auto& e) {
                            return e.shadowOccluder->active;
                        }));
                const std::array<std::pair<float, bool>, 4> values{{
                    {occluder.localStartX, mixedFloat([](const auto& e) {
                         return e.shadowOccluder->localStartX;
                     })},
                    {occluder.localStartY, mixedFloat([](const auto& e) {
                         return e.shadowOccluder->localStartY;
                     })},
                    {occluder.localEndX, mixedFloat([](const auto& e) {
                         return e.shadowOccluder->localEndX;
                     })},
                    {occluder.localEndY, mixedFloat([](const auto& e) {
                         return e.shadowOccluder->localEndY;
                     })},
                }};
                for (Tina::Core::usize index = 0;
                     status && index < values.size(); ++index) {
                    status = setField(index, values[index].second,
                                      std::to_string(values[index].first));
                }
                break;
            }
            case Tina::Editor::World2DComponentKind::SpriteAnimation: {
                const auto& animation = *primary->spriteAnimation;
                status = tree.setChecked(
                    section.activeCheckbox,
                    animation.autoPlay &&
                        !mixedBool([](const auto& e) {
                            return e.spriteAnimation->autoPlay;
                        }));
                if (status) {
                    bool clipMixed = false;
                    for (const auto* entity : selected) {
                        if (Tina::Editor::hasWorld2DComponent(*entity, kind) &&
                            entity->spriteAnimation->clipId != animation.clipId) {
                            clipMixed = true;
                            break;
                        }
                    }
                    const auto clipText = animation.clipId.canonicalText();
                    status = setField(
                        0, clipMixed,
                        std::string{clipText.data(), clipText.size()});
                }
                if (status) {
                    status = setField(
                        1,
                        mixedFloat([](const auto& e) {
                            return e.spriteAnimation->playbackSpeed;
                        }),
                        std::to_string(animation.playbackSpeed));
                }
                break;
            }
            }
            if (!status) {
                return status;
            }
        }
        return Tina::Core::success();
    }

    for (Tina::Core::usize sectionIndex = 0; sectionIndex < 5U; ++sectionIndex) {
        if (auto status = resetSection(componentSections_[sectionIndex]);
            !status) {
            return status;
        }
    }
    const auto& section = componentSections_[MeshRendererSectionIndex];
    std::vector<Tina::AssetFormat::PrefabNodeView> storage;
    auto prefab = document3D_.parseCurrentPrefab(storage);
    if (!prefab) {
        return Tina::Core::failure(std::move(prefab.error()));
    }
    const auto primaryNode = std::find_if(
        storage.begin(), storage.end(), [primaryId](const auto& candidate) {
            return candidate.stableNodeId == primaryId;
        });
    if (primaryId == 0U || primaryNode == storage.end()) {
        return resetSection(section);
    }
    const bool primaryHas = Tina::Editor::hasWorld3DMeshRenderer(*primaryNode);
    if (auto status = tree.setEnabled(section.addButton,
                                      selectionEditable && !primaryHas);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(section.removeButton,
                                      selectionEditable && primaryHas);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(section.activeCheckbox,
                                      selectionEditable && primaryHas);
        !status) {
        return status;
    }
    if (auto status = tree.setChecked(section.activeCheckbox,
                                      primaryHas && primaryNode->visible);
        !status) {
        return status;
    }
    for (Tina::Core::usize index = 0; index < section.fieldCount; ++index) {
        if (auto status = tree.setEnabled(section.fields[index], false);
            !status) {
            return status;
        }
    }
    const auto assetIdPrefix = [](Tina::Core::AssetId assetId) -> std::string {
        if (!assetId) {
            return "n/a";
        }
        const auto text = assetId.canonicalText();
        return std::string{text.data(), 8U};
    };
    if (auto status = tree.setText(section.fields[0],
                                   assetIdPrefix(primaryNode->meshId));
        !status) {
        return status;
    }
    return tree.setText(section.fields[1],
                        assetIdPrefix(primaryNode->materialId));
}

} // namespace Tina::EditorApp::WorkspaceInternal
