#include <tina/asset/CatalogPackagePublish.hpp>

#include "CatalogPackagePath.hpp"
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/AssetErrors.hpp>

#include <new>
#include <string>
#include <system_error>
#include <vector>

namespace Tina::Asset {

Core::Status publishCatalogPackage(std::string_view catalogRootUtf8, std::string_view packageRelativePath,
                                    std::span<const std::byte> manifestBytes,
                                    std::span<const CatalogPackageObjectBlob> objects,
                                    CatalogPackagePublishConfig config)
{
    if (manifestBytes.empty())
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "manifest bytes must be non-empty");
    try
    {
        auto path = Detail::resolveCatalogPackagePath(catalogRootUtf8, packageRelativePath);
        if (!path) return Core::failure(std::move(path.error()));
        std::vector<std::string> paths;
        std::vector<Core::PackageWriteEntry> entries;
        paths.reserve(objects.size());
        entries.reserve(objects.size() + 1);
        entries.push_back({CatalogManifestVirtualPath, manifestBytes});
        for (const auto& object : objects)
        {
            if (!object.assetId || object.bytes.empty())
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "object blob requires id and bytes");
            auto artifact = AssetFormat::makeCookedArtifactPath(object.assetKind, object.assetId);
            if (!artifact) return Core::failure(std::move(artifact.error()));
            paths.emplace_back(artifact->view());
            entries.push_back({paths.back(), object.bytes});
        }
        return Core::writePackageFile(Core::Detail::pathToUtf8(path->fullPath), entries, config.write);
    }
    catch (const std::bad_alloc&) { return Core::failure(AssetErrorCode::AllocationFailed, "catalog publication metadata allocation failed"); }
    catch (const std::system_error& error) {
        return Core::failure(Core::Error{Core::CoreErrorCode::Io, "catalog publication path conversion failed"}.setNativeCode(error.code().value()));
    }
}

} // namespace Tina::Asset
