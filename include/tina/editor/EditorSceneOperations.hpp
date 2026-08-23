#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/editor/World2DAuthoringDocument.hpp>
#include <tina/editor/World3DAuthoringDocument.hpp>

#include <span>
#include <string_view>

namespace Tina::Editor {

struct EditorSceneOperationResult final {
    Core::u32 primaryStableId = 0;
    Core::usize affectedItemCount = 0;
};

// A node template is the authoring answer to "what kind of node am I creating?".
// It names the complete node payload created by one scene operation. Enumerator
// order is the registry order and is also the order the Editor presents.
enum class World2DNodeTemplate : Core::u8 {
    Node2D = 0,
    Marker2D = 1,
    Sprite2D = 2,
    AnimatedSprite2D = 3,
    TileMap2D = 4,
    FxEmitter2D = 5,
    Camera2D = 6,
    PointLight2D = 7,
    ShadowOccluder2D = 8,
    StaticBody2D = 9,
    RigidBody2D = 10,
    CharacterBody2D = 11,
    Area2D = 12,
    CollisionShape2D = 13,
    NavigationRegion2D = 14,
    AudioPlayer2D = 15,
};

inline constexpr Core::usize World2DNodeTemplateCount = 16;

enum class World3DNodeTemplate : Core::u8 {
    Node3D = 0,
    Marker3D = 1,
    Mesh3D = 2,
    SkinnedMesh3D = 3,
    Camera3D = 4,
    DirectionalLight3D = 5,
    PointLight3D = 6,
    SpotLight3D = 7,
};

inline constexpr Core::usize World3DNodeTemplateCount = 8;

// `displayName` is the single source of truth for a node's kind vocabulary: the
// creation picker and the hierarchy label both read it, so a node created as
// "AnimatedSprite2D" also reads back as "AnimatedSprite2D".
struct EditorNodeTemplateInfo final {
    std::string_view displayName{};
    std::string_view category{};
    std::string_view description{};
    bool requiresSpriteAsset = false;
    bool requiresAnimationClipAsset = false;
    AssetFormat::AssetKind requiredMeshAssetKind =
        AssetFormat::AssetKind::Invalid;
    AssetFormat::AssetKind requiredResourceAssetKind =
        AssetFormat::AssetKind::Invalid;
};

[[nodiscard]] std::span<const EditorNodeTemplateInfo>
world2DNodeTemplateRegistry() noexcept;

[[nodiscard]] std::span<const EditorNodeTemplateInfo>
world3DNodeTemplateRegistry() noexcept;

// Resolves the one supported Editor node kind represented by an existing wire
// item. Legacy payload mixtures fail closed instead of being exposed as a node
// with attachable components.
[[nodiscard]] Core::Result<World2DNodeTemplate> classifyWorld2DNodeTemplate(
    const AssetFormat::World2DEntityDesc& entity);

[[nodiscard]] Core::Result<World3DNodeTemplate> classifyWorld3DNodeTemplate(
    const AssetFormat::PrefabNodeView& node);

// Assets required by the requested template. Unused fields are ignored, so a
// Camera2D request does not need to resolve any asset.
struct World2DNodeTemplateAssets final {
    Core::AssetId spriteId{};
    Core::AssetId animationClipId{};
    Core::AssetId resourceId{};
};

struct World3DNodeTemplateAssets final {
    Core::AssetId meshId{};
    Core::AssetId materialId{};
};

// State-changing scene hierarchy commands publish exactly one canonical
// document revision. Reparenting to the current parent is a successful no-op
// and does not publish. parentStableId == 0 creates or reparents an item at the
// scene root.
// Creates one complete node of the requested kind in a single canonical
// revision, so choosing a node kind costs exactly one undo step.
//
// Templates whose wire schema needs an asset fail closed when it is missing:
// Sprite and AnimatedSprite require `spriteId`, AnimatedSprite additionally
// requires `animationClipId`. An unknown template fails closed. Every failure
// leaves the document and both history branches unchanged.
[[nodiscard]] Core::Result<EditorSceneOperationResult>
addWorld2DNode(World2DAuthoringDocument& document,
               World2DNodeTemplate nodeTemplate,
               Core::u32 parentStableId = 0,
               const World2DNodeTemplateAssets& assets = {});

[[nodiscard]] Core::Result<EditorSceneOperationResult>
duplicateWorld2DNodeSubtree(World2DAuthoringDocument& document,
                            Core::u32 stableNodeId);

[[nodiscard]] Core::Status
reparentWorld2DNode(World2DAuthoringDocument& document,
                    Core::u32 stableNodeId,
                    Core::u32 newParentStableId);

// Moves an item before the requested sibling. A zero sibling appends it after
// the remaining siblings. The operation only accepts same-parent moves.
[[nodiscard]] Core::Status
reorderWorld2DNode(World2DAuthoringDocument& document,
                   Core::u32 stableNodeId,
                   Core::u32 beforeSiblingStableId = 0);

[[nodiscard]] Core::Status
renameWorld2DNode(World2DAuthoringDocument& document,
                  Core::u32 stableNodeId,
                  std::string_view name);

// Deletes the complete subtree. primaryStableId is the deleted root's former
// parent, or zero when the caller should fall back to the scene root.
[[nodiscard]] Core::Result<EditorSceneOperationResult>
deleteWorld2DNodeSubtree(World2DAuthoringDocument& document,
                         Core::u32 stableNodeId);

// Mesh3D and SkinnedMesh3D require paired non-zero mesh/material IDs. Other
// templates create their complete typed payload without external assets.
[[nodiscard]] Core::Result<EditorSceneOperationResult>
addWorld3DNode(World3DAuthoringDocument& document,
               World3DNodeTemplate nodeTemplate,
               Core::u32 parentStableId = 0,
               const World3DNodeTemplateAssets& assets = {});

[[nodiscard]] Core::Result<EditorSceneOperationResult>
duplicateWorld3DNodeSubtree(World3DAuthoringDocument& document,
                            Core::u32 stableNodeId);

[[nodiscard]] Core::Status
reparentWorld3DNode(World3DAuthoringDocument& document,
                    Core::u32 stableNodeId,
                    Core::u32 newParentStableId);

[[nodiscard]] Core::Status
reorderWorld3DNode(World3DAuthoringDocument& document,
                   Core::u32 stableNodeId,
                   Core::u32 beforeSiblingStableId = 0);

[[nodiscard]] Core::Status
renameWorld3DNode(World3DAuthoringDocument& document,
                  Core::u32 stableNodeId,
                  std::string_view name);

// Prefab v4 cannot represent an empty hierarchy, so deleting a subtree that
// contains every remaining node fails without changing the document.
[[nodiscard]] Core::Result<EditorSceneOperationResult>
deleteWorld3DNodeSubtree(World3DAuthoringDocument& document,
                         Core::u32 stableNodeId);

} // namespace Tina::Editor
