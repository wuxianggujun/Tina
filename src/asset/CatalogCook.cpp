#include <tina/asset/CatalogCook.hpp>

#include "Utf8Path.hpp"

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackagePublish.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset/SourceImportProbe.hpp>
#include <tina/asset_format/AudioClipPayload.hpp>
#include <tina/asset_format/EnvironmentMapPayload.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/asset_format/SpriteAnimationClipPayload.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/StaticMeshPayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/asset_format/TileMapChunkPayload.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <memory_resource>
#include <new>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace Tina::Asset {
namespace {

struct CookedPackage final {
    CatalogCookResult summary{};
    std::vector<CatalogPackageObjectBlob> objectViews{};
    std::vector<std::vector<std::byte>> objectStorage{};
};

struct CookAssetValidationView final {
    AssetFormat::AssetKind assetKind = AssetFormat::AssetKind::Invalid;
    Core::AssetId assetId{};
    Core::u16 assetTypeVersion = 0;
    std::span<const std::byte> payload{};
    std::span<const AssetFormat::CookedAssetWriteDependency> dependencies{};
};

struct IncrementalCookEntry final {
    AssetFormat::AssetKind assetKind = AssetFormat::AssetKind::Invalid;
    Core::AssetId assetId{};
    Core::u16 assetTypeVersion = 0;
    Core::ContentHash contentHash{};
    std::span<const std::byte> payload{};
    std::span<const std::byte> objectBytes{};
    std::vector<AssetFormat::CookedAssetWriteDependency> dependencies{};
};

struct RecipeSourceCaptureContext final {
    SourceImportCandidate& candidate;
    const SourceImportCaptureConfig& config;
};

[[nodiscard]] Core::Status captureRecipeDependencyBytes(const RecipeSourceCaptureContext* capture,
                                                        std::string_view sourceUtf8Path,
                                                        std::span<const std::byte> consumedBytes)
{
    if (capture == nullptr)
    {
        return Core::success();
    }
    auto sourceIndex = captureSourceImportBytes(capture->candidate, capture->config,
                                                sourceUtf8Path,
                                                AssetFormat::SourceImportReadExtent::WholeFile,
                                                consumedBytes);
    if (!sourceIndex)
    {
        return Core::failure(std::move(sourceIndex.error()).withContext(
            "captureRecipeDependencyBytes", "capture"));
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateRecipeSourcePath(const SourceImportCaptureConfig* config,
                                                    std::string_view sourceUtf8Path)
{
    if (config == nullptr)
    {
        return Core::success();
    }
    auto normalized = normalizeSourceImportPath(*config, sourceUtf8Path);
    if (!normalized)
    {
        return Core::failure(std::move(normalized.error()).withContext(
            "validateRecipeSourcePath", "normalize"));
    }
    return Core::success();
}

[[nodiscard]] Core::Error makeStageFilesystemError(std::string_view message,
                                                    const std::error_code& errorCode)
{
    Core::Error error{Core::CoreErrorCode::Io, message};
    if (errorCode == std::errc::no_such_file_or_directory)
    {
        error.code = Core::CoreErrorCode::NotFound;
    } else if (errorCode == std::errc::permission_denied)
    {
        error.code = Core::CoreErrorCode::PermissionDenied;
    } else if (errorCode == std::errc::file_exists)
    {
        error.code = Core::CoreErrorCode::AlreadyExists;
    }
    if (errorCode)
    {
        error.setNativeCode(static_cast<Core::i64>(errorCode.value()));
        error.addContext("native", errorCode.message());
    }
    return error;
}

[[nodiscard]] Core::Status createFreshStageRoot(std::string_view stagingRootUtf8)
{
    if (stagingRootUtf8.empty() || !Core::countStrictUtf8CodepointsWithoutNul(stagingRootUtf8))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "staging root path is invalid");
    }

    try
    {
        const auto stagingRoot = Detail::pathFromUtf8Bytes(stagingRootUtf8);
        const auto parent = stagingRoot.parent_path();
        std::error_code errorCode;
        if (!std::filesystem::exists(parent.empty() ? std::filesystem::path{"."} : parent, errorCode))
        {
            if (errorCode)
            {
                return Core::failure(makeStageFilesystemError("failed to query staging parent", errorCode));
            }
            return Core::failure(Core::CoreErrorCode::NotFound, "staging parent directory does not exist");
        }
        if (!std::filesystem::is_directory(parent.empty() ? std::filesystem::path{"."} : parent, errorCode))
        {
            if (errorCode)
            {
                return Core::failure(makeStageFilesystemError("failed to query staging parent", errorCode));
            }
            return Core::failure(Core::CoreErrorCode::InvalidArgument, "staging parent is not a directory");
        }
        if (std::filesystem::create_directory(stagingRoot, errorCode))
        {
            return Core::success();
        }
        if (errorCode)
        {
            return Core::failure(makeStageFilesystemError("failed to create staging root", errorCode));
        }
        return Core::failure(Core::CoreErrorCode::AlreadyExists, "staging root already exists");
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "staging root path allocation failed");
    } catch (const std::filesystem::filesystem_error& exception)
    {
        return Core::failure(makeStageFilesystemError("staging root filesystem operation failed", exception.code()));
    }
}

[[nodiscard]] Core::Result<Core::AssetId> deriveTileMapChunkAssetId(Core::AssetId parentTileMapId,
                                                                   Core::u32 stableLayerId,
                                                                   Core::u32 chunkX,
                                                                   Core::u32 chunkY)
{
    // Chunk IDs are persistent recipe output. Hash a fixed, domain-separated little-endian preimage and
    // key it by the stable layer ID so layer reordering or unrelated chunk occupancy does not churn IDs.
    constexpr std::string_view Domain = "tina.asset.tilemap-chunk-id";
    constexpr Core::u8 DerivationVersion = 1U;
    constexpr std::size_t ScalarBytes = sizeof(Core::u32);
    std::array<std::byte, Domain.size() + 1U + Core::AssetId::Bytes{}.size() + ScalarBytes * 3U> input{};

    std::size_t offset = 0;
    for (const char value : Domain)
    {
        input[offset++] = static_cast<std::byte>(static_cast<unsigned char>(value));
    }
    input[offset++] = static_cast<std::byte>(DerivationVersion);
    for (const std::byte value : parentTileMapId.bytes())
    {
        input[offset++] = value;
    }
    const auto appendU32LittleEndian = [&input, &offset](Core::u32 value) {
        for (std::size_t byteIndex = 0; byteIndex < sizeof(value); ++byteIndex)
        {
            input[offset++] = static_cast<std::byte>((value >> (byteIndex * 8U)) & 0xFFU);
        }
    };
    appendU32LittleEndian(stableLayerId);
    appendU32LittleEndian(chunkX);
    appendU32LittleEndian(chunkY);

    auto digest = Core::digestContentHashV1(input);
    if (!digest)
    {
        return Core::failure(std::move(digest.error()).withContext("deriveTileMapChunkAssetId", "digest"));
    }
    auto chunkId = Core::AssetId::fromBytes(digest->bytes());
    if (!chunkId)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "tilemap chunk AssetId derivation produced an invalid zero value");
    }
    return *chunkId;
}

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
    if (name == "TileMapChunk")
    {
        out = AssetFormat::AssetKind::TileMapChunk;
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
    if (name == "SpriteAnimationClip")
    {
        out = AssetFormat::AssetKind::SpriteAnimationClip;
        return true;
    }
    if (name == "EnvironmentMap")
    {
        out = AssetFormat::AssetKind::EnvironmentMap;
        return true;
    }
    return false;
}

[[nodiscard]] Core::u16 currentAssetTypeVersion(AssetFormat::AssetKind kind) noexcept
{
    switch (kind)
    {
    case AssetFormat::AssetKind::Texture2D:
        return AssetFormat::Texture2DWire::SchemaVersion;
    case AssetFormat::AssetKind::Sprite:
        return AssetFormat::SpriteWire::SchemaVersion;
    case AssetFormat::AssetKind::SpriteAnimationClip:
        return AssetFormat::SpriteAnimationClipWire::SchemaVersion;
    case AssetFormat::AssetKind::Tileset:
        return AssetFormat::TilesetWire::SchemaVersion;
    case AssetFormat::AssetKind::TileMap:
        return AssetFormat::TileMapWire::SchemaVersion;
    case AssetFormat::AssetKind::TileMapChunk:
        return AssetFormat::TileMapChunkWire::SchemaVersion;
    case AssetFormat::AssetKind::StaticMesh:
        return AssetFormat::StaticMeshWire::SchemaVersion;
    case AssetFormat::AssetKind::Material:
        return AssetFormat::MaterialWire::SchemaVersion;
    case AssetFormat::AssetKind::Prefab:
        return AssetFormat::PrefabWire::SchemaVersion;
    case AssetFormat::AssetKind::AudioClip:
        return AssetFormat::AudioClipWire::SchemaVersion;
    case AssetFormat::AssetKind::EnvironmentMap:
        return AssetFormat::EnvironmentMapWire::SchemaVersion;
    default:
        return 1U;
    }
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
    const auto path = Detail::pathFromUtf8Bytes(relativeOrAbsolute);
    if (path.is_absolute())
    {
        const auto generic = path.generic_u8string();
        return std::string(generic.begin(), generic.end());
    }
    const auto full = Detail::pathFromUtf8Bytes(baseUtf8) / path;
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
parseAudioClipInline(const std::vector<std::string>& tokens,
                     std::string_view baseDirectoryUtf8,
                     const RecipeSourceCaptureContext* sourceCapture)
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
        if (auto validated = validateRecipeSourcePath(
                sourceCapture != nullptr ? &sourceCapture->config : nullptr, *path);
            !validated)
        {
            return Core::failure(std::move(validated.error()).withContext(
                "parseAudioClipInline", "validateWavPath"));
        }
        std::pmr::unsynchronized_pool_resource wavMemory;
        auto bytes = Core::readFile(
            *path, Core::ReadFileConfig{.maxBytes = 32ULL * 1024ULL * 1024ULL, .memoryResource = &wavMemory});
        if (!bytes)
        {
            return Core::failure(std::move(bytes.error()).withContext("parseAudioClipInline", "readWav"));
        }
        if (auto captured = captureRecipeDependencyBytes(sourceCapture, *path, *bytes); !captured)
        {
            return Core::failure(std::move(captured.error()).withContext(
                "parseAudioClipInline", "captureWav"));
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

[[nodiscard]] Core::Result<CatalogCookAssetSpec>
parseSpriteAnimationInline(const std::vector<std::string>& tokens)
{
    // spriteanim <id> <Once|Loop|PingPong> <spriteId:durationSeconds>...
    if (tokens.size() < 4U)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "spriteanim needs id mode and at least one spriteId:duration frame");
    }
    const auto animationId = Core::AssetId::parseCanonical(tokens[1]);
    if (!animationId)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "invalid sprite animation clip asset id");
    }

    AssetFormat::SpriteAnimationPlaybackMode playbackMode{};
    if (tokens[2] == "Once")
    {
        playbackMode = AssetFormat::SpriteAnimationPlaybackMode::Once;
    } else if (tokens[2] == "Loop")
    {
        playbackMode = AssetFormat::SpriteAnimationPlaybackMode::Loop;
    } else if (tokens[2] == "PingPong")
    {
        playbackMode = AssetFormat::SpriteAnimationPlaybackMode::PingPong;
    } else
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "spriteanim mode must be Once, Loop, or PingPong");
    }

    std::vector<AssetFormat::SpriteAnimationFrameDesc> frames;
    frames.reserve(tokens.size() - 3U);
    for (std::size_t tokenIndex = 3U; tokenIndex < tokens.size(); ++tokenIndex)
    {
        const std::string_view token = tokens[tokenIndex];
        const auto separator = token.rfind(':');
        if (separator == std::string_view::npos || separator == 0U || separator + 1U == token.size())
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "spriteanim frame must be spriteId:durationSeconds");
        }
        const auto spriteId = Core::AssetId::parseCanonical(token.substr(0U, separator));
        float durationSeconds = 0.0F;
        if (!spriteId || !parseFloatToken(token.substr(separator + 1U), durationSeconds) ||
            !(durationSeconds > 0.0F) || !std::isfinite(durationSeconds))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "spriteanim frame requires a valid Sprite AssetId and positive duration");
        }
        if (*spriteId == *animationId)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "sprite animation clip cannot depend on itself");
        }
        frames.push_back(AssetFormat::SpriteAnimationFrameDesc{
            .spriteId = *spriteId,
            .durationSeconds = durationSeconds,
        });
    }

    const AssetFormat::SpriteAnimationClipPayloadDesc desc{
        .playbackMode = playbackMode,
        .frames = frames,
    };
    auto payload = AssetFormat::writeSpriteAnimationClipPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    auto dependencies = AssetFormat::makeSpriteAnimationClipDependencies(desc);
    if (!dependencies)
    {
        return Core::failure(std::move(dependencies.error()));
    }
    return CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::SpriteAnimationClip,
        .assetId = *animationId,
        .assetTypeVersion = AssetFormat::SpriteAnimationClipWire::SchemaVersion,
        .payload = std::move(*payload),
        .dependencies = std::move(*dependencies),
    };
}

[[nodiscard]] const CookAssetValidationView*
findCookAsset(std::span<const CookAssetValidationView> assets, Core::AssetId assetId) noexcept
{
    const auto found = std::lower_bound(
        assets.begin(), assets.end(), assetId,
        [](const CookAssetValidationView& candidate, Core::AssetId id) { return candidate.assetId < id; });
    return found != assets.end() && found->assetId == assetId ? &*found : nullptr;
}

[[nodiscard]] bool pathComponentEquals(const std::filesystem::path& left,
                                       const std::filesystem::path& right) noexcept
{
#if defined(_WIN32)
    const auto& leftText = left.native();
    const auto& rightText = right.native();
    return leftText.size() == rightText.size() &&
           std::equal(leftText.begin(), leftText.end(), rightText.begin(),
                      [](wchar_t leftCharacter, wchar_t rightCharacter) {
                          return std::towlower(leftCharacter) == std::towlower(rightCharacter);
                      });
#else
    return left == right;
#endif
}

[[nodiscard]] bool pathIsSameOrDescendant(const std::filesystem::path& candidate,
                                          const std::filesystem::path& ancestor) noexcept
{
    auto candidatePart = candidate.begin();
    for (auto ancestorPart = ancestor.begin(); ancestorPart != ancestor.end();
         ++ancestorPart, ++candidatePart)
    {
        if (candidatePart == candidate.end() || !pathComponentEquals(*candidatePart, *ancestorPart))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Core::Status validateStageOutsideBaseline(std::string_view stagingRootUtf8,
                                                        std::string_view baselineRootUtf8)
{
    try
    {
        std::error_code errorCode;
        const auto stage = std::filesystem::weakly_canonical(
            Detail::pathFromUtf8Bytes(stagingRootUtf8), errorCode);
        if (errorCode)
        {
            return Core::failure(makeStageFilesystemError("failed to resolve staging root", errorCode));
        }
        const auto baseline = std::filesystem::weakly_canonical(
            Detail::pathFromUtf8Bytes(baselineRootUtf8), errorCode);
        if (errorCode)
        {
            return Core::failure(makeStageFilesystemError("failed to resolve baseline root", errorCode));
        }
        if (pathIsSameOrDescendant(stage.lexically_normal(), baseline.lexically_normal()))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "staging root must be outside the baseline package");
        }
        return Core::success();
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "catalog package path validation allocation failed");
    } catch (const std::filesystem::filesystem_error& exception)
    {
        return Core::failure(makeStageFilesystemError(
            "catalog package path validation failed", exception.code()));
    }
}

[[nodiscard]] Core::Status validateTileMapCookAsset(const CookAssetValidationView& tileMapAsset,
                                                     std::span<const CookAssetValidationView> assets)
{
    if (tileMapAsset.assetTypeVersion != AssetFormat::TileMapWire::SchemaVersion)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "tilemap cook asset type version must match the current payload schema");
    }
    auto tileMap = AssetFormat::parseTileMapPayload(tileMapAsset.payload);
    if (!tileMap)
    {
        return Core::failure(std::move(tileMap.error()));
    }
    const AssetFormat::CookedAssetWriteDependency* tilesetDependency = nullptr;
    Core::u32 deferredChunkDependencyCount = 0;
    for (const auto& dependency : tileMapAsset.dependencies)
    {
        if (dependency.expectedKind == AssetFormat::AssetKind::Tileset &&
            dependency.flags == AssetFormat::DependencyFlags::Required)
        {
            if (tilesetDependency != nullptr)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "tilemap cook asset requires exactly one required Tileset dependency");
            }
            tilesetDependency = &dependency;
        }
        else if (dependency.expectedKind == AssetFormat::AssetKind::TileMapChunk &&
                 dependency.flags == (AssetFormat::DependencyFlags::Required |
                                      AssetFormat::DependencyFlags::Deferred))
        {
            ++deferredChunkDependencyCount;
        }
        else
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "tilemap cook asset dependency must be required Tileset or deferred TileMapChunk");
        }
    }
    if (tilesetDependency == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "tilemap cook asset requires exactly one required Tileset dependency");
    }

    const CookAssetValidationView* tilesetAsset = findCookAsset(assets, tilesetDependency->assetId);
    if (tilesetAsset == nullptr || tilesetAsset->assetKind != AssetFormat::AssetKind::Tileset ||
        tilesetAsset->assetTypeVersion != AssetFormat::TilesetWire::SchemaVersion)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "tilemap Tileset dependency is missing or has the wrong kind/version");
    }
    auto tileset = AssetFormat::parseTilesetPayload(tilesetAsset->payload);
    if (!tileset)
    {
        return Core::failure(std::move(tileset.error()));
    }

    std::array<bool, 65536> knownLocalIds{};
    for (Core::u32 index = 0; index < tileset->tileCount; ++index)
    {
        const auto tile = tileset->tile(index);
        if (!tile || tile->localId == AssetFormat::TileMapWire::EmptyTileId || knownLocalIds[tile->localId])
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "tilemap Tileset dependency has an invalid or duplicate local tile id");
        }
        knownLocalIds[tile->localId] = true;
    }

    Core::u32 chunkRefCount = 0;
    for (Core::u16 layerIndex = 0; layerIndex < tileMap->layerCount; ++layerIndex)
    {
        const auto layer = tileMap->layerAt(layerIndex);
        if (!layer)
        {
            return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                 "tilemap layer disappeared after payload validation");
        }
        if (layer->kind != AssetFormat::TileMapLayerKind::Tile)
        {
            continue;
        }
        chunkRefCount += layer->chunkRefCount;
        for (Core::u32 chunkIndex = 0; chunkIndex < layer->chunkRefCount; ++chunkIndex)
        {
            const auto ref = layer->chunkRefAt(chunkIndex);
            if (!ref)
            {
                return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                     "tilemap chunk ref disappeared after payload validation");
            }
            const auto dependency = std::find_if(
                tileMapAsset.dependencies.begin(), tileMapAsset.dependencies.end(),
                [assetId = ref->chunkAssetId](const AssetFormat::CookedAssetWriteDependency& candidate) {
                    return candidate.assetId == assetId &&
                           candidate.expectedKind == AssetFormat::AssetKind::TileMapChunk &&
                           candidate.flags == (AssetFormat::DependencyFlags::Required |
                                               AssetFormat::DependencyFlags::Deferred);
                });
            if (dependency == tileMapAsset.dependencies.end())
            {
                return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                     "tilemap chunk ref is missing a deferred chunk dependency");
            }
            const CookAssetValidationView* chunkAsset = findCookAsset(assets, ref->chunkAssetId);
            if (chunkAsset == nullptr || chunkAsset->assetKind != AssetFormat::AssetKind::TileMapChunk ||
                chunkAsset->assetTypeVersion != AssetFormat::TileMapChunkWire::SchemaVersion)
            {
                return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                     "tilemap chunk dependency is missing or has the wrong kind/version");
            }
            auto chunk = AssetFormat::parseTileMapChunkPayload(chunkAsset->payload);
            if (!chunk)
            {
                return Core::failure(std::move(chunk.error()));
            }
            if (chunk->parentTileMapId != tileMapAsset.assetId || chunk->layerId != layer->stableLayerId ||
                chunk->chunkX != ref->chunkX || chunk->chunkY != ref->chunkY ||
                chunk->widthCells != ref->widthCells || chunk->heightCells != ref->heightCells ||
                chunk->nonEmptyCount != ref->nonEmptyCount)
            {
                return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                     "tilemap chunk payload does not match its root chunk ref");
            }
            for (Core::u16 y = 0; y < chunk->heightCells; ++y)
            {
                for (Core::u16 x = 0; x < chunk->widthCells; ++x)
                {
                    const auto localId = chunk->cellAt(x, y);
                    if (!localId || (*localId != AssetFormat::TileMapWire::EmptyTileId && !knownLocalIds[*localId]))
                    {
                        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                             "tilemap tile chunk references an unknown Tileset local id");
                    }
                }
            }
        }
    }
    if (chunkRefCount != deferredChunkDependencyCount)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "tilemap deferred chunk dependency count does not match chunk refs");
    }
    return Core::success();
}

[[nodiscard]] Core::Result<CookedPackage> cookPackageInternal(const CatalogCookRequest& request,
                                                               bool validateTileMaps = true)
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
    // Dependency streams must be strictly increasing AssetId (manifest + cooked validation).
    for (const CatalogCookAssetSpec& asset : sorted)
    {
        for (std::size_t depIndex = 1; depIndex < asset.dependencies.size(); ++depIndex)
        {
            if (!(asset.dependencies[depIndex - 1U].assetId < asset.dependencies[depIndex].assetId))
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "cook dependency AssetIds must be strictly increasing");
            }
        }
    }
    std::vector<CookAssetValidationView> validationViews;
    validationViews.reserve(sorted.size());
    for (const CatalogCookAssetSpec& asset : sorted)
    {
        validationViews.push_back(CookAssetValidationView{
            .assetKind = asset.assetKind,
            .assetId = asset.assetId,
            .assetTypeVersion = asset.assetTypeVersion,
            .payload = asset.payload,
            .dependencies = asset.dependencies,
        });
    }
    if (validateTileMaps)
    {
        for (const CookAssetValidationView& asset : validationViews)
        {
            if (asset.assetKind == AssetFormat::AssetKind::TileMap)
            {
                if (auto status = validateTileMapCookAsset(asset, validationViews); !status)
                {
                    return Core::failure(
                        std::move(status.error()).withContext("cookCatalogPackage", "validateTileMap"));
                }
            }
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

Core::Result<CatalogSnapshot>
cookAndStageCatalogPackage(std::string_view stagingRootUtf8, const CatalogCookRequest& request,
                           CatalogPackageStageConfig config)
{
    if (config.validation.manifest.catalog.memoryResource == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "catalog staging validation requires a memory resource");
    }

    auto package = cookPackageInternal(request);
    if (!package)
    {
        return Core::failure(std::move(package.error()).withContext("cookAndStageCatalogPackage", "cook"));
    }

    if (Core::Status created = createFreshStageRoot(stagingRootUtf8); !created)
    {
        return Core::failure(std::move(created.error()).withContext("cookAndStageCatalogPackage", "createStage"));
    }

    auto published = publishCatalogPackage(stagingRootUtf8, DefaultCatalogManifestRelativePath,
                                           package->summary.manifestBytes, package->objectViews);
    if (!published)
    {
        return Core::failure(std::move(published.error()).withContext("cookAndStageCatalogPackage", "publish"));
    }

    config.validation.manifestRelativePath = DefaultCatalogManifestRelativePath;
    config.validation.validateOnOpen = true;
    config.validation.validation.verifyContent = true;
    auto catalog = openCatalogPackage(stagingRootUtf8, config.validation);
    if (!catalog)
    {
        return Core::failure(std::move(catalog.error()).withContext("cookAndStageCatalogPackage", "validate"));
    }
    return std::move(*catalog);
}

Core::Result<CatalogSnapshot>
cookAndStageIncrementalCatalogPackage(std::string_view stagingRootUtf8,
                                      std::string_view baselineRootUtf8,
                                      const CatalogSnapshot& baseline,
                                      std::span<const Core::AssetId> cleanAssetIds,
                                      const CatalogCookRequest& dirtyRequest,
                                      CatalogPackageStageConfig config)
{
    if (config.validation.manifest.catalog.memoryResource == nullptr ||
        config.validation.validation.file.memoryResource == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "incremental catalog staging requires validation memory resources");
    }
    if (stagingRootUtf8.empty() || !Core::isStrictUtf8WithoutNul(stagingRootUtf8))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "incremental catalog staging root is invalid");
    }
    if (dirtyRequest.targetPlatform == AssetFormat::TargetPlatform::Invalid)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "incremental catalog staging requires target platform");
    }
    if (!cleanAssetIds.empty() &&
        (!baseline || baselineRootUtf8.empty() || !Core::isStrictUtf8WithoutNul(baselineRootUtf8)))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "incremental catalog staging requires a valid baseline package");
    }
    if (cleanAssetIds.empty() && dirtyRequest.assets.empty())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "incremental catalog staging requires at least one clean or dirty asset");
    }
    if (!baselineRootUtf8.empty())
    {
        if (const auto status = validateStageOutsideBaseline(stagingRootUtf8, baselineRootUtf8);
            !status)
        {
            return Core::failure(status.error());
        }
    }

    try
    {
        std::vector<Core::AssetId> sortedCleanIds(cleanAssetIds.begin(), cleanAssetIds.end());
        std::sort(sortedCleanIds.begin(), sortedCleanIds.end());
        for (std::size_t index = 0; index < sortedCleanIds.size(); ++index)
        {
            if (!sortedCleanIds[index])
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "clean catalog asset id is invalid");
            }
            if (index > 0U && !(sortedCleanIds[index - 1U] < sortedCleanIds[index]))
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "duplicate clean AssetId in incremental catalog request");
            }
        }

        std::optional<CookedPackage> dirtyPackage;
        if (!dirtyRequest.assets.empty())
        {
            auto cooked = cookPackageInternal(dirtyRequest, false);
            if (!cooked)
            {
                return Core::failure(
                    std::move(cooked.error()).withContext("cookAndStageIncrementalCatalogPackage", "cookDirty"));
            }
            dirtyPackage.emplace(std::move(*cooked));
        }

        std::vector<std::vector<std::byte>> cleanObjectStorage;
        cleanObjectStorage.reserve(sortedCleanIds.size());
        std::vector<IncrementalCookEntry> entries;
        entries.reserve(sortedCleanIds.size() + dirtyRequest.assets.size());

        auto cleanLoadConfig = config.validation.validation.file;
        cleanLoadConfig.verifyContentHash = true;
        for (const Core::AssetId cleanAssetId : sortedCleanIds)
        {
            const auto entryIndex = baseline.find(cleanAssetId);
            if (!entryIndex)
            {
                return Core::failure(Core::CoreErrorCode::NotFound,
                                     "clean AssetId is not present in the baseline catalog");
            }
            const auto baselineEntry = baseline.entry(*entryIndex);
            if (!baselineEntry)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "baseline catalog entry is missing after find");
            }

            auto cleanObject = loadCookedAssetFromCatalog(baselineRootUtf8, baseline, cleanAssetId,
                                                           cleanLoadConfig);
            if (!cleanObject)
            {
                return Core::failure(std::move(cleanObject.error()).withContext(
                    "cookAndStageIncrementalCatalogPackage", "loadCleanObject"));
            }
            if (cleanObject->header().targetPlatform != dirtyRequest.targetPlatform)
            {
                return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                     "clean and dirty catalog target platforms do not match");
            }
            if (cleanObject->header().dependencyCount != baselineEntry->dependencyCount)
            {
                return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                     "clean cooked object dependencies do not match the baseline catalog");
            }

            std::vector<AssetFormat::CookedAssetWriteDependency> dependencies;
            dependencies.reserve(baselineEntry->dependencyCount);
            for (Core::u32 dependencyIndex = 0; dependencyIndex < baselineEntry->dependencyCount;
                 ++dependencyIndex)
            {
                const auto baselineDependency = baseline.dependency(*entryIndex, dependencyIndex);
                const auto objectDependency = cleanObject->dependency(dependencyIndex);
                if (!baselineDependency || !objectDependency ||
                    baselineDependency->assetId != objectDependency->assetId ||
                    baselineDependency->expectedKind != objectDependency->expectedKind ||
                    baselineDependency->flags != objectDependency->flags)
                {
                    return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                                         "clean cooked object dependencies do not match the baseline catalog");
                }
                dependencies.push_back(AssetFormat::CookedAssetWriteDependency{
                    .assetId = baselineDependency->assetId,
                    .expectedKind = baselineDependency->expectedKind,
                    .flags = baselineDependency->flags,
                });
            }

            cleanObjectStorage.emplace_back(cleanObject->bytes().begin(), cleanObject->bytes().end());
            auto retainedObject = AssetFormat::parseCookedAssetView(cleanObjectStorage.back());
            if (!retainedObject)
            {
                return Core::failure(std::move(retainedObject.error()).withContext(
                    "cookAndStageIncrementalCatalogPackage", "retainCleanObject"));
            }
            entries.push_back(IncrementalCookEntry{
                .assetKind = baselineEntry->assetKind,
                .assetId = baselineEntry->assetId,
                .assetTypeVersion = baselineEntry->assetTypeVersion,
                .contentHash = baselineEntry->contentHash,
                .payload = retainedObject->payload(),
                .objectBytes = cleanObjectStorage.back(),
                .dependencies = std::move(dependencies),
            });
        }

        if (dirtyPackage)
        {
            for (const CatalogPackageObjectBlob& object : dirtyPackage->objectViews)
            {
                auto view = AssetFormat::parseCookedAssetView(object.bytes);
                if (!view)
                {
                    return Core::failure(std::move(view.error()).withContext(
                        "cookAndStageIncrementalCatalogPackage", "parseDirtyObject"));
                }
                std::vector<AssetFormat::CookedAssetWriteDependency> dependencies;
                dependencies.reserve(view->header().dependencyCount);
                for (Core::u32 dependencyIndex = 0; dependencyIndex < view->header().dependencyCount;
                     ++dependencyIndex)
                {
                    const auto dependency = view->dependency(dependencyIndex);
                    if (!dependency)
                    {
                        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                             "dirty cooked object dependency is missing after parse");
                    }
                    dependencies.push_back(AssetFormat::CookedAssetWriteDependency{
                        .assetId = dependency->assetId,
                        .expectedKind = dependency->expectedKind,
                        .flags = dependency->flags,
                    });
                }
                entries.push_back(IncrementalCookEntry{
                    .assetKind = view->header().assetKind,
                    .assetId = view->header().assetId,
                    .assetTypeVersion = view->header().assetTypeVersion,
                    .contentHash = view->header().contentHash,
                    .payload = view->payload(),
                    .objectBytes = object.bytes,
                    .dependencies = std::move(dependencies),
                });
            }
        }

        std::sort(entries.begin(), entries.end(), [](const IncrementalCookEntry& left,
                                                     const IncrementalCookEntry& right) {
            return left.assetId < right.assetId;
        });
        for (std::size_t index = 0; index < entries.size(); ++index)
        {
            if (index > 0U && !(entries[index - 1U].assetId < entries[index].assetId))
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "clean and dirty incremental assets contain duplicate AssetId");
            }
            for (std::size_t dependencyIndex = 1; dependencyIndex < entries[index].dependencies.size();
                 ++dependencyIndex)
            {
                if (!(entries[index].dependencies[dependencyIndex - 1U].assetId <
                      entries[index].dependencies[dependencyIndex].assetId))
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "incremental catalog dependency AssetIds must be strictly increasing");
                }
            }
        }

        std::vector<CookAssetValidationView> validationViews;
        validationViews.reserve(entries.size());
        for (const IncrementalCookEntry& entry : entries)
        {
            validationViews.push_back(CookAssetValidationView{
                .assetKind = entry.assetKind,
                .assetId = entry.assetId,
                .assetTypeVersion = entry.assetTypeVersion,
                .payload = entry.payload,
                .dependencies = entry.dependencies,
            });
        }
        for (const CookAssetValidationView& entry : validationViews)
        {
            if (entry.assetKind == AssetFormat::AssetKind::TileMap)
            {
                if (auto status = validateTileMapCookAsset(entry, validationViews); !status)
                {
                    return Core::failure(std::move(status.error()).withContext(
                        "cookAndStageIncrementalCatalogPackage", "validateTileMap"));
                }
            }
        }

        std::vector<AssetFormat::CookedManifestWriteEntry> manifestEntries;
        manifestEntries.reserve(entries.size());
        std::vector<CatalogPackageObjectBlob> objectViews;
        objectViews.reserve(entries.size());
        for (const IncrementalCookEntry& entry : entries)
        {
            manifestEntries.push_back(AssetFormat::CookedManifestWriteEntry{
                .assetId = entry.assetId,
                .contentHash = entry.contentHash,
                .assetKind = entry.assetKind,
                .assetTypeVersion = entry.assetTypeVersion,
                .cookedFileBytes = entry.objectBytes.size(),
                .dependencies = entry.dependencies,
            });
            objectViews.push_back(CatalogPackageObjectBlob{
                .assetKind = entry.assetKind,
                .assetId = entry.assetId,
                .bytes = entry.objectBytes,
            });
        }

        auto manifestBytes = AssetFormat::writeCookedManifestBytes(AssetFormat::CookedManifestWriteDesc{
            .targetPlatform = dirtyRequest.targetPlatform,
            .entries = manifestEntries,
        });
        if (!manifestBytes)
        {
            return Core::failure(std::move(manifestBytes.error()).withContext(
                "cookAndStageIncrementalCatalogPackage", "writeManifest"));
        }

        auto manifestView = AssetFormat::parseCookedManifestView(
            *manifestBytes, config.validation.manifest.manifestLimits);
        if (!manifestView)
        {
            return Core::failure(std::move(manifestView.error()).withContext(
                "cookAndStageIncrementalCatalogPackage", "parseManifest"));
        }
        {
            auto graph = CatalogSnapshot::Create(*manifestView, config.validation.manifest.catalog);
            if (!graph)
            {
                return Core::failure(std::move(graph.error()).withContext(
                    "cookAndStageIncrementalCatalogPackage", "validateDependencies"));
            }
        }

        if (Core::Status created = createFreshStageRoot(stagingRootUtf8); !created)
        {
            return Core::failure(std::move(created.error()).withContext(
                "cookAndStageIncrementalCatalogPackage", "createStage"));
        }
        auto published = publishCatalogPackage(stagingRootUtf8, DefaultCatalogManifestRelativePath,
                                               *manifestBytes, objectViews);
        if (!published)
        {
            return Core::failure(std::move(published.error()).withContext(
                "cookAndStageIncrementalCatalogPackage", "publish"));
        }

        config.validation.manifestRelativePath = DefaultCatalogManifestRelativePath;
        config.validation.validateOnOpen = true;
        config.validation.validation.verifyContent = true;
        auto catalog = openCatalogPackage(stagingRootUtf8, config.validation);
        if (!catalog)
        {
            return Core::failure(std::move(catalog.error()).withContext(
                "cookAndStageIncrementalCatalogPackage", "validate"));
        }
        return std::move(*catalog);
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "incremental catalog staging allocation failed");
    }
}

namespace {

[[nodiscard]] Core::Result<CatalogCookRequest>
parseCatalogCookRecipeInternal(std::string_view recipeText,
                               std::string_view baseDirectoryUtf8,
                               const RecipeSourceCaptureContext* sourceCapture)
{
    CatalogCookRequest request{};
    std::pmr::unsynchronized_pool_resource memory;

    // Multi-line builders for tileset/tilemap.
    enum class MultiState : Core::u8 { None, Tileset, TileMap };
    enum class TileMapBlockState : Core::u8 { None, TileLayer, ObjectLayer };
    struct PendingTileMapProperty final {
        std::string key{};
        std::string value{};
    };
    struct PendingTileMapObject final {
        Core::u32 stableObjectId = 0;
        AssetFormat::TileMapObjectKind kind = AssetFormat::TileMapObjectKind::Point;
        bool visible = true;
        std::string name{};
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        std::vector<PendingTileMapProperty> properties{};
    };
    struct PendingTileMapLayer final {
        Core::u32 stableLayerId = 0;
        AssetFormat::TileMapLayerKind kind = AssetFormat::TileMapLayerKind::Tile;
        bool visible = true;
        std::string name{};
        std::vector<PendingTileMapProperty> properties{};
        std::vector<Core::u16> tiles{};
        std::vector<PendingTileMapObject> objects{};
        Core::u32 rowCount = 0;
    };
    MultiState multi = MultiState::None;
    TileMapBlockState tileMapBlock = TileMapBlockState::None;
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
    std::vector<PendingTileMapLayer> pendingMapLayers{};

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
        if (tileMapBlock != TileMapBlockState::None)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tilemap layer must end before endtilemap");
        }
        if (pendingMapLayers.empty())
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tilemap requires at least one layer");
        }

        std::vector<std::vector<AssetFormat::TileMapPropertyDesc>> layerProperties;
        std::vector<std::vector<std::vector<AssetFormat::TileMapPropertyDesc>>> objectProperties;
        std::vector<std::vector<AssetFormat::TileMapObjectDesc>> layerObjects;
        std::vector<std::vector<AssetFormat::TileMapChunkRefDesc>> layerChunkRefs;
        std::vector<AssetFormat::TileMapLayerDesc> layers;
        std::vector<CatalogCookAssetSpec> chunkAssets;
        layerProperties.reserve(pendingMapLayers.size());
        objectProperties.reserve(pendingMapLayers.size());
        layerObjects.reserve(pendingMapLayers.size());
        layers.reserve(pendingMapLayers.size());
        layerChunkRefs.reserve(pendingMapLayers.size());
        chunkAssets.reserve(pendingMapLayers.size() * 4U);
        constexpr Core::u16 RecipeChunkSize = 16U;
        for (std::size_t pendingLayerIndex = 0; pendingLayerIndex < pendingMapLayers.size(); ++pendingLayerIndex)
        {
            const PendingTileMapLayer& pendingLayer = pendingMapLayers[pendingLayerIndex];
            if (pendingLayer.kind == AssetFormat::TileMapLayerKind::Tile && pendingLayer.rowCount != pendingMapH)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "tile layer row count does not match tilemap height");
            }

            layerProperties.emplace_back();
            auto& properties = layerProperties.back();
            properties.reserve(pendingLayer.properties.size());
            for (const PendingTileMapProperty& property : pendingLayer.properties)
            {
                properties.push_back(AssetFormat::TileMapPropertyDesc{
                    .key = property.key,
                    .value = property.value,
                });
            }

            layerChunkRefs.emplace_back();
            auto& chunkRefs = layerChunkRefs.back();

            if (pendingLayer.kind == AssetFormat::TileMapLayerKind::Tile)
            {
                const Core::u32 chunkCountX = (pendingMapW + RecipeChunkSize - 1U) / RecipeChunkSize;
                const Core::u32 chunkCountY = (pendingMapH + RecipeChunkSize - 1U) / RecipeChunkSize;
                for (Core::u32 chunkY = 0; chunkY < chunkCountY; ++chunkY)
                {
                    for (Core::u32 chunkX = 0; chunkX < chunkCountX; ++chunkX)
                    {
                        const Core::u32 originX = chunkX * RecipeChunkSize;
                        const Core::u32 originY = chunkY * RecipeChunkSize;
                        const Core::u16 widthCells = static_cast<Core::u16>((std::min)(static_cast<Core::u32>(RecipeChunkSize), pendingMapW - originX));
                        const Core::u16 heightCells = static_cast<Core::u16>((std::min)(static_cast<Core::u32>(RecipeChunkSize), pendingMapH - originY));
                        std::vector<Core::u16> chunkCells;
                        chunkCells.reserve(static_cast<std::size_t>(widthCells) * heightCells);
                        Core::u32 nonEmptyCount = 0;
                        for (Core::u16 localY = 0; localY < heightCells; ++localY)
                        {
                            for (Core::u16 localX = 0; localX < widthCells; ++localX)
                            {
                                const Core::u16 cell = pendingLayer.tiles[(originY + localY) * pendingMapW + originX + localX];
                                chunkCells.push_back(cell);
                                nonEmptyCount += cell != AssetFormat::TileMapWire::EmptyTileId ? 1U : 0U;
                            }
                        }
                        if (nonEmptyCount == 0U)
                        {
                            continue;
                        }
                        auto chunkId = deriveTileMapChunkAssetId(pendingMapId, pendingLayer.stableLayerId, chunkX, chunkY);
                        if (!chunkId)
                        {
                            return Core::failure(std::move(chunkId.error()));
                        }
                        auto chunkPayload = AssetFormat::writeTileMapChunkPayloadBytes(AssetFormat::TileMapChunkPayloadDesc{
                            .parentTileMapId = pendingMapId,
                            .layerId = pendingLayer.stableLayerId,
                            .chunkX = chunkX,
                            .chunkY = chunkY,
                            .widthCells = widthCells,
                            .heightCells = heightCells,
                            .cells = chunkCells,
                        });
                        if (!chunkPayload)
                        {
                            return Core::failure(std::move(chunkPayload.error()));
                        }
                        chunkRefs.push_back(AssetFormat::TileMapChunkRefDesc{
                            .chunkX = chunkX,
                            .chunkY = chunkY,
                            .widthCells = widthCells,
                            .heightCells = heightCells,
                            .nonEmptyCount = nonEmptyCount,
                            .chunkAssetId = *chunkId,
                        });
                        chunkAssets.push_back(CatalogCookAssetSpec{
                            .assetKind = AssetFormat::AssetKind::TileMapChunk,
                            .assetId = *chunkId,
                            .assetTypeVersion = AssetFormat::TileMapChunkWire::SchemaVersion,
                            .payload = std::move(*chunkPayload),
                        });
                    }
                }
            }
            objectProperties.emplace_back();
            layerObjects.emplace_back();
            auto& ownedObjectProperties = objectProperties.back();
            auto& objects = layerObjects.back();
            ownedObjectProperties.reserve(pendingLayer.objects.size());
            objects.reserve(pendingLayer.objects.size());
            for (const PendingTileMapObject& pendingObject : pendingLayer.objects)
            {
                ownedObjectProperties.emplace_back();
                auto& objectPropertyViews = ownedObjectProperties.back();
                objectPropertyViews.reserve(pendingObject.properties.size());
                for (const PendingTileMapProperty& property : pendingObject.properties)
                {
                    objectPropertyViews.push_back(AssetFormat::TileMapPropertyDesc{
                        .key = property.key,
                        .value = property.value,
                    });
                }
                objects.push_back(AssetFormat::TileMapObjectDesc{
                    .stableObjectId = pendingObject.stableObjectId,
                    .kind = pendingObject.kind,
                    .visible = pendingObject.visible,
                    .name = pendingObject.name,
                    .x = pendingObject.x,
                    .y = pendingObject.y,
                    .width = pendingObject.width,
                    .height = pendingObject.height,
                    .properties = objectPropertyViews,
                });
            }
            layers.push_back(AssetFormat::TileMapLayerDesc{
                .stableLayerId = pendingLayer.stableLayerId,
                .kind = pendingLayer.kind,
                .visible = pendingLayer.visible,
                .name = pendingLayer.name,
                .properties = properties,
                .chunkRefs = chunkRefs,
                .objects = objects,
            });
        }

        auto payload = AssetFormat::writeTileMapPayloadBytes(AssetFormat::TileMapPayloadDesc{
            .widthCells = pendingMapW,
            .heightCells = pendingMapH,
            .cellSizeMeters = pendingCellSize,
            .chunkSizeCells = RecipeChunkSize,
            .layers = layers,
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
        for (const CatalogCookAssetSpec& chunkAsset : chunkAssets)
        {
            asset.dependencies.push_back(AssetFormat::CookedAssetWriteDependency{
                .assetId = chunkAsset.assetId,
                .expectedKind = AssetFormat::AssetKind::TileMapChunk,
                .flags = AssetFormat::DependencyFlags::Required | AssetFormat::DependencyFlags::Deferred,
            });
        }
        std::sort(asset.dependencies.begin(), asset.dependencies.end(),
                  [](const AssetFormat::CookedAssetWriteDependency& left,
                     const AssetFormat::CookedAssetWriteDependency& right) { return left.assetId < right.assetId; });
        request.assets.push_back(std::move(asset));
        for (CatalogCookAssetSpec& chunkAsset : chunkAssets)
        {
            request.assets.push_back(std::move(chunkAsset));
        }
        multi = MultiState::None;
        tileMapBlock = TileMapBlockState::None;
        pendingMapLayers.clear();
        return Core::success();
    };

    auto flushMulti = [&]() -> Core::Status {
        if (multi == MultiState::Tileset)
        {
            return flushTileset();
        }
        if (multi == MultiState::TileMap)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tilemap requires explicit endtilemap");
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
        if (multi == MultiState::TileMap)
        {
            if (tokens[0] == "endtilemap")
            {
                if (tokens.size() != 1 || tileMapBlock != TileMapBlockState::None)
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "endtilemap requires all layers to be closed");
                }
                if (const auto status = flushTileMap(); !status)
                {
                    return Core::failure(status.error());
                }
                continue;
            }

            if (tokens[0] == "tilelayer" || tokens[0] == "objectlayer")
            {
                if (tileMapBlock != TileMapBlockState::None || tokens.size() != 4)
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "tilemap layer needs id visible name and no open layer");
                }
                Core::u32 layerId = 0;
                Core::u32 visible = 0;
                if (!parseU32Token(tokens[1], layerId) || !parseU32Token(tokens[2], visible) || layerId == 0 ||
                    visible > 1)
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid tilemap layer fields");
                }
                PendingTileMapLayer layer{};
                layer.stableLayerId = layerId;
                layer.kind = tokens[0] == "tilelayer" ? AssetFormat::TileMapLayerKind::Tile
                                                        : AssetFormat::TileMapLayerKind::Object;
                layer.visible = visible != 0;
                layer.name = tokens[3];
                if (layer.kind == AssetFormat::TileMapLayerKind::Tile)
                {
                    layer.tiles.reserve(static_cast<std::size_t>(pendingMapW) * pendingMapH);
                    tileMapBlock = TileMapBlockState::TileLayer;
                }
                else
                {
                    tileMapBlock = TileMapBlockState::ObjectLayer;
                }
                pendingMapLayers.push_back(std::move(layer));
                continue;
            }

            if (tokens[0] == "endlayer")
            {
                if (tokens.size() != 1 || tileMapBlock == TileMapBlockState::None || pendingMapLayers.empty())
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig, "endlayer has no open tilemap layer");
                }
                const PendingTileMapLayer& layer = pendingMapLayers.back();
                if (layer.kind == AssetFormat::TileMapLayerKind::Tile && layer.rowCount != pendingMapH)
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "tile layer row count does not match tilemap height");
                }
                tileMapBlock = TileMapBlockState::None;
                continue;
            }

            if (tileMapBlock == TileMapBlockState::None || pendingMapLayers.empty())
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "tilemap content must be inside tilelayer or objectlayer");
            }
            PendingTileMapLayer& layer = pendingMapLayers.back();
            if (tokens[0] == "property")
            {
                if (tokens.size() != 3)
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig, "property needs key value");
                }
                layer.properties.push_back(PendingTileMapProperty{.key = tokens[1], .value = tokens[2]});
                continue;
            }
            if (tileMapBlock == TileMapBlockState::TileLayer && tokens[0] == "row")
            {
                if (tokens.size() != 1U + pendingMapW)
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "row width does not match tilemap width");
                }
                if (layer.rowCount >= pendingMapH)
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
                    layer.tiles.push_back(static_cast<Core::u16>(cell));
                }
                ++layer.rowCount;
                continue;
            }
            if (tileMapBlock == TileMapBlockState::ObjectLayer && tokens[0] == "point")
            {
                if (tokens.size() != 6)
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "point needs id visible name x y");
                }
                PendingTileMapObject object{};
                object.kind = AssetFormat::TileMapObjectKind::Point;
                Core::u32 visible = 0;
                object.name = tokens[3];
                if (!parseU32Token(tokens[1], object.stableObjectId) || object.stableObjectId == 0 ||
                    !parseU32Token(tokens[2], visible) || visible > 1 || !parseFloatToken(tokens[4], object.x) ||
                    !parseFloatToken(tokens[5], object.y))
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid point fields");
                }
                object.visible = visible != 0;
                layer.objects.push_back(std::move(object));
                continue;
            }
            if (tileMapBlock == TileMapBlockState::ObjectLayer && tokens[0] == "rectangle")
            {
                if (tokens.size() != 8)
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "rectangle needs id visible name x y width height");
                }
                PendingTileMapObject object{};
                object.kind = AssetFormat::TileMapObjectKind::Rectangle;
                Core::u32 visible = 0;
                object.name = tokens[3];
                if (!parseU32Token(tokens[1], object.stableObjectId) || object.stableObjectId == 0 ||
                    !parseU32Token(tokens[2], visible) || visible > 1 || !parseFloatToken(tokens[4], object.x) ||
                    !parseFloatToken(tokens[5], object.y) || !parseFloatToken(tokens[6], object.width) ||
                    !parseFloatToken(tokens[7], object.height))
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid rectangle fields");
                }
                object.visible = visible != 0;
                layer.objects.push_back(std::move(object));
                continue;
            }
            if (tileMapBlock == TileMapBlockState::ObjectLayer && tokens[0] == "objectproperty")
            {
                if (tokens.size() != 4)
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "objectproperty needs objectId key value");
                }
                Core::u32 objectId = 0;
                if (!parseU32Token(tokens[1], objectId) || objectId == 0)
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid objectproperty id");
                }
                auto object = std::find_if(layer.objects.begin(), layer.objects.end(),
                                           [objectId](const PendingTileMapObject& candidate) {
                                               return candidate.stableObjectId == objectId;
                                           });
                if (object == layer.objects.end())
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "objectproperty references unknown object id");
                }
                object->properties.push_back(PendingTileMapProperty{.key = tokens[2], .value = tokens[3]});
                continue;
            }
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid directive inside tilemap");
        }

        // Starting a new top-level directive flushes any open multi-line block.
        if (const auto flushStatus = flushMulti(); !flushStatus)
        {
            return Core::failure(flushStatus.error());
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
        if (tokens[0] == "spriteanim")
        {
            auto asset = parseSpriteAnimationInline(tokens);
            if (!asset)
            {
                return Core::failure(std::move(asset.error()));
            }
            request.assets.push_back(std::move(*asset));
            continue;
        }
        if (tokens[0] == "audioclip")
        {
            auto asset = parseAudioClipInline(tokens, baseDirectoryUtf8, sourceCapture);
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
            std::array<float, 24 * AssetFormat::StaticMeshWire::FloatsPerVertex> vertices{};
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
        if (tokens[0] == "prefab")
        {
            // prefab <id> root [meshId] [materialId]
            if (tokens.size() != 3 && tokens.size() != 5)
            {
                return Core::failure(
                    AssetErrorCode::InvalidCatalogConfig,
                    "prefab currently supports: prefab <id> root [meshId materialId]");
            }
            if (tokens[2] != "root")
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "prefab layout must be root");
            }
            auto prefabId = Core::AssetId::parseCanonical(tokens[1]);
            if (!prefabId)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid prefab asset id");
            }
            AssetFormat::PrefabNodeDesc node{
                .stableNodeId = 1,
                .parentIndex = -1,
            };
            if (tokens.size() == 5)
            {
                auto meshId = Core::AssetId::parseCanonical(tokens[3]);
                auto materialId = Core::AssetId::parseCanonical(tokens[4]);
                if (!meshId || !materialId)
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "invalid prefab mesh/material asset id");
                }
                node.meshId = *meshId;
                node.materialId = *materialId;
            }
            const std::array nodes{node};
            auto payload =
                AssetFormat::writePrefabPayloadBytes(AssetFormat::PrefabPayloadDesc{.nodes = nodes});
            if (!payload)
            {
                return Core::failure(std::move(payload.error()));
            }
            CatalogCookAssetSpec asset{
                .assetKind = AssetFormat::AssetKind::Prefab,
                .assetId = *prefabId,
                .assetTypeVersion = AssetFormat::PrefabWire::SchemaVersion,
                .payload = std::move(*payload),
            };
            if (static_cast<bool>(node.meshId))
            {
                asset.dependencies.push_back(AssetFormat::CookedAssetWriteDependency{
                    .assetId = node.meshId,
                    .expectedKind = AssetFormat::AssetKind::StaticMesh,
                    .flags = AssetFormat::DependencyFlags::Required,
                });
                asset.dependencies.push_back(AssetFormat::CookedAssetWriteDependency{
                    .assetId = node.materialId,
                    .expectedKind = AssetFormat::AssetKind::Material,
                    .flags = AssetFormat::DependencyFlags::Required,
                });
                std::sort(asset.dependencies.begin(), asset.dependencies.end(),
                          [](const AssetFormat::CookedAssetWriteDependency& left,
                             const AssetFormat::CookedAssetWriteDependency& right) {
                              return left.assetId < right.assetId;
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
            //   tilelayer <stableLayerId> <0|1 visible> <name>
            //   property <key> <value>
            //   row <localId>...
            //   endlayer
            //   objectlayer <stableLayerId> <0|1 visible> <name>
            //   point <stableObjectId> <0|1 visible> <name> <x> <y>
            //   rectangle <stableObjectId> <0|1 visible> <name> <x> <y> <width> <height>
            //   objectproperty <stableObjectId> <key> <value>
            //   endlayer
            // endtilemap
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
                !parseFloatToken(tokens[5], cellSize) || width == 0 || height == 0 ||
                width > AssetFormat::TileMapWire::MaxDimension || height > AssetFormat::TileMapWire::MaxDimension ||
                !std::isfinite(cellSize) || !(cellSize > 0.0f))
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid tilemap header fields");
            }
            multi = MultiState::TileMap;
            pendingMapId = *mapId;
            pendingMapTilesetId = *tilesetId;
            pendingMapW = width;
            pendingMapH = height;
            pendingCellSize = cellSize;
            pendingMapLayers.clear();
            tileMapBlock = TileMapBlockState::None;
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
        if (auto validated = validateRecipeSourcePath(
                sourceCapture != nullptr ? &sourceCapture->config : nullptr, *payloadPath);
            !validated)
        {
            return Core::failure(std::move(validated.error()).withContext(
                "parseCatalogCookRecipe", "validatePayloadPath"));
        }
        auto payload = Core::readFile(*payloadPath, Core::ReadFileConfig{.memoryResource = &memory});
        if (!payload)
        {
            return Core::failure(std::move(payload.error()).withContext("parseCatalogCookRecipe", "readPayload"));
        }
        if (auto captured = captureRecipeDependencyBytes(sourceCapture, *payloadPath, *payload); !captured)
        {
            return Core::failure(std::move(captured.error()).withContext(
                "parseCatalogCookRecipe", "capturePayload"));
        }

        CatalogCookAssetSpec asset{
            .assetKind = kind,
            .assetId = *assetId,
            .assetTypeVersion = currentAssetTypeVersion(kind),
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
        return Core::failure(flushStatus.error());
    }
    if (request.assets.empty())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "recipe contains no assets");
    }
    return request;
}

[[nodiscard]] Core::Result<SourceImportCandidate>
finalizeCatalogRecipeSourceImports(SourceImportCandidate candidate,
                                   Core::u32 primarySourceIndex,
                                   const CatalogCookRequest& request)
{
    if (primarySourceIndex >= candidate.sources.size() || !candidate.units.empty())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "catalog recipe source capture state is invalid");
    }

    auto contract = currentCatalogRecipeSourceImportContract(
        candidate.sources[primarySourceIndex].path, request.targetPlatform);
    if (!contract)
    {
        return Core::failure(std::move(contract.error()).withContext(
            "finalizeCatalogRecipeSourceImports", "currentContract"));
    }

    try
    {
        SourceImportCapturedUnit unit{
            .unitId = contract->unitId,
            .importerKind = contract->importerKind,
            .importerVersion = contract->importerVersion,
            .settingsHash = contract->settingsHash,
        };
        unit.inputs.reserve(candidate.sources.size());
        for (Core::u32 sourceIndex = 0; sourceIndex < candidate.sources.size(); ++sourceIndex)
        {
            unit.inputs.push_back(SourceImportCapturedInput{
                .sourceIndex = sourceIndex,
                .flags = sourceIndex == primarySourceIndex ? AssetFormat::SourceImportInputFlags::Primary
                                                           : AssetFormat::SourceImportInputFlags::None,
            });
        }
        unit.outputs.reserve(request.assets.size());
        for (const auto& asset : request.assets)
        {
            unit.outputs.push_back(SourceImportCapturedOutput{
                .assetId = asset.assetId,
                .assetKind = asset.assetKind,
            });
        }
        candidate.targetPlatform = request.targetPlatform;
        candidate.units.push_back(std::move(unit));
        return candidate;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "catalog recipe source result allocation failed");
    }
}

[[nodiscard]] Core::Result<CatalogCookSourceResult>
loadCatalogCookRecipeFileInternal(std::string_view recipeUtf8Path,
                                  const SourceImportCaptureConfig* captureConfig)
{
    if (auto validated = validateRecipeSourcePath(captureConfig, recipeUtf8Path); !validated)
    {
        return Core::failure(std::move(validated.error()).withContext(
            "loadCatalogCookRecipeFileInternal", "validateRecipePath"));
    }
    std::pmr::unsynchronized_pool_resource memory;
    auto bytes = Core::readFile(recipeUtf8Path, Core::ReadFileConfig{.maxBytes = 16ULL * 1024ULL * 1024ULL,
                                                                     .memoryResource = &memory});
    if (!bytes)
    {
        return Core::failure(std::move(bytes.error()).withContext(
            "loadCatalogCookRecipeFileInternal", "readRecipe"));
    }

    SourceImportCandidate sourceImports{};
    std::optional<Core::u32> primarySourceIndex;
    std::optional<RecipeSourceCaptureContext> sourceCapture;
    if (captureConfig != nullptr)
    {
        auto captured = captureSourceImportBytes(sourceImports, *captureConfig, recipeUtf8Path,
                                                 AssetFormat::SourceImportReadExtent::WholeFile,
                                                 *bytes);
        if (!captured)
        {
            return Core::failure(std::move(captured.error()).withContext(
                "loadCatalogCookRecipeFileInternal", "captureRecipe"));
        }
        primarySourceIndex = *captured;
        sourceCapture.emplace(RecipeSourceCaptureContext{sourceImports, *captureConfig});
    }

    try
    {
        std::string text;
        text.resize(bytes->size());
        for (std::size_t index = 0; index < bytes->size(); ++index)
        {
            text[index] = static_cast<char>(std::to_integer<unsigned char>((*bytes)[index]));
        }
        const auto path = Detail::pathFromUtf8Bytes(recipeUtf8Path);
        const auto base = path.parent_path();
        std::string baseUtf8 = ".";
        if (!base.empty())
        {
            const auto generic = base.generic_u8string();
            baseUtf8.assign(generic.begin(), generic.end());
        }
        auto request = parseCatalogCookRecipeInternal(
            text, baseUtf8, sourceCapture ? &*sourceCapture : nullptr);
        if (!request)
        {
            return Core::failure(std::move(request.error()).withContext(
                "loadCatalogCookRecipeFileInternal", "parseRecipe"));
        }
        if (captureConfig == nullptr)
        {
            return CatalogCookSourceResult{.request = std::move(*request)};
        }

        auto finalized = finalizeCatalogRecipeSourceImports(
            std::move(sourceImports), *primarySourceIndex, *request);
        if (!finalized)
        {
            return Core::failure(std::move(finalized.error()).withContext(
                "loadCatalogCookRecipeFileInternal", "finalizeSources"));
        }
        return CatalogCookSourceResult{
            .request = std::move(*request),
            .sourceImports = std::move(*finalized),
        };
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "catalog recipe file load allocation failed");
    }
}

} // namespace

Core::Result<CatalogCookRequest> parseCatalogCookRecipe(std::string_view recipeText,
                                                         std::string_view baseDirectoryUtf8)
{
    return parseCatalogCookRecipeInternal(recipeText, baseDirectoryUtf8, nullptr);
}

Core::Result<CatalogCookRequest> loadCatalogCookRecipeFile(std::string_view recipeUtf8Path)
{
    auto result = loadCatalogCookRecipeFileInternal(recipeUtf8Path, nullptr);
    if (!result)
    {
        return Core::failure(std::move(result.error()));
    }
    return std::move(result->request);
}

Core::Result<CatalogCookSourceResult>
loadCatalogCookRecipeSourceFile(std::string_view recipeUtf8Path,
                                SourceImportCaptureConfig captureConfig)
{
    return loadCatalogCookRecipeFileInternal(recipeUtf8Path, &captureConfig);
}

} // namespace Tina::Asset
