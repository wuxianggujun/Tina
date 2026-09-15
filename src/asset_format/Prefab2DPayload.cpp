#include <tina/asset_format/Prefab2DPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <algorithm>
#include <utility>

namespace Tina::AssetFormat {
namespace {

[[nodiscard]] bool containsPrefabInstance2D(std::span<const World2DEntityDesc> entities) noexcept
{
    return std::any_of(entities.begin(), entities.end(), [](const World2DEntityDesc& entity) {
        return entity.nodeKind == World2DNodeKind::PrefabInstance2D;
    });
}

} // namespace

Core::Status validatePrefab2DSnapshot(std::span<const World2DEntityDesc> entities) noexcept
{
    if (entities.empty())
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "Prefab2D payload requires a non-empty subtree");
    }
    if (entities.front().parentStableEntityId != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "Prefab2D payload must start with a scene-root entity");
    }
    if (entities.front().nodeKind == World2DNodeKind::CollisionShape2D)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "Prefab2D payload root cannot be CollisionShape2D");
    }
    const auto extraRoots = std::count_if(
        entities.begin() + 1, entities.end(),
        [](const World2DEntityDesc& entity) { return entity.parentStableEntityId == 0U; });
    if (extraRoots != 0)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "Prefab2D payload must contain a single rooted tree");
    }
    if (containsPrefabInstance2D(entities))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "Prefab2D payload cannot contain PrefabInstance2D");
    }
    return validateWorld2DSnapshotDesc(World2DSnapshotDesc{.entities = entities});
}

Core::Result<std::vector<CookedAssetWriteDependency>> collectPrefab2DDependencies(
    std::span<const World2DEntityDesc> entities)
{
    if (const Core::Status status = validatePrefab2DSnapshot(entities); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    try
    {
        std::vector<CookedAssetWriteDependency> dependencies;
        const auto append = [&dependencies](Core::AssetId assetId, AssetKind kind) {
            if (!assetId)
            {
                return;
            }
            dependencies.push_back(CookedAssetWriteDependency{
                .assetId = assetId,
                .expectedKind = kind,
                .flags = DependencyFlags::Required,
            });
        };
        for (const World2DEntityDesc& entity : entities)
        {
            if (entity.sprite)
            {
                append(entity.sprite->spriteId, AssetKind::Sprite);
                append(entity.sprite->normalTextureId, AssetKind::Texture2D);
                append(entity.sprite->shaderId, AssetKind::Shader);
            }
            if (entity.spriteAnimation)
            {
                append(entity.spriteAnimation->clipId, AssetKind::SpriteAnimationClip);
            }
            if (entity.resource)
            {
                const auto kind = resourceAssetKindFor(entity.nodeKind);
                if (!kind.has_value() || *kind == AssetKind::Prefab2D)
                {
                    return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                         "Prefab2D payload resource node has no collectable dependency kind");
                }
                append(entity.resource->assetId, *kind);
            }
        }
        std::sort(dependencies.begin(), dependencies.end(),
                  [](const CookedAssetWriteDependency& left, const CookedAssetWriteDependency& right) {
                      return left.assetId < right.assetId;
                  });
        std::vector<CookedAssetWriteDependency> unique;
        unique.reserve(dependencies.size());
        for (const CookedAssetWriteDependency& dependency : dependencies)
        {
            if (!unique.empty() && unique.back().assetId == dependency.assetId)
            {
                if (unique.back().expectedKind != dependency.expectedKind)
                {
                    return Core::failure(AssetFormatErrorCode::DependencyTypeMismatch,
                                         "Prefab2D payload reuses an AssetId with conflicting kinds");
                }
                continue;
            }
            unique.push_back(dependency);
        }
        return unique;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "Prefab2D dependency collection allocation failed");
    }
}

Core::Result<std::vector<std::byte>> writePrefab2DPayloadBytes(std::span<const World2DEntityDesc> entities)
{
    if (const Core::Status status = validatePrefab2DSnapshot(entities); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return writeWorld2DSnapshotBytes(World2DSnapshotDesc{.entities = entities});
}

Core::Result<World2DSnapshotView> parsePrefab2DPayload(std::span<const std::byte> payload,
                                                      std::vector<World2DEntityDesc>& entityStorage)
{
    std::vector<World2DEntityDesc> storage;
    auto snapshot = parseWorld2DSnapshot(payload, storage);
    if (!snapshot)
    {
        return Core::failure(std::move(snapshot.error()));
    }
    if (!snapshot->gameplayBytes.empty() || snapshot->gameplaySchema != 0U || snapshot->gameplayVersion != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "Prefab2D payload cannot carry gameplay bytes");
    }
    if (const Core::Status status = validatePrefab2DSnapshot(storage); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    entityStorage.swap(storage);
    return World2DSnapshotView{
        .schemaVersion = snapshot->schemaVersion,
        .entities = entityStorage,
        .gameplaySchema = 0,
        .gameplayVersion = 0,
        .gameplayBytes = {},
    };
}

} // namespace Tina::AssetFormat
