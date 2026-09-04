#include "SourceImportExecutor.hpp"

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <map>
#include <new>
#include <set>
#include <utility>

namespace Tina::Asset {
namespace {

using AssetFormat::SourceImportUnitId;

[[nodiscard]] constexpr bool isSupportedSourceImporterKind(const Core::u32 value) noexcept
{
    switch (static_cast<SourceImporterKind>(value))
    {
    case SourceImporterKind::CatalogRecipe:
    case SourceImporterKind::Gltf:
    case SourceImporterKind::Texture:
    case SourceImporterKind::Audio:
        return true;
    }
    return false;
}

struct CandidateBuilder final {
    SourceImportCandidate candidate{};
    std::map<std::string, Core::u32, std::less<>> sourceIndexes{};
    std::set<SourceImportUnitId> unitIds{};
    std::set<Core::AssetId> outputIds{};
    bool hasTargetPlatform = false;
};

[[nodiscard]] Core::Status acceptTargetPlatform(CandidateBuilder& builder,
                                                 AssetFormat::TargetPlatform platform)
{
    if (platform == AssetFormat::TargetPlatform::Invalid)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import candidate target platform is invalid");
    }
    if (!builder.hasTargetPlatform)
    {
        builder.candidate.targetPlatform = platform;
        builder.hasTargetPlatform = true;
        return Core::success();
    }
    if (builder.candidate.targetPlatform != platform)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import candidates target different platforms");
    }
    return Core::success();
}

[[nodiscard]] Core::Result<Core::u32>
appendSource(CandidateBuilder& builder, const SourceImportCapturedSource& source)
{
    const auto existing = builder.sourceIndexes.find(source.path);
    if (existing != builder.sourceIndexes.end())
    {
        const auto& current = builder.candidate.sources[existing->second];
        if (current.contentHash != source.contentHash || current.fileBytes != source.fileBytes ||
            current.readExtent != source.readExtent)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "shared source has inconsistent fingerprints");
        }
        return existing->second;
    }

    const auto index = static_cast<Core::u32>(builder.candidate.sources.size());
    builder.candidate.sources.push_back(source);
    builder.sourceIndexes.emplace(source.path, index);
    return index;
}

[[nodiscard]] Core::Status appendUnit(CandidateBuilder& builder,
                                      const SourceImportCandidate& sourceCandidate,
                                      const SourceImportCapturedUnit& sourceUnit)
{
    if (!builder.unitIds.insert(sourceUnit.unitId).second)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import candidate contains duplicate unit ids");
    }

    SourceImportCapturedUnit unit{
        .unitId = sourceUnit.unitId,
        .importerKind = sourceUnit.importerKind,
        .importerVersion = sourceUnit.importerVersion,
        .settingsHash = sourceUnit.settingsHash,
    };
    unit.inputs.reserve(sourceUnit.inputs.size());
    for (const auto& input : sourceUnit.inputs)
    {
        if (input.sourceIndex >= sourceCandidate.sources.size())
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "source import input index is out of range");
        }
        auto index = appendSource(builder, sourceCandidate.sources[input.sourceIndex]);
        if (!index)
        {
            return Core::failure(std::move(index.error()));
        }
        unit.inputs.push_back(SourceImportCapturedInput{
            .sourceIndex = *index,
            .flags = input.flags,
        });
    }
    unit.outputs.reserve(sourceUnit.outputs.size());
    for (const auto& output : sourceUnit.outputs)
    {
        if (!builder.outputIds.insert(output.assetId).second)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "source import output has multiple owners");
        }
        unit.outputs.push_back(output);
    }
    builder.candidate.units.push_back(std::move(unit));
    return Core::success();
}

[[nodiscard]] Core::Status appendCandidate(CandidateBuilder& builder,
                                           const SourceImportCandidate& source)
{
    if (const auto status = acceptTargetPlatform(builder, source.targetPlatform); !status)
    {
        return status;
    }
    for (const auto& unit : source.units)
    {
        if (const auto status = appendUnit(builder, source, unit); !status)
        {
            return status;
        }
    }
    return Core::success();
}

[[nodiscard]] std::optional<Core::u32>
findUnitIndex(const AssetFormat::SourceImportMetadataView& baseline,
              SourceImportUnitId unitId) noexcept
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
        }
        else
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

[[nodiscard]] Core::Status appendBaselineUnit(CandidateBuilder& builder,
                                              const AssetFormat::SourceImportMetadataView& baseline,
                                              Core::u32 unitIndex,
                                              std::vector<Core::AssetId>& retainedAssetIds)
{
    const auto sourceUnit = baseline.unit(unitIndex);
    if (!sourceUnit)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "retained source import unit is missing");
    }
    if (!isSupportedSourceImporterKind(sourceUnit->importerKind))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "retained source import unit has unsupported importer kind");
    }

    SourceImportCandidate temporary{
        .targetPlatform = baseline.header().targetPlatform,
    };
    SourceImportCapturedUnit unit{
        .unitId = sourceUnit->unitId,
        .importerKind = static_cast<SourceImporterKind>(sourceUnit->importerKind),
        .importerVersion = sourceUnit->importerVersion,
        .settingsHash = sourceUnit->settingsHash,
    };
    unit.inputs.reserve(sourceUnit->inputCount);
    for (Core::u32 inputIndex = 0; inputIndex < sourceUnit->inputCount; ++inputIndex)
    {
        const auto input = baseline.unitInputForUnit(unitIndex, inputIndex);
        if (!input)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "retained source import input is missing");
        }
        const auto source = baseline.source(input->sourceIndex);
        const auto path = baseline.sourcePath(input->sourceIndex);
        if (!source || !path)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "retained source import source is missing");
        }
        temporary.sources.push_back(SourceImportCapturedSource{
            .path = std::string(*path),
            .contentHash = source->contentHash,
            .fileBytes = source->fileBytes,
            .readExtent = source->readExtent,
        });
        unit.inputs.push_back(SourceImportCapturedInput{
            .sourceIndex = static_cast<Core::u32>(temporary.sources.size() - 1U),
            .flags = input->flags,
        });
    }
    unit.outputs.reserve(sourceUnit->outputCount);
    for (Core::u32 outputIndex = 0; outputIndex < sourceUnit->outputCount; ++outputIndex)
    {
        const auto output = baseline.outputForUnit(unitIndex, outputIndex);
        if (!output)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "retained source import output is missing");
        }
        unit.outputs.push_back(SourceImportCapturedOutput{
            .assetId = output->assetId,
            .assetKind = output->assetKind,
        });
        retainedAssetIds.push_back(output->assetId);
    }
    temporary.units.push_back(std::move(unit));
    return appendCandidate(builder, temporary);
}

} // namespace

Core::Result<SourceImportCandidateComposeResult>
composeSourceImportCandidate(const SourceImportCandidateComposeDesc& desc)
{
    const bool emptyComposition =
        desc.retainedUnitIds.empty() && desc.recookedCandidates.empty();
    if (!desc.retainedUnitIds.empty() && (desc.baseline == nullptr || !*desc.baseline))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "retained source import units require a baseline");
    }
    if (emptyComposition && (desc.baseline == nullptr || !*desc.baseline))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "empty source import composition requires a baseline");
    }

    try
    {
        CandidateBuilder builder{};
        if (emptyComposition)
        {
            if (const auto status = acceptTargetPlatform(
                    builder, desc.baseline->header().targetPlatform); !status)
            {
                return Core::failure(status.error());
            }
        }
        std::vector<Core::AssetId> retainedAssetIds;
        std::set<SourceImportUnitId> retainedIds;
        for (const auto unitId : desc.retainedUnitIds)
        {
            if (!retainedIds.insert(unitId).second)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "retained source import unit ids are duplicated");
            }
            const auto unitIndex = findUnitIndex(*desc.baseline, unitId);
            if (!unitIndex)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "retained source import unit is absent from baseline");
            }
            if (const auto status = appendBaselineUnit(builder, *desc.baseline, *unitIndex,
                                                       retainedAssetIds);
                !status)
            {
                return Core::failure(status.error());
            }
        }
        for (const auto& recooked : desc.recookedCandidates)
        {
            if (const auto status = appendCandidate(builder, recooked); !status)
            {
                return Core::failure(status.error());
            }
        }
        if ((builder.candidate.units.empty() || builder.candidate.sources.empty()) &&
            !(emptyComposition && builder.candidate.units.empty() &&
              builder.candidate.sources.empty()))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "composed source import candidate is empty");
        }

        std::sort(retainedAssetIds.begin(), retainedAssetIds.end());
        return SourceImportCandidateComposeResult{
            .candidate = std::move(builder.candidate),
            .retainedAssetIds = std::move(retainedAssetIds),
        };
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "source import candidate composition allocation failed");
    }
}

} // namespace Tina::Asset
