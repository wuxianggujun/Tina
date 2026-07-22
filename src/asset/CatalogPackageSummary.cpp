#include <tina/asset/CatalogPackageSummary.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <new>
#include <utility>

namespace Tina::Asset {

Core::Result<CatalogPackageSummary> buildCatalogPackageSummary(const CatalogSnapshot& catalog,
                                                               CatalogPackageSummaryConfig config)
{
    if (!catalog)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog snapshot is empty");
    }
    if (config.memoryResource == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "package summary requires memory resource");
    }

    CatalogPackageSummary summary{
        .entryCount = catalog.entryCount(),
        .dependencyCount = catalog.dependencyCount(),
        .entries = std::pmr::vector<CatalogEntrySummary>{config.memoryResource},
    };

    if (!config.includeEntries || catalog.entryCount() == 0U)
    {
        return summary;
    }

    try
    {
        summary.entries.reserve(catalog.entryCount());
        for (Core::u32 index = 0; index < catalog.entryCount(); ++index)
        {
            const auto entry = catalog.entry(index);
            if (!entry)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog entry missing during summary");
            }
            CatalogEntrySummary row{
                .assetKind = entry->assetKind,
                .assetTypeVersion = entry->assetTypeVersion,
                .dependencyCount = entry->dependencyCount,
                .cookedFileBytes = entry->cookedFileBytes,
            };
            const auto text = entry->assetId.canonicalText();
            std::copy_n(text.data(), text.size(), row.assetIdText.begin());
            summary.entries.push_back(row);
        }
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "catalog package summary allocation failed");
    }

    return summary;
}

} // namespace Tina::Asset
