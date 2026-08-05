#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/SourceImportPlan.hpp>

#include <algorithm>
#include <exception>
#include <new>
#include <utility>

namespace Tina::Asset {
namespace {

using AssetFormat::SourceImportMetadataView;
using AssetFormat::SourceImportUnit;
using Core::u32;

[[nodiscard]] bool inputsEqual(const SourceImportMetadataView& baseline,
                               u32 baselineUnitIndex,
                               const SourceImportMetadataView& candidate,
                               u32 candidateUnitIndex,
                               u32 inputCount) noexcept
{
    for (u32 inputIndex = 0; inputIndex < inputCount; ++inputIndex)
    {
        const auto oldInput = baseline.unitInputForUnit(baselineUnitIndex, inputIndex);
        const auto newInput = candidate.unitInputForUnit(candidateUnitIndex, inputIndex);
        if (!oldInput || !newInput || oldInput->flags != newInput->flags)
        {
            return false;
        }

        const auto oldSource = baseline.source(oldInput->sourceIndex);
        const auto newSource = candidate.source(newInput->sourceIndex);
        const auto oldPath = baseline.sourcePath(oldInput->sourceIndex);
        const auto newPath = candidate.sourcePath(newInput->sourceIndex);
        if (!oldSource || !newSource || !oldPath || !newPath || *oldPath != *newPath ||
            oldSource->contentHash != newSource->contentHash ||
            oldSource->fileBytes != newSource->fileBytes ||
            oldSource->readExtent != newSource->readExtent)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool outputsEqual(const SourceImportMetadataView& baseline,
                                u32 baselineUnitIndex,
                                const SourceImportMetadataView& candidate,
                                u32 candidateUnitIndex,
                                u32 outputCount) noexcept
{
    for (u32 outputIndex = 0; outputIndex < outputCount; ++outputIndex)
    {
        const auto oldOutput = baseline.outputForUnit(baselineUnitIndex, outputIndex);
        const auto newOutput = candidate.outputForUnit(candidateUnitIndex, outputIndex);
        if (!oldOutput || !newOutput || oldOutput->assetId != newOutput->assetId ||
            oldOutput->assetKind != newOutput->assetKind)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool unitsEqual(const SourceImportMetadataView& baseline,
                              u32 baselineUnitIndex,
                              const SourceImportMetadataView& candidate,
                              u32 candidateUnitIndex,
                              const SourceImportUnit& oldUnit,
                              const SourceImportUnit& newUnit) noexcept
{
    return baseline.header().targetPlatform == candidate.header().targetPlatform &&
           oldUnit.importerKind == newUnit.importerKind &&
           oldUnit.importerVersion == newUnit.importerVersion &&
           oldUnit.settingsHash == newUnit.settingsHash &&
           oldUnit.inputCount == newUnit.inputCount &&
           oldUnit.outputCount == newUnit.outputCount &&
           inputsEqual(baseline, baselineUnitIndex, candidate, candidateUnitIndex,
                       oldUnit.inputCount) &&
           outputsEqual(baseline, baselineUnitIndex, candidate, candidateUnitIndex,
                        oldUnit.outputCount);
}

} // namespace

Core::Status validateSourceImportCatalogBinding(
    const AssetFormat::SourceImportMetadataView& metadata,
    AssetFormat::SourceImportManifestRevision catalogRevision)
{
    if (!metadata || !catalogRevision.manifestDigest || catalogRevision.manifestBytes == 0U)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import Catalog binding input is invalid");
    }
    if (metadata.header().manifestRevision != catalogRevision)
    {
        return Core::failure(AssetErrorCode::SourceImportCatalogMismatch,
                             "source import metadata does not match the current Catalog manifest");
    }
    return Core::success();
}

Core::Status validateSourceImportCatalogOutputs(
    const AssetFormat::SourceImportMetadataView& metadata,
    const CatalogSnapshot& catalog)
{
    if (!metadata || !catalog)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import output validation input is invalid");
    }
    if (metadata.header().outputCount != catalog.entryCount())
    {
        return Core::failure(AssetErrorCode::SourceImportCatalogMismatch,
                             "source import outputs do not cover the Catalog");
    }

    for (Core::u32 outputIndex = 0; outputIndex < metadata.header().outputCount; ++outputIndex)
    {
        const auto output = metadata.output(outputIndex);
        if (!output)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "source import output is missing");
        }
        const auto entryIndex = catalog.find(output->assetId);
        const auto entry = entryIndex ? catalog.entry(*entryIndex) : std::nullopt;
        if (!entry || entry->assetKind != output->assetKind)
        {
            return Core::failure(AssetErrorCode::SourceImportCatalogMismatch,
                                 "source import output does not match the Catalog");
        }
    }
    return Core::success();
}

Core::Result<SourceImportPlan>
planSourceImports(const AssetFormat::SourceImportMetadataView& baseline,
                  const AssetFormat::SourceImportMetadataView& candidate,
                  SourceImportPlanConfig config)
{
    if (!baseline || !candidate || config.memoryResource == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import plan requires valid metadata and memory resource");
    }

    try
    {
        std::pmr::vector<SourceImportChange> changes{config.memoryResource};
        changes.reserve(std::min<Core::u32>(
            config.maxChanges,
            std::max(baseline.header().unitCount, candidate.header().unitCount)));

        u32 oldIndex = 0;
        u32 newIndex = 0;
        u32 addedCount = 0;
        u32 removedCount = 0;
        u32 reimportCount = 0;
        const auto append = [&](AssetFormat::SourceImportUnitId unitId,
                                SourceImportChangeKind kind) -> Core::Status {
            if (changes.size() >= config.maxChanges)
            {
                return Core::failure(AssetErrorCode::SourceImportPlanCapacityExceeded,
                                     "source import plan capacity exceeded");
            }
            changes.push_back(SourceImportChange{.unitId = unitId, .kind = kind});
            switch (kind)
            {
            case SourceImportChangeKind::Added:
                ++addedCount;
                break;
            case SourceImportChangeKind::Removed:
                ++removedCount;
                break;
            case SourceImportChangeKind::Reimport:
                ++reimportCount;
                break;
            }
            return Core::success();
        };

        while (oldIndex < baseline.header().unitCount || newIndex < candidate.header().unitCount)
        {
            const auto oldUnit = oldIndex < baseline.header().unitCount
                                     ? baseline.unit(oldIndex)
                                     : std::nullopt;
            const auto newUnit = newIndex < candidate.header().unitCount
                                     ? candidate.unit(newIndex)
                                     : std::nullopt;
            if ((oldIndex < baseline.header().unitCount && !oldUnit) ||
                (newIndex < candidate.header().unitCount && !newUnit))
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "source import unit missing during planning");
            }

            if (!oldUnit)
            {
                if (const auto status = append(newUnit->unitId, SourceImportChangeKind::Added);
                    !status)
                {
                    return Core::failure(status.error());
                }
                ++newIndex;
                continue;
            }
            if (!newUnit)
            {
                if (const auto status = append(oldUnit->unitId, SourceImportChangeKind::Removed);
                    !status)
                {
                    return Core::failure(status.error());
                }
                ++oldIndex;
                continue;
            }
            if (oldUnit->unitId < newUnit->unitId)
            {
                if (const auto status = append(oldUnit->unitId, SourceImportChangeKind::Removed);
                    !status)
                {
                    return Core::failure(status.error());
                }
                ++oldIndex;
                continue;
            }
            if (newUnit->unitId < oldUnit->unitId)
            {
                if (const auto status = append(newUnit->unitId, SourceImportChangeKind::Added);
                    !status)
                {
                    return Core::failure(status.error());
                }
                ++newIndex;
                continue;
            }

            if (!unitsEqual(baseline, oldIndex, candidate, newIndex, *oldUnit, *newUnit))
            {
                if (const auto status = append(newUnit->unitId, SourceImportChangeKind::Reimport);
                    !status)
                {
                    return Core::failure(status.error());
                }
            }
            ++oldIndex;
            ++newIndex;
        }

        return SourceImportPlan{
            .changes = std::move(changes),
            .addedCount = addedCount,
            .removedCount = removedCount,
            .reimportCount = reimportCount,
        };
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "source import plan allocation failed");
    } catch (const std::exception& exception)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, exception.what());
    } catch (...)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "source import plan construction failed");
    }
}

} // namespace Tina::Asset
