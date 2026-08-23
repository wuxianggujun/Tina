#include <tina/editor/EditorSceneOperations.hpp>

#include <tina/editor/EditorErrors.hpp>
#include <tina/core/text/Utf8.hpp>

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
        {.displayName = "Node2D",
         .category = "Core",
         .description = "Transform-only container for organizing child nodes."},
        {.displayName = "Marker2D",
         .category = "Core",
         .description = "Named transform marker for gameplay and authoring anchors."},
        {.displayName = "Sprite2D",
         .category = "Visual",
         .description = "Draws a sprite. Needs a Sprite asset.",
         .requiresSpriteAsset = true},
        {.displayName = "AnimatedSprite2D",
         .category = "Visual",
         .description = "Sprite driven by an animation clip.",
         .requiresSpriteAsset = true,
         .requiresAnimationClipAsset = true},
        {.displayName = "TileMap2D",
         .category = "Visual",
         .description = "Instantiates a cooked TileMap asset at this transform.",
         .requiredResourceAssetKind = AssetFormat::AssetKind::TileMap},
        {.displayName = "FxEmitter2D",
         .category = "Visual",
         .description = "Emits particles and trails from a cooked Fx2D asset.",
         .requiredResourceAssetKind = AssetFormat::AssetKind::Fx2D},
        {.displayName = "Camera2D",
         .category = "Camera",
         .description = "Defines what the 2D viewport renders."},
        {.displayName = "PointLight2D",
         .category = "Lighting",
         .description = "Emits 2D light within a radius."},
        {.displayName = "ShadowOccluder2D",
         .category = "Lighting",
         .description = "Blocks 2D light along a segment."},
        {.displayName = "StaticBody2D",
         .category = "Physics",
         .description = "Immovable physics body that owns CollisionShape2D children."},
        {.displayName = "RigidBody2D",
         .category = "Physics",
         .description = "Dynamic body driven by the fixed-step physics world."},
        {.displayName = "CharacterBody2D",
         .category = "Physics",
         .description = "Kinematic body controlled by gameplay movement."},
        {.displayName = "Area2D",
         .category = "Physics",
         .description = "Sensor body that reports overlap events from child shapes."},
        {.displayName = "CollisionShape2D",
         .category = "Physics",
         .description = "Box, circle, or capsule shape owned by a physics body parent."},
        {.displayName = "NavigationRegion2D",
         .category = "Navigation",
         .description = "Binds a cooked NavigationGrid2D asset.",
         .requiredResourceAssetKind = AssetFormat::AssetKind::NavigationGrid2D},
        {.displayName = "AudioPlayer2D",
         .category = "Audio",
         .description = "Spatial playback source backed by an AudioClip asset.",
         .requiredResourceAssetKind = AssetFormat::AssetKind::AudioClip},
    }};

inline constexpr std::array<EditorNodeTemplateInfo, World3DNodeTemplateCount>
    World3DNodeTemplateRegistry{{
        {.displayName = "Node3D",
         .category = "Core",
         .description = "3D transform container for organizing child nodes."},
        {.displayName = "Marker3D",
         .category = "Core",
         .description = "Named 3D transform marker for gameplay anchors."},
        {.displayName = "Mesh3D",
         .category = "Visual",
         .description = "Renders a mesh. Needs mesh and material assets.",
         .requiredMeshAssetKind = AssetFormat::AssetKind::StaticMesh},
        {.displayName = "SkinnedMesh3D",
         .category = "Visual",
         .description = "Renders a skinned mesh. Needs skinned mesh and material assets.",
         .requiredMeshAssetKind = AssetFormat::AssetKind::SkinnedMesh},
        {.displayName = "Camera3D",
         .category = "Camera",
         .description = "Defines a perspective camera in the 3D scene."},
        {.displayName = "DirectionalLight3D",
         .category = "Lighting",
         .description = "Emits parallel light along the node's local -Z axis."},
        {.displayName = "PointLight3D",
         .category = "Lighting",
         .description = "Emits light from the node position within a range."},
        {.displayName = "SpotLight3D",
         .category = "Lighting",
         .description = "Emits cone-shaped light along the node's local -Z axis."},
    }};

[[nodiscard]] bool isWorld2DPhysicsBodyNodeKind(
    AssetFormat::World2DNodeKind kind) noexcept
{
    return kind == AssetFormat::World2DNodeKind::StaticBody2D ||
           kind == AssetFormat::World2DNodeKind::RigidBody2D ||
           kind == AssetFormat::World2DNodeKind::CharacterBody2D ||
           kind == AssetFormat::World2DNodeKind::Area2D;
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

Core::Result<World2DNodeTemplate> classifyWorld2DNodeTemplate(
    const AssetFormat::World2DEntityDesc& entity)
{
    const auto slot = static_cast<Core::usize>(entity.nodeKind);
    if (slot >= World2DNodeTemplateRegistry.size()) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "World2D wire item contains an unknown Editor node kind");
    }

    const Core::usize payloadCount =
        static_cast<Core::usize>(entity.sprite.has_value()) +
        static_cast<Core::usize>(entity.camera.has_value()) +
        static_cast<Core::usize>(entity.pointLight.has_value()) +
        static_cast<Core::usize>(entity.shadowOccluder.has_value()) +
        static_cast<Core::usize>(entity.spriteAnimation.has_value()) +
        static_cast<Core::usize>(entity.resource.has_value()) +
        static_cast<Core::usize>(entity.physicsBody.has_value()) +
        static_cast<Core::usize>(entity.physicsShape.has_value());
    bool matches = false;
    switch (entity.nodeKind) {
    case AssetFormat::World2DNodeKind::Node2D:
    case AssetFormat::World2DNodeKind::Marker2D:
        matches = payloadCount == 0U;
        break;
    case AssetFormat::World2DNodeKind::Sprite2D:
        matches = payloadCount == 1U && entity.sprite.has_value();
        break;
    case AssetFormat::World2DNodeKind::AnimatedSprite2D:
        matches = payloadCount == 2U && entity.sprite.has_value() &&
                  entity.spriteAnimation.has_value();
        break;
    case AssetFormat::World2DNodeKind::Camera2D:
        matches = payloadCount == 1U && entity.camera.has_value();
        break;
    case AssetFormat::World2DNodeKind::PointLight2D:
        matches = payloadCount == 1U && entity.pointLight.has_value();
        break;
    case AssetFormat::World2DNodeKind::ShadowOccluder2D:
        matches = payloadCount == 1U && entity.shadowOccluder.has_value();
        break;
    case AssetFormat::World2DNodeKind::TileMap2D:
    case AssetFormat::World2DNodeKind::FxEmitter2D:
    case AssetFormat::World2DNodeKind::NavigationRegion2D:
    case AssetFormat::World2DNodeKind::AudioPlayer2D:
        matches = payloadCount == 1U && entity.resource.has_value();
        break;
    case AssetFormat::World2DNodeKind::StaticBody2D:
    case AssetFormat::World2DNodeKind::RigidBody2D:
    case AssetFormat::World2DNodeKind::CharacterBody2D:
    case AssetFormat::World2DNodeKind::Area2D:
        matches = payloadCount == 1U && entity.physicsBody.has_value();
        break;
    case AssetFormat::World2DNodeKind::CollisionShape2D:
        matches = payloadCount == 1U && entity.physicsShape.has_value();
        break;
    }
    if (!matches) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "World2D wire item node kind does not match its canonical payload");
    }
    return static_cast<World2DNodeTemplate>(slot);
}

Core::Result<World3DNodeTemplate> classifyWorld3DNodeTemplate(
    const AssetFormat::PrefabNodeView& node)
{
    const auto slot = static_cast<Core::usize>(node.nodeKind);
    if (slot >= World3DNodeTemplateRegistry.size() ||
        node.hasMesh != node.hasMaterial) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "World3D wire item contains an unknown or incomplete Editor node kind");
    }
    const bool hasMesh = node.hasMesh;
    const bool hasCamera = node.camera.has_value();
    const bool hasLight = node.light.has_value();
    bool matches = false;
    switch (node.nodeKind) {
    case AssetFormat::PrefabNodeKind::Node3D:
    case AssetFormat::PrefabNodeKind::Marker3D:
        matches = !hasMesh && !hasCamera && !hasLight;
        break;
    case AssetFormat::PrefabNodeKind::Mesh3D:
    case AssetFormat::PrefabNodeKind::SkinnedMesh3D:
        matches = hasMesh && !hasCamera && !hasLight;
        break;
    case AssetFormat::PrefabNodeKind::Camera3D:
        matches = !hasMesh && hasCamera && !hasLight;
        break;
    case AssetFormat::PrefabNodeKind::DirectionalLight3D:
    case AssetFormat::PrefabNodeKind::PointLight3D:
    case AssetFormat::PrefabNodeKind::SpotLight3D:
        matches = !hasMesh && !hasCamera && hasLight;
        break;
    }
    if (!matches) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "World3D wire item node kind does not match its canonical payload");
    }
    return static_cast<World3DNodeTemplate>(slot);
}

Core::Result<EditorSceneOperationResult>
addWorld2DNode(World2DAuthoringDocument& document,
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
    if (info.requiredResourceAssetKind != AssetFormat::AssetKind::Invalid &&
        !assets.resourceId) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "Editor scene resource node template requires an asset");
    }

    std::vector<AssetFormat::World2DEntityDesc> storage;
    auto snapshot = document.parseCurrentSnapshot(storage);
    if (!snapshot) {
        return Core::failure(std::move(snapshot.error()));
    }
    const AssetFormat::World2DEntityDesc* parent =
        parentStableId != 0 ? findWorld2DEntity(storage, parentStableId) : nullptr;
    if (parentStableId != 0 && parent == nullptr) {
        return Core::failure(EditorErrorCode::EntityNotFound,
                             "Editor scene parent node was not found");
    }
    if (nodeTemplate == World2DNodeTemplate::CollisionShape2D) {
        if (parent == nullptr) {
            return Core::failure(
                EditorErrorCode::InvalidAuthoringOperation,
                "CollisionShape2D requires a physics body parent");
        }
        auto parentTemplate = classifyWorld2DNodeTemplate(*parent);
        const bool acceptsShape = parentTemplate &&
            (*parentTemplate == World2DNodeTemplate::StaticBody2D ||
             *parentTemplate == World2DNodeTemplate::RigidBody2D ||
             *parentTemplate == World2DNodeTemplate::CharacterBody2D ||
             *parentTemplate == World2DNodeTemplate::Area2D);
        if (!acceptsShape) {
            return Core::failure(
                EditorErrorCode::InvalidAuthoringOperation,
                "CollisionShape2D parent must be StaticBody2D, RigidBody2D, CharacterBody2D, or Area2D");
        }
    }
    if (storage.size() >= document.config().entityCapacity) {
        return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                             "Editor scene node capacity is exhausted");
    }
    auto stableId = allocateStableId(
        std::span<const AssetFormat::World2DEntityDesc>{storage},
        [](const auto& entity) { return entity.stableEntityId; });
    if (!stableId) {
        return Core::failure(std::move(stableId.error()));
    }

    // The type-specific wire payload is created with the node, so creation is
    // one canonical revision and one undo step.
    AssetFormat::World2DEntityDesc created{
        .stableEntityId = *stableId,
        .parentStableEntityId = parentStableId,
        .nodeKind = static_cast<AssetFormat::World2DNodeKind>(nodeTemplate),
    };
    switch (nodeTemplate) {
    case World2DNodeTemplate::Node2D:
    case World2DNodeTemplate::Marker2D:
        break;
    case World2DNodeTemplate::Sprite2D:
        created.sprite.emplace().spriteId = assets.spriteId;
        break;
    case World2DNodeTemplate::AnimatedSprite2D:
        created.sprite.emplace().spriteId = assets.spriteId;
        created.spriteAnimation.emplace().clipId = assets.animationClipId;
        break;
    case World2DNodeTemplate::TileMap2D:
    case World2DNodeTemplate::FxEmitter2D:
    case World2DNodeTemplate::NavigationRegion2D:
    case World2DNodeTemplate::AudioPlayer2D:
        created.resource.emplace().assetId = assets.resourceId;
        break;
    case World2DNodeTemplate::Camera2D:
        created.camera.emplace();
        break;
    case World2DNodeTemplate::PointLight2D:
        created.pointLight.emplace();
        break;
    case World2DNodeTemplate::ShadowOccluder2D:
        created.shadowOccluder.emplace();
        break;
    case World2DNodeTemplate::StaticBody2D:
    case World2DNodeTemplate::RigidBody2D:
    case World2DNodeTemplate::CharacterBody2D:
    case World2DNodeTemplate::Area2D:
        created.physicsBody.emplace();
        break;
    case World2DNodeTemplate::CollisionShape2D:
        created.physicsShape.emplace();
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

Core::Status renameWorld2DNode(World2DAuthoringDocument& document,
                               Core::u32 stableEntityId,
                               std::string_view name)
try
{
    if (name.empty() || name.size() > AssetFormat::World2DSnapshotWire::MaximumNameBytes ||
        !Core::isStrictUtf8WithoutNul(name)) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "World2D node name must be non-empty valid UTF-8");
    }
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
                             "Editor scene node to rename was not found");
    }
    if (target->name == name) {
        return Core::success();
    }
    target->name.assign(name.data(), name.size());
    return document.replace({
        .entities = storage,
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
duplicateWorld2DNodeSubtree(World2DAuthoringDocument& document,
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
                             "Editor scene node to duplicate was not found");
    }

    const Core::usize subtreeSize = static_cast<Core::usize>(std::count_if(
        storage.begin(), storage.end(), [&](const auto& entity) {
            return isWorld2DDescendantOrSelf(storage, entity.stableEntityId,
                                             stableEntityId);
        }));
    if (storage.size() > document.config().entityCapacity ||
        subtreeSize > document.config().entityCapacity - storage.size()) {
        return Core::failure(EditorErrorCode::DocumentCapacityExceeded,
                             "Duplicated Editor scene subtree exceeds node capacity");
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

Core::Status renameWorld3DNode(World3DAuthoringDocument& document,
                               Core::u32 stableNodeId,
                               std::string_view name)
try
{
    if (name.empty() || name.size() > AssetFormat::PrefabWire::MaximumNameBytes ||
        !Core::isStrictUtf8WithoutNul(name)) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "World3D node name must be non-empty valid UTF-8");
    }
    std::vector<AssetFormat::PrefabNodeView> storage;
    auto prefab = document.parseCurrentPrefab(storage);
    if (!prefab) {
        return Core::failure(std::move(prefab.error()));
    }
    const auto target = std::find_if(
        storage.begin(), storage.end(), [stableNodeId](const auto& node) {
            return node.stableNodeId == stableNodeId;
        });
    if (target == storage.end()) {
        return Core::failure(EditorErrorCode::EntityNotFound,
                             "Editor scene node to rename was not found");
    }
    if (target->name == name) {
        return Core::success();
    }
    std::vector<AssetFormat::PrefabNodeDesc> nodes;
    nodes.reserve(storage.size());
    for (const auto& node : storage) {
        auto desc = toPrefabNodeDesc(node);
        if (desc.stableNodeId == stableNodeId) {
            desc.name.assign(name.data(), name.size());
        }
        nodes.push_back(std::move(desc));
    }
    return document.replace({.nodes = nodes});
}
catch (const std::bad_alloc&)
{
    return sceneOperationAllocationFailure();
}

Core::Status reparentWorld2DNode(World2DAuthoringDocument& document,
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
    const AssetFormat::World2DEntityDesc* newParent =
        newParentStableId != 0
            ? findWorld2DEntity(storage, newParentStableId)
            : nullptr;
    if (target == storage.end() ||
        (newParentStableId != 0 && newParent == nullptr)) {
        return Core::failure(EditorErrorCode::EntityNotFound,
                             "Editor scene reparent item or parent was not found");
    }
    if (newParentStableId == stableEntityId ||
        (newParentStableId != 0 &&
         isWorld2DDescendantOrSelf(storage, newParentStableId,
                                  stableEntityId))) {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Editor scene node cannot be parented to its subtree");
    }
    if (target->parentStableEntityId == newParentStableId) {
        return Core::success();
    }
    if (target->nodeKind == AssetFormat::World2DNodeKind::CollisionShape2D &&
        (newParent == nullptr ||
         !isWorld2DPhysicsBodyNodeKind(newParent->nodeKind))) {
        return Core::failure(
            EditorErrorCode::InvalidAuthoringOperation,
            "CollisionShape2D parent must be StaticBody2D, RigidBody2D, CharacterBody2D, or Area2D");
    }
    target->parentStableEntityId = newParentStableId;
    return publishWorld2DHierarchy(document, std::move(storage), *snapshot);
}
catch (const std::bad_alloc&)
{
    return sceneOperationAllocationFailure();
}

Core::Status reorderWorld2DNode(World2DAuthoringDocument& document,
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
deleteWorld2DNodeSubtree(World2DAuthoringDocument& document,
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
                             "Editor scene node to delete was not found");
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
addWorld3DNode(World3DAuthoringDocument& document,
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
    if (info.requiredMeshAssetKind != AssetFormat::AssetKind::Invalid &&
        (!assets.meshId || !assets.materialId)) {
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
    AssetFormat::PrefabNodeDesc created{
        .stableNodeId = *stableId,
        .parentIndex = parentIndex,
        .nodeKind = static_cast<AssetFormat::PrefabNodeKind>(nodeTemplate),
    };
    switch (nodeTemplate) {
    case World3DNodeTemplate::Node3D:
    case World3DNodeTemplate::Marker3D:
        break;
    case World3DNodeTemplate::Mesh3D:
    case World3DNodeTemplate::SkinnedMesh3D:
        created.meshId = assets.meshId;
        created.materialId = assets.materialId;
        break;
    case World3DNodeTemplate::Camera3D:
        created.camera.emplace();
        break;
    case World3DNodeTemplate::DirectionalLight3D:
    case World3DNodeTemplate::PointLight3D:
    case World3DNodeTemplate::SpotLight3D:
        created.light.emplace();
        break;
    }
    if (auto status = document.upsertNode(created); !status) {
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
