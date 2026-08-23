#include <tina/scene/PrefabInstantiate.hpp>

#include <tina/scene/SceneErrors.hpp>
#include <tina/scene/Transform.hpp>

#include <algorithm>
#include <exception>
#include <new>
#include <numbers>
#include <utility>

namespace Tina::Scene {
namespace {

inline constexpr float RadiansToDegrees =
    180.0F / std::numbers::pi_v<float>;

[[nodiscard]] Render::RenderLinearColor prefabLightColor(
    const AssetFormat::PrefabLight3DDesc& light) noexcept
{
    return {
        .red = light.colorRed,
        .green = light.colorGreen,
        .blue = light.colorBlue,
        .alpha = light.colorAlpha,
    };
}

[[nodiscard]] PerspectiveCamera3D prefabCamera(
    const AssetFormat::PrefabCamera3DDesc& camera) noexcept
{
    return {
        .verticalFovDegrees = camera.verticalFovRadians * RadiansToDegrees,
        .nearPlaneMeters = camera.nearPlane,
        .farPlaneMeters = camera.farPlane,
        .active = camera.active,
    };
}

[[nodiscard]] DirectionalLight3D prefabDirectionalLight(
    const AssetFormat::PrefabLight3DDesc& light) noexcept
{
    return {
        .color = prefabLightColor(light),
        .intensity = light.intensity,
        .active = light.active,
    };
}

[[nodiscard]] PointLight3D prefabPointLight(
    const AssetFormat::PrefabLight3DDesc& light) noexcept
{
    return {
        .color = prefabLightColor(light),
        .intensity = light.intensity,
        .influenceRadiusMeters = light.rangeMeters,
        .active = light.active,
    };
}

[[nodiscard]] SpotLight3D prefabSpotLight(
    const AssetFormat::PrefabLight3DDesc& light) noexcept
{
    return {
        .color = prefabLightColor(light),
        .intensity = light.intensity,
        .influenceRadiusMeters = light.rangeMeters,
        .innerConeHalfAngleDegrees = light.innerConeRadians * RadiansToDegrees,
        .outerConeHalfAngleDegrees = light.outerConeRadians * RadiansToDegrees,
        .active = light.active,
    };
}

[[nodiscard]] Core::Status validatePrefabNode(
    const AssetFormat::PrefabNodeView& node, Core::usize index) noexcept
{
    if (node.stableNodeId == 0U) {
        return Core::failure(SceneErrorCode::InvalidComponent,
                             "prefab stableNodeId must be non-zero");
    }
    if (node.parentIndex < -1 ||
        node.parentIndex >= static_cast<Core::i32>(index)) {
        return Core::failure(
            SceneErrorCode::CorruptHierarchy,
            "prefab parentIndex must be -1 or refer to a prior node");
    }
    const LocalTransform local{
        .position = {node.positionX, node.positionY, node.positionZ},
        .rotation = {node.rotationX, node.rotationY, node.rotationZ,
                     node.rotationW},
        .scale = {node.scaleX, node.scaleY, node.scaleZ},
    };
    if (!isValid(local)) {
        return Core::failure(SceneErrorCode::InvalidTransform,
                             "prefab node local transform is invalid");
    }
    if (static_cast<Core::u16>(node.nodeKind) >=
        AssetFormat::PrefabNodeKindCount) {
        return Core::failure(SceneErrorCode::InvalidComponent,
                             "prefab node kind is unsupported");
    }
    const bool meshIdPresent = static_cast<bool>(node.meshId);
    const bool materialIdPresent = static_cast<bool>(node.materialId);
    if (node.hasMesh != meshIdPresent ||
        node.hasMaterial != materialIdPresent ||
        node.hasMesh != node.hasMaterial ||
        (node.hasMesh && node.meshId == node.materialId)) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "prefab mesh flags and paired AssetIds are inconsistent");
    }

    const bool hasCamera = node.camera.has_value();
    const bool hasLight = node.light.has_value();
    bool payloadMatchesKind = false;
    switch (node.nodeKind) {
    case AssetFormat::PrefabNodeKind::Node3D:
    case AssetFormat::PrefabNodeKind::Marker3D:
        payloadMatchesKind = !node.hasMesh && !hasCamera && !hasLight;
        break;
    case AssetFormat::PrefabNodeKind::Mesh3D:
    case AssetFormat::PrefabNodeKind::SkinnedMesh3D:
        payloadMatchesKind = node.hasMesh && !hasCamera && !hasLight;
        break;
    case AssetFormat::PrefabNodeKind::Camera3D:
        payloadMatchesKind = !node.hasMesh && hasCamera && !hasLight &&
                             isValid(prefabCamera(*node.camera));
        break;
    case AssetFormat::PrefabNodeKind::DirectionalLight3D:
        payloadMatchesKind = !node.hasMesh && !hasCamera && hasLight &&
                             isValid(prefabDirectionalLight(*node.light));
        break;
    case AssetFormat::PrefabNodeKind::PointLight3D:
        payloadMatchesKind = !node.hasMesh && !hasCamera && hasLight &&
                             isValid(prefabPointLight(*node.light));
        break;
    case AssetFormat::PrefabNodeKind::SpotLight3D:
        payloadMatchesKind = !node.hasMesh && !hasCamera && hasLight &&
                             isValid(prefabSpotLight(*node.light));
        break;
    }
    if (!payloadMatchesKind) {
        return Core::failure(
            SceneErrorCode::InvalidComponent,
            "prefab node kind and canonical typed payload do not match");
    }
    return Core::success();
}

} // namespace

Core::Result<std::vector<EntityId>> instantiatePrefab(
    World& world,
    const AssetFormat::PrefabPayloadView& prefab,
    PrefabMeshBinding meshBinding) noexcept
{
    std::vector<EntityId> created;
    const auto rollback = [&]() noexcept {
        for (auto it = created.rbegin(); it != created.rend(); ++it) {
            if (world.contains(*it)) {
                (void)world.destroySubtree(*it);
            }
        }
        created.clear();
        (void)world.updateWorldTransforms();
    };

    try {
        if (prefab.schemaVersion != AssetFormat::PrefabWire::SchemaVersion) {
            return Core::failure(
                SceneErrorCode::ConstructionFailed,
                "prefab instantiate requires the current schema version");
        }
        if (prefab.nodes.empty()) {
            return Core::failure(SceneErrorCode::InvalidComponent,
                                 "prefab has no nodes to instantiate");
        }
        if (prefab.nodes.size() > AssetFormat::PrefabWire::MaxNodes) {
            return Core::failure(SceneErrorCode::CapacityExceeded,
                                 "prefab node count exceeds the product limit");
        }
        const Core::usize existingCount = world.entityCount();
        if (existingCount > world.entityCapacity() ||
            prefab.nodes.size() > world.entityCapacity() - existingCount) {
            return Core::failure(
                SceneErrorCode::CapacityExceeded,
                "prefab instantiate exceeds remaining World entity capacity");
        }
        for (Core::usize index = 0; index < prefab.nodes.size(); ++index) {
            const auto& node = prefab.nodes[index];
            if (auto status = validatePrefabNode(node, index); !status) {
                return Core::failure(std::move(status.error()));
            }
            if (std::any_of(
                    prefab.nodes.begin(),
                    prefab.nodes.begin() + static_cast<std::ptrdiff_t>(index),
                    [&node](const auto& previous) {
                        return previous.stableNodeId == node.stableNodeId;
                    })) {
                return Core::failure(
                    SceneErrorCode::InvalidComponent,
                    "prefab stableNodeId values must be unique");
            }
        }

        created.reserve(prefab.nodes.size());
        for (const AssetFormat::PrefabNodeView& node : prefab.nodes) {
            auto entity = world.createEntity({
                .position = {node.positionX, node.positionY, node.positionZ},
                .rotation = {node.rotationX, node.rotationY, node.rotationZ,
                             node.rotationW},
                .scale = {node.scaleX, node.scaleY, node.scaleZ},
            });
            if (!entity) {
                rollback();
                return Core::failure(std::move(entity.error()));
            }
            created.push_back(*entity);

            if (node.parentIndex >= 0) {
                if (auto status = world.setParent(
                        *entity,
                        created[static_cast<Core::usize>(node.parentIndex)],
                        ReparentMode::KeepLocal);
                    !status) {
                    rollback();
                    return Core::failure(std::move(status.error()));
                }
            }

            Core::Status componentStatus = Core::success();
            switch (node.nodeKind) {
            case AssetFormat::PrefabNodeKind::Node3D:
            case AssetFormat::PrefabNodeKind::Marker3D:
                break;
            case AssetFormat::PrefabNodeKind::Camera3D:
                componentStatus = world.setPerspectiveCamera3D(
                    *entity, prefabCamera(*node.camera));
                break;
            case AssetFormat::PrefabNodeKind::DirectionalLight3D:
                componentStatus = world.setDirectionalLight3D(
                    *entity, prefabDirectionalLight(*node.light));
                break;
            case AssetFormat::PrefabNodeKind::PointLight3D:
                componentStatus = world.setPointLight3D(
                    *entity, prefabPointLight(*node.light));
                break;
            case AssetFormat::PrefabNodeKind::SpotLight3D:
                componentStatus = world.setSpotLight3D(
                    *entity, prefabSpotLight(*node.light));
                break;
            case AssetFormat::PrefabNodeKind::Mesh3D:
            case AssetFormat::PrefabNodeKind::SkinnedMesh3D: {
                Asset::AssetHandle meshAsset = meshBinding.resolveMesh
                    ? meshBinding.resolveMesh(node.meshId)
                    : meshBinding.mesh;
                Asset::AssetHandle materialAsset = meshBinding.resolveMaterial
                    ? meshBinding.resolveMaterial(node.materialId)
                    : meshBinding.material;
                if (!meshAsset || !materialAsset) {
                    rollback();
                    return Core::failure(
                        SceneErrorCode::UnresolvedMesh,
                        "prefab mesh/material AssetHandle resolve returned empty");
                }
                const Render::RenderBoundingSphereInput bounds =
                    meshBinding.resolveLocalBounds
                        ? meshBinding.resolveLocalBounds(node.meshId)
                        : meshBinding.localBounds;
                const Render::RenderLinearColor color =
                    meshBinding.resolveBaseColor
                        ? meshBinding.resolveBaseColor(node.materialId)
                        : meshBinding.baseColorFactor;
                const Render::Mesh3DAlphaMode alphaMode =
                    meshBinding.resolveAlphaMode
                        ? meshBinding.resolveAlphaMode(node.materialId)
                        : meshBinding.alphaMode;
                if (node.nodeKind == AssetFormat::PrefabNodeKind::Mesh3D) {
                    const MeshRenderer3D mesh{
                        .mesh = meshAsset,
                        .material = materialAsset,
                        .localBounds = bounds,
                        .baseColorFactor = color,
                        .alphaMode = alphaMode,
                        .visible = node.visible,
                    };
                    if (!isValid(mesh)) {
                        rollback();
                        return Core::failure(
                            SceneErrorCode::InvalidComponent,
                            "prefab static mesh binding is invalid");
                    }
                    componentStatus = world.setMeshRenderer3D(*entity, mesh);
                } else {
                    const SkinnedMeshRenderer3D mesh{
                        .mesh = meshAsset,
                        .material = materialAsset,
                        .localBounds = bounds,
                        .baseColorFactor = color,
                        .alphaMode = alphaMode,
                        .visible = node.visible,
                    };
                    if (!isValid(mesh)) {
                        rollback();
                        return Core::failure(
                            SceneErrorCode::InvalidComponent,
                            "prefab skinned mesh binding is invalid");
                    }
                    componentStatus =
                        world.setSkinnedMeshRenderer3D(*entity, mesh);
                }
                break;
            }
            }
            if (!componentStatus) {
                rollback();
                return Core::failure(std::move(componentStatus.error()));
            }
        }
        if (auto status = world.updateWorldTransforms(); !status) {
            rollback();
            return Core::failure(std::move(status.error()));
        }
        return created;
    } catch (const std::bad_alloc&) {
        rollback();
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "prefab instantiate allocation failed");
    } catch (const std::exception& exception) {
        rollback();
        return Core::failure(SceneErrorCode::ConstructionFailed,
                             exception.what());
    } catch (...) {
        rollback();
        return Core::failure(
            SceneErrorCode::ConstructionFailed,
            "prefab instantiate callback threw an unknown exception");
    }
}

} // namespace Tina::Scene
