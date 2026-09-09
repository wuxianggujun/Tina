#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

using PhysicsDesc = Tina::AssetFormat::PrefabPhysics3DDesc;
constexpr std::array PhysicsFields{
    &PhysicsDesc::halfExtentX, &PhysicsDesc::halfExtentY, &PhysicsDesc::halfExtentZ,
    &PhysicsDesc::radiusMeters, &PhysicsDesc::halfHeightMeters, &PhysicsDesc::massKilograms,
    &PhysicsDesc::friction, &PhysicsDesc::restitution, &PhysicsDesc::maximumSlopeRadians,
    &PhysicsDesc::stepHeightMeters, &PhysicsDesc::floorSnapMeters,
    &PhysicsDesc::moveSpeedMetersPerSecond, &PhysicsDesc::jumpSpeedMetersPerSecond};
constexpr Tina::Core::usize SlopeField = 8;

} // namespace

auto EditorWorkspaceState::applyGameplay3DPropertyCommand(
    Tina::PrimaryWindowUITreeUpdater& tree, EditorCommand command, std::span<const u32> ids)
    -> Tina::Core::Result<Tina::Editor::EditorSceneOperationResult>
try {
    using namespace Tina::AssetFormat;
    if (!isGameplay3DNodePropertyCommand(command))
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                  "Unknown 3D gameplay property command");
    if (ids.size() != 1U)
        return Tina::Core::failure(Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                                  "3D gameplay properties require one selected node");
    std::vector<PrefabNodeView> storage;
    auto prefab = document3D_.parseCurrentPrefab(storage);
    if (!prefab) return Tina::Core::failure(std::move(prefab.error()));
    const auto node = std::find_if(storage.begin(), storage.end(), [&](const auto& value) {
        return value.stableNodeId == ids.front();
    });
    if (node == storage.end())
        return Tina::Core::failure(Tina::Editor::EditorErrorCode::EntityNotFound,
                                  "3D property selection is absent from the document");
    const auto readNumber = [&](UI::UINodeId field, float& destination) -> Tina::Core::Status {
        auto text = tree.text(field);
        if (!text) return Tina::Core::failure(std::move(text.error()));
        auto value = parseInspectorTransformValue(*text, "3D property");
        if (!value) return Tina::Core::failure(std::move(value.error()));
        if (*value) destination = **value;
        return Tina::Core::success();
    };
    Tina::Editor::World3DGameplayNodeProperties input;
    if (command >= EditorCommand::NodeTogglePhysics3D && command <= EditorCommand::NodeTogglePlayer3D) {
        auto physics = node->physics;
        if (command == EditorCommand::NodeTogglePhysics3D) {
            if (physics) physics.reset();
            else physics.emplace();
        } else {
            if (!physics)
                return Tina::Core::failure(Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                                          "Enable the Physics component before editing it");
            if (command >= EditorCommand::NodePhysics3DStatic && command <= EditorCommand::NodePhysics3DCharacter) {
                physics->type = static_cast<PrefabPhysicsBody3D>(
                    static_cast<u32>(command) - static_cast<u32>(EditorCommand::NodePhysics3DStatic));
                if (physics->type == PrefabPhysicsBody3D::Character) {
                    physics->shape = PrefabPhysicsShape3D::Capsule;
                    physics->sensor = false;
                } else physics->playerControlled = false;
            } else if (command >= EditorCommand::NodePhysics3DBox && command <= EditorCommand::NodePhysics3DCapsule) {
                physics->shape = static_cast<PrefabPhysicsShape3D>(
                    static_cast<u32>(command) - static_cast<u32>(EditorCommand::NodePhysics3DBox));
            } else if (command == EditorCommand::NodeTogglePhysics3DSensor) {
                physics->sensor = !physics->sensor;
            } else if (command == EditorCommand::NodeTogglePlayer3D) {
                physics->playerControlled = !physics->playerControlled;
                if (physics->playerControlled && std::any_of(storage.begin(), storage.end(), [&](const auto& other) {
                        return other.stableNodeId != node->stableNodeId && other.physics && other.physics->playerControlled;
                    }))
                    return Tina::Core::failure(Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                                              "The level already has a player-controlled character");
            } else if (command == EditorCommand::NodeApplyPhysics3D) {
                const auto& section = nodePropertySections_[Physics3DPropertiesSectionIndex];
                for (Tina::Core::usize field = 0; field < PhysicsFields.size(); ++field) {
                    float value = (*physics).*PhysicsFields[field];
                    if (field == SlopeField) value /= DegreesToRadians;
                    if (auto status = readNumber(section.fields[field], value); !status)
                        return Tina::Core::failure(std::move(status.error()));
                    (*physics).*PhysicsFields[field] = field == SlopeField ? value * DegreesToRadians : value;
                }
            }
        }
        input.physics.emplace(std::move(physics));
    } else if (command == EditorCommand::NodeApplyCamera3D || command == EditorCommand::NodeToggleCamera3D) {
        if (!node->camera)
            return Tina::Core::failure(Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                                      "Camera properties require a Camera3D node");
        auto camera = *node->camera;
        if (command == EditorCommand::NodeToggleCamera3D) camera.active = !camera.active;
        else {
            const auto& section = nodePropertySections_[Camera3DPropertiesSectionIndex];
            float fovDegrees = camera.verticalFovRadians / DegreesToRadians;
            const std::array destinations{&fovDegrees, &camera.nearPlane, &camera.farPlane};
            for (Tina::Core::usize field = 0; field < destinations.size(); ++field)
                if (auto status = readNumber(section.fields[field], *destinations[field]); !status)
                    return Tina::Core::failure(std::move(status.error()));
            camera.verticalFovRadians = fovDegrees * DegreesToRadians;
        }
        input.camera = camera;
    } else {
        if (node->nodeKind != PrefabNodeKind::SkinnedMesh3D)
            return Tina::Core::failure(Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                                      "Animation properties require a SkinnedMesh3D node");
        auto animation = node->animation;
        if (command == EditorCommand::NodeToggleAnimation3D && animation) animation.reset();
        else if (command == EditorCommand::NodeAssignAnimation3D || command == EditorCommand::NodeToggleAnimation3D) {
            const auto clip = selectedProjectAssetIdOfKind(AssetKind::AnimationClip3D);
            if (!clip)
                return Tina::Core::failure(Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                                          "Select an AnimationClip3D asset to bind");
            if (!animation) animation.emplace();
            animation->clipId = clip;
        } else {
            if (!animation)
                return Tina::Core::failure(Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                                          "Bind an animation clip before editing playback");
            if (command == EditorCommand::NodeToggleAnimation3DAutoPlay) animation->autoPlay = !animation->autoPlay;
            else if (command == EditorCommand::NodeApplyAnimation3D) {
                if (auto status = readNumber(nodePropertySections_[Animation3DPropertiesSectionIndex].fields[0],
                                             animation->playbackSpeed); !status)
                    return Tina::Core::failure(std::move(status.error()));
            } else return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                             "Unknown 3D gameplay property command");
        }
        input.animation.emplace(std::move(animation));
    }
    return Tina::Editor::applyWorld3DGameplayNodeProperties(document3D_, ids, input);
} catch (const std::bad_alloc&) {
    return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory, "3D property staging allocation failed");
}

auto EditorWorkspaceState::refreshGameplay3DPropertiesUi(Tina::PrimaryWindowUITreeUpdater& tree)
    -> Tina::Core::Status
try {
    using namespace Tina::AssetFormat;
    if (workspaceMode_ != WorkspaceMode::World3D || !sceneDocumentActive()) return Tina::Core::success();
    std::vector<PrefabNodeView> storage;
    auto prefab = document3D_.parseCurrentPrefab(storage);
    if (!prefab) return Tina::Core::failure(std::move(prefab.error()));
    const u32 stableId = stableEntityIdForHierarchyItem(selectionKey_);
    const auto node = std::find_if(storage.begin(), storage.end(), [&](const auto& value) {
        return value.stableNodeId == stableId;
    });
    if (node == storage.end()) return Tina::Core::success();
    const bool editable = authoringEnabled() && !assetInspectorActive_ && viewportSelectedEntityCount_ == 1U;
    const auto toggle = [&](UI::UINodeId target, bool enabled, bool checked) -> Tina::Core::Status {
        if (auto status = tree.setEnabled(target, enabled); !status) return status;
        return tree.setChecked(target, checked);
    };
    const auto number = [&](UI::UINodeId target, bool enabled, float value) -> Tina::Core::Status {
        if (auto status = tree.setEnabled(target, enabled); !status) return status;
        if (inspectorFieldIsBeingEdited(target)) return Tina::Core::success();
        auto text = formatEditorNumber(value);
        if (!text) return Tina::Core::failure(std::move(text.error()));
        return tree.setText(target, text->view());
    };
    auto& physicsUi = nodePropertySections_[Physics3DPropertiesSectionIndex];
    physicsUi.rootLayout.visibility = UI::UIVisibility::Visible;
    if (auto status = tree.setLayoutStyle(physicsUi.collapsible.root, physicsUi.rootLayout); !status) return status;
    const auto physics = node->physics.value_or(PhysicsDesc{});
    const bool physicsEditable = editable && node->physics.has_value();
    const bool character = physics.type == PrefabPhysicsBody3D::Character;
    if (auto status = toggle(physicsUi.activeSwitch, editable, node->physics.has_value()); !status) return status;
    if (auto status = toggle(physicsUi.toggles[0], physicsEditable && !character, physics.sensor); !status) return status;
    if (auto status = toggle(physicsUi.toggles[1], physicsEditable && character, physics.playerControlled); !status) return status;
    if (auto status = tree.setEnabled(physics3DBodyDropdown_, physicsEditable); !status) return status;
    if (auto status = tree.setEnabled(physics3DShapeDropdown_, physicsEditable && !character); !status) return status;
    if (auto status = tree.setDropdownSelectedItem(physics3DBodyDropdown_, physics3DBodyItems_[static_cast<u32>(physics.type)]); !status) return status;
    if (auto status = tree.setDropdownSelectedItem(physics3DShapeDropdown_, physics3DShapeItems_[static_cast<u32>(physics.shape)]); !status) return status;
    for (Tina::Core::usize field = 0; field < PhysicsFields.size(); ++field) {
        bool applicable = true;
        if (field < 3) applicable = physics.shape == PrefabPhysicsShape3D::Box;
        else if (field == 3) applicable = physics.shape != PrefabPhysicsShape3D::Box;
        else if (field == 4) applicable = physics.shape == PrefabPhysicsShape3D::Capsule;
        else if (field == 5) applicable = physics.type == PrefabPhysicsBody3D::Dynamic || character;
        else if (field >= 8) applicable = character && (field < 11 || physics.playerControlled);
        float value = physics.*PhysicsFields[field];
        if (field == SlopeField) value /= DegreesToRadians;
        if (auto status = number(physicsUi.fields[field], physicsEditable && applicable, value); !status) return status;
    }
    if (node->nodeKind == PrefabNodeKind::SkinnedMesh3D) {
        auto& animationUi = nodePropertySections_[Animation3DPropertiesSectionIndex];
        animationUi.rootLayout.visibility = UI::UIVisibility::Visible;
        if (auto status = tree.setLayoutStyle(animationUi.collapsible.root, animationUi.rootLayout); !status) return status;
        const auto animation = node->animation.value_or(PrefabAnimation3DDesc{});
        if (auto status = toggle(animationUi.activeSwitch, editable && node->animation.has_value(), node->animation.has_value()); !status) return status;
        if (auto status = toggle(animationUi.toggles[0], editable && node->animation.has_value(), animation.autoPlay); !status) return status;
        if (auto status = number(animationUi.fields[0], editable && node->animation.has_value(), animation.playbackSpeed); !status) return status;
        if (auto status = tree.setEnabled(animationUi.resourceSlot, editable); !status) return status;
        if (auto status = tree.setEnabled(animationUi.resourceAssignButton,
                editable && static_cast<bool>(selectedProjectAssetIdOfKind(AssetKind::AnimationClip3D))); !status) return status;
        std::string label = "None";
        if (animation.clipId) {
            const auto text = animation.clipId.canonicalText();
            label.assign(text.data(), 8U);
        }
        if (auto status = tree.setText(animationUi.resourceLabel, label); !status) return status;
    }
    if (node->camera) {
        auto& cameraUi = nodePropertySections_[Camera3DPropertiesSectionIndex];
        cameraUi.rootLayout.visibility = UI::UIVisibility::Visible;
        if (auto status = tree.setLayoutStyle(cameraUi.collapsible.root, cameraUi.rootLayout); !status) return status;
        if (auto status = toggle(cameraUi.activeSwitch, editable, node->camera->active); !status) return status;
        const std::array values{node->camera->verticalFovRadians / DegreesToRadians,
                                node->camera->nearPlane, node->camera->farPlane};
        for (Tina::Core::usize field = 0; field < values.size(); ++field)
            if (auto status = number(cameraUi.fields[field], editable, values[field]); !status) return status;
    }
    return Tina::Core::success();
} catch (const std::bad_alloc&) {
    return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory, "3D Inspector refresh allocation failed");
}

} // namespace Tina::EditorApp::WorkspaceInternal
