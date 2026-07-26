#include <tina/scene/PrefabInstantiate.hpp>

#include <tina/scene/SceneErrors.hpp>
#include <tina/scene/Transform.hpp>

#include <utility>

namespace Tina::Scene {

Core::Result<std::vector<EntityId>> instantiatePrefab(
    World& world,
    const AssetFormat::PrefabPayloadView& prefab,
    PrefabMeshBinding meshBinding) noexcept
{
    if (prefab.nodes.empty())
    {
        return Core::failure(SceneErrorCode::InvalidComponent, "prefab has no nodes to instantiate");
    }

    std::vector<EntityId> created;
    created.reserve(prefab.nodes.size());

    auto rollback = [&]() noexcept {
        for (auto it = created.rbegin(); it != created.rend(); ++it)
        {
            (void)world.destroyEntity(*it);
        }
        created.clear();
    };

    for (const AssetFormat::PrefabNodeView& node : prefab.nodes)
    {
        LocalTransform local{};
        local.position = {node.positionX, node.positionY, node.positionZ};
        local.rotation = {node.rotationX, node.rotationY, node.rotationZ, node.rotationW};
        local.scale = {node.scaleX, node.scaleY, node.scaleZ};
        auto entity = world.createEntity(local);
        if (!entity)
        {
            rollback();
            return Core::failure(std::move(entity.error()));
        }
        created.push_back(*entity);

        if (node.parentIndex >= 0)
        {
            if (static_cast<std::size_t>(node.parentIndex) >= created.size())
            {
                rollback();
                return Core::failure(SceneErrorCode::CorruptHierarchy,
                                     "prefab parentIndex out of range during instantiate");
            }
            if (auto status = world.setParent(
                    *entity, created[static_cast<std::size_t>(node.parentIndex)], ReparentMode::KeepLocal);
                !status)
            {
                rollback();
                return Core::failure(std::move(status.error()));
            }
        }

        if (!node.hasMesh)
        {
            continue;
        }

        Asset::AssetHandle meshAsset = meshBinding.mesh;
        Asset::AssetHandle materialAsset = meshBinding.material;
        if (meshBinding.resolveMesh)
        {
            if (!static_cast<bool>(node.meshId))
            {
                rollback();
                return Core::failure(SceneErrorCode::UnresolvedMesh,
                                     "prefab meshed node missing mesh AssetId for resolve");
            }
            meshAsset = meshBinding.resolveMesh(node.meshId);
        }
        if (meshBinding.resolveMaterial)
        {
            if (!static_cast<bool>(node.materialId))
            {
                rollback();
                return Core::failure(SceneErrorCode::UnresolvedMesh,
                                     "prefab meshed node missing material AssetId for resolve");
            }
            materialAsset = meshBinding.resolveMaterial(node.materialId);
        }
        if (!meshAsset || !materialAsset)
        {
            rollback();
            return Core::failure(SceneErrorCode::UnresolvedMesh,
                                 "prefab mesh/material AssetHandle resolve returned empty");
        }

        Render::RenderBoundingSphereInput bounds = meshBinding.localBounds;
        if (meshBinding.resolveLocalBounds && static_cast<bool>(node.meshId))
        {
            bounds = meshBinding.resolveLocalBounds(node.meshId);
        }
        Render::RenderLinearColor color = meshBinding.baseColorFactor;
        if (meshBinding.resolveBaseColor && static_cast<bool>(node.materialId))
        {
            color = meshBinding.resolveBaseColor(node.materialId);
        }

        MeshRenderer3D mesh{
            .mesh = meshAsset,
            .material = materialAsset,
            .localBounds = bounds,
            .baseColorFactor = color,
            .visible = node.visible,
        };
        if (!isValid(mesh))
        {
            rollback();
            return Core::failure(SceneErrorCode::InvalidComponent, "prefab mesh binding is invalid");
        }
        if (auto status = world.setMeshRenderer3D(*entity, mesh); !status)
        {
            rollback();
            return Core::failure(std::move(status.error()));
        }
    }

    return created;
}

} // namespace Tina::Scene
