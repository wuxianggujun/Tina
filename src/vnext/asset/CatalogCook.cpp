#include <tina/asset/CatalogCook.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackagePublish.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/io/ReadFile.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <string>
#include <utility>

namespace Tina::Asset {
namespace {

struct CookedPackage final {
    CatalogCookResult summary{};
    std::vector<CatalogPackageObjectBlob> objectViews{};
    std::vector<std::vector<std::byte>> objectStorage{};
};

[[nodiscard]] bool isKnownKindName(std::string_view name, AssetFormat::AssetKind& out) noexcept
{
    if (name == "Texture2D")
    {
        out = AssetFormat::AssetKind::Texture2D;
        return true;
    }
    if (name == "Shader")
    {
        out = AssetFormat::AssetKind::Shader;
        return true;
    }
    if (name == "Font")
    {
        out = AssetFormat::AssetKind::Font;
        return true;
    }
    if (name == "Sprite")
    {
        out = AssetFormat::AssetKind::Sprite;
        return true;
    }
    if (name == "Tileset")
    {
        out = AssetFormat::AssetKind::Tileset;
        return true;
    }
    if (name == "TileMap")
    {
        out = AssetFormat::AssetKind::TileMap;
        return true;
    }
    if (name == "StaticMesh")
    {
        out = AssetFormat::AssetKind::StaticMesh;
        return true;
    }
    if (name == "Material")
    {
        out = AssetFormat::AssetKind::Material;
        return true;
    }
    if (name == "Prefab")
    {
        out = AssetFormat::AssetKind::Prefab;
        return true;
    }
    if (name == "AudioClip")
    {
        out = AssetFormat::AssetKind::AudioClip;
        return true;
    }
    return false;
}

[[nodiscard]] bool isKnownPlatformName(std::string_view name, AssetFormat::TargetPlatform& out) noexcept
{
    if (name == "WindowsX64")
    {
        out = AssetFormat::TargetPlatform::WindowsX64;
        return true;
    }
    if (name == "LinuxX64")
    {
        out = AssetFormat::TargetPlatform::LinuxX64;
        return true;
    }
    if (name == "Any")
    {
        out = AssetFormat::TargetPlatform::Any;
        return true;
    }
    return false;
}

[[nodiscard]] std::string trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    {
        text.remove_suffix(1);
    }
    return std::string(text);
}

[[nodiscard]] std::vector<std::string> splitWs(std::string_view line)
{
    std::vector<std::string> tokens;
    std::string current;
    for (const char ch : line)
    {
        if (std::isspace(static_cast<unsigned char>(ch)))
        {
            if (!current.empty())
            {
                tokens.push_back(current);
                current.clear();
            }
        } else
        {
            current.push_back(ch);
        }
    }
    if (!current.empty())
    {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

[[nodiscard]] Core::Result<std::string> joinPath(std::string_view baseUtf8, std::string_view relativeOrAbsolute)
{
    const auto path = std::filesystem::u8path(relativeOrAbsolute);
    if (path.is_absolute())
    {
        const auto generic = path.generic_u8string();
        return std::string(generic.begin(), generic.end());
    }
    const auto full = std::filesystem::u8path(baseUtf8) / path;
    const auto generic = full.generic_u8string();
    return std::string(generic.begin(), generic.end());
}

[[nodiscard]] bool parseU32Token(std::string_view text, Core::u32& out) noexcept
{
    const auto [end, err] = std::from_chars(text.data(), text.data() + text.size(), out);
    return err == std::errc{} && end == text.data() + text.size();
}

[[nodiscard]] bool parseFloatToken(std::string_view text, float& out) noexcept
{
    const auto [end, err] = std::from_chars(text.data(), text.data() + text.size(), out);
    return err == std::errc{} && end == text.data() + text.size();
}

[[nodiscard]] std::optional<std::byte> parseHexByte(std::string_view text) noexcept
{
    if (text.size() != 2)
    {
        return std::nullopt;
    }
    auto nibble = [](char ch) -> int {
        if (ch >= '0' && ch <= '9')
        {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f')
        {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F')
        {
            return ch - 'A' + 10;
        }
        return -1;
    };
    const int high = nibble(text[0]);
    const int low = nibble(text[1]);
    if (high < 0 || low < 0)
    {
        return std::nullopt;
    }
    return static_cast<std::byte>((high << 4) | low);
}

[[nodiscard]] Core::Result<CatalogCookAssetSpec> parseTexture2dInline(const std::vector<std::string>& tokens)
{
    // texture2d <id> <w> <h> <hexRRGGBBAA>...
    if (tokens.size() < 5)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "texture2d needs id width height pixels...");
    }
    auto assetId = Core::AssetId::parseCanonical(tokens[1]);
    if (!assetId)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid texture2d asset id");
    }
    Core::u32 width = 0;
    Core::u32 height = 0;
    if (!parseU32Token(tokens[2], width) || !parseU32Token(tokens[3], height) || width == 0 || height == 0 ||
        width > AssetFormat::Texture2DWire::MaxDimension || height > AssetFormat::Texture2DWire::MaxDimension)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid texture2d dimensions");
    }
    const auto expectedPixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (tokens.size() != 4U + expectedPixels)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "texture2d pixel count mismatch");
    }
    std::vector<std::byte> pixels;
    pixels.reserve(expectedPixels * 4U);
    for (std::size_t index = 0; index < expectedPixels; ++index)
    {
        const auto& token = tokens[4U + index];
        if (token.size() != 8)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "pixel must be 8 hex chars RRGGBBAA");
        }
        for (std::size_t byteIndex = 0; byteIndex < 4U; ++byteIndex)
        {
            auto byte = parseHexByte(std::string_view(token).substr(byteIndex * 2U, 2U));
            if (!byte)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid pixel hex");
            }
            pixels.push_back(*byte);
        }
    }
    auto payload = AssetFormat::writeTexture2DPayloadBytes(AssetFormat::Texture2DPayloadDesc{
        .width = static_cast<Core::u16>(width),
        .height = static_cast<Core::u16>(height),
        .pixelFormat = AssetFormat::Texture2DPixelFormat::Rgba8Unorm,
        .pixels = pixels,
    });
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    return CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Texture2D,
        .assetId = *assetId,
        .assetTypeVersion = AssetFormat::Texture2DWire::SchemaVersion,
        .payload = std::move(*payload),
    };
}

[[nodiscard]] Core::Result<CatalogCookAssetSpec> parseSpriteInline(const std::vector<std::string>& tokens)
{
    // sprite <id> <textureId> [u0 v0 u1 v1 pivotX pivotY ppu]
    if (tokens.size() != 3 && tokens.size() != 10)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "sprite needs id textureId [u0 v0 u1 v1 pivotX pivotY ppu]");
    }
    auto spriteId = Core::AssetId::parseCanonical(tokens[1]);
    auto textureId = Core::AssetId::parseCanonical(tokens[2]);
    if (!spriteId || !textureId)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid sprite/texture asset id");
    }
    AssetFormat::SpritePayloadDesc desc{.textureId = *textureId};
    if (tokens.size() == 10)
    {
        if (!parseFloatToken(tokens[3], desc.u0) || !parseFloatToken(tokens[4], desc.v0) ||
            !parseFloatToken(tokens[5], desc.u1) || !parseFloatToken(tokens[6], desc.v1) ||
            !parseFloatToken(tokens[7], desc.pivotX) || !parseFloatToken(tokens[8], desc.pivotY) ||
            !parseFloatToken(tokens[9], desc.pixelsPerUnit))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid sprite numeric fields");
        }
    }
    auto payload = AssetFormat::writeSpritePayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    CatalogCookAssetSpec asset{
        .assetKind = AssetFormat::AssetKind::Sprite,
        .assetId = *spriteId,
        .assetTypeVersion = AssetFormat::SpriteWire::SchemaVersion,
        .payload = std::move(*payload),
    };
    asset.dependencies.push_back(AssetFormat::CookedAssetWriteDependency{
        .assetId = *textureId,
        .expectedKind = AssetFormat::AssetKind::Texture2D,
        .flags = AssetFormat::DependencyFlags::Required,
    });
    return asset;
}

[[nodiscard]] Core::Result<CookedPackage> cookPackageInternal(const CatalogCookRequest& request)
{
    if (request.assets.empty())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cook request requires at least one asset");
    }
    if (request.targetPlatform == AssetFormat::TargetPlatform::Invalid)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cook request requires target platform");
    }

    std::vector<CatalogCookAssetSpec> sorted = request.assets;
    std::sort(sorted.begin(), sorted.end(), [](const CatalogCookAssetSpec& left, const CatalogCookAssetSpec& right) {
        return left.assetId < right.assetId;
    });
    for (std::size_t index = 1; index < sorted.size(); ++index)
    {
        if (!(sorted[index - 1U].assetId < sorted[index].assetId))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "duplicate AssetId in cook request");
        }
    }

    CookedPackage package{};
    package.objectStorage.reserve(sorted.size());
    package.objectViews.reserve(sorted.size());
    std::vector<AssetFormat::CookedManifestWriteEntry> entries;
    entries.reserve(sorted.size());
    Core::u32 dependencyCount = 0;

    for (const auto& asset : sorted)
    {
        if (!asset.assetId || asset.assetKind == AssetFormat::AssetKind::Invalid)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cook asset requires id and kind");
        }
        auto cooked = AssetFormat::writeCookedAssetBytes(AssetFormat::CookedAssetWriteDesc{
            .assetKind = asset.assetKind,
            .assetTypeVersion = asset.assetTypeVersion,
            .targetPlatform = request.targetPlatform,
            .assetId = asset.assetId,
            .dependencies = asset.dependencies,
            .payload = asset.payload,
            .computeContentHash = true,
        });
        if (!cooked)
        {
            return Core::failure(std::move(cooked.error()).withContext("cookCatalogPackage", "writeAsset"));
        }
        auto hash = Core::digestContentHashV1(asset.payload);
        if (!hash)
        {
            return Core::failure(std::move(hash.error()));
        }
        dependencyCount += static_cast<Core::u32>(asset.dependencies.size());
        entries.push_back(AssetFormat::CookedManifestWriteEntry{
            .assetId = asset.assetId,
            .contentHash = *hash,
            .assetKind = asset.assetKind,
            .assetTypeVersion = asset.assetTypeVersion,
            .cookedFileBytes = cooked->size(),
            .dependencies = asset.dependencies,
        });
        package.objectStorage.push_back(std::move(*cooked));
    }

    auto manifest = AssetFormat::writeCookedManifestBytes(AssetFormat::CookedManifestWriteDesc{
        .targetPlatform = request.targetPlatform,
        .entries = entries,
    });
    if (!manifest)
    {
        return Core::failure(std::move(manifest.error()).withContext("cookCatalogPackage", "writeManifest"));
    }

    for (std::size_t index = 0; index < sorted.size(); ++index)
    {
        package.objectViews.push_back(CatalogPackageObjectBlob{
            .assetKind = sorted[index].assetKind,
            .assetId = sorted[index].assetId,
            .bytes = package.objectStorage[index],
        });
    }
    package.summary = CatalogCookResult{
        .entryCount = static_cast<Core::u32>(sorted.size()),
        .dependencyCount = dependencyCount,
        .manifestBytes = std::move(*manifest),
    };
    return package;
}

} // namespace

Core::Result<CatalogCookResult> cookCatalogPackage(const CatalogCookRequest& request)
{
    auto package = cookPackageInternal(request);
    if (!package)
    {
        return Core::failure(std::move(package.error()));
    }
    return std::move(package->summary);
}

Core::Status cookAndPublishCatalogPackage(std::string_view catalogRootUtf8, const CatalogCookRequest& request)
{
    auto package = cookPackageInternal(request);
    if (!package)
    {
        return Core::failure(std::move(package.error()));
    }
    return publishCatalogPackage(catalogRootUtf8, DefaultCatalogManifestRelativePath, package->summary.manifestBytes,
                                 package->objectViews);
}

Core::Result<CatalogCookRequest> parseCatalogCookRecipe(std::string_view recipeText, std::string_view baseDirectoryUtf8)
{
    CatalogCookRequest request{};
    std::pmr::unsynchronized_pool_resource memory;
    std::size_t cursor = 0;
    while (cursor <= recipeText.size())
    {
        const auto end = recipeText.find('\n', cursor);
        auto lineView = recipeText.substr(cursor, end == std::string_view::npos ? std::string_view::npos : end - cursor);
        if (!lineView.empty() && lineView.back() == '\r')
        {
            lineView.remove_suffix(1);
        }
        cursor = end == std::string_view::npos ? recipeText.size() + 1 : end + 1;

        const auto line = trim(lineView);
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        const auto tokens = splitWs(line);
        if (tokens.empty())
        {
            continue;
        }
        if (tokens[0] == "platform")
        {
            if (tokens.size() != 2 || !isKnownPlatformName(tokens[1], request.targetPlatform))
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid platform line in recipe");
            }
            continue;
        }
        if (tokens[0] == "texture2d")
        {
            auto asset = parseTexture2dInline(tokens);
            if (!asset)
            {
                return Core::failure(std::move(asset.error()));
            }
            request.assets.push_back(std::move(*asset));
            continue;
        }
        if (tokens[0] == "sprite")
        {
            auto asset = parseSpriteInline(tokens);
            if (!asset)
            {
                return Core::failure(std::move(asset.error()));
            }
            request.assets.push_back(std::move(*asset));
            continue;
        }
        if (tokens[0] != "asset" || tokens.size() < 4)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid asset line in recipe");
        }
        AssetFormat::AssetKind kind = AssetFormat::AssetKind::Invalid;
        if (!isKnownKindName(tokens[1], kind))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "unknown asset kind in recipe");
        }
        auto assetId = Core::AssetId::parseCanonical(tokens[2]);
        if (!assetId)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid asset id in recipe (expect 32 hex)");
        }
        auto payloadPath = joinPath(baseDirectoryUtf8, tokens[3]);
        if (!payloadPath)
        {
            return Core::failure(std::move(payloadPath.error()));
        }
        auto payload = Core::readFile(*payloadPath, Core::ReadFileConfig{.memoryResource = &memory});
        if (!payload)
        {
            return Core::failure(std::move(payload.error()).withContext("parseCatalogCookRecipe", "readPayload"));
        }

        CatalogCookAssetSpec asset{
            .assetKind = kind,
            .assetId = *assetId,
            .payload = std::vector<std::byte>(payload->begin(), payload->end()),
        };
        for (std::size_t index = 4; index < tokens.size(); ++index)
        {
            const auto& depToken = tokens[index];
            const auto colon = depToken.find(':');
            if (colon == std::string::npos)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "dependency must be id:Kind");
            }
            auto depId = Core::AssetId::parseCanonical(std::string_view(depToken).substr(0, colon));
            AssetFormat::AssetKind depKind = AssetFormat::AssetKind::Invalid;
            if (!depId || !isKnownKindName(std::string_view(depToken).substr(colon + 1), depKind))
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid dependency token in recipe");
            }
            asset.dependencies.push_back(AssetFormat::CookedAssetWriteDependency{
                .assetId = *depId,
                .expectedKind = depKind,
                .flags = AssetFormat::DependencyFlags::Required,
            });
        }
        request.assets.push_back(std::move(asset));
    }
    if (request.assets.empty())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "recipe contains no assets");
    }
    return request;
}

Core::Result<CatalogCookRequest> loadCatalogCookRecipeFile(std::string_view recipeUtf8Path)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto bytes = Core::readFile(recipeUtf8Path, Core::ReadFileConfig{.maxBytes = 16ULL * 1024ULL * 1024ULL,
                                                                     .memoryResource = &memory});
    if (!bytes)
    {
        return Core::failure(std::move(bytes.error()).withContext("loadCatalogCookRecipeFile", "read"));
    }
    std::string text;
    text.resize(bytes->size());
    for (std::size_t index = 0; index < bytes->size(); ++index)
    {
        text[index] = static_cast<char>(std::to_integer<unsigned char>((*bytes)[index]));
    }
    const auto path = std::filesystem::u8path(recipeUtf8Path);
    const auto base = path.parent_path();
    std::string baseUtf8 = ".";
    if (!base.empty())
    {
        const auto generic = base.generic_u8string();
        baseUtf8.assign(generic.begin(), generic.end());
    }
    return parseCatalogCookRecipe(text, baseUtf8);
}

} // namespace Tina::Asset
