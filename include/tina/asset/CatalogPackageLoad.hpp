#pragma once

#include <tina/asset/CatalogLoadPlan.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset/CookedAssetBatch.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace Tina::Asset {

// Owning result of opening a catalog package and loading cooked objects in one transaction.
// If any step fails, neither catalog nor assets are published.
struct CatalogPackageCookedLoad final {
    CatalogSnapshot catalog{};
    std::pmr::vector<CookedAssetFile> assets{};
};

// openCatalogPackage → plan (requested ids, or all when empty) → loadCookedAssetsFromPlan.
// Failure destroys any intermediate Snapshot/assets and returns the first structured error.
[[nodiscard]] Core::Result<CatalogPackageCookedLoad>
loadCookedAssetsFromPackage(std::string_view catalogRootUtf8, std::span<const Core::AssetId> requestedAssetIds,
                            CatalogPackageOpenConfig openConfig, CookedAssetBatchLoadConfig batchConfig);

} // namespace Tina::Asset
