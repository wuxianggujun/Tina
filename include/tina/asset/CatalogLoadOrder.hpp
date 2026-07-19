#pragma once

#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::Asset {

struct CatalogLoadOrderConfig final {
    std::pmr::memory_resource* memoryResource = nullptr;
};

// Computes a deterministic dependency-respecting load order for a set of requested AssetIds.
// Dependencies are expanded transitively; each entry appears once. Order is dependencies-first
// (post-order of the DAG). CatalogSnapshot must already be cycle-free.
//
// Output: owning vector of Catalog entry indices in load order.
[[nodiscard]] Core::Result<std::pmr::vector<Core::u32>>
computeCatalogLoadOrder(const CatalogSnapshot& catalog, std::span<const Core::AssetId> requestedAssetIds,
                        CatalogLoadOrderConfig config);

} // namespace Tina::Asset
