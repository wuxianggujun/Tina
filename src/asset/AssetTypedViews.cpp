#include <tina/asset/AssetTypedViews.hpp>

#include <tina/asset/AssetErrors.hpp>

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
    if (file.header().assetKind != AssetFormat::AssetKind::Prefab)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "cooked asset is not Prefab");
    }
    OwnedPrefabPayload owned{};
    auto view = AssetFormat::parsePrefabPayload(file.payload(), owned.nodes);
    if (!view)
    {
        return Core::failure(std::move(view.error()));
    }
    // Count mesh/material flags must match dependency count (2 per meshed node).
    Core::u32 expectedDeps = 0;
    for (const auto& node : owned.nodes)
    {
        if (node.hasMesh)
        {
            expectedDeps += 2;
        }
    }
    if (file.header().dependencyCount != expectedDeps)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "prefab dependency count does not match mesh/material flags");
    }
    // Bind AssetIds from the dependency stream in node order (mesh, material) × N.
    Core::u32 depIndex = 0;
    for (auto& node : owned.nodes)
    {
        if (!node.hasMesh)
        {
            continue;
        }
        auto meshDep = file.dependency(depIndex++);
        auto materialDep = file.dependency(depIndex++);
        if (!meshDep || !materialDep)
        {
            return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                 "prefab dependency stream shorter than meshed nodes");
        }
        if (meshDep->expectedKind != AssetFormat::AssetKind::StaticMesh ||
            materialDep->expectedKind != AssetFormat::AssetKind::Material)
        {
            return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                 "prefab dependency kinds must be StaticMesh then Material");
        }
        node.meshId = meshDep->assetId;
        node.materialId = materialDep->assetId;
    }
    owned.view = *view;
    // view.nodes aliases owned.nodes — re-bind after mutation.
    owned.view.nodes = owned.nodes;
    return owned;
}

} // namespace Tina::Asset
