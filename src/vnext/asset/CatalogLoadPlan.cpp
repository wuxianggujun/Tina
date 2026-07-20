#include <tina/asset/CatalogLoadPlan.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <new>
#include <utility>

namespace Tina::Asset {

Core::Result<std::pmr::vector<CatalogLoadPlanEntry>>
planCatalogLoads(const CatalogSnapshot& catalog, std::span<const Core::AssetId> requestedAssetIds,
                 CatalogLoadPlanConfig config)
{
    if (config.memoryResource == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog load plan requires memory resource");
    }

    auto order = computeCatalogLoadOrder(catalog, requestedAssetIds,
                                         CatalogLoadOrderConfig{.memoryResource = config.memoryResource});
    if (!order)
    {
        return Core::failure(std::move(order.error()).withContext("planCatalogLoads", "loadOrder"));
    }

    std::pmr::vector<CatalogLoadPlanEntry> plan{config.memoryResource};
    try
    {
        plan.reserve(order->size());
        for (const auto entryIndex : *order)
        {
            const auto entry = catalog.entry(entryIndex);
            if (!entry)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog entry missing during load plan");
            }
            auto relativePath = AssetFormat::makeCookedArtifactPath(entry->assetKind, entry->assetId);
            if (!relativePath)
            {
                return Core::failure(std::move(relativePath.error()).withContext("planCatalogLoads", "artifactPath"));
            }
            plan.push_back(CatalogLoadPlanEntry{
                .entryIndex = entryIndex,
                .assetId = entry->assetId,
                .assetKind = entry->assetKind,
                .dependencyCount = entry->dependencyCount,
                .cookedFileBytes = entry->cookedFileBytes,
                .relativePath = *relativePath,
            });
        }
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "catalog load plan allocation failed");
    }

    return plan;
}

Core::Result<std::pmr::vector<CatalogLoadPlanEntry>> planCatalogLoadsAll(const CatalogSnapshot& catalog,
                                                                         CatalogLoadPlanConfig config)
{
    if (!catalog)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog snapshot is empty");
    }
    if (config.memoryResource == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog load plan requires memory resource");
    }

    std::pmr::vector<Core::AssetId> requested{config.memoryResource};
    try
    {
        requested.reserve(catalog.entryCount());
        for (Core::u32 index = 0; index < catalog.entryCount(); ++index)
        {
            const auto entry = catalog.entry(index);
            if (!entry)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog entry missing during load plan all");
            }
            requested.push_back(entry->assetId);
        }
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "catalog load plan all allocation failed");
    }

    return planCatalogLoads(catalog, requested, config);
}

} // namespace Tina::Asset
