#include <tina/editor/EditorProjectWorkspace.hpp>

#include "core/io/PathUtil.hpp"

#include <tina/core/text/Utf8.hpp>
#include <tina/editor/EditorErrors.hpp>

#include <algorithm>
#include <filesystem>
#include <new>
#include <string>
#include <utility>

namespace Tina::Editor {
namespace {

[[nodiscard]] bool validTargetPlatform(AssetFormat::TargetPlatform platform) noexcept
{
    switch (platform)
    {
    case AssetFormat::TargetPlatform::Any:
    case AssetFormat::TargetPlatform::WindowsX64:
    case AssetFormat::TargetPlatform::LinuxX64:
        return true;
    case AssetFormat::TargetPlatform::Invalid:
        return false;
    }
    return false;
}

using Core::Detail::pathFromUtf8Bytes;
using Core::Detail::pathIsSameOrDescendant;

[[nodiscard]] bool pathsEqual(const std::filesystem::path& left,
                              const std::filesystem::path& right) noexcept
{
    return pathIsSameOrDescendant(left, right) &&
           pathIsSameOrDescendant(right, left);
}

[[nodiscard]] Core::Result<std::filesystem::path>
normalizeRootPath(std::string_view rootUtf8, Core::usize byteCapacity)
{
    if (rootUtf8.empty() || rootUtf8.size() > byteCapacity ||
        !Core::isStrictUtf8WithoutNul(rootUtf8))
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor project root must be bounded strict UTF-8 without NUL");
    }

    const auto path = pathFromUtf8Bytes(rootUtf8);
    if (!path.is_absolute())
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor project roots must be absolute paths");
    }

    auto normalized = path.lexically_normal();
    while (!normalized.empty() && normalized != normalized.root_path() &&
           normalized.filename().empty())
    {
        normalized = normalized.parent_path();
    }
    if (normalized.empty() || normalized == normalized.root_path())
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor project roots must identify dedicated directories");
    }
    return normalized;
}

[[nodiscard]] bool isStrictDescendant(const std::filesystem::path& candidate,
                                      const std::filesystem::path& root)
{
    return !pathsEqual(candidate, root) && pathIsSameOrDescendant(candidate, root);
}

[[nodiscard]] Core::Result<std::string>
toCanonicalUtf8(const std::filesystem::path& path, Core::usize byteCapacity)
{
    const auto generic = path.generic_u8string();
    if (generic.size() > byteCapacity)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Normalized Editor project root exceeds path capacity");
    }
    return std::string(generic.begin(), generic.end());
}

} // namespace

Core::Result<EditorProjectWorkspace> EditorProjectWorkspace::Create(
    EditorProjectWorkspaceDesc desc,
    EditorProjectWorkspaceConfig config)
{
    if (config.rootPathByteCapacity == 0U)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor project root path capacity must be non-zero");
    }
    if (!validTargetPlatform(desc.targetPlatform))
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor project target platform is invalid");
    }

    try
    {
        auto projectRoot = normalizeRootPath(desc.projectRootUtf8,
                                             config.rootPathByteCapacity);
        if (!projectRoot)
        {
            return Core::failure(std::move(projectRoot.error())
                                     .withContext("EditorProjectWorkspace::Create", "projectRoot"));
        }
        auto sourceRoot = normalizeRootPath(desc.sourceRootUtf8,
                                            config.rootPathByteCapacity);
        if (!sourceRoot)
        {
            return Core::failure(std::move(sourceRoot.error())
                                     .withContext("EditorProjectWorkspace::Create", "sourceRoot"));
        }
        auto catalogRoot = normalizeRootPath(desc.cookedCatalogRootUtf8,
                                             config.rootPathByteCapacity);
        if (!catalogRoot)
        {
            return Core::failure(std::move(catalogRoot.error())
                                     .withContext("EditorProjectWorkspace::Create", "cookedCatalogRoot"));
        }

        if (!isStrictDescendant(*sourceRoot, *projectRoot) ||
            !isStrictDescendant(*catalogRoot, *projectRoot))
        {
            return Core::failure(
                EditorErrorCode::InvalidConfiguration,
                "Editor source and Cooked Catalog roots must be below the project root");
        }
        if (pathsEqual(*sourceRoot, *catalogRoot) || isStrictDescendant(*sourceRoot, *catalogRoot) ||
            isStrictDescendant(*catalogRoot, *sourceRoot))
        {
            return Core::failure(
                EditorErrorCode::InvalidConfiguration,
                "Editor source and Cooked Catalog roots must not overlap");
        }

        auto projectRootUtf8 = toCanonicalUtf8(*projectRoot, config.rootPathByteCapacity);
        auto sourceRootUtf8 = toCanonicalUtf8(*sourceRoot, config.rootPathByteCapacity);
        auto catalogRootUtf8 = toCanonicalUtf8(*catalogRoot, config.rootPathByteCapacity);
        if (!projectRootUtf8)
        {
            return Core::failure(std::move(projectRootUtf8.error()));
        }
        if (!sourceRootUtf8)
        {
            return Core::failure(std::move(sourceRootUtf8.error()));
        }
        if (!catalogRootUtf8)
        {
            return Core::failure(std::move(catalogRootUtf8.error()));
        }

        return EditorProjectWorkspace{config,
                                      std::move(*projectRootUtf8),
                                      std::move(*sourceRootUtf8),
                                      std::move(*catalogRootUtf8),
                                      desc.targetPlatform};
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Editor project workspace allocation failed");
    }
    catch (const std::filesystem::filesystem_error&)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor project root path normalization failed");
    }
}

EditorProjectWorkspace::EditorProjectWorkspace(
    EditorProjectWorkspaceConfig config,
    std::string projectRootUtf8,
    std::string sourceRootUtf8,
    std::string cookedCatalogRootUtf8,
    AssetFormat::TargetPlatform targetPlatform) noexcept
    : m_config(config), m_projectRootUtf8(std::move(projectRootUtf8)),
      m_sourceRootUtf8(std::move(sourceRootUtf8)),
      m_cookedCatalogRootUtf8(std::move(cookedCatalogRootUtf8)),
      m_targetPlatform(targetPlatform)
{
}

} // namespace Tina::Editor
