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
    // When false, only check virtual entry presence and exact size (no payload page reads).
    bool verifyContent = true;
    // When true (and verifyContent), known typed payload objects, including SpriteAnimationClip,
    // must also parse and validate their dependency contracts. Default false for raw fixtures.
    bool verifyTypedPayload = false;
};

// Validates every Catalog entry against its pinned immutable package, without per-object
// filesystem calls or heap payload copies. Stops at the first failure.
[[nodiscard]] Core::Status validateCatalogPackage(const CatalogSnapshot& catalog,
                                                        CatalogPackageValidationConfig config);

} // namespace Tina::Asset
