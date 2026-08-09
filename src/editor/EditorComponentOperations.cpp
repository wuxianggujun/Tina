#include <tina/editor/EditorComponentOperations.hpp>

#include <tina/editor/EditorErrors.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <new>
#include <utility>
#include <vector>

namespace Tina::Editor {
namespace {

inline constexpr std::array<EditorComponentInfo, World2DComponentKindCount>
    World2DComponentRegistry{{
        {.kind = World2DComponentKind::Sprite,
         .displayName = "SpriteRenderer2D",
         .removable = true},
        {.kind = World2DComponentKind::Camera,
         .displayName = "Camera2D",
         .removable = true},
        {.kind = World2DComponentKind::PointLight,
         .displayName = "PointLight2D",
         .removable = true},
        {.kind = World2DComponentKind::ShadowOccluder,
         .displayName = "ShadowOccluder2D",
         .removable = true},
    }};

[[nodiscard]] std::unexpected<Core::Error> componentOperationAllocationFailure()
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory,
                         "Editor component operation allocation failed");
}

[[nodiscard]] bool isKnownWorld2DComponentKind(World2DComponentKind kind) noexcept
{
    return static_cast<Core::u8>(kind) < World2DComponentKindCount;
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
                                 "Editor component operation target was not found");
        }
        const auto index =
            static_cast<Core::usize>(std::distance(items.begin(), found));
        if (std::find(indices.begin(), indices.end(), index) == indices.end()) {
            indices.push_back(index);
        }
    }
    return indices;
}

[[nodiscard]] Core::Status validateWorld2DComponentSelection(
    std::span<const Core::u32> stableEntityIds, World2DComponentKind kind) noexcept
{
    if (!isKnownWorld2DComponentKind(kind)) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Editor component kind is unknown");
    }
    if (stableEntityIds.empty()) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Editor component operation requires a selection");
    }
    return Core::success();
}

void attachDefaultWorld2DComponent(AssetFormat::World2DEntityDesc& entity,
                                   World2DComponentKind kind,
                                   Core::AssetId spriteAssetId)
{
    switch (kind) {
    case World2DComponentKind::Sprite: {
        auto& sprite = entity.sprite.emplace();
        sprite.spriteId = spriteAssetId;
        break;
    }
    case World2DComponentKind::Camera:
        entity.camera.emplace();
        break;
    case World2DComponentKind::PointLight:
        entity.pointLight.emplace();
        break;
    case World2DComponentKind::ShadowOccluder:
        entity.shadowOccluder.emplace();
        break;
    }
}

void detachWorld2DComponent(AssetFormat::World2DEntityDesc& entity,
                            World2DComponentKind kind) noexcept
{
    switch (kind) {
    case World2DComponentKind::Sprite:
        entity.sprite.reset();
        break;
    case World2DComponentKind::Camera:
        entity.camera.reset();
        break;
    case World2DComponentKind::PointLight:
        entity.pointLight.reset();
        break;
    case World2DComponentKind::ShadowOccluder:
        entity.shadowOccluder.reset();
        break;
    }
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
                             "Editor component operation requires a selection");
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
    return componentOperationAllocationFailure();
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
                             "Editor component edit rejects non-finite values");
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
                             "Editor component operation requires a selection");
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
    return componentOperationAllocationFailure();
}

} // namespace

std::span<const EditorComponentInfo> world2DComponentRegistry() noexcept
{
    return World2DComponentRegistry;
}

bool hasWorld2DComponent(const AssetFormat::World2DEntityDesc& entity,
                         World2DComponentKind kind) noexcept
{
    switch (kind) {
    case World2DComponentKind::Sprite:
        return entity.sprite.has_value();
    case World2DComponentKind::Camera:
        return entity.camera.has_value();
    case World2DComponentKind::PointLight:
        return entity.pointLight.has_value();
    case World2DComponentKind::ShadowOccluder:
        return entity.shadowOccluder.has_value();
    }
    return false;
}

Core::Result<EditorSceneOperationResult>
addWorld2DComponent(World2DAuthoringDocument& document,
                    std::span<const Core::u32> stableEntityIds,
                    World2DComponentKind kind, Core::AssetId spriteAssetId)
{
    if (auto status = validateWorld2DComponentSelection(stableEntityIds, kind);
        !status) {
        return Core::failure(std::move(status.error()));
    }
    if (kind == World2DComponentKind::Sprite && !spriteAssetId) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Editor sprite component requires a non-zero sprite AssetId");
    }
    bool anyMissing = false;
    auto result = editWorld2DEntities(
        document, stableEntityIds,
        [&](AssetFormat::World2DEntityDesc& entity) -> Core::Status {
            if (!hasWorld2DComponent(entity, kind)) {
                anyMissing = true;
                attachDefaultWorld2DComponent(entity, kind, spriteAssetId);
            }
            return Core::success();
        });
    if (result && !anyMissing) {
        return Core::failure(
            EditorErrorCode::ComponentAlreadyPresent,
            "Editor component is already present on every selected entity");
    }
    return result;
}

Core::Result<EditorSceneOperationResult>
removeWorld2DComponent(World2DAuthoringDocument& document,
                       std::span<const Core::u32> stableEntityIds,
                       World2DComponentKind kind)
{
    if (auto status = validateWorld2DComponentSelection(stableEntityIds, kind);
        !status) {
        return Core::failure(std::move(status.error()));
    }
    bool anyPresent = false;
    auto result = editWorld2DEntities(
        document, stableEntityIds,
        [&](AssetFormat::World2DEntityDesc& entity) -> Core::Status {
            if (hasWorld2DComponent(entity, kind)) {
                anyPresent = true;
                detachWorld2DComponent(entity, kind);
            }
            return Core::success();
        });
    if (result && !anyPresent) {
        return Core::failure(
            EditorErrorCode::ComponentNotFound,
            "Editor component is missing from every selected entity");
    }
    return result;
}

Core::Result<EditorSceneOperationResult>
applyWorld2DSpriteEdit(World2DAuthoringDocument& document,
                       std::span<const Core::u32> stableEntityIds,
                       const World2DSpriteEditInput& input)
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
            "Editor sprite component requires a non-zero sprite AssetId");
    }
    return editWorld2DEntities(
        document, stableEntityIds,
        [&](AssetFormat::World2DEntityDesc& entity) -> Core::Status {
            if (!entity.sprite) {
                return Core::failure(
                    EditorErrorCode::ComponentNotFound,
                    "Editor sprite edit targets an entity without a sprite");
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
applyWorld2DCameraEdit(World2DAuthoringDocument& document,
                       std::span<const Core::u32> stableEntityIds,
                       const World2DCameraEditInput& input)
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
            if (!entity.camera) {
                return Core::failure(
                    EditorErrorCode::ComponentNotFound,
                    "Editor camera edit targets an entity without a camera");
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
applyWorld2DPointLightEdit(World2DAuthoringDocument& document,
                           std::span<const Core::u32> stableEntityIds,
                           const World2DPointLightEditInput& input)
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
            if (!entity.pointLight) {
                return Core::failure(
                    EditorErrorCode::ComponentNotFound,
                    "Editor point light edit targets an entity without a light");
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
applyWorld2DShadowOccluderEdit(World2DAuthoringDocument& document,
                               std::span<const Core::u32> stableEntityIds,
                               const World2DShadowOccluderEditInput& input)
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
            if (!entity.shadowOccluder) {
                return Core::failure(
                    EditorErrorCode::ComponentNotFound,
                    "Editor occluder edit targets an entity without an occluder");
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

bool hasWorld3DMeshRenderer(const AssetFormat::PrefabNodeView& node) noexcept
{
    return node.hasMesh && node.hasMaterial;
}

Core::Result<EditorSceneOperationResult>
addWorld3DMeshRenderer(World3DAuthoringDocument& document,
                       std::span<const Core::u32> stableNodeIds,
                       Core::AssetId meshId, Core::AssetId materialId)
{
    if (!meshId || !materialId) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Editor mesh renderer requires paired mesh and material assets");
    }
    bool anyMissing = false;
    auto result = editWorld3DNodes(
        document, stableNodeIds,
        [&](AssetFormat::PrefabNodeDesc& node) -> Core::Status {
            if (!node.meshId || !node.materialId) {
                anyMissing = true;
                node.meshId = meshId;
                node.materialId = materialId;
            }
            return Core::success();
        });
    if (result && !anyMissing) {
        return Core::failure(
            EditorErrorCode::ComponentAlreadyPresent,
            "Editor mesh renderer is already present on every selected node");
    }
    return result;
}

Core::Result<EditorSceneOperationResult>
removeWorld3DMeshRenderer(World3DAuthoringDocument& document,
                          std::span<const Core::u32> stableNodeIds)
{
    bool anyPresent = false;
    auto result = editWorld3DNodes(
        document, stableNodeIds,
        [&](AssetFormat::PrefabNodeDesc& node) -> Core::Status {
            if (node.meshId || node.materialId) {
                anyPresent = true;
                node.meshId = {};
                node.materialId = {};
            }
            return Core::success();
        });
    if (result && !anyPresent) {
        return Core::failure(
            EditorErrorCode::ComponentNotFound,
            "Editor mesh renderer is missing from every selected node");
    }
    return result;
}

Core::Result<EditorSceneOperationResult>
applyWorld3DMeshRendererEdit(World3DAuthoringDocument& document,
                             std::span<const Core::u32> stableNodeIds,
                             const World3DMeshRendererEditInput& input)
{
    if ((input.meshId.has_value() && !*input.meshId) ||
        (input.materialId.has_value() && !*input.materialId)) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Editor mesh renderer edit requires non-zero asset IDs");
    }
    return editWorld3DNodes(
        document, stableNodeIds,
        [&](AssetFormat::PrefabNodeDesc& node) -> Core::Status {
            if (!node.meshId || !node.materialId) {
                return Core::failure(
                    EditorErrorCode::ComponentNotFound,
                    "Editor mesh renderer edit targets a node without one");
            }
            applyOptional(input.meshId, node.meshId);
            applyOptional(input.materialId, node.materialId);
            applyOptional(input.visible, node.visible);
            return Core::success();
        });
}

} // namespace Tina::Editor
