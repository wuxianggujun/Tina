#include <tina/asset/CatalogPackageChangeDetector.hpp>

#include "CatalogPackagePath.hpp"

#include <tina/asset/AssetErrors.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/io/PackageFile.hpp>
#include <string>
#include <new>
#include <system_error>
#include <utility>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::Result<std::string>
catalogPackagePath(std::string_view catalogRootUtf8, std::string_view packageRelativePath)
{
    auto path = Detail::resolveCatalogPackagePath(catalogRootUtf8, packageRelativePath);
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
try
{
    if (config.maxManifestBytes == 0U ||
        config.maxManifestBytes > AssetFormat::Wire::MaxManifestFileBytes)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "invalid catalog package change detector config");
    }

    auto packagePath = catalogPackagePath(catalogRootUtf8, config.packageRelativePath);
    if (!packagePath)
    {
        return Core::failure(std::move(packagePath.error()).withContext(
            "captureCatalogPackageRevision", "packagePath"));
    }
    auto package = Core::PackageReader::Open(*packagePath, config.package);
    if (!package) return Core::failure(std::move(package.error()));
    auto manifestBytes = package->viewFile(CatalogManifestVirtualPath, config.maxManifestBytes);
    if (!manifestBytes)
    {
        return Core::failure(std::move(manifestBytes.error()).withContext(
            "captureCatalogPackageRevision", "readManifest"));
    }
    if (manifestBytes->bytes().empty())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "catalog manifest must not be empty");
    }
    auto digest = Core::digestContentHashV1(manifestBytes->bytes());
    if (!digest)
    {
        return Core::failure(std::move(digest.error()).withContext(
            "captureCatalogPackageRevision", "digestManifest"));
    }
    return CatalogPackageRevision{
        .manifestDigest = *digest,
        .manifestBytes = static_cast<Core::u64>(manifestBytes->bytes().size()),
    };
}
catch (const std::bad_alloc&)
{
    return Core::failure(AssetErrorCode::AllocationFailed, "catalog revision allocation failed");
}
catch (const std::system_error& error)
{
    return Core::failure(Core::Error{Core::CoreErrorCode::Io, "catalog revision path conversion failed"}
                             .setNativeCode(error.code().value()));
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
