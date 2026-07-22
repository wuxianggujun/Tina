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
    if (!isValid(MeshRenderer3D{
            .fixtureMeshKey = meshBinding.fixtureMeshKey,
            .fixtureMaterialKey = meshBinding.fixtureMaterialKey,
            .localBounds = meshBinding.localBounds,
            .baseColorFactor = meshBinding.baseColorFactor,
            .visible = true,
        }))
    {
        return Core::failure(SceneErrorCode::InvalidComponent, "prefab mesh binding is invalid");
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

        if (node.hasMesh)
        {
            MeshRenderer3D mesh{
                .fixtureMeshKey = meshBinding.fixtureMeshKey,
                .fixtureMaterialKey = meshBinding.fixtureMaterialKey,
                .localBounds = meshBinding.localBounds,
                .baseColorFactor = meshBinding.baseColorFactor,
                .visible = node.visible,
            };
            if (auto status = world.setMeshRenderer3D(*entity, mesh); !status)
            {
                rollback();
                return Core::failure(std::move(status.error()));
            }
        }
    }

    return created;
}

} // namespace Tina::Scene
