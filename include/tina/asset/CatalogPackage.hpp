#pragma once

#include <tina/asset/CatalogFile.hpp>
#include <tina/asset/CatalogPackageValidation.hpp>
#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <string_view>

namespace Tina::Asset {

// Default relative path of the cooked Manifest under a catalog root.
inline constexpr std::string_view DefaultCatalogManifestRelativePath = "manifest.tmnft";

struct CatalogPackageOpenConfig final {
    CatalogFileLoadConfig manifest{};
    // When true, run validateCatalogPackageOnDisk after the Snapshot is built.
    // On validation failure the Snapshot is destroyed and not published.
    bool validateOnOpen = true;
    CatalogPackageValidationConfig validation{};
    // Relative path from catalog root to the Manifest file (no absolute / no "..").
    std::string_view manifestRelativePath = DefaultCatalogManifestRelativePath;
};

// Opens a catalog package: catalogRoot/manifestRelativePath → CatalogSnapshot,
// optionally validates every object on disk. Failure does not publish a Snapshot.
[[nodiscard]] Core::Result<CatalogSnapshot> openCatalogPackage(std::string_view catalogRootUtf8,
                                                               CatalogPackageOpenConfig config);

} // namespace Tina::Asset
