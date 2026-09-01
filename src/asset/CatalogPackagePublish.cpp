#include <tina/asset/CatalogPackagePublish.hpp>

#include "core/io/PathUtil.hpp"

#include <tina/asset/AssetErrors.hpp>
#include <tina/core/io/WriteFile.hpp>

#include <filesystem>
#include <string>
#include <utility>

namespace Tina::Asset {
namespace {

[[nodiscard]] bool containsEmbeddedNul(std::string_view text) noexcept
{
    return text.find('\0') != std::string_view::npos;
}

[[nodiscard]] Core::Result<std::string> joinRootRelative(std::string_view catalogRootUtf8,
                                                         std::string_view relativeUtf8)
{
    if (catalogRootUtf8.empty() || containsEmbeddedNul(catalogRootUtf8))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog root path is invalid");
    }
    if (relativeUtf8.empty() || containsEmbeddedNul(relativeUtf8))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "relative path is invalid");
    }
    const auto relative = Core::Detail::pathFromUtf8Bytes(relativeUtf8);
    if (relative.is_absolute() || Core::Detail::pathHasParentComponent(relative))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "relative path is not safe");
    }
    const auto full = Core::Detail::pathFromUtf8Bytes(catalogRootUtf8) / relative;
    const auto generic = full.generic_u8string();
    return std::string(generic.begin(), generic.end());
}

} // namespace

Core::Status publishCatalogPackage(std::string_view catalogRootUtf8, std::string_view manifestRelativePath,
                                   std::span<const std::byte> manifestBytes,
                                   std::span<const CatalogPackageObjectBlob> objects,
                                   CatalogPackagePublishConfig config)
{
    if (manifestBytes.empty())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "manifest bytes must be non-empty");
    }

    if (config.writeObjects)
    {
        for (const auto& object : objects)
        {
            if (object.bytes.empty() || !object.assetId)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "object blob requires id and bytes");
            }
            auto artifact = AssetFormat::makeCookedArtifactPath(object.assetKind, object.assetId);
            if (!artifact)
            {
                return Core::failure(std::move(artifact.error()).withContext("publishCatalogPackage", "artifactPath"));
            }
            auto fullPath = joinRootRelative(catalogRootUtf8, artifact->view());
            if (!fullPath)
            {
                return Core::failure(std::move(fullPath.error()));
            }
            auto writeStatus = Core::writeFile(*fullPath, object.bytes, config.write);
            if (!writeStatus)
            {
                return Core::failure(
                    std::move(writeStatus.error()).withContext("publishCatalogPackage", "writeObject"));
            }
        }
    }

    auto manifestPath = joinRootRelative(catalogRootUtf8, manifestRelativePath);
    if (!manifestPath)
    {
        return Core::failure(std::move(manifestPath.error()));
    }
    // Manifest last so readers never observe a new manifest pointing at missing objects
    // (best-effort; not a full transactional multi-file commit).
    auto manifestStatus = Core::writeFile(*manifestPath, manifestBytes, config.write);
    if (!manifestStatus)
    {
        return Core::failure(
            std::move(manifestStatus.error()).withContext("publishCatalogPackage", "writeManifest"));
    }
    return Core::success();
}

} // namespace Tina::Asset
