#include <tina/asset/CatalogPackage.hpp>

#include "CatalogPackagePath.hpp"
#include <tina/asset/AssetErrors.hpp>

#include <new>
#include <system_error>
#include <utility>

namespace Tina::Asset {

Core::Result<CatalogSnapshot> openCatalogPackage(std::string_view catalogRootUtf8, CatalogPackageOpenConfig config)
{
    if (config.manifest.catalog.memoryResource == nullptr || config.manifest.maxFileBytes == 0 ||
        config.manifest.maxFileBytes > AssetFormat::Wire::MaxManifestFileBytes)
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog open requires memory and a manifest byte budget");
    if (config.objectValidation != CatalogObjectValidation::OnDemand &&
        config.objectValidation != CatalogObjectValidation::OnOpen)
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "unknown catalog object validation mode");
    try
    {
        auto path = Detail::resolveCatalogPackagePath(catalogRootUtf8, config.packageRelativePath);
        if (!path) return Core::failure(std::move(path.error()));
        auto reader = Core::PackageReader::Open(Core::Detail::pathToUtf8(path->fullPath), config.package);
        if (!reader) return Core::failure(std::move(reader.error()).withContext("openCatalogPackage", "openPackage"));
        auto manifestBytes = reader->viewFile(CatalogManifestVirtualPath, config.manifest.maxFileBytes);
        if (!manifestBytes) return Core::failure(std::move(manifestBytes.error()).withContext("openCatalogPackage", "manifest"));
        auto manifest = AssetFormat::parseCookedManifestView(manifestBytes->bytes(), config.manifest.manifestLimits);
        if (!manifest) return Core::failure(std::move(manifest.error()).withContext("openCatalogPackage", "parseManifest"));
        auto snapshot = CatalogSnapshot::Create(*manifest, config.manifest.catalog);
        if (!snapshot) return Core::failure(std::move(snapshot.error()));
        snapshot->m_packageReader = std::move(*reader);
        if (config.objectValidation == CatalogObjectValidation::OnOpen)
        {
            if (config.validation.file.memoryResource == nullptr)
                config.validation.file.memoryResource = config.manifest.catalog.memoryResource;
            auto status = validateCatalogPackage(*snapshot, config.validation);
            if (!status) return Core::failure(std::move(status.error()).withContext("openCatalogPackage", "validate"));
        }
        return std::move(*snapshot);
    }
    catch (const std::bad_alloc&) { return Core::failure(AssetErrorCode::AllocationFailed, "catalog package open allocation failed"); }
    catch (const std::system_error& error) {
        return Core::failure(Core::Error{Core::CoreErrorCode::Io, "catalog package path conversion failed"}.setNativeCode(error.code().value()));
    }
}

} // namespace Tina::Asset
