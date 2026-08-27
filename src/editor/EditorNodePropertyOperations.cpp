#include <tina/editor/EditorNodePropertyOperations.hpp>

#include <tina/editor/EditorErrors.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <new>
#include <utility>
#include <vector>

namespace Tina::Editor {
namespace {

[[nodiscard]] std::unexpected<Core::Error> nodePropertyAllocationFailure()
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory,
                         "Editor node property operation allocation failed");
}

[[nodiscard]] bool allFinite(std::initializer_list<float> values) noexcept
{
    return std::all_of(values.begin(), values.end(),
                       [](float value) { return std::isfinite(value); });
}

// Every listed stable ID must resolve; duplicates in the selection are
// tolerated and count once. Returns indices into `storage`.
template <typename Item, typename IdResolver>
[[nodiscard]] Core::Result<std::vector<Core::usize>>
resolveSelection(std::span<const Item> items, std::span<const Core::u32> stableIds,
                 IdResolver&& resolveId)
{
    std::vector<Core::usize> indices;
    indices.reserve(stableIds.size());
    for (const Core::u32 stableId : stableIds) {
        const auto found = std::find_if(
            items.begin(), items.end(), [&](const Item& item) {
                return resolveId(item) == stableId;
            });
        if (found == items.end()) {
            return Core::failure(EditorErrorCode::EntityNotFound,
                                 "Editor node property target was not found");
        }
        const auto index =
            static_cast<Core::usize>(std::distance(items.begin(), found));
        if (std::find(indices.begin(), indices.end(), index) == indices.end()) {
            indices.push_back(index);
        }
    }
    return indices;
}

// Shared batch-edit harness: parse, resolve the selection, let `editEntity`
// stage per-entity changes, then publish one replace() revision only when a
// value actually changed. `editEntity` fails closed for ineligible entities.
template <typename EditEntity>
[[nodiscard]] Core::Result<EditorSceneOperationResult>
editWorld2DEntities(World2DAuthoringDocument& document,
                    std::span<const Core::u32> stableEntityIds,
                    EditEntity&& editEntity)
try
{
    if (stableEntityIds.empty()) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Editor node property edit requires a selection");
    }
    std::vector<AssetFormat::World2DEntityDesc> storage;
    auto snapshot = document.parseCurrentSnapshot(storage);
    if (!snapshot) {
        return Core::failure(std::move(snapshot.error()));
    }
    auto indices = resolveSelection(
        std::span<const AssetFormat::World2DEntityDesc>{storage}, stableEntityIds,
        [](const auto& entity) { return entity.stableEntityId; });
    if (!indices) {
        return Core::failure(std::move(indices.error()));
    }

    Core::usize changedCount = 0;
    Core::u32 primaryStableId = 0;
    for (const Core::usize index : *indices) {
        const AssetFormat::World2DEntityDesc before = storage[index];
        if (auto status = editEntity(storage[index]); !status) {
            return Core::failure(std::move(status.error()));
        }
        if (!(storage[index] == before)) {
            ++changedCount;
            if (primaryStableId == 0) {
                primaryStableId = storage[index].stableEntityId;
            }
        }
    }
    if (changedCount == 0) {
        return EditorSceneOperationResult{
            .primaryStableId = stableEntityIds.front(),
            .affectedItemCount = 0,
        };
    }
    if (auto status = document.replace({
            .entities = storage,
            .gameplaySchema = snapshot->gameplaySchema,
            .gameplayVersion = snapshot->gameplayVersion,
            .gameplayBytes = snapshot->gameplayBytes,
        }); !status) {
        return Core::failure(std::move(status.error()));
    }
    return EditorSceneOperationResult{
        .primaryStableId = primaryStableId,
        .affectedItemCount = changedCount,
    };
}
catch (const std::bad_alloc&)
{
    return nodePropertyAllocationFailure();
}

template <typename Value>
void applyOptional(std::optional<Value> input, Value& target) noexcept
{
    if (input.has_value()) {
        target = *input;
    }
}

[[nodiscard]] Core::Status finiteEditStatus(bool finite) noexcept
{
    if (!finite) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Editor node property edit rejects non-finite values");
    }
    return Core::success();
}

[[nodiscard]] Core::Status validatePhysicsBodyInput(
    const World2DPhysicsBodyNodeProperties& input) noexcept
{
    if (!allFinite({
            input.linearVelocityX.value_or(0.0F),
            input.linearVelocityY.value_or(0.0F),
            input.angularVelocityRadiansPerSecond.value_or(0.0F),
            input.linearDamping.value_or(0.0F),
            input.angularDamping.value_or(0.0F),
            input.gravityScale.value_or(1.0F),
        })) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Physics body properties reject non-finite values");
    }
    if ((input.linearDamping.has_value() && *input.linearDamping < 0.0F) ||
        (input.angularDamping.has_value() && *input.angularDamping < 0.0F)) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Physics body damping must be non-negative");
    }
    return Core::success();
}

[[nodiscard]] Core::Status validatePhysicsShapeInput(
    const World2DPhysicsShapeNodeProperties& input) noexcept
{
    if (input.kind.has_value() &&
        static_cast<Core::u8>(*input.kind) >
            static_cast<Core::u8>(AssetFormat::World2DPhysicsShapeKind::Capsule)) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Physics shape kind is unsupported");
    }
    if (!allFinite({
            input.halfExtentX.value_or(0.5F),
            input.halfExtentY.value_or(0.5F),
            input.radius.value_or(0.5F),
            input.localCenterX.value_or(0.0F),
            input.localCenterY.value_or(0.0F),
            input.localAngleRadians.value_or(0.0F),
            input.localPointAX.value_or(-0.5F),
            input.localPointAY.value_or(0.0F),
            input.localPointBX.value_or(0.5F),
            input.localPointBY.value_or(0.0F),
            input.density.value_or(1.0F),
            input.friction.value_or(0.6F),
            input.restitution.value_or(0.0F),
        })) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Physics shape properties reject non-finite values");
    }
    if ((input.halfExtentX.has_value() && *input.halfExtentX <= 0.0F) ||
        (input.halfExtentY.has_value() && *input.halfExtentY <= 0.0F) ||
        (input.radius.has_value() && *input.radius <= 0.0F)) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Physics shape dimensions must be positive");
    }
    if (input.density.has_value() && *input.density < 0.0F) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Physics shape density must be non-negative");
    }
    if (input.friction.has_value() &&
        (*input.friction < 0.0F || *input.friction > 1.0F)) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Physics shape friction must be between zero and one");
    }
    if (input.restitution.has_value() &&
        (*input.restitution < 0.0F || *input.restitution > 1.0F)) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Physics shape restitution must be between zero and one");
    }
    return Core::success();
}

[[nodiscard]] Core::Status requireWorld2DNodeKind(
    const AssetFormat::World2DEntityDesc& entity,
    std::initializer_list<World2DNodeTemplate> allowedKinds,
    const char* mismatchMessage)
{
    auto nodeKind = classifyWorld2DNodeTemplate(entity);
    if (!nodeKind) {
        return Core::failure(std::move(nodeKind.error()));
    }
    if (std::find(allowedKinds.begin(), allowedKinds.end(), *nodeKind) ==
        allowedKinds.end()) {
        return Core::failure(EditorErrorCode::NodePropertyUnavailable,
                             mismatchMessage);
    }
    return Core::success();
}

template <typename EditNode>
[[nodiscard]] Core::Result<EditorSceneOperationResult>
editWorld3DNodes(World3DAuthoringDocument& document,
                 std::span<const Core::u32> stableNodeIds, EditNode&& editNode)
try
{
    if (stableNodeIds.empty()) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Editor node property edit requires a selection");
    }
    std::vector<AssetFormat::PrefabNodeView> storage;
    auto prefab = document.parseCurrentPrefab(storage);
    if (!prefab) {
        return Core::failure(std::move(prefab.error()));
    }
    auto indices = resolveSelection(
        std::span<const AssetFormat::PrefabNodeView>{storage}, stableNodeIds,
        [](const auto& node) { return node.stableNodeId; });
    if (!indices) {
        return Core::failure(std::move(indices.error()));
    }

    std::vector<AssetFormat::PrefabNodeDesc> nodes;
    nodes.reserve(storage.size());
    for (const auto& node : storage) {
        nodes.push_back({
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
        });
    }

    Core::usize changedCount = 0;
    Core::u32 primaryStableId = 0;
    for (const Core::usize index : *indices) {
        const AssetFormat::PrefabNodeDesc before = nodes[index];
        if (auto status = editNode(nodes[index]); !status) {
            return Core::failure(std::move(status.error()));
        }
        const auto& after = nodes[index];
        const bool changed = after.meshId != before.meshId ||
                             after.materialId != before.materialId ||
                             after.visible != before.visible;
        if (changed) {
            ++changedCount;
            if (primaryStableId == 0) {
                primaryStableId = after.stableNodeId;
            }
        }
    }
    if (changedCount == 0) {
        return EditorSceneOperationResult{
            .primaryStableId = stableNodeIds.front(),
            .affectedItemCount = 0,
        };
    }
    if (auto status = document.replace({.nodes = nodes}); !status) {
        return Core::failure(std::move(status.error()));
    }
    return EditorSceneOperationResult{
        .primaryStableId = primaryStableId,
        .affectedItemCount = changedCount,
    };
}
catch (const std::bad_alloc&)
{
    return nodePropertyAllocationFailure();
}

} // namespace

Core::Result<EditorSceneOperationResult>
applyWorld2DSpriteNodeProperties(World2DAuthoringDocument& document,
                       std::span<const Core::u32> stableEntityIds,
                       const World2DSpriteNodeProperties& input)
{
    if (auto status = finiteEditStatus(allFinite({
            input.sizeX.value_or(1.0F),
            input.sizeY.value_or(1.0F),
            input.pivotX.value_or(0.5F),
            input.pivotY.value_or(0.5F),
            input.uvU0.value_or(0.0F),
            input.uvV0.value_or(0.0F),
            input.uvU1.value_or(1.0F),
            input.uvV1.value_or(1.0F),
        })); !status) {
        return Core::failure(std::move(status.error()));
    }
    if (input.spriteId.has_value() && !*input.spriteId) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Sprite2D properties require a non-zero sprite AssetId");
    }
    return editWorld2DEntities(
        document, stableEntityIds,
        [&](AssetFormat::World2DEntityDesc& entity) -> Core::Status {
            if (auto status = requireWorld2DNodeKind(
                    entity,
                    {World2DNodeTemplate::Sprite2D,
                     World2DNodeTemplate::AnimatedSprite2D},
                    "Rendering properties require a Sprite2D or AnimatedSprite2D node");
                !status) {
                return status;
            }
            auto& sprite = *entity.sprite;
            applyOptional(input.spriteId, sprite.spriteId);
            applyOptional(input.sizeX, sprite.sizeX);
            applyOptional(input.sizeY, sprite.sizeY);
            applyOptional(input.pivotX, sprite.pivotX);
            applyOptional(input.pivotY, sprite.pivotY);
            if (input.sizeX.has_value() || input.sizeY.has_value()) {
                sprite.overrides =
                    sprite.overrides | AssetFormat::World2DSpriteOverrideFlags::Size;
            }
            if (input.pivotX.has_value() || input.pivotY.has_value()) {
                sprite.overrides =
                    sprite.overrides | AssetFormat::World2DSpriteOverrideFlags::Pivot;
            }
            applyOptional(input.uvU0, sprite.uvU0);
            applyOptional(input.uvV0, sprite.uvV0);
            applyOptional(input.uvU1, sprite.uvU1);
            applyOptional(input.uvV1, sprite.uvV1);
            if (input.uvU0.has_value() || input.uvV0.has_value() ||
                input.uvU1.has_value() || input.uvV1.has_value()) {
                sprite.overrides =
                    sprite.overrides | AssetFormat::World2DSpriteOverrideFlags::UvRect;
                // Rejected here rather than at the wire layer so the message names
                // the authoring rule; the document keeps its previous bytes.
                if (!(sprite.uvU0 >= 0.0F) || !(sprite.uvV0 >= 0.0F) ||
                    !(sprite.uvU1 <= 1.0F) || !(sprite.uvV1 <= 1.0F) ||
                    !(sprite.uvU0 < sprite.uvU1) ||
                    !(sprite.uvV0 < sprite.uvV1)) {
                    return Core::failure(
                        EditorErrorCode::InvalidAuthoringOperation,
                        "Sprite UV rect must satisfy 0 <= u0 < u1 <= 1 and 0 <= v0 < v1 <= 1");
                }
            }
            if (input.color.has_value()) {
                sprite.colorRed = (*input.color)[0];
                sprite.colorGreen = (*input.color)[1];
                sprite.colorBlue = (*input.color)[2];
                sprite.colorAlpha = (*input.color)[3];
            }
            applyOptional(input.sortingLayer, sprite.sortingLayer);
            applyOptional(input.orderInLayer, sprite.orderInLayer);
            applyOptional(input.flipX, sprite.flipX);
            applyOptional(input.flipY, sprite.flipY);
            applyOptional(input.visible, sprite.visible);
            return Core::success();
        });
}

Core::Result<EditorSceneOperationResult>
applyWorld2DCameraNodeProperties(World2DAuthoringDocument& document,
                       std::span<const Core::u32> stableEntityIds,
                       const World2DCameraNodeProperties& input)
{
    if (auto status = finiteEditStatus(allFinite({
            input.viewportX.value_or(0.0F),
            input.viewportY.value_or(0.0F),
            input.viewportWidth.value_or(1.0F),
            input.viewportHeight.value_or(1.0F),
            input.fixedWorldHeightMeters.value_or(18.0F),
            input.referencePixelsPerMeter.value_or(16.0F),
        })); !status) {
        return Core::failure(std::move(status.error()));
    }
    return editWorld2DEntities(
        document, stableEntityIds,
        [&](AssetFormat::World2DEntityDesc& entity) -> Core::Status {
            if (auto status = requireWorld2DNodeKind(
                    entity, {World2DNodeTemplate::Camera2D},
                    "Camera properties require a Camera2D node");
                !status) {
                return status;
            }
            auto& camera = *entity.camera;
            applyOptional(input.projection, camera.projection);
            applyOptional(input.pixelSnap, camera.pixelSnap);
            applyOptional(input.viewportX, camera.viewportX);
            applyOptional(input.viewportY, camera.viewportY);
            applyOptional(input.viewportWidth, camera.viewportWidth);
            applyOptional(input.viewportHeight, camera.viewportHeight);
            applyOptional(input.fixedWorldHeightMeters, camera.fixedWorldHeightMeters);
            applyOptional(input.referencePixelsPerMeter, camera.referencePixelsPerMeter);
            applyOptional(input.referenceHeightPixels, camera.referenceHeightPixels);
            applyOptional(input.active, camera.active);
            return Core::success();
        });
}

Core::Result<EditorSceneOperationResult>
applyWorld2DPointLightNodeProperties(World2DAuthoringDocument& document,
                           std::span<const Core::u32> stableEntityIds,
                           const World2DPointLightNodeProperties& input)
{
    if (auto status = finiteEditStatus(allFinite({
            input.colorRed.value_or(1.0F),
            input.colorGreen.value_or(1.0F),
            input.colorBlue.value_or(1.0F),
            input.colorAlpha.value_or(1.0F),
            input.intensity.value_or(1.0F),
            input.radiusMeters.value_or(4.0F),
            input.sourceRadiusMeters.value_or(0.0F),
        })); !status) {
        return Core::failure(std::move(status.error()));
    }
    return editWorld2DEntities(
        document, stableEntityIds,
        [&](AssetFormat::World2DEntityDesc& entity) -> Core::Status {
            if (auto status = requireWorld2DNodeKind(
                    entity, {World2DNodeTemplate::PointLight2D},
                    "Light properties require a PointLight2D node");
                !status) {
                return status;
            }
            auto& light = *entity.pointLight;
            applyOptional(input.colorRed, light.colorRed);
            applyOptional(input.colorGreen, light.colorGreen);
            applyOptional(input.colorBlue, light.colorBlue);
            applyOptional(input.colorAlpha, light.colorAlpha);
            applyOptional(input.intensity, light.intensity);
            applyOptional(input.radiusMeters, light.radiusMeters);
            applyOptional(input.sourceRadiusMeters, light.sourceRadiusMeters);
            applyOptional(input.active, light.active);
            return Core::success();
        });
}

Core::Result<EditorSceneOperationResult>
applyWorld2DShadowOccluderNodeProperties(World2DAuthoringDocument& document,
                               std::span<const Core::u32> stableEntityIds,
                               const World2DShadowOccluderNodeProperties& input)
{
    if (auto status = finiteEditStatus(allFinite({
            input.localStartX.value_or(-0.5F),
            input.localStartY.value_or(0.0F),
            input.localEndX.value_or(0.5F),
            input.localEndY.value_or(0.0F),
        })); !status) {
        return Core::failure(std::move(status.error()));
    }
    return editWorld2DEntities(
        document, stableEntityIds,
        [&](AssetFormat::World2DEntityDesc& entity) -> Core::Status {
            if (auto status = requireWorld2DNodeKind(
                    entity, {World2DNodeTemplate::ShadowOccluder2D},
                    "Occlusion properties require a ShadowOccluder2D node");
                !status) {
                return status;
            }
            auto& occluder = *entity.shadowOccluder;
            applyOptional(input.localStartX, occluder.localStartX);
            applyOptional(input.localStartY, occluder.localStartY);
            applyOptional(input.localEndX, occluder.localEndX);
            applyOptional(input.localEndY, occluder.localEndY);
            applyOptional(input.active, occluder.active);
            return Core::success();
        });
}

Core::Result<EditorSceneOperationResult>
applyWorld2DAnimatedSpriteNodeProperties(World2DAuthoringDocument& document,
                                std::span<const Core::u32> stableEntityIds,
                                const World2DAnimatedSpriteNodeProperties& input)
{
    if (auto status = finiteEditStatus(
            allFinite({input.playbackSpeed.value_or(1.0F)})); !status) {
        return Core::failure(std::move(status.error()));
    }
    if (input.playbackSpeed.has_value() && *input.playbackSpeed <= 0.0F) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "AnimatedSprite2D playback speed must be greater than zero");
    }
    if (input.clipId.has_value() && !*input.clipId) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Editor sprite animation requires a non-zero clip AssetId");
    }
    return editWorld2DEntities(
        document, stableEntityIds,
        [&](AssetFormat::World2DEntityDesc& entity) -> Core::Status {
            if (auto status = requireWorld2DNodeKind(
                    entity, {World2DNodeTemplate::AnimatedSprite2D},
                    "Animation properties require an AnimatedSprite2D node");
                !status) {
                return status;
            }
            auto& animation = *entity.spriteAnimation;
            applyOptional(input.clipId, animation.clipId);
            applyOptional(input.playbackSpeed, animation.playbackSpeed);
            applyOptional(input.autoPlay, animation.autoPlay);
            return Core::success();
        });
}

Core::Result<EditorSceneOperationResult>
applyWorld2DPhysicsBodyNodeProperties(
    World2DAuthoringDocument& document,
    std::span<const Core::u32> stableEntityIds,
    const World2DPhysicsBodyNodeProperties& input)
{
    if (auto status = validatePhysicsBodyInput(input); !status) {
        return Core::failure(std::move(status.error()));
    }
    return editWorld2DEntities(
        document, stableEntityIds,
        [&](AssetFormat::World2DEntityDesc& entity) -> Core::Status {
            if (auto status = requireWorld2DNodeKind(
                    entity,
                    {World2DNodeTemplate::StaticBody2D,
                     World2DNodeTemplate::RigidBody2D,
                     World2DNodeTemplate::CharacterBody2D,
                     World2DNodeTemplate::Area2D},
                    "Physics body properties require a physics body node");
                !status) {
                return status;
            }
            auto& body = *entity.physicsBody;
            applyOptional(input.linearVelocityX, body.linearVelocityX);
            applyOptional(input.linearVelocityY, body.linearVelocityY);
            applyOptional(input.angularVelocityRadiansPerSecond,
                          body.angularVelocityRadiansPerSecond);
            applyOptional(input.linearDamping, body.linearDamping);
            applyOptional(input.angularDamping, body.angularDamping);
            applyOptional(input.gravityScale, body.gravityScale);
            applyOptional(input.enabled, body.enabled);
            applyOptional(input.enableSleep, body.enableSleep);
            applyOptional(input.initiallyAwake, body.initiallyAwake);
            applyOptional(input.fixedRotation, body.fixedRotation);
            applyOptional(input.continuousCollision, body.continuousCollision);
            return Core::success();
        });
}

Core::Result<EditorSceneOperationResult>
applyWorld2DPhysicsShapeNodeProperties(
    World2DAuthoringDocument& document,
    std::span<const Core::u32> stableEntityIds,
    const World2DPhysicsShapeNodeProperties& input)
{
    if (auto status = validatePhysicsShapeInput(input); !status) {
        return Core::failure(std::move(status.error()));
    }
    return editWorld2DEntities(
        document, stableEntityIds,
        [&](AssetFormat::World2DEntityDesc& entity) -> Core::Status {
            if (auto status = requireWorld2DNodeKind(
                    entity, {World2DNodeTemplate::CollisionShape2D},
                    "Physics shape properties require a CollisionShape2D node");
                !status) {
                return status;
            }
            auto& shape = *entity.physicsShape;
            applyOptional(input.kind, shape.kind);
            applyOptional(input.halfExtentX, shape.halfExtentX);
            applyOptional(input.halfExtentY, shape.halfExtentY);
            applyOptional(input.radius, shape.radius);
            applyOptional(input.localCenterX, shape.localCenterX);
            applyOptional(input.localCenterY, shape.localCenterY);
            applyOptional(input.localAngleRadians, shape.localAngleRadians);
            applyOptional(input.localPointAX, shape.localPointAX);
            applyOptional(input.localPointAY, shape.localPointAY);
            applyOptional(input.localPointBX, shape.localPointBX);
            applyOptional(input.localPointBY, shape.localPointBY);
            applyOptional(input.density, shape.density);
            applyOptional(input.friction, shape.friction);
            applyOptional(input.restitution, shape.restitution);
            applyOptional(input.enabled, shape.enabled);
            applyOptional(input.sensor, shape.sensor);
            applyOptional(input.sensorEvents, shape.sensorEvents);
            applyOptional(input.contactEvents, shape.contactEvents);
            applyOptional(input.hitEvents, shape.hitEvents);
            if ((shape.kind == AssetFormat::World2DPhysicsShapeKind::Box &&
                 (!(shape.halfExtentX > 0.0F) || !(shape.halfExtentY > 0.0F))) ||
                ((shape.kind == AssetFormat::World2DPhysicsShapeKind::Circle ||
                  shape.kind == AssetFormat::World2DPhysicsShapeKind::Capsule) &&
                 !(shape.radius > 0.0F))) {
                return Core::failure(
                    EditorErrorCode::InvalidAuthoringOperation,
                    "Physics shape dimensions are invalid for the selected kind");
            }
            return Core::success();
        });
}

Core::Result<EditorSceneOperationResult>
applyWorld2DResourceNodeProperties(World2DAuthoringDocument& document,
                       std::span<const Core::u32> stableEntityIds,
                       const World2DResourceNodeProperties& input)
{
    if (input.assetId.has_value() && !*input.assetId) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Resource node properties require a non-zero asset AssetId");
    }
    return editWorld2DEntities(
        document, stableEntityIds,
        [&](AssetFormat::World2DEntityDesc& entity) -> Core::Status {
            if (auto status = requireWorld2DNodeKind(
                    entity,
                    {World2DNodeTemplate::TileMap2D,
                     World2DNodeTemplate::FxEmitter2D,
                     World2DNodeTemplate::NavigationRegion2D,
                     World2DNodeTemplate::AudioPlayer2D},
                    "Resource properties require a TileMap2D, FxEmitter2D, "
                    "NavigationRegion2D, or AudioPlayer2D node");
                !status) {
                return status;
            }
            auto& resource = *entity.resource;
            applyOptional(input.assetId, resource.assetId);
            applyOptional(input.active, resource.active);
            return Core::success();
        });
}

Core::Result<EditorSceneOperationResult>
applyWorld3DMeshNodeProperties(World3DAuthoringDocument& document,
                             std::span<const Core::u32> stableNodeIds,
                             const World3DMeshNodeProperties& input)
{
    if ((input.meshId.has_value() && !*input.meshId) ||
        (input.materialId.has_value() && !*input.materialId)) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Mesh3D properties require non-zero asset IDs");
    }
    return editWorld3DNodes(
        document, stableNodeIds,
        [&](AssetFormat::PrefabNodeDesc& node) -> Core::Status {
            if (!node.meshId || !node.materialId) {
                return Core::failure(
                    EditorErrorCode::NodePropertyUnavailable,
                    "Rendering properties require a Mesh3D node");
            }
            applyOptional(input.meshId, node.meshId);
            applyOptional(input.materialId, node.materialId);
            applyOptional(input.visible, node.visible);
            return Core::success();
        });
}

} // namespace Tina::Editor
