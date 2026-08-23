#include <tina/asset/MediaCook.hpp>

#include "WavDecode.hpp"
#include "stb_image.h"

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/SourceImportProbe.hpp>
#include <tina/asset_format/AudioClipPayload.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/text/Utf8.hpp>

#include <cstddef>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace Tina::Asset {
namespace {

inline constexpr Core::u64 MaxImageSourceFileBytes = 64ULL * 1024ULL * 1024ULL;
inline constexpr Core::u64 MaxWavSourceFileBytes = 32ULL * 1024ULL * 1024ULL;

inline constexpr Core::u8 TextureMediaIdTag = 0x75;
inline constexpr Core::u8 SpriteMediaIdTag = 0x76;
inline constexpr Core::u8 AudioMediaIdTag = 0x77;

// Same path-stable derivation scheme as the glTF importer, seeded with the
// canonical source-root-relative locator so identities survive project moves.
[[nodiscard]] Core::AssetId deriveMediaAssetId(std::string_view seed, Core::u8 tag)
{
    Core::AssetId::Bytes bytes{};
    for (std::size_t index = 0; index < seed.size(); ++index)
    {
        bytes[index % 16] = static_cast<std::byte>(
            static_cast<Core::u8>(bytes[index % 16]) ^ static_cast<Core::u8>(seed[index]) ^ tag);
    }
    bytes[0] = static_cast<std::byte>(tag);
    return *Core::AssetId::fromBytes(bytes);
}

struct MediaSourceCapture final {
    CatalogCookSourceResult result{};
    Core::u32 primarySourceIndex = 0;
    std::pmr::vector<std::byte> sourceBytes{};
};

[[nodiscard]] Core::Result<MediaSourceCapture>
captureMediaPrimarySource(std::string_view sourceUtf8Path,
                          AssetFormat::TargetPlatform targetPlatform,
                          const SourceImportCaptureConfig& captureConfig,
                          Core::u64 maxFileBytes)
{
    if (sourceUtf8Path.empty() || !Core::isStrictUtf8WithoutNul(sourceUtf8Path))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "media source path must be strict UTF-8 without NUL");
    }
    if (targetPlatform == AssetFormat::TargetPlatform::Invalid)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "media cook target platform must be valid");
    }
    MediaSourceCapture capture{};
    capture.result.sourceImports.targetPlatform = targetPlatform;
    auto bytes = Core::readFile(sourceUtf8Path,
                                Core::ReadFileConfig{
                                    .maxBytes = maxFileBytes,
                                    .memoryResource = std::pmr::get_default_resource(),
                                });
    if (!bytes)
    {
        return Core::failure(std::move(bytes.error()).withContext(
            "captureMediaPrimarySource", "readSource"));
    }
    capture.sourceBytes = std::move(*bytes);
    auto sourceIndex = captureSourceImportBytes(capture.result.sourceImports, captureConfig,
                                                sourceUtf8Path,
                                                AssetFormat::SourceImportReadExtent::WholeFile,
                                                capture.sourceBytes);
    if (!sourceIndex)
    {
        return Core::failure(std::move(sourceIndex.error()).withContext(
            "captureMediaPrimarySource", "capturePrimary"));
    }
    capture.primarySourceIndex = *sourceIndex;
    return capture;
}

[[nodiscard]] Core::Status
finalizeMediaSourceImportUnit(CatalogCookSourceResult& result, Core::u32 primarySourceIndex,
                              const SourceImportUnitContract& contract)
try
{
    if (primarySourceIndex >= result.sourceImports.sources.size() ||
        result.request.assets.size() > AssetFormat::SourceImportWire::MaxOutputs)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "media source import contract exceeds current metadata limits");
    }
    SourceImportCapturedUnit unit{
        .unitId = contract.unitId,
        .importerKind = contract.importerKind,
        .importerVersion = contract.importerVersion,
        .settingsHash = contract.settingsHash,
    };
    unit.inputs.reserve(result.sourceImports.sources.size());
    for (Core::u32 sourceIndex = 0; sourceIndex < result.sourceImports.sources.size(); ++sourceIndex)
    {
        unit.inputs.push_back(SourceImportCapturedInput{
            .sourceIndex = sourceIndex,
            .flags = sourceIndex == primarySourceIndex
                         ? AssetFormat::SourceImportInputFlags::Primary
                         : AssetFormat::SourceImportInputFlags::None,
        });
    }
    unit.outputs.reserve(result.request.assets.size());
    for (const auto& asset : result.request.assets)
    {
        unit.outputs.push_back(SourceImportCapturedOutput{
            .assetId = asset.assetId,
            .assetKind = asset.assetKind,
        });
    }
    result.sourceImports.units.push_back(std::move(unit));
    return Core::success();
}
catch (const std::bad_alloc&)
{
    return Core::failure(AssetErrorCode::AllocationFailed,
                         "media source import unit allocation failed");
}

} // namespace

Core::Result<CatalogCookSourceResult>
cookTextureFileToCatalogSourceResult(std::string_view imageUtf8Path,
                                     AssetFormat::TargetPlatform targetPlatform,
                                     SourceImportCaptureConfig captureConfig) noexcept
try
{
    auto capture = captureMediaPrimarySource(imageUtf8Path, targetPlatform, captureConfig,
                                             MaxImageSourceFileBytes);
    if (!capture)
    {
        return Core::failure(std::move(capture.error()).withContext(
            "cookTextureFileToCatalogSourceResult", "primarySource"));
    }
    const auto& encodedBytes = capture->sourceBytes;
    if (encodedBytes.size() > static_cast<Core::u64>((std::numeric_limits<int>::max)()))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "image file too large to decode");
    }
    const auto* encoded = reinterpret_cast<const stbi_uc*>(encodedBytes.data());
    const int encodedSize = static_cast<int>(encodedBytes.size());
    int headerWidth = 0;
    int headerHeight = 0;
    int headerComponents = 0;
    if (stbi_info_from_memory(encoded, encodedSize, &headerWidth, &headerHeight,
                              &headerComponents) == 0 ||
        headerWidth <= 0 || headerHeight <= 0 ||
        headerWidth > static_cast<int>((std::numeric_limits<Core::u16>::max)()) ||
        headerHeight > static_cast<int>((std::numeric_limits<Core::u16>::max)()))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "image is not a decodable Texture2D source or exceeds size limits");
    }
    int width = 0;
    int height = 0;
    int components = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels{
        stbi_load_from_memory(encoded, encodedSize, &width, &height, &components, 4),
        &stbi_image_free};
    if (!pixels || width != headerWidth || height != headerHeight)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "image decode failed");
    }
    const std::size_t pixelBytes =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;

    auto texturePayload = AssetFormat::writeTexture2DPayloadBytes(AssetFormat::Texture2DPayloadDesc{
        .width = static_cast<Core::u16>(width),
        .height = static_cast<Core::u16>(height),
        .pixelFormat = AssetFormat::Texture2DPixelFormat::Rgba8Unorm,
        .pixels = std::span<const std::byte>{
            reinterpret_cast<const std::byte*>(pixels.get()), pixelBytes},
    });
    pixels.reset();
    if (!texturePayload)
    {
        return Core::failure(std::move(texturePayload.error()).withContext(
            "cookTextureFileToCatalogSourceResult", "texturePayload"));
    }

    const std::string_view idSeed =
        capture->result.sourceImports.sources[capture->primarySourceIndex].path;
    const Core::AssetId textureId = deriveMediaAssetId(idSeed, TextureMediaIdTag);
    const Core::AssetId spriteId = deriveMediaAssetId(idSeed, SpriteMediaIdTag);
    auto spritePayload = AssetFormat::writeSpritePayloadBytes(AssetFormat::SpritePayloadDesc{
        .textureId = textureId,
    });
    if (!spritePayload)
    {
        return Core::failure(std::move(spritePayload.error()).withContext(
            "cookTextureFileToCatalogSourceResult", "spritePayload"));
    }

    capture->result.request.targetPlatform = targetPlatform;
    capture->result.request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Texture2D,
        .assetId = textureId,
        .payload = std::move(*texturePayload),
    });
    capture->result.request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Sprite,
        .assetId = spriteId,
        .payload = std::move(*spritePayload),
        .dependencies = {AssetFormat::CookedAssetWriteDependency{
            .assetId = textureId,
            .expectedKind = AssetFormat::AssetKind::Texture2D,
            .flags = AssetFormat::DependencyFlags::Required,
        }},
    });

    auto contract = currentTextureSourceImportContract(idSeed);
    if (!contract)
    {
        return Core::failure(std::move(contract.error()).withContext(
            "cookTextureFileToCatalogSourceResult", "currentContract"));
    }
    if (auto status = finalizeMediaSourceImportUnit(capture->result,
                                                    capture->primarySourceIndex, *contract);
        !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return std::move(capture->result);
}
catch (const std::bad_alloc&)
{
    return Core::failure(AssetErrorCode::AllocationFailed, "texture media cook allocation failed");
}

Core::Result<CatalogCookSourceResult>
cookAudioFileToCatalogSourceResult(std::string_view wavUtf8Path,
                                   AssetFormat::TargetPlatform targetPlatform,
                                   SourceImportCaptureConfig captureConfig) noexcept
try
{
    auto capture = captureMediaPrimarySource(wavUtf8Path, targetPlatform, captureConfig,
                                             MaxWavSourceFileBytes);
    if (!capture)
    {
        return Core::failure(std::move(capture.error()).withContext(
            "cookAudioFileToCatalogSourceResult", "primarySource"));
    }
    std::vector<float> pcm;
    auto clipDesc = Detail::decodePcm16WavToClipDesc(capture->sourceBytes, pcm);
    if (!clipDesc)
    {
        return Core::failure(std::move(clipDesc.error()).withContext(
            "cookAudioFileToCatalogSourceResult", "decodeWav"));
    }
    auto clipPayload = AssetFormat::writeAudioClipPayloadBytes(*clipDesc);
    if (!clipPayload)
    {
        return Core::failure(std::move(clipPayload.error()).withContext(
            "cookAudioFileToCatalogSourceResult", "clipPayload"));
    }

    const std::string_view idSeed =
        capture->result.sourceImports.sources[capture->primarySourceIndex].path;
    capture->result.request.targetPlatform = targetPlatform;
    capture->result.request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::AudioClip,
        .assetId = deriveMediaAssetId(idSeed, AudioMediaIdTag),
        .payload = std::move(*clipPayload),
    });

    auto contract = currentAudioSourceImportContract(idSeed);
    if (!contract)
    {
        return Core::failure(std::move(contract.error()).withContext(
            "cookAudioFileToCatalogSourceResult", "currentContract"));
    }
    if (auto status = finalizeMediaSourceImportUnit(capture->result,
                                                    capture->primarySourceIndex, *contract);
        !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return std::move(capture->result);
}
catch (const std::bad_alloc&)
{
    return Core::failure(AssetErrorCode::AllocationFailed, "audio media cook allocation failed");
}

} // namespace Tina::Asset
