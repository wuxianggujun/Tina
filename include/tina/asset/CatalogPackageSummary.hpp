#pragma once

#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <array>
#include <memory_resource>
#include <vector>

namespace Tina::Asset {

struct CatalogEntrySummary final {
    Core::AssetId::CanonicalText assetIdText{};
    AssetFormat::AssetKind assetKind = AssetFormat::AssetKind::Invalid;
    Core::u16 assetTypeVersion = 0;
    Core::u32 dependencyCount = 0;
    Core::u64 cookedFileBytes = 0;
};

struct CatalogPackageSummary final {
    Core::u32 entryCount = 0;
    Core::u32 dependencyCount = 0;
    std::pmr::vector<CatalogEntrySummary> entries{};
};

struct CatalogPackageSummaryConfig final {
    std::pmr::memory_resource* memoryResource = nullptr;
    // When false, only totals are filled; entries stays empty.
    bool includeEntries = true;
};

// Builds a diagnostic summary from an immutable CatalogSnapshot. Does not touch the filesystem.
[[nodiscard]] Core::Result<CatalogPackageSummary> buildCatalogPackageSummary(const CatalogSnapshot& catalog,
                                                                             CatalogPackageSummaryConfig config);

} // namespace Tina::Asset
