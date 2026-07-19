#include <tina/asset/CatalogFile.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/core/io/ReadFile.hpp>

#include <utility>

namespace Tina::Asset {

Core::Result<CatalogSnapshot> loadCatalogSnapshotFromManifestFile(std::string_view utf8Path,
                                                                  CatalogFileLoadConfig config)
{
    if (config.catalog.memoryResource == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog file load requires catalog memory resource");
    }
    if (config.maxFileBytes == 0 || config.maxFileBytes > AssetFormat::Wire::MaxManifestFileBytes)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid catalog file maxFileBytes");
    }

    auto* fileResource =
        config.fileMemoryResource != nullptr ? config.fileMemoryResource : config.catalog.memoryResource;

    Core::ReadFileConfig readConfig{
        .maxBytes = config.maxFileBytes,
        .memoryResource = fileResource,
    };
    auto fileBytes = Core::readFile(utf8Path, readConfig);
    if (!fileBytes)
    {
        return Core::failure(std::move(fileBytes.error()).withContext("loadCatalogSnapshotFromManifestFile", "readFile"));
    }

    auto manifest = AssetFormat::parseCookedManifestView(*fileBytes, config.manifestLimits);
    if (!manifest)
    {
        return Core::failure(
            std::move(manifest.error()).withContext("loadCatalogSnapshotFromManifestFile", "parseCookedManifestView"));
    }

    auto snapshot = CatalogSnapshot::Create(*manifest, config.catalog);
    if (!snapshot)
    {
        return Core::failure(
            std::move(snapshot.error()).withContext("loadCatalogSnapshotFromManifestFile", "CatalogSnapshot::Create"));
    }
    return std::move(*snapshot);
}

} // namespace Tina::Asset
