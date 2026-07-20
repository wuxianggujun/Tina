#pragma once

#include <tina/asset/CatalogLoadOrder.hpp>
#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::Asset {

struct CatalogLoadPlanEntry final {
    Core::u32 entryIndex = 0;
    Core::AssetId assetId;
    AssetFormat::AssetKind assetKind = AssetFormat::AssetKind::Invalid;
    Core::u32 dependencyCount = 0;
    Core::u64 cookedFileBytes = 0;
    AssetFormat::CookedArtifactPath relativePath{};
};

struct CatalogLoadPlanConfig final {
    std::pmr::memory_resource* memoryResource = nullptr;
};

// Expands requested AssetIds via computeCatalogLoadOrder and attaches Catalog entry metadata plus
// the deterministic cooked object relative path. Does not read the filesystem.
[[nodiscard]] Core::Result<std::pmr::vector<CatalogLoadPlanEntry>>
planCatalogLoads(const CatalogSnapshot& catalog, std::span<const Core::AssetId> requestedAssetIds,
                 CatalogLoadPlanConfig config);

// Plans loads for every Catalog entry (each entry id is requested once, in index order).
// Result remains dependencies-first and de-duplicated.
[[nodiscard]] Core::Result<std::pmr::vector<CatalogLoadPlanEntry>>
planCatalogLoadsAll(const CatalogSnapshot& catalog, CatalogLoadPlanConfig config);

// Sums CatalogLoadPlanEntry::cookedFileBytes with checked overflow. Empty plan returns 0.
[[nodiscard]] Core::Result<Core::u64> totalCookedFileBytes(std::span<const CatalogLoadPlanEntry> plan);

} // namespace Tina::Asset
