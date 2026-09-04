#include <tina/asset/Mesh3DShaderOverride.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset_format/AssetFormat.hpp>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::Result<bool> readOverrideFlag(const CookedAssetFile& file) noexcept
{
    switch (file.header().assetKind)
    {
    case AssetFormat::AssetKind::StaticMesh:
    {
        auto mesh = parseStaticMeshFromCooked(file);
        if (!mesh)
        {
            return Core::failure(std::move(mesh.error()));
        }
        return mesh->hasShaderOverride;
    }
    case AssetFormat::AssetKind::SkinnedMesh:
    {
        auto mesh = parseSkinnedMeshFromCooked(file);
        if (!mesh)
        {
            return Core::failure(std::move(mesh.error()));
        }
        return mesh->hasShaderOverride;
    }
    default:
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "a Mesh3D shader override can only be read from a cooked "
                             "StaticMesh or SkinnedMesh");
    }
}

} // namespace

Core::Result<std::optional<Core::AssetId>>
readMesh3DShaderOverride(const CookedAssetFile& file) noexcept
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    auto declared = readOverrideFlag(file);
    if (!declared)
    {
        return Core::failure(std::move(declared.error()));
    }

    const Core::u32 dependencyCount = file.header().dependencyCount;
    std::optional<Core::AssetId> overrideId{};
    for (Core::u32 index = 0; index < dependencyCount; ++index)
    {
        const auto dependency = file.dependency(index);
        if (!dependency)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "a cooked Mesh3D dependency could not be read");
        }
        if (dependency->expectedKind != AssetFormat::AssetKind::Shader)
        {
            continue;
        }
        // Position is not checked: parseCookedAssetView already requires the whole dependency
        // stream to be strictly AssetId-sorted, so no reader may key off dependency order.
        if (overrideId.has_value())
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "a cooked Mesh3D may name at most one Shader override");
        }
        if (dependency->flags != AssetFormat::DependencyFlags::Required)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "a cooked Mesh3D Shader override dependency must be Required");
        }
        if (!dependency->assetId)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "a cooked Mesh3D Shader override dependency has no AssetId");
        }
        overrideId = dependency->assetId;
    }

    // Both directions are cook defects. A flag without a dependency would leave the mesh
    // asking for a fragment stage nothing can resolve; a dependency without the flag would
    // make the override invisible to every payload reader while still pinning the Shader.
    if (*declared && !overrideId.has_value())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "a cooked Mesh3D declares a shader override but carries no required "
                             "Shader dependency");
    }
    if (!*declared && overrideId.has_value())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "a cooked Mesh3D carries a Shader dependency but its payload declares "
                             "no shader override");
    }
    return overrideId;
}

} // namespace Tina::Asset
