#pragma once

#include <tina/asset/CatalogPackage.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/hash/ContentHash.hpp>

#include <compare>
#include <memory_resource>
#include <string_view>

namespace Tina::Asset {

inline constexpr Core::u64 DefaultCatalogManifestProbeMaxBytes =
    static_cast<Core::u64>(AssetFormat::Wire::CookedManifestHeaderBytes) +
    static_cast<Core::u64>(AssetFormat::Wire::MaxManifestEntries) *
        AssetFormat::Wire::ManifestEntryBytes +
    static_cast<Core::u64>(AssetFormat::Wire::MaxManifestDependencies) *
        AssetFormat::Wire::DependencyEntryBytes;

struct CatalogPackageRevision final {
    Core::ContentHash manifestDigest{};
    Core::u64 manifestBytes = 0;

    auto operator<=>(const CatalogPackageRevision&) const = default;
};

enum class CatalogPackageChangeState : Core::u8 {
    Unchanged = 0,
    Changed = 1,
};

struct CatalogPackageChangeProbe final {
    CatalogPackageChangeState state = CatalogPackageChangeState::Unchanged;
    CatalogPackageRevision candidate{};
};

struct CatalogPackageChangeDetectorConfig final {
    std::pmr::memory_resource* scratchMemoryResource = nullptr;
    Core::u64 maxManifestBytes = DefaultCatalogManifestProbeMaxBytes;
    std::string_view manifestRelativePath = DefaultCatalogManifestRelativePath;
};

// Captures a fixed-size revision from the complete manifest bytes. Object files are deliberately
// not read here; a Changed candidate must still pass full package validation before acceptance.
[[nodiscard]] Core::Result<CatalogPackageRevision>
captureCatalogPackageRevision(std::string_view catalogRootUtf8,
                              CatalogPackageChangeDetectorConfig config);

// Compares a fresh candidate with an accepted baseline. The detector never advances the baseline;
// callers accept probe.candidate only after the corresponding package validation/reload succeeds.
[[nodiscard]] Core::Result<CatalogPackageChangeProbe>
pollCatalogPackageChange(std::string_view catalogRootUtf8, CatalogPackageRevision baseline,
                         CatalogPackageChangeDetectorConfig config);

} // namespace Tina::Asset
