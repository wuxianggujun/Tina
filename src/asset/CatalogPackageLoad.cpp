#include <tina/asset/CatalogPackageLoad.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <utility>

namespace Tina::Asset {

Core::Result<CatalogPackageCookedLoad>
loadCookedAssetsFromPackage(std::string_view catalogRootUtf8, std::span<const Core::AssetId> requestedAssetIds,
                            CatalogPackageOpenConfig openConfig, CookedAssetBatchLoadConfig batchConfig)
{
    auto* memoryResource =
        batchConfig.memoryResource != nullptr ? batchConfig.memoryResource : batchConfig.file.memoryResource;
    if (memoryResource == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "package cooked load requires memory resource");
    }
    if (batchConfig.file.memoryResource == nullptr)
    {
        batchConfig.file.memoryResource = memoryResource;
    }
    if (batchConfig.memoryResource == nullptr)
    {
        batchConfig.memoryResource = memoryResource;
    }
    if (openConfig.manifest.catalog.memoryResource == nullptr)
    {
        openConfig.manifest.catalog.memoryResource = memoryResource;
    }
    if (openConfig.validateOnOpen && openConfig.validation.verifyContent &&
        openConfig.validation.file.memoryResource == nullptr)
    {
        openConfig.validation.file.memoryResource = memoryResource;
    }

    auto catalog = openCatalogPackage(catalogRootUtf8, openConfig);
    if (!catalog)
    {
        return Core::failure(std::move(catalog.error()).withContext("loadCookedAssetsFromPackage", "open"));
    }

    Core::Result<std::pmr::vector<CatalogLoadPlanEntry>> plan =
        Core::failure(AssetErrorCode::InvalidCatalogConfig, "plan not computed");
    if (requestedAssetIds.empty())
    {
        plan = planCatalogLoadsAll(*catalog, CatalogLoadPlanConfig{.memoryResource = memoryResource});
    } else
    {
        plan = planCatalogLoads(*catalog, requestedAssetIds, CatalogLoadPlanConfig{.memoryResource = memoryResource});
    }
    if (!plan)
    {
        return Core::failure(std::move(plan.error()).withContext("loadCookedAssetsFromPackage", "plan"));
    }

    auto assets = loadCookedAssetsFromPlan(catalogRootUtf8, *catalog, *plan, batchConfig);
    if (!assets)
    {
        return Core::failure(std::move(assets.error()).withContext("loadCookedAssetsFromPackage", "load"));
    }

    return CatalogPackageCookedLoad{
        .catalog = std::move(*catalog),
        .assets = std::move(*assets),
    };
}

} // namespace Tina::Asset
