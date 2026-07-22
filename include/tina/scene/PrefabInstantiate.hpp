#pragma once

#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/scene/Entity.hpp>
#include <tina/scene/MeshRenderer3D.hpp>
#include <tina/scene/World.hpp>

#include <vector>

namespace Tina::Scene {

// Fixture-path mesh binding applied to every Prefab node with hasMesh.
// Full AssetHandle/FrameResourceRef resolve remains Deferred (M11-E8 first slice).
struct PrefabMeshBinding final {
    u32 fixtureMeshKey = 1;
    u32 fixtureMaterialKey = 1;
    Render::RenderBoundingSphereInput localBounds{.radius = 0.5F};
    Render::RenderLinearColor baseColorFactor{};
};

// Instantiates Prefab nodes into World in stable order:
// createEntity(local) → setParent(keep-local) → optional setMeshRenderer3D.
// On any failure, destroys all entities created by this call (no partial hierarchy).
// Caller must still call updateWorldTransforms() before extract.
[[nodiscard]] Core::Result<std::vector<EntityId>> instantiatePrefab(
    World& world,
    const AssetFormat::PrefabPayloadView& prefab,
    PrefabMeshBinding meshBinding = {}) noexcept;

} // namespace Tina::Scene
