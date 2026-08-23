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
