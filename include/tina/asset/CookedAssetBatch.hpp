#pragma once

#include <tina/asset/CatalogLoadOrder.hpp>
#include <tina/asset/CatalogLoadPlan.hpp>
#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <memory_resource>
#include <span>
#include <string_view>
#include <vector>

namespace Tina::Asset {

struct CookedAssetBatchLoadConfig final {
    CookedAssetFileLoadConfig file{};
    // Used for load-order/plan scratch and the returned vector allocator.
    // When null, file.memoryResource is used.
    std::pmr::memory_resource* memoryResource = nullptr;
};

// Expands requested AssetIds through computeCatalogLoadOrder, then loads each cooked object
// under catalogRoot in dependencies-first order. Any single load failure destroys already-loaded
// files and returns the first structured error (no partial batch publish).
[[nodiscard]] Core::Result<std::pmr::vector<CookedAssetFile>>
loadCookedAssetsFromCatalog(std::string_view catalogRootUtf8, const CatalogSnapshot& catalog,
                            std::span<const Core::AssetId> requestedAssetIds, CookedAssetBatchLoadConfig config);

// Loads cooked objects for a precomputed plan in plan order. Plan rows must refer to the same
// catalog. Failure rolls back already-loaded files and does not publish a partial batch.
[[nodiscard]] Core::Result<std::pmr::vector<CookedAssetFile>>
loadCookedAssetsFromPlan(std::string_view catalogRootUtf8, const CatalogSnapshot& catalog,
                         std::span<const CatalogLoadPlanEntry> plan, CookedAssetBatchLoadConfig config);

} // namespace Tina::Asset
