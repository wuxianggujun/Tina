#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/editor/World2DAuthoringDocument.hpp>
#include <tina/editor/World3DAuthoringDocument.hpp>

namespace Tina::Editor {

struct EditorSceneOperationResult final {
    Core::u32 primaryStableId = 0;
    Core::usize affectedItemCount = 0;
};

// State-changing scene hierarchy commands publish exactly one canonical
// document revision. Reparenting to the current parent is a successful no-op
// and does not publish. parentStableId == 0 creates or reparents an item at the
// scene root.
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

// Deletes the complete subtree. primaryStableId is the deleted root's former
// parent, or zero when the caller should fall back to the scene root.
[[nodiscard]] Core::Result<EditorSceneOperationResult>
deleteWorld2DEntitySubtree(World2DAuthoringDocument& document,
                           Core::u32 stableEntityId);

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

// Prefab v2 cannot represent an empty hierarchy, so deleting a subtree that
// contains every remaining node fails without changing the document.
[[nodiscard]] Core::Result<EditorSceneOperationResult>
deleteWorld3DNodeSubtree(World3DAuthoringDocument& document,
                         Core::u32 stableNodeId);

} // namespace Tina::Editor
