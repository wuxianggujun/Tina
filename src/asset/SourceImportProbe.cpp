#include <tina/asset/SourceImportProbe.hpp>

#include "GltfFileSnapshot.hpp"
#include "Utf8Path.hpp"

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/GltfCook.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <memory_resource>
#include <new>
#include <optional>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

namespace Tina::Asset {
namespace {

inline constexpr Core::u32 CatalogRecipeImporterVersion = 1U;
inline constexpr Core::u32 GltfImporterVersion = 2U;
inline constexpr Core::u32 TextureImporterVersion = 2U;
inline constexpr Core::u32 AudioImporterVersion = 2U;

[[nodiscard]] constexpr std::array<std::byte, 16>
canonicalCatalogRecipeSettings(AssetFormat::TargetPlatform targetPlatform) noexcept
{
    return {
        std::byte{'T'}, std::byte{'I'}, std::byte{'N'}, std::byte{'A'},
        std::byte{'R'}, std::byte{'S'}, std::byte{'E'}, std::byte{'T'},
        std::byte{1}, std::byte{0},
        static_cast<std::byte>(static_cast<Core::u16>(targetPlatform) & 0xFFU),
        static_cast<std::byte>((static_cast<Core::u16>(targetPlatform) >> 8U) & 0xFFU),
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
    };
}

[[nodiscard]] Core::Result<Core::ContentHash> digestGltfCookSettings(const GltfCookIds& ids)
{
    constexpr std::array Domain{
        std::byte{'T'}, std::byte{'I'}, std::byte{'N'}, std::byte{'A'},
        std::byte{'G'}, std::byte{'L'}, std::byte{'T'}, std::byte{'F'},
        std::byte{'C'}, std::byte{'F'}, std::byte{'G'}, std::byte{0},
    };
    constexpr Core::u32 SettingsSchemaVersion = 2;
    constexpr std::size_t AssetIdBytes = Core::AssetId::Bytes{}.size();
    std::array<std::byte, Domain.size() + sizeof(Core::u32) + AssetIdBytes * 3U> canonical{};
    auto cursor = std::copy(Domain.begin(), Domain.end(), canonical.begin());
    for (Core::u32 shift = 0; shift < 32U; shift += 8U)
    {
        *cursor++ = static_cast<std::byte>((SettingsSchemaVersion >> shift) & 0xFFU);
    }
    const auto appendId = [&cursor](Core::AssetId id) {
        const auto bytes = id.bytes();
        cursor = std::copy(bytes.begin(), bytes.end(), cursor);
    };
    appendId(ids.meshId);
    appendId(ids.materialId);
    appendId(ids.prefabId);
    return digestSourceImportSettings(canonical);
}

[[nodiscard]] Core::Result<SourceImportUnitProbeDesc>
makeProbeDesc(std::string_view sourceRootUtf8,
              std::string_view primarySourceUtf8Path,
              SourceImporterKind importerKind,
              AssetFormat::TargetPlatform targetPlatform,
              const GltfCookIds* gltfIds,
              Core::AssetId stableMediaAssetId)
{
    SourceImportCaptureConfig captureConfig{.sourceRootUtf8 = sourceRootUtf8};
    auto normalized = normalizeSourceImportPath(captureConfig, primarySourceUtf8Path);
    if (!normalized)
    {
        return Core::failure(std::move(normalized.error()).withContext(
            "makeSourceImportProbeDesc", "normalizePrimary"));
    }

    Core::Result<SourceImportUnitContract> contract =
        Core::failure(AssetErrorCode::InvalidCatalogConfig,
                      "source import probe importer contract is invalid");
    if (importerKind == SourceImporterKind::CatalogRecipe)
    {
        contract = currentCatalogRecipeSourceImportContract(*normalized, targetPlatform);
    } else if (importerKind == SourceImporterKind::Gltf && gltfIds != nullptr)
    {
        contract = currentGltfSourceImportContract(*normalized, *gltfIds);
    } else if (importerKind == SourceImporterKind::Texture)
    {
        contract = currentTextureSourceImportContract(*normalized, stableMediaAssetId);
    } else if (importerKind == SourceImporterKind::Audio)
    {
        contract = currentAudioSourceImportContract(*normalized, stableMediaAssetId);
    }
    if (!contract)
    {
        return Core::failure(std::move(contract.error()).withContext(
            "makeSourceImportProbeDesc", "currentContract"));
    }

    try
    {
        return SourceImportUnitProbeDesc{
            .sourceRootUtf8 = std::string(sourceRootUtf8),
            .primarySourceUtf8Path = std::string(primarySourceUtf8Path),
            .normalizedPrimarySourcePath = std::move(*normalized),
            .expected = *contract,
        };
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "source import probe description allocation failed");
    }
}

[[nodiscard]] SourceImportProbeResult dirty(SourceImportProbeReason reason) noexcept
{
    return SourceImportProbeResult{
        .state = SourceImportProbeState::Dirty,
        .reason = reason,
    };
}

[[nodiscard]] SourceImportProbeResult noBaseline() noexcept
{
    return SourceImportProbeResult{
        .state = SourceImportProbeState::NoBaseline,
        .reason = SourceImportProbeReason::StateNotFound,
    };
}

[[nodiscard]] bool validCatalogRevision(
    AssetFormat::SourceImportManifestRevision revision) noexcept
{
    return revision.manifestDigest && revision.manifestBytes != 0U;
}

[[nodiscard]] Core::Status validateProbeDesc(const SourceImportUnitProbeDesc& desc)
{
    if (desc.sourceRootUtf8.empty() || desc.primarySourceUtf8Path.empty() ||
        desc.normalizedPrimarySourcePath.empty() || !desc.expected.unitId ||
        desc.expected.importerVersion == 0U || !desc.expected.settingsHash ||
        !Core::isStrictUtf8WithoutNul(desc.sourceRootUtf8) ||
        !Core::isStrictUtf8WithoutNul(desc.primarySourceUtf8Path) ||
        !Core::isStrictUtf8WithoutNul(desc.normalizedPrimarySourcePath))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import probe description is invalid");
    }
    if (desc.expected.importerKind != SourceImporterKind::CatalogRecipe &&
        desc.expected.importerKind != SourceImporterKind::Gltf &&
        desc.expected.importerKind != SourceImporterKind::Texture &&
        desc.expected.importerKind != SourceImporterKind::Audio)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import probe importer is unsupported");
    }

    auto normalized = normalizeSourceImportPath(
        SourceImportCaptureConfig{.sourceRootUtf8 = desc.sourceRootUtf8},
        desc.primarySourceUtf8Path);
    if (!normalized)
    {
        return Core::failure(std::move(normalized.error()).withContext(
            "validateSourceImportProbeDesc", "normalizePrimary"));
    }
    if (*normalized != desc.normalizedPrimarySourcePath)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import probe primary locator is inconsistent");
    }
    return Core::success();
}

[[nodiscard]] std::optional<Core::u32>
findUnitIndex(const AssetFormat::SourceImportMetadataView& baseline,
              AssetFormat::SourceImportUnitId unitId) noexcept
{
    Core::u32 first = 0;
    Core::u32 count = baseline.header().unitCount;
    while (count > 0U)
    {
        const Core::u32 step = count / 2U;
        const Core::u32 index = first + step;
        const auto unit = baseline.unit(index);
        if (!unit)
        {
            return std::nullopt;
        }
        if (unit->unitId < unitId)
        {
            first = index + 1U;
            count -= step + 1U;
        } else
        {
            count = step;
        }
    }
    if (first >= baseline.header().unitCount)
    {
        return std::nullopt;
    }
    const auto unit = baseline.unit(first);
    return unit && unit->unitId == unitId ? std::optional<Core::u32>{first} : std::nullopt;
}

[[nodiscard]] std::filesystem::path snapshotContainmentPath(std::filesystem::path path)
{
#if defined(_WIN32)
    const std::wstring& native = path.native();
    if (native.starts_with(L"\\\\?\\"))
    {
        return path;
    }
    if (native.starts_with(L"\\\\"))
    {
        return std::filesystem::path{L"\\\\?\\UNC\\" + native.substr(2)};
    }
    return std::filesystem::path{L"\\\\?\\" + native};
#else
    return path;
#endif
}

[[nodiscard]] Core::Result<std::filesystem::path>
canonicalSourceRoot(std::string_view sourceRootUtf8)
{
    std::error_code errorCode;
    auto root = std::filesystem::canonical(Detail::pathFromUtf8Bytes(sourceRootUtf8), errorCode);
    if (errorCode || !std::filesystem::is_directory(root, errorCode) || errorCode)
    {
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                             "failed to resolve source import authoring root");
    }
    return snapshotContainmentPath(std::move(root));
}

struct FingerprintProbe final {
    std::optional<SourceImportProbeReason> dirtyReason{};
    GltfDetail::FileSnapshot snapshot{};
};

[[nodiscard]] Core::Result<FingerprintProbe>
probeFingerprint(const AssetFormat::SourceImportSource& baselineSource,
                 const std::filesystem::path& requestedPath,
                 const std::filesystem::path& containmentRoot)
{
    const bool prefix = baselineSource.readExtent == AssetFormat::SourceImportReadExtent::Prefix;
    const Core::u64 maxFileBytes = prefix ? (std::numeric_limits<Core::u64>::max)()
                                          : AssetFormat::SourceImportWire::MaxFileBytes;
    const Core::u64 requestedBytes = prefix ? baselineSource.fileBytes : 0U;
    auto snapshot = GltfDetail::readFileSnapshot(requestedPath, &containmentRoot,
                                                 maxFileBytes, requestedBytes, prefix);
    if (!snapshot)
    {
        return Core::failure(std::move(snapshot.error()).withContext(
            "probeSourceImportUnit", "readSourceSnapshot"));
    }
    if (snapshot->fileSize < baselineSource.fileBytes ||
        (!prefix && snapshot->fileSize != baselineSource.fileBytes))
    {
        return FingerprintProbe{
            .dirtyReason = SourceImportProbeReason::SourceSizeChanged,
            .snapshot = std::move(*snapshot),
        };
    }
    auto digest = Core::digestContentHashV1(snapshot->bytes);
    if (!digest)
    {
        return Core::failure(std::move(digest.error()).withContext(
            "probeSourceImportUnit", "digestSourceSnapshot"));
    }
    return FingerprintProbe{
        .dirtyReason = *digest == baselineSource.contentHash
                           ? std::nullopt
                           : std::optional{SourceImportProbeReason::SourceContentChanged},
        .snapshot = std::move(*snapshot),
    };
}

template <typename Probe>
[[nodiscard]] Core::Result<SourceImportProbeResult>
withSourceImportState(std::string_view stateUtf8Path, Probe&& probe)
{
    if (stateUtf8Path.empty() || !Core::isStrictUtf8WithoutNul(stateUtf8Path))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import state path is invalid");
    }
    auto stateBytes = Core::readFile(
        stateUtf8Path,
        Core::ReadFileConfig{
            .maxBytes = Core::MaxReadFileBytes,
            .memoryResource = std::pmr::new_delete_resource(),
        });
    if (!stateBytes)
    {
        if (stateBytes.error().code == Core::CoreErrorCode::NotFound)
        {
            return noBaseline();
        }
        return Core::failure(std::move(stateBytes.error()).withContext(
            "probeSourceImportState", "readState"));
    }
    auto baseline = AssetFormat::parseSourceImportMetadataView(*stateBytes);
    if (!baseline)
    {
        if (baseline.error().code == AssetFormat::AssetFormatErrorCode::UnsupportedSchema)
        {
            return dirty(SourceImportProbeReason::StateSchemaChanged);
        }
        return Core::failure(std::move(baseline.error()).withContext(
            "probeSourceImportState", "parseState"));
    }
    return std::forward<Probe>(probe)(*baseline);
}

} // namespace

Core::Result<SourceImportUnitContract> currentCatalogRecipeSourceImportContract(
    std::string_view normalizedPrimarySourcePath,
    AssetFormat::TargetPlatform targetPlatform)
{
    if (targetPlatform < AssetFormat::TargetPlatform::Any ||
        targetPlatform > AssetFormat::TargetPlatform::LinuxX64)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "Catalog recipe source import target platform is invalid");
    }
    auto unitId = deriveSourceImportUnitId(SourceImporterKind::CatalogRecipe,
                                           normalizedPrimarySourcePath);
    auto settingsHash = digestSourceImportSettings(canonicalCatalogRecipeSettings(targetPlatform));
    if (!unitId)
    {
        return Core::failure(std::move(unitId.error()));
    }
    if (!settingsHash)
    {
        return Core::failure(std::move(settingsHash.error()));
    }
    return SourceImportUnitContract{
        .unitId = *unitId,
        .importerKind = SourceImporterKind::CatalogRecipe,
        .importerVersion = CatalogRecipeImporterVersion,
        .settingsHash = *settingsHash,
    };
}

Core::Result<SourceImportUnitContract>
currentGltfSourceImportContract(std::string_view normalizedPrimarySourcePath,
                                const GltfCookIds& ids)
{
    auto unitId = deriveSourceImportUnitId(SourceImporterKind::Gltf,
                                           normalizedPrimarySourcePath);
    auto settingsHash = digestGltfCookSettings(ids);
    if (!unitId)
    {
        return Core::failure(std::move(unitId.error()));
    }
    if (!settingsHash)
    {
        return Core::failure(std::move(settingsHash.error()));
    }
    return SourceImportUnitContract{
        .unitId = *unitId,
        .importerKind = SourceImporterKind::Gltf,
        .importerVersion = GltfImporterVersion,
        .settingsHash = *settingsHash,
    };
}

Core::Result<SourceImportUnitProbeDesc> makeCatalogRecipeSourceImportProbeDesc(
    std::string_view sourceRootUtf8,
    std::string_view primarySourceUtf8Path,
    AssetFormat::TargetPlatform targetPlatform)
{
    return makeProbeDesc(sourceRootUtf8, primarySourceUtf8Path,
                         SourceImporterKind::CatalogRecipe, targetPlatform, nullptr, {});
}

Core::Result<SourceImportUnitProbeDesc>
makeGltfSourceImportProbeDesc(std::string_view sourceRootUtf8,
                              std::string_view primarySourceUtf8Path,
                              const GltfCookIds& ids)
{
    return makeProbeDesc(sourceRootUtf8, primarySourceUtf8Path,
                         SourceImporterKind::Gltf,
                         AssetFormat::TargetPlatform::Invalid, &ids, {});
}

namespace {

[[nodiscard]] Core::Result<SourceImportUnitContract>
currentMediaSourceImportContract(SourceImporterKind importerKind,
                                 Core::u32 importerVersion,
                                 std::string_view canonicalSettingsTag,
                                 std::string_view normalizedPrimarySourcePath,
                                 Core::AssetId stableAssetId)
{
    auto unitId = deriveSourceImportUnitId(importerKind, normalizedPrimarySourcePath);
    Core::Result<Core::ContentHash> settingsHash = digestSourceImportSettings(
        std::as_bytes(std::span{canonicalSettingsTag.data(), canonicalSettingsTag.size()}));
    if (stableAssetId)
    {
        std::vector<std::byte> canonical;
        canonical.reserve(canonicalSettingsTag.size() + 1U +
                          Core::AssetId::Bytes{}.size());
        const auto tagBytes = std::as_bytes(std::span{
            canonicalSettingsTag.data(), canonicalSettingsTag.size()});
        canonical.insert(canonical.end(), tagBytes.begin(), tagBytes.end());
        canonical.push_back(std::byte{1});
        const auto idBytes = stableAssetId.bytes();
        canonical.insert(canonical.end(), idBytes.begin(), idBytes.end());
        settingsHash = digestSourceImportSettings(canonical);
    }
    if (!unitId)
    {
        return Core::failure(std::move(unitId.error()));
    }
    if (!settingsHash)
    {
        return Core::failure(std::move(settingsHash.error()));
    }
    return SourceImportUnitContract{
        .unitId = *unitId,
        .importerKind = importerKind,
        .importerVersion = importerVersion,
        .settingsHash = *settingsHash,
    };
}

} // namespace

Core::Result<SourceImportUnitContract>
currentTextureSourceImportContract(std::string_view normalizedPrimarySourcePath,
                                   Core::AssetId stableAssetId)
{
    return currentMediaSourceImportContract(SourceImporterKind::Texture,
                                            TextureImporterVersion,
                                            "tina.import.texture.v2",
                                            normalizedPrimarySourcePath,
                                            stableAssetId);
}

Core::Result<SourceImportUnitContract>
currentAudioSourceImportContract(std::string_view normalizedPrimarySourcePath,
                                 Core::AssetId stableAssetId)
{
    return currentMediaSourceImportContract(SourceImporterKind::Audio,
                                            AudioImporterVersion,
                                           "tina.import.audio.v2",
                                            normalizedPrimarySourcePath,
                                            stableAssetId);
}

Core::Result<SourceImportUnitProbeDesc>
makeTextureSourceImportProbeDesc(std::string_view sourceRootUtf8,
                                 std::string_view primarySourceUtf8Path,
                                 Core::AssetId stableAssetId)
{
    return makeProbeDesc(sourceRootUtf8, primarySourceUtf8Path,
                         SourceImporterKind::Texture,
                         AssetFormat::TargetPlatform::Invalid, nullptr,
                         stableAssetId);
}

Core::Result<SourceImportUnitProbeDesc>
makeAudioSourceImportProbeDesc(std::string_view sourceRootUtf8,
                               std::string_view primarySourceUtf8Path,
                               Core::AssetId stableAssetId)
{
    return makeProbeDesc(sourceRootUtf8, primarySourceUtf8Path,
                         SourceImporterKind::Audio,
                         AssetFormat::TargetPlatform::Invalid, nullptr,
                         stableAssetId);
}

Core::Result<SourceImportProbeResult>
probeSourceImportUnit(const AssetFormat::SourceImportMetadataView& baseline,
                      AssetFormat::SourceImportManifestRevision currentCatalogRevision,
                      const SourceImportUnitProbeDesc& desc)
{
    if (!baseline || !validCatalogRevision(currentCatalogRevision))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import probe baseline or Catalog revision is invalid");
    }
    if (const auto status = validateProbeDesc(desc); !status)
    {
        return Core::failure(status.error());
    }
    if (baseline.header().manifestRevision != currentCatalogRevision)
    {
        return dirty(SourceImportProbeReason::CatalogRevisionChanged);
    }
    const auto unitIndex = findUnitIndex(baseline, desc.expected.unitId);
    if (!unitIndex)
    {
        return dirty(SourceImportProbeReason::UnitNotFound);
    }
    const auto unit = baseline.unit(*unitIndex);
    if (!unit)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import probe baseline unit is missing");
    }
    if (unit->importerKind != static_cast<Core::u32>(desc.expected.importerKind))
    {
        return dirty(SourceImportProbeReason::ImporterKindChanged);
    }
    if (unit->importerVersion != desc.expected.importerVersion)
    {
        return dirty(SourceImportProbeReason::ImporterVersionChanged);
    }
    if (unit->settingsHash != desc.expected.settingsHash)
    {
        return dirty(SourceImportProbeReason::SettingsChanged);
    }

    std::optional<Core::u32> primarySourceIndex;
    for (Core::u32 inputIndex = 0; inputIndex < unit->inputCount; ++inputIndex)
    {
        const auto input = baseline.unitInputForUnit(*unitIndex, inputIndex);
        if (!input)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "source import probe baseline input is missing");
        }
        if (AssetFormat::hasSourceImportInputFlag(
                input->flags, AssetFormat::SourceImportInputFlags::Primary))
        {
            primarySourceIndex = input->sourceIndex;
            break;
        }
    }
    if (!primarySourceIndex)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import probe baseline has no primary input");
    }
    const auto primaryPath = baseline.sourcePath(*primarySourceIndex);
    const auto primarySource = baseline.source(*primarySourceIndex);
    if (!primaryPath || !primarySource)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import probe primary source is missing");
    }
    if (*primaryPath != desc.normalizedPrimarySourcePath)
    {
        return dirty(SourceImportProbeReason::PrimarySourceChanged);
    }

    try
    {
        auto sourceRoot = canonicalSourceRoot(desc.sourceRootUtf8);
        if (!sourceRoot)
        {
            return Core::failure(std::move(sourceRoot.error()).withContext(
                "probeSourceImportUnit", "sourceRoot"));
        }
        const auto requestedPrimary = std::filesystem::absolute(
            Detail::pathFromUtf8Bytes(desc.primarySourceUtf8Path)).lexically_normal();
        auto primaryProbe = probeFingerprint(*primarySource, requestedPrimary, *sourceRoot);
        if (!primaryProbe)
        {
            return Core::failure(std::move(primaryProbe.error()));
        }
        if (primaryProbe->dirtyReason)
        {
            return dirty(*primaryProbe->dirtyReason);
        }

        const std::filesystem::path secondaryRoot =
            desc.expected.importerKind == SourceImporterKind::Gltf
                ? primaryProbe->snapshot.finalPath.parent_path()
                : *sourceRoot;
        const std::filesystem::path requestedRoot = std::filesystem::absolute(
            Detail::pathFromUtf8Bytes(desc.sourceRootUtf8)).lexically_normal();
        for (Core::u32 inputIndex = 0; inputIndex < unit->inputCount; ++inputIndex)
        {
            const auto input = baseline.unitInputForUnit(*unitIndex, inputIndex);
            if (!input || input->sourceIndex == *primarySourceIndex)
            {
                continue;
            }
            const auto source = baseline.source(input->sourceIndex);
            const auto path = baseline.sourcePath(input->sourceIndex);
            if (!source || !path)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "source import probe secondary source is missing");
            }
            const auto requestedPath =
                (requestedRoot / Detail::pathFromUtf8Bytes(*path)).lexically_normal();
            auto sourceProbe = probeFingerprint(*source, requestedPath, secondaryRoot);
            if (!sourceProbe)
            {
                return Core::failure(std::move(sourceProbe.error()));
            }
            if (sourceProbe->dirtyReason)
            {
                return dirty(*sourceProbe->dirtyReason);
            }
        }
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "source import probe allocation failed");
    } catch (const std::filesystem::filesystem_error&)
    {
        return Core::failure(AssetErrorCode::CatalogFileLoadFailed,
                             "source import probe filesystem operation failed");
    }

    return SourceImportProbeResult{
        .state = SourceImportProbeState::Clean,
        .reason = SourceImportProbeReason::None,
        .cleanUnitCount = 1U,
        .cleanObjectCount = unit->outputCount,
    };
}

Core::Result<SourceImportBatchProbeResult>
probeSourceImportUnits(const AssetFormat::SourceImportMetadataView& baseline,
                       AssetFormat::SourceImportManifestRevision currentCatalogRevision,
                       std::span<const SourceImportUnitProbeDesc> descs)
{
    if (!baseline || !validCatalogRevision(currentCatalogRevision))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import batch probe input is invalid");
    }

    try
    {
        std::vector<AssetFormat::SourceImportUnitId> expectedIds;
        expectedIds.reserve(descs.size());
        for (const auto& desc : descs)
        {
            if (const auto status = validateProbeDesc(desc); !status)
            {
                return Core::failure(status.error());
            }
            expectedIds.push_back(desc.expected.unitId);
        }
        std::sort(expectedIds.begin(), expectedIds.end());
        if (std::adjacent_find(expectedIds.begin(), expectedIds.end()) != expectedIds.end())
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "source import batch contains duplicate unit ids");
        }

        SourceImportBatchProbeResult result{};
        result.units.reserve(descs.size());
        if (baseline.header().manifestRevision != currentCatalogRevision)
        {
            result.units.assign(descs.size(), dirty(SourceImportProbeReason::CatalogRevisionChanged));
            result.dirtyUnitCount = static_cast<Core::u32>(descs.size());
            return result;
        }

        for (Core::u32 unitIndex = 0; unitIndex < baseline.header().unitCount; ++unitIndex)
        {
            const auto unit = baseline.unit(unitIndex);
            if (!unit)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "source import batch baseline unit is missing");
            }
            if (!std::binary_search(expectedIds.begin(), expectedIds.end(), unit->unitId))
            {
                ++result.removedUnitCount;
            }
        }

        for (const auto& desc : descs)
        {
            auto unit = probeSourceImportUnit(baseline, currentCatalogRevision, desc);
            if (!unit)
            {
                return Core::failure(std::move(unit.error()));
            }
            if (unit->state == SourceImportProbeState::Clean)
            {
                ++result.cleanUnitCount;
                result.cleanObjectCount += unit->cleanObjectCount;
            }
            else
            {
                ++result.dirtyUnitCount;
            }
            result.units.push_back(*unit);
        }
        return result;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "source import batch probe allocation failed");
    }
}

Core::Result<SourceImportProbeResult>
probeSourceImportState(std::string_view stateUtf8Path,
                       AssetFormat::SourceImportManifestRevision currentCatalogRevision,
                       const SourceImportUnitProbeDesc& desc)
{
    if (!validCatalogRevision(currentCatalogRevision))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import probe Catalog revision is invalid");
    }
    if (const auto status = validateProbeDesc(desc); !status)
    {
        return Core::failure(status.error());
    }
    return withSourceImportState(stateUtf8Path, [&](const auto& baseline) -> Core::Result<SourceImportProbeResult> {
        if (baseline.header().unitCount != 1U)
        {
            return dirty(SourceImportProbeReason::UnitSetChanged);
        }
        const std::array descriptions{desc};
        auto batch = probeSourceImportUnits(baseline, currentCatalogRevision, descriptions);
        if (!batch)
        {
            return Core::failure(std::move(batch.error()));
        }
        if (batch->removedUnitCount != 0U)
        {
            return dirty(SourceImportProbeReason::UnitSetChanged);
        }
        return batch->units.front();
    });
}

Core::Result<SourceImportProbeResult> probeCatalogRecipeSourceImportState(
    std::string_view stateUtf8Path,
    AssetFormat::SourceImportManifestRevision currentCatalogRevision,
    std::string_view sourceRootUtf8,
    std::string_view primarySourceUtf8Path)
{
    if (!validCatalogRevision(currentCatalogRevision))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import probe Catalog revision is invalid");
    }
    return withSourceImportState(stateUtf8Path, [&](const auto& baseline) -> Core::Result<SourceImportProbeResult> {
        if (baseline.header().unitCount != 1U)
        {
            return dirty(SourceImportProbeReason::UnitSetChanged);
        }
        auto desc = makeCatalogRecipeSourceImportProbeDesc(
            sourceRootUtf8, primarySourceUtf8Path, baseline.header().targetPlatform);
        if (!desc)
        {
            return Core::failure(std::move(desc.error()));
        }
        const std::array descriptions{*desc};
        auto batch = probeSourceImportUnits(baseline, currentCatalogRevision, descriptions);
        if (!batch)
        {
            return Core::failure(std::move(batch.error()));
        }
        if (batch->removedUnitCount != 0U)
        {
            return dirty(SourceImportProbeReason::UnitSetChanged);
        }
        return batch->units.front();
    });
}

Core::Result<SourceImportProbeResult>
probeGltfSourceImportState(std::string_view stateUtf8Path,
                           AssetFormat::SourceImportManifestRevision currentCatalogRevision,
                           std::string_view sourceRootUtf8,
                           std::string_view primarySourceUtf8Path,
                           const GltfCookIds& ids)
{
    auto desc = makeGltfSourceImportProbeDesc(sourceRootUtf8, primarySourceUtf8Path, ids);
    if (!desc)
    {
        return Core::failure(std::move(desc.error()));
    }
    return probeSourceImportState(stateUtf8Path, currentCatalogRevision, *desc);
}

Core::Result<SourceImportProbeResult>
probeGltfSourceImportState(std::string_view stateUtf8Path,
                           AssetFormat::SourceImportManifestRevision currentCatalogRevision,
                           std::string_view sourceRootUtf8,
                           std::string_view primarySourceUtf8Path)
{
    return probeGltfSourceImportState(stateUtf8Path, currentCatalogRevision,
                                      sourceRootUtf8, primarySourceUtf8Path, GltfCookIds{});
}

} // namespace Tina::Asset
