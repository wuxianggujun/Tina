#pragma once

#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/io/ReadFile.hpp>

#include <string_view>

namespace Tina::Asset {

struct CatalogFileLoadConfig final {
    CatalogConfig catalog{};
    AssetFormat::CookedManifestLimits manifestLimits{};
    Core::u64 maxFileBytes = AssetFormat::Wire::MaxManifestFileBytes;
    // Optional separate PMR for temporary file bytes. When null, catalog.memoryResource is used.
    std::pmr::memory_resource* fileMemoryResource = nullptr;
};

// Reads a cooked Manifest file, parses it, and builds an owning CatalogSnapshot.
// Temporary file bytes are released after Create succeeds or fails.
[[nodiscard]] Core::Result<CatalogSnapshot> loadCatalogSnapshotFromManifestFile(std::string_view utf8Path,
                                                                                CatalogFileLoadConfig config);

} // namespace Tina::Asset
