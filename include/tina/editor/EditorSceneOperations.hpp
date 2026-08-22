#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
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
// It names the component set a newly created scene item starts with, so the
// caller never has to create a bare item and convert it afterwards. Enumerator
// order is the registry order and is also the order the Editor presents.
enum class World2DNodeTemplate : Core::u8 {
    Empty = 0,
    Sprite = 1,
    AnimatedSprite = 2,
    Camera = 3,
    PointLight = 4,
    ShadowOccluder = 5,
};

inline constexpr Core::usize World2DNodeTemplateCount = 6;

enum class World3DNodeTemplate : Core::u8 {
    Empty = 0,
    Mesh = 1,
};

inline constexpr Core::usize World3DNodeTemplateCount = 2;

// `displayName` is the single source of truth for a node's kind vocabulary: the
// creation picker and the hierarchy label both read it, so a node created as
// "AnimatedSprite2D" also reads back as "AnimatedSprite2D".
struct EditorNodeTemplateInfo final {
    std::string_view displayName{};
    std::string_view description{};
    bool requiresSpriteAsset = false;
    bool requiresAnimationClipAsset = false;
    bool requiresMeshAssets = false;
};

[[nodiscard]] std::span<const EditorNodeTemplateInfo>
world2DNodeTemplateRegistry() noexcept;

[[nodiscard]] std::span<const EditorNodeTemplateInfo>
world3DNodeTemplateRegistry() noexcept;

// Reports the template that best describes an existing item, so hierarchy
// labels stay consistent with what the creation picker offers. Classification
// is most-specific-first and total: every item maps to some template.
[[nodiscard]] World2DNodeTemplate classifyWorld2DEntityTemplate(
    const AssetFormat::World2DEntityDesc& entity) noexcept;

[[nodiscard]] World3DNodeTemplate classifyWorld3DNodeTemplate(
    const AssetFormat::PrefabNodeView& node) noexcept;

// Assets required by the requested template. Unused fields are ignored, so a
// Camera2D request does not need to resolve any asset.
struct World2DNodeTemplateAssets final {
    Core::AssetId spriteId{};
    Core::AssetId animationClipId{};
};

struct World3DNodeTemplateAssets final {
    Core::AssetId meshId{};
    Core::AssetId materialId{};
};

// State-changing scene hierarchy commands publish exactly one canonical
// document revision. Reparenting to the current parent is a successful no-op
// and does not publish. parentStableId == 0 creates or reparents an item at the
// scene root.
// Creates one item carrying the requested template's components in a single
// canonical revision, so choosing a node kind costs exactly one undo step
// rather than a create followed by a separate component attach.
//
// Templates whose wire schema needs an asset fail closed when it is missing:
// Sprite and AnimatedSprite require `spriteId`, AnimatedSprite additionally
// requires `animationClipId`. An unknown template fails closed. Every failure
// leaves the document and both history branches unchanged.
[[nodiscard]] Core::Result<EditorSceneOperationResult>
addWorld2DEntityOfTemplate(World2DAuthoringDocument& document,
                           World2DNodeTemplate nodeTemplate,
                           Core::u32 parentStableId = 0,
                           const World2DNodeTemplateAssets& assets = {});

// Equivalent to addWorld2DEntityOfTemplate with World2DNodeTemplate::Empty.
[[nodiscard]] Core::Result<EditorSceneOperationResult>
addWorld2DEntity(World2DAuthoringDocument& document,
                 Core::u32 parentStableId = 0);

[[nodiscard]] Core::Result<EditorSceneOperationResult>
duplicateWorld2DEntitySubtree(World2DAuthoringDocument& document,
                              Core::u32 stableEntityId);

[[nodiscard]] Core::Status
reparentWorld2DEntity(World2DAuthoringDocument& document,
                      Core::u32 stableEntityId,
                      Core::u32 newParentStableId);

// Moves an item before the requested sibling. A zero sibling appends it after
// the remaining siblings. The operation only accepts same-parent moves.
[[nodiscard]] Core::Status
reorderWorld2DEntity(World2DAuthoringDocument& document,
                     Core::u32 stableEntityId,
                     Core::u32 beforeSiblingStableId = 0);

// Deletes the complete subtree. primaryStableId is the deleted root's former
// parent, or zero when the caller should fall back to the scene root.
[[nodiscard]] Core::Result<EditorSceneOperationResult>
deleteWorld2DEntitySubtree(World2DAuthoringDocument& document,
                           Core::u32 stableEntityId);

// Prefab v2 represents a MeshRenderer as paired non-zero mesh/material IDs, so
// World3DNodeTemplate::Mesh requires both assets and fails closed without them.
[[nodiscard]] Core::Result<EditorSceneOperationResult>
addWorld3DNodeOfTemplate(World3DAuthoringDocument& document,
                         World3DNodeTemplate nodeTemplate,
                         Core::u32 parentStableId = 0,
                         const World3DNodeTemplateAssets& assets = {});

// Equivalent to addWorld3DNodeOfTemplate with World3DNodeTemplate::Empty.
[[nodiscard]] Core::Result<EditorSceneOperationResult>
addWorld3DNode(World3DAuthoringDocument& document,
               Core::u32 parentStableId = 0);

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

// Prefab v2 cannot represent an empty hierarchy, so deleting a subtree that
// contains every remaining node fails without changing the document.
[[nodiscard]] Core::Result<EditorSceneOperationResult>
deleteWorld3DNodeSubtree(World3DAuthoringDocument& document,
                         Core::u32 stableNodeId);

} // namespace Tina::Editor
