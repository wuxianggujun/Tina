#include <tina/editor/EditorSceneOperations.hpp>

#include <tina/editor/EditorErrors.hpp>

#include <algorithm>
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

Core::Result<EditorSceneOperationResult>
addWorld2DEntity(World2DAuthoringDocument& document,
                 Core::u32 parentStableId)
try
{
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
    if (auto status = document.upsertEntity({
            .stableEntityId = *stableId,
            .parentStableEntityId = parentStableId,
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
addWorld3DNode(World3DAuthoringDocument& document, Core::u32 parentStableId)
try
{
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
    if (auto status = document.upsertNode({
            .stableNodeId = *stableId,
            .parentIndex = parentIndex,
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
