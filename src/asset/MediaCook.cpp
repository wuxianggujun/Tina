#include <tina/asset/MediaCook.hpp>

#include "WavDecode.hpp"
#include "DerivedAssetId.hpp"
#include "stb_image.h"

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/SourceImportProbe.hpp>
#include <tina/asset_format/AudioClipPayload.hpp>
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

[[nodiscard]] Core::AssetId deriveMediaAssetIdUnchecked(std::string_view seed, Core::u8 tag)
{
    const auto kind = tag == Detail::AudioMediaAssetIdTag
                          ? AssetFormat::AssetKind::AudioClip
                          : AssetFormat::AssetKind::Texture2D;
    return Detail::deriveVersionedAssetId(seed, kind, tag, 0U);
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

Core::Result<Core::AssetId>
deriveTextureMediaAssetId(std::string_view normalizedSourcePath) noexcept
{
    if (normalizedSourcePath.empty() ||
        !Core::isStrictUtf8WithoutNul(normalizedSourcePath))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "texture media identity path must be strict UTF-8 without NUL");
    }
    return deriveMediaAssetIdUnchecked(normalizedSourcePath, Detail::TextureMediaAssetIdTag);
}

Core::Result<Core::AssetId>
deriveAudioMediaAssetId(std::string_view normalizedSourcePath) noexcept
{
    if (normalizedSourcePath.empty() ||
        !Core::isStrictUtf8WithoutNul(normalizedSourcePath))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "audio media identity path must be strict UTF-8 without NUL");
    }
    return deriveMediaAssetIdUnchecked(normalizedSourcePath, Detail::AudioMediaAssetIdTag);
}

Core::Result<CatalogCookSourceResult>
cookTextureFileToCatalogSourceResult(std::string_view imageUtf8Path,
                                     AssetFormat::TargetPlatform targetPlatform,
                                     SourceImportCaptureConfig captureConfig,
                                     Core::AssetId stableAssetId) noexcept
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

    auto texturePayload = AssetFormat::writeTexture2DPayloadBytesRgba8(
        static_cast<Core::u16>(width), static_cast<Core::u16>(height),
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(pixels.get()), pixelBytes});
    pixels.reset();
    if (!texturePayload)
    {
        return Core::failure(std::move(texturePayload.error()).withContext(
            "cookTextureFileToCatalogSourceResult", "texturePayload"));
    }

    const std::string_view idSeed =
        capture->result.sourceImports.sources[capture->primarySourceIndex].path;
    auto derivedTextureId = deriveTextureMediaAssetId(idSeed);
    if (!derivedTextureId)
    {
        return Core::failure(std::move(derivedTextureId.error()));
    }
    const Core::AssetId textureId = stableAssetId ? stableAssetId : *derivedTextureId;

    capture->result.request.targetPlatform = targetPlatform;
    capture->result.request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::Texture2D,
        .assetId = textureId,
        .assetTypeVersion = AssetFormat::Texture2DWire::SchemaVersion,
        .payload = std::move(*texturePayload),
    });

    auto contract = currentTextureSourceImportContract(idSeed, stableAssetId);
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
                                   SourceImportCaptureConfig captureConfig,
                                   Core::AssetId stableAssetId) noexcept
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
    auto derivedAudioId = deriveAudioMediaAssetId(idSeed);
    if (!derivedAudioId)
    {
        return Core::failure(std::move(derivedAudioId.error()));
    }
    capture->result.request.targetPlatform = targetPlatform;
    capture->result.request.assets.push_back(CatalogCookAssetSpec{
        .assetKind = AssetFormat::AssetKind::AudioClip,
        .assetId = stableAssetId ? stableAssetId : *derivedAudioId,
        .payload = std::move(*clipPayload),
    });

    auto contract = currentAudioSourceImportContract(idSeed, stableAssetId);
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
