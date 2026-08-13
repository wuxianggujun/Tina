#include <tina/asset/AssetTypedViews.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <new>
#include <vector>

namespace Tina::Asset {

Core::Result<AssetFormat::Texture2DPayloadView> parseTexture2DFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::Texture2D)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "cooked asset is not Texture2D");
    }
    return AssetFormat::parseTexture2DPayload(file.payload());
}

Core::Result<AssetFormat::EnvironmentMapPayloadView>
parseEnvironmentMapFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::EnvironmentMap ||
        file.header().assetTypeVersion != AssetFormat::EnvironmentMapWire::SchemaVersion)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "cooked asset is not a supported EnvironmentMap");
    }
    return AssetFormat::parseEnvironmentMapPayload(file.payload());
}

Core::Result<AssetFormat::SpritePayloadView> parseSpriteFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::Sprite)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "cooked asset is not Sprite");
    }
    return AssetFormat::parseSpritePayload(file.payload());
}

Core::Result<AssetFormat::SpriteAnimationClipPayloadView>
parseSpriteAnimationClipFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::SpriteAnimationClip)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "cooked asset is not SpriteAnimationClip");
    }
    if (file.header().assetTypeVersion != AssetFormat::SpriteAnimationClipWire::SchemaVersion)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "sprite animation asset type version is unsupported");
    }

    auto view = AssetFormat::parseSpriteAnimationClipPayload(file.payload());
    if (!view)
    {
        return Core::failure(std::move(view.error()));
    }
    if (file.header().dependencyCount != view->spriteDependencyCount)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "sprite animation dependency count does not match payload");
    }

    try
    {
        std::vector<bool> referencedDependencies(view->spriteDependencyCount, false);
        for (Core::u32 dependencyIndex = 0; dependencyIndex < view->spriteDependencyCount;
             ++dependencyIndex)
        {
            const auto dependency = file.dependency(dependencyIndex);
            if (!dependency || !dependency->assetId ||
                dependency->expectedKind != AssetFormat::AssetKind::Sprite ||
                dependency->flags != AssetFormat::DependencyFlags::Required)
            {
                return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                     "sprite animation dependencies must be required Sprite assets");
            }
        }
        for (Core::u32 frameIndex = 0; frameIndex < view->frameCount; ++frameIndex)
        {
            const auto frame = view->frame(frameIndex);
            if (!frame || static_cast<Core::usize>(frame->spriteDependencyIndex) >= referencedDependencies.size())
            {
                return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                     "sprite animation frame dependency index is invalid");
            }
            referencedDependencies[frame->spriteDependencyIndex] = true;
        }
        for (const bool referenced : referencedDependencies)
        {
            if (!referenced)
            {
                return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                     "sprite animation contains an unused dependency");
            }
        }
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "sprite animation validation allocation failed");
    }
    return *view;
}

Core::Result<AssetFormat::TilesetPayloadView> parseTilesetFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::Tileset)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "cooked asset is not Tileset");
    }
    return AssetFormat::parseTilesetPayload(file.payload());
}

Core::Result<AssetFormat::TileMapPayloadView> parseTileMapFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::TileMap)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "cooked asset is not TileMap");
    }
    return AssetFormat::parseTileMapPayload(file.payload());
}

Core::Result<AssetFormat::AudioClipPayloadView> parseAudioClipFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::AudioClip)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "cooked asset is not AudioClip");
    }
    return AssetFormat::parseAudioClipPayload(file.payload());
}

Core::Result<AssetFormat::StaticMeshPayloadView> parseStaticMeshFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::StaticMesh)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "cooked asset is not StaticMesh");
    }
    return AssetFormat::parseStaticMeshPayload(file.payload());
}

Core::Result<AssetFormat::MaterialPayloadView> parseMaterialFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::Material)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "cooked asset is not Material");
    }
    return AssetFormat::parseMaterialPayload(file.payload());
}

Core::Result<OwnedPrefabPayload> parsePrefabFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::Prefab ||
        file.header().assetTypeVersion != AssetFormat::PrefabWire::SchemaVersion)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "cooked asset is not a supported Prefab");
    }
    OwnedPrefabPayload owned{};
    auto view = AssetFormat::parsePrefabPayload(file.payload(), owned.nodes);
    if (!view)
    {
        return Core::failure(std::move(view.error()));
    }
    try
    {
        struct ExpectedDependency final {
            Core::AssetId assetId{};
            AssetFormat::AssetKind kind = AssetFormat::AssetKind::Invalid;
        };
        std::vector<ExpectedDependency> expected;
        expected.reserve(owned.nodes.size() * 2U);
        for (const auto& node : owned.nodes)
        {
            if (!node.hasMesh)
            {
                continue;
            }
            expected.push_back(ExpectedDependency{.assetId = node.meshId,
                                                  .kind = AssetFormat::AssetKind::StaticMesh});
            expected.push_back(ExpectedDependency{.assetId = node.materialId,
                                                  .kind = AssetFormat::AssetKind::Material});
        }
        std::sort(expected.begin(), expected.end(), [](const ExpectedDependency& left,
                                                       const ExpectedDependency& right) {
            return left.assetId < right.assetId;
        });
        const auto duplicate = std::adjacent_find(
            expected.begin(), expected.end(), [](const ExpectedDependency& left, const ExpectedDependency& right) {
                return left.assetId == right.assetId && left.kind != right.kind;
            });
        if (duplicate != expected.end())
        {
            return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                 "prefab uses one AssetId with conflicting dependency kinds");
        }
        expected.erase(std::unique(expected.begin(), expected.end(),
                                   [](const ExpectedDependency& left, const ExpectedDependency& right) {
                                       return left.assetId == right.assetId;
                                   }),
                       expected.end());
        if (file.header().dependencyCount != expected.size())
        {
            return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                 "prefab dependency set does not match payload references");
        }
        for (Core::u32 index = 0; index < file.header().dependencyCount; ++index)
        {
            const auto dependency = file.dependency(index);
            const auto& required = expected[index];
            if (!dependency || dependency->assetId != required.assetId ||
                dependency->expectedKind != required.kind ||
                dependency->flags != AssetFormat::DependencyFlags::Required)
            {
                return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                     "prefab dependency set does not match payload references");
            }
        }
        owned.view = *view;
        owned.view.nodes = owned.nodes;
        return owned;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "prefab validation allocation failed");
    }
}

Core::Result<AssetFormat::NavigationGrid2DPayloadView>
parseNavigationGrid2DFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::NavigationGrid2D ||
        file.header().assetTypeVersion != AssetFormat::NavigationGrid2DWire::SchemaVersion ||
        file.header().dependencyCount != 0U)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "cooked asset is not an independent NavigationGrid2D v1 asset");
    }
    return AssetFormat::parseNavigationGrid2DPayload(file.payload());
}

Core::Result<AssetFormat::Fx2DPayloadDesc>
parseFx2DFromCooked(const CookedAssetFile& file)
{
    if (!file) {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::Fx2D ||
        file.header().assetTypeVersion != AssetFormat::Fx2DWire::SchemaVersion ||
        file.header().dependencyCount != 1U) {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "cooked asset is not an Fx2D v1 asset with one Sprite dependency");
    }
    const auto dependency = file.dependency(0U);
    if (!dependency || dependency->expectedKind != AssetFormat::AssetKind::Sprite ||
        dependency->flags != AssetFormat::DependencyFlags::Required) {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "Fx2D requires exactly one required Sprite dependency");
    }
    auto payload = AssetFormat::parseFx2DPayloadBytes(file.payload());
    if (!payload) return Core::failure(std::move(payload.error()));
    if (payload->spriteAssetId != dependency->assetId) {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "Fx2D payload Sprite AssetId does not match dependency");
    }
    return payload;
}

Core::Result<Navigation2D::NavigationGrid2DData>
loadNavigationGrid2DDataFromCooked(const CookedAssetFile& file,
                                   std::pmr::memory_resource& resource)
{
    auto payload = parseNavigationGrid2DFromCooked(file);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    return Navigation2D::NavigationGrid2DData::Create({
        .widthCells = payload->widthCells,
        .heightCells = payload->heightCells,
        .originXMeters = payload->originXMeters,
        .originYMeters = payload->originYMeters,
        .cellSizeMeters = payload->cellSizeMeters,
        .cellFlags = payload->cellFlags,
        .traversalCosts = payload->traversalCosts,
    }, resource);
}

} // namespace Tina::Asset
