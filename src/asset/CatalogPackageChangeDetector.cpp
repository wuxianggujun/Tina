#include <tina/asset/CatalogPackageChangeDetector.hpp>

#include "CatalogPackagePath.hpp"

#include <tina/asset/AssetErrors.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <string>
#include <utility>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::Result<std::string>
catalogManifestPath(std::string_view catalogRootUtf8, std::string_view manifestRelativePath)
{
    auto path = Detail::resolveCatalogManifestPath(catalogRootUtf8, manifestRelativePath);
    if (!path)
    {
        return Core::failure(std::move(path.error()));
    }
    const auto generic = path->fullPath.generic_u8string();
    return std::string(generic.begin(), generic.end());
}

} // namespace

Core::Result<CatalogPackageRevision>
captureCatalogPackageRevision(std::string_view catalogRootUtf8,
                              CatalogPackageChangeDetectorConfig config)
{
    if (config.scratchMemoryResource == nullptr || config.maxManifestBytes == 0U ||
        config.maxManifestBytes > Core::MaxReadFileBytes)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "invalid catalog package change detector config");
    }

    auto manifestPath = catalogManifestPath(catalogRootUtf8, config.manifestRelativePath);
    if (!manifestPath)
    {
        return Core::failure(std::move(manifestPath.error()).withContext(
            "captureCatalogPackageRevision", "manifestPath"));
    }
    auto manifestBytes = Core::readFile(
        *manifestPath,
        Core::ReadFileConfig{
            .maxBytes = config.maxManifestBytes,
            .memoryResource = config.scratchMemoryResource,
        });
    if (!manifestBytes)
    {
        return Core::failure(std::move(manifestBytes.error()).withContext(
            "captureCatalogPackageRevision", "readManifest"));
    }
    if (manifestBytes->empty())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "catalog manifest must not be empty");
    }
    auto digest = Core::digestContentHashV1(*manifestBytes);
    if (!digest)
    {
        return Core::failure(std::move(digest.error()).withContext(
            "captureCatalogPackageRevision", "digestManifest"));
    }
    return CatalogPackageRevision{
        .manifestDigest = *digest,
        .manifestBytes = static_cast<Core::u64>(manifestBytes->size()),
    };
}

Core::Result<CatalogPackageChangeProbe>
pollCatalogPackageChange(std::string_view catalogRootUtf8, CatalogPackageRevision baseline,
                         CatalogPackageChangeDetectorConfig config)
{
    if (!baseline.manifestDigest || baseline.manifestBytes == 0U)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "catalog package change baseline is invalid");
    }
    auto candidate = captureCatalogPackageRevision(catalogRootUtf8, config);
    if (!candidate)
    {
        return Core::failure(std::move(candidate.error()).withContext(
            "pollCatalogPackageChange", "captureCandidate"));
    }
    return CatalogPackageChangeProbe{
        .state = *candidate == baseline ? CatalogPackageChangeState::Unchanged
                                        : CatalogPackageChangeState::Changed,
        .candidate = *candidate,
    };
}

} // namespace Tina::Asset
