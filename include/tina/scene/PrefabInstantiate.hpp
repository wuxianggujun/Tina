#pragma once

#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/scene/Entity.hpp>
#include <tina/scene/MeshRenderer3D.hpp>
#include <tina/scene/World.hpp>

#include <functional>
#include <vector>

namespace Tina::Scene {

// Maps cooked Prefab node AssetIds to backend meshKey/materialKey tables.
// When resolve* is empty, meshKey/materialKey apply to every meshed node.
// Games typically bind GPU meshes once then resolve AssetId -> key.
struct PrefabMeshBinding final {
    u32 meshKey = 1;
    u32 materialKey = 1;
    Render::RenderBoundingSphereInput localBounds{.radius = 0.5F};
    Render::RenderLinearColor baseColorFactor{};
    // Optional: return 0 to fail instantiate for that node.
    std::function<u32(Core::AssetId meshId)> resolveMeshKey{};
    std::function<u32(Core::AssetId materialId)> resolveMaterialKey{};
    // Optional: override bounds/color per mesh AssetId (empty = use defaults above).
    std::function<Render::RenderBoundingSphereInput(Core::AssetId meshId)> resolveLocalBounds{};
    std::function<Render::RenderLinearColor(Core::AssetId materialId)> resolveBaseColor{};
};

// Instantiates Prefab nodes into World in stable order:
// createEntity(local) -> setParent(keep-local) -> optional setMeshRenderer3D.
// On any failure, destroys all entities created by this call (no partial hierarchy).
// Caller must still call updateWorldTransforms() before extract.
[[nodiscard]] Core::Result<std::vector<EntityId>> instantiatePrefab(
    World& world,
    const AssetFormat::PrefabPayloadView& prefab,
    PrefabMeshBinding meshBinding = {}) noexcept;

} // namespace Tina::Scene
