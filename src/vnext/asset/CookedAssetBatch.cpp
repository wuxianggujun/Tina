#include <tina/asset/CookedAssetBatch.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <utility>

namespace Tina::Asset {

Core::Result<std::pmr::vector<CookedAssetFile>>
loadCookedAssetsFromCatalog(std::string_view catalogRootUtf8, const CatalogSnapshot& catalog,
                            std::span<const Core::AssetId> requestedAssetIds, CookedAssetBatchLoadConfig config)
{
    auto* memoryResource = config.memoryResource != nullptr ? config.memoryResource : config.file.memoryResource;
    if (memoryResource == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "batch cooked load requires memory resource");
    }
    if (config.file.memoryResource == nullptr)
    {
        config.file.memoryResource = memoryResource;
    }

    auto order = computeCatalogLoadOrder(catalog, requestedAssetIds,
                                         CatalogLoadOrderConfig{.memoryResource = memoryResource});
    if (!order)
    {
        return Core::failure(std::move(order.error()).withContext("loadCookedAssetsFromCatalog", "loadOrder"));
    }

    std::pmr::vector<CookedAssetFile> loaded{memoryResource};
    try
    {
        loaded.reserve(order->size());
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "batch cooked load reserve failed");
    }

    for (const auto entryIndex : *order)
    {
        const auto entry = catalog.entry(entryIndex);
        if (!entry)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog entry missing during batch load");
        }

        auto asset = loadCookedAssetFromCatalog(catalogRootUtf8, catalog, entry->assetId, config.file);
        if (!asset)
        {
            // Destroy already-loaded assets by dropping the vector before returning.
            loaded.clear();
            return Core::failure(std::move(asset.error()).withContext("loadCookedAssetsFromCatalog", "loadOne"));
        }
        loaded.push_back(std::move(*asset));
    }

    return loaded;
}

} // namespace Tina::Asset
