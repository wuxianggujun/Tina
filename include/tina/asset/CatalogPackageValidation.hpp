#pragma once

#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <string_view>

namespace Tina::Asset {

struct CatalogPackageValidationConfig final {
    CookedAssetFileLoadConfig file{};
    // When true (default), load each object and verify ContentHash + Catalog entry alignment.
    // file.verifyContentHash is forced to true for this mode; the remaining file limits apply.
    // When false, only check that the deterministic object path exists as a regular file and
    // that file size equals CatalogEntry::cookedFileBytes (no full parse).
    bool verifyContent = true;
};

// Validates every Catalog entry against files under catalogRoot.
// Stops at the first failure, ignores unrelated extra files, and retains at most one loaded
// object at a time. catalogRootUtf8 must be strict UTF-8 without embedded NUL.
[[nodiscard]] Core::Status validateCatalogPackageOnDisk(std::string_view catalogRootUtf8,
                                                        const CatalogSnapshot& catalog,
                                                        CatalogPackageValidationConfig config);

} // namespace Tina::Asset
