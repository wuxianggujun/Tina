#include <tina/asset/AssetTypedViews.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <new>
#include <vector>

namespace Tina::Asset {

Core::Result<OwnedBitmapFont> parseBitmapFontFromCooked(const CookedAssetFile& file)
try {
    if (!file || file.header().assetKind != AssetFormat::AssetKind::Font ||
        file.header().assetTypeVersion != AssetFormat::BitmapFontWire::SchemaVersion)
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "Cooked asset is not a supported bitmap font");
    auto font = AssetFormat::parseBitmapFontPayload(file.payload());
    if (!font) return Core::failure(font.error());
    if (file.header().dependencyCount != font->descriptor().pages.size())
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "Bitmap font page dependency count mismatch");
    OwnedBitmapFont result{std::move(*font), {}};
    for (Core::u32 index = 0; index < file.header().dependencyCount; ++index) {
        auto dependency = file.dependency(index);
        if (!dependency || !dependency->assetId || dependency->expectedKind != AssetFormat::AssetKind::Texture2D ||
            dependency->flags != AssetFormat::DependencyFlags::Required ||
            std::find(result.textureIds.begin(), result.textureIds.end(), dependency->assetId) != result.textureIds.end())
            return Core::failure(AssetErrorCode::CatalogEntryMismatch, "Invalid bitmap font page dependency");
        result.textureIds.push_back(dependency->assetId);
    }
    return result;
} catch (const std::bad_alloc&) { return Core::failure(Core::CoreErrorCode::OutOfMemory, "Bitmap font dependency allocation failed"); }

Core::Result<AssetFormat::Texture2DPayloadView>
parseTexture2DFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::Texture2D ||
        file.header().assetTypeVersion != AssetFormat::Texture2DWire::SchemaVersion)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "cooked asset is not a supported Texture2D");
    }
    return AssetFormat::parseTexture2DPayload(file.payload());
}

Core::Result<Text::BitmapFontAtlas> loadBitmapFontAtlasFromCooked(
    const CookedAssetFile& file, std::span<const CookedAssetFile* const> pages)
try {
    auto loaded = parseBitmapFontFromCooked(file);
    if (!loaded) return Core::failure(loaded.error());
    if (pages.size() != loaded->textureIds.size())
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "Bitmap font page count mismatch");
    std::vector<std::vector<Core::u8>> pixels;
    Core::u64 totalBytes = 0;
    for (Core::usize index = 0; index < pages.size(); ++index) {
        auto found = std::find_if(pages.begin(), pages.end(), [&](const auto* page) {
            return page && *page && page->header().assetId == loaded->textureIds[index];
        });
        if (found == pages.end()) return Core::failure(AssetErrorCode::CatalogEntryMismatch, "Bitmap font page asset is missing");
        auto texture = parseTexture2DFromCooked(**found);
        if (!texture) return Core::failure(texture.error());
        const auto& metrics = loaded->font.descriptor().pages[index];
        if (auto status = AssetFormat::validateBitmapFontTexturePage(metrics, *texture); !status) return Core::failure(status.error());
        const auto bytes = texture->basePixels();
        if (bytes.size() > 64ULL * 1024ULL * 1024ULL - totalBytes)
            return Core::failure(AssetErrorCode::CatalogEntryMismatch, "Bitmap font pixel budget exceeded");
        totalBytes += bytes.size();
        const auto* begin = reinterpret_cast<const Core::u8*>(bytes.data());
        pixels.emplace_back(begin, begin + bytes.size());
    }
    return Text::BitmapFontAtlas::Create(std::move(loaded->font), std::move(pixels));
} catch (const std::bad_alloc&) { return Core::failure(Core::CoreErrorCode::OutOfMemory, "Bitmap font atlas allocation failed"); }

Core::Result<AssetFormat::ShaderPayloadView>
parseShaderFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::Shader ||
        file.header().assetTypeVersion != AssetFormat::ShaderWire::SchemaVersion)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "cooked asset is not a supported Shader");
    }
    return AssetFormat::parseShaderPayload(file.payload());
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

Core::Result<AssetFormat::SkinnedMeshPayloadView> parseSkinnedMeshFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::SkinnedMesh ||
        file.header().assetTypeVersion != AssetFormat::SkinnedMeshWire::SchemaVersion)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "cooked asset is not a supported SkinnedMesh");
    }
    return AssetFormat::parseSkinnedMeshPayload(file.payload());
}

Core::Result<AssetFormat::AnimationClip3DPayloadView>
parseAnimationClip3DFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::AnimationClip3D ||
        file.header().assetTypeVersion != AssetFormat::AnimationClip3DWire::SchemaVersion)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "cooked asset is not a supported AnimationClip3D");
    }
    return AssetFormat::parseAnimationClip3DPayload(file.payload());
}

Core::Result<AssetFormat::MaterialPayloadView> parseMaterialFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::Material ||
        file.header().assetTypeVersion != AssetFormat::MaterialWire::SchemaVersion)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "cooked asset is not a supported Material");
    }
    auto view = AssetFormat::parseMaterialPayload(file.payload());
    if (!view)
    {
        return Core::failure(std::move(view.error()));
    }
    if (file.header().dependencyCount != view->textureDependencyCount())
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "Material texture flags and cooked dependencies do not match");
    }
    for (Core::u32 index = 0; index < file.header().dependencyCount; ++index)
    {
        const auto dependency = file.dependency(index);
        if (!dependency || !dependency->assetId ||
            dependency->expectedKind != AssetFormat::AssetKind::Texture2D ||
            dependency->flags != AssetFormat::DependencyFlags::Required)
        {
            return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                 "Material dependencies must be required Texture2D assets");
        }
    }
    return *view;
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
        expected.reserve(owned.nodes.size() * 3U);
        for (const auto& node : owned.nodes)
        {
            if (node.animation)
                expected.push_back({.assetId = node.animation->clipId, .kind = AssetFormat::AssetKind::AnimationClip3D});
            if (!node.hasMesh)
            {
                continue;
            }
            expected.push_back(ExpectedDependency{.assetId = node.meshId,
                                                  .kind = node.nodeKind == AssetFormat::PrefabNodeKind::SkinnedMesh3D
                                                      ? AssetFormat::AssetKind::SkinnedMesh : AssetFormat::AssetKind::StaticMesh});
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
            const bool kindMatches = required.kind == AssetFormat::AssetKind::Invalid
                                         ? dependency &&
                                               (dependency->expectedKind == AssetFormat::AssetKind::StaticMesh ||
                                                dependency->expectedKind == AssetFormat::AssetKind::SkinnedMesh)
                                         : dependency && dependency->expectedKind == required.kind;
            if (!dependency || dependency->assetId != required.assetId ||
                !kindMatches ||
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
                             "cooked asset is not a current Fx2D asset with one Sprite dependency");
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

Core::Result<OwnedPrefab2DPayload> parsePrefab2DFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::Prefab2D ||
        file.header().assetTypeVersion != AssetFormat::Prefab2DWire::SchemaVersion)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "cooked asset is not a supported Prefab2D");
    }
    OwnedPrefab2DPayload owned{};
    auto view = AssetFormat::parsePrefab2DPayload(file.payload(), owned.entities);
    if (!view)
    {
        return Core::failure(std::move(view.error()));
    }
    auto expected = AssetFormat::collectPrefab2DDependencies(owned.entities);
    if (!expected)
    {
        return Core::failure(std::move(expected.error()));
    }
    if (file.header().dependencyCount != expected->size())
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "Prefab2D dependency count does not match payload references");
    }
    for (Core::u32 index = 0; index < file.header().dependencyCount; ++index)
    {
        const auto dependency = file.dependency(index);
        if (!dependency || dependency->assetId != (*expected)[index].assetId ||
            dependency->expectedKind != (*expected)[index].expectedKind ||
            dependency->flags != AssetFormat::DependencyFlags::Required)
        {
            return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                 "Prefab2D cooked dependencies do not match payload references");
        }
    }
    owned.view = *view;
    owned.view.entities = owned.entities;
    return owned;
}

} // namespace Tina::Asset
