#include <tina/editor/EditorSceneOperations.hpp>

#include <tina/editor/EditorErrors.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace Tina::Editor {
namespace {

template <typename Item, typename IdResolver>
[[nodiscard]] Core::Result<Core::u32>
allocateStableId(std::span<const Item> items, IdResolver&& resolveId,
                 std::span<const Core::u32> reserved = {})
{
    Core::u32 maximumId = 0;
    for (const Item& item : items) {
        maximumId = (std::max)(maximumId, resolveId(item));
    }
    for (const Core::u32 id : reserved) {
        maximumId = (std::max)(maximumId, id);
    }
    if (maximumId != (std::numeric_limits<Core::u32>::max)()) {
        return maximumId + 1U;
    }
    return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                         "Editor scene has exhausted stable item identities");
}

[[nodiscard]] std::unexpected<Core::Error> sceneOperationAllocationFailure()
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory,
                         "Editor scene operation allocation failed");
}

// Indexed by the template enumerator, so the enum order is the presentation
// order. Display names match the hierarchy label vocabulary exactly.
inline constexpr std::array<EditorNodeTemplateInfo, World2DNodeTemplateCount>
    World2DNodeTemplateRegistry{{
        {.displayName = "Entity2D",
         .description = "Transform only. Start here and add components later."},
        {.displayName = "Sprite2D",
         .description = "Draws a sprite. Needs a Sprite asset.",
         .requiresSpriteAsset = true},
        {.displayName = "AnimatedSprite2D",
         .description = "Sprite driven by an animation clip.",
         .requiresSpriteAsset = true,
         .requiresAnimationClipAsset = true},
        {.displayName = "Camera2D",
         .description = "Defines what the 2D viewport renders."},
        {.displayName = "PointLight2D",
         .description = "Emits 2D light within a radius."},
        {.displayName = "ShadowOccluder2D",
         .description = "Blocks 2D light along a segment."},
    }};

inline constexpr std::array<EditorNodeTemplateInfo, World3DNodeTemplateCount>
    World3DNodeTemplateRegistry{{
        {.displayName = "Node3D",
         .description = "Transform only. Start here and add a mesh later."},
        {.displayName = "Mesh3D",
         .description = "Renders a mesh. Needs mesh and material assets.",
         .requiresMeshAssets = true},
    }};

[[nodiscard]] const AssetFormat::World2DEntityDesc*
findWorld2DEntity(std::span<const AssetFormat::World2DEntityDesc> entities,
                  Core::u32 stableId) noexcept
{
    const auto found = std::find_if(
        entities.begin(), entities.end(), [stableId](const auto& entity) {
            return entity.stableEntityId == stableId;
        });
    return found != entities.end() ? &*found : nullptr;
}

[[nodiscard]] bool isWorld2DDescendantOrSelf(
    std::span<const AssetFormat::World2DEntityDesc> entities,
    Core::u32 candidateId, Core::u32 ancestorId) noexcept
{
    Core::u32 cursor = candidateId;
    for (Core::usize depth = 0; depth <= entities.size(); ++depth) {
        if (cursor == ancestorId) {
            return true;
        }
        const auto* entity = findWorld2DEntity(entities, cursor);
        if (entity == nullptr || entity->parentStableEntityId == 0) {
            return false;
        }
        cursor = entity->parentStableEntityId;
    }
    return false;
}

[[nodiscard]] Core::Status publishWorld2DHierarchy(
    World2DAuthoringDocument& document,
    std::vector<AssetFormat::World2DEntityDesc> entities,
    const AssetFormat::World2DSnapshotView& source)
{
    std::vector<AssetFormat::World2DEntityDesc> ordered;
    ordered.reserve(entities.size());
    while (ordered.size() != entities.size()) {
        bool progressed = false;
        for (const auto& entity : entities) {
            const bool alreadyPublished = std::any_of(
                ordered.begin(), ordered.end(), [&](const auto& candidate) {
                    return candidate.stableEntityId == entity.stableEntityId;
                });
            if (alreadyPublished) {
                continue;
            }
            const bool parentPublished = entity.parentStableEntityId == 0 ||
                std::any_of(ordered.begin(), ordered.end(), [&](const auto& candidate) {
                    return candidate.stableEntityId == entity.parentStableEntityId;
                });
            if (parentPublished) {
                ordered.push_back(entity);
                progressed = true;
            }
        }
        if (!progressed) {
            return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                 "Editor scene reparenting would create a hierarchy cycle");
        }
    }
    return document.replace({
        .entities = ordered,
        .gameplaySchema = source.gameplaySchema,
        .gameplayVersion = source.gameplayVersion,
        .gameplayBytes = source.gameplayBytes,
    });
}

[[nodiscard]] AssetFormat::PrefabNodeDesc
toPrefabNodeDesc(const AssetFormat::PrefabNodeView& node) noexcept
{
    return {
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
    };
}

[[nodiscard]] Core::i32 findPrefabNodeIndex(
    std::span<const AssetFormat::PrefabNodeView> nodes,
    Core::u32 stableId) noexcept
{
    const auto found = std::find_if(
        nodes.begin(), nodes.end(), [stableId](const auto& node) {
            return node.stableNodeId == stableId;
        });
    return found == nodes.end()
               ? -1
               : static_cast<Core::i32>(std::distance(nodes.begin(), found));
}

[[nodiscard]] bool isPrefabDescendantOrSelf(
    std::span<const AssetFormat::PrefabNodeView> nodes,
    Core::usize candidateIndex, Core::usize ancestorIndex) noexcept
{
    Core::i32 cursor = static_cast<Core::i32>(candidateIndex);
    for (Core::usize depth = 0; depth <= nodes.size() && cursor >= 0; ++depth) {
        if (static_cast<Core::usize>(cursor) == ancestorIndex) {
            return true;
        }
        cursor = nodes[static_cast<Core::usize>(cursor)].parentIndex;
    }
    return false;
}

struct PrefabHierarchyNode final {
    AssetFormat::PrefabNodeDesc node{};
    Core::u32 parentStableId = 0;
};

[[nodiscard]] Core::Status publishPrefabHierarchy(
    World3DAuthoringDocument& document,
    std::vector<PrefabHierarchyNode> nodes)
{
    std::vector<AssetFormat::PrefabNodeDesc> ordered;
    ordered.reserve(nodes.size());
    while (ordered.size() != nodes.size()) {
        bool progressed = false;
        for (const auto& item : nodes) {
            const bool alreadyPublished = std::any_of(
                ordered.begin(), ordered.end(), [&](const auto& candidate) {
                    return candidate.stableNodeId == item.node.stableNodeId;
                });
            if (alreadyPublished) {
                continue;
            }
            Core::i32 parentIndex = -1;
            if (item.parentStableId != 0) {
                const auto parent = std::find_if(
                    ordered.begin(), ordered.end(), [&](const auto& candidate) {
                        return candidate.stableNodeId == item.parentStableId;
                    });
                if (parent == ordered.end()) {
                    continue;
                }
                parentIndex = static_cast<Core::i32>(
                    std::distance(ordered.begin(), parent));
            }
            auto candidate = item.node;
            candidate.parentIndex = parentIndex;
            ordered.push_back(candidate);
            progressed = true;
        }
        if (!progressed) {
            return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                 "Editor scene reparenting would create a hierarchy cycle");
        }
    }
    return document.replace({.nodes = ordered});
}

} // namespace

std::span<const EditorNodeTemplateInfo> world2DNodeTemplateRegistry() noexcept
{
    return World2DNodeTemplateRegistry;
}

std::span<const EditorNodeTemplateInfo> world3DNodeTemplateRegistry() noexcept
{
    return World3DNodeTemplateRegistry;
}

World2DNodeTemplate classifyWorld2DEntityTemplate(
    const AssetFormat::World2DEntityDesc& entity) noexcept
{
    // Most specific first: an animated sprite also carries a sprite, and a
    // camera or light is a more useful label than the occluder it may also own.
    if (entity.spriteAnimation.has_value()) {
        return World2DNodeTemplate::AnimatedSprite;
    }
    if (entity.camera.has_value()) {
        return World2DNodeTemplate::Camera;
    }
    if (entity.pointLight.has_value()) {
        return World2DNodeTemplate::PointLight;
    }
    if (entity.sprite.has_value()) {
        return World2DNodeTemplate::Sprite;
    }
    if (entity.shadowOccluder.has_value()) {
        return World2DNodeTemplate::ShadowOccluder;
    }
    return World2DNodeTemplate::Empty;
}

World3DNodeTemplate classifyWorld3DNodeTemplate(
    const AssetFormat::PrefabNodeView& node) noexcept
{
    return node.hasMesh ? World3DNodeTemplate::Mesh : World3DNodeTemplate::Empty;
}

Core::Result<EditorSceneOperationResult>
addWorld2DEntityOfTemplate(World2DAuthoringDocument& document,
                           World2DNodeTemplate nodeTemplate,
                           Core::u32 parentStableId,
                           const World2DNodeTemplateAssets& assets)
try
{
    if (static_cast<Core::usize>(nodeTemplate) >= World2DNodeTemplateCount) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Editor scene node template is unknown");
    }
    const EditorNodeTemplateInfo& info =
        World2DNodeTemplateRegistry[static_cast<Core::usize>(nodeTemplate)];
    if (info.requiresSpriteAsset && !assets.spriteId) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Editor scene node template requires a Sprite asset");
    }
    if (info.requiresAnimationClipAsset && !assets.animationClipId) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Editor scene node template requires a SpriteAnimationClip asset");
    }

    std::vector<AssetFormat::World2DEntityDesc> storage;
    auto snapshot = document.parseCurrentSnapshot(storage);
    if (!snapshot) {
        return Core::failure(std::move(snapshot.error()));
    }
    if (parentStableId != 0 &&
        findWorld2DEntity(storage, parentStableId) == nullptr) {
        return Core::failure(EditorErrorCode::EntityNotFound,
                             "Editor scene parent entity was not found");
    }
    if (storage.size() >= document.config().entityCapacity) {
        return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                             "Editor scene entity capacity is exhausted");
    }
    auto stableId = allocateStableId(
        std::span<const AssetFormat::World2DEntityDesc>{storage},
        [](const auto& entity) { return entity.stableEntityId; });
    if (!stableId) {
        return Core::failure(std::move(stableId.error()));
    }

    // Built as one desc so the whole node, components included, publishes as a
    // single canonical revision and undoes in one step.
    AssetFormat::World2DEntityDesc created{
        .stableEntityId = *stableId,
        .parentStableEntityId = parentStableId,
    };
    switch (nodeTemplate) {
    case World2DNodeTemplate::Empty:
        break;
    case World2DNodeTemplate::Sprite:
        created.sprite.emplace().spriteId = assets.spriteId;
        break;
    case World2DNodeTemplate::AnimatedSprite:
        created.sprite.emplace().spriteId = assets.spriteId;
        created.spriteAnimation.emplace().clipId = assets.animationClipId;
        break;
    case World2DNodeTemplate::Camera:
        created.camera.emplace();
        break;
    case World2DNodeTemplate::PointLight:
        created.pointLight.emplace();
        break;
    case World2DNodeTemplate::ShadowOccluder:
        created.shadowOccluder.emplace();
        break;
    }
    if (auto status = document.upsertEntity(created); !status) {
        return Core::failure(std::move(status.error()));
    }
    return EditorSceneOperationResult{
        .primaryStableId = *stableId,
        .affectedItemCount = 1,
    };
}
catch (const std::bad_alloc&)
{
    return sceneOperationAllocationFailure();
}

Core::Result<EditorSceneOperationResult>
addWorld2DEntity(World2DAuthoringDocument& document,
                 Core::u32 parentStableId)
{
    return addWorld2DEntityOfTemplate(document, World2DNodeTemplate::Empty,
                                      parentStableId);
}

Core::Result<EditorSceneOperationResult>
duplicateWorld2DEntitySubtree(World2DAuthoringDocument& document,
                              Core::u32 stableEntityId)
try
{
    std::vector<AssetFormat::World2DEntityDesc> storage;
    auto snapshot = document.parseCurrentSnapshot(storage);
    if (!snapshot) {
        return Core::failure(std::move(snapshot.error()));
    }
    if (findWorld2DEntity(storage, stableEntityId) == nullptr) {
        return Core::failure(EditorErrorCode::EntityNotFound,
                             "Editor scene entity to duplicate was not found");
    }

    const Core::usize subtreeSize = static_cast<Core::usize>(std::count_if(
        storage.begin(), storage.end(), [&](const auto& entity) {
            return isWorld2DDescendantOrSelf(storage, entity.stableEntityId,
                                             stableEntityId);
        }));
    if (storage.size() > document.config().entityCapacity ||
        subtreeSize > document.config().entityCapacity - storage.size()) {
        return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                             "Duplicated Editor scene subtree exceeds entity capacity");
    }

    std::vector<AssetFormat::World2DEntityDesc> duplicates;
    std::vector<Core::u32> sourceIds;
    std::vector<Core::u32> duplicateIds;
    duplicates.reserve(subtreeSize);
    sourceIds.reserve(subtreeSize);
    duplicateIds.reserve(subtreeSize);
    for (const auto& entity : storage) {
        if (!isWorld2DDescendantOrSelf(storage, entity.stableEntityId,
                                      stableEntityId)) {
            continue;
        }
        auto duplicateId = allocateStableId(
            std::span<const AssetFormat::World2DEntityDesc>{storage},
            [](const auto& candidate) { return candidate.stableEntityId; },
            duplicateIds);
        if (!duplicateId) {
            return Core::failure(std::move(duplicateId.error()));
        }
        auto duplicate = entity;
        duplicate.stableEntityId = *duplicateId;
        if (entity.stableEntityId != stableEntityId) {
            const auto parent = std::find(sourceIds.begin(), sourceIds.end(),
                                          entity.parentStableEntityId);
            if (parent == sourceIds.end()) {
                return Core::failure(
                    EditorErrorCode::InvalidAuthoringOperation,
                    "Editor scene subtree is not in canonical parent-first order");
            }
            duplicate.parentStableEntityId = duplicateIds[
                static_cast<Core::usize>(std::distance(sourceIds.begin(), parent))];
        }
        sourceIds.push_back(entity.stableEntityId);
        duplicateIds.push_back(*duplicateId);
        duplicates.push_back(duplicate);
    }
    storage.insert(storage.end(), duplicates.begin(), duplicates.end());
    if (auto status = document.replace({
            .entities = storage,
            .gameplaySchema = snapshot->gameplaySchema,
            .gameplayVersion = snapshot->gameplayVersion,
            .gameplayBytes = snapshot->gameplayBytes,
        }); !status) {
        return Core::failure(std::move(status.error()));
    }
    return EditorSceneOperationResult{
        .primaryStableId = duplicateIds.front(),
        .affectedItemCount = duplicates.size(),
    };
}
catch (const std::bad_alloc&)
{
    return sceneOperationAllocationFailure();
}

Core::Status reparentWorld2DEntity(World2DAuthoringDocument& document,
                                   Core::u32 stableEntityId,
                                   Core::u32 newParentStableId)
try
{
    std::vector<AssetFormat::World2DEntityDesc> storage;
    auto snapshot = document.parseCurrentSnapshot(storage);
    if (!snapshot) {
        return Core::failure(std::move(snapshot.error()));
    }
    auto target = std::find_if(storage.begin(), storage.end(),
                               [stableEntityId](const auto& entity) {
                                   return entity.stableEntityId == stableEntityId;
                               });
    if (target == storage.end() ||
        (newParentStableId != 0 &&
         findWorld2DEntity(storage, newParentStableId) == nullptr)) {
        return Core::failure(EditorErrorCode::EntityNotFound,
                             "Editor scene reparent item or parent was not found");
    }
    if (newParentStableId == stableEntityId ||
        (newParentStableId != 0 &&
         isWorld2DDescendantOrSelf(storage, newParentStableId,
                                  stableEntityId))) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Editor scene entity cannot be parented to its subtree");
    }
    if (target->parentStableEntityId == newParentStableId) {
        return Core::success();
    }
    target->parentStableEntityId = newParentStableId;
    return publishWorld2DHierarchy(document, std::move(storage), *snapshot);
}
catch (const std::bad_alloc&)
{
    return sceneOperationAllocationFailure();
}

Core::Status reorderWorld2DEntity(World2DAuthoringDocument& document,
                                  Core::u32 stableEntityId,
                                  Core::u32 beforeSiblingStableId)
try
{
    std::vector<AssetFormat::World2DEntityDesc> storage;
    auto snapshot = document.parseCurrentSnapshot(storage);
    if (!snapshot) {
        return Core::failure(std::move(snapshot.error()));
    }
    const auto target = std::find_if(
        storage.begin(), storage.end(), [stableEntityId](const auto& entity) {
            return entity.stableEntityId == stableEntityId;
        });
    if (target == storage.end()) {
        return Core::failure(EditorErrorCode::EntityNotFound,
                             "Editor scene item to reorder was not found");
    }
    if (beforeSiblingStableId == stableEntityId) {
        return Core::success();
    }
    const Core::u32 parentStableId = target->parentStableEntityId;
    if (beforeSiblingStableId != 0) {
        const auto before = std::find_if(
            storage.begin(), storage.end(), [beforeSiblingStableId](const auto& entity) {
                return entity.stableEntityId == beforeSiblingStableId;
            });
        if (before == storage.end()) {
            return Core::failure(EditorErrorCode::EntityNotFound,
                                 "Editor scene reorder sibling was not found");
        }
        if (before->parentStableEntityId != parentStableId) {
            return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                 "Editor scene reorder requires siblings");
        }
    }

    const Core::usize rootIndex = storage.size();
    std::vector<std::vector<Core::usize>> children(storage.size() + 1U);
    for (Core::usize index = 0; index < storage.size(); ++index) {
        Core::usize parentIndex = rootIndex;
        if (storage[index].parentStableEntityId != 0) {
            const auto parent = std::find_if(
                storage.begin(), storage.end(), [&](const auto& entity) {
                    return entity.stableEntityId == storage[index].parentStableEntityId;
                });
            if (parent == storage.end()) {
                return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                     "Editor scene reorder found a missing parent");
            }
            parentIndex = static_cast<Core::usize>(
                std::distance(storage.begin(), parent));
        }
        children[parentIndex].push_back(index);
    }
    const Core::usize targetIndex = static_cast<Core::usize>(
        std::distance(storage.begin(), target));
    const Core::usize parentIndex = parentStableId == 0
        ? rootIndex
        : static_cast<Core::usize>(std::distance(
              storage.begin(), std::find_if(storage.begin(), storage.end(),
                  [parentStableId](const auto& entity) {
                      return entity.stableEntityId == parentStableId;
                  })));
    auto& siblings = children[parentIndex];
    const auto targetSibling = std::find(siblings.begin(), siblings.end(), targetIndex);
    if (targetSibling == siblings.end()) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Editor scene reorder lost the target sibling");
    }
    siblings.erase(targetSibling);
    if (beforeSiblingStableId == 0) {
        siblings.push_back(targetIndex);
    } else {
        const auto beforeSibling = std::find_if(
            siblings.begin(), siblings.end(), [&](Core::usize index) {
                return storage[index].stableEntityId == beforeSiblingStableId;
            });
        if (beforeSibling == siblings.end()) {
            return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                 "Editor scene reorder target is not a sibling");
        }
        siblings.insert(beforeSibling, targetIndex);
    }

    std::vector<AssetFormat::World2DEntityDesc> ordered;
    ordered.reserve(storage.size());
    std::vector<Core::usize> pending;
    pending.reserve(storage.size());
    for (auto root = children[rootIndex].rbegin();
         root != children[rootIndex].rend(); ++root) {
        pending.push_back(*root);
    }
    while (!pending.empty()) {
        const Core::usize index = pending.back();
        pending.pop_back();
        ordered.push_back(storage[index]);
        for (auto child = children[index].rbegin();
             child != children[index].rend(); ++child) {
            pending.push_back(*child);
        }
    }
    if (ordered.size() != storage.size()) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Editor scene reorder produced an invalid hierarchy");
    }
    return document.replace({
        .entities = ordered,
        .gameplaySchema = snapshot->gameplaySchema,
        .gameplayVersion = snapshot->gameplayVersion,
        .gameplayBytes = snapshot->gameplayBytes,
    });
}
catch (const std::bad_alloc&)
{
    return sceneOperationAllocationFailure();
}

Core::Result<EditorSceneOperationResult>
deleteWorld2DEntitySubtree(World2DAuthoringDocument& document,
                           Core::u32 stableEntityId)
try
{
    std::vector<AssetFormat::World2DEntityDesc> storage;
    auto snapshot = document.parseCurrentSnapshot(storage);
    if (!snapshot) {
        return Core::failure(std::move(snapshot.error()));
    }
    const auto* target = findWorld2DEntity(storage, stableEntityId);
    if (target == nullptr) {
        return Core::failure(EditorErrorCode::EntityNotFound,
                             "Editor scene entity to delete was not found");
    }
    const Core::u32 fallbackStableId = target->parentStableEntityId;
    const Core::usize affectedItemCount = static_cast<Core::usize>(std::count_if(
        storage.begin(), storage.end(), [&](const auto& entity) {
            return isWorld2DDescendantOrSelf(storage, entity.stableEntityId,
                                             stableEntityId);
        }));
    if (auto status = document.eraseEntitySubtree(stableEntityId); !status) {
        return Core::failure(std::move(status.error()));
    }
    return EditorSceneOperationResult{
        .primaryStableId = fallbackStableId,
        .affectedItemCount = affectedItemCount,
    };
}
catch (const std::bad_alloc&)
{
    return sceneOperationAllocationFailure();
}

Core::Result<EditorSceneOperationResult>
addWorld3DNodeOfTemplate(World3DAuthoringDocument& document,
                         World3DNodeTemplate nodeTemplate,
                         Core::u32 parentStableId,
                         const World3DNodeTemplateAssets& assets)
try
{
    if (static_cast<Core::usize>(nodeTemplate) >= World3DNodeTemplateCount) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Editor scene node template is unknown");
    }
    const EditorNodeTemplateInfo& info =
        World3DNodeTemplateRegistry[static_cast<Core::usize>(nodeTemplate)];
    if (info.requiresMeshAssets && (!assets.meshId || !assets.materialId)) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Editor scene node template requires paired mesh and material assets");
    }

    std::vector<AssetFormat::PrefabNodeView> storage;
    auto prefab = document.parseCurrentPrefab(storage);
    if (!prefab) {
        return Core::failure(std::move(prefab.error()));
    }
    const Core::i32 parentIndex = parentStableId == 0
        ? -1
        : findPrefabNodeIndex(storage, parentStableId);
    if (parentStableId != 0 && parentIndex < 0) {
        return Core::failure(EditorErrorCode::EntityNotFound,
                             "Editor scene parent node was not found");
    }
    if (storage.size() >= document.config().nodeCapacity) {
        return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                             "Editor scene node capacity is exhausted");
    }
    auto stableId = allocateStableId(
        std::span<const AssetFormat::PrefabNodeView>{storage},
        [](const auto& node) { return node.stableNodeId; });
    if (!stableId) {
        return Core::failure(std::move(stableId.error()));
    }
    const bool withMesh = nodeTemplate == World3DNodeTemplate::Mesh;
    if (auto status = document.upsertNode({
            .stableNodeId = *stableId,
            .parentIndex = parentIndex,
            .meshId = withMesh ? assets.meshId : Core::AssetId{},
            .materialId = withMesh ? assets.materialId : Core::AssetId{},
        }); !status) {
        return Core::failure(std::move(status.error()));
    }
    return EditorSceneOperationResult{
        .primaryStableId = *stableId,
        .affectedItemCount = 1,
    };
}
catch (const std::bad_alloc&)
{
    return sceneOperationAllocationFailure();
}

Core::Result<EditorSceneOperationResult>
addWorld3DNode(World3DAuthoringDocument& document, Core::u32 parentStableId)
{
    return addWorld3DNodeOfTemplate(document, World3DNodeTemplate::Empty,
                                    parentStableId);
}

Core::Result<EditorSceneOperationResult>
duplicateWorld3DNodeSubtree(World3DAuthoringDocument& document,
                            Core::u32 stableNodeId)
try
{
    std::vector<AssetFormat::PrefabNodeView> storage;
    auto prefab = document.parseCurrentPrefab(storage);
    if (!prefab) {
        return Core::failure(std::move(prefab.error()));
    }
    const Core::i32 rootIndex = findPrefabNodeIndex(storage, stableNodeId);
    if (rootIndex < 0) {
        return Core::failure(EditorErrorCode::EntityNotFound,
                             "Editor scene node to duplicate was not found");
    }

    Core::usize subtreeSize = 0;
    for (Core::usize index = 0; index < storage.size(); ++index) {
        subtreeSize += isPrefabDescendantOrSelf(
                           storage, index, static_cast<Core::usize>(rootIndex))
                           ? 1U
                           : 0U;
    }
    if (storage.size() > document.config().nodeCapacity ||
        subtreeSize > document.config().nodeCapacity - storage.size()) {
        return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                             "Duplicated Editor scene subtree exceeds node capacity");
    }

    std::vector<AssetFormat::PrefabNodeDesc> nodes;
    nodes.reserve(storage.size() + subtreeSize);
    for (const auto& node : storage) {
        nodes.push_back(toPrefabNodeDesc(node));
    }
    std::vector<Core::u32> duplicateIds;
    duplicateIds.reserve(subtreeSize);
    std::vector<Core::i32> duplicateIndices(storage.size(), -1);
    const Core::usize originalCount = storage.size();
    for (Core::usize index = 0; index < storage.size(); ++index) {
        if (!isPrefabDescendantOrSelf(storage, index,
                                      static_cast<Core::usize>(rootIndex))) {
            continue;
        }
        auto duplicateId = allocateStableId(
            std::span<const AssetFormat::PrefabNodeDesc>{nodes.data(), originalCount},
            [](const auto& node) { return node.stableNodeId; }, duplicateIds);
        if (!duplicateId) {
            return Core::failure(std::move(duplicateId.error()));
        }
        auto duplicate = toPrefabNodeDesc(storage[index]);
        duplicate.stableNodeId = *duplicateId;
        if (static_cast<Core::i32>(index) != rootIndex) {
            const Core::i32 sourceParent = storage[index].parentIndex;
            if (sourceParent < 0 ||
                duplicateIndices[static_cast<Core::usize>(sourceParent)] < 0) {
                return Core::failure(
                    EditorErrorCode::InvalidAuthoringOperation,
                    "Editor scene subtree is not in canonical parent-first order");
            }
            duplicate.parentIndex = duplicateIndices[
                static_cast<Core::usize>(sourceParent)];
        }
        duplicateIndices[index] = static_cast<Core::i32>(nodes.size());
        duplicateIds.push_back(*duplicateId);
        nodes.push_back(duplicate);
    }
    if (auto status = document.replace({.nodes = nodes}); !status) {
        return Core::failure(std::move(status.error()));
    }
    return EditorSceneOperationResult{
        .primaryStableId = duplicateIds.front(),
        .affectedItemCount = duplicateIds.size(),
    };
}
catch (const std::bad_alloc&)
{
    return sceneOperationAllocationFailure();
}

Core::Status reparentWorld3DNode(World3DAuthoringDocument& document,
                                 Core::u32 stableNodeId,
                                 Core::u32 newParentStableId)
try
{
    std::vector<AssetFormat::PrefabNodeView> storage;
    auto prefab = document.parseCurrentPrefab(storage);
    if (!prefab) {
        return Core::failure(std::move(prefab.error()));
    }
    const Core::i32 targetIndex = findPrefabNodeIndex(storage, stableNodeId);
    const Core::i32 newParentIndex = newParentStableId == 0
        ? -1
        : findPrefabNodeIndex(storage, newParentStableId);
    if (targetIndex < 0 || (newParentStableId != 0 && newParentIndex < 0)) {
        return Core::failure(EditorErrorCode::EntityNotFound,
                             "Editor scene reparent node or parent was not found");
    }
    if (newParentStableId == stableNodeId ||
        (newParentIndex >= 0 &&
         isPrefabDescendantOrSelf(storage,
                                  static_cast<Core::usize>(newParentIndex),
                                  static_cast<Core::usize>(targetIndex)))) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Editor scene node cannot be parented to its subtree");
    }
    const Core::i32 currentParentIndex =
        storage[static_cast<Core::usize>(targetIndex)].parentIndex;
    const Core::u32 currentParentStableId = currentParentIndex < 0
        ? 0
        : storage[static_cast<Core::usize>(currentParentIndex)].stableNodeId;
    if (currentParentStableId == newParentStableId) {
        return Core::success();
    }

    std::vector<PrefabHierarchyNode> nodes;
    nodes.reserve(storage.size());
    for (Core::usize index = 0; index < storage.size(); ++index) {
        Core::u32 parentStableId = storage[index].parentIndex < 0
            ? 0
            : storage[static_cast<Core::usize>(storage[index].parentIndex)]
                  .stableNodeId;
        if (index == static_cast<Core::usize>(targetIndex)) {
            parentStableId = newParentStableId;
        }
        nodes.push_back({
            .node = toPrefabNodeDesc(storage[index]),
            .parentStableId = parentStableId,
        });
    }
    return publishPrefabHierarchy(document, std::move(nodes));
}
catch (const std::bad_alloc&)
{
    return sceneOperationAllocationFailure();
}

Core::Status reorderWorld3DNode(World3DAuthoringDocument& document,
                                Core::u32 stableNodeId,
                                Core::u32 beforeSiblingStableId)
try
{
    std::vector<AssetFormat::PrefabNodeView> storage;
    auto prefab = document.parseCurrentPrefab(storage);
    if (!prefab) {
        return Core::failure(std::move(prefab.error()));
    }
    const Core::i32 targetIndex = findPrefabNodeIndex(storage, stableNodeId);
    if (targetIndex < 0 || (beforeSiblingStableId != 0 &&
                            findPrefabNodeIndex(storage, beforeSiblingStableId) < 0)) {
        return Core::failure(EditorErrorCode::EntityNotFound,
                             "Editor scene reorder item or sibling was not found");
    }
    if (beforeSiblingStableId == stableNodeId) {
        return Core::success();
    }
    const Core::u32 targetParentStableId = storage[static_cast<Core::usize>(targetIndex)].parentIndex < 0
        ? 0U
        : storage[static_cast<Core::usize>(storage[static_cast<Core::usize>(targetIndex)].parentIndex)].stableNodeId;
    const Core::i32 beforeIndex = beforeSiblingStableId == 0
        ? -1
        : findPrefabNodeIndex(storage, beforeSiblingStableId);
    const Core::u32 beforeParentStableId = beforeIndex < 0
        ? targetParentStableId
        : storage[static_cast<Core::usize>(beforeIndex)].parentIndex < 0
              ? 0U
              : storage[static_cast<Core::usize>(storage[static_cast<Core::usize>(beforeIndex)].parentIndex)].stableNodeId;
    if (beforeSiblingStableId != 0 && beforeParentStableId != targetParentStableId) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Editor scene reorder requires siblings");
    }

    const Core::usize rootIndex = storage.size();
    std::vector<std::vector<Core::usize>> children(storage.size() + 1U);
    for (Core::usize index = 0; index < storage.size(); ++index) {
        const Core::i32 parent = storage[index].parentIndex;
        const Core::usize parentIndex = parent < 0
            ? rootIndex
            : static_cast<Core::usize>(parent);
        if (parentIndex >= children.size()) {
            return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                 "Editor scene reorder found an invalid parent index");
        }
        children[parentIndex].push_back(index);
    }
    const Core::usize parentIndex = targetParentStableId == 0
        ? rootIndex
        : static_cast<Core::usize>(findPrefabNodeIndex(storage, targetParentStableId));
    auto& siblings = children[parentIndex];
    const Core::usize targetIndexValue = static_cast<Core::usize>(targetIndex);
    const auto targetSibling = std::find(siblings.begin(), siblings.end(), targetIndexValue);
    if (targetSibling == siblings.end()) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Editor scene reorder lost the target sibling");
    }
    siblings.erase(targetSibling);
    if (beforeSiblingStableId == 0) {
        siblings.push_back(targetIndexValue);
    } else {
        const auto beforeSibling = std::find_if(
            siblings.begin(), siblings.end(), [&](Core::usize index) {
                return storage[index].stableNodeId == beforeSiblingStableId;
            });
        if (beforeSibling == siblings.end()) {
            return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                                 "Editor scene reorder target is not a sibling");
        }
        siblings.insert(beforeSibling, targetIndexValue);
    }

    std::vector<PrefabHierarchyNode> hierarchy;
    hierarchy.reserve(storage.size());
    std::vector<Core::usize> pending;
    pending.reserve(storage.size());
    for (auto root = children[rootIndex].rbegin();
         root != children[rootIndex].rend(); ++root) {
        pending.push_back(*root);
    }
    while (!pending.empty()) {
        const Core::usize index = pending.back();
        pending.pop_back();
        const auto& node = storage[index];
        hierarchy.push_back({
            .node = toPrefabNodeDesc(node),
            .parentStableId = node.parentIndex < 0
                ? 0U
                : storage[static_cast<Core::usize>(node.parentIndex)].stableNodeId,
        });
        for (auto child = children[index].rbegin();
             child != children[index].rend(); ++child) {
            pending.push_back(*child);
        }
    }
    return publishPrefabHierarchy(document, std::move(hierarchy));
}
catch (const std::bad_alloc&)
{
    return sceneOperationAllocationFailure();
}

Core::Result<EditorSceneOperationResult>
deleteWorld3DNodeSubtree(World3DAuthoringDocument& document,
                         Core::u32 stableNodeId)
try
{
    std::vector<AssetFormat::PrefabNodeView> storage;
    auto prefab = document.parseCurrentPrefab(storage);
    if (!prefab) {
        return Core::failure(std::move(prefab.error()));
    }
    const Core::i32 targetIndex = findPrefabNodeIndex(storage, stableNodeId);
    if (targetIndex < 0) {
        return Core::failure(EditorErrorCode::EntityNotFound,
                             "Editor scene node to delete was not found");
    }
    const auto& target = storage[static_cast<Core::usize>(targetIndex)];
    const Core::u32 fallbackStableId = target.parentIndex < 0
        ? 0
        : storage[static_cast<Core::usize>(target.parentIndex)].stableNodeId;
    Core::usize affectedItemCount = 0;
    for (Core::usize index = 0; index < storage.size(); ++index) {
        affectedItemCount += isPrefabDescendantOrSelf(
                                 storage, index,
                                 static_cast<Core::usize>(targetIndex))
                                 ? 1U
                                 : 0U;
    }
    if (affectedItemCount == storage.size()) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Editor scene cannot delete every node from a World3D document");
    }
    if (auto status = document.eraseNodeSubtree(stableNodeId); !status) {
        return Core::failure(std::move(status.error()));
    }
    return EditorSceneOperationResult{
        .primaryStableId = fallbackStableId,
        .affectedItemCount = affectedItemCount,
    };
}
catch (const std::bad_alloc&)
{
    return sceneOperationAllocationFailure();
}

} // namespace Tina::Editor
