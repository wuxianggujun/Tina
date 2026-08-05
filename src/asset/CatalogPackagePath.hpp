#pragma once

#include "Utf8Path.hpp"

#include <tina/asset/AssetErrors.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/text/Utf8.hpp>

#include <filesystem>
#include <string_view>

namespace Tina::Asset::Detail {

struct CatalogManifestPath final {
    std::filesystem::path fullPath;
    std::filesystem::path directory;
    std::filesystem::path fileName;
};

[[nodiscard]] inline bool hasPathEscapeComponent(const std::filesystem::path& relative) noexcept
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

[[nodiscard]] inline Core::Result<CatalogManifestPath>
resolveCatalogManifestPath(std::string_view catalogRootUtf8,
                           std::string_view manifestRelativePath)
{
    if (catalogRootUtf8.empty() || !Core::countStrictUtf8CodepointsWithoutNul(catalogRootUtf8))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "catalog root path is invalid");
    }
    if (manifestRelativePath.empty() ||
        !Core::countStrictUtf8CodepointsWithoutNul(manifestRelativePath))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "manifest relative path is invalid");
    }

    const auto relative = pathFromUtf8Bytes(manifestRelativePath);
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory() ||
        hasPathEscapeComponent(relative))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "manifest relative path is not safe");
    }

    const auto normalizedRelative = relative.lexically_normal();
    const auto fileName = normalizedRelative.filename();
    if (fileName.empty() || fileName == "." || fileName == "..")
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "manifest relative path does not name a file");
    }

    auto fullPath = pathFromUtf8Bytes(catalogRootUtf8) / normalizedRelative;
    fullPath = fullPath.lexically_normal();
    return CatalogManifestPath{
        .fullPath = fullPath,
        .directory = fullPath.parent_path(),
        .fileName = fileName,
    };
}

} // namespace Tina::Asset::Detail
