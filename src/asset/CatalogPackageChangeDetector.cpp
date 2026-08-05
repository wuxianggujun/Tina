#include <tina/asset/CatalogPackageChangeDetector.hpp>

#include "Utf8Path.hpp"

#include <tina/asset/AssetErrors.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/text/Utf8.hpp>

#include <filesystem>
#include <string>
#include <utility>

namespace Tina::Asset {
namespace {

[[nodiscard]] bool hasPathEscapeComponent(const std::filesystem::path& relative) noexcept
{
    for (const auto& component : relative)
    {
        if (component == "..")
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] Core::Result<std::string>
catalogManifestPath(std::string_view catalogRootUtf8, std::string_view manifestRelativePath)
{
    if (catalogRootUtf8.empty() || !Core::countStrictUtf8CodepointsWithoutNul(catalogRootUtf8))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog root path is invalid");
    }
    if (manifestRelativePath.empty() ||
        !Core::countStrictUtf8CodepointsWithoutNul(manifestRelativePath))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "manifest relative path is invalid");
    }

    const auto relative = Detail::pathFromUtf8Bytes(manifestRelativePath);
    if (relative.is_absolute() || hasPathEscapeComponent(relative))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "manifest relative path is not safe");
    }
    const auto fullPath = Detail::pathFromUtf8Bytes(catalogRootUtf8) / relative;
    const auto generic = fullPath.generic_u8string();
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
