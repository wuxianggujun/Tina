#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

enum class World2DNodePropertyGroup : Tina::Core::u8 {
    Rendering = 0,
    Camera = 1,
    Light = 2,
    Occlusion = 3,
    Animation = 4,
    PhysicsBody = 5,
    PhysicsShape = 6,
    Resource = 7,
};

} // namespace

auto EditorWorkspaceState::nodePropertySelectionStableIds() const
 -> Tina::Core::Result<std::vector<u32>>
try{
    if (viewportSelectedEntityCount_ == 0U) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::EntityNotFound,
            "Node property commands require a viewport scene selection");
    }
    std::vector<u32> ids;
    ids.reserve(viewportSelectedEntityCount_);
    for (Tina::Core::usize index = 0; index < viewportSelectedEntityCount_;
         ++index) {
        const u64 stableId = viewportSelectedEntityIds_[index];
        if (stableId == 0U || stableId > (std::numeric_limits<u32>::max)()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::EntityNotFound,
                "Node property selection contains an invalid stable ID");
        }
        ids.push_back(static_cast<u32>(stableId));
    }
    return ids;
}
catch (const std::bad_alloc&)
{
    return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                               "Node property selection staging allocation failed");
}

auto EditorWorkspaceState::selectedProjectAssetIdOfKind(Tina::AssetFormat::AssetKind kind) const noexcept -> Tina::Core::AssetId{
    const auto* asset = projectAssets_.selectedInspectorSnapshot();
    if (asset != nullptr && asset->assetKind == kind) {
        return asset->assetId;
    }
    return {};
}

auto EditorWorkspaceState::selectedProjectSpriteAssetId() const noexcept
    -> Tina::Core::AssetId
{
    const auto* asset = projectAssets_.selectedInspectorSnapshot();
    if (asset == nullptr) {
        return {};
    }
    if (asset->assetKind == Tina::AssetFormat::AssetKind::Sprite) {
        return asset->assetId;
    }
    if (asset->assetKind != Tina::AssetFormat::AssetKind::Texture2D) {
        return {};
    }
    return asset->assetId;
}

auto EditorWorkspaceState::projectAssetExists(
    Tina::Core::AssetId assetId) const noexcept -> bool{
    return assetId && projectAssets_.inspectorSnapshot(assetId) != nullptr;
}

auto EditorWorkspaceState::resolvedNodeTemplateSpriteAssetId(
    u8 fallbackMarker) const noexcept -> Tina::Core::AssetId{
    const Tina::Core::AssetId selected = selectedProjectSpriteAssetId();
    if (selected) {
        return selected;
    }
    const Tina::Core::AssetId fallback = editorAssetId(fallbackMarker);
    return projectAssetExists(fallback) ? fallback : Tina::Core::AssetId{};
}

auto EditorWorkspaceState::resolvedNodeTemplateAssetId(
    Tina::AssetFormat::AssetKind kind, u8 fallbackMarker) const noexcept
    -> Tina::Core::AssetId{
    const Tina::Core::AssetId selected = selectedProjectAssetIdOfKind(kind);
    if (selected) {
        return selected;
    }
    const Tina::Core::AssetId fallback = editorAssetId(fallbackMarker);
    return projectAssetExists(fallback) ? fallback : Tina::Core::AssetId{};
}

auto EditorWorkspaceState::resolveNodeTemplateAssets(
    Tina::Core::usize slot, bool world2D) const noexcept
    -> Tina::Core::Result<NodeTemplateAssetResolution>{
    NodeTemplateAssetResolution resolution{};
    if (!world2D) {
        const auto registry = Tina::Editor::world3DNodeTemplateRegistry();
        if (slot >= registry.size()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Editor scene node template is unknown");
        }
        const auto& info = registry[slot];
        if (info.requiredMeshAssetKind ==
            Tina::AssetFormat::AssetKind::Invalid) {
            return resolution;
        }
        resolution.world3D.meshId = resolvedNodeTemplateAssetId(
            info.requiredMeshAssetKind,
            info.requiredMeshAssetKind ==
                    Tina::AssetFormat::AssetKind::SkinnedMesh
                ? 0x34U
                : 0x31U);
        resolution.world3D.materialId = resolvedNodeTemplateAssetId(
            Tina::AssetFormat::AssetKind::Material, 0x32U);
        if (!resolution.world3D.meshId) {
            resolution.missingAssetKindName =
                Tina::Editor::projectAssetKindLabel(info.requiredMeshAssetKind);
        } else if (!resolution.world3D.materialId) {
            resolution.missingAssetKindName =
                Tina::Editor::projectAssetKindLabel(
                    Tina::AssetFormat::AssetKind::Material);
        }
        return resolution;
    }

    const auto registry = Tina::Editor::world2DNodeTemplateRegistry();
    if (slot >= registry.size()) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Editor scene node template is unknown");
    }
    const auto& info = registry[slot];
    if (info.requiresSpriteAsset) {
        resolution.world2D.spriteId = resolvedNodeTemplateSpriteAssetId(0x22U);
        if (!resolution.world2D.spriteId) {
            resolution.missingAssetKindName = "Sprite or Texture2D";
            return resolution;
        }
    }
    if (info.requiresAnimationClipAsset) {
        resolution.world2D.animationClipId = resolvedNodeTemplateAssetId(
            Tina::AssetFormat::AssetKind::SpriteAnimationClip, 0x10U);
        if (!resolution.world2D.animationClipId) {
            resolution.missingAssetKindName =
                Tina::Editor::projectAssetKindLabel(
                    Tina::AssetFormat::AssetKind::SpriteAnimationClip);
            return resolution;
        }
    }
    if (info.requiredResourceAssetKind ==
        Tina::AssetFormat::AssetKind::Invalid) {
        return resolution;
    }
    u8 fallbackMarker = 0U;
    switch (info.requiredResourceAssetKind) {
    case Tina::AssetFormat::AssetKind::TileMap:
        fallbackMarker = 0x42U;
        break;
    case Tina::AssetFormat::AssetKind::NavigationGrid2D:
        fallbackMarker = 0x61U;
        break;
    case Tina::AssetFormat::AssetKind::Fx2D:
        fallbackMarker = 0x62U;
        break;
    case Tina::AssetFormat::AssetKind::AudioClip:
        fallbackMarker = 0x63U;
        break;
    default:
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Create Node resource kind has no preview fallback");
    }
    resolution.world2D.resourceId = resolvedNodeTemplateAssetId(
        info.requiredResourceAssetKind, fallbackMarker);
    if (!resolution.world2D.resourceId) {
        resolution.missingAssetKindName = Tina::Editor::projectAssetKindLabel(
            info.requiredResourceAssetKind);
    }
    return resolution;
}

auto EditorWorkspaceState::inspectorFieldCommitCommand(
    UI::UINodeId field) const noexcept -> std::optional<EditorCommand>{
    if (!field.hasValue()) {
        return std::nullopt;
    }
    const std::array<UI::UINodeId, 10> transformFields{
        inspectorPositionX_, inspectorPositionY_, inspectorPositionZ_,
        inspectorRotationX_, inspectorRotationY_, inspectorRotationZ_,
        inspectorScaleX_,    inspectorScaleY_,    inspectorScaleZ_,
        inspectorParentStableId_};
    for (Tina::Core::usize index = 0; index < transformFields.size(); ++index) {
        if (transformFields[index] == field) {
            return index + 1U == transformFields.size()
                       ? EditorCommand::SceneReparent
                       : EditorCommand::ApplyTransform;
        }
    }
    if (field == pointLightColorField_.textEdit) {
        return EditorCommand::NodeApplyPointLight;
    }
    if (field == spriteColorField_.textEdit) {
        return EditorCommand::NodeApplySprite;
    }
    const std::array<EditorCommand, 7> sectionCommands{
        EditorCommand::NodeApplySprite,
        EditorCommand::NodeApplyCamera,
        EditorCommand::NodeApplyPointLight,
        EditorCommand::NodeApplyShadowOccluder,
        EditorCommand::NodeApplyAnimationProperties,
        EditorCommand::NodeApplyPhysicsBody,
        EditorCommand::NodeApplyPhysicsShape};
    for (Tina::Core::usize sectionIndex = 0;
         sectionIndex < sectionCommands.size(); ++sectionIndex) {
        const auto& section = nodePropertySections_[sectionIndex];
        for (Tina::Core::usize index = 0; index < section.fieldCount; ++index) {
            if (section.fields[index] == field) {
                return sectionCommands[sectionIndex];
            }
        }
    }
    return std::nullopt;
}

auto EditorWorkspaceState::requiredResourceAssetKind(
    Tina::Editor::World2DNodeTemplate nodeTemplate) noexcept
    -> Tina::AssetFormat::AssetKind{
    const auto registry = Tina::Editor::world2DNodeTemplateRegistry();
    const auto slot = static_cast<Tina::Core::usize>(nodeTemplate);
    return slot < registry.size() ? registry[slot].requiredResourceAssetKind
                                  : Tina::AssetFormat::AssetKind::Invalid;
}

auto EditorWorkspaceState::inspectorFieldIsBeingEdited(
    UI::UINodeId field) const noexcept -> bool{
    return field.hasValue() && inspectorFieldEdit_.field == field;
}

auto EditorWorkspaceState::processInspectorFieldCommit(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    auto focus = tree.focusedNode();
    if (!focus) {
        return Tina::Core::failure(std::move(focus.error()));
    }
    const UI::UINodeId focused = *focus;
    const UI::UINodeId tracked = inspectorFieldEdit_.field;
    const bool enterPressed = pendingInspectorFieldCommit_;
    pendingInspectorFieldCommit_ = false;
    const u32 currentStableId = stableEntityIdForHierarchyItem(selectionKey_);

    // Focus moving to another Inspector field ends the previous field's intent.
    // Enter ends it without moving focus, which is how a single-field edit is
    // confirmed without reaching for the pointer.
    const bool commitTracked = tracked.hasValue() &&
                               (enterPressed || focused != tracked);
    if (commitTracked) {
        auto currentText = tree.text(tracked);
        // Selection moved out from under the typed text, so the value no longer
        // belongs to any node the command would target. A destroyed or rebuilt
        // field is the same case: drop the intent, since nothing was published.
        const bool selectionIntact =
            inspectorFieldEdit_.stableId == currentStableId &&
            inspectorFieldEdit_.viewportSelectionRevision ==
                viewportSelectionRevision_;
        const bool changed = selectionIntact && currentText.has_value() &&
                             *currentText != inspectorFieldEdit_.baselineTextUtf8;
        const EditorCommand command = inspectorFieldEdit_.command;
        if (enterPressed && focused == tracked && changed) {
            // Enter keeps focus, so the field must re-baseline against the value
            // just published or the next blur would publish it a second time.
            try {
                inspectorFieldEdit_.baselineTextUtf8.assign(*currentText);
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Inspector field commit baseline allocation failed");
            }
        } else {
            inspectorFieldEdit_ = {};
        }
        if (changed && !queueEditorCommand(command)) {
            // Another command owns this frame. Re-arm the same intent so the edit
            // is published next frame instead of being lost.
            pendingInspectorFieldCommit_ = true;
            inspectorFieldEdit_.field = tracked;
            inspectorFieldEdit_.command = command;
            inspectorFieldEdit_.stableId = currentStableId;
            inspectorFieldEdit_.viewportSelectionRevision =
                viewportSelectionRevision_;
            return Tina::Core::success();
        }
    }

    if (!inspectorFieldEdit_.field.hasValue() && focused.hasValue()) {
        const auto command = inspectorFieldCommitCommand(focused);
        if (command.has_value()) {
            auto text = tree.text(focused);
            if (!text) {
                return Tina::Core::failure(std::move(text.error()));
            }
            try {
                inspectorFieldEdit_.field = focused;
                inspectorFieldEdit_.baselineTextUtf8.assign(*text);
                inspectorFieldEdit_.command = *command;
                inspectorFieldEdit_.stableId = currentStableId;
                inspectorFieldEdit_.viewportSelectionRevision =
                    viewportSelectionRevision_;
            } catch (const std::bad_alloc&) {
                inspectorFieldEdit_ = {};
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Inspector field commit baseline allocation failed");
            }
        }
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::runNodePropertyCommand(
    Tina::PrimaryWindowUITreeUpdater& tree, EditorCommand command,
    bool& published) -> Tina::Core::Status{
    published = false;
    const auto reject = [&](const Tina::Core::Error& error) -> Tina::Core::Status {
        try {
            authoringFeedback_ = "Node property edit rejected: ";
            authoringFeedback_ += error.message;
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Node property rejection feedback allocation failed");
        }
        ++counters_.inspectorRejectedTransactions;
        return Tina::Core::success();
    };
    const auto isRejectable = [](const Tina::Core::Error& error) noexcept {
        return error.code == Tina::Editor::EditorErrorCode::InvalidAuthoringOperation ||
               error.code == Tina::Editor::EditorErrorCode::EntityNotFound ||
               error.code == Tina::Editor::EditorErrorCode::NodePropertyUnavailable ||
               error.code.domain == Tina::Core::ErrorDomain::Asset;
    };

    const bool meshCommand = command == EditorCommand::NodeToggleMeshVisible;
    // Assign reads the Project Assets selection, so it is the one command that
    // may legitimately run while that selection is what changed last.
    const bool assignSpriteCommand =
        command == EditorCommand::NodeAssignSprite ||
        command == EditorCommand::NodeAssignResource;
    if (!authoringEnabled() || (!assignSpriteCommand && assetInspectorActive_) ||
        !sceneDocumentActive() ||
        tileMapEditingContext()) {
        return reject(Tina::Core::Error{
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Node property edits require an editable scene document"});
    }
    if (meshCommand != (workspaceMode_ == WorkspaceMode::World3D)) {
        return reject(Tina::Core::Error{
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Node property edit does not match the active workspace"});
    }

    auto idsResult = nodePropertySelectionStableIds();
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
                    "Node property validation message allocation failed");
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
                "Node property selection is absent from the World2D document");
        }
        return *entity;
    };

    Tina::Core::Result<Tina::Editor::EditorSceneOperationResult> result =
        Tina::Editor::EditorSceneOperationResult{};
    std::string_view successVerb{};
    std::string_view propertyGroupName{};

    switch (command) {
    case EditorCommand::NodeToggleSpriteVisible: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->sprite) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                "Rendering properties require a Sprite2D selection");
            break;
        }
        result = Tina::Editor::applyWorld2DSpriteNodeProperties(
            document_, ids, {.visible = !primary->sprite->visible});
        successVerb = "toggled";
        propertyGroupName = "Rendering visibility";
        break;
    }
    case EditorCommand::NodeToggleCameraActive: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->camera) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                "Toggle requires a Camera2D on the primary selection");
            break;
        }
        result = Tina::Editor::applyWorld2DCameraNodeProperties(
            document_, ids, {.active = !primary->camera->active});
        successVerb = "toggled";
        propertyGroupName = "Camera active";
        break;
    }
    case EditorCommand::NodeTogglePointLightActive: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->pointLight) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                "Toggle requires a PointLight2D on the primary selection");
            break;
        }
        result = Tina::Editor::applyWorld2DPointLightNodeProperties(
            document_, ids, {.active = !primary->pointLight->active});
        successVerb = "toggled";
        propertyGroupName = "Light active";
        break;
    }
    case EditorCommand::NodeToggleShadowOccluderActive: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->shadowOccluder) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                "Toggle requires a ShadowOccluder2D on the primary selection");
            break;
        }
        result = Tina::Editor::applyWorld2DShadowOccluderNodeProperties(
            document_, ids, {.active = !primary->shadowOccluder->active});
        successVerb = "toggled";
        propertyGroupName = "Occlusion active";
        break;
    }
    case EditorCommand::NodeToggleSpriteAnimationAutoPlay: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->spriteAnimation) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                "Toggle requires a SpriteAnimation2D on the primary selection");
            break;
        }
        result = Tina::Editor::applyWorld2DAnimatedSpriteNodeProperties(
            document_, ids, {.autoPlay = !primary->spriteAnimation->autoPlay});
        successVerb = "toggled";
        propertyGroupName = "Animation auto play";
        break;
    }
    case EditorCommand::NodeTogglePhysicsBodyEnabled: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->physicsBody) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                "Toggle requires a physics body selection");
            break;
        }
        result = Tina::Editor::applyWorld2DPhysicsBodyNodeProperties(
            document_, ids, {.enabled = !primary->physicsBody->enabled});
        successVerb = "toggled";
        propertyGroupName = "Physics body enabled";
        break;
    }
    case EditorCommand::NodeTogglePhysicsShapeEnabled: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->physicsShape) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                "Toggle requires a CollisionShape2D selection");
            break;
        }
        result = Tina::Editor::applyWorld2DPhysicsShapeNodeProperties(
            document_, ids, {.enabled = !primary->physicsShape->enabled});
        successVerb = "toggled";
        propertyGroupName = "Physics shape enabled";
        break;
    }
    case EditorCommand::NodeApplyAnimationProperties: {
        const auto& section = nodePropertySections_[4];
        auto clipText = tree.text(section.fields[0]);
        if (!clipText) {
            return Tina::Core::failure(std::move(clipText.error()));
        }
        Tina::Editor::World2DAnimatedSpriteNodeProperties input{};
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
        result = Tina::Editor::applyWorld2DAnimatedSpriteNodeProperties(document_, ids,
                                                               input);
        successVerb = "applied";
        propertyGroupName = "Animation";
        break;
    }
    case EditorCommand::NodeApplyPhysicsBody: {
        const auto& section = nodePropertySections_[5];
        Tina::Editor::World2DPhysicsBodyNodeProperties input{};
        const std::array<std::pair<UI::UINodeId, std::optional<float>*>, 6>
            fieldBindings{{
                {section.fields[0], &input.linearVelocityX},
                {section.fields[1], &input.linearVelocityY},
                {section.fields[2], &input.angularVelocityRadiansPerSecond},
                {section.fields[3], &input.linearDamping},
                {section.fields[4], &input.angularDamping},
                {section.fields[5], &input.gravityScale},
            }};
        const std::array<std::string_view, 6> fieldNames{
            "Linear velocity X", "Linear velocity Y", "Angular velocity",
            "Linear damping", "Angular damping", "Gravity scale"};
        for (Tina::Core::usize index = 0; index < fieldBindings.size(); ++index) {
            auto parsed = parseFloatField(fieldBindings[index].first,
                                          fieldNames[index]);
            if (!parsed) {
                if (isRejectable(parsed.error())) {
                    return reject(parsed.error());
                }
                return Tina::Core::failure(std::move(parsed.error()));
            }
            *fieldBindings[index].second = *parsed;
        }
        result = Tina::Editor::applyWorld2DPhysicsBodyNodeProperties(
            document_, ids, input);
        successVerb = "applied";
        propertyGroupName = "Physics body";
        break;
    }
    case EditorCommand::NodeApplyPhysicsShape: {
        const auto& section = nodePropertySections_[6];
        Tina::Editor::World2DPhysicsShapeNodeProperties input{};
        auto kindText = tree.text(section.fields[0]);
        if (!kindText) {
            return Tina::Core::failure(std::move(kindText.error()));
        }
        if (*kindText != "Mixed" && *kindText != "n/a") {
            if (*kindText == "Box") {
                input.kind = Tina::AssetFormat::World2DPhysicsShapeKind::Box;
            } else if (*kindText == "Circle") {
                input.kind = Tina::AssetFormat::World2DPhysicsShapeKind::Circle;
            } else if (*kindText == "Capsule") {
                input.kind = Tina::AssetFormat::World2DPhysicsShapeKind::Capsule;
            } else {
                return reject(Tina::Core::Error{
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Shape kind must be Box, Circle, Capsule, or Mixed"});
            }
        }
        // Angle is authored in degrees and stored in radians, so it is parsed
        // separately from the pass-through float fields below.
        std::optional<float> localAngleDegrees{};
        const std::array<std::pair<UI::UINodeId, std::optional<float>*>, 9>
            fieldBindings{{
                {section.fields[1], &input.halfExtentX},
                {section.fields[2], &input.halfExtentY},
                {section.fields[3], &input.radius},
                {section.fields[4], &input.localCenterX},
                {section.fields[5], &input.localCenterY},
                {section.fields[6], &localAngleDegrees},
                {section.fields[7], &input.density},
                {section.fields[8], &input.friction},
                {section.fields[9], &input.restitution},
            }};
        const std::array<std::string_view, 9> fieldNames{
            "Half extent X", "Half extent Y", "Radius", "Center X", "Center Y",
            "Angle deg", "Density", "Friction", "Restitution"};
        for (Tina::Core::usize index = 0; index < fieldBindings.size(); ++index) {
            auto parsed = parseFloatField(fieldBindings[index].first,
                                          fieldNames[index]);
            if (!parsed) {
                if (isRejectable(parsed.error())) {
                    return reject(parsed.error());
                }
                return Tina::Core::failure(std::move(parsed.error()));
            }
            *fieldBindings[index].second = *parsed;
        }
        if (localAngleDegrees.has_value()) {
            input.localAngleRadians = *localAngleDegrees * DegreesToRadians;
        }
        result = Tina::Editor::applyWorld2DPhysicsShapeNodeProperties(
            document_, ids, input);
        successVerb = "applied";
        propertyGroupName = "Physics shape";
        break;
    }
    case EditorCommand::NodeToggleMeshVisible: {
        std::vector<Tina::AssetFormat::PrefabNodeView> storage;
        auto prefab = document3D_.parseCurrentPrefab(storage);
        if (!prefab) {
            return Tina::Core::failure(std::move(prefab.error()));
        }
        const auto node = std::find_if(
            storage.begin(), storage.end(), [&](const auto& candidate) {
                return candidate.stableNodeId == ids.front();
            });
        if (node == storage.end() || !node->hasMesh || !node->hasMaterial) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                "Rendering properties require a Mesh3D selection");
            break;
        }
        result = Tina::Editor::applyWorld3DMeshNodeProperties(
            document3D_, ids, {.visible = !node->visible});
        successVerb = "toggled";
        propertyGroupName = "Rendering visibility";
        break;
    }
    case EditorCommand::NodeToggleResourceActive: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->resource) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                "Toggle requires a resource node selection");
            break;
        }
        result = Tina::Editor::applyWorld2DResourceNodeProperties(
            document_, ids, {.active = !primary->resource->active});
        successVerb = "toggled";
        propertyGroupName = "Resource active";
        break;
    }
    case EditorCommand::NodeAssignResource: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        auto primaryTemplate =
            Tina::Editor::classifyWorld2DNodeTemplate(*primary);
        if (!primaryTemplate) {
            result = Tina::Core::failure(std::move(primaryTemplate.error()));
            break;
        }
        const Tina::AssetFormat::AssetKind kind =
            requiredResourceAssetKind(*primaryTemplate);
        const Tina::Core::AssetId assetId = selectedProjectAssetIdOfKind(kind);
        if (!assetId) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Assign requires a matching asset kind selected in Project Assets");
            break;
        }
        result = Tina::Editor::applyWorld2DResourceNodeProperties(
            document_, ids, {.assetId = assetId});
        successVerb = "assigned";
        propertyGroupName = "Resource";
        break;
    }
    case EditorCommand::NodeAssignSprite: {
        const Tina::Core::AssetId spriteId = selectedProjectSpriteAssetId();
        if (!spriteId) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Assign requires a Sprite or imported Texture2D selected in Project Assets");
            break;
        }
        result = Tina::Editor::applyWorld2DSpriteNodeProperties(document_, ids,
                                                      {.spriteId = spriteId});
        successVerb = "assigned";
        propertyGroupName = "Rendering sprite";
        break;
    }
    case EditorCommand::NodeApplySprite: {
        const auto& section = nodePropertySections_[0];
        Tina::Editor::World2DSpriteNodeProperties input{};
        auto sizeX = parseFloatField(section.fields[0], "Size X");
        auto sizeY = parseFloatField(section.fields[1], "Size Y");
        auto pivotX = parseFloatField(section.fields[2], "Pivot X");
        auto pivotY = parseFloatField(section.fields[3], "Pivot Y");
        auto uvU0 = parseFloatField(section.fields[4], "UV u0");
        auto uvV0 = parseFloatField(section.fields[5], "UV v0");
        auto uvU1 = parseFloatField(section.fields[6], "UV u1");
        auto uvV1 = parseFloatField(section.fields[7], "UV v1");
        const bool numericFieldsParsed = sizeX && sizeY && pivotX && pivotY &&
                                         uvU0 && uvV0 && uvU1 && uvV1;
        auto sortingLayer = numericFieldsParsed
            ? parseIntField(section.fields[8], "Sort Layer", -32768L, 32767L)
            : Tina::Core::Result<std::optional<long>>{std::optional<long>{}};
        auto orderInLayer = sortingLayer
            ? parseIntField(section.fields[9], "Order", -2147483647L, 2147483647L)
            : Tina::Core::Result<std::optional<long>>{std::optional<long>{}};
        if (!numericFieldsParsed || !sortingLayer || !orderInLayer) {
            const Tina::Core::Error error =
                !sizeX ? sizeX.error() : !sizeY ? sizeY.error()
                : !pivotX ? pivotX.error() : !pivotY ? pivotY.error()
                : !uvU0 ? uvU0.error() : !uvV0 ? uvV0.error()
                : !uvU1 ? uvU1.error() : !uvV1 ? uvV1.error()
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
        input.uvU0 = *uvU0;
        input.uvV0 = *uvV0;
        input.uvU1 = *uvU1;
        input.uvV1 = *uvV1;
        if (sortingLayer->has_value()) {
            input.sortingLayer = static_cast<Tina::Core::i16>(**sortingLayer);
        }
        if (orderInLayer->has_value()) {
            input.orderInLayer = static_cast<Tina::Core::i32>(**orderInLayer);
        }
        auto spriteColorText = tree.text(spriteColorField_.textEdit);
        if (!spriteColorText) {
            return Tina::Core::failure(std::move(spriteColorText.error()));
        }
        if (*spriteColorText != "Mixed" && *spriteColorText != "n/a") {
            auto color = UI::parseColorFieldValue(*spriteColorText);
            if (!color) {
                return reject(Tina::Core::Error{
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Sprite color must use #RRGGBBAA hexadecimal format"});
            }
            input.color = std::array<Tina::Core::u8, 4>{
                color->red, color->green, color->blue, color->alpha};
        }
        result = Tina::Editor::applyWorld2DSpriteNodeProperties(document_, ids, input);
        successVerb = "applied";
        propertyGroupName = "Rendering";
        break;
    }
    case EditorCommand::NodeToggleSpriteFlipX:
    case EditorCommand::NodeToggleSpriteFlipY: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->sprite) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                "Flip requires a Sprite2D or AnimatedSprite2D selection");
            break;
        }
        Tina::Editor::World2DSpriteNodeProperties input{};
        if (command == EditorCommand::NodeToggleSpriteFlipX) {
            input.flipX = !primary->sprite->flipX;
            propertyGroupName = "Rendering flip X";
        } else {
            input.flipY = !primary->sprite->flipY;
            propertyGroupName = "Rendering flip Y";
        }
        result = Tina::Editor::applyWorld2DSpriteNodeProperties(document_, ids,
                                                               input);
        successVerb = "toggled";
        break;
    }
    case EditorCommand::NodeApplyCamera: {
        const auto& section = nodePropertySections_[1];
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
        Tina::Editor::World2DCameraNodeProperties input{};
        input.fixedWorldHeightMeters = *height;
        input.referencePixelsPerMeter = *pixelsPerMeter;
        if (heightPixels->has_value()) {
            input.referenceHeightPixels = static_cast<u32>(**heightPixels);
        }
        result = Tina::Editor::applyWorld2DCameraNodeProperties(document_, ids, input);
        successVerb = "applied";
        propertyGroupName = "Camera";
        break;
    }
    case EditorCommand::NodeApplyPointLight: {
        const auto& section = nodePropertySections_[2];
        Tina::Editor::World2DPointLightNodeProperties input{};
        auto colorText = tree.text(pointLightColorField_.textEdit);
        if (!colorText) {
            return Tina::Core::failure(std::move(colorText.error()));
        }
        if (*colorText != "Mixed" && *colorText != "n/a") {
            auto color = UI::parseColorFieldValue(*colorText);
            if (!color) {
                return reject(Tina::Core::Error{
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Color must use #RRGGBBAA hexadecimal format"});
            }
            constexpr float ByteToUnit = 1.0F / 255.0F;
            input.colorRed = static_cast<float>(color->red) * ByteToUnit;
            input.colorGreen = static_cast<float>(color->green) * ByteToUnit;
            input.colorBlue = static_cast<float>(color->blue) * ByteToUnit;
        }
        const std::array<std::pair<UI::UINodeId, std::optional<float>*>, 3>
            fieldBindings{{
                {section.fields[0], &input.intensity},
                {section.fields[1], &input.radiusMeters},
                {section.fields[2], &input.sourceRadiusMeters},
            }};
        const std::array<std::string_view, 3> fieldNames{
            "Intensity", "Radius", "Src Radius"};
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
        result = Tina::Editor::applyWorld2DPointLightNodeProperties(document_, ids, input);
        successVerb = "applied";
        propertyGroupName = "Light";
        break;
    }
    case EditorCommand::NodeApplyShadowOccluder: {
        const auto& section = nodePropertySections_[3];
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
        result = Tina::Editor::applyWorld2DShadowOccluderNodeProperties(
            document_, ids,
            {.localStartX = *startX, .localStartY = *startY,
             .localEndX = *endX, .localEndY = *endY});
        successVerb = "applied";
        propertyGroupName = "Occlusion";
        break;
    }
    default:
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::InvalidArgument,
            "Unknown Inspector node property command");
    }

    if (!result) {
        if (isRejectable(result.error())) {
            return reject(result.error());
        }
        return Tina::Core::failure(std::move(result.error()));
    }
    try {
        authoringFeedback_ = "Node ";
        authoringFeedback_ += propertyGroupName;
        authoringFeedback_ += ' ';
        authoringFeedback_ += successVerb;
        if (result->affectedItemCount == 0U) {
            authoringFeedback_ =
                "Node properties unchanged; no document revision published";
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
            "Node property feedback allocation failed");
    }
    if (result->affectedItemCount > 0U) {
        published = true;
        ++counters_.authoringEdits;
        ++counters_.inspectorTransactions;
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::refreshNodePropertySectionsUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    const u32 primaryId = stableEntityIdForHierarchyItem(selectionKey_);
    const bool selectionEditable = authoringEnabled() && !assetInspectorActive_ &&
                                   primaryId != 0U && !tileMapEditingContext() &&
                                   sceneDocumentActive();
    const bool world2D = workspaceMode_ == WorkspaceMode::World2D;
    const bool nodeContextVisible = !assetInspectorActive_ &&
                                    !tileMapEditingContext() && primaryId != 0U;
    for (NodePropertySectionUi& section : nodePropertySections_) {
        section.rootLayout.visibility = UI::UIVisibility::Collapsed;
        if (auto status = tree.setLayoutStyle(
                section.collapsible.root, section.rootLayout);
            !status) {
            return status;
        }
    }

    const auto resetSection = [&](const NodePropertySectionUi& section)
        -> Tina::Core::Status {
        if (auto status = tree.setEnabled(section.activeSwitch, false); !status) {
            return status;
        }
        if (auto status = tree.setChecked(section.activeSwitch, false); !status) {
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
        if (section.resourceAssignButton.hasValue()) {
            if (auto status = tree.setEnabled(section.resourceAssignButton, false); !status) {
                return status;
            }
        }
        if (section.resourceSlot.hasValue()) {
            if (auto status = tree.setEnabled(section.resourceSlot, false);
                !status) {
                return status;
            }
        }
        if (section.resourceLabel.hasValue()) {
            if (auto status = tree.setText(
                    section.resourceLabel,
                    "Click to choose a Sprite or Texture2D");
                !status) {
                return status;
            }
            auto theme = tree.productTheme();
            if (!theme) {
                return Tina::Core::failure(std::move(theme.error()));
            }
            if (auto status = tree.setBoxPaint(
                    section.resourceSlot,
                    UI::makeSolidBox(theme->colors.surfaceContainerLow));
                !status) {
                return status;
            }
        }
        if (&section == &nodePropertySections_[0]) {
            spriteColorMixed_ = false;
            if (auto status = tree.setText(spriteColorField_.textEdit, "n/a");
                !status) {
                return status;
            }
            if (auto status = tree.setEnabled(spriteColorField_.swatchButton,
                                              false);
                !status) {
                return status;
            }
            if (auto status = tree.setEnabled(spriteColorField_.textEdit, false);
                !status) {
                return status;
            }
            for (const UI::UINodeId flip : {spriteFlipXSwitch_,
                                            spriteFlipYSwitch_}) {
                if (auto status = tree.setChecked(flip, false); !status) {
                    return status;
                }
                if (auto status = tree.setEnabled(flip, false); !status) {
                    return status;
                }
            }
        }
        if (&section == &nodePropertySections_[2]) {
            pointLightColorMixed_ = false;
            if (auto status = tree.setText(pointLightColorField_.textEdit, "n/a");
                !status) {
                return status;
            }
            if (auto status = tree.setEnabled(
                    pointLightColorField_.swatchButton, false); !status) {
                return status;
            }
            if (auto status = tree.setEnabled(
                    pointLightColorField_.textEdit, false); !status) {
                return status;
            }
            for (const UI::UINodeId slider : pointLightColorPicker_.sliders()) {
                if (auto status = tree.setEnabled(slider, false); !status) {
                    return status;
                }
            }
        }
        return Tina::Core::success();
    };

    const auto setSectionVisible =
        [&](Tina::Core::usize sectionIndex, bool visible) -> Tina::Core::Status {
        auto& section = nodePropertySections_[sectionIndex];
        section.rootLayout.visibility = visible ? UI::UIVisibility::Visible
                                                : UI::UIVisibility::Collapsed;
        return tree.setLayoutStyle(section.collapsible.root, section.rootLayout);
    };

    if (!nodeContextVisible) {
        for (const NodePropertySectionUi& section : nodePropertySections_) {
            if (auto status = resetSection(section); !status) {
                return status;
            }
        }
        return Tina::Core::success();
    }

    if (world2D) {
        if (auto status = resetSection(
                nodePropertySections_[MeshPropertiesSectionIndex]);
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
                "Node property section staging allocation failed");
        }
        if (primary == nullptr && !selected.empty()) {
            primary = selected.front();
        }

        if (primary == nullptr || selected.empty()) {
            for (Tina::Core::usize sectionIndex = 0;
                 sectionIndex < MeshPropertiesSectionIndex;
                 ++sectionIndex) {
                if (auto status = resetSection(nodePropertySections_[sectionIndex]);
                    !status) {
                    return status;
                }
            }
            return Tina::Core::success();
        }
        auto primaryTemplate = Tina::Editor::classifyWorld2DNodeTemplate(*primary);
        if (!primaryTemplate) {
            return Tina::Core::failure(std::move(primaryTemplate.error()));
        }
        bool uniformNodeKinds = true;
        for (const auto* entity : selected) {
            auto nodeTemplate =
                Tina::Editor::classifyWorld2DNodeTemplate(*entity);
            if (!nodeTemplate) {
                return Tina::Core::failure(std::move(nodeTemplate.error()));
            }
            uniformNodeKinds = uniformNodeKinds &&
                               *nodeTemplate == *primaryTemplate;
        }
        const auto groupBelongsToNode =
            [nodeTemplate = *primaryTemplate](
                World2DNodePropertyGroup group) noexcept {
                switch (nodeTemplate) {
                case Tina::Editor::World2DNodeTemplate::Sprite2D:
                    return group == World2DNodePropertyGroup::Rendering;
                case Tina::Editor::World2DNodeTemplate::AnimatedSprite2D:
                    return group == World2DNodePropertyGroup::Rendering ||
                           group == World2DNodePropertyGroup::Animation;
                case Tina::Editor::World2DNodeTemplate::Camera2D:
                    return group == World2DNodePropertyGroup::Camera;
                case Tina::Editor::World2DNodeTemplate::PointLight2D:
                    return group == World2DNodePropertyGroup::Light;
                case Tina::Editor::World2DNodeTemplate::ShadowOccluder2D:
                    return group == World2DNodePropertyGroup::Occlusion;
                case Tina::Editor::World2DNodeTemplate::TileMap2D:
                case Tina::Editor::World2DNodeTemplate::FxEmitter2D:
                case Tina::Editor::World2DNodeTemplate::NavigationRegion2D:
                case Tina::Editor::World2DNodeTemplate::AudioPlayer2D:
                    return group == World2DNodePropertyGroup::Resource;
                case Tina::Editor::World2DNodeTemplate::Node2D:
                case Tina::Editor::World2DNodeTemplate::Marker2D:
                    return false;
                case Tina::Editor::World2DNodeTemplate::CollisionShape2D:
                    return group == World2DNodePropertyGroup::PhysicsShape;
                case Tina::Editor::World2DNodeTemplate::StaticBody2D:
                case Tina::Editor::World2DNodeTemplate::RigidBody2D:
                case Tina::Editor::World2DNodeTemplate::CharacterBody2D:
                case Tina::Editor::World2DNodeTemplate::Area2D:
                    return group == World2DNodePropertyGroup::PhysicsBody;
                }
                return false;
            };
        for (Tina::Core::usize sectionIndex = 0;
             sectionIndex < MeshPropertiesSectionIndex;
             ++sectionIndex) {
            const auto& section = nodePropertySections_[sectionIndex];
            const auto group = static_cast<World2DNodePropertyGroup>(
                sectionIndex);
            const bool sectionVisible = uniformNodeKinds &&
                                        groupBelongsToNode(group);
            if (auto status = setSectionVisible(sectionIndex, sectionVisible);
                !status) {
                return status;
            }
            if (!sectionVisible) {
                if (auto status = resetSection(section); !status) {
                    return status;
                }
                continue;
            }
            if (auto status = tree.setEnabled(section.activeSwitch,
                                              selectionEditable);
                !status) {
                return status;
            }
            const bool fieldsEditable = selectionEditable;
            for (Tina::Core::usize index = 0; index < section.fieldCount;
                 ++index) {
                if (auto status = tree.setEnabled(section.fields[index],
                                                  fieldsEditable);
                    !status) {
                    return status;
                }
            }
            if (section.resourceAssignButton.hasValue()) {
                // Rendering accepts Sprite or Texture2D; a resource node accepts
                // only its own declared kind.
                const bool assignableSelection =
                    sectionIndex == 0U
                        ? static_cast<bool>(selectedProjectSpriteAssetId())
                        : static_cast<bool>(selectedProjectAssetIdOfKind(
                              requiredResourceAssetKind(*primaryTemplate)));
                if (auto status = tree.setEnabled(section.resourceAssignButton,
                                                  fieldsEditable &&
                                                      assignableSelection);
                    !status) {
                    return status;
                }
            }
            if (section.resourceSlot.hasValue()) {
                // The slot opens the picker, so it stays reachable even when
                // Project Assets has nothing compatible selected.
                if (auto status = tree.setEnabled(section.resourceSlot,
                                                  fieldsEditable);
                    !status) {
                    return status;
                }
            }
            if (sectionIndex == 2U) {
                if (auto status = tree.setEnabled(
                        pointLightColorField_.swatchButton, fieldsEditable);
                    !status) {
                    return status;
                }
                if (auto status = tree.setEnabled(
                        pointLightColorField_.textEdit, fieldsEditable);
                    !status) {
                    return status;
                }
                for (const UI::UINodeId slider : pointLightColorPicker_.sliders()) {
                    if (auto status = tree.setEnabled(slider, fieldsEditable);
                        !status) {
                        return status;
                    }
                }
            }
            const auto mixedFloat = [&](auto&& accessor) {
                const float primaryValue = accessor(*primary);
                for (const auto* entity : selected) {
                    if (std::abs(accessor(*entity) - primaryValue) > 1.0e-4F) {
                        return true;
                    }
                }
                return false;
            };
            const auto mixedBool = [&](auto&& accessor) {
                const bool primaryValue = accessor(*primary);
                for (const auto* entity : selected) {
                    if (accessor(*entity) != primaryValue) {
                        return true;
                    }
                }
                return false;
            };
            const auto setField = [&](Tina::Core::usize index, bool mixedValue,
                                      std::string_view value) -> Tina::Core::Status {
                // Refreshes run on every authoring command, so the field the user
                // is typing in keeps its in-flight text until it commits.
                if (inspectorFieldIsBeingEdited(section.fields[index])) {
                    return Tina::Core::success();
                }
                return tree.setText(section.fields[index],
                                    mixedValue ? std::string_view{"Mixed"} : value);
            };
            const auto setNumberField = [&](Tina::Core::usize index,
                                            bool mixedValue,
                                            float value) -> Tina::Core::Status {
                if (mixedValue) {
                    return setField(index, true, {});
                }
                auto text = formatEditorNumber(value);
                if (!text) {
                    return Tina::Core::failure(std::move(text.error()));
                }
                return setField(index, false, text->view());
            };

            Tina::Core::Status status = Tina::Core::success();
            switch (group) {
            case World2DNodePropertyGroup::Rendering: {
                const auto& sprite = *primary->sprite;
                std::string resourceText;
                try {
                    const bool mixedSpriteId = std::any_of(
                        selected.begin(), selected.end(),
                        [&sprite](const auto* entity) {
                            return entity->sprite->spriteId != sprite.spriteId;
                        });
                    if (mixedSpriteId) {
                        resourceText = "Mixed";
                    } else {
                        const auto* resourceAsset =
                            projectAssets_.inspectorSnapshot(sprite.spriteId);
                        if (resourceAsset != nullptr) {
                            resourceText = resourceAsset->displayName;
                        } else if (sprite.spriteId) {
                            const auto idText = sprite.spriteId.canonicalText();
                            resourceText.assign("Missing asset ");
                            resourceText.append(idText.data(), 8U);
                        } else {
                            resourceText = "Click to choose a Sprite or Texture2D";
                        }
                    }
                } catch (const std::bad_alloc&) {
                    return Tina::Core::failure(
                        Tina::Core::CoreErrorCode::OutOfMemory,
                        "Sprite resource slot label allocation failed");
                }
                if (status) {
                    status = tree.setText(section.resourceLabel, resourceText);
                }
                if (status) {
                    status = tree.setEnabled(section.resourceAssignButton,
                                             fieldsEditable &&
                                                 selectedProjectSpriteAssetId());
                }
                if (status) {
                    auto theme = tree.productTheme();
                    if (!theme) {
                        return Tina::Core::failure(std::move(theme.error()));
                    }
                    status = tree.setBoxPaint(
                        section.resourceSlot,
                        UI::makeSolidBox(projectAssetDragOverSpriteResource_
                                             ? theme->colors.primaryContainer
                                             : theme->colors.surfaceContainerLow));
                }
                if (status) {
                    status = tree.setChecked(
                        section.activeSwitch,
                        sprite.visible &&
                            !mixedBool([](const auto& e) {
                                return e.sprite->visible;
                            }));
                }
                if (status) {
                    status = setNumberField(
                        0,
                        mixedFloat([](const auto& e) { return e.sprite->sizeX; }),
                        sprite.sizeX);
                }
                if (status) {
                    status = setNumberField(
                        1,
                        mixedFloat([](const auto& e) { return e.sprite->sizeY; }),
                        sprite.sizeY);
                }
                if (status) {
                    status = setNumberField(
                        2,
                        mixedFloat([](const auto& e) { return e.sprite->pivotX; }),
                        sprite.pivotX);
                }
                if (status) {
                    status = setNumberField(
                        3,
                        mixedFloat([](const auto& e) { return e.sprite->pivotY; }),
                        sprite.pivotY);
                }
                if (status) {
                    status = setNumberField(
                        4, mixedFloat([](const auto& e) {
                            return e.sprite->uvU0;
                        }),
                        sprite.uvU0);
                }
                if (status) {
                    status = setNumberField(
                        5, mixedFloat([](const auto& e) {
                            return e.sprite->uvV0;
                        }),
                        sprite.uvV0);
                }
                if (status) {
                    status = setNumberField(
                        6, mixedFloat([](const auto& e) {
                            return e.sprite->uvU1;
                        }),
                        sprite.uvU1);
                }
                if (status) {
                    status = setNumberField(
                        7, mixedFloat([](const auto& e) {
                            return e.sprite->uvV1;
                        }),
                        sprite.uvV1);
                }
                if (status) {
                    status = setField(
                        8,
                        mixedFloat([](const auto& e) {
                            return static_cast<float>(e.sprite->sortingLayer);
                        }),
                        std::to_string(sprite.sortingLayer));
                }
                if (status) {
                    status = setField(
                        9,
                        mixedFloat([](const auto& e) {
                            return static_cast<float>(e.sprite->orderInLayer);
                        }),
                        std::to_string(sprite.orderInLayer));
                }
                spriteColorMixed_ =
                    mixedBool([](const auto& e) { return e.sprite->colorRed; }) ||
                    mixedBool([](const auto& e) { return e.sprite->colorGreen; }) ||
                    mixedBool([](const auto& e) { return e.sprite->colorBlue; }) ||
                    mixedBool([](const auto& e) { return e.sprite->colorAlpha; });
                spriteColorValue_ = UI::rgba8(sprite.colorRed, sprite.colorGreen,
                                              sprite.colorBlue, sprite.colorAlpha);
                if (status &&
                    !inspectorFieldIsBeingEdited(spriteColorField_.textEdit)) {
                    const UI::UIColorFieldText colorText =
                        UI::formatColorFieldValue(spriteColorValue_);
                    status = tree.setText(
                        spriteColorField_.textEdit,
                        spriteColorMixed_ ? std::string_view{"Mixed"}
                                          : colorText.view());
                }
                if (status) {
                    auto spriteTheme = tree.productTheme();
                    if (!spriteTheme) {
                        return Tina::Core::failure(std::move(spriteTheme.error()));
                    }
                    status = tree.setBoxPaint(
                        spriteColorField_.swatchButton,
                        UI::makePanelBoxPaint(*spriteTheme, spriteColorValue_,
                                              UI::UIElevation::Flat));
                }
                if (status) {
                    status = tree.setEnabled(spriteColorField_.swatchButton,
                                             fieldsEditable);
                }
                if (status) {
                    status = tree.setEnabled(spriteColorField_.textEdit,
                                             fieldsEditable);
                }
                if (status) {
                    status = tree.setChecked(
                        spriteFlipXSwitch_,
                        sprite.flipX &&
                            !mixedBool([](const auto& e) {
                                return e.sprite->flipX;
                            }));
                }
                if (status) {
                    status = tree.setChecked(
                        spriteFlipYSwitch_,
                        sprite.flipY &&
                            !mixedBool([](const auto& e) {
                                return e.sprite->flipY;
                            }));
                }
                if (status) {
                    status = tree.setEnabled(spriteFlipXSwitch_, fieldsEditable);
                }
                if (status) {
                    status = tree.setEnabled(spriteFlipYSwitch_, fieldsEditable);
                }
                break;
            }
            case World2DNodePropertyGroup::Camera: {
                const auto& camera = *primary->camera;
                status = tree.setChecked(
                    section.activeSwitch,
                    camera.active &&
                        !mixedBool([](const auto& e) { return e.camera->active; }));
                if (status) {
                    status = setNumberField(
                        0,
                        mixedFloat([](const auto& e) {
                            return e.camera->fixedWorldHeightMeters;
                        }),
                        camera.fixedWorldHeightMeters);
                }
                if (status) {
                    status = setNumberField(
                        1,
                        mixedFloat([](const auto& e) {
                            return e.camera->referencePixelsPerMeter;
                        }),
                        camera.referencePixelsPerMeter);
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
            case World2DNodePropertyGroup::Light: {
                const auto& light = *primary->pointLight;
                status = tree.setChecked(
                    section.activeSwitch,
                    light.active &&
                        !mixedBool([](const auto& e) {
                            return e.pointLight->active;
                        }));
                pointLightColorMixed_ =
                    mixedFloat([](const auto& e) {
                        return e.pointLight->colorRed;
                    }) ||
                    mixedFloat([](const auto& e) {
                        return e.pointLight->colorGreen;
                    }) ||
                    mixedFloat([](const auto& e) {
                        return e.pointLight->colorBlue;
                    });
                const auto toByte = [](float channel) noexcept {
                    return static_cast<u8>(std::lround(
                        std::clamp(channel, 0.0F, 1.0F) * 255.0F));
                };
                pointLightColorValue_ = UI::rgba8(
                    toByte(light.colorRed), toByte(light.colorGreen),
                    toByte(light.colorBlue));
                const UI::UIColorPickerState colorState =
                    UI::synchronizeColorPickerValue(pointLightColorValue_, false);
                if (status &&
                    !inspectorFieldIsBeingEdited(pointLightColorField_.textEdit)) {
                    status = tree.setText(
                        pointLightColorField_.textEdit,
                        pointLightColorMixed_ ? std::string_view{"Mixed"}
                                              : colorState.text.view());
                }
                for (Tina::Core::usize index = 0;
                     status && index < colorState.channelCount; ++index) {
                    status = tree.setSliderValue(
                        pointLightColorPicker_.channelSliders[index],
                        colorState.channelValues[index]);
                    if (status) {
                        status = tree.setText(
                            pointLightColorPicker_.channelValueLabels[index],
                            colorState.channelTexts[index].view());
                    }
                }
                if (!status) {
                    return status;
                }
                auto productTheme = tree.productTheme();
                if (!productTheme) {
                    return Tina::Core::failure(std::move(productTheme.error()));
                }
                status = tree.setBoxPaint(
                    pointLightColorField_.swatchButton,
                    UI::makePanelBoxPaint(*productTheme, pointLightColorValue_,
                                          UI::UIElevation::Flat));
                const bool colorEditable = fieldsEditable;
                if (status) {
                    status = tree.setEnabled(
                        pointLightColorField_.swatchButton, colorEditable);
                }
                if (status) {
                    status = tree.setEnabled(
                        pointLightColorField_.textEdit, colorEditable);
                }
                for (const UI::UINodeId slider : pointLightColorPicker_.sliders()) {
                    if (status) {
                        status = tree.setEnabled(slider, colorEditable);
                    }
                }
                const std::array<std::pair<float, bool>, 3> values{{
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
                    status = setNumberField(index, values[index].second,
                                            values[index].first);
                }
                break;
            }
            case World2DNodePropertyGroup::Occlusion: {
                const auto& occluder = *primary->shadowOccluder;
                status = tree.setChecked(
                    section.activeSwitch,
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
                    status = setNumberField(index, values[index].second,
                                            values[index].first);
                }
                break;
            }
            case World2DNodePropertyGroup::Animation: {
                const auto& animation = *primary->spriteAnimation;
                status = tree.setChecked(
                    section.activeSwitch,
                    animation.autoPlay &&
                        !mixedBool([](const auto& e) {
                            return e.spriteAnimation->autoPlay;
                        }));
                if (status) {
                    bool clipMixed = false;
                    for (const auto* entity : selected) {
                        if (entity->spriteAnimation->clipId != animation.clipId) {
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
                    status = setNumberField(
                        1,
                        mixedFloat([](const auto& e) {
                            return e.spriteAnimation->playbackSpeed;
                        }),
                        animation.playbackSpeed);
                }
                break;
            }
            case World2DNodePropertyGroup::PhysicsBody: {
                const auto& body = *primary->physicsBody;
                status = tree.setChecked(
                    section.activeSwitch,
                    body.enabled &&
                        !mixedBool([](const auto& e) {
                            return e.physicsBody->enabled;
                        }));
                const std::array<std::pair<float, bool>, 6> values{{
                    {body.linearVelocityX, mixedFloat([](const auto& e) {
                         return e.physicsBody->linearVelocityX;
                     })},
                    {body.linearVelocityY, mixedFloat([](const auto& e) {
                         return e.physicsBody->linearVelocityY;
                     })},
                    {body.angularVelocityRadiansPerSecond,
                     mixedFloat([](const auto& e) {
                         return e.physicsBody->angularVelocityRadiansPerSecond;
                     })},
                    {body.linearDamping, mixedFloat([](const auto& e) {
                         return e.physicsBody->linearDamping;
                     })},
                    {body.angularDamping, mixedFloat([](const auto& e) {
                         return e.physicsBody->angularDamping;
                     })},
                    {body.gravityScale, mixedFloat([](const auto& e) {
                         return e.physicsBody->gravityScale;
                     })},
                }};
                for (Tina::Core::usize index = 0;
                     status && index < values.size(); ++index) {
                    status = setNumberField(index, values[index].second,
                                            values[index].first);
                }
                break;
            }
            case World2DNodePropertyGroup::PhysicsShape: {
                const auto& shape = *primary->physicsShape;
                status = tree.setChecked(
                    section.activeSwitch,
                    shape.enabled &&
                        !mixedBool([](const auto& e) {
                            return e.physicsShape->enabled;
                        }));
                bool kindMixed = false;
                for (const auto* entity : selected) {
                    if (entity->physicsShape->kind != shape.kind) {
                        kindMixed = true;
                        break;
                    }
                }
                const auto kindText = [](Tina::AssetFormat::World2DPhysicsShapeKind kind)
                    -> std::string_view {
                    switch (kind) {
                    case Tina::AssetFormat::World2DPhysicsShapeKind::Box:
                        return "Box";
                    case Tina::AssetFormat::World2DPhysicsShapeKind::Circle:
                        return "Circle";
                    case Tina::AssetFormat::World2DPhysicsShapeKind::Capsule:
                        return "Capsule";
                    }
                    return "n/a";
                };
                if (status) {
                    status = setField(0, kindMixed, kindText(shape.kind));
                }
                // Angle is stored in radians and authored in degrees.
                const std::array<std::pair<float, bool>, 9> values{{
                    {shape.halfExtentX, mixedFloat([](const auto& e) {
                         return e.physicsShape->halfExtentX;
                     })},
                    {shape.halfExtentY, mixedFloat([](const auto& e) {
                         return e.physicsShape->halfExtentY;
                     })},
                    {shape.radius, mixedFloat([](const auto& e) {
                         return e.physicsShape->radius;
                     })},
                    {shape.localCenterX, mixedFloat([](const auto& e) {
                         return e.physicsShape->localCenterX;
                     })},
                    {shape.localCenterY, mixedFloat([](const auto& e) {
                         return e.physicsShape->localCenterY;
                     })},
                    {shape.localAngleRadians * RadiansToDegrees,
                     mixedFloat([](const auto& e) {
                         return e.physicsShape->localAngleRadians;
                     })},
                    {shape.density, mixedFloat([](const auto& e) {
                         return e.physicsShape->density;
                     })},
                    {shape.friction, mixedFloat([](const auto& e) {
                         return e.physicsShape->friction;
                     })},
                    {shape.restitution, mixedFloat([](const auto& e) {
                         return e.physicsShape->restitution;
                     })},
                }};
                for (Tina::Core::usize index = 0;
                     status && index < values.size(); ++index) {
                    status = setNumberField(index + 1U, values[index].second,
                                            values[index].first);
                }
                break;
            }
            case World2DNodePropertyGroup::Resource: {
                const auto& resource = *primary->resource;
                status = tree.setChecked(
                    section.activeSwitch,
                    resource.active &&
                        !mixedBool([](const auto& e) {
                            return e.resource->active;
                        }));
                std::string resourceText;
                try {
                    const bool mixedAssetId = std::any_of(
                        selected.begin(), selected.end(),
                        [&resource](const auto* entity) {
                            return entity->resource->assetId != resource.assetId;
                        });
                    if (mixedAssetId) {
                        resourceText = "Mixed";
                    } else if (const auto* asset = projectAssets_.inspectorSnapshot(
                                   resource.assetId);
                               asset != nullptr) {
                        resourceText = asset->displayName;
                    } else if (resource.assetId) {
                        const auto idText = resource.assetId.canonicalText();
                        resourceText.assign("Missing asset ");
                        resourceText.append(idText.data(), 8U);
                    } else {
                        resourceText = "Click to choose an asset";
                    }
                } catch (const std::bad_alloc&) {
                    return Tina::Core::failure(
                        Tina::Core::CoreErrorCode::OutOfMemory,
                        "Resource slot label allocation failed");
                }
                if (status) {
                    status = tree.setText(section.resourceLabel, resourceText);
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

    for (Tina::Core::usize sectionIndex = 0;
         sectionIndex < MeshPropertiesSectionIndex; ++sectionIndex) {
        if (auto status = resetSection(nodePropertySections_[sectionIndex]);
            !status) {
            return status;
        }
    }
    const auto& section = nodePropertySections_[MeshPropertiesSectionIndex];
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
        if (auto status = resetSection(section); !status) {
            return status;
        }
        return Tina::Core::success();
    }
    auto primaryTemplate =
        Tina::Editor::classifyWorld3DNodeTemplate(*primaryNode);
    if (!primaryTemplate) {
        return Tina::Core::failure(std::move(primaryTemplate.error()));
    }
    bool uniformMeshNodes =
        *primaryTemplate == Tina::Editor::World3DNodeTemplate::Mesh3D;
    bool visibilityMixed = false;
    bool meshMixed = false;
    bool materialMixed = false;
    for (Tina::Core::usize index = 0;
         uniformMeshNodes && index < viewportSelectedEntityCount_; ++index) {
        const u64 selectedId = viewportSelectedEntityIds_[index];
        const auto selectedNode = std::find_if(
            storage.begin(), storage.end(), [&](const auto& candidate) {
                return candidate.stableNodeId == selectedId;
            });
        if (selectedNode == storage.end()) {
            uniformMeshNodes = false;
            continue;
        }
        auto nodeTemplate =
            Tina::Editor::classifyWorld3DNodeTemplate(*selectedNode);
        if (!nodeTemplate) {
            return Tina::Core::failure(std::move(nodeTemplate.error()));
        }
        uniformMeshNodes =
            *nodeTemplate == Tina::Editor::World3DNodeTemplate::Mesh3D;
        if (uniformMeshNodes) {
            visibilityMixed = visibilityMixed ||
                              selectedNode->visible != primaryNode->visible;
            meshMixed = meshMixed || selectedNode->meshId != primaryNode->meshId;
            materialMixed = materialMixed ||
                            selectedNode->materialId != primaryNode->materialId;
        }
    }
    if (auto status = setSectionVisible(MeshPropertiesSectionIndex,
                                        uniformMeshNodes);
        !status) {
        return status;
    }
    if (!uniformMeshNodes) {
        if (auto status = resetSection(section); !status) {
            return status;
        }
        return Tina::Core::success();
    }
    if (auto status = tree.setEnabled(section.activeSwitch,
                                      selectionEditable);
        !status) {
        return status;
    }
    if (auto status = tree.setChecked(
            section.activeSwitch, primaryNode->visible && !visibilityMixed);
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
    const std::string meshText = assetIdPrefix(primaryNode->meshId);
    if (auto status = tree.setText(
            section.fields[0],
            meshMixed ? std::string_view{"Mixed"} : std::string_view{meshText});
        !status) {
        return status;
    }
    const std::string materialText = assetIdPrefix(primaryNode->materialId);
    if (auto status = tree.setText(
            section.fields[1], materialMixed ? std::string_view{"Mixed"}
                                             : std::string_view{materialText});
        !status) {
        return status;
    }
    return Tina::Core::success();
}

} // namespace Tina::EditorApp::WorkspaceInternal
