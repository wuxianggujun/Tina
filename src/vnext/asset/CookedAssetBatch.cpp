#include <tina/asset/CookedAssetBatch.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <new>
#include <utility>

namespace Tina::Asset {
namespace {

[[nodiscard]] std::pmr::memory_resource* resolveBatchMemory(CookedAssetBatchLoadConfig& config)
{
    auto* memoryResource = config.memoryResource != nullptr ? config.memoryResource : config.file.memoryResource;
    if (memoryResource != nullptr && config.file.memoryResource == nullptr)
    {
        config.file.memoryResource = memoryResource;
    }
    return memoryResource;
}

} // namespace

Core::Result<std::pmr::vector<CookedAssetFile>>
loadCookedAssetsFromPlan(std::string_view catalogRootUtf8, const CatalogSnapshot& catalog,
                         std::span<const CatalogLoadPlanEntry> plan, CookedAssetBatchLoadConfig config)
{
    auto* memoryResource = resolveBatchMemory(config);
    if (memoryResource == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "batch cooked load requires memory resource");
    }
    if (!catalog)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog snapshot is empty");
    }

    std::pmr::vector<CookedAssetFile> loaded{memoryResource};
    try
    {
        loaded.reserve(plan.size());
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "batch cooked load reserve failed");
    }

    for (const auto& row : plan)
    {
        const auto entry = catalog.entry(row.entryIndex);
        if (!entry)
        {
            loaded.clear();
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "plan entry index missing from catalog");
        }
        if (entry->assetId != row.assetId || entry->assetKind != row.assetKind)
        {
            loaded.clear();
            return Core::failure(AssetErrorCode::CatalogEntryMismatch, "plan row does not match catalog entry");
        }

        auto asset = loadCookedAssetFromCatalog(catalogRootUtf8, catalog, row.assetId, config.file);
        if (!asset)
        {
            loaded.clear();
            return Core::failure(std::move(asset.error()).withContext("loadCookedAssetsFromPlan", "loadOne"));
        }
        loaded.push_back(std::move(*asset));
    }

    return loaded;
}

Core::Result<std::pmr::vector<CookedAssetFile>>
loadCookedAssetsFromCatalog(std::string_view catalogRootUtf8, const CatalogSnapshot& catalog,
                            std::span<const Core::AssetId> requestedAssetIds, CookedAssetBatchLoadConfig config)
{
    auto* memoryResource = resolveBatchMemory(config);
    if (memoryResource == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "batch cooked load requires memory resource");
    }

    auto plan = planCatalogLoads(catalog, requestedAssetIds, CatalogLoadPlanConfig{.memoryResource = memoryResource});
    if (!plan)
    {
        return Core::failure(std::move(plan.error()).withContext("loadCookedAssetsFromCatalog", "plan"));
    }
    return loadCookedAssetsFromPlan(catalogRootUtf8, catalog, *plan, config);
}

} // namespace Tina::Asset
