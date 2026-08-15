#pragma once

#include <tina/asset/AssetHandle.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/scene/Entity.hpp>
#include <tina/scene/MeshRenderer3D.hpp>
#include <tina/scene/SkinnedMeshRenderer3D.hpp>
#include <tina/scene/World.hpp>

#include <functional>
#include <vector>

namespace Tina::Scene {

// Maps cooked Prefab node AssetIds to weak mesh/material AssetHandles.
// When resolve* is empty, mesh/material apply to every meshed node.
struct PrefabMeshBinding final {
    Asset::AssetHandle mesh{};
    Asset::AssetHandle material{};
    Render::RenderBoundingSphereInput localBounds{.radius = 0.5F};
    Render::RenderLinearColor baseColorFactor{};
    Render::Mesh3DAlphaMode alphaMode = Render::Mesh3DAlphaMode::Opaque;
    // Optional: return an empty handle to fail instantiate for that node.
    std::function<Asset::AssetHandle(Core::AssetId meshId)> resolveMesh{};
    std::function<Asset::AssetHandle(Core::AssetId materialId)> resolveMaterial{};
    // Optional: resolve the authored mesh kind. Empty preserves the StaticMesh default.
    // Only StaticMesh and SkinnedMesh are valid renderer kinds.
    std::function<AssetFormat::AssetKind(Core::AssetId meshId)> resolveMeshKind{};
    // Optional: override bounds/color per mesh AssetId (empty = use defaults above).
    std::function<Render::RenderBoundingSphereInput(Core::AssetId meshId)> resolveLocalBounds{};
    std::function<Render::RenderLinearColor(Core::AssetId materialId)> resolveBaseColor{};
    std::function<Render::Mesh3DAlphaMode(Core::AssetId materialId)> resolveAlphaMode{};
};

// Instantiates Prefab nodes into World in stable order:
// createEntity(local) -> setParent(keep-local) -> optional kind-specific 3D renderer.
// On any failure, destroys all entities created by this call (no partial hierarchy).
// Caller must still call updateWorldTransforms() before extract.
[[nodiscard]] Core::Result<std::vector<EntityId>> instantiatePrefab(
    World& world,
    const AssetFormat::PrefabPayloadView& prefab,
    PrefabMeshBinding meshBinding = {}) noexcept;

} // namespace Tina::Scene
