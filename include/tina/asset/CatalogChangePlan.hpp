#pragma once

#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <memory_resource>
#include <vector>

namespace Tina::Asset {

enum class CatalogChangeKind : Core::u8 {
    Added = 0,
    Removed = 1,
    Modified = 2,
    Affected = 3,
};

struct CatalogChange final {
    Core::AssetId assetId;
    CatalogChangeKind kind = CatalogChangeKind::Added;
};

struct CatalogChangePlan final {
    std::pmr::vector<CatalogChange> changes{};
    Core::u32 addedCount = 0;
    Core::u32 removedCount = 0;
    Core::u32 modifiedCount = 0;
    Core::u32 affectedCount = 0;
};

struct CatalogChangePlanConfig final {
    std::pmr::memory_resource* memoryResource = nullptr;
    // Maximum number of rows across all change kinds. Zero permits only an empty plan.
    Core::u32 maxChanges = 0;
};

// Compares two immutable Catalog snapshots. Direct changes are Added, Removed, or Modified;
// Affected marks unchanged entries in the new catalog that depend transitively on an Added or
// Modified entry. The result contains one row per AssetId in ascending AssetId order.
[[nodiscard]] Core::Result<CatalogChangePlan>
planCatalogChanges(const CatalogSnapshot& oldCatalog, const CatalogSnapshot& newCatalog,
                    CatalogChangePlanConfig config);

} // namespace Tina::Asset
