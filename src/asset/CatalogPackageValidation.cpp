#include <tina/asset/CatalogPackageValidation.hpp>

#include "Utf8Path.hpp"

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/TileMapChunkPayload.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/text/Utf8.hpp>

#include <cstdint>
#include <filesystem>
#include <new>
#include <string>
#include <system_error>
#include <utility>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::Error filesystemError(Core::ErrorCode code, std::string_view message,
                                          std::error_code errorCode)
{
    Core::Error error{code, message};
    if (errorCode)
    {
        error.setNativeCode(static_cast<Core::i64>(errorCode.value()));
        error.addContext("native", errorCode.message());
    }
    return error;
}

[[nodiscard]] Core::Error withEntryContext(Core::Error error, const CatalogEntry& entry,
                                           std::string_view phase)
{
    const auto assetIdText = entry.assetId.canonicalText();
    error.addContext("assetId", std::string_view(assetIdText.data(), assetIdText.size()));
    error.addContext("validateCatalogPackageOnDisk", phase);
    return error;
}

[[nodiscard]] bool hasPathEscapeComponent(const std::filesystem::path& relative) noexcept
{
    for (const auto& part : relative)
    {
        if (part == "..")
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] Core::Result<std::string> resolveObjectPath(std::string_view catalogRootUtf8,
                                                          const CatalogEntry& entry)
{
    auto artifactPath = AssetFormat::makeCookedArtifactPath(entry.assetKind, entry.assetId);
    if (!artifactPath)
    {
        return Core::failure(
            std::move(artifactPath.error()).withContext("validateCatalogPackageOnDisk", "makeCookedArtifactPath"));
    }

    const auto root = Detail::pathFromUtf8Bytes(catalogRootUtf8);
    const auto relative = Detail::pathFromUtf8Bytes(artifactPath->view());
    if (relative.is_absolute() || hasPathEscapeComponent(relative))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "artifact relative path is not safe");
    }

    const auto fullPath = root / relative;
    const auto generic = fullPath.generic_u8string();
    return std::string(generic.begin(), generic.end());
}

[[nodiscard]] Core::Status validateFilePresenceAndSize(std::string_view utf8Path, Core::u64 expectedBytes)
{
    if (utf8Path.empty() || !Core::isStrictUtf8WithoutNul(utf8Path))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "resolved object path is invalid");
    }

    std::error_code errorCode;
    const auto path = Detail::pathFromUtf8Bytes(utf8Path);
    const auto status = std::filesystem::status(path, errorCode);
    if (errorCode)
    {
        if (errorCode == std::errc::no_such_file_or_directory)
        {
            return Core::failure(
                filesystemError(Core::CoreErrorCode::NotFound, "catalog package object file not found", errorCode));
        }
        if (errorCode == std::errc::permission_denied)
        {
            return Core::failure(filesystemError(Core::CoreErrorCode::PermissionDenied,
                                                 "catalog package object file permission denied", errorCode));
        }
        return Core::failure(filesystemError(Core::CoreErrorCode::Io,
                                             "failed to query catalog package object file", errorCode));
    }
    if (!std::filesystem::exists(status))
    {
        return Core::failure(Core::CoreErrorCode::NotFound, "catalog package object file not found");
    }
    if (!std::filesystem::is_regular_file(status))
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "catalog package object path is not a regular file");
    }

    const auto fileSize = std::filesystem::file_size(path, errorCode);
    if (errorCode)
    {
        if (errorCode == std::errc::permission_denied)
        {
            return Core::failure(filesystemError(Core::CoreErrorCode::PermissionDenied,
                                                 "catalog package object size permission denied", errorCode));
        }
        return Core::failure(filesystemError(Core::CoreErrorCode::Io,
                                             "failed to query catalog package object size", errorCode));
    }
    if (fileSize != static_cast<std::uintmax_t>(expectedBytes))
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "catalog package object file size does not match catalog entry");
    }
    return Core::success();
}

} // namespace

Core::Status validateCatalogPackageOnDisk(std::string_view catalogRootUtf8, const CatalogSnapshot& catalog,
                                          CatalogPackageValidationConfig config)
{
    if (!catalog)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog snapshot is empty");
    }
    if (catalogRootUtf8.empty() || !Core::isStrictUtf8WithoutNul(catalogRootUtf8))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog root path is invalid");
    }
    if (config.verifyContent &&
        (config.file.memoryResource == nullptr || config.file.maxFileBytes == 0 ||
         config.file.maxFileBytes > Core::MaxReadFileBytes ||
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

            auto objectPath = resolveObjectPath(catalogRootUtf8, *entry);
            if (!objectPath)
            {
                return Core::failure(withEntryContext(std::move(objectPath.error()), *entry, "resolvePath"));
            }

            auto presence = validateFilePresenceAndSize(*objectPath, entry->cookedFileBytes);
            if (!presence)
            {
                return Core::failure(withEntryContext(std::move(presence.error()), *entry, "presence"));
            }

            if (!config.verifyContent)
            {
                continue;
            }

            // Full validation must not be weakened by the nested file config.
            auto asset = loadCookedAssetFromCatalog(catalogRootUtf8, catalog, entry->assetId, config.file);
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
    } catch (const std::filesystem::filesystem_error& exception)
    {
        return Core::failure(filesystemError(Core::CoreErrorCode::Io,
                                             "catalog package validation filesystem failure", exception.code()));
    } catch (...)
    {
        return Core::failure(Core::CoreErrorCode::Internal, "catalog package validation failed unexpectedly");
    }

    return Core::success();
}

} // namespace Tina::Asset
