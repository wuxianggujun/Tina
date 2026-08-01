#include <tina/asset/CatalogPackage.hpp>

#include "Utf8Path.hpp"

#include <tina/asset/AssetErrors.hpp>

#include <filesystem>
#include <string>
#include <utility>

namespace Tina::Asset {
namespace {

[[nodiscard]] bool containsEmbeddedNul(std::string_view text) noexcept
{
    return text.find('\0') != std::string_view::npos;
}

[[nodiscard]] bool hasPathEscapeComponent(const std::filesystem::path& relative) noexcept
{
    for (const auto& part : relative)
    {
        if (part == "..")
        {
            return true;
        }
    }
    return false;
}

} // namespace

Core::Result<CatalogSnapshot> openCatalogPackage(std::string_view catalogRootUtf8, CatalogPackageOpenConfig config)
{
    if (catalogRootUtf8.empty() || containsEmbeddedNul(catalogRootUtf8))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog root path is invalid");
    }
    if (config.manifestRelativePath.empty() || containsEmbeddedNul(config.manifestRelativePath))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "manifest relative path is invalid");
    }
    if (config.manifest.catalog.memoryResource == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog package open requires memory resource");
    }

    const auto relative = Detail::pathFromUtf8Bytes(config.manifestRelativePath);
    if (relative.is_absolute() || hasPathEscapeComponent(relative))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "manifest relative path is not safe");
    }

    const auto root = Detail::pathFromUtf8Bytes(catalogRootUtf8);
    const auto fullPath = root / relative;
    const auto generic = fullPath.generic_u8string();
    const std::string utf8Path(generic.begin(), generic.end());

    auto snapshot = loadCatalogSnapshotFromManifestFile(utf8Path, config.manifest);
    if (!snapshot)
    {
        return Core::failure(std::move(snapshot.error()).withContext("openCatalogPackage", "loadManifest"));
    }

    if (config.validateOnOpen)
    {
        if (config.validation.verifyContent && config.validation.file.memoryResource == nullptr)
        {
            config.validation.file.memoryResource = config.manifest.catalog.memoryResource;
        }
        auto status = validateCatalogPackageOnDisk(catalogRootUtf8, *snapshot, config.validation);
        if (!status)
        {
            return Core::failure(std::move(status.error()).withContext("openCatalogPackage", "validate"));
        }
    }

    return std::move(*snapshot);
}

} // namespace Tina::Asset
