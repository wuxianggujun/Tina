#pragma once

#include <tina/asset/CatalogFile.hpp>
#include <tina/asset/CatalogPackageValidation.hpp>
#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/io/PackageFile.hpp>

#include <string_view>

namespace Tina::Asset {

inline constexpr std::string_view DefaultCatalogPackageRelativePath = "catalog.pck";
inline constexpr std::string_view CatalogManifestVirtualPath = "manifest.tmnft";

enum class CatalogObjectValidation : Core::u8 {
    // Validate package index + manifest now; verify an object's bytes on first load.
    OnDemand,
    // Apply validation to all catalog objects before returning the snapshot.
    OnOpen,
};

struct CatalogPackageOpenConfig final {
    CatalogFileLoadConfig manifest{};
    CatalogObjectValidation objectValidation = CatalogObjectValidation::OnDemand;
    // Used for OnOpen. Offline cook/validate/reload explicitly select OnOpen;
    // mounting a runtime catalog does not fault every payload page into memory.
    CatalogPackageValidationConfig validation{};
    std::string_view packageRelativePath = DefaultCatalogPackageRelativePath;
    Core::PackageOpenConfig package{};
};

// Opens one immutable package containing the manifest and all cooked objects. Failure never
// publishes a partial snapshot. No loose-file fallback; an old catalog must be recooked.
[[nodiscard]] Core::Result<CatalogSnapshot> openCatalogPackage(std::string_view catalogRootUtf8,
                                                               CatalogPackageOpenConfig config);

} // namespace Tina::Asset
