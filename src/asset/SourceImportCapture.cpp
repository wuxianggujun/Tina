#include <tina/asset/SourceImportCapture.hpp>

#include "Utf8Path.hpp"

#include <tina/asset/AssetErrors.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/io/WriteFile.hpp>
#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <new>
#include <numeric>
#include <utility>

namespace Tina::Asset {
namespace {

[[nodiscard]] int comparePathBytes(std::string_view left, std::string_view right) noexcept
{
    const auto sharedBytes = (std::min)(left.size(), right.size());
    for (Core::usize index = 0; index < sharedBytes; ++index)
    {
        const auto leftByte = static_cast<unsigned char>(left[index]);
        const auto rightByte = static_cast<unsigned char>(right[index]);
        if (leftByte != rightByte)
        {
            return leftByte < rightByte ? -1 : 1;
        }
    }
    if (left.size() == right.size())
    {
        return 0;
    }
    return left.size() < right.size() ? -1 : 1;
}

[[nodiscard]] bool escapesRoot(const std::filesystem::path& relative) noexcept
{
    if (relative.empty() || relative.is_absolute() || relative.has_root_path())
    {
        return true;
    }
    for (const auto& component : relative)
    {
        if (component == "..")
        {
            return true;
        }
    }
    return false;
}

struct CanonicalUnit final {
    AssetFormat::SourceImportUnitId unitId{};
    Core::u32 importerKind = 0;
    Core::u32 importerVersion = 0;
    Core::ContentHash settingsHash{};
    std::vector<AssetFormat::SourceImportMetadataWriteInput> inputs{};
    std::vector<AssetFormat::SourceImportMetadataWriteOutput> outputs{};
};

} // namespace

Core::Result<std::string> normalizeSourceImportPath(const SourceImportCaptureConfig& config,
                                                    std::string_view sourceUtf8Path)
{
    if (config.sourceRootUtf8.empty() || sourceUtf8Path.empty() ||
        !Core::isStrictUtf8WithoutNul(config.sourceRootUtf8) ||
        !Core::isStrictUtf8WithoutNul(sourceUtf8Path))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import root and source path must be strict UTF-8 without NUL");
    }

    try
    {
        const auto root = std::filesystem::absolute(Detail::pathFromUtf8Bytes(config.sourceRootUtf8))
                              .lexically_normal();
        const auto source = std::filesystem::absolute(Detail::pathFromUtf8Bytes(sourceUtf8Path))
                                .lexically_normal();
        const auto relative = source.lexically_relative(root);
        if (escapesRoot(relative))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "source import path is outside the configured authoring root");
        }
        const auto generic = relative.generic_u8string();
        std::string normalized(generic.begin(), generic.end());
        if (normalized.empty() || normalized.size() > AssetFormat::SourceImportWire::MaxPathBytes)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "source import path exceeds the current metadata limit");
        }
        return normalized;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "source import path normalization allocation failed");
    } catch (const std::filesystem::filesystem_error&)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import path normalization failed");
    }
}

Core::Result<Core::u32> captureSourceImportBytes(SourceImportCandidate& candidate,
                                                const SourceImportCaptureConfig& config,
                                                std::string_view sourceUtf8Path,
                                                std::span<const std::byte> consumedBytes)
{
    if (config.maxSources == 0 || config.maxSources > AssetFormat::SourceImportWire::MaxSources)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import capture limit is invalid");
    }
    auto path = normalizeSourceImportPath(config, sourceUtf8Path);
    if (!path)
    {
        return Core::failure(std::move(path.error()).withContext("captureSourceImportBytes", "path"));
    }
    auto digest = Core::digestContentHashV1(consumedBytes);
    if (!digest)
    {
        return Core::failure(std::move(digest.error()).withContext("captureSourceImportBytes", "digest"));
    }
    if (consumedBytes.size() > (std::numeric_limits<Core::u64>::max)())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import consumed byte count overflows u64");
    }

    for (Core::u32 index = 0; index < candidate.sources.size(); ++index)
    {
        const auto& existing = candidate.sources[index];
        if (existing.path != *path)
        {
            continue;
        }
        if (existing.contentHash != *digest || existing.fileBytes != consumedBytes.size())
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "one source import path produced inconsistent consumed bytes");
        }
        return index;
    }
    if (candidate.sources.size() >= config.maxSources)
    {
        return Core::failure(AssetErrorCode::SourceImportCaptureCapacityExceeded,
                             "source import capture capacity exceeded");
    }

    try
    {
        candidate.sources.push_back(SourceImportCapturedSource{
            .path = std::move(*path),
            .contentHash = *digest,
            .fileBytes = static_cast<Core::u64>(consumedBytes.size()),
        });
        return static_cast<Core::u32>(candidate.sources.size() - 1U);
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "source import capture allocation failed");
    }
}

Core::Result<AssetFormat::SourceImportUnitId>
deriveSourceImportUnitId(SourceImporterKind importerKind, std::string_view primarySourcePath)
{
    if ((importerKind != SourceImporterKind::CatalogRecipe && importerKind != SourceImporterKind::Gltf) ||
        primarySourcePath.empty() || !Core::isStrictUtf8WithoutNul(primarySourcePath))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import unit identity input is invalid");
    }
    try
    {
        constexpr std::array Domain{
            std::byte{'T'}, std::byte{'I'}, std::byte{'N'}, std::byte{'A'},
            std::byte{'I'}, std::byte{'U'}, std::byte{'N'}, std::byte{'I'}, std::byte{'T'}, std::byte{0},
        };
        std::vector<std::byte> identity;
        identity.reserve(Domain.size() + sizeof(Core::u32) + primarySourcePath.size());
        identity.insert(identity.end(), Domain.begin(), Domain.end());
        const auto kind = static_cast<Core::u32>(importerKind);
        for (Core::u32 shift = 0; shift < 32U; shift += 8U)
        {
            identity.push_back(static_cast<std::byte>((kind >> shift) & 0xFFU));
        }
        const auto pathBytes = std::as_bytes(std::span(primarySourcePath.data(), primarySourcePath.size()));
        identity.insert(identity.end(), pathBytes.begin(), pathBytes.end());
        auto digest = Core::digestContentHashV1(identity);
        if (!digest)
        {
            return Core::failure(std::move(digest.error()));
        }
        const auto unitId = AssetFormat::SourceImportUnitId::fromBytes(digest->bytes());
        if (!unitId)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "source import unit identity digest is zero");
        }
        return *unitId;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "source import unit identity allocation failed");
    }
}

Core::Result<Core::ContentHash>
digestSourceImportSettings(std::span<const std::byte> canonicalSettingsBytes)
{
    return Core::digestContentHashV1(canonicalSettingsBytes);
}

Core::Result<std::vector<std::byte>>
writeSourceImportCandidateBytes(const SourceImportCandidate& candidate,
                                AssetFormat::SourceImportManifestRevision manifestRevision)
{
    if (candidate.sources.empty() || candidate.units.empty())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import candidate requires sources and units");
    }
    if (candidate.sources.size() > AssetFormat::SourceImportWire::MaxSources ||
        candidate.units.size() > AssetFormat::SourceImportWire::MaxUnits)
    {
        return Core::failure(AssetErrorCode::SourceImportCaptureCapacityExceeded,
                             "source import candidate exceeds current metadata limits");
    }

    try
    {
        std::vector<Core::u32> sourceOrder(candidate.sources.size());
        std::iota(sourceOrder.begin(), sourceOrder.end(), 0U);
        std::sort(sourceOrder.begin(), sourceOrder.end(), [&](Core::u32 left, Core::u32 right) {
            return comparePathBytes(candidate.sources[left].path, candidate.sources[right].path) < 0;
        });

        std::vector<Core::u32> sourceRemap(candidate.sources.size());
        std::vector<AssetFormat::SourceImportMetadataWriteSource> sources;
        sources.reserve(candidate.sources.size());
        for (Core::usize canonicalIndex = 0; canonicalIndex < sourceOrder.size(); ++canonicalIndex)
        {
            const Core::u32 oldIndex = sourceOrder[canonicalIndex];
            const auto& source = candidate.sources[oldIndex];
            if (canonicalIndex > 0 &&
                comparePathBytes(candidate.sources[sourceOrder[canonicalIndex - 1U]].path, source.path) == 0)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "source import candidate contains duplicate source paths");
            }
            sourceRemap[oldIndex] = static_cast<Core::u32>(canonicalIndex);
            sources.push_back(AssetFormat::SourceImportMetadataWriteSource{
                .path = source.path,
                .contentHash = source.contentHash,
                .fileBytes = source.fileBytes,
            });
        }

        std::vector<CanonicalUnit> units;
        units.reserve(candidate.units.size());
        for (const auto& captured : candidate.units)
        {
            CanonicalUnit unit{
                .unitId = captured.unitId,
                .importerKind = static_cast<Core::u32>(captured.importerKind),
                .importerVersion = captured.importerVersion,
                .settingsHash = captured.settingsHash,
            };
            unit.inputs.reserve(captured.inputs.size());
            for (const auto& input : captured.inputs)
            {
                if (input.sourceIndex >= sourceRemap.size())
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "source import input index is out of range");
                }
                unit.inputs.push_back(AssetFormat::SourceImportMetadataWriteInput{
                    .sourceIndex = sourceRemap[input.sourceIndex],
                    .flags = input.flags,
                });
            }
            std::sort(unit.inputs.begin(), unit.inputs.end(), [](const auto& left, const auto& right) {
                return left.sourceIndex < right.sourceIndex;
            });
            if (std::adjacent_find(unit.inputs.begin(), unit.inputs.end(), [](const auto& left, const auto& right) {
                    return left.sourceIndex == right.sourceIndex;
                }) != unit.inputs.end())
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "source import unit contains duplicate inputs");
            }

            unit.outputs.reserve(captured.outputs.size());
            for (const auto& output : captured.outputs)
            {
                unit.outputs.push_back(AssetFormat::SourceImportMetadataWriteOutput{
                    .assetId = output.assetId,
                    .assetKind = output.assetKind,
                });
            }
            std::sort(unit.outputs.begin(), unit.outputs.end(), [](const auto& left, const auto& right) {
                return left.assetId < right.assetId;
            });
            if (std::adjacent_find(unit.outputs.begin(), unit.outputs.end(), [](const auto& left, const auto& right) {
                    return left.assetId == right.assetId;
                }) != unit.outputs.end())
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "source import unit contains duplicate outputs");
            }
            units.push_back(std::move(unit));
        }
        std::sort(units.begin(), units.end(), [](const CanonicalUnit& left, const CanonicalUnit& right) {
            return left.unitId < right.unitId;
        });
        if (std::adjacent_find(units.begin(), units.end(), [](const CanonicalUnit& left, const CanonicalUnit& right) {
                return left.unitId == right.unitId;
            }) != units.end())
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "source import candidate contains duplicate unit ids");
        }

        std::vector<AssetFormat::SourceImportMetadataWriteUnit> writeUnits;
        writeUnits.reserve(units.size());
        for (const auto& unit : units)
        {
            writeUnits.push_back(AssetFormat::SourceImportMetadataWriteUnit{
                .unitId = unit.unitId,
                .importerKind = unit.importerKind,
                .importerVersion = unit.importerVersion,
                .settingsHash = unit.settingsHash,
                .inputs = unit.inputs,
                .outputs = unit.outputs,
            });
        }
        return AssetFormat::writeSourceImportMetadataBytes(AssetFormat::SourceImportMetadataWriteDesc{
            .targetPlatform = candidate.targetPlatform,
            .manifestRevision = manifestRevision,
            .sources = sources,
            .units = writeUnits,
        });
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "source import candidate canonicalization allocation failed");
    }
}

Core::Status commitSourceImportCandidate(std::string_view stateUtf8Path,
                                         const SourceImportCandidate& candidate,
                                         AssetFormat::SourceImportManifestRevision manifestRevision)
{
    auto bytes = writeSourceImportCandidateBytes(candidate, manifestRevision);
    if (!bytes)
    {
        return Core::failure(std::move(bytes.error()).withContext("commitSourceImportCandidate", "build"));
    }
    auto status = Core::writeFile(stateUtf8Path, *bytes, Core::WriteFileConfig{.atomicReplace = true});
    if (!status)
    {
        return Core::failure(std::move(status.error()).withContext("commitSourceImportCandidate", "replace"));
    }
    return Core::success();
}

} // namespace Tina::Asset
