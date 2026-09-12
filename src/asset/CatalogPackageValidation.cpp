#include <tina/asset/CatalogPackageValidation.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/TileMapChunkPayload.hpp>

#include <new>
#include <utility>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::Error withEntryContext(Core::Error error, const CatalogEntry& entry,
                                           std::string_view phase)
{
    const auto assetIdText = entry.assetId.canonicalText();
    error.addContext("assetId", std::string_view(assetIdText.data(), assetIdText.size()));
    error.addContext("validateCatalogPackage", phase);
    return error;
}

} // namespace

Core::Status validateCatalogPackage(const CatalogSnapshot& catalog,
                                          CatalogPackageValidationConfig config)
{
    if (!catalog)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog snapshot is empty");
    }
    if (!catalog.packageReader())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog has no mounted package");
    }
    if (config.verifyContent &&
        (config.file.maxFileBytes == 0 ||
         config.file.maxFileBytes > AssetFormat::Wire::MaxCookedFileBytes))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid content verification file config");
    }
    config.file.verifyContentHash = true;

    try
    {
        for (Core::u32 entryIndex = 0; entryIndex < catalog.entryCount(); ++entryIndex)
        {
            const auto entry = catalog.entry(entryIndex);
            if (!entry)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "catalog entry missing during package validation");
            }

            auto objectPath = AssetFormat::makeCookedArtifactPath(entry->assetKind, entry->assetId);
            if (!objectPath)
            {
                return Core::failure(withEntryContext(std::move(objectPath.error()), *entry, "resolvePath"));
            }

            const auto size = catalog.packageReader().getFileSize(objectPath->view());
            if (!size)
            {
                return Core::failure(withEntryContext(Core::Error{Core::CoreErrorCode::NotFound,
                    "catalog object not found in package"}, *entry, "presence"));
            }
            if (*size != entry->cookedFileBytes)
            {
                return Core::failure(withEntryContext(Core::Error{AssetErrorCode::CatalogEntryMismatch,
                    "package object size does not match catalog"}, *entry, "size"));
            }

            if (!config.verifyContent)
            {
                continue;
            }

            // Full validation must not be weakened by the nested file config.
            auto asset = loadCookedAssetFromCatalog(catalog, entry->assetId, config.file);
            if (!asset)
            {
                return Core::failure(withEntryContext(std::move(asset.error()), *entry, "content"));
            }
            if (config.verifyTypedPayload)
            {
                if (entry->assetKind == AssetFormat::AssetKind::Texture2D)
                {
                    auto typed = parseTexture2DFromCooked(*asset);
                    if (!typed)
                    {
                        return Core::failure(
                            withEntryContext(std::move(typed.error()), *entry, "typedTexture2D"));
                    }
                } else if (entry->assetKind == AssetFormat::AssetKind::Sprite)
                {
                    auto typed = parseSpriteFromCooked(*asset);
                    if (!typed)
                    {
                        return Core::failure(withEntryContext(std::move(typed.error()), *entry, "typedSprite"));
                    }
                } else if (entry->assetKind == AssetFormat::AssetKind::SpriteAnimationClip)
                {
                    auto typed = parseSpriteAnimationClipFromCooked(*asset);
                    if (!typed)
                    {
                        return Core::failure(
                            withEntryContext(std::move(typed.error()), *entry, "typedSpriteAnimationClip"));
                    }
                } else if (entry->assetKind == AssetFormat::AssetKind::Tileset)
                {
                    auto typed = parseTilesetFromCooked(*asset);
                    if (!typed)
                    {
                        return Core::failure(withEntryContext(std::move(typed.error()), *entry, "typedTileset"));
                    }
                } else if (entry->assetKind == AssetFormat::AssetKind::TileMap)
                {
                    auto typed = parseTileMapFromCooked(*asset);
                    if (!typed)
                    {
                        return Core::failure(withEntryContext(std::move(typed.error()), *entry, "typedTileMap"));
                    }
                } else if (entry->assetKind == AssetFormat::AssetKind::TileMapChunk)
                {
                    auto typed = AssetFormat::parseTileMapChunkPayload(asset->payload());
                    if (!typed)
                    {
                        return Core::failure(
                            withEntryContext(std::move(typed.error()), *entry, "typedTileMapChunk"));
                    }
                } else if (entry->assetKind == AssetFormat::AssetKind::AudioClip)
                {
                    auto typed = parseAudioClipFromCooked(*asset);
                    if (!typed)
                    {
                        return Core::failure(withEntryContext(std::move(typed.error()), *entry, "typedAudioClip"));
                    }
                } else if (entry->assetKind == AssetFormat::AssetKind::StaticMesh)
                {
                    auto typed = parseStaticMeshFromCooked(*asset);
                    if (!typed)
                    {
                        return Core::failure(withEntryContext(std::move(typed.error()), *entry, "typedStaticMesh"));
                    }
                } else if (entry->assetKind == AssetFormat::AssetKind::SkinnedMesh)
                {
                    auto typed = parseSkinnedMeshFromCooked(*asset);
                    if (!typed)
                    {
                        return Core::failure(withEntryContext(std::move(typed.error()), *entry, "typedSkinnedMesh"));
                    }
                } else if (entry->assetKind == AssetFormat::AssetKind::AnimationClip3D)
                {
                    auto typed = parseAnimationClip3DFromCooked(*asset);
                    if (!typed)
                    {
                        return Core::failure(withEntryContext(std::move(typed.error()), *entry,
                                                              "typedAnimationClip3D"));
                    }
                } else if (entry->assetKind == AssetFormat::AssetKind::Material)
                {
                    auto typed = parseMaterialFromCooked(*asset);
                    if (!typed)
                    {
                        return Core::failure(withEntryContext(std::move(typed.error()), *entry, "typedMaterial"));
                    }
                } else if (entry->assetKind == AssetFormat::AssetKind::EnvironmentMap)
                {
                    auto typed = parseEnvironmentMapFromCooked(*asset);
                    if (!typed)
                    {
                        return Core::failure(
                            withEntryContext(std::move(typed.error()), *entry, "typedEnvironmentMap"));
                    }
                } else if (entry->assetKind == AssetFormat::AssetKind::Prefab)
                {
                    auto typed = parsePrefabFromCooked(*asset);
                    if (!typed)
                    {
                        return Core::failure(withEntryContext(std::move(typed.error()), *entry, "typedPrefab"));
                    }
                } else if (entry->assetKind == AssetFormat::AssetKind::NavigationGrid2D)
                {
                    auto typed = parseNavigationGrid2DFromCooked(*asset);
                    if (!typed) {
                        return Core::failure(withEntryContext(std::move(typed.error()), *entry, "typedNavigationGrid2D"));
                    }
                } else if (entry->assetKind == AssetFormat::AssetKind::Fx2D)
                {
                    auto typed = parseFx2DFromCooked(*asset);
                    if (!typed) {
                        return Core::failure(withEntryContext(std::move(typed.error()), *entry, "typedFx2D"));
                    }
                }
            }
        }
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "catalog package validation allocation failed");
    } catch (...)
    {
        return Core::failure(Core::CoreErrorCode::Internal, "catalog package validation failed unexpectedly");
    }

    return Core::success();
}

} // namespace Tina::Asset
