#include <tina/asset/CatalogCook.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackagePublish.hpp>
#include <tina/asset_format/AudioClipPayload.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/StaticMeshPayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/io/ReadFile.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory_resource>
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

[[nodiscard]] Core::u16 readLeU16(std::span<const std::byte> bytes, std::size_t offset) noexcept
{
    return static_cast<Core::u16>(std::to_integer<Core::u8>(bytes[offset])) |
           static_cast<Core::u16>(static_cast<Core::u16>(std::to_integer<Core::u8>(bytes[offset + 1U])) << 8U);
}

[[nodiscard]] Core::u32 readLeU32(std::span<const std::byte> bytes, std::size_t offset) noexcept
{
    Core::u32 value = 0;
    for (std::size_t index = 0; index < 4U; ++index)
    {
        value |= static_cast<Core::u32>(std::to_integer<Core::u8>(bytes[offset + index])) << (index * 8U);
    }
    return value;
}

// Cook-time PCM WAV decode only (RIFF/WAVE, PCM 16-bit). Keeps Asset free of miniaudio.
// MP3/Ogg still go through Audio adapter decode + offline cook paths.
[[nodiscard]] Core::Result<AssetFormat::AudioClipPayloadDesc>
decodePcm16WavToClipDesc(std::span<const std::byte> bytes, std::vector<float>& pcmOut)
{
    if (bytes.size() < 44U)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "wav file too short");
    }
    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "wav missing RIFF/WAVE header");
    }

    Core::u16 audioFormat = 0;
    Core::u16 channels = 0;
    Core::u32 sampleRate = 0;
    Core::u16 bitsPerSample = 0;
    std::span<const std::byte> dataChunk{};
    std::size_t offset = 12;
    while (offset + 8U <= bytes.size())
    {
        const char* tag = reinterpret_cast<const char*>(bytes.data() + offset);
        const Core::u32 chunkSize = readLeU32(bytes, offset + 4U);
        const std::size_t dataOffset = offset + 8U;
        if (dataOffset + chunkSize > bytes.size())
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "wav chunk overruns file");
        }
        if (std::memcmp(tag, "fmt ", 4) == 0)
        {
            if (chunkSize < 16U)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "wav fmt chunk too small");
            }
            audioFormat = readLeU16(bytes, dataOffset);
            channels = readLeU16(bytes, dataOffset + 2U);
            sampleRate = readLeU32(bytes, dataOffset + 4U);
            bitsPerSample = readLeU16(bytes, dataOffset + 14U);
        }
        else if (std::memcmp(tag, "data", 4) == 0)
        {
            dataChunk = bytes.subspan(dataOffset, chunkSize);
        }
        offset = dataOffset + chunkSize + (chunkSize & 1U); // word-align
    }

    if (audioFormat != 1U)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "wav cook supports only PCM format=1 (use sine/samples or offline decode for compressed)");
    }
    if (bitsPerSample != 16U)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "wav cook supports only 16-bit PCM");
    }
    if (channels == 0 || channels > AssetFormat::AudioClipWire::MaxChannels ||
        sampleRate < AssetFormat::AudioClipWire::MinSampleRate ||
        sampleRate > AssetFormat::AudioClipWire::MaxSampleRate)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "wav geometry out of AudioClip range");
    }
    if (dataChunk.empty() || (dataChunk.size() % (channels * 2U)) != 0U)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "wav data chunk size invalid");
    }

    const Core::u32 frameCount =
        static_cast<Core::u32>(dataChunk.size() / (static_cast<std::size_t>(channels) * 2U));
    if (frameCount == 0 || frameCount > AssetFormat::AudioClipWire::MaxFrameCount)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "wav frameCount out of range");
    }

    pcmOut.resize(static_cast<std::size_t>(frameCount) * channels);
    for (Core::u32 frame = 0; frame < frameCount; ++frame)
    {
        for (Core::u16 channel = 0; channel < channels; ++channel)
        {
            const std::size_t sampleIndex =
                static_cast<std::size_t>(frame) * channels + channel;
            const std::size_t byteIndex = sampleIndex * 2U;
            const auto lo = std::to_integer<Core::u8>(dataChunk[byteIndex]);
            const auto hi = std::to_integer<Core::u8>(dataChunk[byteIndex + 1U]);
            const auto sample = static_cast<std::int16_t>(static_cast<Core::u16>(lo) |
                                                          static_cast<Core::u16>(static_cast<Core::u16>(hi) << 8U));
            pcmOut[sampleIndex] = static_cast<float>(sample) / 32768.0F;
        }
    }

    return AssetFormat::AudioClipPayloadDesc{
        .channels = channels,
        .sampleRate = sampleRate,
        .frameCount = frameCount,
        .interleavedPcm = pcmOut,
    };
}

[[nodiscard]] Core::Result<CatalogCookAssetSpec>
parseAudioClipInline(const std::vector<std::string>& tokens, std::string_view baseDirectoryUtf8)
{
    // audioclip <id> <sampleRate> <channels> <frameCount> <f0 f1 ...>
    // audioclip <id> <sampleRate> <channels> <frameCount> sine <freqHz>
    // audioclip <id> file <relativeOrAbsolutePath.wav>   // PCM16 WAV only (M11-A20)
    if (tokens.size() < 3)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "audioclip needs id + (geometry samples|sine) or file path");
    }
    auto assetId = Core::AssetId::parseCanonical(tokens[1]);
    if (!assetId)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid audioclip asset id");
    }

    std::vector<float> pcm;
    AssetFormat::AudioClipPayloadDesc desc{};

    if (tokens[2] == "file")
    {
        if (tokens.size() != 4)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "audioclip file needs exactly one path");
        }
        auto path = joinPath(baseDirectoryUtf8, tokens[3]);
        if (!path)
        {
            return Core::failure(std::move(path.error()));
        }
        std::pmr::unsynchronized_pool_resource wavMemory;
        auto bytes = Core::readFile(
            *path, Core::ReadFileConfig{.maxBytes = 32ULL * 1024ULL * 1024ULL, .memoryResource = &wavMemory});
        if (!bytes)
        {
            return Core::failure(std::move(bytes.error()).withContext("parseAudioClipInline", "readWav"));
        }
        auto decoded = decodePcm16WavToClipDesc(*bytes, pcm);
        if (!decoded)
        {
            return Core::failure(std::move(decoded.error()));
        }
        desc = *decoded;
        desc.interleavedPcm = pcm;
    }
    else
    {
        if (tokens.size() < 5)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "audioclip needs id sampleRate channels frameCount samples|sine freq");
        }
        Core::u32 sampleRate = 0;
        Core::u32 channels = 0;
        Core::u32 frameCount = 0;
        if (!parseU32Token(tokens[2], sampleRate) || !parseU32Token(tokens[3], channels) ||
            !parseU32Token(tokens[4], frameCount))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid audioclip geometry fields");
        }
        if (channels == 0 || channels > AssetFormat::AudioClipWire::MaxChannels || frameCount == 0 ||
            frameCount > AssetFormat::AudioClipWire::MaxFrameCount ||
            sampleRate < AssetFormat::AudioClipWire::MinSampleRate ||
            sampleRate > AssetFormat::AudioClipWire::MaxSampleRate)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "audioclip geometry out of range");
        }

        const std::size_t sampleCount =
            static_cast<std::size_t>(frameCount) * static_cast<std::size_t>(channels);
        pcm.resize(sampleCount, 0.0F);

        if (tokens.size() >= 6 && tokens[5] == "sine")
        {
            if (tokens.size() != 7)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "audioclip sine needs freqHz");
            }
            float frequency = 0.0F;
            if (!parseFloatToken(tokens[6], frequency) || !(frequency > 0.0F) || !std::isfinite(frequency))
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid audioclip sine frequency");
            }
            constexpr float kPi = 3.14159265358979323846F;
            for (Core::u32 frame = 0; frame < frameCount; ++frame)
            {
                const float t = static_cast<float>(frame) / static_cast<float>(sampleRate);
                const float sample = 0.25F * std::sin(2.0F * kPi * frequency * t);
                for (Core::u32 channel = 0; channel < channels; ++channel)
                {
                    pcm[static_cast<std::size_t>(frame) * channels + channel] = sample;
                }
            }
        }
        else
        {
            if (tokens.size() != 5U + sampleCount)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "audioclip sample count mismatch");
            }
            for (std::size_t index = 0; index < sampleCount; ++index)
            {
                float value = 0.0F;
                if (!parseFloatToken(tokens[5U + index], value) || !std::isfinite(value))
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid audioclip sample value");
                }
                pcm[index] = value;
            }
        }

        desc = AssetFormat::AudioClipPayloadDesc{
            .channels = static_cast<Core::u16>(channels),
            .sampleRate = sampleRate,
            .frameCount = frameCount,
            .interleavedPcm = pcm,
        };
    }

    auto payload = AssetFormat::writeAudioClipPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    return CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::AudioClip,
        .assetId = *assetId,
        .assetTypeVersion = AssetFormat::AudioClipWire::SchemaVersion,
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

    // Multi-line builders for tileset/tilemap.
    enum class MultiState : Core::u8 { None, Tileset, TileMap };
    MultiState multi = MultiState::None;
    Core::AssetId pendingTilesetId{};
    Core::AssetId pendingTilesetTextureId{};
    Core::u16 pendingTilePxW = 0;
    Core::u16 pendingTilePxH = 0;
    std::vector<AssetFormat::TilesetTileDesc> pendingTiles{};
    Core::AssetId pendingMapId{};
    Core::AssetId pendingMapTilesetId{};
    Core::u32 pendingMapW = 0;
    Core::u32 pendingMapH = 0;
    float pendingCellSize = 1.0f;
    std::vector<Core::u16> pendingMapTiles{};
    Core::u32 pendingMapRows = 0;

    auto flushTileset = [&]() -> Core::Status {
        if (multi != MultiState::Tileset)
        {
            return Core::success();
        }
        if (pendingTiles.empty())
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tileset requires at least one tile line");
        }
        auto payload = AssetFormat::writeTilesetPayloadBytes(AssetFormat::TilesetPayloadDesc{
            .tilePixelWidth = pendingTilePxW,
            .tilePixelHeight = pendingTilePxH,
            .tiles = pendingTiles,
            .textureId = pendingTilesetTextureId,
        });
        if (!payload)
        {
            return Core::failure(std::move(payload.error()));
        }
        CatalogCookAssetSpec asset{
            .assetKind = AssetFormat::AssetKind::Tileset,
            .assetId = pendingTilesetId,
            .assetTypeVersion = AssetFormat::TilesetWire::SchemaVersion,
            .payload = std::move(*payload),
        };
        asset.dependencies.push_back(AssetFormat::CookedAssetWriteDependency{
            .assetId = pendingTilesetTextureId,
            .expectedKind = AssetFormat::AssetKind::Texture2D,
            .flags = AssetFormat::DependencyFlags::Required,
        });
        request.assets.push_back(std::move(asset));
        multi = MultiState::None;
        pendingTiles.clear();
        return Core::success();
    };

    auto flushTileMap = [&]() -> Core::Status {
        if (multi != MultiState::TileMap)
        {
            return Core::success();
        }
        if (pendingMapRows != pendingMapH)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tilemap row count does not match height");
        }
        auto payload = AssetFormat::writeTileMapPayloadBytes(AssetFormat::TileMapPayloadDesc{
            .widthCells = pendingMapW,
            .heightCells = pendingMapH,
            .cellSizeMeters = pendingCellSize,
            .tiles = pendingMapTiles,
            .tilesetId = pendingMapTilesetId,
        });
        if (!payload)
        {
            return Core::failure(std::move(payload.error()));
        }
        CatalogCookAssetSpec asset{
            .assetKind = AssetFormat::AssetKind::TileMap,
            .assetId = pendingMapId,
            .assetTypeVersion = AssetFormat::TileMapWire::SchemaVersion,
            .payload = std::move(*payload),
        };
        asset.dependencies.push_back(AssetFormat::CookedAssetWriteDependency{
            .assetId = pendingMapTilesetId,
            .expectedKind = AssetFormat::AssetKind::Tileset,
            .flags = AssetFormat::DependencyFlags::Required,
        });
        request.assets.push_back(std::move(asset));
        multi = MultiState::None;
        pendingMapTiles.clear();
        pendingMapRows = 0;
        return Core::success();
    };

    auto flushMulti = [&]() -> Core::Status {
        if (multi == MultiState::Tileset)
        {
            return flushTileset();
        }
        if (multi == MultiState::TileMap)
        {
            return flushTileMap();
        }
        return Core::success();
    };

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

        // Continue multi-line blocks first.
        if (multi == MultiState::Tileset && tokens[0] == "tile")
        {
            if (tokens.size() != 7)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "tile needs localId flags u0 v0 u1 v1");
            }
            Core::u32 localId = 0;
            Core::u32 flags = 0;
            AssetFormat::TilesetTileDesc tile{};
            if (!parseU32Token(tokens[1], localId) || !parseU32Token(tokens[2], flags) ||
                !parseFloatToken(tokens[3], tile.u0) || !parseFloatToken(tokens[4], tile.v0) ||
                !parseFloatToken(tokens[5], tile.u1) || !parseFloatToken(tokens[6], tile.v1) || localId > 0xFFFFU ||
                flags > 0xFFFFU)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid tile fields");
            }
            tile.localId = static_cast<Core::u16>(localId);
            tile.materialFlags = static_cast<Core::u16>(flags);
            pendingTiles.push_back(tile);
            continue;
        }
        if (multi == MultiState::TileMap && tokens[0] == "row")
        {
            if (tokens.size() != 1U + pendingMapW)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "row width does not match tilemap width");
            }
            if (pendingMapRows >= pendingMapH)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "too many tilemap rows");
            }
            for (Core::u32 index = 0; index < pendingMapW; ++index)
            {
                Core::u32 cell = 0;
                if (!parseU32Token(tokens[1U + index], cell) || cell > 0xFFFFU)
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid tilemap cell id");
                }
                pendingMapTiles.push_back(static_cast<Core::u16>(cell));
            }
            ++pendingMapRows;
            continue;
        }

        // Starting a new top-level directive flushes any open multi-line block.
        if (const auto flushStatus = flushMulti(); !flushStatus)
        {
            return Core::failure(std::move(flushStatus.error()));
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
        if (tokens[0] == "audioclip")
        {
            auto asset = parseAudioClipInline(tokens, baseDirectoryUtf8);
            if (!asset)
            {
                return Core::failure(std::move(asset.error()));
            }
            request.assets.push_back(std::move(*asset));
            continue;
        }
        if (tokens[0] == "staticmesh")
        {
            // staticmesh <id> cube  — canonical unit cube (product-3D before glTF).
            if (tokens.size() != 3 || tokens[2] != "cube")
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "staticmesh currently supports: staticmesh <id> cube");
            }
            auto meshId = Core::AssetId::parseCanonical(tokens[1]);
            if (!meshId)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid staticmesh asset id");
            }
            std::array<AssetFormat::StaticMeshSubmeshDesc, 1> submeshes{};
            std::array<float, 24 * 8> vertices{};
            std::array<Core::u16, 36> indices{};
            const AssetFormat::StaticMeshPayloadDesc desc =
                AssetFormat::makeCanonicalUnitCubeMeshDesc(submeshes, vertices, indices);
            if (desc.vertices.empty() || desc.indices.empty())
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "failed to build canonical cube mesh");
            }
            auto payload = AssetFormat::writeStaticMeshPayloadBytes(desc);
            if (!payload)
            {
                return Core::failure(std::move(payload.error()));
            }
            request.assets.push_back(CatalogCookAssetSpec{
                .assetKind = AssetFormat::AssetKind::StaticMesh,
                .assetId = *meshId,
                .assetTypeVersion = AssetFormat::StaticMeshWire::SchemaVersion,
                .payload = std::move(*payload),
            });
            continue;
        }
        if (tokens[0] == "material")
        {
            // material <id> unlit <r> <g> <b> [a] [tex32hex]
            // Optional trailing Texture2D id (M11-E5); solid factor always required.
            if (tokens.size() < 6 || tokens.size() > 8)
            {
                return Core::failure(
                    AssetErrorCode::InvalidCatalogConfig,
                    "material currently supports: material <id> unlit <r> <g> <b> [a] [textureId]");
            }
            if (tokens[2] != "unlit")
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "material model must be unlit");
            }
            auto materialId = Core::AssetId::parseCanonical(tokens[1]);
            if (!materialId)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid material asset id");
            }
            float r = 0.0F;
            float g = 0.0F;
            float b = 0.0F;
            float a = 1.0F;
            if (!parseFloatToken(tokens[3], r) || !parseFloatToken(tokens[4], g) || !parseFloatToken(tokens[5], b))
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid material baseColor RGB");
            }
            Core::AssetId textureId{};
            if (tokens.size() >= 7)
            {
                // tokens[6] may be alpha float OR texture id (32 hex). Prefer float parse first.
                float alphaCandidate = 0.0F;
                if (parseFloatToken(tokens[6], alphaCandidate))
                {
                    a = alphaCandidate;
                    if (tokens.size() == 8)
                    {
                        auto tex = Core::AssetId::parseCanonical(tokens[7]);
                        if (!tex)
                        {
                            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                                 "invalid material baseColor texture id");
                        }
                        textureId = *tex;
                    }
                }
                else
                {
                    auto tex = Core::AssetId::parseCanonical(tokens[6]);
                    if (!tex)
                    {
                        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                             "invalid material alpha or texture id");
                    }
                    textureId = *tex;
                    if (tokens.size() == 8)
                    {
                        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                             "material: when token6 is texture id, do not pass 8 tokens");
                    }
                }
            }
            AssetFormat::MaterialPayloadDesc desc{
                .model = AssetFormat::MaterialModel::UnlitBaseColor,
                .baseColorR = r,
                .baseColorG = g,
                .baseColorB = b,
                .baseColorA = a,
                .doubleSided = false,
                .alphaMode = AssetFormat::MaterialAlphaMode::Opaque,
                .baseColorTextureId = textureId,
            };
            auto payload = AssetFormat::writeMaterialPayloadBytes(desc);
            if (!payload)
            {
                return Core::failure(std::move(payload.error()));
            }
            CatalogCookAssetSpec asset{
                .assetKind = AssetFormat::AssetKind::Material,
                .assetId = *materialId,
                .assetTypeVersion = AssetFormat::MaterialWire::SchemaVersion,
                .payload = std::move(*payload),
            };
            if (textureId)
            {
                asset.dependencies.push_back(AssetFormat::CookedAssetWriteDependency{
                    .assetId = textureId,
                    .expectedKind = AssetFormat::AssetKind::Texture2D,
                    .flags = AssetFormat::DependencyFlags::Required,
                });
            }
            request.assets.push_back(std::move(asset));
            continue;
        }
        if (tokens[0] == "tileset")
        {
            // tileset <id> <textureId> <tilePxW> <tilePxH>
            if (tokens.size() != 5)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "tileset needs id textureId tilePxW tilePxH");
            }
            auto tilesetId = Core::AssetId::parseCanonical(tokens[1]);
            auto textureId = Core::AssetId::parseCanonical(tokens[2]);
            Core::u32 tw = 0;
            Core::u32 th = 0;
            if (!tilesetId || !textureId || !parseU32Token(tokens[3], tw) || !parseU32Token(tokens[4], th) || tw == 0 ||
                th == 0 || tw > 0xFFFFU || th > 0xFFFFU)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid tileset header fields");
            }
            multi = MultiState::Tileset;
            pendingTilesetId = *tilesetId;
            pendingTilesetTextureId = *textureId;
            pendingTilePxW = static_cast<Core::u16>(tw);
            pendingTilePxH = static_cast<Core::u16>(th);
            pendingTiles.clear();
            continue;
        }
        if (tokens[0] == "tilemap")
        {
            // tilemap <id> <tilesetId> <w> <h> <cellSize>
            if (tokens.size() != 6)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "tilemap needs id tilesetId width height cellSize");
            }
            auto mapId = Core::AssetId::parseCanonical(tokens[1]);
            auto tilesetId = Core::AssetId::parseCanonical(tokens[2]);
            Core::u32 width = 0;
            Core::u32 height = 0;
            float cellSize = 0.0f;
            if (!mapId || !tilesetId || !parseU32Token(tokens[3], width) || !parseU32Token(tokens[4], height) ||
                !parseFloatToken(tokens[5], cellSize) || width == 0 || height == 0)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid tilemap header fields");
            }
            multi = MultiState::TileMap;
            pendingMapId = *mapId;
            pendingMapTilesetId = *tilesetId;
            pendingMapW = width;
            pendingMapH = height;
            pendingCellSize = cellSize;
            pendingMapTiles.clear();
            pendingMapTiles.reserve(static_cast<std::size_t>(width) * height);
            pendingMapRows = 0;
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
    if (const auto flushStatus = flushMulti(); !flushStatus)
    {
        return Core::failure(std::move(flushStatus.error()));
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
